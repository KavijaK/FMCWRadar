# PCB Design

This directory contains all PCB design files for the FMCW Radar unit based on the STM32 microcontroller.

## Sub-directories

| Directory | Description |
|-----------|-------------|
| [`Schematics/`](Schematics/) | Circuit schematics (e.g., KiCad `.sch` / `.kicad_sch` files, PDF exports) |
| [`Layout/`](Layout/) | PCB layout files (e.g., KiCad `.kicad_pcb`, Gerber files) |
| [`BOM/`](BOM/) | Bill of Materials (component lists and part references) |

## Design Notes

- Target MCU: STM32
- RF front-end interfaces with the STM32 via ADC/SPI
- Radar transmit and receive paths are on-board
