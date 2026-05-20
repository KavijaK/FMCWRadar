#include "usb_stream.h"

#include "fmcw_capture.h"
#include "fmcw_protocol.h"
#include "fmcw_queue.h"
#include "usbd_desc.h"
#include "usbd_vendor_stream.h"

USBD_HandleTypeDef hUsbDeviceHS;

static volatile bool s_configured;
static volatile bool s_in_flight;
static volatile uint8_t s_current_slot = FMCW_SLOT_INVALID;

/* Creates the vendor-specific USB HS device and starts enumeration. */
void usb_stream_init(void)
{
  s_configured = false;
  s_in_flight = false;
  s_current_slot = FMCW_SLOT_INVALID;

  if (USBD_Init(&hUsbDeviceHS, &HS_Desc, DEVICE_HS) != USBD_OK) {
    return;
  }
  if (USBD_RegisterClass(&hUsbDeviceHS, &USBD_VENDOR_STREAM) != USBD_OK) {
    return;
  }
  (void)USBD_Start(&hUsbDeviceHS);
}

/* Returns true once the host has configured the streaming interface. */
bool usb_stream_ready(void)
{
  return s_configured;
}

/* Called by the USB class driver when configuration changes or disconnects. */
void usb_stream_class_configured(bool configured)
{
  s_configured = configured;
  if (!configured) {
    s_in_flight = false;
    s_current_slot = FMCW_SLOT_INVALID;
  }
}

/* Starts one bulk IN transfer if USB is configured, idle, and a frame is ready. */
void usb_stream_poll(void)
{
  uint8_t slot = FMCW_SLOT_INVALID;

  if (!s_configured || s_in_flight) {
    return;
  }

  if (!fmcw_capture_usb_dequeue(&slot)) {
    return;
  }

  s_current_slot = slot;
  s_in_flight = true;
  if (USBD_LL_Transmit(&hUsbDeviceHS, FMCW_USB_EP_IN,
                       fmcw_capture_slot_base(slot), FMCW_FRAME_BYTES) != USBD_OK) {
    s_in_flight = false;
    s_current_slot = FMCW_SLOT_INVALID;
    fmcw_capture_usb_requeue_front(slot);
  }
}

/* Releases the frame slot after EP1 IN finishes transmitting it to the host. */
void usb_stream_ep_in_complete(uint8_t epnum)
{
  if (epnum != FMCW_USB_EP_IN_NUM) {
    return;
  }

  uint8_t slot = s_current_slot;
  s_current_slot = FMCW_SLOT_INVALID;
  s_in_flight = false;

  if (slot != FMCW_SLOT_INVALID) {
    fmcw_capture_usb_release(slot);
  }
}
