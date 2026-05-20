#include "fmcw_capture.h"

#include <string.h>

#include "fmcw_queue.h"
#include "fmcw_time.h"
#include "stm32f4xx.h"

__attribute__((section(".dma_pool"), aligned(32)))
uint8_t g_fmcw_adc_pool[FMCW_ADC_NUM_BUFFERS][FMCW_FRAME_BYTES];

static fmcw_queue_t s_free_q;
static fmcw_queue_t s_usb_tx_q;
static volatile uint8_t s_active_slot[2];
static volatile uint32_t s_next_slope_seq;
static volatile uint32_t s_muxout_count;
static volatile uint64_t s_last_muxout_us;
static volatile uint16_t s_flags_pending;
static volatile bool s_aligned;

/* Disables interrupts while queue state shared with ISRs is updated. */
static uint32_t irq_save(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

/* Restores the interrupt mask captured by irq_save(). */
static void irq_restore(uint32_t primask)
{
  __set_PRIMASK(primask);
}

/* Moves accumulated warning/error bits into the next emitted frame header. */
static uint16_t take_pending_flags(void)
{
  uint32_t primask = irq_save();
  uint16_t flags = s_flags_pending;
  s_flags_pending = 0u;
  irq_restore(primask);
  return flags;
}

/* Adds warning/error bits from any ISR or polling context. */
static void add_pending_flags(uint16_t flags)
{
  uint32_t primask = irq_save();
  s_flags_pending |= flags;
  irq_restore(primask);
}

/* Clears all capture state and seeds the four-buffer ring: two buffers attached
 * to DMA M0/M1 and two buffers waiting in the free queue.
 */
void fmcw_capture_state_init(void)
{
  memset(g_fmcw_adc_pool, 0, sizeof(g_fmcw_adc_pool));
  fmcw_queue_reset(&s_free_q);
  fmcw_queue_reset(&s_usb_tx_q);

  s_active_slot[0] = 0u;
  s_active_slot[1] = 1u;
  (void)fmcw_queue_push(&s_free_q, 2u);
  (void)fmcw_queue_push(&s_free_q, 3u);

  s_next_slope_seq = 0u;
  s_muxout_count = 0u;
  s_last_muxout_us = 0u;
  s_flags_pending = 0u;
  s_aligned = false;
}

/* Reattaches logical slots 0 and 1 to the DMA double-buffer registers. */
void fmcw_capture_arm_dma(void)
{
  s_active_slot[0] = 0u;
  s_active_slot[1] = 1u;
}

/* Marks the stream aligned when TIM9 has started DCMI after the first MUXOUT
 * edge plus the ADC pipeline offset.
 */
void fmcw_capture_start_aligned(void)
{
  s_aligned = true;
  s_next_slope_seq = 0u;
  s_last_muxout_us = fmcw_time_us();
}

/* Records every later MUXOUT edge as a slope heartbeat and watchdog reference. */
void fmcw_capture_note_muxout(void)
{
  s_muxout_count++;
  s_last_muxout_us = fmcw_time_us();
}

/* Records DMA faults; alignment is considered suspect until the host discards
 * affected frames or the board is restarted.
 */
void fmcw_capture_note_dma_error(uint16_t flags)
{
  add_pending_flags((uint16_t)(flags | FMCW_FLAG_ALIGN_LOST));
}

/* Records DCMI overrun/synchronization faults for the next frame header. */
void fmcw_capture_note_dcmi_error(uint16_t flags)
{
  add_pending_flags(flags);
}

/* Detects a missing MUXOUT heartbeat after alignment. This catches a stalled
 * ADF4158 ramp without putting hard realtime work in the main loop.
 */
void fmcw_capture_watchdog_poll(void)
{
  if (!s_aligned) {
    return;
  }

  uint64_t now = fmcw_time_us();
  if ((now - s_last_muxout_us) > 1500u) {
    add_pending_flags(FMCW_FLAG_MUX_MISSING);
    s_last_muxout_us = now;
  }
}

/* Handles one completed slope buffer: fills its USB header, queues it for USB,
 * and installs a free slot into the just-finished DMA memory register.
 */
void fmcw_capture_dma_tc_isr(void)
{
  uint32_t ct = (DMA2_Stream1->CR & DMA_SxCR_CT) ? 1u : 0u;
  uint32_t completed_register = ct ^ 1u;
  uint8_t completed_slot = s_active_slot[completed_register];
  uint8_t next_slot = FMCW_SLOT_INVALID;
  uint16_t flags = take_pending_flags();

  if (fmcw_capture_usb_queue_depth() >= 2u) {
    flags |= FMCW_FLAG_USB_LATE;
  }

  bool have_free = fmcw_queue_pop(&s_free_q, &next_slot);
  if (!have_free) {
    add_pending_flags(FMCW_FLAG_DROPPED);
    next_slot = completed_slot;
  } else {
    fmcw_frame_meta_t meta = {
      .slope_seq = s_next_slope_seq,
      .muxout_count = s_muxout_count,
      .slope_id = (uint16_t)(s_next_slope_seq & 1u),
      .flags = flags,
      .timestamp_us = fmcw_time_us(),
    };

    fmcw_frame_fill(fmcw_capture_slot_base(completed_slot), &meta);
    if (!fmcw_queue_push(&s_usb_tx_q, completed_slot)) {
      add_pending_flags(FMCW_FLAG_DROPPED);
      next_slot = completed_slot;
    }
  }

  s_active_slot[completed_register] = next_slot;
  if (completed_register == 0u) {
    DMA2_Stream1->M0AR = (uint32_t)fmcw_capture_slot_payload(next_slot);
  } else {
    DMA2_Stream1->M1AR = (uint32_t)fmcw_capture_slot_payload(next_slot);
  }

  s_next_slope_seq++;
}

/* Converts DMA error status into protocol flags for the host. */
void fmcw_capture_dma_error_isr(uint32_t dma_lisr)
{
  (void)dma_lisr;
  fmcw_capture_note_dma_error(FMCW_FLAG_DCMI_OVR);
}

/* Gives the USB layer the next completed frame slot, if one is ready. */
bool fmcw_capture_usb_dequeue(uint8_t *slot)
{
  uint32_t primask = irq_save();
  bool ok = fmcw_queue_pop(&s_usb_tx_q, slot);
  irq_restore(primask);
  return ok;
}

/* Returns a slot to the front of the USB queue when the USB endpoint was busy. */
void fmcw_capture_usb_requeue_front(uint8_t slot)
{
  uint32_t primask = irq_save();
  if (s_usb_tx_q.count < FMCW_QUEUE_CAPACITY) {
    s_usb_tx_q.head = (uint8_t)((s_usb_tx_q.head + FMCW_QUEUE_CAPACITY - 1u) % FMCW_QUEUE_CAPACITY);
    s_usb_tx_q.item[s_usb_tx_q.head] = slot;
    s_usb_tx_q.count++;
  } else {
    s_flags_pending |= FMCW_FLAG_DROPPED;
  }
  irq_restore(primask);
}

/* Releases a transmitted frame slot back to the DMA free pool. */
void fmcw_capture_usb_release(uint8_t slot)
{
  uint32_t primask = irq_save();
  if (!fmcw_queue_push(&s_free_q, slot)) {
    s_flags_pending |= FMCW_FLAG_DROPPED;
  }
  irq_restore(primask);
}

/* Reports how many completed frames are waiting for USB transmission. */
uint8_t fmcw_capture_usb_queue_depth(void)
{
  uint32_t primask = irq_save();
  uint8_t count = fmcw_queue_count(&s_usb_tx_q);
  irq_restore(primask);
  return count;
}

/* Returns the start of a slot, including the 64-byte frame header. */
uint8_t *fmcw_capture_slot_base(uint8_t slot)
{
  return g_fmcw_adc_pool[slot];
}

/* Returns the payload address where DMA writes packed ADC samples. */
uint8_t *fmcw_capture_slot_payload(uint8_t slot)
{
  return &g_fmcw_adc_pool[slot][FMCW_HEADER_BYTES];
}
