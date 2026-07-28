#  Velocity Estimation (Doppler FFT)

This module processes the **Slow-Time** data. It uses the "Double FFT" architecture to extract velocity information independently of the range measurement.


##  The Mathematics

The Doppler frequency ($f_d$) is the derivative of the phase over time:
$$f_d = \frac{1}{2\pi} \cdot \frac{d\phi}{dt}$$

Once $f_d$ is extracted via the second FFT, radial velocity ($v$) is calculated as:
$$v = \frac{f_d \cdot \lambda}{2}$$

### The Nyquist Velocity Limit ($v_{max}$)
If a target moves so fast that the phase rotates more than $180^\circ$ ($\pi$ radians) between two chirps, the system experiences velocity aliasing. The maximum measurable speed is constrained by the Chirp Repetition Interval ($T_{pri}$):
$$v_{max} = \frac{\lambda}{4 \cdot T_{pri}}$$

To measure faster targets (e.g., highway traffic), $T_{pri}$ must be minimized in the hardware configuration.

##  The "Phase Shift" Concept
The Range FFT (Fast-Time) is not precise enough to measure the tiny distance a target moves during a single chirp ($T_c$). However, because the radar wavelength ($\lambda$) is very small, even a millimeter of movement causes a massive shift in the **Phase ($\phi$)** of the reflected signal.

By analyzing the same Range Bin across $M$ consecutive chirps, we track the rate of phase rotation. 



