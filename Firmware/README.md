# Firmware

This directory contains the STM32 firmware for the FMCW Radar PCB.

## Sub-directories

| Directory | Description |
|-----------|-------------|
| [`Core/`](Core/) | Core application code — main entry point, peripheral initialization, interrupt handlers |
| [`Core/Inc/`](Core/Inc/) | Header files (`.h`) for core application modules |
| [`Core/Src/`](Core/Src/) | Source files (`.c`) for core application modules |
| [`Drivers/`](Drivers/) | STM32 HAL/LL drivers and any custom peripheral drivers |
| [`Middlewares/`](Middlewares/) | Optional middleware components (e.g., FreeRTOS, USB stack) |

## Toolchain

- Target MCU: STM32 (ARM Cortex-M)
- Recommended IDE: STM32CubeIDE or any GCC-ARM toolchain
- Build system: Makefile / CMake

## Getting Started

1. Open the project in STM32CubeIDE (or your preferred toolchain).
2. Configure the target device and clock settings in `Core/Src/main.c`.
3. Build and flash to the hardware target.
