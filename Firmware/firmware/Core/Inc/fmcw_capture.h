#ifndef FMCW_CAPTURE_H
#define FMCW_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "fmcw_protocol.h"

#define FMCW_ADC_NUM_BUFFERS       4u
#define FMCW_DMA_WORDS_PER_SLOPE   5000u

extern uint8_t g_fmcw_adc_pool[FMCW_ADC_NUM_BUFFERS][FMCW_FRAME_BYTES];

void fmcw_capture_state_init(void);
void fmcw_capture_arm_dma(void);
void fmcw_capture_start_aligned(void);
void fmcw_capture_note_muxout(void);
void fmcw_capture_note_dma_error(uint16_t flags);
void fmcw_capture_note_dcmi_error(uint16_t flags);
void fmcw_capture_watchdog_poll(void);

void fmcw_capture_dma_tc_isr(void);
void fmcw_capture_dma_error_isr(uint32_t dma_lisr);

bool fmcw_capture_usb_dequeue(uint8_t *slot);
void fmcw_capture_usb_requeue_front(uint8_t slot);
void fmcw_capture_usb_release(uint8_t slot);
uint8_t fmcw_capture_usb_queue_depth(void);

uint8_t *fmcw_capture_slot_base(uint8_t slot);
uint8_t *fmcw_capture_slot_payload(uint8_t slot);

#endif

