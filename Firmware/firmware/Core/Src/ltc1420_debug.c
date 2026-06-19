#include "ltc1420_debug.h"

#include "main.h"

#include <stdio.h>

#define LTC1420_DEBUG_WORD_COUNT     2048U
#define LTC1420_DEBUG_TIMEOUT_MS     50U
#define LTC1420_PIXCLK_MEASURE_MS    5U
#define LTC1420_HSYNC_DRIVE_PIN      GPIO_PIN_7
#define LTC1420_HSYNC_DRIVE_PORT     GPIOA
#define LTC1420_VSYNC_PIN            GPIO_PIN_7
#define LTC1420_VSYNC_PORT           GPIOB
#define LTC1420_DCMI_BASE_CR         (DCMI_CR_PCKPOL | DCMI_CR_VSPOL | DCMI_CR_EDM_1)

static uint32_t ltc1420_debug_buffer[LTC1420_DEBUG_WORD_COUNT];

static void LTC1420_DebugConfigureDcmiPins(void);
static void LTC1420_DebugConfigureGain(void);
static void LTC1420_DebugConfigureHsyncDrive(void);
static void LTC1420_DebugStartHsyncLine(void);
static void LTC1420_DebugPrepareVsyncStart(void);
static void LTC1420_DebugReleaseVsyncToDcmi(void);
static void LTC1420_DebugShortDelay(void);
static uint32_t LTC1420_DebugMeasurePixclk(void);
static void LTC1420_DebugInitDcmi(void);
static void LTC1420_DebugInitDma(void);
static uint32_t LTC1420_DebugRunCapture(void);
static void LTC1420_DebugPrintSummary(void);
static void LTC1420_DebugPrintRegisters(const char *label);
static void LTC1420_DebugPrintGpioSnapshots(void);
static uint16_t LTC1420_DebugReadGpioSample(void);
static void LTC1420_DebugFormatBits12(uint16_t code, char bits[13]);
static int16_t LTC1420_DebugSignExtend12(uint16_t code);

void LTC1420_DebugCaptureOnce(void)
{
  printf("LTC1420 DEBUG: starting one-shot DCMI capture\r\n");

  LTC1420_DebugConfigureGain();
  LTC1420_DebugConfigureHsyncDrive();
  LTC1420_DebugPrepareVsyncStart();
  (void)LTC1420_DebugMeasurePixclk();
  LTC1420_DebugConfigureDcmiPins();
  LTC1420_DebugInitDcmi();
  LTC1420_DebugInitDma();

  for (uint32_t i = 0U; i < LTC1420_DEBUG_WORD_COUNT; ++i)
  {
    ltc1420_debug_buffer[i] = 0U;
  }

  (void)LTC1420_DebugRunCapture();
  LTC1420_DebugPrintSummary();
  LTC1420_DebugPrintGpioSnapshots();
}

static void LTC1420_DebugConfigureDcmiPins(void)
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

  printf("LTC1420 DEBUG: DCMI data/PIXCLK/HSYNC pins configured, PB7 VSYNC held high\r\n");
}

static void LTC1420_DebugConfigureGain(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOD_CLK_ENABLE();

  gpio.Pin = ADC_GAIN_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ADC_GAIN_GPIO_Port, &gpio);

  HAL_GPIO_WritePin(ADC_GAIN_GPIO_Port, ADC_GAIN_Pin, GPIO_PIN_SET);
  printf("LTC1420 DEBUG: ADC_GAIN PD12=1 (1x gain)\r\n");
}

static void LTC1420_DebugConfigureHsyncDrive(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();

  gpio.Pin = LTC1420_HSYNC_DRIVE_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(LTC1420_HSYNC_DRIVE_PORT, &gpio);

  HAL_GPIO_WritePin(LTC1420_HSYNC_DRIVE_PORT, LTC1420_HSYNC_DRIVE_PIN, GPIO_PIN_RESET);
  printf("LTC1420 DEBUG: PA7 HSYNC drive=0, will pulse high after frame start\r\n");
}

static void LTC1420_DebugStartHsyncLine(void)
{
  HAL_GPIO_WritePin(LTC1420_HSYNC_DRIVE_PORT, LTC1420_HSYNC_DRIVE_PIN, GPIO_PIN_SET);
  printf("LTC1420 DEBUG: PA7 HSYNC drive=1, line valid\r\n");
}

