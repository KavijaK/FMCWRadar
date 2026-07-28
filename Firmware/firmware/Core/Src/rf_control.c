#include "rf_control.h"

#include "main.h"

#include <stdio.h>

static void RFConfigurePins(void);

void RFEnable(void)
{
  RFConfigurePins();

  if (HAL_GPIO_ReadPin(TX_STDBY_GPIO_Port, TX_STDBY_Pin) == GPIO_PIN_RESET)
  {
    RFDisable();
    printf("RF: TX_STDBY is low, RF output disabled\r\n");
    return;
  }

  HAL_GPIO_WritePin(MIXER_EN_GPIO_Port, MIXER_EN_Pin, GPIO_PIN_RESET);
//HAL_GPIO_WritePin(PA_EN_GPIO_Port, PA_EN_Pin, GPIO_PIN_SET);
  printf("RF: MIXER_EN=0(active) PA_EN=1\r\n");
}

void RFDisable(void)
{
  RFConfigurePins();
  HAL_GPIO_WritePin(MIXER_EN_GPIO_Port, MIXER_EN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(PA_EN_GPIO_Port, PA_EN_Pin, GPIO_PIN_RESET);
}

static void RFConfigurePins(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  gpio.Pin = TX_STDBY_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(TX_STDBY_GPIO_Port, &gpio);

  gpio.Pin = PA_EN_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PA_EN_GPIO_Port, &gpio);

  gpio.Pin = MIXER_EN_Pin;
  HAL_GPIO_Init(MIXER_EN_GPIO_Port, &gpio);
}
