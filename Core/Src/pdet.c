#include "pdet.h"

#include "main.h"

#include <stdio.h>

#define PDET_ADC_CHANNEL          14U
#define PDET_ADC_MAX_COUNT        4095U
#define PDET_ADC_VREF_MV          3300U
#define PDET_ADC_TIMEOUT_MS       10U

volatile uint32_t pdet_failure_stage;
volatile uint32_t pdet_failure_sr;

void PDET_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  pdet_failure_stage = 0U;
  pdet_failure_sr = 0U;

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_ADC1_CLK_ENABLE();

  gpio.Pin = PDET_Pin;
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PDET_GPIO_Port, &gpio);

  ADC1->CR1 = 0U;                         /* 12-bit, single conversion. */
  ADC1->CR2 = 0U;
  ADC1->SQR1 = 0U;                        /* One regular conversion. */
  ADC1->SQR3 = PDET_ADC_CHANNEL;
  ADC1->SMPR1 = (ADC1->SMPR1 & ~ADC_SMPR1_SMP14) | ADC_SMPR1_SMP14;

  ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0;
  ADC1->CR2 |= ADC_CR2_ADON;

  printf("PDET: ADC1 channel 14 initialized\r\n");
}

uint32_t PDET_ReadRaw(uint32_t *raw)
{
  uint32_t tickstart;

  ADC1->SR = 0U;
  ADC1->CR2 |= ADC_CR2_SWSTART;

  tickstart = HAL_GetTick();
  while ((ADC1->SR & ADC_SR_EOC) == 0U)
  {
    if ((HAL_GetTick() - tickstart) > PDET_ADC_TIMEOUT_MS)
    {
      pdet_failure_stage = 1U;
      pdet_failure_sr = ADC1->SR;
      return 0U;
    }
  }

  *raw = ADC1->DR & PDET_ADC_MAX_COUNT;
  return 1U;
}

void PDET_PrintOnce(void)
{
  uint32_t raw;
  uint32_t millivolts;

  if (PDET_ReadRaw(&raw) == 0U)
  {
    printf("PDET: ADC timeout, SR=0x%08lX\r\n", (unsigned long)pdet_failure_sr);
    return;
  }

  millivolts = (raw * PDET_ADC_VREF_MV) / PDET_ADC_MAX_COUNT;
  printf("PDET: raw=%lu, voltage=%lu mV\r\n",
         (unsigned long)raw,
         (unsigned long)millivolts);
}