static void LTC1420_DebugPrepareVsyncStart(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();

  gpio.Pin = LTC1420_VSYNC_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LTC1420_VSYNC_PORT, &gpio);

  HAL_GPIO_WritePin(LTC1420_VSYNC_PORT, LTC1420_VSYNC_PIN, GPIO_PIN_SET);
  printf("LTC1420 DEBUG: PB7 VSYNC precharged high, 40k pulldown will create frame-start edge\r\n");
}

static void LTC1420_DebugReleaseVsyncToDcmi(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = LTC1420_VSYNC_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF13_DCMI;
  HAL_GPIO_Init(LTC1420_VSYNC_PORT, &gpio);

  printf("LTC1420 DEBUG: PB7 released to DCMI_VSYNC AF13, pulldown should pull it low\r\n");
}

static void LTC1420_DebugShortDelay(void)
{
  for (volatile uint32_t i = 0U; i < 2000U; ++i)
  {
  }
}

static uint32_t LTC1420_DebugMeasurePixclk(void)
{
  GPIO_InitTypeDef gpio = {0};
  uint32_t edges;
  uint32_t hz;

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();
  __HAL_RCC_TIM3_FORCE_RESET();
  __HAL_RCC_TIM3_RELEASE_RESET();

  gpio.Pin = GPIO_PIN_6;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF2_TIM3;
  HAL_GPIO_Init(GPIOA, &gpio);

  TIM3->CR1 = 0U;
  TIM3->CR2 = 0U;
  TIM3->DIER = 0U;
  TIM3->PSC = 0U;
  TIM3->ARR = 0xFFFFU;
  TIM3->CCMR1 = TIM_CCMR1_CC1S_0;
  TIM3->CCER = TIM_CCER_CC1E;
  TIM3->SMCR = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1 | TIM_SMCR_SMS_2 |
               TIM_SMCR_TS_0 | TIM_SMCR_TS_2;
  TIM3->EGR = TIM_EGR_UG;
  TIM3->CNT = 0U;
  TIM3->SR = 0U;
  TIM3->CR1 = TIM_CR1_CEN;

  HAL_Delay(LTC1420_PIXCLK_MEASURE_MS);

  edges = TIM3->CNT;
  TIM3->CR1 = 0U;
  TIM3->SMCR = 0U;
  TIM3->CCER = 0U;

  hz = edges * (1000U / LTC1420_PIXCLK_MEASURE_MS);
  printf("LTC1420 DEBUG: PA6 PIXCLK edges=%lu in %lu ms, approx=%lu Hz\r\n",
         (unsigned long)edges,
         (unsigned long)LTC1420_PIXCLK_MEASURE_MS,
         (unsigned long)hz);

  if (edges == 0U)
  {
    printf("LTC1420 DEBUG: PA6 PIXCLK is not toggling at the MCU pin\r\n");
  }

  return edges;
}

static void LTC1420_DebugInitDcmi(void)
{
  __HAL_RCC_DCMI_CLK_ENABLE();
  __HAL_RCC_DCMI_FORCE_RESET();
  __HAL_RCC_DCMI_RELEASE_RESET();

  /* VSPOL high means high is vertical blanking; HSPOL low means low is line blanking. */
  DCMI->CR = LTC1420_DCMI_BASE_CR;
  DCMI->IER = 0U;
  DCMI->ICR = DCMI_ICR_FRAME_ISC | DCMI_ICR_OVR_ISC |
              DCMI_ICR_ERR_ISC | DCMI_ICR_VSYNC_ISC |
              DCMI_ICR_LINE_ISC;

  printf("LTC1420 DEBUG: DCMI configured, 12-bit falling-edge capture\r\n");
}

