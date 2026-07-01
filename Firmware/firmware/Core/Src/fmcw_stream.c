#include "fmcw_stream.h"

#include "main.h"

#include <stdio.h>

#define FMCW_STREAM_FRAME_COUNT          8U
#define FMCW_STREAM_VERSION              1U
#define FMCW_STREAM_DCMI_CR              (DCMI_CR_PCKPOL | DCMI_CR_VSPOL | DCMI_CR_EDM_1)
#define FMCW_STREAM_HSYNC_DRIVE_PIN      GPIO_PIN_7
#define FMCW_STREAM_HSYNC_DRIVE_PORT     GPIOA
#define FMCW_STREAM_DCMI_HSYNC_PIN       GPIO_PIN_4
#define FMCW_STREAM_DCMI_HSYNC_PORT      GPIOA
#define FMCW_STREAM_PIXCLK_PIN           GPIO_PIN_6
#define FMCW_STREAM_PIXCLK_PORT          GPIOA
#define FMCW_STREAM_VSYNC_PIN            GPIO_PIN_7
#define FMCW_STREAM_VSYNC_PORT           GPIOB
#define FMCW_STREAM_ALIGN_TIMEOUT_MS     2000U

#define FMCW_STREAM_FLAG_DROPPED         0x0001U
#define FMCW_STREAM_FLAG_USB_LATE        0x0002U
#define FMCW_STREAM_FLAG_MUX_TIMEOUT     0x0004U
#define FMCW_STREAM_FLAG_DMA_ERROR       0x0008U
#define FMCW_STREAM_FLAG_DCMI_ERROR      0x0010U
#define FMCW_STREAM_FLAG_DCMI_OVERRUN    0x0020U

typedef struct __attribute__((packed))
{
  uint32_t magic;
  uint16_t version;
  uint16_t header_len;
  uint32_t frame_len;
  uint32_t slope_seq;
  uint32_t triangle_seq;
  uint16_t slope_id;
  uint16_t flags;
  uint32_t sample_rate_hz;
  uint32_t samples_per_slope;
  uint16_t adc_bits;
  uint16_t words_per_slope;
  uint32_t timestamp_us;
  uint32_t muxout_count;
  uint32_t dropped_frames;
  uint32_t dcmi_risr;
  uint32_t dma_lisr;
  uint32_t reserved0;
  uint32_t reserved1;
} FMCWStream_Header;

typedef struct __attribute__((aligned(4)))
{
  FMCWStream_Header header;
  uint32_t payload[FMCW_STREAM_WORDS_PER_SLOPE];
  uint8_t padding[FMCW_STREAM_PADDING_BYTES];
} FMCWStream_Frame;

_Static_assert(sizeof(FMCWStream_Header) == FMCW_STREAM_HEADER_BYTES,
               "FMCW stream header must stay 64 bytes");
_Static_assert(FMCW_STREAM_PADDING_BYTES == 416U,
               "FMCW stream USB padding should align frames to 512 bytes");
_Static_assert(sizeof(FMCWStream_Frame) == FMCW_STREAM_FRAME_BYTES,
               "FMCW stream frame size mismatch");

static FMCWStream_Frame fmcw_stream_frames[FMCW_STREAM_FRAME_COUNT] __attribute__((aligned(4)));
static uint8_t fmcw_stream_dma_frame[2];

static uint8_t fmcw_stream_free_queue[FMCW_STREAM_FRAME_COUNT];
static uint8_t fmcw_stream_ready_queue[FMCW_STREAM_FRAME_COUNT];
static volatile uint8_t fmcw_stream_free_head;
static volatile uint8_t fmcw_stream_free_tail;
static volatile uint8_t fmcw_stream_free_count;
static volatile uint8_t fmcw_stream_ready_head;
static volatile uint8_t fmcw_stream_ready_tail;
static volatile uint8_t fmcw_stream_ready_count;

static volatile uint8_t fmcw_stream_initialized;
static volatile uint8_t fmcw_stream_align_armed;
static volatile uint8_t fmcw_stream_running;
static volatile uint32_t fmcw_stream_slope_seq;
static volatile uint32_t fmcw_stream_muxout_count;
static volatile uint32_t fmcw_stream_dropped_frames;
static volatile uint32_t fmcw_stream_dma_errors;
static volatile uint32_t fmcw_stream_pending_flags;
static volatile uint32_t fmcw_stream_started_tick;
static uint8_t fmcw_stream_first_dma_reported;
static uint8_t fmcw_stream_no_frame_reported;
static uint32_t fmcw_stream_last_report_tick;

