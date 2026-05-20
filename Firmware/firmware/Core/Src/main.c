#include "main.h"

#include "fmcw_adf4158.h"
#include "fmcw_board_config.h"
#include "fmcw_capture.h"
#include "fmcw_hardware.h"
#include "fmcw_time.h"
#include "usb_stream.h"

int main(void)
{
  /* Bring up HAL, clocks, and the 1 us timebase used for frame timestamps and
   * watchdogs before any peripheral starts producing data.
   */
  HAL_Init();
  fmcw_clock_config();
  fmcw_time_init();

  /* Configure the capture state and CubeMX board pins. fmcw_gpio_init() also
   * samples TX_STDBY once so a normally-high TX-enable input immediately turns
   * on MIXER_EN and PA_EN.
   */
  fmcw_capture_state_init();
  fmcw_gpio_init();
  fmcw_usb_phy_reset();

  /* Build the hardware-paced acquisition path: DCMI latches the ADC bus, DMA2
   * writes one slope per buffer, USB streams ready buffers, and TIM9 performs
   * the first MUXOUT-aligned capture start.
   */
  fmcw_dcmi_init();
  fmcw_dma2_stream1_init();
  usb_stream_init();
  fmcw_tim9_init();
  fmcw_nvic_init();

#if FMCW_REQUIRE_USB_ENUMERATION_BEFORE_CAPTURE
  while (!usb_stream_ready()) {
    fmcw_tx_service();
    usb_stream_poll();
  }
#endif

#if FMCW_ENABLE_ADF4158_AUTOPROGRAM
  fmcw_adf4158_init();
  fmcw_adf4158_program_default();
#endif

  fmcw_tim9_start();

  while (1) {
    fmcw_tx_service();
    usb_stream_poll();
    fmcw_capture_watchdog_poll();
  }
}
