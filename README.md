# 6 GHz FMCW Radar

A custom Frequency-Modulated Continuous-Wave radar project for contactless distance, velocity, and breathing-rate measurement.

The project combines a mixed-signal RF PCB, STM32-based high-speed data acquisition, USB streaming, and host-side radar signal processing. The board generates a swept 6 GHz-class RF signal, receives reflections, converts them into a lower-frequency beat signal, digitizes that signal, and sends the raw samples to a computer for analysis.

![FMCW radar PCB preview](Hardware/PCB/Images/PCB%20Preview.png)

## What Is FMCW Radar?

FMCW stands for Frequency-Modulated Continuous-Wave.

Instead of transmitting a short pulse, an FMCW radar continuously sends a signal whose frequency changes over time. This frequency sweep is called a chirp. When the transmitted signal reflects from a target and returns to the receiver, the radar mixes the received signal with a copy of the transmitted signal.

The difference between those two frequencies becomes a beat signal. That beat signal can be processed to estimate target distance. By comparing multiple chirps over time, the same radar can also estimate velocity and small periodic motion such as breathing.

```text
Swept RF chirp -> target reflection -> receiver -> mixer beat signal
               -> filtering -> ADC -> USB -> host signal processing
```

## Project Overview

This project is built as a complete radar acquisition platform:

- The RF front end transmits and receives the 6 GHz-class radar signal.
- The mixer converts the received RF signal into an IF beat signal.
- The analog filtering chain prepares the IF signal for digitization.
- A 12-bit high-speed ADC samples the IF waveform.
- An STM32F429 captures the ADC data using hardware peripherals.
- USB High Speed streams raw radar frames to a host computer.
- Host-side DSP can perform range FFTs, velocity estimation, and breathing-rate experiments.

The main design goal is to keep acquisition deterministic. The STM32 does not manually sample the ADC in a software loop. Instead, the radar uses clocked hardware capture, DMA buffering, and USB streaming so the raw waveform can be moved to the computer reliably.

## PCB Design

The board combines RF, analog, digital, power, USB, and debug sections on one PCB.

![FMCW radar PCB design](Hardware/PCB/Images/PCB%20Design.png)

The RF transmit and receive sections are separated on the right side of the board. The STM32, ADC, clocking, and digital capture path sit closer to the center. The lower section contains the power supplies and USB-C interface.

## Key Specifications

| Category | Specification |
| --- | --- |
| Radar type | FMCW radar |
| RF class | 6 GHz |
| Main use cases | Distance, velocity, and breathing-rate measurement |
| Chirp / PLL device | ADF4158 |
| ADC | LTC1420 |
| ADC resolution | 12-bit |
| ADC sample rate target | 10 MSps |
| Controller | STM32F429VET6TR |
| Main acquisition interface | DCMI parallel capture |
| Data movement | DMA into SRAM frame buffers |
| USB PHY | USB3317 ULPI |
| Host link | USB 2.0 High Speed |
| Raw payload target | About 20 MB/s |
| Debug interface | SWD and SWO/SWV |
| Host processing | Python / DSP pipeline |

## Hardware Blocks

| Block | Purpose |
| --- | --- |
| Transmitter | Generates and amplifies the swept RF signal. |
| Receiver | Captures and conditions reflected RF energy. |
| Mixer | Converts RF return information into a lower-frequency IF signal. |
| IF filtering | Filters and scales the beat signal for the ADC. |
| ADC | Converts the analog IF waveform into 12-bit digital samples. |
| STM32 controller | Aligns capture timing, frames samples, and streams data. |
| USB interface | Sends raw frames to the host computer. |
| Power system | Generates the analog, digital, RF, ADC, and support rails. |

## Schematic

The full PCB schematic is available here:

[Hardware/PCB/Schematics/FMCWRadar.pdf](Hardware/PCB/Schematics/FMCWRadar.pdf)

The schematic is organized into functional pages:

| Page | Section | Description |
| --- | --- | --- |
| 1 | Transmitter | PLL, VCO, PA, coupler, detector, and RF transmit path. |
| 2 | Receiver | RF receive input, filtering, and gain chain. |
| 3 | Mixer | LO/RF mixing into differential IF outputs. |
| 4 | IF filtering | Differential IF filtering and amplification. |
| 5 | ADC | LTC1420 ADC input stage, clocking, overflow, and parallel data bus. |
| 6 | MCU | STM32F429, clock routing, DCMI bus, debug, and control signals. |
| 7 | Power supply | Regulators and analog/digital power rails. |
| 8 | USB | USB3317 ULPI PHY and USB-C connection. |
| 9 | PCB composite | Board routing and placement overview. |
| 10 | Assembly view | Component placement and reference designators. |
| 11 | BOM | Exported bill of materials. |

## What Has Been Implemented

The hardware design and firmware architecture have been brought together into a working acquisition plan:

- ADF4158-based FMCW chirp generation.
- 10 MHz ADC/DCMI sample-clock path.
- STM32 DCMI capture of the 12-bit ADC bus.
- DMA buffering for continuous high-speed sample movement.
- USB High-Speed streaming to the host computer.
- SWO/SWV debug logs for bring-up.
- Host-side frame parsing and raw capture support.

## Current Status

The project is in firmware bring-up and acquisition validation. The main staged checks are:

1. Power rail validation.
2. Clock validation.
3. USB enumeration.
4. ADF4158 chirp and MUXOUT validation.
5. ADC/DCMI sample capture validation.
6. Continuous USB streaming.
7. Range/velocity/breathing DSP experiments.

One important timing item is still under review: the acquisition architecture assumes one radar slope maps cleanly to one ADC frame. The final ADF4158 slope timing and firmware frame size must be kept consistent before using the captured data for final measurements.

## Repository Areas

| Folder | Contents |
| --- | --- |
| `Hardware/` | PCB design, schematic, BOM, and mechanical work. |
| `Firmware/` | STM32 acquisition firmware and host capture tools. |
| `DSP/` | Signal-processing work for range, velocity, and breathing-rate estimation. |
| `Docs/` | Supporting documentation and project notes. |

## Goal

The goal is a clear, modular, and debuggable radar platform where each subsystem can be tested independently:

- RF transmit and receive chain
- Mixer and IF path
- ADC sampling
- STM32 capture timing
- USB data streaming
- Host-side radar DSP

That structure makes the project easier to debug, easier to optimize, and easier to hand off for future development.