static void FMCWStream_ResetQueues(void);
static uint8_t FMCWStream_PopFree(uint8_t *index);
static void FMCWStream_PushFree(uint8_t index);
static uint8_t FMCWStream_PeekReady(uint8_t *index);
static uint8_t FMCWStream_PopReady(uint8_t *index);
static void FMCWStream_PushReady(uint8_t index);
static void FMCWStream_FillHeader(uint8_t frame_index,
                                  uint16_t flags,
                                  uint32_t dcmi_risr,
                                  uint32_t dma_lisr);
static void FMCWStream_ConfigureGain(void);
static void FMCWStream_ConfigureHsyncDrive(void);
static void FMCWStream_PrepareVsyncStart(void);
static void FMCWStream_ReleaseVsyncToDcmi(void);
static void FMCWStream_ShortDelay(void);
static void FMCWStream_ConfigureDcmiPins(void);
static void FMCWStream_ConfigureMuxoutExti(void);
static void FMCWStream_ConfigureDcmi(void);
static void FMCWStream_ConfigureDma(void);
static void FMCWStream_StartCaptureFromMuxout(void);
static uint16_t FMCWStream_ReadAdcBus(void);
static uint32_t FMCWStream_CountPixclkEdges(uint32_t sample_ms);
static void FMCWStream_PrintCapturePins(const char *tag);

void FMCWStream_Init(void)
{
  uint8_t first;
  uint8_t second;

  fmcw_stream_running = 0U;
  fmcw_stream_align_armed = 0U;
  fmcw_stream_slope_seq = 0U;
  fmcw_stream_muxout_count = 0U;
  fmcw_stream_dropped_frames = 0U;
  fmcw_stream_dma_errors = 0U;
  fmcw_stream_pending_flags = 0U;
  fmcw_stream_started_tick = 0U;
  fmcw_stream_first_dma_reported = 0U;
  fmcw_stream_no_frame_reported = 0U;
  fmcw_stream_last_report_tick = HAL_GetTick();

  FMCWStream_ResetQueues();
  (void)FMCWStream_PopFree(&first);
  (void)FMCWStream_PopFree(&second);
  fmcw_stream_dma_frame[0] = first;
  fmcw_stream_dma_frame[1] = second;

  FMCWStream_ConfigureGain();
  FMCWStream_ConfigureHsyncDrive();
  FMCWStream_PrepareVsyncStart();
  FMCWStream_ConfigureDcmiPins();
  printf("STREAM: PIXCLK edges in 2 ms=%lu, ADC bus=0x%03X\r\n",
         (unsigned long)FMCWStream_CountPixclkEdges(2U),
         (unsigned int)FMCWStream_ReadAdcBus());
  FMCWStream_ConfigureMuxoutExti();
  FMCWStream_ConfigureDcmi();
  FMCWStream_ConfigureDma();

  fmcw_stream_initialized = 1U;
  printf("STREAM: initialized, %lu buffers, frame=%lu bytes, sample_rate=%lu Hz\r\n",
         (unsigned long)FMCW_STREAM_FRAME_COUNT,
         (unsigned long)FMCW_STREAM_FRAME_BYTES,
         (unsigned long)FMCW_STREAM_SAMPLE_RATE_HZ);
}

