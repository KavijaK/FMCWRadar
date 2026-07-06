
#  Digital Signal Processing (DSP) Core

This directory contains the complete signal processing pipeline for the FMCW Radar. It transforms raw, time-domain Intermediate Frequency (IF) signals sampled by the ADC into actionable spatial data (Range, Velocity, and Micro-displacement).

## 📂 Sub-Directory Architecture

| Module | Core Function | Output Data Type |
| :--- | :--- | :--- |
| [`/DistanceMeasurement`](DistanceMeasurement/) | Fast-Time 1D-FFT | Range Profile (Spatial distance) |
| [`/VelocityEstimation`](VelocityEstimation/) | Slow-Time 2D-FFT | Range-Doppler Map (Macro-velocity) |
| [`/BreathingPaceMeasurement`](BreathingPaceMeasurement/) | Phase Unwrapping | Micro-Doppler displacement (Vibration/Vitals) |
| [`/Utils`](Utils/) | CFAR, Filters, Windowing | Threshold masks & Filtered arrays |

## 🔄 The Data Processing Pipeline

In FMCW radar, data is processed in a 2D matrix format known as **Fast-Time** (samples within a single chirp) and **Slow-Time** (successive chirps over time).

```text
[Raw ADC Stream] ──(De-interleave)──> M x N Matrix (M chirps, N samples)
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────┐
│ 1. DISTANCE MEASUREMENT (Fast-Time Processing)              │
│    - DC Offset Removal & Hamming Windowing                  │
│    - 1D FFT across N samples                                │
│    - Output: Range Bins (Where are the targets?)            │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. VELOCITY ESTIMATION (Slow-Time Processing)               │
│    - 1D FFT across M chirps (for each Range Bin)            │
│    - Measures Phase Rotation (Δφ) between chirps            │
│    - Output: Range-Doppler Map (How fast are they moving?)  │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. TARGET EXTRACTION & MICRO-DOPPLER                        │
│    - CFAR (Constant False Alarm Rate) Target Detection      │
│    - Phase Extraction on specific target bins               │
│    - Output: Object Tracking & Sub-millimeter vibrations    │
└─────────────────────────────────────────────────────────────┘
- All algorithms are designed to run on the host processing system receiving data from the STM32 firmware over USB/UART.
- Python (NumPy / SciPy) is the recommended language for prototyping; C/C++ for embedded deployment.
