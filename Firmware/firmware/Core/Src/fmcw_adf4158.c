#include "fmcw_adf4158.h"

#include "fmcw_board_config.h"
#include "fmcw_hardware.h"
#include "stm32f4xx_hal.h"

#if FMCW_ENABLE_ADF4158_AUTOPROGRAM
static SPI_HandleTypeDef s_hspi_adf4158;
#endif

/* Configures the optional SPI path used to program the ADF4158 ramp registers.
 * The normal RF enable path does not depend on this because the PLL may already
 * be programmed during bring-up.
 */
void fmcw_adf4158_init(void)
{
#if FMCW_ENABLE_ADF4158_AUTOPROGRAM
  GPIO_InitTypeDef gpio = {0};

  FMCW_ADF4158_GPIO_CLK_ENABLE();
  FMCW_ADF4158_LE_GPIO_CLK_ENABLE();
  FMCW_ADF4158_CE_GPIO_CLK_ENABLE();
  FMCW_ADF4158_SPI_CLK_ENABLE();
  FMCW_ADF4158_SPI_FORCE_RESET();
  FMCW_ADF4158_SPI_RELEASE_RESET();

  gpio.Pin = FMCW_ADF4158_SPI_SCK_PIN | FMCW_ADF4158_SPI_MOSI_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = FMCW_ADF4158_SPI_AF;
  HAL_GPIO_Init(FMCW_ADF4158_SPI_GPIO_PORT, &gpio);

  gpio.Pin = FMCW_ADF4158_LE_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(FMCW_ADF4158_LE_PORT, &gpio);
  gpio.Pin = FMCW_ADF4158_CE_PIN;
  HAL_GPIO_Init(FMCW_ADF4158_CE_PORT, &gpio);

  HAL_GPIO_WritePin(FMCW_ADF4158_LE_PORT, FMCW_ADF4158_LE_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(FMCW_ADF4158_CE_PORT, FMCW_ADF4158_CE_PIN, GPIO_PIN_SET);

  s_hspi_adf4158.Instance = FMCW_ADF4158_SPI;
  s_hspi_adf4158.Init.Mode = SPI_MODE_MASTER;
  s_hspi_adf4158.Init.Direction = SPI_DIRECTION_1LINE;
  s_hspi_adf4158.Init.DataSize = SPI_DATASIZE_8BIT;
  s_hspi_adf4158.Init.CLKPolarity = SPI_POLARITY_LOW;
  s_hspi_adf4158.Init.CLKPhase = SPI_PHASE_1EDGE;
  s_hspi_adf4158.Init.NSS = SPI_NSS_SOFT;
  s_hspi_adf4158.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  s_hspi_adf4158.Init.FirstBit = SPI_FIRSTBIT_MSB;
  s_hspi_adf4158.Init.TIMode = SPI_TIMODE_DISABLE;
  s_hspi_adf4158.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  s_hspi_adf4158.Init.CRCPolynomial = 7u;
  if (HAL_SPI_Init(&s_hspi_adf4158) != HAL_OK) {
    fmcw_error_trap(0x2001u);
  }
#endif
}

/* Sends one 32-bit ADF4158 register word MSB-first and pulses LE to latch it. */
void fmcw_adf4158_write32(uint32_t word)
{
#if FMCW_ENABLE_ADF4158_AUTOPROGRAM
  uint8_t bytes[4] = {
    (uint8_t)(word >> 24),
    (uint8_t)(word >> 16),
    (uint8_t)(word >> 8),
    (uint8_t)word,
  };

  HAL_GPIO_WritePin(FMCW_ADF4158_LE_PORT, FMCW_ADF4158_LE_PIN, GPIO_PIN_RESET);
  if (HAL_SPI_Transmit(&s_hspi_adf4158, bytes, sizeof(bytes), 10u) != HAL_OK) {
    fmcw_error_trap(0x2002u);
  }
  HAL_GPIO_WritePin(FMCW_ADF4158_LE_PORT, FMCW_ADF4158_LE_PIN, GPIO_PIN_SET);
  for (volatile uint32_t i = 0; i < 64u; ++i) {
  }
  HAL_GPIO_WritePin(FMCW_ADF4158_LE_PORT, FMCW_ADF4158_LE_PIN, GPIO_PIN_RESET);
#else
  (void)word;
#endif
}

/* Programs the default R7-to-R0 table from fmcw_board_config.h. Keep this
 * disabled until the actual center frequency, deviation, and ramp timing words
 * have been filled in.
 */
void fmcw_adf4158_program_default(void)
{
#if FMCW_ENABLE_ADF4158_AUTOPROGRAM
  static const uint32_t regs[] = FMCW_ADF4158_INIT_REGISTERS;
  for (uint32_t i = 0u; i < (sizeof(regs) / sizeof(regs[0])); ++i) {
    fmcw_adf4158_write32(regs[i]);
  }
  HAL_Delay(1u);
#endif
}
