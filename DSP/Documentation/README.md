## 1. The High-Bandwidth Bottleneck & USB Bulk Transfer Design

**The Engineering Challenge:**
The radar utilizes a 10 MHz sampling rate. At 16 bits per sample, the ADC generates 20 Megabytes of data per second. Traditional microcontroller-to-PC interfaces (such as Virtual COM/CDC over Full-Speed USB) are physically limited to roughly 1.5 MB/s and rely on unstructured, continuous byte streams. This causes OS-level buffer overflows, dropped bytes, and catastrophic phase misalignment in the radar data.

**The Design Choice: Packetized USB Bulk Transfers**
To guarantee zero data loss, the architecture abandons Virtual COM ports entirely in favor of raw USB High-Speed (480 Mbps) Bulk Transfers. Rather than streaming data continuously, the STM32 microcontroller's Direct Memory Access (DMA) accumulates an exact predefined block of data (e.g., 20,480 bytes). The USB hardware then transmits this entire block as one atomic "packet." The host operating system guarantees that USB Bulk packets arrive exactly as packaged, meaning the host software never has to guess where a radar sweep begins or ends. The physical packet boundary *is* the sweep boundary.

## 2. Hardware-Level Synchronization & Header Processing

**The Engineering Challenge:**
A triangular FMCW radar alternates between sweeping the frequency UP and sweeping it DOWN. To process this mathematically, the host computer must perfectly separate the UP sweeps from the DOWN sweeps. Relying on software to analyze the analog wave to find the turning point is computationally expensive and highly susceptible to noise.

**The Design Choice: Explicit Hardware State Injection**
Synchronization is handled entirely on the silicon. Before the STM32 transmits a 20,000-byte ADC payload, it prefixes it with a 64-byte metadata header. This header contains two critical pieces of information:

*   **The Magic Word (`0x52444152`):** A static 32-bit hexadecimal anchor. If the host PC does not see this exact sequence at the start of a packet, the packet is instantly discarded. This makes the system immune to cable noise or initialization glitches.
*   **The Slope ID:** A binary state indicator (`0` for UP, `1` for DOWN) generated directly by the hardware timer that controls the radar's VCO.

By injecting the hardware's internal state machine directly into the data stream, the host computer performs zero alignment calculations. It simply reads the Slope ID, routes the attached payload to the corresponding UP or DOWN memory bank, and waits for a matching pair.

## 3. Data Unpacking and Analog Signal Reconstruction

**The Engineering Challenge:**
The ADC generates 12-bit samples. Storing a 12-bit sample in a standard 16-bit integer wastes 4 bits (25% of the total bandwidth). Furthermore, radar beat signals are AC waveforms (swinging above and below a central zero-point). The ADC outputs these as raw unsigned values (e.g., 0 to 4095), which distorts the mathematical phase required for Fourier analysis.

**The Design Choice: 32-bit Bit-Packing and Two's Complement Extension**
To maximize the 20 MB/s USB bandwidth, the STM32 packs two 12-bit ADC samples into a single 32-bit word before transmission.

On the host side, extracting this data requires high-speed bitwise manipulation:
*   **Masking:** A bitmask isolates the lower 12 bits for Sample A, and a right-shift isolates the upper 12 bits for Sample B.
*   **Sign Extension:** Because the 12-bit ADC values represent an alternating current, the 12th bit is actually the "sign bit." The host software uses arithmetic bit-shifting to force this 12th bit into the 16th-bit position. This mathematically converts the raw unsigned ADC readings into perfect signed 16-bit integers, restoring the true negative and positive voltage swings of the physical waveform without requiring heavy floating-point math during extraction.

## 4. Matrix Arrangement: Fast-Time vs. Slow-Time

To extract a target's velocity, a single radar sweep is insufficient. The radar must observe the target over a Coherent Processing Interval (CPI).

