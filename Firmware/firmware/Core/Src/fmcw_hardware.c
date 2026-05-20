#include "fmcw_hardware.h"

#include "fmcw_board_config.h"
#include "fmcw_capture.h"
#include "stm32f4xx_hal.h"

static volatile bool s_tx_enabled;

/* Configures one or more pins for an alternate function such as DCMI, ULPI, or
 * TIM9 MUXOUT capture.
 */
static void gpio_af(GPIO_TypeDef *port, uint32_t pins, uint32_t af, uint32_t pull)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = pins;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = pull;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = af;
  HAL_GPIO_Init(port, &gpio);
}

/* Sets a safe output level before enabling the pin driver, avoiding brief
 * unwanted pulses on RF/PLL control lines.
 */
static void gpio_output(GPIO_TypeDef *port, uint32_t pins, GPIO_PinState initial)
{
  GPIO_InitTypeDef gpio = {0};

  HAL_GPIO_WritePin(port, pins, initial);
  gpio.Pin = pins;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(port, &gpio);
}

/* Configures ordinary digital inputs such as TX_STDBY and ADC overflow. */
static void gpio_input(GPIO_TypeDef *port, uint32_t pins, uint32_t pull)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = pins;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = pull;
  HAL_GPIO_Init(port, &gpio);
}

/* Parks unused or analog monitor pins in analog mode to reduce digital noise. */
static void gpio_analog(GPIO_TypeDef *port, uint32_t pins)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = pins;
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(port, &gpio);
}

/* Applies the requested RF transmit state to both mixer and PA enables. */
static void fmcw_tx_set_enabled(bool enabled)
{
  GPIO_PinState state = enabled ? GPIO_PIN_SET : GPIO_PIN_RESET;

  HAL_GPIO_WritePin(FMCW_MIXER_EN_PORT, FMCW_MIXER_EN_PIN, state);
  HAL_GPIO_WritePin(FMCW_PA_EN_PORT, FMCW_PA_EN_PIN, state);
  s_tx_enabled = enabled;
}

/* HAL calls this during HAL_Init(); keep only global clocks/priority grouping
 * here so the explicit firmware init order stays easy to audit.
 */
void HAL_MspInit(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
}

/* Creates SYSCLK=168 MHz from the 20 MHz HSE bypass and exports MCO1=10 MHz
 * for the ADC/DCMI sample clock path.
 */
void fmcw_clock_config(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_BYPASS;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = 20u;
  osc.PLL.PLLN = 336u;
  osc.PLL.PLLP = RCC_PLLP_DIV2;
  osc.PLL.PLLQ = 7u;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    fmcw_error_trap(0x1001u);
  }

  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV4;
  clk.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) {
    fmcw_error_trap(0x1002u);
  }

  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_2);
}

/* Applies the CubeMX pin map, initializes safe default output levels, and
 * samples TX_STDBY once so a normally-high board starts RF transmission.
 */
