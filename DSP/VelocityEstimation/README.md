# Velocity Estimation

This module implements Doppler-shift based radial velocity estimation for the FMCW Radar.

## Principle

In FMCW radar, a moving target introduces a Doppler frequency shift in the received signal. By applying a second FFT (Doppler FFT) across successive chirps (slow-time dimension), the radial velocity of a target can be estimated.

```
v = (f_d * lambda) / 2
```

Where:
- `v`       — radial velocity of the target (m/s)
- `f_d`     — Doppler frequency shift (Hz)
- `lambda`  — wavelength of the carrier (m)

## Files

| File | Description |
|------|-------------|
| *(to be added)* | Doppler FFT processing |
| *(to be added)* | Velocity extraction and unit conversion |

## References

- M. A. Richards, *Fundamentals of Radar Signal Processing*, McGraw-Hill, 2005.
