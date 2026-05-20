#include "usbd_vendor_stream.h"

#include "usb_stream.h"
#include "usbd_ctlreq.h"

static uint8_t init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t deinit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t data_in(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t *get_hs_config_descriptor(uint16_t *length);
static uint8_t *get_fs_config_descriptor(uint16_t *length);
static uint8_t *get_other_speed_config_descriptor(uint16_t *length);
static uint8_t *get_device_qualifier_descriptor(uint16_t *length);

USBD_ClassTypeDef USBD_VENDOR_STREAM = {
  init,
  deinit,
  setup,
  NULL,
  NULL,
  data_in,
  NULL,
  NULL,
  NULL,
  NULL,
  get_hs_config_descriptor,
  get_fs_config_descriptor,
  get_other_speed_config_descriptor,
  get_device_qualifier_descriptor,
};

__ALIGN_BEGIN static uint8_t hs_config_desc[USB_VENDOR_CONFIG_DESC_SIZ] __ALIGN_END = {
  0x09, USB_DESC_TYPE_CONFIGURATION,
  LOBYTE(USB_VENDOR_CONFIG_DESC_SIZ), HIBYTE(USB_VENDOR_CONFIG_DESC_SIZ),
  0x01, 0x01, 0x00,
  0xC0, 0x32,

  0x09, USB_DESC_TYPE_INTERFACE,
  0x00, 0x00, 0x01,
  0xFF, 0x00, 0x00, 0x00,

  0x07, USB_DESC_TYPE_ENDPOINT,
  FMCW_USB_EP_IN,
  USBD_EP_TYPE_BULK,
  LOBYTE(FMCW_USB_HS_MPS), HIBYTE(FMCW_USB_HS_MPS),
  0x00,
};

__ALIGN_BEGIN static uint8_t fs_config_desc[USB_VENDOR_CONFIG_DESC_SIZ] __ALIGN_END = {
  0x09, USB_DESC_TYPE_CONFIGURATION,
  LOBYTE(USB_VENDOR_CONFIG_DESC_SIZ), HIBYTE(USB_VENDOR_CONFIG_DESC_SIZ),
  0x01, 0x01, 0x00,
  0xC0, 0x32,

  0x09, USB_DESC_TYPE_INTERFACE,
  0x00, 0x00, 0x01,
  0xFF, 0x00, 0x00, 0x00,

  0x07, USB_DESC_TYPE_ENDPOINT,
  FMCW_USB_EP_IN,
  USBD_EP_TYPE_BULK,
  LOBYTE(FMCW_USB_FS_MPS), HIBYTE(FMCW_USB_FS_MPS),
  0x00,
};

__ALIGN_BEGIN static uint8_t other_speed_config_desc[USB_VENDOR_CONFIG_DESC_SIZ] __ALIGN_END = {
  0x09, USB_DESC_TYPE_OTHER_SPEED_CONFIGURATION,
  LOBYTE(USB_VENDOR_CONFIG_DESC_SIZ), HIBYTE(USB_VENDOR_CONFIG_DESC_SIZ),
  0x01, 0x01, 0x00,
  0xC0, 0x32,

  0x09, USB_DESC_TYPE_INTERFACE,
  0x00, 0x00, 0x01,
  0xFF, 0x00, 0x00, 0x00,

  0x07, USB_DESC_TYPE_ENDPOINT,
  FMCW_USB_EP_IN,
  USBD_EP_TYPE_BULK,
  LOBYTE(FMCW_USB_FS_MPS), HIBYTE(FMCW_USB_FS_MPS),
  0x00,
};

__ALIGN_BEGIN static uint8_t device_qualifier_desc[USB_VENDOR_DEVICE_QUALIFIER_SIZ] __ALIGN_END = {
  USB_VENDOR_DEVICE_QUALIFIER_SIZ,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00, 0x02,
  0x00, 0x00, 0x00,
  USB_MAX_EP0_SIZE,
  0x01,
  0x00,
};

/* Opens the single bulk IN endpoint and tells the stream layer USB is ready. */
static uint8_t init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  (void)cfgidx;
  uint16_t mps = (pdev->dev_speed == USBD_SPEED_HIGH) ? FMCW_USB_HS_MPS : FMCW_USB_FS_MPS;
  USBD_LL_OpenEP(pdev, FMCW_USB_EP_IN, USBD_EP_TYPE_BULK, mps);
  pdev->ep_in[FMCW_USB_EP_IN_NUM].is_used = 1u;
  usb_stream_class_configured(true);
  return USBD_OK;
}

/* Closes the bulk endpoint and blocks new frame transmissions. */
static uint8_t deinit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  (void)cfgidx;
  USBD_LL_CloseEP(pdev, FMCW_USB_EP_IN);
  pdev->ep_in[FMCW_USB_EP_IN_NUM].is_used = 0u;
  usb_stream_class_configured(false);
  return USBD_OK;
}

/* Handles only the standard interface requests needed by the USB stack. */
static uint8_t setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  static uint8_t interface_alt_setting;

  switch (req->bmRequest & USB_REQ_TYPE_MASK) {
    case USB_REQ_TYPE_CLASS:
    case USB_REQ_TYPE_VENDOR:
      USBD_CtlError(pdev, req);
      return USBD_FAIL;

    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest) {
        case USB_REQ_GET_INTERFACE:
          USBD_CtlSendData(pdev, &interface_alt_setting, 1u);
          break;
        case USB_REQ_SET_INTERFACE:
          interface_alt_setting = (uint8_t)(req->wValue);
          break;
        default:
          USBD_CtlError(pdev, req);
          return USBD_FAIL;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      return USBD_FAIL;
  }

  return USBD_OK;
}

/* Notifies usb_stream.c that the active frame slot has finished transmitting. */
static uint8_t data_in(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  (void)pdev;
  usb_stream_ep_in_complete(epnum);
  return USBD_OK;
}

/* Returns the high-speed descriptor with 512-byte bulk packets. */
static uint8_t *get_hs_config_descriptor(uint16_t *length)
{
  *length = sizeof(hs_config_desc);
  return hs_config_desc;
}

/* Returns a full-speed fallback descriptor for enumeration diagnostics. */
static uint8_t *get_fs_config_descriptor(uint16_t *length)
{
  *length = sizeof(fs_config_desc);
  return fs_config_desc;
}

/* Reports the descriptor the host would see at the other operating speed. */
static uint8_t *get_other_speed_config_descriptor(uint16_t *length)
{
  *length = sizeof(other_speed_config_desc);
  return other_speed_config_desc;
}

/* Reports high-speed capability to hosts that request a device qualifier. */
static uint8_t *get_device_qualifier_descriptor(uint16_t *length)
{
  *length = sizeof(device_qualifier_desc);
  return device_qualifier_desc;
}
