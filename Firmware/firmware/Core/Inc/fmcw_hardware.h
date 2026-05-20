#ifndef FMCW_HARDWARE_H
#define FMCW_HARDWARE_H

#include <stdbool.h>
#include <stdint.h>

/* Configures the STM32 clock tree for the acquisition architecture. */
void fmcw_clock_config(void);

/* Applies the CubeMX board pin map and leaves RF outputs disabled until
 * fmcw_tx_service() observes TX_STDBY high.
 */
void fmcw_gpio_init(void);

/* Pulses the external USB3317 reset pin when the board exposes one. */
void fmcw_usb_phy_reset(void);

/* Initializes DCMI for 12-bit continuous capture. CAPTURE remains off until
 * TIM9 performs the first MUXOUT-aligned start.
 */
void fmcw_dcmi_init(void);

/* Initializes DMA2 Stream 1 Channel 1 in double-buffer mode for one
 * 5000-word DMA completion per FMCW slope.
 */
void fmcw_dma2_stream1_init(void);

/* Configures TIM9 on PE5/MUXOUT to do one-shot DCMI alignment and slope
 * heartbeat capture.
 */
void fmcw_tim9_init(void);
void fmcw_tim9_start(void);

/* Enables interrupts after all peripherals have been configured. */
void fmcw_nvic_init(void);

/* Reads TX_STDBY and mirrors it to MIXER_EN and PA_EN. */
void fmcw_tx_service(void);

/* Returns the most recent RF-chain enable state applied by fmcw_tx_service(). */
bool fmcw_tx_enabled(void);

/* Last-resort fault stop used when clock/peripheral bring-up cannot continue. */
void fmcw_error_trap(uint32_t code);

#endif
