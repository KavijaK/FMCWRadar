# FMCWRadar
Contactless Distance and Velocity Estimation using Frequency Modulated Continuous Wave Radar

## Project Overview

This project develops a complete FMCW (Frequency Modulated Continuous Wave) Radar system capable of:

- **Distance Measurement** — range estimation using beat-frequency analysis
- **Velocity Estimation** — radial velocity estimation via Doppler shift
- **Breathing Pace Measurement** — contactless vital-sign detection using micro-Doppler analysis

The system consists of a custom hardware PCB (based on an STM32 microcontroller), a mechanical enclosure, embedded STM32 firmware, and a host-side DSP pipeline.

## Repository Structure

```
FMCWRadar/
├── Hardware/                    # All hardware design files
│   ├── PCB/                     # PCB schematics, layout, and BOM
│   │   ├── Schematics/          # Circuit schematics
│   │   ├── Layout/              # PCB layout and Gerber files
│   │   └── BOM/                 # Bill of Materials
│   └── Enclosure/               # Mechanical enclosure design
│       ├── CAD/                 # 3D CAD model files
│       └── Renders/             # Rendered images of the enclosure
├── Firmware/                    # STM32 embedded firmware
│   ├── Core/
│   │   ├── Inc/                 # Header files
│   │   └── Src/                 # Source files
│   ├── Drivers/                 # STM32 HAL/LL and peripheral drivers
│   └── Middlewares/             # Optional middleware (FreeRTOS, etc.)
├── DSP/                         # Digital Signal Processing algorithms
│   ├── VelocityEstimation/      # Doppler-based velocity estimation
│   ├── DistanceMeasurement/     # Beat-frequency range measurement
│   ├── BreathingPaceMeasurement/# Vital-sign / breathing rate detection
│   └── Utils/                   # Shared DSP utilities (FFT, filters, etc.)
└── Docs/                        # Project documentation
```

## Getting Started

1. **Hardware** — See [`Hardware/PCB/README.md`](Hardware/PCB/README.md) for PCB design notes and [`Hardware/Enclosure/README.md`](Hardware/Enclosure/README.md) for enclosure details.
2. **Firmware** — See [`Firmware/README.md`](Firmware/README.md) for build and flash instructions.
3. **DSP** — See [`DSP/README.md`](DSP/README.md) for an overview of the signal processing pipeline.
4. **Docs** — See [`Docs/README.md`](Docs/README.md) for additional project documentation.
