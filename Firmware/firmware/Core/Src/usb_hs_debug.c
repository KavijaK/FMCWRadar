#include "usb_hs_debug.h"

#include "main.h"

#include <stdio.h>

#define USB_HS_DEBUG_EP_IN              0x81U
#define USB_HS_DEBUG_EP0_MPS            64U
#define USB_HS_DEBUG_HS_BULK_MPS        512U
#define USB_HS_DEBUG_FS_BULK_MPS        64U
#define USB_HS_DEBUG_TX_SIZE            16384U
#define USB_HS_DEBUG_MS_VENDOR_CODE     0x21U
#define USB_HS_DEBUG_BUILD_TAG          "usb-hs-ep0-status-fix-2026-06-24"
#define USB_HS_DEBUG_RUN_PHY_PROBES     0U

#define USB_HS_GPVNDCTL                 (*(__IO uint32_t *)((uint32_t)USB_OTG_HS + 0x34U))
#define USB_HS_GPVNDCTL_REGDATA(data)   ((uint32_t)(data) & 0xFFUL)
#define USB_HS_GPVNDCTL_RDATA(word)     ((uint8_t)((word) & 0xFFUL))
#define USB_HS_GPVNDCTL_REGADDR(reg)    (((uint32_t)(reg) & 0x3FUL) << 16)
#define USB_HS_GPVNDCTL_REGWR           (1UL << 22)
#define USB_HS_GPVNDCTL_NEWREGREQ       (1UL << 25)
#define USB_HS_GPVNDCTL_VSTSBUSY        (1UL << 26)
#define USB_HS_GPVNDCTL_VSTSDONE        (1UL << 27)
#define USB_HS_GPVNDCTL_TIMEOUT         1000000UL

#define ULPI_REG_VENDOR_ID_LOW          0x00U
#define ULPI_REG_VENDOR_ID_HIGH         0x01U
#define ULPI_REG_PRODUCT_ID_LOW         0x02U
#define ULPI_REG_PRODUCT_ID_HIGH        0x03U
#define ULPI_REG_FUNCTION_CONTROL       0x04U
#define ULPI_REG_INTERFACE_CONTROL      0x07U
#define ULPI_REG_INTERFACE_CONTROL_SET  0x08U
#define ULPI_REG_INTERFACE_CONTROL_CLR  0x09U
#define ULPI_REG_OTG_CONTROL            0x0AU
#define ULPI_REG_OTG_CONTROL_SET        0x0BU
#define ULPI_REG_OTG_CONTROL_CLR        0x0CU
#define ULPI_REG_USB_INT_STATUS         0x13U
#define ULPI_REG_USB_INT_LATCH          0x14U
#define ULPI_REG_DEBUG                  0x15U
#define ULPI_REG_SCRATCH                0x16U
#define ULPI_REG_USB_IO_PWR             0x39U
#define ULPI_REG_USB_IO_PWR_SET         0x3AU
#define ULPI_REG_USB_IO_PWR_CLR         0x3BU

#define ULPI_FUNC_FS_ATTACH             0x45U
#define ULPI_FUNC_NONDRIVING            0x49U
#define ULPI_IFACE_INDICATOR_COMPLEMENT 0x20U
#define ULPI_IFACE_INDICATOR_PASSTHRU   0x40U
#define ULPI_OTG_DP_PULLDOWN            0x02U
#define ULPI_OTG_DM_PULLDOWN            0x04U
#define ULPI_OTG_DISCHARGE_VBUS         0x08U
#define ULPI_OTG_CHARGE_VBUS            0x10U
#define ULPI_OTG_DRV_VBUS               0x20U
#define ULPI_OTG_DRV_VBUS_EXTERNAL      0x40U
#define ULPI_OTG_USE_EXTERNAL_VBUS      0x80U
#define ULPI_USB_IO_PWR_CHG_DP          0x10U
#define ULPI_USB_IO_PWR_CHG_DM          0x20U
#define USB_HS_FORCE_ATTACH_PROBE_MS    5000U
#define USB_HS_WEAK_PULLUP_PROBE_MS     3000U

#define USB_REQ_GET_STATUS              0x00U
#define USB_REQ_CLEAR_FEATURE           0x01U
#define USB_REQ_SET_FEATURE             0x03U
#define USB_REQ_SET_ADDRESS             0x05U
#define USB_REQ_GET_DESCRIPTOR          0x06U
#define USB_REQ_GET_CONFIGURATION       0x08U
#define USB_REQ_SET_CONFIGURATION       0x09U
#define USB_REQ_GET_INTERFACE           0x0AU
#define USB_REQ_SET_INTERFACE           0x0BU

#define USB_DESC_TYPE_DEVICE            0x01U
#define USB_DESC_TYPE_CONFIGURATION     0x02U
#define USB_DESC_TYPE_STRING            0x03U
#define USB_DESC_TYPE_DEVICE_QUALIFIER  0x06U
#define USB_DESC_TYPE_OTHER_SPEED       0x07U

typedef struct
{
  uint8_t bmRequestType;
  uint8_t bRequest;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
} USBHSDebug_SetupRequest;

typedef enum
{
  USB_HS_DEBUG_EP0_IDLE = 0,
  USB_HS_DEBUG_EP0_DATA_IN,
  USB_HS_DEBUG_EP0_STATUS_IN,
  USB_HS_DEBUG_EP0_STATUS_OUT
} USBHSDebug_Ep0State;

static PCD_HandleTypeDef *usb_hs_debug_pcd;
static uint8_t usb_hs_debug_configuration;
static uint8_t usb_hs_debug_pending_address;
static uint8_t usb_hs_debug_has_pending_address;
static USBHSDebug_Ep0State usb_hs_debug_ep0_state;
static volatile uint8_t usb_hs_debug_tx_busy;
static volatile uint8_t usb_hs_debug_stream_enabled;
static volatile uint32_t usb_hs_debug_completed_transfers;
static volatile uint64_t usb_hs_debug_completed_bytes;
static uint8_t usb_hs_debug_tx_buffer[USB_HS_DEBUG_TX_SIZE];
static uint8_t usb_hs_debug_ep0_buffer[64];
static uint32_t usb_hs_debug_sequence;
static uint32_t usb_hs_debug_last_report_tick;
static uint64_t usb_hs_debug_last_report_bytes;
static uint8_t usb_hs_debug_setup_print_count;
static uint8_t usb_hs_debug_ep0_print_count;
static uint8_t usb_hs_debug_phy_clock_ready;
static uint8_t usb_hs_debug_usb_started;

static const uint8_t usb_hs_debug_device_desc[] = {
  0x12, USB_DESC_TYPE_DEVICE,
  0x00, 0x02,
  0x00, 0x00, 0x00,
  USB_HS_DEBUG_EP0_MPS,
  0x09, 0x12,
  0x58, 0x41,
  0x00, 0x01,
  0x01, 0x02, 0x03,
  0x01
};

static const uint8_t usb_hs_debug_device_qualifier_desc[] = {
  0x0A, USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00, 0x02,
  0x00, 0x00, 0x00,
  USB_HS_DEBUG_EP0_MPS,
  0x01,
  0x00
};

static const uint8_t usb_hs_debug_string_lang[] = {
  0x04, USB_DESC_TYPE_STRING,
  0x09, 0x04
};

static const uint8_t usb_hs_debug_string_manufacturer[] = {
  0x0C, USB_DESC_TYPE_STRING,
  'K', 0, 'a', 0, 'r', 0, 'u', 0, 'n', 0
};

static const uint8_t usb_hs_debug_string_product[] = {
  0x32, USB_DESC_TYPE_STRING,
  'F', 0, 'M', 0, 'C', 0, 'W', 0, ' ', 0,
  'R', 0, 'a', 0, 'd', 0, 'a', 0, 'r', 0, ' ', 0,
  'H', 0, 'S', 0, ' ', 0,
  'B', 0, 'u', 0, 'l', 0, 'k', 0, ' ', 0,
  'D', 0, 'e', 0, 'b', 0, 'u', 0, 'g', 0
};