uint8_t FMCWStream_Start(void)
{
  uint32_t start_tick;
  GPIO_PinState last_muxout;

  if (fmcw_stream_initialized == 0U)
  {
    FMCWStream_Init();
  }

  if (fmcw_stream_running != 0U)
  {
    return 1U;
  }

  printf("STREAM: waiting for ADF4158 MUXOUT edge on PE5\r\n");
  fmcw_stream_align_armed = 1U;
  last_muxout = HAL_GPIO_ReadPin(PLL_MUXOUT_GPIO_Port, PLL_MUXOUT_Pin);
  start_tick = HAL_GetTick();
  while (fmcw_stream_running == 0U)
  {
    GPIO_PinState now_muxout = HAL_GPIO_ReadPin(PLL_MUXOUT_GPIO_Port, PLL_MUXOUT_Pin);
    if ((last_muxout == GPIO_PIN_RESET) && (now_muxout == GPIO_PIN_SET))
    {
      printf("STREAM: MUXOUT edge caught by polling fallback\r\n");
      FMCWStream_MuxoutIRQHandler();
    }
    last_muxout = now_muxout;

    if ((HAL_GetTick() - start_tick) > FMCW_STREAM_ALIGN_TIMEOUT_MS)
    {
      fmcw_stream_align_armed = 0U;
      fmcw_stream_pending_flags |= FMCW_STREAM_FLAG_MUX_TIMEOUT;
      printf("STREAM: MUXOUT alignment timeout, starting unaligned debug capture\r\n");
      FMCWStream_StartCaptureFromMuxout();
      break;
    }
  }

  printf("STREAM: capture aligned, muxout_count=%lu\r\n",
         (unsigned long)fmcw_stream_muxout_count);
  FMCWStream_PrintCapturePins("start");
  return 1U;
}

void FMCWStream_Task(void)
{
  uint32_t now = HAL_GetTick();

  if ((fmcw_stream_running != 0U) &&
      (fmcw_stream_slope_seq == 0U) &&
      (fmcw_stream_no_frame_reported == 0U) &&
      ((now - fmcw_stream_started_tick) > 100U))
  {
    fmcw_stream_no_frame_reported = 1U;
    printf("STREAM: no DMA frame yet, SR=0x%08lX RISR=0x%08lX CR=0x%08lX NDTR=%lu LISR=0x%08lX ADC=0x%03X\r\n",
           (unsigned long)DCMI->SR,
           (unsigned long)DCMI->RISR,
           (unsigned long)DCMI->CR,
           (unsigned long)DMA2_Stream1->NDTR,
           (unsigned long)DMA2->LISR,
           (unsigned int)FMCWStream_ReadAdcBus());
    FMCWStream_PrintCapturePins("no-dma");
  }

  if ((fmcw_stream_slope_seq != 0U) && (fmcw_stream_first_dma_reported == 0U))
  {
    fmcw_stream_first_dma_reported = 1U;
    printf("STREAM: first DMA frame ready, slopes=%lu ready=%lu free=%lu\r\n",
           (unsigned long)fmcw_stream_slope_seq,
           (unsigned long)fmcw_stream_ready_count,
           (unsigned long)fmcw_stream_free_count);
  }

  if ((fmcw_stream_running != 0U) &&
      ((now - fmcw_stream_last_report_tick) >= 1000U))
  {
    uint32_t slopes;
    uint32_t muxouts;
    uint32_t dropped;
    uint32_t dma_errors;
    uint8_t ready;
    uint8_t free_count;

    __disable_irq();
    slopes = fmcw_stream_slope_seq;
    muxouts = fmcw_stream_muxout_count;
    dropped = fmcw_stream_dropped_frames;
    dma_errors = fmcw_stream_dma_errors;
    ready = fmcw_stream_ready_count;
    free_count = fmcw_stream_free_count;
    __enable_irq();

    printf("STREAM: slopes=%lu muxout=%lu ready=%lu free=%lu dropped=%lu dma_err=%lu\r\n",
           (unsigned long)slopes,
           (unsigned long)muxouts,
           (unsigned long)ready,
           (unsigned long)free_count,
           (unsigned long)dropped,
           (unsigned long)dma_errors);
    fmcw_stream_last_report_tick = now;
  }
}

