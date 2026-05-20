# Board Configuration Notes

The implementation keeps PCB-specific choices in `firmware/Core/Inc/fmcw_board_config.h`. The current values were merged from the CubeMX project at `C:\Users\karun\OneDrive\Desktop\FMCWRadarFirmware`.

## CubeMX Pin Choices

The active DCMI data map is:

| Signal | Pin |
| --- | --- |
| DCMI_PIXCLK | PA6 |
| DCMI_HSYNC | PA4 GPIO output, driven high by TIM9 alignment ISR |
| DCMI_HSYNC feedback | PA7 GPIO input, routed from PA4 on the PCB |
| DCMI_VSYNC | PB7 AF13, expected tied to GND |
| D0..D4 | PA9, PC7, PC8, PC9, PC11 |
| D5..D7 | PD3, PB8, PB9 |
| D8..D9 | PC10, PC12 |
| D10..D11 | PD6, PD2 |
| MUXOUT | PE5 AF3 TIM9_CH1 |

The default ULPI map is:

| Signal | Pin |
| --- | --- |
| CK | PA5 |
| STP | PC0 |
| DIR | PC2 |
| NXT | PC3 |
| D0..D2 | PA3, PB0, PB1 |
| D3..D7 | PB10, PB11, PB12, PB13, PB5 |

The RF control map is:

| Signal | Pin | Firmware behavior |
| --- | --- | --- |
| TX_STDBY | PE11 input | Active high TX-enable request |
| MIXER_EN | PD13 output | High when TX_STDBY is high |
| PA_EN | PE0 output | High when TX_STDBY is high |
| USB_RESET | PE13 output | Active-low USB3317 reset pulse at boot |
| PLL_LE | PA1 output | ADF4158 latch enable, low when idle |
| PLL_CE | PE1 output | ADF4158 chip enable, high by default |
| PLL_CLK / PLL_DATA | PE2 / PE6 | SPI4 SCK/MOSI when autoprogramming is enabled |

## Intentional CubeMX Overrides

The CubeMX-generated `main.c` is useful as a pin reference, but the acquisition firmware intentionally overrides these generated settings:

- `SYSCLK` stays at 168 MHz instead of the generated 180 MHz so TIM9's `PSC=3, CCR2=13` alignment delay remains about 309 ns.
- DCMI uses falling PIXCLK per bring-up request and active-high HSYNC because PA4 is held high after the first MUXOUT alignment event. PA7 is the routed-back HSYNC feedback check.
- PE5 is configured as `TIM9_CH1 AF3`, not a plain GPIO input, because MUXOUT must reset/capture TIM9.
- USB HS internal DMA is enabled for zero-copy bulk transfers; CubeMX generated it disabled.

## Header Correction

The HTML architecture document says the USB header struct is 64 bytes, but its sample C struct uses `reserved[2]`, which makes it 56 bytes. The implementation uses `reserved[4]` and enforces the real 64-byte header in both C and Python tests.

## ADF4158

The SPI driver and register write order are implemented, but autoprogramming is disabled by default because the final center frequency, INT/FRAC values, deviation words, and exact SPI pins are not in the HTML handoff. To enable it:

1. Set `FMCW_ENABLE_ADF4158_AUTOPROGRAM` to `1`.
2. Confirm or change the SPI/LE/CE pin macros.
3. Replace `FMCW_ADF4158_INIT_REGISTERS` with the final R7-to-R0 register words.

If the PLL is programmed externally during bring-up, leave autoprogramming disabled. TIM9 still aligns on the first MUXOUT pulse.