**The Design Choice: The Radar Data Cube (2D Matrix)**
The host collects exactly 128 UP/DOWN pairs to form one CPI (representing ~256 milliseconds of observation). The data is arranged into two distinct matrices (one for UP sweeps, one for DOWN sweeps).
*   **The X-Axis (Fast-Time):** Contains the 10,000 samples collected during a single 1 ms sweep. This axis captures the instantaneous beat frequency, which corresponds to the target's distance.
*   **The Y-Axis (Slow-Time):** Contains the 128 individual sweeps. This axis captures how the target moves from one sweep to the next, which corresponds to the target's velocity.

## 5. The Mathematics of the Dual 2D Fourier Transform

The core of the digital signal processing pipeline transforms these matrices from the time domain into the frequency domain to isolate targets.

**Step A: Windowing (Suppressing Spectral Leakage)**
Because the radar only observes the target for finite intervals (1 ms sweeps), the edges of the time-domain signal are abruptly cut off. In the frequency domain, this sudden cut-off convolves with the signal, creating a Sinc function ($\frac{\sin(x)}{x}$) that leaks noise across the spectrum. Before processing, the data is multiplied by a **Hann Window**—a bell-shaped curve that smoothly tapers the edges of the signal to zero, ensuring sharp, narrow frequency peaks.

**Step B: The Range FFT (Fast-Time Analysis)**
A 1D FFT is applied across the X-axis of the matrix. For a transmitted frequency $f_T$ and received frequency $f_R$, the beat frequency $f_b = |f_T - f_R|$. The FFT identifies the dominant beat frequencies in the sweep, yielding the target's apparent range.

**Step C: The Doppler FFT (Slow-Time Analysis)**
As a target moves a microscopic distance $\Delta d$ between sweeps, the reflected wave undergoes a phase shift:
$$ \Delta\phi = \frac{4\pi \Delta d}{\lambda} $$
A second 1D FFT is applied vertically across the Y-axis. This mathematical operation isolates the rate of change of $\Delta\phi$ across the 128 sweeps, separating targets based entirely on their radial velocity.

**Step D: The Zero-Velocity Shift**
Because standard FFT algorithms output frequencies starting from zero and wrapping around, the matrix is mathematically shifted to place $0$ Hz in the exact center. This maps approaching vehicles to the positive upper-half of the matrix, and receding vehicles to the negative lower-half.

## 6. Target Detection and Range-Doppler Decoupling

**The Engineering Challenge (Range-Doppler Coupling):**
In FMCW radar, a target's velocity imposes a Doppler shift on the returning wave.
*   If a vehicle is driving towards the radar, the wave is compressed (higher frequency).
*   During an UP sweep, this higher frequency makes the vehicle appear *closer* than it is.
*   During a DOWN sweep, this same higher frequency makes the vehicle appear *further* than it is.

**The Design Choice: Triangular Averaging**
This is the primary reason the system utilizes a triangular waveform and processes two separate matrices. By running the 2D FFT on both the UP matrix and the DOWN matrix, the system generates two separate Range-Doppler heatmaps.

The detection algorithm identifies the target's peak frequency in the UP heatmap ($f_{up}$) and its peak in the DOWN heatmap ($f_{down}$). Because the Doppler shift pushes these two peaks in exactly opposite directions by the exact same amount, the true, error-free range frequency ($f_{true}$) is isolated by simply averaging them:
$$ f_{true} = \frac{f_{up} + f_{down}}{2} $$

**Final Physical Conversion:**
With the exact, uncoupled beat frequency isolated, the absolute distance to the target ($R$) is calculated using the speed of light ($c$), the sweep bandwidth ($BW$), and the sweep duration ($T_c$):
$$ R = f_{true} \times \left( \frac{c \cdot T_c}{2 \cdot BW} \right) $$
The velocity ($v$) is extracted directly from the target's Y-axis position (the Doppler bin) on the heatmap, calculated as:
$$ v = f_{doppler} \times \frac{\lambda}{2} $$

Through this architecture, hardware-enforced synchronization and dual-matrix fourier mathematics combine to produce a high-resolution, uncoupled telemetry feed capable of tracking multiple high-speed targets simultaneously.
