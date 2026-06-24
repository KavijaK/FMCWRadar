#include "adf4158.h"

#include "main.h"

#include <stdio.h>

static SPI_HandleTypeDef hspi4;

volatile uint32_t adf4158_failure_stage;
volatile uint32_t adf4158_failure_status;
/* Temporary bladeRF test profile knobs. Change only these two values for this test preset.
 * Bandwidth range: 1 MHz to 200 MHz. One-way chirp time range: 1 ms to 8 s.
 */
#define ADF4158_TEST_BANDWIDTH_HZ       2000000UL
#define ADF4158_TEST_CHIRP_TIME_US      8000000UL

#define ADF4158_TEST_CENTER_HZ          5800000000ULL
#define ADF4158_TEST_PFD_HZ             20000000UL
#define ADF4158_TEST_MODULUS            33554432ULL
#define ADF4158_TEST_STEPS              2000UL
#define ADF4158_TEST_DEV_MAX            32767UL
#define ADF4158_TEST_DEV_OFFSET_MAX     9UL
#define ADF4158_TEST_CLK_MAX            4095UL

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

/* Temporary bladeRF transmitter test profile is generated from
 * ADF4158_TEST_BANDWIDTH_HZ and ADF4158_TEST_CHIRP_TIME_US.
 * Equations:
 *   fDEV = fPFD / 2^25 * DEV * 2^DEV_OFFSET
 *   bandwidth = fDEV * N_STEPS
 *   step_time = CLK1 * CLK2 / fPFD
 *   chirp_time = step_time * N_STEPS
 * Center frequency is fixed at 5.8 GHz for this test profile.
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

static void ADF4158_Failure(uint32_t stage, HAL_StatusTypeDef status);
static void ADF4158_Write32(uint32_t word);
static void ADF4158_BuildBladeRfTestRegisters(uint32_t registers[10],
                                              uint32_t *dev,
                                              uint32_t *dev_offset,
                                              uint32_t *clk1,
                                              uint32_t *clk2,
                                              uint64_t *actual_bandwidth_hz,
                                              uint64_t *start_hz,
                                              uint64_t *stop_hz,
                                              uint32_t *actual_chirp_time_us);
static void ADF4158_SelectTestClockDividers(uint32_t target_product,
                                            uint32_t *clk1,
                                            uint32_t *clk2);
static uint32_t ADF4158_RoundDivU64(uint64_t numerator, uint64_t denominator);
static void ADF4158_PrintMHz(const char *label, uint64_t hz);

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
  HAL_GPIO_WritePin(MIXER_EN_GPIO_Port, MIXER_EN_Pin, GPIO_PIN_RESET);

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
  const uint32_t count = sizeof(adf4158_registers) / sizeof(adf4158_registers[0]);

  printf("ADF4158: programming 1 ms, 200.272 MHz triangle ramp\r\n");

  for (uint32_t i = 0U; i < count; ++i)
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
  uint32_t registers[10];
  uint32_t dev;
  uint32_t dev_offset;
  uint32_t clk1;
  uint32_t clk2;
  uint64_t actual_bandwidth_hz;
  uint64_t start_hz;
  uint64_t stop_hz;
  uint32_t actual_chirp_time_us;
  const uint32_t count = sizeof(registers) / sizeof(registers[0]);

  ADF4158_BuildBladeRfTestRegisters(registers,
                                    &dev,
                                    &dev_offset,
                                    &clk1,
                                    &clk2,
                                    &actual_bandwidth_hz,
                                    &start_hz,
                                    &stop_hz,
                                    &actual_chirp_time_us);

  printf("ADF4158 TEST: programming bladeRF triangle ramp\r\n");
  printf("ADF4158 TEST: target BW=%lu Hz, chirp=%lu us\r\n",
         (unsigned long)ADF4158_TEST_BANDWIDTH_HZ,
         (unsigned long)ADF4158_TEST_CHIRP_TIME_US);
  printf("ADF4158 TEST: DEV=%lu DEV_OFFSET=%lu CLK1=%lu CLK2=%lu\r\n",
         (unsigned long)dev,
         (unsigned long)dev_offset,
         (unsigned long)clk1,
         (unsigned long)clk2);
  ADF4158_PrintMHz("ADF4158 TEST: start=", start_hz);
  ADF4158_PrintMHz(" stop=", stop_hz);
  ADF4158_PrintMHz(" BW=", actual_bandwidth_hz);
  printf(" chirp=%lu us\r\n", (unsigned long)actual_chirp_time_us);

  for (uint32_t i = 0U; i < count; ++i)
  {
    printf("ADF4158 TEST: write %s = 0x%08lX\r\n",
           adf4158_register_names[i],
           (unsigned long)registers[i]);
    ADF4158_Write32(registers[i]);
  }

  HAL_Delay(1U);
  printf("ADF4158 TEST: programming complete\r\n");
}

void ADF4158_EnableRfOutput(void)
{
  if (HAL_GPIO_ReadPin(TX_STDBY_GPIO_Port, TX_STDBY_Pin) == GPIO_PIN_RESET)
  {
    HAL_GPIO_WritePin(MIXER_EN_GPIO_Port, MIXER_EN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PA_EN_GPIO_Port, PA_EN_Pin, GPIO_PIN_RESET);
    printf("RF: TX_STDBY is low, output remains disabled\r\n");
    return;
  }

  HAL_GPIO_WritePin(MIXER_EN_GPIO_Port, MIXER_EN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(PA_EN_GPIO_Port, PA_EN_Pin, GPIO_PIN_SET);
  printf("RF: MIXER_EN=1 PA_EN=1\r\n");
}

static void ADF4158_BuildBladeRfTestRegisters(uint32_t registers[10],
                                              uint32_t *dev,
                                              uint32_t *dev_offset,
                                              uint32_t *clk1,
                                              uint32_t *clk2,
                                              uint64_t *actual_bandwidth_hz,
                                              uint64_t *start_hz,
                                              uint64_t *stop_hz,
                                              uint32_t *actual_chirp_time_us)
{
  uint32_t selected_dev = 0U;
  uint32_t selected_offset = 0U;
  const uint64_t bandwidth = ADF4158_TEST_BANDWIDTH_HZ;
  const uint64_t chirp_us = ADF4158_TEST_CHIRP_TIME_US;
  const uint64_t target_step_denominator = (uint64_t)ADF4158_TEST_STEPS * ADF4158_TEST_PFD_HZ;
  uint32_t clk_product;
  uint64_t actual_bw;
  uint64_t start;
  uint64_t remainder;
  uint32_t int_word;
  uint32_t frac;
  uint32_t frac_msb;
  uint32_t frac_lsb;

  if ((ADF4158_TEST_BANDWIDTH_HZ < 1000000UL) ||
      (ADF4158_TEST_BANDWIDTH_HZ > 200000000UL) ||
      (ADF4158_TEST_CHIRP_TIME_US < 1000UL) ||
      (ADF4158_TEST_CHIRP_TIME_US > 8000000UL))
  {
    printf("ADF4158 TEST: requested bandwidth/chirp is outside the supported test range\r\n");
    ADF4158_Failure(3U, HAL_ERROR);
  }

  for (uint32_t offset = 0U; offset <= ADF4158_TEST_DEV_OFFSET_MAX; ++offset)
  {
    const uint64_t scale = 1ULL << offset;
    const uint32_t candidate = ADF4158_RoundDivU64(bandwidth * ADF4158_TEST_MODULUS,
                                                   target_step_denominator * scale);
    if ((candidate > 0U) && (candidate <= ADF4158_TEST_DEV_MAX))
    {
      selected_dev = candidate;
      selected_offset = offset;
      break;
    }
  }

  if (selected_dev == 0U)
  {
    printf("ADF4158 TEST: could not derive a valid DEV/DEV_OFFSET\r\n");
    ADF4158_Failure(4U, HAL_ERROR);
  }

  clk_product = ADF4158_RoundDivU64(chirp_us * ADF4158_TEST_PFD_HZ,
                                    1000000ULL * ADF4158_TEST_STEPS);
  if (clk_product == 0U)
  {
    clk_product = 1U;
  }
  ADF4158_SelectTestClockDividers(clk_product, clk1, clk2);

  actual_bw = ADF4158_RoundDivU64((uint64_t)ADF4158_TEST_PFD_HZ *
                                  selected_dev *
                                  (1ULL << selected_offset) *
                                  ADF4158_TEST_STEPS,
                                  ADF4158_TEST_MODULUS);
  start = ADF4158_TEST_CENTER_HZ - (actual_bw / 2ULL);
  int_word = (uint32_t)(start / ADF4158_TEST_PFD_HZ);
  remainder = start % ADF4158_TEST_PFD_HZ;
  frac = ADF4158_RoundDivU64(remainder * ADF4158_TEST_MODULUS,
                             ADF4158_TEST_PFD_HZ);
  if (frac >= ADF4158_TEST_MODULUS)
  {
    ++int_word;
    frac = 0U;
  }

  frac_msb = frac >> 13;
  frac_lsb = frac & 0x1FFFU;

  registers[0] = 0x00000007U;
  registers[1] = (ADF4158_TEST_STEPS << 3) | 0x6U;
  registers[2] = registers[1] | (1UL << 23);
  registers[3] = (selected_dev << 3) | (selected_offset << 19) | 0x5U;
  registers[4] = registers[3] | (1UL << 23);
  registers[5] = (0x00780504U & ~(0xFFFUL << 7)) | (*clk2 << 7);
  registers[6] = 0x00000443U;
  registers[7] = (0x0F40800AU & ~(0xFFFUL << 3)) | (*clk1 << 3);
  registers[8] = (frac_lsb << 15) | 0x1U;
  registers[9] = 0xF8000000U | (int_word << 15) | (frac_msb << 3);

  *dev = selected_dev;
  *dev_offset = selected_offset;
  *actual_bandwidth_hz = actual_bw;
  *start_hz = start;
  *stop_hz = start + actual_bw;
  *actual_chirp_time_us = ADF4158_RoundDivU64((uint64_t)(*clk1) *
                                               (*clk2) *
                                               ADF4158_TEST_STEPS *
                                               1000000ULL,
                                               ADF4158_TEST_PFD_HZ);
}

static void ADF4158_SelectTestClockDividers(uint32_t target_product,
                                            uint32_t *clk1,
                                            uint32_t *clk2)
{
  uint32_t best_clk1 = 1U;
  uint32_t best_clk2 = 1U;
  uint32_t best_error = 0xFFFFFFFFU;

  for (uint32_t candidate_clk1 = 1U; candidate_clk1 <= ADF4158_TEST_CLK_MAX; ++candidate_clk1)
  {
    uint32_t candidate_clk2 = ADF4158_RoundDivU64(target_product, candidate_clk1);
    uint32_t product;
    uint32_t error;

    if (candidate_clk2 == 0U)
    {
      candidate_clk2 = 1U;
    }
    if (candidate_clk2 > ADF4158_TEST_CLK_MAX)
    {
      continue;
    }

    product = candidate_clk1 * candidate_clk2;
    error = (product > target_product) ? (product - target_product) : (target_product - product);
    if (error < best_error)
    {
      best_error = error;
      best_clk1 = candidate_clk1;
      best_clk2 = candidate_clk2;
      if (error == 0U)
      {
        break;
      }
    }
  }

  if (best_error == 0xFFFFFFFFU)
  {
    printf("ADF4158 TEST: could not derive valid CLK1/CLK2\r\n");
    ADF4158_Failure(5U, HAL_ERROR);
  }

  *clk1 = best_clk1;
  *clk2 = best_clk2;
}

static uint32_t ADF4158_RoundDivU64(uint64_t numerator, uint64_t denominator)
{
  return (uint32_t)((numerator + (denominator / 2ULL)) / denominator);
}

static void ADF4158_PrintMHz(const char *label, uint64_t hz)
{
  const uint32_t mhz = (uint32_t)(hz / 1000000ULL);
  const uint32_t frac_hz = (uint32_t)(hz % 1000000ULL);

  printf("%s%lu.%06lu MHz",
         label,
         (unsigned long)mhz,
         (unsigned long)frac_hz);
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