static void LTC1420_DebugInitDma(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();

  DMA2_Stream1->CR &= ~DMA_SxCR_EN;
  while ((DMA2_Stream1->CR & DMA_SxCR_EN) != 0U)
  {
  }

  DMA2->LIFCR = DMA_LIFCR_CFEIF1 | DMA_LIFCR_CDMEIF1 | DMA_LIFCR_CTEIF1 |
                DMA_LIFCR_CHTIF1 | DMA_LIFCR_CTCIF1;

  DMA2_Stream1->PAR = (uint32_t)&DCMI->DR;
  DMA2_Stream1->M0AR = (uint32_t)ltc1420_debug_buffer;
  DMA2_Stream1->NDTR = LTC1420_DEBUG_WORD_COUNT;
  DMA2_Stream1->FCR = DMA_SxFCR_DMDIS |
                      (3U << DMA_SxFCR_FTH_Pos) |
                      DMA_SxFCR_FEIE;
  DMA2_Stream1->CR = (1U << DMA_SxCR_CHSEL_Pos) |
                     DMA_SxCR_MINC |
                     (2U << DMA_SxCR_PSIZE_Pos) |
                     (2U << DMA_SxCR_MSIZE_Pos) |
                     (3U << DMA_SxCR_PL_Pos) |
                     DMA_SxCR_TEIE |
                     DMA_SxCR_DMEIE;

  printf("LTC1420 DEBUG: DMA2 Stream1 armed for %lu words\r\n",
         (unsigned long)LTC1420_DEBUG_WORD_COUNT);
}

static uint32_t LTC1420_DebugRunCapture(void)
{
  uint32_t tickstart;

  LTC1420_DebugPrintRegisters("before");

  DMA2_Stream1->CR |= DMA_SxCR_EN;
  DCMI->CR = LTC1420_DCMI_BASE_CR | DCMI_CR_ENABLE | DCMI_CR_CAPTURE;

  printf("LTC1420 DEBUG: DCMI capture armed, generating VSYNC then HSYNC start\r\n");
  LTC1420_DebugReleaseVsyncToDcmi();
  LTC1420_DebugShortDelay();
  LTC1420_DebugStartHsyncLine();
  LTC1420_DebugShortDelay();
  LTC1420_DebugPrintRegisters("after-sync-start");

  tickstart = HAL_GetTick();
  while ((DMA2->LISR & DMA_LISR_TCIF1) == 0U)
  {
    if ((DMA2->LISR & (DMA_LISR_TEIF1 | DMA_LISR_DMEIF1 | DMA_LISR_FEIF1)) != 0U)
    {
      printf("LTC1420 DEBUG: DMA error while capturing\r\n");
      break;
    }

    if ((HAL_GetTick() - tickstart) > LTC1420_DEBUG_TIMEOUT_MS)
    {
      printf("LTC1420 DEBUG: timeout waiting for DMA complete\r\n");
      LTC1420_DebugPrintRegisters("timeout-active");
      break;
    }
  }

  DCMI->CR &= ~DCMI_CR_CAPTURE;
  DMA2_Stream1->CR &= ~DMA_SxCR_EN;
  while ((DMA2_Stream1->CR & DMA_SxCR_EN) != 0U)
  {
  }

  LTC1420_DebugPrintRegisters("after");

  return ((DMA2->LISR & DMA_LISR_TCIF1) != 0U) ? 1U : 0U;
}

static void LTC1420_DebugPrintSummary(void)
{
  uint16_t low_min = 0xFFFFU;
  uint16_t low_max = 0U;
  uint16_t high_min = 0xFFFFU;
  uint16_t high_max = 0U;
  uint32_t changed_words = 0U;
  uint32_t nonzero_words = 0U;

  for (uint32_t i = 0U; i < LTC1420_DEBUG_WORD_COUNT; ++i)
  {
    const uint32_t word = ltc1420_debug_buffer[i];
    const uint16_t low = (uint16_t)(word & 0xFFFFU);
    const uint16_t high = (uint16_t)(word >> 16);

    if (i > 0U && word != ltc1420_debug_buffer[i - 1U])
    {
      ++changed_words;
    }
    if (word != 0U)
    {
      ++nonzero_words;
    }

    if (low < low_min)
    {
      low_min = low;
    }
    if (low > low_max)
    {
      low_max = low;
    }
    if (high < high_min)
    {
      high_min = high;
    }
    if (high > high_max)
    {
      high_max = high;
    }
  }

  printf("LTC1420 DEBUG: words=%lu changed=%lu nonzero=%lu\r\n",
         (unsigned long)LTC1420_DEBUG_WORD_COUNT,
         (unsigned long)changed_words,
         (unsigned long)nonzero_words);
  printf("LTC1420 DEBUG: low sample min=0x%03X max=0x%03X, high sample min=0x%03X max=0x%03X\r\n",
         low_min & 0x0FFFU,
         low_max & 0x0FFFU,
         high_min & 0x0FFFU,
         high_max & 0x0FFFU);

  printf("LTC1420 DEBUG: first decoded samples\r\n");
  for (uint32_t i = 0U; i < 8U; ++i)
  {
    const uint32_t word = ltc1420_debug_buffer[i];
    const uint16_t sample_a = (uint16_t)(word & 0x0FFFU);
    const uint16_t sample_b = (uint16_t)((word >> 16) & 0x0FFFU);
    const int16_t signed_a = LTC1420_DebugSignExtend12(sample_a);
    const int16_t signed_b = LTC1420_DebugSignExtend12(sample_b);
    char bits_a[13];
    char bits_b[13];

    LTC1420_DebugFormatBits12(sample_a, bits_a);
    LTC1420_DebugFormatBits12(sample_b, bits_b);

    printf("  word[%lu]=0x%08lX  A=0x%03X b%s %+d mV  B=0x%03X b%s %+d mV\r\n",
           (unsigned long)i,
           (unsigned long)word,
           sample_a,
           bits_a,
           signed_a,
           sample_b,
           bits_b,
           signed_b);
  }
}

