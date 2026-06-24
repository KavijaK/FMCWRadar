#include "main.h"

#include "adf4158.h"
#include "pdet.h"
#include "usb_hs_debug.h"

#include <stdio.h>

#define ENABLE_LTC1420_DEBUG_CAPTURE 0U
#define ENABLE_ADF4158_BLADERF_TEST 0U
#define ENABLE_USB_HS_DEBUG 1U

#if ENABLE_USB_HS_DEBUG && defined(__GNUC__)
#define FMCW_MAYBE_UNUSED __attribute__((unused))
#else
#define FMCW_MAYBE_UNUSED
#endif

#if ENABLE_LTC1420_DEBUG_CAPTURE
#include "ltc1420_debug.h"

#endif

DCMI_HandleTypeDef hdcmi;
DMA_HandleTypeDef hdma_dcmi;

PCD_HandleTypeDef hpcd_USB_OTG_HS;

volatile uint32_t clock_failure_stage;
volatile uint32_t clock_failure_status;
volatile uint32_t clock_failure_rcc_cr;
volatile uint32_t clock_failure_rcc_pllcfgr;

void SystemClock_Config(void);
static void Clock_Failure(uint32_t stage, HAL_StatusTypeDef status);
static void MX_GPIO_Init(void) FMCW_MAYBE_UNUSED;
static void MX_DMA_Init(void) FMCW_MAYBE_UNUSED;
static void MX_DCMI_Init(void) FMCW_MAYBE_UNUSED;
static void MX_USB_OTG_HS_PCD_Init(void) FMCW_MAYBE_UNUSED;

int __io_putchar(int ch)
{
  ITM_SendChar((uint32_t)ch);
  return ch;
}

int main(void)
{
  HAL_Init();

  SystemClock_Config();

  printf("CLOCK: initialized, SYSCLK=%lu Hz\r\n", HAL_RCC_GetSysClockFreq());

#if ENABLE_USB_HS_DEBUG
  USBHSDebug_Init(&hpcd_USB_OTG_HS);
#else
  ADF4158_Init();
#if ENABLE_ADF4158_BLADERF_TEST
  ADF4158_ProgramBladeRfTest();
#else
  ADF4158_Program();
#endif
  ADF4158_EnableRfOutput();
  PDET_Init();
  HAL_Delay(10U);
  PDET_PrintOnce();

#if ENABLE_LTC1420_DEBUG_CAPTURE
  LTC1420_DebugCaptureOnce();
#endif
#endif

  while (1)
  {
#if ENABLE_USB_HS_DEBUG
    USBHSDebug_Task();
#else
    printf("Heartbeat: %lu ms\r\n", HAL_GetTick());
    HAL_Delay(1000);
#endif
  }

}

void SystemClock_Config(void)
{
  HAL_StatusTypeDef status;
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  clock_failure_stage = 0U;
  clock_failure_status = HAL_OK;
  clock_failure_rcc_cr = 0U;
  clock_failure_rcc_pllcfgr = 0U;

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  status = HAL_RCC_OscConfig(&RCC_OscInitStruct);
  if (status != HAL_OK)
  {
    Clock_Failure(1U, status);
  }

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_NONE;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 20;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  status = HAL_RCC_OscConfig(&RCC_OscInitStruct);
  if (status != HAL_OK)
  {
    Clock_Failure(2U, status);
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_2);
}

static void Clock_Failure(uint32_t stage, HAL_StatusTypeDef status)
{
  clock_failure_stage = stage;
  clock_failure_status = status;
  clock_failure_rcc_cr = RCC->CR;
  clock_failure_rcc_pllcfgr = RCC->PLLCFGR;

  while (1)
  {
  }
}

static void MX_DCMI_Init(void)
{

  hdcmi.Instance = DCMI;
  hdcmi.Init.SynchroMode = DCMI_SYNCHRO_HARDWARE;
  hdcmi.Init.PCKPolarity = DCMI_PCKPOLARITY_FALLING;
  hdcmi.Init.VSPolarity = DCMI_VSPOLARITY_HIGH;
  hdcmi.Init.HSPolarity = DCMI_HSPOLARITY_LOW;
  hdcmi.Init.CaptureRate = DCMI_CR_ALL_FRAME;
  hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_12B;
  hdcmi.Init.JPEGMode = DCMI_JPEG_DISABLE;
  if (HAL_DCMI_Init(&hdcmi) != HAL_OK)
  {
    Error_Handler();
  }

}

static void MX_USB_OTG_HS_PCD_Init(void)
{

  hpcd_USB_OTG_HS.Instance = USB_OTG_HS;
  hpcd_USB_OTG_HS.Init.dev_endpoints = 6;
  hpcd_USB_OTG_HS.Init.speed = PCD_SPEED_HIGH;
  hpcd_USB_OTG_HS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.phy_itface = USB_OTG_ULPI_PHY;
  hpcd_USB_OTG_HS.Init.Sof_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB_OTG_HS.Init.use_external_vbus = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_HS) != HAL_OK)
  {
    Error_Handler();
  }
}


static void MX_DMA_Init(void)
{

  __HAL_RCC_DMA2_CLK_ENABLE();

  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);

}


static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, PLL_LE_Pin|PLL_TXDATA_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, MISC1_Pin|MISC2_Pin|USB_RESET_Pin|PA_EN_Pin
                          |PLL_CE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, ADC_GAIN_Pin|MIXER_EN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : PLL_CLK_Pin PLL_DATA_Pin */
  GPIO_InitStruct.Pin = PLL_CLK_Pin|PLL_DATA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI4;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : PLL_MUXOUT_Pin TX_STDBY_Pin */
  GPIO_InitStruct.Pin = PLL_MUXOUT_Pin|TX_STDBY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : PLL_LE_Pin PLL_TXDATA_Pin */
  GPIO_InitStruct.Pin = PLL_LE_Pin|PLL_TXDATA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PDET_Pin */
  GPIO_InitStruct.Pin = PDET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PDET_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : MISC1_Pin MISC2_Pin USB_RESET_Pin PA_EN_Pin
                           PLL_CE_Pin */
  GPIO_InitStruct.Pin = MISC1_Pin|MISC2_Pin|USB_RESET_Pin|PA_EN_Pin
                          |PLL_CE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : ADC_OF_Pin */
  GPIO_InitStruct.Pin = ADC_OF_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ADC_OF_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : ADC_GAIN_Pin MIXER_EN_Pin */
  GPIO_InitStruct.Pin = ADC_GAIN_Pin|MIXER_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : PA8 */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF0_MCO;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}


void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