void fmcw_gpio_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  s_tx_enabled = false;

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  gpio.Pin = FMCW_DCMI_HSYNC_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(FMCW_DCMI_HSYNC_GPIO_PORT, &gpio);
  HAL_GPIO_WritePin(FMCW_DCMI_HSYNC_GPIO_PORT, FMCW_DCMI_HSYNC_PIN, GPIO_PIN_RESET);
  gpio_input(FMCW_DCMI_HSYNC_FEEDBACK_GPIO_PORT, FMCW_DCMI_HSYNC_FEEDBACK_PIN,
             GPIO_PULLDOWN);

  /* GPIO-only board controls imported from CubeMX. RF outputs stay low until
   * TX_STDBY is sampled at the end of this function.
   */
  gpio_output(FMCW_ADF4158_LE_PORT, FMCW_ADF4158_LE_PIN, GPIO_PIN_RESET);
  gpio_output(FMCW_PLL_TXDATA_PORT, FMCW_PLL_TXDATA_PIN, GPIO_PIN_RESET);
  gpio_output(FMCW_ADC_GAIN_PORT, FMCW_ADC_GAIN_PIN, GPIO_PIN_RESET);
  gpio_output(FMCW_MIXER_EN_PORT, FMCW_MIXER_EN_PIN, GPIO_PIN_RESET);
  gpio_output(FMCW_PA_EN_PORT, FMCW_PA_EN_PIN, GPIO_PIN_RESET);
  gpio_output(FMCW_MISC1_PORT, FMCW_MISC1_PIN, GPIO_PIN_RESET);
  gpio_output(FMCW_MISC2_PORT, FMCW_MISC2_PIN, GPIO_PIN_RESET);
  gpio_output(FMCW_ADF4158_CE_PORT, FMCW_ADF4158_CE_PIN,
              FMCW_ADF4158_CE_DEFAULT_ENABLE ? GPIO_PIN_SET : GPIO_PIN_RESET);
  gpio_input(FMCW_TX_STDBY_PORT, FMCW_TX_STDBY_PIN, GPIO_NOPULL);
  gpio_input(FMCW_ADC_OF_PORT, FMCW_ADC_OF_PIN, GPIO_NOPULL);
  gpio_analog(FMCW_PDET_PORT, FMCW_PDET_PIN);

  /* ULPI HS PHY pins from the CubeMX project. */
  gpio_af(GPIOA, GPIO_PIN_3 | GPIO_PIN_5, GPIO_AF10_OTG_HS, GPIO_NOPULL);
  gpio_af(GPIOA, GPIO_PIN_8, GPIO_AF0_MCO, GPIO_NOPULL);

  gpio_af(GPIOB, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_5 |
                 GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13,
          GPIO_AF10_OTG_HS, GPIO_NOPULL);

  gpio_af(GPIOC, GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_3, GPIO_AF10_OTG_HS, GPIO_NOPULL);

  /* DCMI ADC bus from the CubeMX pin map. PA4/HSYNC intentionally stays a GPIO
   * output so the alignment ISR can drive it high once and then leave it high.
   * PA7 is not configured as DCMI; it is the PCB-routed feedback copy of PA4.
   */
  gpio_af(GPIOA, GPIO_PIN_6 | GPIO_PIN_9, GPIO_AF13_DCMI, GPIO_NOPULL);
  gpio_af(GPIOB, GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9, GPIO_AF13_DCMI, GPIO_NOPULL);
  gpio_af(GPIOC, GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 |
                 GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12,
          GPIO_AF13_DCMI, GPIO_NOPULL);
  gpio_af(GPIOD, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_6, GPIO_AF13_DCMI, GPIO_NOPULL);

  /* MUXOUT must be TIM9_CH1 so the timer can reset/count from each ADF4158
   * slope edge. CubeMX marks PE5 as GPIO input; this firmware uses AF3.
   */
  gpio_af(GPIOE, GPIO_PIN_5, GPIO_AF3_TIM9, GPIO_PULLDOWN);

  fmcw_tx_service();
}

/* Resets the USB3317 PHY so ULPI starts from a known state before USB init. */
void fmcw_usb_phy_reset(void)
{
#if FMCW_USB_PHY_RESET_ENABLED
  GPIO_InitTypeDef gpio = {0};
  __HAL_RCC_GPIOE_CLK_ENABLE();
  gpio.Pin = FMCW_USB_PHY_RESET_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(FMCW_USB_PHY_RESET_PORT, &gpio);

  HAL_GPIO_WritePin(FMCW_USB_PHY_RESET_PORT, FMCW_USB_PHY_RESET_PIN, GPIO_PIN_RESET);
  HAL_Delay(1u);
  HAL_GPIO_WritePin(FMCW_USB_PHY_RESET_PORT, FMCW_USB_PHY_RESET_PIN, GPIO_PIN_SET);
  HAL_Delay(5u);
#endif
}

/* Polls TX_STDBY and mirrors it to MIXER_EN and PA_EN. This runs at boot and
 * in the main loop so an external TX-enable change is applied immediately.
 */
void fmcw_tx_service(void)
{
  bool request_tx =
    (HAL_GPIO_ReadPin(FMCW_TX_STDBY_PORT, FMCW_TX_STDBY_PIN) ==
     FMCW_TX_STDBY_ACTIVE_STATE);

  if (request_tx != s_tx_enabled) {
    fmcw_tx_set_enabled(request_tx);
  }
}

/* Reports the most recent RF-chain state written to the mixer/PA outputs. */
bool fmcw_tx_enabled(void)
{
  return s_tx_enabled;
}

/* Initializes DCMI in 12-bit continuous mode with falling-edge PIXCLK sampling
 * and leaves CAPTURE off. TIM9 turns CAPTURE on only after the first MUXOUT
 * edge plus the ADC pipeline offset.
 */
