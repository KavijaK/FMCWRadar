# Breathing Pace Measurement

This module implements contactless breathing rate detection using micro-Doppler analysis of FMCW radar returns.

## Principle

A stationary person's chest wall moves slowly (~0.2–1 Hz) during breathing. This periodic displacement modulates the phase of the received radar signal. By tracking the phase of the dominant range bin over time and applying spectral analysis, the breathing rate can be extracted.

```
f_breathing  ≈  phase_variation_frequency  (typically 0.1 – 0.5 Hz)
```

Processing steps:
1. Perform range FFT to locate the stationary subject.
2. Extract the phase of the dominant range bin across successive chirps.
3. Apply a band-pass filter (e.g., 0.1 – 1.0 Hz) to isolate breathing motion.
4. Estimate the dominant frequency (peak of FFT) as the breathing rate.

## Files

| File | Description |
|------|-------------|
| *(to be added)* | Phase extraction from range bins |
| *(to be added)* | Band-pass filtering |
| *(to be added)* | Breathing rate spectral estimation |

## References

- C. Li and J. Lin, "Random Body Movement Cancellation in Doppler Radar Vital Sign Detection," *IEEE Trans. Microw. Theory Tech.*, vol. 56, no. 12, pp. 3143–3152, 2008.
