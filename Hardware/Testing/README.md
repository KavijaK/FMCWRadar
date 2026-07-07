# Hardware / Testing

This folder contains GNU Radio Companion (GRC) test flows and notes used during hardware bring-up and verification of the FMCW radar front-end. Tests were performed with a bladeRF micro 2.0, a spectrum analyzer, waterfall/FFT plots, and H near-field probes. The tests include transmitter, receiver, antenna checks, and debug steps for LNAs and the mixer.

## Overview / Goals
- Verify bladeRF TX/RX functionality and expected waterfall/FFT signatures.
- Detect and measure the 5.8 GHz chirp transmitted by the FMCW transmitter.
- Use bladeRF as a reference source and the spectrum analyzer for accurate power/frequency measurements.
- Use H near-field probes to compare relative power from bladeRF and the FMCW transmitter and to localize faults.
- Debug and fix transmitter faults, LNA(s), and mixer behavior on the receiver chain.
- Test antennas with controlled frequency offsets to avoid LO/DC leakage.

## Equipment
- bladeRF micro 2.0 (TX and RX)
- GNU Radio Companion (GRC)
- Spectrum analyzer (centered at 5.8 GHz)
- H near-field probes (for near-field relative power measurement)
- Directional couplers / SMA cables / attenuators
- Transmitter horn antenna (for TX-to-PCB coupling during antenna tests)
- Splitter for distributing LO/reference as needed

## Test flows (what each .grc does)
1. Self-test: bladerf_self_test.grc
   - Purpose: Confirm bladeRF functionality and GRC plotting behavior.
   - What to check: bladeRF TX -> bladeRF RX loop, waterfall and FFT sinks, adjustable chirp time so time-varying frequencies are clear.
   - Implementation note: Include a chirp generator (or tone source) with a GUI slider for chirp time and amplitude so you can quickly change sweep parameters and observe how the waterfall reacts.

2. Receiver check: rx_5p8GHz_check.grc
   - Purpose: Receiver chain verification and antenna testing.
   - What to check: Probe LNA outputs with H near-field probe, verify which LNAs are active or faulty. Feed a bladeRF transmitter directly into the RX SMA input for a controlled reference signal.
   - Mixer check: Observe mixer output and beat/difference frequencies when LO/reference is offset slightly from the transmitter. Log the IF/beat frequency and confirm expected behavior.
   - Explicit checks to run:
     - Verify LNA1 output (near-field probe) — expected: present RF when working, otherwise replace.
     - Verify LNA2 output downstream — expected: present if LNA1 absent only LNA2 may show lower power.
     - With a known bladeRF TX tone/chirp at the RX SMA input, measure mixer IF/beat frequency while varying LO/reference and confirm linear response.

3. Transmitter check: tx_5p8GHz_check.grc
   - Purpose: Verify FMCW radar transmitter output and waterfall plots.
   - What to check: Observe the 5.8 GHz chirp on waterfall and spectrum analyzer, use adjustable center-frequency slider (smooth ±300 kHz) and chirp time control to visualize the sweep clearly.
   - Implementation note: Focus on waterfall diagrams — set chirp time, sweep range, and sample rate so the chirp appears as a clear slanted line across the waterfall.

4. Antenna test: bladerf_antenna_test_5p8GHz.grc
   - Purpose: Measure transmitted power into the horn antenna and received levels on bladeRF.
   - What to check: Set transmitter to constant 5.800 GHz and set bladeRF receiver/horn to 5.799 GHz (small offset) to avoid DC/LO spike and observe received power and beat frequency.
   - Implementation note: Use attenuators and possibly an anechoic/coupled setup to protect RX and get repeatable power readings.

## Quick test checklist (run in order)
1. Self-test: Run bladerf_self_test.grc to confirm bladeRF and GRC sinks behave as expected (waterfall/FFT). Adjust chirp time until the chirp is clearly visible.
2. TX check: Run tx_5p8GHz_check.grc connected to the FMCW transmitter (or bladeRF acting as TX) and confirm stable 5.8 GHz chirp on waterfall and spectrum analyzer.
3. Antenna test: Run bladerf_antenna_test_5p8GHz.grc with TX set to constant 5.800 GHz and bladeRF RX to 5.799 GHz. Record received power at horn antenna and note any LO/DC leakage effects.
4. RX check: Run rx_5p8GHz_check.grc. Probe LNA stages with H-probe while driving the RX SMA with bladeRF. Verify LNA outputs and mixer IF/beat frequency. Replace/repair failed LNAs and re-test.

## How to run (high-level)
- Open the .grc file in GNU Radio Companion.
- Set sample rate to the rate supported by bladeRF for operation near 5.8 GHz.
- Set center frequency (default: 5.8e9) and chirp parameters as appropriate for the flow.
- Use GUI sliders to vary chirp time and small center-frequency offsets.
- Always use attenuators when connecting TX to RX or spectrum analyzer to avoid damage.

## Recommended default parameters
- Center frequency: 5.8e9 (5.8 GHz)
- Sweep range for transmitter-check: ±300e3 (±300 kHz)
- Chirp time slider: 1e-3 to 50e-3 s (1 ms to 50 ms)
- FFT size / waterfall resolution: 2048 or 4096 bins (tune as needed)

## Measurement & safety notes
- Always use attenuators or directional couplers when connecting TX to RX or to the spectrum analyzer.
- Limit bladeRF TX output power to avoid damaging the receiver front-end or LNAs.
- Ensure antennas are properly oriented and maintain safe RF exposure distances.
- Log measurement conditions (attenuation, cable loss, probe positions) to allow reproducible comparisons.

## Observed issues & fixes (from this test campaign)
- Transmitter faults were discovered and iteratively fixed until stable chirp output was observed on the spectrum analyzer and waterfall plots.
- Receiver debug using bladeRF reference showed a failed first LNA (no output) while the second LNA was functional. Replacing the first LNA restored expected mixer output.
- Using a small offset between the LO-fed transmitter and the bladeRF reference helped visualize the beat frequency and confirmed proper LO splitting and mixer response.
- Near-field H-probe testing was instrumental in locating faulty components and in interpreting spectrum analyzer power readings.

## Files to upload
- bladerf_self_test.grc
- rx_5p8GHz_check.grc
- tx_5p8GHz_check.grc
- bladerf_antenna_test_5p8GHz.grc
- Screenshots of waterfall/FFT and spectrum-analyzer plots (include measurement settings)
- Short test logs (date, equipment, attenuations, observations) for each run

## Next steps / suggested improvements
- Add automatic logging of FFT/waterfall snapshots and timestamps inside each GRC flow.
- Add a small Python or GNU Radio block to compute and log beat frequency and SNR for antenna tests.
- Calibrate power readings by measuring cable/probe losses and adding correction factors.
