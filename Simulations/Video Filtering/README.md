# Video Filtering Simulation

## Overview

This directory contains the LTSPICE simulation for the video filtering stage of the FMCW Radar system. The filter is designed to condition the received signal after the mixer stage, compensating fo[...]

## Filter Design

### Architecture

The video filter implements a **second-order high-pass Chebyshev filter (0.1 dB ripple)** followed by a **passive low-pass filter**, utilizing a **multiple feedback topology** for improved noise p[...]

### Key Characteristics

| Parameter | Value | Purpose |
|-----------|-------|---------|
| Filter Type | High-Pass + Low-Pass | Bandpass filtering of video signal |
| High-Pass Order | 2nd Order Chebyshev | Steep rolloff below cutoff frequency |
| Ripple Specification | 0.1 dB | Minimal passband distortion |
| Topology | Multiple Feedback | Differential noise mitigation |
| Passive LP Filter | Yes | Final stage attenuation of high frequencies |

### Design Rationale

The high-pass filter counteracts the **40 dB/decade signal attenuation** that occurs with distance in the FMCW radar system. The key design objectives are:

1. **Linear gain increase with frequency** - Compensates for distance-dependent power loss
2. **Peak gain around 200 kHz** - Matches the expected receive signal frequency range
3. **Differential noise rejection** - Multiple feedback topology reduces common-mode noise
4. **Passive LP filtering** - Additional rolloff to attenuate high-frequency noise and mixing products

## Simulation Details

### Simulation Tool
- **Software**: LTSPICE (version 4.1)
- **Analysis Type**: AC Frequency Sweep

### Frequency Response Analysis

| Parameter | Value |
|-----------|-------|
| Start Frequency | 10 Hz |
| Stop Frequency | 1 MHz |
| Points per Decade | 300 |
| Sweep Type | Logarithmic (decade) |

### Expected Performance

- **Passband**: Linear gain increase from low frequencies to ~200 kHz
- **Peak Response**: ~200 kHz (aligned with FMCW transmit frequency)
- **Attenuation Rate**: Compensates for 40 dB/decade receive signal loss
- **Stopband**: Passive LP filter provides rolloff above peak frequency

## Circuit Components

The simulation includes:

### Active Components
- **U4**: INA849 Instrumentation Amplifier (gain stage)
- **U5**: THS4561 Op-Amp (active filtering stage)

### Passive Components
- **Resistors**: Multiple feedback network resistors (1k, 4.99k, 49.9Ω, 422Ω, 120kΩ values)
- **Capacitors**: Precision filtering capacitors (1n, 2.4n, 10n values)
- **High-pass elements**: Coupling and feedback capacitors

### Power Supply
- **Dual Supply**: ±5V
- **Decoupling**: Multiple bypass capacitors (1μ, 100n) on supply rails

## Files

- `fmcw fliter - Copy.asc` - LTSPICE schematic file containing the complete filter circuit

## Usage

### To Open/Simulate in LTSPICE:

1. Open LTSPICE (version 4.1 or compatible)
2. File → Open → Select `fmcw fliter - Copy.asc`
3. Click the simulation icon or press Ctrl+R
4. The AC frequency sweep will execute from 10 Hz to 1 MHz

### Expected Output:

- **Bode Plot**: Shows magnitude response with linear gain increase peaking at ~200 kHz
- **Phase Response**: Shows phase shift through the filter stages

## Performance Specifications

- **Input Impedance**: High (determined by instrumentation amplifier)
- **Output Impedance**: Low (suitable for ADC input or subsequent processing)
- **Noise Performance**: Improved by differential topology in multiple feedback configuration
- **Bandwidth (operating region)**: DC-coupled high-pass behaviour with cutoff < 1 kHz and effective operation up to ~1 MHz; note that the response is not flat — "bandwidth" here denotes the intended frequency range over which the filter provides the designed frequency-dependent gain and acceptable group delay rather than a flat passband
- **Group Delay**: Approximately constant across the intended operating region to preserve signal integrity
- **Gain**: Frequency-dependent, peaking near 200 kHz

## Design Notes

1. The **multiple feedback topology** in the high-pass stage provides excellent noise rejection by suppressing differential noise components
2. Chebyshev 0.1 dB ripple specification ensures minimal passband ripple while maintaining steep transition
3. The passive low-pass filter provides additional selectivity without adding noise from active components
4. Component tolerances should be maintained within ±1% for accurate frequency response

## Future Improvements

- PCB layout optimization for EMI/EMC performance
- Prototype testing and measurement validation; testing the PCB and adjust the gain using resistor R19 on the INA849
- Frequency response tuning based on actual system measurements

## References

- LTSPICE Documentation
- INA849 Instrumentation Amplifier Datasheet
- THS4561 Op-Amp Datasheet
- Chebyshev Filter Design Theory

---

**Last Updated**: 2026-06-24  
**Status**: LTSPICE Simulation Phase