uint8_t FMCWStream_DMA_IRQHandler(void)
{
  uint32_t lisr;
  uint32_t dcmi_risr;
  uint16_t flags;
  uint8_t completed_slot;
  uint8_t completed_frame;
  uint8_t next_frame;

  if (fmcw_stream_running == 0U)
  {
    return 0U;
  }

  lisr = DMA2->LISR;
  if ((lisr & (DMA_LISR_TCIF1 | DMA_LISR_TEIF1 | DMA_LISR_DMEIF1 | DMA_LISR_FEIF1)) == 0U)
  {
    return 1U;
  }

  DMA2->LIFCR = DMA_LIFCR_CTCIF1 | DMA_LIFCR_CHTIF1 |
                DMA_LIFCR_CTEIF1 | DMA_LIFCR_CDMEIF1 |
                DMA_LIFCR_CFEIF1;

  if ((lisr & (DMA_LISR_TEIF1 | DMA_LISR_DMEIF1 | DMA_LISR_FEIF1)) != 0U)
  {
    ++fmcw_stream_dma_errors;
    fmcw_stream_pending_flags |= FMCW_STREAM_FLAG_DMA_ERROR;
  }

  if ((lisr & DMA_LISR_TCIF1) == 0U)
  {
    return 1U;
  }

  completed_slot = ((DMA2_Stream1->CR & DMA_SxCR_CT) != 0U) ? 0U : 1U;
  completed_frame = fmcw_stream_dma_frame[completed_slot];
  dcmi_risr = DCMI->RISR;
  DCMI->ICR = DCMI_ICR_FRAME_ISC | DCMI_ICR_OVR_ISC |
              DCMI_ICR_ERR_ISC | DCMI_ICR_VSYNC_ISC |
              DCMI_ICR_LINE_ISC;

  flags = (uint16_t)fmcw_stream_pending_flags;
  fmcw_stream_pending_flags = 0U;
  if ((dcmi_risr & DCMI_RISR_ERR_RIS) != 0U)
  {
    flags |= FMCW_STREAM_FLAG_DCMI_ERROR;
  }
  if ((dcmi_risr & DCMI_RISR_OVR_RIS) != 0U)
  {
    flags |= FMCW_STREAM_FLAG_DCMI_OVERRUN;
  }

  FMCWStream_FillHeader(completed_frame, flags, dcmi_risr, lisr);

  if (FMCWStream_PopFree(&next_frame) != 0U)
  {
    fmcw_stream_dma_frame[completed_slot] = next_frame;
    if (completed_slot == 0U)
    {
      DMA2_Stream1->M0AR = (uint32_t)fmcw_stream_frames[next_frame].payload;
    }
    else
    {
      DMA2_Stream1->M1AR = (uint32_t)fmcw_stream_frames[next_frame].payload;
    }
    FMCWStream_PushReady(completed_frame);
  }
  else
  {
    ++fmcw_stream_dropped_frames;
    fmcw_stream_pending_flags |= (uint32_t)(flags |
                                            FMCW_STREAM_FLAG_DROPPED |
                                            FMCW_STREAM_FLAG_USB_LATE);
  }

  ++fmcw_stream_slope_seq;
  return 1U;
}

void FMCWStream_MuxoutIRQHandler(void)
{
  ++fmcw_stream_muxout_count;
  if ((fmcw_stream_align_armed != 0U) && (fmcw_stream_running == 0U))
  {
    fmcw_stream_align_armed = 0U;
    FMCWStream_StartCaptureFromMuxout();
  }
}

uint8_t FMCWStream_USBNextBuffer(const uint8_t **data, uint32_t *length)
{
  uint8_t index;

  if ((data == NULL) || (length == NULL))
  {
    return 0U;
  }

  __disable_irq();
  if (FMCWStream_PeekReady(&index) == 0U)
  {
    __enable_irq();
    return 0U;
  }
  *data = (const uint8_t *)&fmcw_stream_frames[index];
  *length = FMCW_STREAM_FRAME_BYTES;
  __enable_irq();
  return 1U;
}

void FMCWStream_USBTxDone(const uint8_t *data, uint32_t length)
{
  uint8_t index;

  if ((data == NULL) || (length != FMCW_STREAM_FRAME_BYTES))
  {
    return;
  }

  __disable_irq();
  if ((FMCWStream_PeekReady(&index) != 0U) &&
      (data == (const uint8_t *)&fmcw_stream_frames[index]) &&
      (FMCWStream_PopReady(&index) != 0U))
  {
    FMCWStream_PushFree(index);
  }
  __enable_irq();
}

static void FMCWStream_ResetQueues(void)
{
  fmcw_stream_free_head = 0U;
  fmcw_stream_free_tail = 0U;
  fmcw_stream_free_count = 0U;
  fmcw_stream_ready_head = 0U;
  fmcw_stream_ready_tail = 0U;
  fmcw_stream_ready_count = 0U;

  for (uint8_t i = 0U; i < FMCW_STREAM_FRAME_COUNT; ++i)
  {
    FMCWStream_PushFree(i);
  }
}

