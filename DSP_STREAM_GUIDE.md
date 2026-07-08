# FMCW Stream DSP Guide

This firmware streams one USB frame per FMCW slope. The PC-side DSP should
parse fixed-size frames, verify sequence continuity, decode 12-bit ADC samples,
then run range/Doppler processing.

## Current Stream

- USB VID/PID: `1209:4158`
- Bulk IN endpoint: `0x81`
- Start stream command: vendor control request `bmRequestType=0x40`, `bRequest=0x30`
- Frame size: `20480` bytes
- Header size: `64` bytes
- ADC payload: `20000` bytes
- Padding: `416` bytes, ignore
- ADC sample rate: `10 MHz`
- Samples per slope: `10000`
- DCMI words per slope: `5000`
- ADC format: 12-bit two's-complement, 1 LSB = 1 mV

## Header

Little-endian Python format:

```python
HEADER = struct.Struct("<IHHIIIHHIIHHIIIIIII")
```

| Field | Meaning |
|---|---|
| `magic` | Must be `0x52444152` |
| `version` | Currently `1` |
| `header_len` | Must be `64` |
| `frame_len` | Must be `20480` |
| `slope_seq` | Monotonic slope counter |
| `triangle_seq` | `slope_seq / 2` |
| `slope_id` | Alternates `0, 1, 0, 1...` |
| `flags` | Stream health flags |
| `sample_rate_hz` | `10000000` |
| `samples_per_slope` | `10000` |
| `adc_bits` | `12` |
| `words_per_slope` | `5000` |
| `timestamp_us` | Coarse firmware timestamp |
| `muxout_count` | ADF4158 MUXOUT edge count |
| `dropped_frames` | Cumulative firmware drops |
| `dcmi_risr` | DCMI status snapshot |
| `dma_lisr` | DMA status snapshot |

Flags:

```text
0x0001  dropped frame occurred
0x0002  USB/host was late returning buffers
0x0004  MUXOUT alignment timeout
0x0008  DMA error
0x0010  DCMI error
0x0020  DCMI overrun
```

## ADC Decode

Each 32-bit payload word contains two ADC samples:

```python
word = uint32_little_endian
sample0_raw = word & 0x0FFF
sample1_raw = (word >> 16) & 0x0FFF
```

Sign extension:

```python
def sign_extend_12(x):
    x &= 0x0FFF
    return x - 0x1000 if x & 0x0800 else x
```

Voltage:

```python
sample_mV = sign_extend_12(raw)
sample_V = sample_mV / 1000.0
```

## Current RF Preset

Normal firmware preset:

- One-way chirp time: `1.000 ms`
- Triangle period: `2.000 ms`
- Start frequency: `5.700000000 GHz`
- Stop frequency: `5.900271606 GHz`
- Center frequency: `5.800135803 GHz`
- One-way bandwidth: `200.271606 MHz`
- Chirp slope: `200.271606 GHz/s`

Range conversion from beat frequency:

```python
range_m = c * beat_freq_hz / (2 * chirp_slope_hz_per_s)
```

With the current preset:

```python
chirp_slope_hz_per_s = 200.271606e6 / 1.0e-3
```

## Recommended DSP Workflow

1. **Frame validation**
   - Check `magic`, `version`, `header_len`, and `frame_len`.
   - Check `slope_seq` increments by 1. If it jumps, mark missing slopes.
   - Check `flags == 0` for clean data.

2. **ADC decode**
   - Decode 5000 words into 10000 signed samples.
   - Convert to volts if needed.
   - Remove DC offset per chirp.

3. **Range FFT per chirp**
   - Apply a Hann or Blackman window.
   - Use `rfft` on the real ADC samples.
   - Convert FFT bin frequency to range using chirp slope.
   - Keep only useful range bins.

4. **Slope handling**
   - `slope_id` alternates between the two halves of the triangle.
   - Initially process only one `slope_id` for a stable range-time plot.
   - Once verified, compare both slope phases for range/Doppler refinement.

5. **Range-time visualization**
   - Stack range FFT magnitudes over time.
   - This shows stationary targets as horizontal bright bands.

6. **Doppler processing**
   - For each range bin, collect complex or magnitude values across many chirps.
   - Run a slow-time FFT across chirps.
   - If using only real ADC magnitude spectra, Doppler sign/quality is limited.
   - For better Doppler, preserve complex FFT bins from each chirp before taking magnitude.

7. **Calibration**
   - Measure known target distances.
   - Verify slope polarity for `slope_id=0` and `slope_id=1`.
   - Estimate/remove static clutter from range bins.
   - Apply range-bin scaling and any front-end gain calibration.

## Visualization Tool

Install dependencies:

```powershell
python -m pip install pyusb libusb-package numpy matplotlib
```

Run:

```powershell
python .\tools\fmcw_dsp_view.py
```

Useful options:

```powershell
python .\tools\fmcw_dsp_view.py --slope-id 0 --max-range-m 100 --process-every 10
python .\tools\fmcw_dsp_view.py --slope-id all --max-range-m 200
python .\tools\fmcw_dsp_view.py --save-raw capture.bin
```

The live viewer is intentionally decimated. It is for visual bring-up, not
lossless DSP validation.
