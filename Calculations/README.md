## 1. Distance (Range) Measurements and Resolution

In an FMCW radar, distance is measured by calculating the beat frequency ($f_b$) between the transmitted and received signals. To perform this range extraction, the system captures **512 samples per chirp** to build the first dimension of the 2D range-Doppler matrix using a Fast Fourier Transform (FFT).

The system operates with a triangular waveform, sweeping from 5.7 GHz to 5.9 GHz. This provides a total sweep bandwidth ($B$) of 200 MHz. The chirp duration ($T_c$) is 2 ms (0.002 seconds).

**Range Derivation ($R$)**

The target distance is directly proportional to the measured beat frequency. By substituting the speed of light ($c = 3 \times 10^8$ m/s), the 2 ms chirp duration, and the 200 MHz bandwidth into the standard FMCW range equation:

$$
R = \frac{c \cdot T_c \cdot f_b}{2B}
$$

$$
R = \frac{(3 \times 10^8) \cdot 0.002}{2 \cdot (200 \times 10^6)} \cdot f_b
$$

$$
R = 0.0015 \cdot f_b
$$

This coefficient dictates that for every 1 Hz of beat frequency detected by the FFT, the target is 0.0015 meters away. 

**Range Resolution ($\Delta R$)**

The range resolution defines the radar's ability to distinguish between two closely spaced targets in traffic. It is dictated entirely by the 200 MHz bandwidth:

$$
\Delta R = \frac{c}{2B}
$$

$$
\Delta R = \frac{3 \times 10^8}{400 \times 10^6} = 0.75
$$

The system provides a strict range resolution of **0.75 meters**.

**Maximum ADC Limit**

Because the transmitted bandwidth sweeps from 5.7 GHz to 5.9 GHz over 2 ms, the maximum expected beat frequency ($f_b$) for typical traffic targets maxes out at approximately 200 kHz. Since the external ADC operates at a massive 10 MHz sampling frequency, the Nyquist limit (5 MHz) is vastly higher than the 200 kHz beat signal. Therefore, the ADC limit does not constrain or affect the range calculations; the maximum detectable range is strictly limited by the physical transmitting power and environmental losses.

---

## 2. Velocity (Doppler) Measurements and Resolution

Velocity tracking in this architecture employs a dual-method approach to ensure precise slow-speed resolution while accommodating high-speed traffic. The system builds a 2D matrix using **128 chirps** ($N_c$) and 512 samples per chirp to perform a 2D Doppler FFT. For these calculations, the wavelength ($\lambda$) at 5.8 GHz is 0.0517 meters.

**Doppler Velocity Derivation ($v$)**

For micro-Doppler and fine speed measurements, velocity is extracted by analyzing the phase shift ($\Delta \phi$) of the beat signal across consecutive chirps:

$$
v = \frac{\lambda \cdot \Delta \phi}{4\pi T_c}
$$

$$
v = \frac{0.0517 \cdot \Delta \phi}{4\pi \cdot 0.002}
$$

$$
v \approx 2.057 \cdot \Delta \phi
$$

**Velocity Resolution ($\Delta v$)**

The resolution defines the minimum speed difference required to separate two targets moving at the exact same distance. It is defined by the total observation frame time (128 chirps $\times$ 2 ms = 0.256 seconds):

$$
\Delta v = \frac{\lambda}{2 N_c T_c}
$$

$$
\Delta v = \frac{0.0517}{2 \cdot 128 \cdot 0.002}
$$

$$
\Delta v \approx 0.101
$$

The 2D matrix yields a highly sensitive velocity resolution of **0.101 m/s**.

**Maximum Unambiguous Doppler Velocity ($v_{max}$) and Range-Rate Tracking**

To prevent velocity aliasing in the Doppler phase, the phase shift between chirps must remain under 180 degrees. Using the 2 ms chirp time, the theoretical limit is:

$$
v_{max} = \frac{\lambda}{4 T_c} = \frac{0.0517}{4 \cdot 0.002} = 6.4625
$$

Because standard traffic routinely exceeds this **6.46 m/s** Doppler limit, the system utilizes **Range-Rate Tracking** to measure high-speed vehicles. By tracking the absolute movement of a target's range bin ($\Delta R$) across consecutive radar frames ($\Delta t$), the processing backend extracts the true absolute velocity ($v = \frac{\Delta R}{\Delta t}$). This completely bypasses the Doppler aliasing constraint, allowing accurate tracking of vehicles traveling at 20 m/s and beyond.

---

## 3. Power Propagation and The Radar Equation

The physical limit of the radar's detection capabilities relies on how much of the transmitted power makes it back to the receiver. The system transmits at an output power of **23 dBm**, which equates to **0.2 Watts** of linear power. 

**Deriving the Radar Equation**

The journey of the RF signal is mapped using the standard radar range equation:
1.  **Transmission:** The 0.2 W signal is focused by the directional gain ($G$) of the antenna, creating a power density at the target of $\frac{P_t G}{4\pi R^2}$.
2.  **Target Reflection:** The target (e.g., a pedestrian with a Radar Cross Section $\sigma$ of 1 square meter) reflects a portion of this energy back: $\frac{P_t G \sigma}{4\pi R^2}$.
3.  **Capture:** The receiving antenna captures this energy based on its effective aperture ($A_e = \frac{G \lambda^2}{4\pi}$). 

Combining these gives the total received power ($P_r$):

$$
P_r = \frac{P_t G^2 \lambda^2 \sigma}{(4\pi)^3 R^4}
$$

**Real-World Losses and the 100m Detection Threshold**

Power drops off as the inverse fourth power of the distance ($\frac{1}{R^4}$), meaning the returning signal weakens rapidly. Given real-world thermal losses, atmospheric spreading, and a realistic thermal noise floor of roughly -103 dBm for the active bandwidth, we can map the power at the receiver for a 1 square meter target using a 10 dBi antenna.

**Received Power vs. Distance Chart (1 square meter Target)**

| Distance | Received Power (Watts) | Received Power (dBm) |
| :--- | :--- | :--- |
| **10 m** | $2.694 \times 10^{-9}$ | **-55.69 dBm** |
| **20 m** | $1.684 \times 10^{-10}$ | **-67.73 dBm** |
| **30 m** | $3.326 \times 10^{-11}$ | **-74.78 dBm** |
| **40 m** | $1.052 \times 10^{-11}$ | **-79.77 dBm** |
| **50 m** | $4.310 \times 10^{-12}$ | **-83.65 dBm** |
| **60 m** | $2.078 \times 10^{-12}$ | **-86.82 dBm** |
| **70 m** | $1.121 \times 10^{-12}$ | **-89.50 dBm** |
| **80 m** | $6.577 \times 10^{-13}$ | **-91.82 dBm** |
| **90 m** | $4.106 \times 10^{-13}$ | **-93.86 dBm** |
| **100 m** | $2.694 \times 10^{-13}$ | **-95.69 dBm** |

At 100 meters, the returning power is -95.69 dBm. This sits safely above the thermal noise floor, providing a mathematically viable signal-to-noise ratio to detect a person at that distance before requiring extensive digital frame averaging to pull the signal out of the clutter.