void fmcw_dcmi_init(void)
{
  __HAL_RCC_DCMI_CLK_ENABLE();
  __HAL_RCC_DCMI_FORCE_RESET();
  __HAL_RCC_DCMI_RELEASE_RESET();

  DCMI->CR = DCMI_CR_HSPOL |
             DCMI_CR_EDM_1;
  DCMI->IER = DCMI_IER_OVR_IE | DCMI_IER_ERR_IE;
  DCMI->ICR = DCMI_ICR_FRAME_ISC | DCMI_ICR_OVR_ISC |
              DCMI_ICR_ERR_ISC | DCMI_ICR_VSYNC_ISC |
              DCMI_ICR_LINE_ISC;
  DCMI->CR |= DCMI_CR_ENABLE;
}

/* Initializes DMA2 Stream 1 Channel 1 as a double-buffer stream. Each transfer
 * completion represents exactly one 1 ms slope payload.
 */
void fmcw_dma2_stream1_init(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();

  DMA2_Stream1->CR &= ~DMA_SxCR_EN;
  while ((DMA2_Stream1->CR & DMA_SxCR_EN) != 0u) {
  }

  DMA2->LIFCR = DMA_LIFCR_CFEIF1 | DMA_LIFCR_CDMEIF1 | DMA_LIFCR_CTEIF1 |
                DMA_LIFCR_CHTIF1 | DMA_LIFCR_CTCIF1;

  fmcw_capture_arm_dma();
  DMA2_Stream1->PAR = (uint32_t)&DCMI->DR;
  DMA2_Stream1->M0AR = (uint32_t)fmcw_capture_slot_payload(0u);
  DMA2_Stream1->M1AR = (uint32_t)fmcw_capture_slot_payload(1u);
  DMA2_Stream1->NDTR = FMCW_DMA_WORDS_PER_SLOPE;
  DMA2_Stream1->FCR = DMA_SxFCR_DMDIS |
                      (3u << DMA_SxFCR_FTH_Pos) |
                      DMA_SxFCR_FEIE;
  DMA2_Stream1->CR = (1u << DMA_SxCR_CHSEL_Pos) |
                     DMA_SxCR_DBM |
                     DMA_SxCR_MINC |
                     (2u << DMA_SxCR_PSIZE_Pos) |
                     (2u << DMA_SxCR_MSIZE_Pos) |
                     (3u << DMA_SxCR_PL_Pos) |
                     DMA_SxCR_TCIE |
                     DMA_SxCR_TEIE |
                     DMA_SxCR_DMEIE |
                     (1u << DMA_SxCR_MBURST_Pos);
  DMA2_Stream1->CR |= DMA_SxCR_EN;
}

/* Configures TIM9 to reset from MUXOUT on PE5 and raise CC2 after the alignment
 * delay that compensates the LTC1420 pipeline.
 */
void fmcw_tim9_init(void)
{
  __HAL_RCC_TIM9_CLK_ENABLE();
  __HAL_RCC_TIM9_FORCE_RESET();
  __HAL_RCC_TIM9_RELEASE_RESET();

  TIM9->CR1 = 0u;
  TIM9->PSC = 3u;
  TIM9->ARR = 50000u;
  TIM9->CCR2 = 13u;
  TIM9->CCMR1 = (1u << TIM_CCMR1_CC1S_Pos) |
                (2u << TIM_CCMR1_IC1F_Pos);
  TIM9->CCER = TIM_CCER_CC1E;
  TIM9->SMCR = (4u << TIM_SMCR_SMS_Pos) |
               (5u << TIM_SMCR_TS_Pos);
  TIM9->DIER = TIM_DIER_CC1IE | TIM_DIER_CC2IE;
  TIM9->SR = 0u;
  TIM9->EGR = TIM_EGR_UG;
}

/* Starts TIM9; the first MUXOUT pulse will trigger DCMI capture alignment. */
void fmcw_tim9_start(void)
{
  TIM9->CNT = 0u;
  TIM9->SR = 0u;
  TIM9->CR1 |= TIM_CR1_CEN;
}

/* Enables interrupts after all peripheral registers and buffers are ready. */
void fmcw_nvic_init(void)
{
  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 0u, 0u);
  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);

  HAL_NVIC_SetPriority(DCMI_IRQn, 1u, 0u);
  HAL_NVIC_EnableIRQ(DCMI_IRQn);

  HAL_NVIC_SetPriority(OTG_HS_IRQn, 2u, 0u);
  HAL_NVIC_EnableIRQ(OTG_HS_IRQn);

  HAL_NVIC_SetPriority(TIM1_BRK_TIM9_IRQn, 3u, 0u);
  HAL_NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn);
}

/* Stops execution on an unrecoverable bring-up error. Inspect `code` in the
 * debugger to identify which initialization step failed.
 */
void fmcw_error_trap(uint32_t code)
{
  (void)code;
  __disable_irq();
  while (1) {
  }
}
