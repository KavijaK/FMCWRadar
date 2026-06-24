#ifndef USB_HS_DEBUG_H
#define USB_HS_DEBUG_H

#include "stm32f4xx_hal.h"

void USBHSDebug_Init(PCD_HandleTypeDef *hpcd);
void USBHSDebug_Task(void);

#endif /* USB_HS_DEBUG_H */
