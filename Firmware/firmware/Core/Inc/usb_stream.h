#ifndef USB_STREAM_H
#define USB_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include "usbd_def.h"

#define FMCW_USB_EP_IN       0x81u
#define FMCW_USB_EP_IN_NUM   1u
#define FMCW_USB_HS_MPS      512u
#define FMCW_USB_FS_MPS      64u

extern USBD_HandleTypeDef hUsbDeviceHS;

void usb_stream_init(void);
void usb_stream_poll(void);
bool usb_stream_ready(void);
void usb_stream_class_configured(bool configured);
void usb_stream_ep_in_complete(uint8_t epnum);

#endif

