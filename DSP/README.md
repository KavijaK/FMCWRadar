# DSP — Digital Signal Processing

This directory contains all signal processing algorithms for the FMCW Radar.

## Sub-directories

| Directory | Description |
|-----------|-------------|
| [`VelocityEstimation/`](VelocityEstimation/) | Doppler-shift based velocity estimation algorithms |
| [`DistanceMeasurement/`](DistanceMeasurement/) | Beat-frequency based range / distance measurement algorithms |
| [`BreathingPaceMeasurement/`](BreathingPaceMeasurement/) | Vital-sign (breathing rate) detection using micro-Doppler analysis |
| [`Utils/`](Utils/) | Shared DSP utilities (FFT wrappers, windowing functions, filters, etc.) |

## Processing Pipeline Overview

```
ADC samples (raw IF signal)
        │
        ▼
  ┌─────────────┐
  │  Range FFT  │  ← DistanceMeasurement
  └─────┬───────┘
        │
        ▼
  ┌─────────────┐
  │ Doppler FFT │  ← VelocityEstimation
  └─────┬───────┘
        │
        ▼
  ┌──────────────────────┐
  │ Micro-Doppler / CFAR │  ← BreathingPaceMeasurement
  └──────────────────────┘
```

## Notes

- All algorithms are designed to run on the host processing system receiving data from the STM32 firmware over USB/UART.
- Python (NumPy / SciPy) is the recommended language for prototyping; C/C++ for embedded deployment.
