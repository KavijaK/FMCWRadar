# FMCW Lab

PC-side radar workbench for the STM32 FMCW radar firmware.

This app keeps the existing USB HS acquisition path unchanged and does the
radar processing on the PC:

- FMCW range measurement
- Range-Doppler CPI processing
- Phase-based micromotion tracking
- Breathing and heartbeat rate estimates
- SAR aperture capture and experimental image formation

## Run

From the project root:

```powershell
python .\tools\fmcw_lab\run.py
```

Dependencies:

```powershell
python -m pip install pyusb libusb-package numpy pyqtgraph PyQt6
```

Only one script can own the USB device at a time. Close `fmcw_stream_read.py`,
`fmcw_live_view.py`, and `fmcw_breathing_view.py` before connecting here.

## USB Behavior

Press **Connect + Start USB**. The app:

1. Opens VID/PID `1209:4158`.
2. Sends vendor request `0x30`.
3. Reads fixed `20480` byte frames from endpoint `0x81`.
4. Parses the same 64 byte header used by `fmcw_stream_read.py`.

No firmware changes are required.

## Tabs

### Range

Shows a short ADC time-domain view and the latest range FFT. The green marker is
the strongest peak in the target search window. The magenta marker is the
phase-tracked target bin.

Leave **Target m** blank for automatic target selection. Enter a range, for
example `1.2`, to force micromotion processing to a specific target.

### Range-Doppler

Accumulates a CPI of selected-slope range FFTs and performs a slow-time FFT.
The velocity axis assumes one selected slope repeats every full triangle period.

If the map is too noisy, increase CPI. If the GUI lags, increase Process decim.

The heat colors are relative power:

```text
black/dark blue = weak return
cyan/green/yellow = stronger return
red/white = strongest return
```

The app also reports the strongest non-zero-Doppler cell as a quick velocity
measurement. This is a 2D FFT estimate:

```text
fast-time FFT across ADC samples -> range
slow-time FFT across chirps      -> Doppler / velocity
```

Read the Range-Doppler plot like this:

- horizontal axis: target range
- vertical axis: target radial velocity
- near zero velocity: stationary clutter
- positive/negative velocity: direction depends on selected slope and RF phase convention
- bright compact spot away from zero velocity: moving target candidate

### Waterfall

The waterfall is range versus time/history. Each row is an integrated range FFT
profile. A stationary object appears as a horizontal bright line at fixed range.
A walking/moving object appears as a line that bends or slopes across range over
time. The waterfall does not directly show velocity; use Range-Doppler for that.

### Micromotion + Vitals

Tracks complex phase at the selected range bin and converts phase to displacement:

```text
displacement = phase * wavelength / (4*pi)
```

Breathing band: `0.10 Hz` to `0.70 Hz`.

Heartbeat band: `0.80 Hz` to `2.50 Hz`.

Breathing should work first. Heartbeat needs a strong stationary chest target and
low body motion.

### SAR Experiments

This is an experimental capture mode. Press **Capture SAR Aperture** while the
radar is moving along a known straight path. Enter the approximate platform
speed in m/s.

The app stores complex range FFT profiles, then forms:

- an aperture FFT quicklook
- a simple monostatic backprojection image

This is not a replacement for a controlled SAR rail or IMU/encoder metadata, but
it is enough to test phase coherence and aperture imaging behavior.

## Defaults

- Sample rate: `10 MHz`
- Samples per slope: `10000`
- Chirp time: `1 ms`
- Bandwidth: `200.271606 MHz`
- Center frequency: `5.800135803 GHz`
- Processed slope: `0`
- Processing decimation: `10`

These match the current normal firmware stream assumptions.
