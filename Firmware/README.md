# FMCW Radar Acquisition Firmware

This repository implements the architecture in `c:\Users\karun\Downloads\fmcw_architecture.html` for the STM32F429VET6TR + LTC1420 + ADF4158 + USB3317 board. The CubeMX project at `C:\Users\karun\OneDrive\Desktop\FMCWRadarFirmware` was used as the board pin source for this merge.

The firmware is built as an STM32Cube/HAL project with register-level setup for the timing-critical path:

- HSE 20 MHz bypass clock, PLL to 168 MHz, MCO1 = HSE / 2 = 10 MHz
- DCMI in 12-bit continuous mode with PA4 held as static active-high HSYNC
- DMA2 Stream 1 Channel 1 in double-buffer mode, extended to a 4-frame SRAM ring
- TIM9 input capture/reset mode on PE5 for one-shot alignment and MUXOUT diagnostics
- USB OTG HS ULPI vendor-specific bulk IN endpoint with internal DMA enabled
- Host-side Python receiver/parser with resync, stats, raw save, and optional FFT preview

## Start Here

Read these in order:

1. `README.md` for the system overview and build commands.
2. `docs/board_configuration.md` for board-specific pins and options.
3. `docs/bringup.md` for the scope/checklist sequence.
4. `firmware/Core/Inc/fmcw_board_config.h` for the actual pins compiled into firmware.
5. `firmware/Core/Src/main.c`, then `fmcw_hardware.c`, then `fmcw_capture.c` for the runtime flow.

## Layout

- `firmware/` - STM32F429 firmware sources, linker script, and CMake build metadata.
- `host/` - Python host receiver and protocol parser.
- `tests/` - Host-side protocol tests that run without USB hardware.
- `docs/` - Bring-up notes and board-configuration notes.

## Build Firmware

Install Arm GNU Toolchain and STM32CubeF4, then configure with:

```powershell
cmake -S firmware -B build/firmware `
  -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi-gcc.cmake `
  -DSTM32_CUBE_F4_PATH=C:/Users/karun/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3
cmake --build build/firmware
```

The project expects the standard STM32CubeF4 layout containing `Drivers/` and `Middlewares/ST/STM32_USB_Device_Library/`.

## Board Configuration

All PCB-dependent choices live in `firmware/Core/Inc/fmcw_board_config.h`. Review that file before flashing:

- DCMI pins imported from CubeMX: D0=PA9, D1=PC7, D2=PC8, D3=PC9, D4=PC11, D5=PD3, D6=PB8, D7=PB9, D8=PC10, D9=PC12, D10=PD6, D11=PD2, PIXCLK=PA6, VSYNC=PB7, HSYNC drive=PA4, HSYNC feedback=PA7.
- ULPI pins imported from CubeMX: CK=PA5, STP=PC0, DIR=PC2, NXT=PC3, D0=PA3, D1=PB0, D2=PB1, D3=PB10, D4=PB11, D5=PB12, D6=PB13, D7=PB5.
- USB3317 reset is `USB_RESET` on PE13, active low.
- ADF4158 pins imported from CubeMX: SPI4 SCK=PE2, MOSI=PE6, LE=PA1, CE=PE1, MUXOUT=PE5.
- RF transmit control: `TX_STDBY` on PE11 is active high. When it is high, firmware drives `MIXER_EN` on PD13 and `PA_EN` on PE0 high. When it is low, both outputs are driven low.
- Whether capture waits for USB enumeration before enabling TIM9/alignment

Two generated CubeMX choices were intentionally not copied:

- CubeMX generated `SYSCLK=180 MHz`; the firmware keeps `168 MHz` because the architecture's TIM9 alignment uses 13 ticks at a 42 MHz timer count rate, approximately the LTC1420 3-cycle/300 ns pipeline delay.
- DCMI now samples on the falling PIXCLK edge per board bring-up request, while keeping active-high HSYNC with PA4 driven high once after the first MUXOUT alignment event. PA7 is read back as the routed HSYNC feedback signal.

The ADF4158 frequency/ramp register values are necessarily board/application-specific. The driver and call order are implemented, but the register table must be filled with the final PLL plan for your RF center frequency and deviation.

## Firmware Flow

- `main.c`: Initializes HAL, clocks, GPIO, USB, DCMI, DMA, TIM9, then continuously services TX standby, USB streaming, and the MUXOUT watchdog.
- `fmcw_hardware.c`: Owns clocks, GPIO modes, USB PHY reset, DCMI, DMA2 Stream 1, TIM9, NVIC, and TX standby gating.
- `fmcw_capture.c`: Owns the 4-buffer SRAM ring, slope sequence numbers, MUXOUT timestamps, DMA completion handling, and USB queue handoff.
- `fmcw_adf4158.c`: Optional SPI programming path for ADF4158. It is disabled until `FMCW_ENABLE_ADF4158_AUTOPROGRAM` is set and real register values are added.
- `usb_stream.c` and `usbd_vendor_stream.c`: Expose a vendor-specific USB HS bulk IN stream.

## Host Capture

```powershell
pip install -r host/requirements.txt
python host/fmcw_capture.py --frames 1000 --raw out_frames.bin --stats
```

Run protocol tests:

```powershell
python -m unittest discover -s tests
```
