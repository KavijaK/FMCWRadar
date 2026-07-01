# Distance Measurement

This module implements beat frequency based range (distance) measurement for the FMCW Radar.

## Principle

In FMCW radar, the received signal is mixed with the transmitted chirp to produce an intermediate frequency (IF) beat signal. The frequency of this beat signal is proportional to the range of the target.

```
R = (f_beat * c * T_chirp) / (2 * B)
```

Where:
- `R`        — range to target (m)
- `f_beat`   — beat frequency (Hz)
- `c`        — speed of light (3 × 10⁸ m/s)
- `T_chirp`  — chirp duration (s)
- `B`        — chirp bandwidth (Hz)

## Files

| File | Description |
|------|-------------|
| *(to be added)* | Range FFT processing |
| *(to be added)* | Range bin to distance conversion |
| *(to be added)* | CFAR target detection |

## References

- G. Brooker, *Understanding Millimetre Wave FMCW Radars*, 1st International Conference on Sensing Technology, 2005.