static void LTC1420_DebugPrintRegisters(const char *label)
{
  printf("LTC1420 DEBUG: %s SR=0x%08lX RISR=0x%08lX CR=0x%08lX NDTR=%lu LISR=0x%08lX PA4=%lu PB7=%lu\r\n",
         label,
         (unsigned long)DCMI->SR,
         (unsigned long)DCMI->RISR,
         (unsigned long)DCMI->CR,
         (unsigned long)DMA2_Stream1->NDTR,
         (unsigned long)DMA2->LISR,
         (unsigned long)HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4),
         (unsigned long)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7));
}

static void LTC1420_DebugPrintGpioSnapshots(void)
{
  printf("LTC1420 DEBUG: direct GPIO ADC-bus snapshots\r\n");
  for (uint32_t i = 0U; i < 8U; ++i)
  {
    const uint16_t sample = LTC1420_DebugReadGpioSample();
    const int16_t signed_sample = LTC1420_DebugSignExtend12(sample);
    char bits[13];

    LTC1420_DebugFormatBits12(sample, bits);
    printf("  gpio[%lu]=0x%03X b%s %+d mV\r\n",
           (unsigned long)i,
           sample,
           bits,
           signed_sample);
    HAL_Delay(1U);
  }
}

static uint16_t LTC1420_DebugReadGpioSample(void)
{
  uint16_t sample = 0U;

  if ((GPIOA->IDR & GPIO_PIN_9) != 0U)  { sample |= (1U << 0); }
  if ((GPIOC->IDR & GPIO_PIN_7) != 0U)  { sample |= (1U << 1); }
  if ((GPIOC->IDR & GPIO_PIN_8) != 0U)  { sample |= (1U << 2); }
  if ((GPIOC->IDR & GPIO_PIN_9) != 0U)  { sample |= (1U << 3); }
  if ((GPIOC->IDR & GPIO_PIN_11) != 0U) { sample |= (1U << 4); }
  if ((GPIOD->IDR & GPIO_PIN_3) != 0U)  { sample |= (1U << 5); }
  if ((GPIOB->IDR & GPIO_PIN_8) != 0U)  { sample |= (1U << 6); }
  if ((GPIOB->IDR & GPIO_PIN_9) != 0U)  { sample |= (1U << 7); }
  if ((GPIOC->IDR & GPIO_PIN_10) != 0U) { sample |= (1U << 8); }
  if ((GPIOC->IDR & GPIO_PIN_12) != 0U) { sample |= (1U << 9); }
  if ((GPIOD->IDR & GPIO_PIN_6) != 0U)  { sample |= (1U << 10); }
  if ((GPIOD->IDR & GPIO_PIN_2) != 0U)  { sample |= (1U << 11); }

  return sample;
}

static void LTC1420_DebugFormatBits12(uint16_t code, char bits[13])
{
  code &= 0x0FFFU;
  for (uint32_t i = 0U; i < 12U; ++i)
  {
    const uint32_t shift = 11U - i;
    bits[i] = ((code & (1U << shift)) != 0U) ? '1' : '0';
  }
  bits[12] = '\0';
}

static int16_t LTC1420_DebugSignExtend12(uint16_t code)
{
  code &= 0x0FFFU;
  if ((code & 0x0800U) != 0U)
  {
    return (int16_t)(code | 0xF000U);
  }

  return (int16_t)code;
}
