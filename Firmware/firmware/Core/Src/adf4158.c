#include "adf4158.h"

#include "main.h"

#include <stdio.h>

static SPI_HandleTypeDef hspi4;

volatile uint32_t adf4158_failure_stage;
volatile uint32_t adf4158_failure_status;

#define ADF4158_REGISTER_COUNT          10U

/* ADIsimPLL-verified 1 ms slope with the 20 MHz TCXO reference:
 * start frequency = 5.700000000 GHz from INT = 285, FRAC = 0.
 * step time = CLK1 * CLK2 / fPFD = 1 * 10 / 20 MHz = 0.5 us.
 * step size = fPFD / 2^25 * DEV * 2^DEV_OFFSET
 *           = 20 MHz / 2^25 * 21000 * 2^3 = 100.135803 kHz.
 * one-way bandwidth = 2000 steps * 100.135803 kHz = 200.271606 MHz.
 * stop frequency = 5.900271606 GHz; sweep midpoint = 5.800135803 GHz.
 * triangle period = 2 * 2000 steps * 0.5 us = 2.000 ms.
 */
static const uint32_t adf4158_registers[] = {
  0x00000007U, /* R7: ramp delay */
  0x00003E86U, /* R6: N_STEPS = 2000, ramp 1 */
  0x00803E86U, /* R6: N_STEPS = 2000, ramp 2 */
  0x001A9045U, /* R5: DEV = 21000, DEV_OFFSET = 3, ramp 1 */
  0x009A9045U, /* R5: DEV = 21000, DEV_OFFSET = 3, ramp 2 */
  0x00780504U, /* R4: CLK2 = 10, ramp divider mode, ramp MUXOUT */
  0x00000443U, /* R3: triangle ramp function */
  0x0F40800AU, /* R2: CLK1 = 1, R divider = 1, CP = 5 mA, prescaler = 8/9 */
  0x00000001U, /* R1: FRAC LSB = 0 */
  0xF88E8000U, /* R0: INT = 285, FRAC MSB = 0, ramp enabled */
};

/* Temporary bladeRF transmitter test profile:
 * center frequency = 5.800000000 GHz.
 * start frequency = 5.799000013 GHz from INT = 289, FRAC = 31876732.
 * step time = CLK1 * CLK2 / fPFD = 800 * 1000 / 20 MHz = 40.000 ms.
 * one-way chirp time = 200 steps * 40.000 ms = 8.000000 s.
 * step size = 20 MHz / 2^25 * 16777 * 2^0 = 9.999871 kHz.
 * one-way bandwidth = 200 steps * 9.999871 kHz = 1.999974 MHz.
 * stop frequency = 5.800999987 GHz.
 */
static const uint32_t adf4158_bladerf_test_registers[] = {
  0x00000007U, /* R7: ramp delay */
  0x00000646U, /* R6: N_STEPS = 200, ramp 1 */
  0x00800646U, /* R6: N_STEPS = 200, ramp 2 */
  0x00020C4DU, /* R5: DEV = 16777, DEV_OFFSET = 0, ramp 1 */
  0x00820C4DU, /* R5: DEV = 16777, DEV_OFFSET = 0, ramp 2 */
  0x0079F404U, /* R4: CLK2 = 1000, ramp divider mode, ramp MUXOUT */
  0x00000443U, /* R3: triangle ramp function */
  0x0F409902U, /* R2: CLK1 = 800, R divider = 1, CP = 5 mA, prescaler = 8/9 */
  0x033E0001U, /* R1: FRAC LSB = 1660 */
  0xF890F998U, /* R0: INT = 289, FRAC MSB = 3891, ramp enabled */
};

/* Register write order is R7 down to R0.
 */
static const char * const adf4158_register_names[] = {
  "R7 delay",
  "R6 steps ramp1",
  "R6 steps ramp2",
  "R5 deviation ramp1",
  "R5 deviation ramp2",
  "R4 clock/muxout",
  "R3 ramp mode",
  "R2 reference/clk1",
  "R1 frac lsb",
  "R0 ramp enable",
};

_Static_assert((sizeof(adf4158_registers) / sizeof(adf4158_registers[0])) == ADF4158_REGISTER_COUNT,
               "ADF4158 original preset must contain 10 register writes");
_Static_assert((sizeof(adf4158_bladerf_test_registers) / sizeof(adf4158_bladerf_test_registers[0])) == ADF4158_REGISTER_COUNT,
               "ADF4158 test preset must contain 10 register writes");
_Static_assert((sizeof(adf4158_register_names) / sizeof(adf4158_register_names[0])) == ADF4158_REGISTER_COUNT,
               "ADF4158 register names must match register write count");

static void ADF4158_Failure(uint32_t stage, HAL_StatusTypeDef status);
static void ADF4158_Write32(uint32_t word);

