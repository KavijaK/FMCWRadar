#include "ltc1420_debug.h"

#include "main.h"

#include <stdio.h>

#define LTC1420_DEBUG_WORD_COUNT     2048U
#define LTC1420_DEBUG_TIMEOUT_MS     50U
#define LTC1420_DEBUG_PRINT_SAMPLES  8U
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
static void LTC1420_DebugInitDcmi(void);
static void LTC1420_DebugInitDma(void);
static uint32_t LTC1420_DebugRunCapture(void);
static void LTC1420_DebugPrintSamples(void);
static void LTC1420_DebugFormatBits12(uint16_t code, char bits[13]);
static int16_t LTC1420_DebugSignExtend12(uint16_t code);

void LTC1420_DebugCaptureOnce(void)
{
  printf("LTC1420 DEBUG: starting one-shot DCMI capture\r\n");

  LTC1420_DebugConfigureGain();
  LTC1420_DebugConfigureHsyncDrive();
  LTC1420_DebugPrepareVsyncStart();
  LTC1420_DebugConfigureDcmiPins();
  LTC1420_DebugInitDcmi();
  LTC1420_DebugInitDma();

  for (uint32_t i = 0U; i < LTC1420_DEBUG_WORD_COUNT; ++i)
  {
    ltc1420_debug_buffer[i] = 0U;
  }

  if (LTC1420_DebugRunCapture() != 0U)
  {
    LTC1420_DebugPrintSamples();
  }
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

  printf("LTC1420 DEBUG: DCMI GPIO configured\r\n");
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
  printf("LTC1420 DEBUG: ADC_GAIN=1x\r\n");
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
}

static void LTC1420_DebugStartHsyncLine(void)
{
  HAL_GPIO_WritePin(LTC1420_HSYNC_DRIVE_PORT, LTC1420_HSYNC_DRIVE_PIN, GPIO_PIN_SET);
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
}

static void LTC1420_DebugShortDelay(void)
{
  for (volatile uint32_t i = 0U; i < 2000U; ++i)
  {
  }
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

  printf("LTC1420 DEBUG: DCMI configured, 12-bit falling-edge\r\n");
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

  printf("LTC1420 DEBUG: DMA armed for %lu words\r\n",
         (unsigned long)LTC1420_DEBUG_WORD_COUNT);
}

static uint32_t LTC1420_DebugRunCapture(void)
{
  uint32_t tickstart;

  DMA2_Stream1->CR |= DMA_SxCR_EN;
  DCMI->CR = LTC1420_DCMI_BASE_CR | DCMI_CR_ENABLE | DCMI_CR_CAPTURE;

  LTC1420_DebugReleaseVsyncToDcmi();
  LTC1420_DebugShortDelay();
  LTC1420_DebugStartHsyncLine();
  LTC1420_DebugShortDelay();

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
      break;
    }
  }

  DCMI->CR &= ~DCMI_CR_CAPTURE;
  DMA2_Stream1->CR &= ~DMA_SxCR_EN;
  while ((DMA2_Stream1->CR & DMA_SxCR_EN) != 0U)
  {
  }

  if ((DMA2->LISR & DMA_LISR_TCIF1) != 0U)
  {
    printf("LTC1420 DEBUG: capture complete\r\n");
  }

  return ((DMA2->LISR & DMA_LISR_TCIF1) != 0U) ? 1U : 0U;
}

static void LTC1420_DebugPrintSamples(void)
{
  printf("LTC1420 DEBUG: first samples, each DCMI word holds two ADC samples\r\n");
  for (uint32_t i = 0U; i < LTC1420_DEBUG_PRINT_SAMPLES; ++i)
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

    printf("  [%lu] raw=0x%08lX  A b%s %+d mV  B b%s %+d mV\r\n",
           (unsigned long)i,
           (unsigned long)word,
           bits_a,
           signed_a,
           bits_b,
           signed_b);
  }
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