static uint8_t FMCWStream_PopFree(uint8_t *index)
{
  if ((index == NULL) || (fmcw_stream_free_count == 0U))
  {
    return 0U;
  }

  *index = fmcw_stream_free_queue[fmcw_stream_free_head];
  fmcw_stream_free_head = (uint8_t)((fmcw_stream_free_head + 1U) % FMCW_STREAM_FRAME_COUNT);
  --fmcw_stream_free_count;
  return 1U;
}

static void FMCWStream_PushFree(uint8_t index)
{
  if (fmcw_stream_free_count >= FMCW_STREAM_FRAME_COUNT)
  {
    return;
  }

  fmcw_stream_free_queue[fmcw_stream_free_tail] = index;
  fmcw_stream_free_tail = (uint8_t)((fmcw_stream_free_tail + 1U) % FMCW_STREAM_FRAME_COUNT);
  ++fmcw_stream_free_count;
}

static uint8_t FMCWStream_PeekReady(uint8_t *index)
{
  if ((index == NULL) || (fmcw_stream_ready_count == 0U))
  {
    return 0U;
  }

  *index = fmcw_stream_ready_queue[fmcw_stream_ready_head];
  return 1U;
}

static uint8_t FMCWStream_PopReady(uint8_t *index)
{
  if ((index == NULL) || (fmcw_stream_ready_count == 0U))
  {
    return 0U;
  }

  *index = fmcw_stream_ready_queue[fmcw_stream_ready_head];
  fmcw_stream_ready_head = (uint8_t)((fmcw_stream_ready_head + 1U) % FMCW_STREAM_FRAME_COUNT);
  --fmcw_stream_ready_count;
  return 1U;
}

static void FMCWStream_PushReady(uint8_t index)
{
  if (fmcw_stream_ready_count >= FMCW_STREAM_FRAME_COUNT)
  {
    return;
  }

  fmcw_stream_ready_queue[fmcw_stream_ready_tail] = index;
  fmcw_stream_ready_tail = (uint8_t)((fmcw_stream_ready_tail + 1U) % FMCW_STREAM_FRAME_COUNT);
  ++fmcw_stream_ready_count;
}

static void FMCWStream_FillHeader(uint8_t frame_index,
                                  uint16_t flags,
                                  uint32_t dcmi_risr,
                                  uint32_t dma_lisr)
{
  FMCWStream_Header *header = &fmcw_stream_frames[frame_index].header;
  uint32_t seq = fmcw_stream_slope_seq;

  header->magic = FMCW_STREAM_MAGIC;
  header->version = FMCW_STREAM_VERSION;
  header->header_len = FMCW_STREAM_HEADER_BYTES;
  header->frame_len = FMCW_STREAM_FRAME_BYTES;
  header->slope_seq = seq;
  header->triangle_seq = seq / 2U;
  header->slope_id = (uint16_t)(seq & 1U);
  header->flags = flags;
  header->sample_rate_hz = FMCW_STREAM_SAMPLE_RATE_HZ;
  header->samples_per_slope = FMCW_STREAM_SAMPLES_PER_SLOPE;
  header->adc_bits = FMCW_STREAM_ADC_BITS;
  header->words_per_slope = FMCW_STREAM_WORDS_PER_SLOPE;
  header->timestamp_us = HAL_GetTick() * 1000UL;
  header->muxout_count = fmcw_stream_muxout_count;
  header->dropped_frames = fmcw_stream_dropped_frames;
  header->dcmi_risr = dcmi_risr;
  header->dma_lisr = dma_lisr;
  header->reserved0 = 0U;
  header->reserved1 = 0U;
}

static void FMCWStream_ConfigureGain(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOD_CLK_ENABLE();

  gpio.Pin = ADC_GAIN_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ADC_GAIN_GPIO_Port, &gpio);
  HAL_GPIO_WritePin(ADC_GAIN_GPIO_Port, ADC_GAIN_Pin, GPIO_PIN_SET);

  printf("STREAM: ADC_GAIN=1x\r\n");
}

