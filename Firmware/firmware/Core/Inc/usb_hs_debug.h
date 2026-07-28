#ifndef USB_HS_DEBUG_H
#define USB_HS_DEBUG_H

#include "stm32f4xx_hal.h"

typedef uint8_t (*USBHSDebug_NextTxBufferCallback)(const uint8_t **data,
                                                   uint32_t *length);
typedef void (*USBHSDebug_TxDoneCallback)(const uint8_t *data,
                                          uint32_t length);

void USBHSDebug_Init(PCD_HandleTypeDef *hpcd);
void USBHSDebug_Task(void);
void USBHSDebug_SetTxCallbacks(USBHSDebug_NextTxBufferCallback next,
                               USBHSDebug_TxDoneCallback done);
uint8_t USBHSDebug_IsConfigured(void);
void USBHSDebug_ClearStreamStartRequest(void);
uint8_t USBHSDebug_IsStreamStartRequested(void);

#endif /* USB_HS_DEBUG_H */
