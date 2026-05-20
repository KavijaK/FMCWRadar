#ifndef FMCW_BOARD_CONFIG_H
#define FMCW_BOARD_CONFIG_H

#include "stm32f4xx_hal.h"

/* Capture policy */
#define FMCW_REQUIRE_USB_ENUMERATION_BEFORE_CAPTURE  1u
#define FMCW_ENABLE_ADF4158_AUTOPROGRAM              0u

/* USB identifiers. Replace before shipping a product. */
#define FMCW_USB_VID  0x0483u
#define FMCW_USB_PID  0x5740u
#define FMCW_USB_BCD  0x0100u

/* PA4 is driven low at boot, then high by the TIM9 CC2 alignment ISR.
 * The PCB routes this generated HSYNC signal back to PA7 for firmware-side
 * feedback/bring-up checks.
 */
#define FMCW_DCMI_HSYNC_GPIO_PORT GPIOA
#define FMCW_DCMI_HSYNC_PIN       GPIO_PIN_4
#define FMCW_DCMI_HSYNC_FEEDBACK_GPIO_PORT GPIOA
#define FMCW_DCMI_HSYNC_FEEDBACK_PIN       GPIO_PIN_7

/* CubeMX-imported board pins from:
 * C:\Users\karun\OneDrive\Desktop\FMCWRadarFirmware\FMCWRadarFirmware.ioc
 *
 * DCMI:
 * D0..D4 = PA9, PC7, PC8, PC9, PC11
 * D5..D7 = PD3, PB8, PB9
 * D8..D9 = PC10, PC12
 * D10..D11 = PD6, PD2
 * PIXCLK = PA6, VSYNC = PB7 tied low, MUXOUT = PE5 TIM9_CH1
 * HSYNC feedback = PA7 input, routed from PA4 on the PCB
 */

/* USB3317 ULPI defaults:
 * CK PA5, STP PC0, DIR PC2, NXT PC3,
 * D0 PA3, D1 PB0, D2 PB1, D3 PB10, D4 PB11, D5 PB12, D6 PB13, D7 PB5.
 */

/* USB3317 reset from CubeMX: USB_RESET on PE13, active low. */
#define FMCW_USB_PHY_RESET_ENABLED  1u
#define FMCW_USB_PHY_RESET_PORT     GPIOE
#define FMCW_USB_PHY_RESET_PIN      GPIO_PIN_13

/* RF transmit gate. TX_STDBY is the board's TX-enable input; high means the
 * firmware may enable the mixer and PA outputs.
 */
#define FMCW_TX_STDBY_PORT          GPIOE
#define FMCW_TX_STDBY_PIN           GPIO_PIN_11
#define FMCW_TX_STDBY_ACTIVE_STATE  GPIO_PIN_SET
#define FMCW_MIXER_EN_PORT          GPIOD
#define FMCW_MIXER_EN_PIN           GPIO_PIN_13
#define FMCW_PA_EN_PORT             GPIOE
#define FMCW_PA_EN_PIN              GPIO_PIN_0

/* Other CubeMX-imported control/status pins. */
#define FMCW_ADC_OF_PORT            GPIOD
#define FMCW_ADC_OF_PIN             GPIO_PIN_11
#define FMCW_ADC_GAIN_PORT          GPIOD
#define FMCW_ADC_GAIN_PIN           GPIO_PIN_12
#define FMCW_PDET_PORT              GPIOC
#define FMCW_PDET_PIN               GPIO_PIN_4
#define FMCW_MISC1_PORT             GPIOE
#define FMCW_MISC1_PIN              GPIO_PIN_7
#define FMCW_MISC2_PORT             GPIOE
#define FMCW_MISC2_PIN              GPIO_PIN_12
#define FMCW_PLL_TXDATA_PORT        GPIOA
#define FMCW_PLL_TXDATA_PIN         GPIO_PIN_2

/* ADF4158 SPI/control pins from CubeMX. PE5 is kept as TIM9_CH1 AF3 in this
 * firmware, even though the generated CubeMX file marks it as plain GPIO input;
 * TIM9 capture is required by the architecture for slope alignment.
 */
#define FMCW_ADF4158_SPI                 SPI4
#define FMCW_ADF4158_SPI_CLK_ENABLE()    __HAL_RCC_SPI4_CLK_ENABLE()
#define FMCW_ADF4158_SPI_FORCE_RESET()   __HAL_RCC_SPI4_FORCE_RESET()
#define FMCW_ADF4158_SPI_RELEASE_RESET() __HAL_RCC_SPI4_RELEASE_RESET()
#define FMCW_ADF4158_GPIO_CLK_ENABLE()   __HAL_RCC_GPIOE_CLK_ENABLE()
#define FMCW_ADF4158_SPI_GPIO_PORT       GPIOE
#define FMCW_ADF4158_SPI_AF              GPIO_AF5_SPI4
#define FMCW_ADF4158_SPI_SCK_PIN         GPIO_PIN_2
#define FMCW_ADF4158_SPI_MISO_PIN        0u
#define FMCW_ADF4158_SPI_MOSI_PIN        GPIO_PIN_6
#define FMCW_ADF4158_LE_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define FMCW_ADF4158_LE_PORT             GPIOA
#define FMCW_ADF4158_LE_PIN              GPIO_PIN_1
#define FMCW_ADF4158_CE_GPIO_CLK_ENABLE() __HAL_RCC_GPIOE_CLK_ENABLE()
#define FMCW_ADF4158_CE_PORT             GPIOE
#define FMCW_ADF4158_CE_PIN              GPIO_PIN_1
#define FMCW_ADF4158_CE_DEFAULT_ENABLE   1u

/* Write order is R7 -> R0. Fill these with the final center-frequency/ramp plan.
 * Autoprogramming remains disabled until FMCW_ENABLE_ADF4158_AUTOPROGRAM is set.
 */
#define FMCW_ADF4158_INIT_REGISTERS \
  { 0x00000007u, 0x00000006u, 0x00000005u, 0x00000004u, \
    0x00000003u, 0x00000002u, 0x00000001u, 0x00000000u }

#endif