static void FMCWStream_ConfigureHsyncDrive(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();

  gpio.Pin = FMCW_STREAM_HSYNC_DRIVE_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(FMCW_STREAM_HSYNC_DRIVE_PORT, &gpio);
  HAL_GPIO_WritePin(FMCW_STREAM_HSYNC_DRIVE_PORT,
                    FMCW_STREAM_HSYNC_DRIVE_PIN,
                    GPIO_PIN_RESET);
}

static void FMCWStream_PrepareVsyncStart(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();

  gpio.Pin = FMCW_STREAM_VSYNC_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(FMCW_STREAM_VSYNC_PORT, &gpio);
  HAL_GPIO_WritePin(FMCW_STREAM_VSYNC_PORT, FMCW_STREAM_VSYNC_PIN, GPIO_PIN_SET);
}

static void FMCWStream_ReleaseVsyncToDcmi(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = FMCW_STREAM_VSYNC_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF13_DCMI;
  HAL_GPIO_Init(FMCW_STREAM_VSYNC_PORT, &gpio);
}

static void FMCWStream_ShortDelay(void)
{
  for (volatile uint32_t i = 0U; i < 2000U; ++i)
  {
  }
}

static void FMCWStream_ConfigureDcmiPins(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF13_DCMI;

  gpio.Pin = GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_9;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  HAL_GPIO_Init(GPIOB, &gpio);

  gpio.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 |
             GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
  HAL_GPIO_Init(GPIOC, &gpio);

  gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_6;
  HAL_GPIO_Init(GPIOD, &gpio);

  printf("STREAM: DCMI GPIO configured, PA7 drives HSYNC, PB7 prepared for VSYNC edge\r\n");
}

static void FMCWStream_ConfigureMuxoutExti(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_SYSCFG_CLK_ENABLE();

  gpio.Pin = PLL_MUXOUT_Pin;
  gpio.Mode = GPIO_MODE_IT_RISING;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PLL_MUXOUT_GPIO_Port, &gpio);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

static void FMCWStream_ConfigureDcmi(void)
{
  __HAL_RCC_DCMI_CLK_ENABLE();
  __HAL_RCC_DCMI_FORCE_RESET();
  __HAL_RCC_DCMI_RELEASE_RESET();

  DCMI->CR = FMCW_STREAM_DCMI_CR;
  DCMI->IER = 0U;
  DCMI->ICR = DCMI_ICR_FRAME_ISC | DCMI_ICR_OVR_ISC |
              DCMI_ICR_ERR_ISC | DCMI_ICR_VSYNC_ISC |
              DCMI_ICR_LINE_ISC;

  printf("STREAM: DCMI ready, 12-bit falling-edge capture\r\n");
}

static void FMCWStream_ConfigureDma(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();

  DMA2_Stream1->CR &= ~DMA_SxCR_EN;
  while ((DMA2_Stream1->CR & DMA_SxCR_EN) != 0U)
  {
  }

  DMA2->LIFCR = DMA_LIFCR_CTCIF1 | DMA_LIFCR_CHTIF1 |
                DMA_LIFCR_CTEIF1 | DMA_LIFCR_CDMEIF1 |
                DMA_LIFCR_CFEIF1;

  DMA2_Stream1->PAR = (uint32_t)&DCMI->DR;
  DMA2_Stream1->M0AR = (uint32_t)fmcw_stream_frames[fmcw_stream_dma_frame[0]].payload;
  DMA2_Stream1->M1AR = (uint32_t)fmcw_stream_frames[fmcw_stream_dma_frame[1]].payload;
  DMA2_Stream1->NDTR = FMCW_STREAM_WORDS_PER_SLOPE;
  DMA2_Stream1->FCR = DMA_SxFCR_DMDIS |
                      (3U << DMA_SxFCR_FTH_Pos) |
                      DMA_SxFCR_FEIE;
  DMA2_Stream1->CR = (1U << DMA_SxCR_CHSEL_Pos) |
                     DMA_SxCR_MINC |
                     (2U << DMA_SxCR_PSIZE_Pos) |
                     (2U << DMA_SxCR_MSIZE_Pos) |
                     (3U << DMA_SxCR_PL_Pos) |
                     DMA_SxCR_DBM |
                     DMA_SxCR_TCIE |
                     DMA_SxCR_TEIE |
                     DMA_SxCR_DMEIE;

  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 0U, 0U);
  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);

  printf("STREAM: DMA2 Stream1 DBM armed for %lu words per slope\r\n",
         (unsigned long)FMCW_STREAM_WORDS_PER_SLOPE);
}