void ADF4158_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  adf4158_failure_stage = 0U;
  adf4158_failure_status = HAL_OK;

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_SPI4_CLK_ENABLE();
  __HAL_RCC_SPI4_FORCE_RESET();
  __HAL_RCC_SPI4_RELEASE_RESET();

  HAL_GPIO_WritePin(PLL_LE_GPIO_Port, PLL_LE_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(PLL_CE_GPIO_Port, PLL_CE_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(PA_EN_GPIO_Port, PA_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(MIXER_EN_GPIO_Port, MIXER_EN_Pin, GPIO_PIN_SET);

  gpio.Pin = PLL_CLK_Pin | PLL_DATA_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF5_SPI4;
  HAL_GPIO_Init(GPIOE, &gpio);

  gpio.Pin = PLL_LE_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(PLL_LE_GPIO_Port, &gpio);

  gpio.Pin = PLL_CE_Pin | PA_EN_Pin;
  HAL_GPIO_Init(GPIOE, &gpio);

  gpio.Pin = MIXER_EN_Pin;
  HAL_GPIO_Init(MIXER_EN_GPIO_Port, &gpio);

  gpio.Pin = PLL_MUXOUT_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PLL_MUXOUT_GPIO_Port, &gpio);

  HAL_GPIO_WritePin(PLL_CE_GPIO_Port, PLL_CE_Pin, GPIO_PIN_SET);
  HAL_Delay(1U);

  hspi4.Instance = SPI4;
  hspi4.Init.Mode = SPI_MODE_MASTER;
  hspi4.Init.Direction = SPI_DIRECTION_1LINE;
  hspi4.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi4.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi4.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi4.Init.NSS = SPI_NSS_SOFT;
  hspi4.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi4.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi4.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi4.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi4.Init.CRCPolynomial = 7U;

  if (HAL_SPI_Init(&hspi4) != HAL_OK)
  {
    ADF4158_Failure(1U, HAL_ERROR);
  }

  printf("ADF4158: SPI4 initialized\r\n");
}

void ADF4158_Program(void)
{
  printf("ADF4158: programming 1 ms, 200.272 MHz triangle ramp\r\n");

  for (uint32_t i = 0U; i < ADF4158_REGISTER_COUNT; ++i)
  {
    printf("ADF4158: write %s = 0x%08lX\r\n",
           adf4158_register_names[i],
           (unsigned long)adf4158_registers[i]);
    ADF4158_Write32(adf4158_registers[i]);
  }

  HAL_Delay(1U);
  printf("ADF4158: programming complete\r\n");
}

void ADF4158_ProgramBladeRfTest(void)
{
  printf("ADF4158 TEST: programming 8 s, 1.999974 MHz triangle ramp\r\n");
  printf("ADF4158 TEST: start=5799.000013 MHz stop=5800.999987 MHz center=5800.000000 MHz\r\n");
  printf("ADF4158 TEST: DEV=16777 DEV_OFFSET=0 CLK1=800 CLK2=1000 N_STEPS=200\r\n");

  for (uint32_t i = 0U; i < ADF4158_REGISTER_COUNT; ++i)
  {
    printf("ADF4158 TEST: write %s = 0x%08lX\r\n",
           adf4158_register_names[i],
           (unsigned long)adf4158_bladerf_test_registers[i]);
    ADF4158_Write32(adf4158_bladerf_test_registers[i]);
  }

  HAL_Delay(1U);
  printf("ADF4158 TEST: programming complete\r\n");
}

void ADF4158_EnableRfOutput(void)
{
  if (HAL_GPIO_ReadPin(TX_STDBY_GPIO_Port, TX_STDBY_Pin) == GPIO_PIN_RESET)
  {
    HAL_GPIO_WritePin(MIXER_EN_GPIO_Port, MIXER_EN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PA_EN_GPIO_Port, PA_EN_Pin, GPIO_PIN_RESET);
    printf("RF: TX_STDBY is low, output remains disabled\r\n");
    return;
  }

  HAL_GPIO_WritePin(MIXER_EN_GPIO_Port, MIXER_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(PA_EN_GPIO_Port, PA_EN_Pin, GPIO_PIN_SET);
  printf("RF: MIXER_EN=0(active) PA_EN=1\r\n");
}

static void ADF4158_Write32(uint32_t word)
{
  uint8_t bytes[4];
  HAL_StatusTypeDef status;

  bytes[0] = (uint8_t)(word >> 24);
  bytes[1] = (uint8_t)(word >> 16);
  bytes[2] = (uint8_t)(word >> 8);
  bytes[3] = (uint8_t)word;

  HAL_GPIO_WritePin(PLL_LE_GPIO_Port, PLL_LE_Pin, GPIO_PIN_RESET);
  status = HAL_SPI_Transmit(&hspi4, bytes, 4U, 10U);
  if (status != HAL_OK)
  {
    ADF4158_Failure(2U, status);
  }

  HAL_GPIO_WritePin(PLL_LE_GPIO_Port, PLL_LE_Pin, GPIO_PIN_SET);
  for (volatile uint32_t i = 0U; i < 64U; ++i)
  {
  }
  HAL_GPIO_WritePin(PLL_LE_GPIO_Port, PLL_LE_Pin, GPIO_PIN_RESET);
}

static void ADF4158_Failure(uint32_t stage, HAL_StatusTypeDef status)
{
  adf4158_failure_stage = stage;
  adf4158_failure_status = status;

  printf("ADF4158: failure stage=%lu status=%lu\r\n",
         (unsigned long)stage,
         (unsigned long)status);

  while (1)
  {
  }
}
