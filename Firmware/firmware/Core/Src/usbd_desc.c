#include "usbd_desc.h"

#include "fmcw_board_config.h"
#include "stm32f4xx_hal.h"
#include "usbd_ctlreq.h"

#define USB_LANGID_STRING     0x0409u

static uint8_t USBD_DeviceDesc[USB_LEN_DEV_DESC] = {
  0x12, USB_DESC_TYPE_DEVICE,
  0x00, 0x02,
  0x00,
  0x00,
  0x00,
  USB_MAX_EP0_SIZE,
  LOBYTE(FMCW_USB_VID), HIBYTE(FMCW_USB_VID),
  LOBYTE(FMCW_USB_PID), HIBYTE(FMCW_USB_PID),
  LOBYTE(FMCW_USB_BCD), HIBYTE(FMCW_USB_BCD),
  USBD_IDX_MFC_STR,
  USBD_IDX_PRODUCT_STR,
  USBD_IDX_SERIAL_STR,
  USBD_MAX_NUM_CONFIGURATION,
};

static uint8_t USBD_LangIDDesc[USB_LEN_LANGID_STR_DESC] = {
  USB_LEN_LANGID_STR_DESC,
  USB_DESC_TYPE_STRING,
  LOBYTE(USB_LANGID_STRING),
  HIBYTE(USB_LANGID_STRING),
};

static uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZ];

static void int_to_unicode(uint32_t value, uint8_t *pbuf, uint8_t len)
{
  for (uint8_t idx = 0u; idx < len; idx++) {
    uint8_t digit = (uint8_t)((value >> 28) & 0x0fu);
    pbuf[2u * idx] = (digit < 10u) ? (uint8_t)('0' + digit) : (uint8_t)('A' + digit - 10u);
    pbuf[(2u * idx) + 1u] = 0u;
    value <<= 4;
  }
}

static uint8_t *device_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  *length = sizeof(USBD_DeviceDesc);
  return USBD_DeviceDesc;
}

static uint8_t *lang_id_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  *length = sizeof(USBD_LangIDDesc);
  return USBD_LangIDDesc;
}

static uint8_t *manufacturer_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  USBD_GetString((uint8_t *)"FMCW Radar", USBD_StrDesc, length);
  return USBD_StrDesc;
}

static uint8_t *product_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if (speed == USBD_SPEED_HIGH) {
    USBD_GetString((uint8_t *)"STM32F429 FMCW HS Stream", USBD_StrDesc, length);
  } else {
    USBD_GetString((uint8_t *)"STM32F429 FMCW FS Fallback", USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

static uint8_t *serial_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  USBD_StrDesc[0] = 26u;
  USBD_StrDesc[1] = USB_DESC_TYPE_STRING;
  int_to_unicode(HAL_GetUIDw0(), &USBD_StrDesc[2], 8u);
  int_to_unicode(HAL_GetUIDw1(), &USBD_StrDesc[18], 4u);
  *length = USBD_StrDesc[0];
  return USBD_StrDesc;
}

static uint8_t *config_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  USBD_GetString((uint8_t *)"FMCW Stream Config", USBD_StrDesc, length);
  return USBD_StrDesc;
}

static uint8_t *interface_descriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  USBD_GetString((uint8_t *)"Radar Bulk IN", USBD_StrDesc, length);
  return USBD_StrDesc;
}

USBD_DescriptorsTypeDef HS_Desc = {
  device_descriptor,
  lang_id_descriptor,
  manufacturer_descriptor,
  product_descriptor,
  serial_descriptor,
  config_descriptor,
  interface_descriptor,
};
