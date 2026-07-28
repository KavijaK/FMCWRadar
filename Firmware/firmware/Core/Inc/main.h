/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define PLL_CLK_Pin GPIO_PIN_2
#define PLL_CLK_GPIO_Port GPIOE
#define PLL_MUXOUT_Pin GPIO_PIN_5
#define PLL_MUXOUT_GPIO_Port GPIOE
#define PLL_DATA_Pin GPIO_PIN_6
#define PLL_DATA_GPIO_Port GPIOE
#define ULP_STP_Pin GPIO_PIN_0
#define ULP_STP_GPIO_Port GPIOC
#define ULPI_DIR_Pin GPIO_PIN_2
#define ULPI_DIR_GPIO_Port GPIOC
#define ULPI_NXT_Pin GPIO_PIN_3
#define ULPI_NXT_GPIO_Port GPIOC
#define PLL_LE_Pin GPIO_PIN_1
#define PLL_LE_GPIO_Port GPIOA
#define PLL_TXDATA_Pin GPIO_PIN_2
#define PLL_TXDATA_GPIO_Port GPIOA
#define ULPI_D0_Pin GPIO_PIN_3
#define ULPI_D0_GPIO_Port GPIOA
#define ULPI_CLK_Pin GPIO_PIN_5
#define ULPI_CLK_GPIO_Port GPIOA
#define PDET_Pin GPIO_PIN_4
#define PDET_GPIO_Port GPIOC
#define ULPI_D1_Pin GPIO_PIN_0
#define ULPI_D1_GPIO_Port GPIOB
#define ULPI_D2_Pin GPIO_PIN_1
#define ULPI_D2_GPIO_Port GPIOB
#define MISC1_Pin GPIO_PIN_7
#define MISC1_GPIO_Port GPIOE
#define TX_STDBY_Pin GPIO_PIN_11
#define TX_STDBY_GPIO_Port GPIOE
#define MISC2_Pin GPIO_PIN_12
#define MISC2_GPIO_Port GPIOE
#define USB_RESET_Pin GPIO_PIN_13
#define USB_RESET_GPIO_Port GPIOE
#define ULPI_D3_Pin GPIO_PIN_10
#define ULPI_D3_GPIO_Port GPIOB
#define ULPI_D4_Pin GPIO_PIN_11
#define ULPI_D4_GPIO_Port GPIOB
#define ULPI_D5_Pin GPIO_PIN_12
#define ULPI_D5_GPIO_Port GPIOB
#define ULPI_D6_Pin GPIO_PIN_13
#define ULPI_D6_GPIO_Port GPIOB
#define ADC_OF_Pin GPIO_PIN_11
#define ADC_OF_GPIO_Port GPIOD
#define ADC_GAIN_Pin GPIO_PIN_12
#define ADC_GAIN_GPIO_Port GPIOD
#define MIXER_EN_Pin GPIO_PIN_13
#define MIXER_EN_GPIO_Port GPIOD
#define SWD_IO_Pin GPIO_PIN_13
#define SWD_IO_GPIO_Port GPIOA
#define SWD_CLK_Pin GPIO_PIN_14
#define SWD_CLK_GPIO_Port GPIOA
#define SWD_SWO_Pin GPIO_PIN_3
#define SWD_SWO_GPIO_Port GPIOB
#define ULPI_D7_Pin GPIO_PIN_5
#define ULPI_D7_GPIO_Port GPIOB
#define PA_EN_Pin GPIO_PIN_0
#define PA_EN_GPIO_Port GPIOE
#define PLL_CE_Pin GPIO_PIN_1
#define PLL_CE_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