static const uint8_t usb_hs_debug_string_serial[] = {
  0x14, USB_DESC_TYPE_STRING,
  'F', 0, 'M', 0, 'C', 0, 'W', 0,
  'H', 0, 'S', 0, '0', 0, '0', 0, '1', 0
};

static const uint8_t usb_hs_debug_ms_os_string[] = {
  0x12, USB_DESC_TYPE_STRING,
  'M', 0, 'S', 0, 'F', 0, 'T', 0,
  '1', 0, '0', 0, '0', 0,
  USB_HS_DEBUG_MS_VENDOR_CODE,
  0x00
};

static const uint8_t usb_hs_debug_ms_compat_id_desc[] = {
  0x28, 0x00, 0x00, 0x00,
  0x00, 0x01,
  0x04, 0x00,
  0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

  0x00,
  0x01,
  'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void USBHSDebug_StartDevice(PCD_HandleTypeDef *hpcd);
static void USBHSDebug_PreparePhyPinsForReset(void);
static void USBHSDebug_ResetPhy(void);
static void USBHSDebug_PulseStpWake(void);
static uint32_t USBHSDebug_CountUlpiClockTransitions(uint32_t sample_ms);
static uint8_t USBHSDebug_BringUpPhyClock(void);
static uint8_t USBHSDebug_ReadUlpiDataBus(void);
static void USBHSDebug_PrintUlpiPins(const char *tag);
static void USBHSDebug_PrintUlpiActivity(const char *tag, uint32_t sample_ms);
static uint8_t USBHSDebug_ReadUlpiRegister(uint8_t reg, uint8_t *value);
static uint8_t USBHSDebug_WriteUlpiRegister(uint8_t reg, uint8_t value);
static void USBHSDebug_ConfigureUsb3317DeviceMode(void);
static void USBHSDebug_PrintUlpiRegisters(void);
static void USBHSDebug_PrintUlpiRuntimeStatus(const char *tag);
static void USBHSDebug_RunForcedFsAttachProbe(void);
static void USBHSDebug_RunWeakPullupProbe(void);
static void USBHSDebug_ResetUsbCorePeripheral(void);
static void USBHSDebug_PrintUsbCorePreflight(const char *tag);
static void USBHSDebug_PrintCoreStatus(const char *tag);
static void USBHSDebug_ParseSetup(PCD_HandleTypeDef *hpcd,
                                  USBHSDebug_SetupRequest *req);
static void USBHSDebug_HandleStandardRequest(PCD_HandleTypeDef *hpcd,
                                             const USBHSDebug_SetupRequest *req);
static void USBHSDebug_HandleVendorRequest(PCD_HandleTypeDef *hpcd,
                                           const USBHSDebug_SetupRequest *req);
static void USBHSDebug_SendControlData(PCD_HandleTypeDef *hpcd,
                                       const uint8_t *data,
                                       uint16_t data_len,
                                       uint16_t requested_len);
static void USBHSDebug_SendStatus(PCD_HandleTypeDef *hpcd);
static void USBHSDebug_ReceiveStatusOut(PCD_HandleTypeDef *hpcd);
static void USBHSDebug_StallControl(PCD_HandleTypeDef *hpcd);
static uint16_t USBHSDebug_BulkMps(const PCD_HandleTypeDef *hpcd);
static void USBHSDebug_BuildConfigDescriptor(uint8_t descriptor_type,
                                             uint16_t bulk_mps);
static void USBHSDebug_OpenBulkIn(PCD_HandleTypeDef *hpcd);
static void USBHSDebug_CloseBulkIn(PCD_HandleTypeDef *hpcd);
static void USBHSDebug_FillTxBuffer(void);
static void USBHSDebug_QueueNextPacket(void);
static const char *USBHSDebug_SpeedName(const PCD_HandleTypeDef *hpcd);

void USBHSDebug_Init(PCD_HandleTypeDef *hpcd)
{
  usb_hs_debug_pcd = hpcd;
  usb_hs_debug_configuration = 0U;
  usb_hs_debug_pending_address = 0U;
  usb_hs_debug_has_pending_address = 0U;
  usb_hs_debug_ep0_state = USB_HS_DEBUG_EP0_IDLE;
  usb_hs_debug_stream_enabled = 0U;
  usb_hs_debug_tx_busy = 0U;
  usb_hs_debug_sequence = 0U;
  usb_hs_debug_completed_bytes = 0U;
  usb_hs_debug_completed_transfers = 0U;
  usb_hs_debug_last_report_tick = HAL_GetTick();
  usb_hs_debug_last_report_bytes = 0U;
  usb_hs_debug_setup_print_count = 0U;
  usb_hs_debug_ep0_print_count = 0U;
  usb_hs_debug_phy_clock_ready = 0U;
  usb_hs_debug_usb_started = 0U;

  printf("USB HS DEBUG BUILD: %s\r\n", USB_HS_DEBUG_BUILD_TAG);
  printf("USB HS DEBUG: resetting USB3317 PHY\r\n");
  USBHSDebug_PreparePhyPinsForReset();
  USBHSDebug_PrintUlpiPins("pins before PHY reset");
  usb_hs_debug_phy_clock_ready = USBHSDebug_BringUpPhyClock();
  if (usb_hs_debug_phy_clock_ready == 0U)
  {
    printf("USB HS DEBUG: ULPI clock is not running; skip USB core init\r\n");
    return;
  }

  USBHSDebug_StartDevice(hpcd);
}

void USBHSDebug_Task(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t elapsed = now - usb_hs_debug_last_report_tick;

  if ((usb_hs_debug_stream_enabled != 0U) && (usb_hs_debug_tx_busy == 0U))
  {
    USBHSDebug_QueueNextPacket();
  }

  if (elapsed >= 1000U)
  {
    uint64_t bytes;
    uint32_t transfers;
    uint32_t delta_bytes;
    uint32_t rate_mbps_x1000;

    __disable_irq();
    bytes = usb_hs_debug_completed_bytes;
    transfers = usb_hs_debug_completed_transfers;
    __enable_irq();

    delta_bytes = (uint32_t)(bytes - usb_hs_debug_last_report_bytes);
    rate_mbps_x1000 = (elapsed == 0U) ? 0U : ((delta_bytes * 8U) / elapsed);

    if (usb_hs_debug_configuration != 0U)
    {
      printf("USB HS DEBUG: configured %s, tx=%lu KiB, transfers=%lu, rate=%lu.%03lu Mbps\r\n",
             USBHSDebug_SpeedName(usb_hs_debug_pcd),
             (unsigned long)(bytes / 1024ULL),
             (unsigned long)transfers,
             (unsigned long)(rate_mbps_x1000 / 1000U),
             (unsigned long)(rate_mbps_x1000 % 1000U));
    }
    else
    {
      if (usb_hs_debug_phy_clock_ready == 0U)
      {
        printf("USB HS DEBUG: PHY not ready, waiting for ULPI_CLK\r\n");
        USBHSDebug_PrintUlpiPins("PHY not ready pins");
        printf("USB HS DEBUG: ULPI_CLK transitions in 2 ms = %lu\r\n",
               (unsigned long)USBHSDebug_CountUlpiClockTransitions(2U));
      }
      else
      {
        if (usb_hs_debug_usb_started == 0U)
        {
          printf("USB HS DEBUG: USB core not started; not waiting for enumeration yet\r\n");
          USBHSDebug_PrintUlpiPins("USB core stopped pins");
          USBHSDebug_PrintUsbCorePreflight("USB core stopped");
        }
        else
        {
          USBHSDebug_PrintCoreStatus("waiting for host enumeration");
          USBHSDebug_PrintUlpiRuntimeStatus("waiting PHY");
        }
      }
    }

    usb_hs_debug_last_report_tick = now;
    usb_hs_debug_last_report_bytes = bytes;
  }

  HAL_Delay(1U);
}

static void USBHSDebug_StartDevice(PCD_HandleTypeDef *hpcd)
{
  hpcd->Instance = USB_OTG_HS;
  hpcd->Init.dev_endpoints = 6U;
  hpcd->Init.speed = PCD_SPEED_HIGH;
  hpcd->Init.ep0_mps = USB_HS_DEBUG_EP0_MPS;
  hpcd->Init.dma_enable = DISABLE;
  hpcd->Init.phy_itface = USB_OTG_ULPI_PHY;
  hpcd->Init.Sof_enable = DISABLE;
  hpcd->Init.low_power_enable = DISABLE;
  hpcd->Init.lpm_enable = DISABLE;
  hpcd->Init.battery_charging_enable = DISABLE;
  hpcd->Init.vbus_sensing_enable = DISABLE;
  hpcd->Init.use_dedicated_ep1 = DISABLE;
  hpcd->Init.use_external_vbus = ENABLE;

  printf("USB HS DEBUG: MSP preflight before HAL_PCD_Init\r\n");
  HAL_PCD_MspInit(hpcd);
  USBHSDebug_ResetUsbCorePeripheral();
  USBHSDebug_PrintUsbCorePreflight("after MSP preflight");

  printf("USB HS DEBUG: calling HAL_PCD_Init\r\n");
  if (HAL_PCD_Init(hpcd) != HAL_OK)
  {
    usb_hs_debug_usb_started = 0U;
    printf("USB HS DEBUG: HAL_PCD_Init failed\r\n");
    USBHSDebug_PrintUsbCorePreflight("HAL_PCD_Init failed");
    USBHSDebug_PrintUlpiPins("pins after HAL_PCD_Init failure");
    USBHSDebug_PrintUlpiRegisters();
    return;
  }
  printf("USB HS DEBUG: HAL_PCD_Init complete\r\n");

  USBHSDebug_PrintUlpiPins("pins after PCD init");
  USBHSDebug_ConfigureUsb3317DeviceMode();
  USBHSDebug_PrintUlpiRegisters();
  if (USB_HS_DEBUG_RUN_PHY_PROBES != 0U)
  {
    USBHSDebug_RunForcedFsAttachProbe();
    USBHSDebug_RunWeakPullupProbe();
  }

  (void)HAL_PCDEx_SetRxFiFo(hpcd, 0x80U);
  (void)HAL_PCDEx_SetTxFiFo(hpcd, 0U, 0x40U);
  (void)HAL_PCDEx_SetTxFiFo(hpcd, 1U, 0x200U);

  HAL_NVIC_SetPriority(OTG_HS_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(OTG_HS_IRQn);

  (void)HAL_PCD_DevDisconnect(hpcd);
  HAL_Delay(100U);
  USBHSDebug_PrintCoreStatus("before connect");

  if (HAL_PCD_Start(hpcd) != HAL_OK)
  {
    usb_hs_debug_usb_started = 0U;
    printf("USB HS DEBUG: HAL_PCD_Start failed\r\n");
    USBHSDebug_PrintUsbCorePreflight("HAL_PCD_Start failed");
    return;
  }

  usb_hs_debug_usb_started = 1U;
  printf("USB HS DEBUG: device started, VID=0x1209 PID=0x4158 EP_IN=0x81\r\n");
  USBHSDebug_PrintUlpiPins("pins after device connect");
  USBHSDebug_PrintUlpiRuntimeStatus("after device connect");
  USBHSDebug_PrintUlpiActivity("connected ULPI activity", 5U);
  USBHSDebug_PrintCoreStatus("after connect");
}

static void USBHSDebug_PreparePhyPinsForReset(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  HAL_GPIO_WritePin(ULP_STP_GPIO_Port, ULP_STP_Pin, GPIO_PIN_RESET);
  gpio.Pin = ULP_STP_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(ULP_STP_GPIO_Port, &gpio);

  gpio.Pin = ULPI_DIR_Pin | ULPI_NXT_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &gpio);

  gpio.Pin = ULPI_D0_Pin | ULPI_CLK_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = ULPI_D1_Pin | ULPI_D2_Pin | ULPI_D3_Pin | ULPI_D4_Pin |
             ULPI_D5_Pin | ULPI_D6_Pin | ULPI_D7_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  HAL_GPIO_Init(GPIOB, &gpio);
}

static void USBHSDebug_ResetPhy(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOE_CLK_ENABLE();

  HAL_GPIO_WritePin(USB_RESET_GPIO_Port, USB_RESET_Pin, GPIO_PIN_RESET);
  gpio.Pin = USB_RESET_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USB_RESET_GPIO_Port, &gpio);

  USBHSDebug_PrintUlpiPins("pins while PHY reset asserted");
  HAL_Delay(10U);
  HAL_GPIO_WritePin(USB_RESET_GPIO_Port, USB_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(10U);
}

static uint8_t USBHSDebug_BringUpPhyClock(void)
{
  uint32_t transitions;

  for (uint32_t attempt = 1U; attempt <= 3U; ++attempt)
  {
    USBHSDebug_ResetPhy();
    HAL_Delay(50U);
    USBHSDebug_PrintUlpiPins("pins after PHY reset release");
    USBHSDebug_PrintUlpiActivity("post-reset ULPI activity", 5U);
    transitions = USBHSDebug_CountUlpiClockTransitions(2U);
    printf("USB HS DEBUG: ULPI_CLK transitions in 2 ms attempt=%lu count=%lu\r\n",
           (unsigned long)attempt,
           (unsigned long)transitions);

    if (transitions != 0U)
    {
      return 1U;
    }
  }

  printf("USB HS DEBUG: trying STP wake pulse after reset attempts\r\n");
  USBHSDebug_PulseStpWake();
  HAL_Delay(20U);
  USBHSDebug_PrintUlpiPins("pins after STP wake");
  USBHSDebug_PrintUlpiActivity("post-STP-wake ULPI activity", 5U);
  transitions = USBHSDebug_CountUlpiClockTransitions(2U);
  printf("USB HS DEBUG: ULPI_CLK transitions in 2 ms after STP wake count=%lu\r\n",
         (unsigned long)transitions);
  if (transitions != 0U)
  {
    return 1U;
  }

  return 0U;
}

static void USBHSDebug_PulseStpWake(void)
{
  HAL_GPIO_WritePin(ULP_STP_GPIO_Port, ULP_STP_Pin, GPIO_PIN_SET);
  HAL_Delay(2U);
  HAL_GPIO_WritePin(ULP_STP_GPIO_Port, ULP_STP_Pin, GPIO_PIN_RESET);
}

static uint32_t USBHSDebug_CountUlpiClockTransitions(uint32_t sample_ms)
{
  GPIO_InitTypeDef gpio = {0};
  uint32_t start;
  uint32_t transitions = 0U;
  GPIO_PinState last;

  __HAL_RCC_GPIOA_CLK_ENABLE();

  gpio.Pin = ULPI_CLK_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(ULPI_CLK_GPIO_Port, &gpio);

  last = HAL_GPIO_ReadPin(ULPI_CLK_GPIO_Port, ULPI_CLK_Pin);
  start = HAL_GetTick();
  while ((HAL_GetTick() - start) < sample_ms)
  {
    GPIO_PinState now = HAL_GPIO_ReadPin(ULPI_CLK_GPIO_Port, ULPI_CLK_Pin);
    if (now != last)
    {
      ++transitions;
      last = now;
    }
  }

  return transitions;
}

static uint8_t USBHSDebug_ReadUlpiDataBus(void)
{
  uint8_t value = 0U;

  value |= (HAL_GPIO_ReadPin(ULPI_D0_GPIO_Port, ULPI_D0_Pin) == GPIO_PIN_SET) ? (1U << 0) : 0U;
  value |= (HAL_GPIO_ReadPin(ULPI_D1_GPIO_Port, ULPI_D1_Pin) == GPIO_PIN_SET) ? (1U << 1) : 0U;
  value |= (HAL_GPIO_ReadPin(ULPI_D2_GPIO_Port, ULPI_D2_Pin) == GPIO_PIN_SET) ? (1U << 2) : 0U;
  value |= (HAL_GPIO_ReadPin(ULPI_D3_GPIO_Port, ULPI_D3_Pin) == GPIO_PIN_SET) ? (1U << 3) : 0U;
  value |= (HAL_GPIO_ReadPin(ULPI_D4_GPIO_Port, ULPI_D4_Pin) == GPIO_PIN_SET) ? (1U << 4) : 0U;
  value |= (HAL_GPIO_ReadPin(ULPI_D5_GPIO_Port, ULPI_D5_Pin) == GPIO_PIN_SET) ? (1U << 5) : 0U;
  value |= (HAL_GPIO_ReadPin(ULPI_D6_GPIO_Port, ULPI_D6_Pin) == GPIO_PIN_SET) ? (1U << 6) : 0U;
  value |= (HAL_GPIO_ReadPin(ULPI_D7_GPIO_Port, ULPI_D7_Pin) == GPIO_PIN_SET) ? (1U << 7) : 0U;

  return value;
}

static void USBHSDebug_PrintUlpiPins(const char *tag)
{
  printf("USB HS PHY: %s STP=%lu DIR=%lu NXT=%lu CLK=%lu DATA=0x%02lX RESETB=%lu\r\n",
         tag,
         (unsigned long)HAL_GPIO_ReadPin(ULP_STP_GPIO_Port, ULP_STP_Pin),
         (unsigned long)HAL_GPIO_ReadPin(ULPI_DIR_GPIO_Port, ULPI_DIR_Pin),
         (unsigned long)HAL_GPIO_ReadPin(ULPI_NXT_GPIO_Port, ULPI_NXT_Pin),
         (unsigned long)HAL_GPIO_ReadPin(ULPI_CLK_GPIO_Port, ULPI_CLK_Pin),
         (unsigned long)USBHSDebug_ReadUlpiDataBus(),
         (unsigned long)HAL_GPIO_ReadPin(USB_RESET_GPIO_Port, USB_RESET_Pin));
}

static void USBHSDebug_PrintUlpiActivity(const char *tag, uint32_t sample_ms)
{
  uint32_t start = HAL_GetTick();
  uint32_t samples = 0U;
  uint32_t clk_edges = 0U;
  uint32_t dir_edges = 0U;
  uint32_t nxt_edges = 0U;
  uint32_t data_changes = 0U;
  GPIO_PinState last_clk = HAL_GPIO_ReadPin(ULPI_CLK_GPIO_Port, ULPI_CLK_Pin);
  GPIO_PinState last_dir = HAL_GPIO_ReadPin(ULPI_DIR_GPIO_Port, ULPI_DIR_Pin);
  GPIO_PinState last_nxt = HAL_GPIO_ReadPin(ULPI_NXT_GPIO_Port, ULPI_NXT_Pin);
  uint8_t last_data = USBHSDebug_ReadUlpiDataBus();

  while ((HAL_GetTick() - start) < sample_ms)
  {
    GPIO_PinState clk = HAL_GPIO_ReadPin(ULPI_CLK_GPIO_Port, ULPI_CLK_Pin);
    GPIO_PinState dir = HAL_GPIO_ReadPin(ULPI_DIR_GPIO_Port, ULPI_DIR_Pin);
    GPIO_PinState nxt = HAL_GPIO_ReadPin(ULPI_NXT_GPIO_Port, ULPI_NXT_Pin);
    uint8_t data = USBHSDebug_ReadUlpiDataBus();

    if (clk != last_clk)
    {
      ++clk_edges;
      last_clk = clk;
    }
    if (dir != last_dir)
    {
      ++dir_edges;
      last_dir = dir;
    }
    if (nxt != last_nxt)
    {
      ++nxt_edges;
      last_nxt = nxt;
    }
    if (data != last_data)
    {
      ++data_changes;
      last_data = data;
    }

    ++samples;
  }

  printf("USB HS PHY: %s %lu ms samples=%lu clk_edges=%lu dir_edges=%lu nxt_edges=%lu data_changes=%lu final_DATA=0x%02lX\r\n",
         tag,
         (unsigned long)sample_ms,
         (unsigned long)samples,
         (unsigned long)clk_edges,
         (unsigned long)dir_edges,
         (unsigned long)nxt_edges,
         (unsigned long)data_changes,
         (unsigned long)last_data);
}

static uint8_t USBHSDebug_WaitUlpiReady(void)
{
  uint32_t timeout = USB_HS_GPVNDCTL_TIMEOUT;

  while ((USB_HS_GPVNDCTL & USB_HS_GPVNDCTL_VSTSBUSY) != 0U)
  {
    if (timeout == 0U)
    {
      return 0U;
    }
    --timeout;
  }

  return 1U;
}

static uint8_t USBHSDebug_WaitUlpiDone(uint32_t *final_status)
{
  uint32_t timeout = USB_HS_GPVNDCTL_TIMEOUT;

  while (timeout != 0U)
  {
    uint32_t status = USB_HS_GPVNDCTL;
    if (((status & USB_HS_GPVNDCTL_NEWREGREQ) == 0U) &&
        ((status & USB_HS_GPVNDCTL_VSTSBUSY) == 0U) &&
        ((status & USB_HS_GPVNDCTL_VSTSDONE) != 0U))
    {
      *final_status = status;
      return 1U;
    }
    --timeout;
  }

  *final_status = USB_HS_GPVNDCTL;
  return 0U;
}

static uint8_t USBHSDebug_ReadUlpiRegister(uint8_t reg, uint8_t *value)
{
  uint32_t status;

  if (USBHSDebug_WaitUlpiReady() == 0U)
  {
    return 0U;
  }

  USB_HS_GPVNDCTL = USB_HS_GPVNDCTL_REGADDR(reg) |
                    USB_HS_GPVNDCTL_NEWREGREQ;

  if (USBHSDebug_WaitUlpiDone(&status) == 0U)
  {
    return 0U;
  }

  *value = USB_HS_GPVNDCTL_RDATA(status);
  return 1U;
}

static uint8_t USBHSDebug_WriteUlpiRegister(uint8_t reg, uint8_t value)
{
  uint32_t status;

  if (USBHSDebug_WaitUlpiReady() == 0U)
  {
    return 0U;
  }

  USB_HS_GPVNDCTL = USB_HS_GPVNDCTL_REGADDR(reg) |
                    USB_HS_GPVNDCTL_REGDATA(value) |
                    USB_HS_GPVNDCTL_REGWR |
                    USB_HS_GPVNDCTL_NEWREGREQ;

  return USBHSDebug_WaitUlpiDone(&status);
}

static void USBHSDebug_ConfigureUsb3317DeviceMode(void)
{
  uint8_t iface;
  uint8_t otg;
  uint8_t int_status;

  printf("USB HS PHY: configuring USB3317 peripheral VBUS/session mode\r\n");

  /*
   * USB3317 ties EXTVBUS high internally. Passing that indicator through makes
   * VbusValid deterministic even if the internal comparator state is confusing.
   */
  if ((USBHSDebug_WriteUlpiRegister(ULPI_REG_INTERFACE_CONTROL_CLR,
                                    ULPI_IFACE_INDICATOR_COMPLEMENT) == 0U) ||
      (USBHSDebug_WriteUlpiRegister(ULPI_REG_INTERFACE_CONTROL_SET,
                                    ULPI_IFACE_INDICATOR_PASSTHRU) == 0U))
  {
    printf("USB HS PHY: interface-control VBUS setup failed GPVNDCTL=0x%08lX\r\n",
           (unsigned long)USB_HS_GPVNDCTL);
    return;
  }

  if ((USBHSDebug_WriteUlpiRegister(ULPI_REG_OTG_CONTROL_CLR,
                                    ULPI_OTG_DP_PULLDOWN |
                                    ULPI_OTG_DM_PULLDOWN |
                                    ULPI_OTG_DISCHARGE_VBUS |
                                    ULPI_OTG_CHARGE_VBUS |
                                    ULPI_OTG_DRV_VBUS |
                                    ULPI_OTG_DRV_VBUS_EXTERNAL) == 0U) ||
      (USBHSDebug_WriteUlpiRegister(ULPI_REG_OTG_CONTROL_SET,
                                    ULPI_OTG_USE_EXTERNAL_VBUS) == 0U))
  {
    printf("USB HS PHY: OTG-control VBUS setup failed GPVNDCTL=0x%08lX\r\n",
           (unsigned long)USB_HS_GPVNDCTL);
    return;
  }

  if ((USBHSDebug_ReadUlpiRegister(ULPI_REG_INTERFACE_CONTROL, &iface) != 0U) &&
      (USBHSDebug_ReadUlpiRegister(ULPI_REG_OTG_CONTROL, &otg) != 0U) &&
      (USBHSDebug_ReadUlpiRegister(ULPI_REG_USB_INT_STATUS, &int_status) != 0U))
  {
    printf("USB HS PHY: device-mode regs IFACE=0x%02lX OTG=0x%02lX INT_ST=0x%02lX\r\n",
           (unsigned long)iface,
           (unsigned long)otg,
           (unsigned long)int_status);
  }
}

static void USBHSDebug_PrintUlpiRegisters(void)
{
  uint8_t vid_l;
  uint8_t vid_h;
  uint8_t pid_l;
  uint8_t pid_h;
  uint8_t func_ctl;
  uint8_t iface_ctl;
  uint8_t otg_ctl;
  uint8_t int_status;
  uint8_t int_latch;
  uint8_t debug;
  uint8_t scratch_original;
  uint8_t scratch_a5;
  uint8_t scratch_5a;
  uint8_t failed_reg = 0xFFU;

  if (USBHSDebug_ReadUlpiRegister(ULPI_REG_VENDOR_ID_LOW, &vid_l) == 0U) { failed_reg = ULPI_REG_VENDOR_ID_LOW; }
  else if (USBHSDebug_ReadUlpiRegister(ULPI_REG_VENDOR_ID_HIGH, &vid_h) == 0U) { failed_reg = ULPI_REG_VENDOR_ID_HIGH; }
  else if (USBHSDebug_ReadUlpiRegister(ULPI_REG_PRODUCT_ID_LOW, &pid_l) == 0U) { failed_reg = ULPI_REG_PRODUCT_ID_LOW; }
  else if (USBHSDebug_ReadUlpiRegister(ULPI_REG_PRODUCT_ID_HIGH, &pid_h) == 0U) { failed_reg = ULPI_REG_PRODUCT_ID_HIGH; }
  else if (USBHSDebug_ReadUlpiRegister(ULPI_REG_FUNCTION_CONTROL, &func_ctl) == 0U) { failed_reg = ULPI_REG_FUNCTION_CONTROL; }
  else if (USBHSDebug_ReadUlpiRegister(ULPI_REG_INTERFACE_CONTROL, &iface_ctl) == 0U) { failed_reg = ULPI_REG_INTERFACE_CONTROL; }
  else if (USBHSDebug_ReadUlpiRegister(ULPI_REG_OTG_CONTROL, &otg_ctl) == 0U) { failed_reg = ULPI_REG_OTG_CONTROL; }
  else if (USBHSDebug_ReadUlpiRegister(ULPI_REG_USB_INT_STATUS, &int_status) == 0U) { failed_reg = ULPI_REG_USB_INT_STATUS; }
  else if (USBHSDebug_ReadUlpiRegister(ULPI_REG_USB_INT_LATCH, &int_latch) == 0U) { failed_reg = ULPI_REG_USB_INT_LATCH; }
  else if (USBHSDebug_ReadUlpiRegister(ULPI_REG_DEBUG, &debug) == 0U) { failed_reg = ULPI_REG_DEBUG; }
  else
  {
  }

  if (failed_reg != 0xFFU)
  {
    printf("USB HS PHY: ULPI register read failed at reg 0x%02lX GPVNDCTL=0x%08lX\r\n",
           (unsigned long)failed_reg,
           (unsigned long)USB_HS_GPVNDCTL);
    return;
  }

  printf("USB HS PHY: ULPI ID VID=0x%02lX%02lX PID=0x%02lX%02lX\r\n",
         (unsigned long)vid_h,
         (unsigned long)vid_l,
         (unsigned long)pid_h,
         (unsigned long)pid_l);
  printf("USB HS PHY: ULPI regs FUNC=0x%02lX IFACE=0x%02lX OTG=0x%02lX INT_ST=0x%02lX INT_LATCH=0x%02lX DEBUG=0x%02lX\r\n",
         (unsigned long)func_ctl,
         (unsigned long)iface_ctl,
         (unsigned long)otg_ctl,
         (unsigned long)int_status,
         (unsigned long)int_latch,
         (unsigned long)debug);

  if (USBHSDebug_ReadUlpiRegister(ULPI_REG_SCRATCH, &scratch_original) == 0U)
  {
    printf("USB HS PHY: ULPI scratch read failed GPVNDCTL=0x%08lX\r\n",
           (unsigned long)USB_HS_GPVNDCTL);
    return;
  }

  if ((USBHSDebug_WriteUlpiRegister(ULPI_REG_SCRATCH, 0xA5U) == 0U) ||
      (USBHSDebug_ReadUlpiRegister(ULPI_REG_SCRATCH, &scratch_a5) == 0U) ||
      (USBHSDebug_WriteUlpiRegister(ULPI_REG_SCRATCH, 0x5AU) == 0U) ||
      (USBHSDebug_ReadUlpiRegister(ULPI_REG_SCRATCH, &scratch_5a) == 0U))
  {
    printf("USB HS PHY: ULPI scratch test failed GPVNDCTL=0x%08lX\r\n",
           (unsigned long)USB_HS_GPVNDCTL);
    return;
  }

  (void)USBHSDebug_WriteUlpiRegister(ULPI_REG_SCRATCH, scratch_original);

  printf("USB HS PHY: ULPI scratch original=0x%02lX A5->0x%02lX 5A->0x%02lX %s\r\n",
         (unsigned long)scratch_original,
         (unsigned long)scratch_a5,
         (unsigned long)scratch_5a,
         ((scratch_a5 == 0xA5U) && (scratch_5a == 0x5AU)) ? "PASS" : "FAIL");
}

static void USBHSDebug_PrintUlpiRuntimeStatus(const char *tag)
{
  uint8_t func_ctl;
  uint8_t iface_ctl;
  uint8_t otg_ctl;
  uint8_t int_status;
  uint8_t int_latch;
  uint8_t debug;

  if ((USBHSDebug_ReadUlpiRegister(ULPI_REG_FUNCTION_CONTROL, &func_ctl) == 0U) ||
      (USBHSDebug_ReadUlpiRegister(ULPI_REG_INTERFACE_CONTROL, &iface_ctl) == 0U) ||
      (USBHSDebug_ReadUlpiRegister(ULPI_REG_OTG_CONTROL, &otg_ctl) == 0U) ||
      (USBHSDebug_ReadUlpiRegister(ULPI_REG_USB_INT_STATUS, &int_status) == 0U) ||
      (USBHSDebug_ReadUlpiRegister(ULPI_REG_USB_INT_LATCH, &int_latch) == 0U) ||
      (USBHSDebug_ReadUlpiRegister(ULPI_REG_DEBUG, &debug) == 0U))
  {
    printf("USB HS PHY: %s runtime register read failed GPVNDCTL=0x%08lX\r\n",
           tag,
           (unsigned long)USB_HS_GPVNDCTL);
    return;
  }

  printf("USB HS PHY: %s FUNC=0x%02lX IFACE=0x%02lX OTG=0x%02lX INT_ST=0x%02lX INT_LATCH=0x%02lX DEBUG=0x%02lX LINESTATE=%lu VBUS=%lu SESS=%lu END=%lu\r\n",
         tag,
         (unsigned long)func_ctl,
         (unsigned long)iface_ctl,
         (unsigned long)otg_ctl,
         (unsigned long)int_status,
         (unsigned long)int_latch,
         (unsigned long)debug,
         (unsigned long)(debug & 0x03U),
         (unsigned long)((int_status >> 1) & 0x01U),
         (unsigned long)((int_status >> 2) & 0x01U),
         (unsigned long)((int_status >> 3) & 0x01U));
}

static void USBHSDebug_RunForcedFsAttachProbe(void)
{
  printf("USB HS PHY: forced FS attach probe for %lu ms; probe DP/DN now\r\n",
         (unsigned long)USB_HS_FORCE_ATTACH_PROBE_MS);

  USBHSDebug_PrintUlpiRuntimeStatus("before forced FS attach");

  if (USBHSDebug_WriteUlpiRegister(ULPI_REG_FUNCTION_CONTROL, ULPI_FUNC_FS_ATTACH) == 0U)
  {
    printf("USB HS PHY: forced FS attach write failed GPVNDCTL=0x%08lX\r\n",
           (unsigned long)USB_HS_GPVNDCTL);
    return;
  }

  USBHSDebug_PrintUlpiRuntimeStatus("forced FS attach start");
  HAL_Delay(USB_HS_FORCE_ATTACH_PROBE_MS);
  USBHSDebug_PrintUlpiRuntimeStatus("forced FS attach end");

  if (USBHSDebug_WriteUlpiRegister(ULPI_REG_FUNCTION_CONTROL, ULPI_FUNC_NONDRIVING) == 0U)
  {
    printf("USB HS PHY: forced FS detach write failed GPVNDCTL=0x%08lX\r\n",
           (unsigned long)USB_HS_GPVNDCTL);
  }
  else
  {
    USBHSDebug_PrintUlpiRuntimeStatus("after forced FS detach");
  }
}

static void USBHSDebug_RunWeakPullupProbe(void)
{
  uint8_t usb_io_pwr;

  printf("USB HS PHY: weak DP/DM pull-up probe; watch DP/DN at the IC pins\r\n");

  (void)USBHSDebug_WriteUlpiRegister(ULPI_REG_FUNCTION_CONTROL, ULPI_FUNC_NONDRIVING);
  (void)USBHSDebug_WriteUlpiRegister(ULPI_REG_USB_IO_PWR_CLR,
                                     ULPI_USB_IO_PWR_CHG_DP | ULPI_USB_IO_PWR_CHG_DM);

  if (USBHSDebug_ReadUlpiRegister(ULPI_REG_USB_IO_PWR, &usb_io_pwr) != 0U)
  {
    printf("USB HS PHY: weak pull-up start USB_IO_PWR=0x%02lX\r\n",
           (unsigned long)usb_io_pwr);
  }

  printf("USB HS PHY: weak DP pull-up for %lu ms\r\n",
         (unsigned long)USB_HS_WEAK_PULLUP_PROBE_MS);
  (void)USBHSDebug_WriteUlpiRegister(ULPI_REG_USB_IO_PWR_SET, ULPI_USB_IO_PWR_CHG_DP);
  HAL_Delay(USB_HS_WEAK_PULLUP_PROBE_MS);

  printf("USB HS PHY: weak DM pull-up for %lu ms\r\n",
         (unsigned long)USB_HS_WEAK_PULLUP_PROBE_MS);
  (void)USBHSDebug_WriteUlpiRegister(ULPI_REG_USB_IO_PWR_CLR, ULPI_USB_IO_PWR_CHG_DP);
  (void)USBHSDebug_WriteUlpiRegister(ULPI_REG_USB_IO_PWR_SET, ULPI_USB_IO_PWR_CHG_DM);
  HAL_Delay(USB_HS_WEAK_PULLUP_PROBE_MS);

  printf("USB HS PHY: weak DP+DM pull-ups for %lu ms\r\n",
         (unsigned long)USB_HS_WEAK_PULLUP_PROBE_MS);
  (void)USBHSDebug_WriteUlpiRegister(ULPI_REG_USB_IO_PWR_SET,
                                     ULPI_USB_IO_PWR_CHG_DP | ULPI_USB_IO_PWR_CHG_DM);
  HAL_Delay(USB_HS_WEAK_PULLUP_PROBE_MS);

  (void)USBHSDebug_WriteUlpiRegister(ULPI_REG_USB_IO_PWR_CLR,
                                     ULPI_USB_IO_PWR_CHG_DP | ULPI_USB_IO_PWR_CHG_DM);
  if (USBHSDebug_ReadUlpiRegister(ULPI_REG_USB_IO_PWR, &usb_io_pwr) != 0U)
  {
    printf("USB HS PHY: weak pull-up end USB_IO_PWR=0x%02lX\r\n",
           (unsigned long)usb_io_pwr);
  }
}

static void USBHSDebug_ResetUsbCorePeripheral(void)
{
  __HAL_RCC_USB_OTG_HS_FORCE_RESET();
  HAL_Delay(1U);
  __HAL_RCC_USB_OTG_HS_RELEASE_RESET();
  HAL_Delay(1U);
}

static void USBHSDebug_PrintUsbCorePreflight(const char *tag)
{
  printf("USB HS DEBUG: %s AHB1ENR=0x%08lX AHB1RSTR=0x%08lX GUSBCFG=0x%08lX GRSTCTL=0x%08lX GINTSTS=0x%08lX GCCFG=0x%08lX GPVNDCTL=0x%08lX\r\n",
         tag,
         (unsigned long)RCC->AHB1ENR,
         (unsigned long)RCC->AHB1RSTR,
         (unsigned long)USB_OTG_HS->GUSBCFG,
         (unsigned long)USB_OTG_HS->GRSTCTL,
         (unsigned long)USB_OTG_HS->GINTSTS,
         (unsigned long)USB_OTG_HS->GCCFG,
         (unsigned long)USB_HS_GPVNDCTL);
}

static void USBHSDebug_PrintCoreStatus(const char *tag)
{
  uint32_t USBx_BASE = (uint32_t)USB_OTG_HS;

  printf("USB HS DEBUG: %s tick=%lu cfg=%lu speed=%s GINTSTS=0x%08lX GINTMSK=0x%08lX GAHBCFG=0x%08lX GUSBCFG=0x%08lX GCCFG=0x%08lX DCTL=0x%08lX DSTS=0x%08lX PCGCCTL=0x%08lX\r\n",
         tag,
         (unsigned long)HAL_GetTick(),
         (unsigned long)usb_hs_debug_configuration,
         USBHSDebug_SpeedName(usb_hs_debug_pcd),
         (unsigned long)USB_OTG_HS->GINTSTS,
         (unsigned long)USB_OTG_HS->GINTMSK,
         (unsigned long)USB_OTG_HS->GAHBCFG,
         (unsigned long)USB_OTG_HS->GUSBCFG,
         (unsigned long)USB_OTG_HS->GCCFG,
         (unsigned long)USBx_DEVICE->DCTL,
         (unsigned long)USBx_DEVICE->DSTS,
         (unsigned long)USBx_PCGCCTL);
}

static void USBHSDebug_ParseSetup(PCD_HandleTypeDef *hpcd,
                                  USBHSDebug_SetupRequest *req)
{
  req->bmRequestType = (uint8_t)(hpcd->Setup[0] & 0xFFU);
  req->bRequest = (uint8_t)((hpcd->Setup[0] >> 8) & 0xFFU);
  req->wValue = (uint16_t)((hpcd->Setup[0] >> 16) & 0xFFFFU);
  req->wIndex = (uint16_t)(hpcd->Setup[1] & 0xFFFFU);
  req->wLength = (uint16_t)((hpcd->Setup[1] >> 16) & 0xFFFFU);
}

static void USBHSDebug_HandleStandardRequest(PCD_HandleTypeDef *hpcd,
                                             const USBHSDebug_SetupRequest *req)
{
  uint8_t desc_type = (uint8_t)(req->wValue >> 8);
  uint8_t desc_index = (uint8_t)(req->wValue & 0xFFU);

  switch (req->bRequest)
  {
    case USB_REQ_GET_DESCRIPTOR:
      if (desc_type == USB_DESC_TYPE_DEVICE)
      {
        USBHSDebug_SendControlData(hpcd,
                                   usb_hs_debug_device_desc,
                                   sizeof(usb_hs_debug_device_desc),
                                   req->wLength);
      }
      else if (desc_type == USB_DESC_TYPE_CONFIGURATION)
      {
        USBHSDebug_BuildConfigDescriptor(USB_DESC_TYPE_CONFIGURATION,
                                         USBHSDebug_BulkMps(hpcd));
        USBHSDebug_SendControlData(hpcd,
                                   usb_hs_debug_ep0_buffer,
                                   25U,
                                   req->wLength);
      }
      else if (desc_type == USB_DESC_TYPE_OTHER_SPEED)
      {
        USBHSDebug_BuildConfigDescriptor(USB_DESC_TYPE_OTHER_SPEED,
                                         USB_HS_DEBUG_FS_BULK_MPS);
        USBHSDebug_SendControlData(hpcd,
                                   usb_hs_debug_ep0_buffer,
                                   25U,
                                   req->wLength);
      }
      else if (desc_type == USB_DESC_TYPE_DEVICE_QUALIFIER)
      {
        USBHSDebug_SendControlData(hpcd,
                                   usb_hs_debug_device_qualifier_desc,
                                   sizeof(usb_hs_debug_device_qualifier_desc),
                                   req->wLength);
      }
      else if (desc_type == USB_DESC_TYPE_STRING)
      {
        if (desc_index == 0U)
        {
          USBHSDebug_SendControlData(hpcd,
                                     usb_hs_debug_string_lang,
                                     sizeof(usb_hs_debug_string_lang),
                                     req->wLength);
        }
        else if (desc_index == 1U)
        {
          USBHSDebug_SendControlData(hpcd,
                                     usb_hs_debug_string_manufacturer,
                                     sizeof(usb_hs_debug_string_manufacturer),
                                     req->wLength);
        }
        else if (desc_index == 2U)
        {
          USBHSDebug_SendControlData(hpcd,
                                     usb_hs_debug_string_product,
                                     sizeof(usb_hs_debug_string_product),
                                     req->wLength);
        }
        else if (desc_index == 3U)
        {
          USBHSDebug_SendControlData(hpcd,
                                     usb_hs_debug_string_serial,
                                     sizeof(usb_hs_debug_string_serial),
                                     req->wLength);
        }
        else if (desc_index == 0xEEU)
        {
          USBHSDebug_SendControlData(hpcd,
                                     usb_hs_debug_ms_os_string,
                                     sizeof(usb_hs_debug_ms_os_string),
                                     req->wLength);
        }
        else
        {
          USBHSDebug_StallControl(hpcd);
        }
      }
      else
      {
        USBHSDebug_StallControl(hpcd);
      }
      break;

    case USB_REQ_SET_ADDRESS:
      usb_hs_debug_pending_address = (uint8_t)(req->wValue & 0x7FU);
      usb_hs_debug_has_pending_address = 0U;
      (void)HAL_PCD_SetAddress(hpcd, usb_hs_debug_pending_address);
      printf("USB HS DEBUG: set address %lu\r\n",
             (unsigned long)usb_hs_debug_pending_address);
      USBHSDebug_SendStatus(hpcd);
      break;

    case USB_REQ_SET_CONFIGURATION:
      if ((req->wValue == 0U) || (req->wValue == 1U))
      {
        usb_hs_debug_configuration = (uint8_t)req->wValue;
        usb_hs_debug_tx_busy = 0U;
        usb_hs_debug_stream_enabled = 0U;
        usb_hs_debug_sequence = 0U;

        if (usb_hs_debug_configuration != 0U)
        {
          USBHSDebug_OpenBulkIn(hpcd);
          usb_hs_debug_stream_enabled = 1U;
        }
        else
        {
          USBHSDebug_CloseBulkIn(hpcd);
        }

        USBHSDebug_SendStatus(hpcd);
      }
      else
      {
        USBHSDebug_StallControl(hpcd);
      }
      break;

    case USB_REQ_GET_CONFIGURATION:
      usb_hs_debug_ep0_buffer[0] = usb_hs_debug_configuration;
      USBHSDebug_SendControlData(hpcd, usb_hs_debug_ep0_buffer, 1U, req->wLength);
      break;

    case USB_REQ_GET_STATUS:
      usb_hs_debug_ep0_buffer[0] = 0U;
      usb_hs_debug_ep0_buffer[1] = 0U;
      USBHSDebug_SendControlData(hpcd, usb_hs_debug_ep0_buffer, 2U, req->wLength);
      break;

    case USB_REQ_CLEAR_FEATURE:
    case USB_REQ_SET_FEATURE:
    case USB_REQ_SET_INTERFACE:
      USBHSDebug_SendStatus(hpcd);
      break;

    case USB_REQ_GET_INTERFACE:
      usb_hs_debug_ep0_buffer[0] = 0U;
      USBHSDebug_SendControlData(hpcd, usb_hs_debug_ep0_buffer, 1U, req->wLength);
      break;

    default:
      USBHSDebug_StallControl(hpcd);
      break;
  }
}

static void USBHSDebug_HandleVendorRequest(PCD_HandleTypeDef *hpcd,
                                           const USBHSDebug_SetupRequest *req)
{
  if (((req->bmRequestType & 0x80U) != 0U) &&
      (req->bRequest == USB_HS_DEBUG_MS_VENDOR_CODE) &&
      (req->wIndex == 0x0004U))
  {
    USBHSDebug_SendControlData(hpcd,
                               usb_hs_debug_ms_compat_id_desc,
                               sizeof(usb_hs_debug_ms_compat_id_desc),
                               req->wLength);
  }
  else
  {
    USBHSDebug_StallControl(hpcd);
  }
}

static void USBHSDebug_SendControlData(PCD_HandleTypeDef *hpcd,
                                       const uint8_t *data,
                                       uint16_t data_len,
                                       uint16_t requested_len)
{
  uint16_t send_len = data_len;

  if (requested_len < send_len)
  {
    send_len = requested_len;
  }

  usb_hs_debug_ep0_state = USB_HS_DEBUG_EP0_DATA_IN;
  (void)HAL_PCD_EP_Transmit(hpcd, 0x80U, (uint8_t *)data, send_len);
}

static void USBHSDebug_SendStatus(PCD_HandleTypeDef *hpcd)
{
  usb_hs_debug_ep0_state = USB_HS_DEBUG_EP0_STATUS_IN;
  (void)HAL_PCD_EP_Transmit(hpcd, 0x80U, usb_hs_debug_ep0_buffer, 0U);
}

static void USBHSDebug_ReceiveStatusOut(PCD_HandleTypeDef *hpcd)
{
  usb_hs_debug_ep0_state = USB_HS_DEBUG_EP0_STATUS_OUT;
  (void)HAL_PCD_EP_Receive(hpcd, 0x00U, usb_hs_debug_ep0_buffer, 0U);
}

static void USBHSDebug_StallControl(PCD_HandleTypeDef *hpcd)
{
  usb_hs_debug_ep0_state = USB_HS_DEBUG_EP0_IDLE;
  (void)HAL_PCD_EP_SetStall(hpcd, 0x80U);
  (void)HAL_PCD_EP_SetStall(hpcd, 0x00U);
}

static uint16_t USBHSDebug_BulkMps(const PCD_HandleTypeDef *hpcd)
{
  return (hpcd->Init.speed == PCD_SPEED_HIGH) ?
         USB_HS_DEBUG_HS_BULK_MPS :
         USB_HS_DEBUG_FS_BULK_MPS;
}

static void USBHSDebug_BuildConfigDescriptor(uint8_t descriptor_type,
                                             uint16_t bulk_mps)
{
  usb_hs_debug_ep0_buffer[0] = 0x09U;
  usb_hs_debug_ep0_buffer[1] = descriptor_type;
  usb_hs_debug_ep0_buffer[2] = 25U;
  usb_hs_debug_ep0_buffer[3] = 0x00U;
  usb_hs_debug_ep0_buffer[4] = 0x01U;
  usb_hs_debug_ep0_buffer[5] = 0x01U;
  usb_hs_debug_ep0_buffer[6] = 0x00U;
  usb_hs_debug_ep0_buffer[7] = 0x80U;
  usb_hs_debug_ep0_buffer[8] = 50U;

  usb_hs_debug_ep0_buffer[9] = 0x09U;
  usb_hs_debug_ep0_buffer[10] = 0x04U;
  usb_hs_debug_ep0_buffer[11] = 0x00U;
  usb_hs_debug_ep0_buffer[12] = 0x00U;
  usb_hs_debug_ep0_buffer[13] = 0x01U;
  usb_hs_debug_ep0_buffer[14] = 0xFFU;
  usb_hs_debug_ep0_buffer[15] = 0x00U;
  usb_hs_debug_ep0_buffer[16] = 0x00U;
  usb_hs_debug_ep0_buffer[17] = 0x00U;

  usb_hs_debug_ep0_buffer[18] = 0x07U;
  usb_hs_debug_ep0_buffer[19] = 0x05U;
  usb_hs_debug_ep0_buffer[20] = USB_HS_DEBUG_EP_IN;
  usb_hs_debug_ep0_buffer[21] = 0x02U;
  usb_hs_debug_ep0_buffer[22] = (uint8_t)(bulk_mps & 0xFFU);
  usb_hs_debug_ep0_buffer[23] = (uint8_t)(bulk_mps >> 8);
  usb_hs_debug_ep0_buffer[24] = 0x00U;
}

static void USBHSDebug_OpenBulkIn(PCD_HandleTypeDef *hpcd)
{
  uint16_t mps = USBHSDebug_BulkMps(hpcd);

  (void)HAL_PCD_EP_Open(hpcd, USB_HS_DEBUG_EP_IN, mps, EP_TYPE_BULK);
  printf("USB HS DEBUG: configured %s bulk IN, MPS=%lu\r\n",
         USBHSDebug_SpeedName(hpcd),
         (unsigned long)mps);
}

static void USBHSDebug_CloseBulkIn(PCD_HandleTypeDef *hpcd)
{
  (void)HAL_PCD_EP_Close(hpcd, USB_HS_DEBUG_EP_IN);
}

static void USBHSDebug_FillTxBuffer(void)
{
  for (uint32_t i = 0U; i < USB_HS_DEBUG_TX_SIZE; ++i)
  {
    usb_hs_debug_tx_buffer[i] = (uint8_t)usb_hs_debug_sequence;
    ++usb_hs_debug_sequence;
  }
}

static void USBHSDebug_QueueNextPacket(void)
{
  if ((usb_hs_debug_pcd == NULL) ||
      (usb_hs_debug_configuration == 0U) ||
      (usb_hs_debug_tx_busy != 0U))
  {
    return;
  }

  USBHSDebug_FillTxBuffer();
  usb_hs_debug_tx_busy = 1U;
  if (HAL_PCD_EP_Transmit(usb_hs_debug_pcd,
                          USB_HS_DEBUG_EP_IN,
                          usb_hs_debug_tx_buffer,
                          USB_HS_DEBUG_TX_SIZE) != HAL_OK)
  {
    usb_hs_debug_tx_busy = 0U;
  }
}

static const char *USBHSDebug_SpeedName(const PCD_HandleTypeDef *hpcd)
{
  if (hpcd == NULL)
  {
    return "unknown";
  }

  if (hpcd->Init.speed == PCD_SPEED_HIGH)
  {
    return "high-speed";
  }

  if (hpcd->Init.speed == PCD_SPEED_FULL)
  {
    return "full-speed";
  }

  return "unknown-speed";
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
  USBHSDebug_SetupRequest req;

  if (hpcd != usb_hs_debug_pcd)
  {
    return;
  }

  USBHSDebug_ParseSetup(hpcd, &req);
  usb_hs_debug_ep0_state = USB_HS_DEBUG_EP0_IDLE;

  if (usb_hs_debug_setup_print_count < 12U)
  {
    ++usb_hs_debug_setup_print_count;
    printf("USB HS DEBUG: setup bm=0x%02lX b=0x%02lX value=0x%04lX index=0x%04lX len=%lu\r\n",
           (unsigned long)req.bmRequestType,
           (unsigned long)req.bRequest,
           (unsigned long)req.wValue,
           (unsigned long)req.wIndex,
           (unsigned long)req.wLength);
  }

  if ((req.bmRequestType & 0x60U) == 0x00U)
  {
    USBHSDebug_HandleStandardRequest(hpcd, &req);
  }
  else if ((req.bmRequestType & 0x60U) == 0x40U)
  {
    USBHSDebug_HandleVendorRequest(hpcd, &req);
  }
  else
  {
    USBHSDebug_StallControl(hpcd);
  }
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
  if (hpcd != usb_hs_debug_pcd)
  {
    return;
  }

  usb_hs_debug_configuration = 0U;
  usb_hs_debug_stream_enabled = 0U;
  usb_hs_debug_tx_busy = 0U;
  usb_hs_debug_sequence = 0U;
  usb_hs_debug_pending_address = 0U;
  usb_hs_debug_has_pending_address = 0U;
  usb_hs_debug_ep0_state = USB_HS_DEBUG_EP0_IDLE;
  usb_hs_debug_ep0_print_count = 0U;

  (void)HAL_PCD_EP_Open(hpcd, 0x00U, USB_HS_DEBUG_EP0_MPS, EP_TYPE_CTRL);
  (void)HAL_PCD_EP_Open(hpcd, 0x80U, USB_HS_DEBUG_EP0_MPS, EP_TYPE_CTRL);

  USBHSDebug_PrintCoreStatus("USB bus reset / enum done");
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  if (hpcd != usb_hs_debug_pcd)
  {
    return;
  }

  if (epnum == 0U)
  {
    if (usb_hs_debug_ep0_print_count < 8U)
    {
      ++usb_hs_debug_ep0_print_count;
      printf("USB HS DEBUG: ep0 IN complete state=%lu\r\n",
             (unsigned long)usb_hs_debug_ep0_state);
    }

    if (usb_hs_debug_has_pending_address != 0U)
    {
      (void)HAL_PCD_SetAddress(hpcd, usb_hs_debug_pending_address);
      usb_hs_debug_has_pending_address = 0U;
    }

    if (usb_hs_debug_ep0_state == USB_HS_DEBUG_EP0_DATA_IN)
    {
      USBHSDebug_ReceiveStatusOut(hpcd);
    }
    else
    {
      usb_hs_debug_ep0_state = USB_HS_DEBUG_EP0_IDLE;
    }
  }
  else if (epnum == (USB_HS_DEBUG_EP_IN & 0x7FU))
  {
    usb_hs_debug_completed_bytes += USB_HS_DEBUG_TX_SIZE;
    ++usb_hs_debug_completed_transfers;
    usb_hs_debug_tx_busy = 0U;
    USBHSDebug_QueueNextPacket();
  }
  else
  {
  }
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  if (hpcd != usb_hs_debug_pcd)
  {
    return;
  }

  if (epnum == 0U)
  {
    if (usb_hs_debug_ep0_print_count < 8U)
    {
      ++usb_hs_debug_ep0_print_count;
      printf("USB HS DEBUG: ep0 OUT complete state=%lu count=%lu\r\n",
             (unsigned long)usb_hs_debug_ep0_state,
             (unsigned long)HAL_PCD_EP_GetRxCount(hpcd, 0x00U));
    }

    if (usb_hs_debug_ep0_state == USB_HS_DEBUG_EP0_STATUS_OUT)
    {
      usb_hs_debug_ep0_state = USB_HS_DEBUG_EP0_IDLE;
    }
  }
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
{
  if (hpcd == usb_hs_debug_pcd)
  {
    printf("USB HS DEBUG: host connect detected\r\n");
  }
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
{
  if (hpcd == usb_hs_debug_pcd)
  {
    usb_hs_debug_configuration = 0U;
    usb_hs_debug_stream_enabled = 0U;
    usb_hs_debug_tx_busy = 0U;
    printf("USB HS DEBUG: host disconnect detected\r\n");
  }
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
{
  if (hpcd == usb_hs_debug_pcd)
  {
    printf("USB HS DEBUG: suspend\r\n");
  }
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
{
  if (hpcd == usb_hs_debug_pcd)
  {
    printf("USB HS DEBUG: resume\r\n");
  }
}