static void FMCWStream_StartCaptureFromMuxout(void)
{
  DMA2->LIFCR = DMA_LIFCR_CTCIF1 | DMA_LIFCR_CHTIF1 |
                DMA_LIFCR_CTEIF1 | DMA_LIFCR_CDMEIF1 |
                DMA_LIFCR_CFEIF1;
  DCMI->ICR = DCMI_ICR_FRAME_ISC | DCMI_ICR_OVR_ISC |
              DCMI_ICR_ERR_ISC | DCMI_ICR_VSYNC_ISC |
              DCMI_ICR_LINE_ISC;

  DMA2_Stream1->CR |= DMA_SxCR_EN;
  DCMI->CR = FMCW_STREAM_DCMI_CR | DCMI_CR_ENABLE | DCMI_CR_CAPTURE;

  FMCWStream_ReleaseVsyncToDcmi();
  FMCWStream_ShortDelay();
  FMCW_STREAM_HSYNC_DRIVE_PORT->BSRR = FMCW_STREAM_HSYNC_DRIVE_PIN;
  FMCWStream_ShortDelay();

  fmcw_stream_started_tick = HAL_GetTick();
  fmcw_stream_running = 1U;
}

static uint16_t FMCWStream_ReadAdcBus(void)
{
  uint16_t sample = 0U;

  sample |= (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9)  == GPIO_PIN_SET) ? (1U << 0U)  : 0U;
  sample |= (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_7)  == GPIO_PIN_SET) ? (1U << 1U)  : 0U;
  sample |= (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_8)  == GPIO_PIN_SET) ? (1U << 2U)  : 0U;
  sample |= (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_9)  == GPIO_PIN_SET) ? (1U << 3U)  : 0U;
  sample |= (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_11) == GPIO_PIN_SET) ? (1U << 4U)  : 0U;
  sample |= (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_3)  == GPIO_PIN_SET) ? (1U << 5U)  : 0U;
  sample |= (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8)  == GPIO_PIN_SET) ? (1U << 6U)  : 0U;
  sample |= (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9)  == GPIO_PIN_SET) ? (1U << 7U)  : 0U;
  sample |= (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_10) == GPIO_PIN_SET) ? (1U << 8U)  : 0U;
  sample |= (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_SET) ? (1U << 9U)  : 0U;
  sample |= (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_6)  == GPIO_PIN_SET) ? (1U << 10U) : 0U;
  sample |= (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_2)  == GPIO_PIN_SET) ? (1U << 11U) : 0U;

  return sample;
}

static uint32_t FMCWStream_CountPixclkEdges(uint32_t sample_ms)
{
  uint32_t count = 0U;
  GPIO_PinState last = HAL_GPIO_ReadPin(FMCW_STREAM_PIXCLK_PORT, FMCW_STREAM_PIXCLK_PIN);
  uint32_t start = HAL_GetTick();

  while ((HAL_GetTick() - start) < sample_ms)
  {
    GPIO_PinState now = HAL_GPIO_ReadPin(FMCW_STREAM_PIXCLK_PORT, FMCW_STREAM_PIXCLK_PIN);
    if (now != last)
    {
      ++count;
      last = now;
    }
  }

  return count;
}

static void FMCWStream_PrintCapturePins(const char *tag)
{
  printf("STREAM: %s pins PA7=%lu PA4(HSYNC)=%lu PB7(VSYNC)=%lu SR=0x%08lX NDTR=%lu\r\n",
         tag,
         (unsigned long)HAL_GPIO_ReadPin(FMCW_STREAM_HSYNC_DRIVE_PORT,
                                         FMCW_STREAM_HSYNC_DRIVE_PIN),
         (unsigned long)HAL_GPIO_ReadPin(FMCW_STREAM_DCMI_HSYNC_PORT,
                                         FMCW_STREAM_DCMI_HSYNC_PIN),
         (unsigned long)HAL_GPIO_ReadPin(FMCW_STREAM_VSYNC_PORT,
                                         FMCW_STREAM_VSYNC_PIN),
         (unsigned long)DCMI->SR,
         (unsigned long)DMA2_Stream1->NDTR);
}
