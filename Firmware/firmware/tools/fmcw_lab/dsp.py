"""DSP primitives for live FMCW processing."""

from __future__ import annotations

import dataclasses
import math
from typing import Optional

import numpy as np


C_M_S = 299_792_458.0


@dataclasses.dataclass
class RadarConfig:
    sample_rate_hz: float = 10_000_000.0
    samples_per_slope: int = 10_000
    bandwidth_hz: float = 200_271_606.0
    chirp_s: float = 1.0e-3
    center_hz: float = 5_800_135_803.0
    adc_bits: int = 12

    @property
    def slope_hz_s(self) -> float:
        return self.bandwidth_hz / self.chirp_s

    @property
    def triangle_period_s(self) -> float:
        return 2.0 * self.chirp_s

    @property
    def wavelength_m(self) -> float:
        return C_M_S / self.center_hz

    @property
    def adc_full_scale(self) -> float:
        return float(2 ** (self.adc_bits - 1))


def range_axis(config: RadarConfig, nfft: int) -> np.ndarray:
    freq_hz = np.fft.rfftfreq(nfft, d=1.0 / config.sample_rate_hz)
    return C_M_S * freq_hz / (2.0 * config.slope_hz_s)


def doppler_velocity_axis(config: RadarConfig, cpi_chirps: int, slow_decimation: int = 1) -> np.ndarray:
    slow_period = config.triangle_period_s * max(1, slow_decimation)
    fd = np.fft.fftshift(np.fft.fftfreq(cpi_chirps, d=slow_period))
    return fd * config.wavelength_m / 2.0


def hann_window(length: int) -> np.ndarray:
    return np.hanning(length).astype(np.float32)


def range_fft(samples: np.ndarray, config: RadarConfig, nfft: int, window: Optional[np.ndarray] = None) -> np.ndarray:
    x = samples.astype(np.float32, copy=False)
    x = x - np.mean(x)
    if window is None:
        window = hann_window(len(x))
    return np.fft.rfft(x * window, n=nfft)


def dbfs_from_fft(spectrum: np.ndarray, sample_count: int, config: RadarConfig, window: Optional[np.ndarray]) -> np.ndarray:
    if window is None:
        coherent_gain = 1.0
    else:
        coherent_gain = float(np.sum(window) / len(window))
    mag = np.abs(spectrum)
    mag = mag / max(sample_count * coherent_gain / 2.0, 1.0e-12)
    return 20.0 * np.log10((mag / config.adc_full_scale) + 1.0e-15)


def range_profile_dbfs(samples: np.ndarray, config: RadarConfig, nfft: int, window: Optional[np.ndarray] = None):
    spec = range_fft(samples, config, nfft, window)
    return spec, dbfs_from_fft(spec, len(samples), config, window)


def estimate_peak(range_m: np.ndarray, db: np.ndarray, min_range_m: float, max_range_m: float, ignore_bins: int = 2):
    valid = np.flatnonzero((range_m >= min_range_m) & (range_m <= max_range_m))
    valid = valid[valid >= ignore_bins]
    if len(valid) == 0:
        return None

    local = valid[int(np.argmax(db[valid]))]
    if local <= 0 or local >= len(db) - 1:
        return int(local), float(range_m[local]), float(db[local])

    y0 = float(db[local - 1])
    y1 = float(db[local])
    y2 = float(db[local + 1])
    denom = y0 - 2.0 * y1 + y2
    if abs(denom) < 1.0e-9:
        return int(local), float(range_m[local]), y1

    frac = 0.5 * (y0 - y2) / denom
    frac = max(-0.5, min(0.5, frac))
    bin_spacing = float(range_m[1] - range_m[0])
    return int(local), float(range_m[local] + frac * bin_spacing), y1


def range_doppler_db(range_fft_stack: np.ndarray, remove_clutter: bool = True) -> np.ndarray:
    cube = range_fft_stack.astype(np.complex64, copy=True)
    if remove_clutter and cube.shape[0] > 1:
        cube -= np.mean(cube, axis=0, keepdims=True)
    slow_window = np.hanning(cube.shape[0]).astype(np.float32)
    cube *= slow_window[:, None]
    rd = np.fft.fftshift(np.fft.fft(cube, axis=0), axes=0)
    rd_db = 20.0 * np.log10(np.abs(rd) + 1.0e-12)
    return rd_db - float(np.max(rd_db))


def estimate_range_doppler_peak(
    rd_db: np.ndarray,
    range_m: np.ndarray,
    velocity_m_s: np.ndarray,
    min_range_m: float = 0.3,
    max_range_m: float | None = None,
    min_abs_velocity_m_s: float = 0.02,
):
    """Return the strongest moving target in a range-Doppler map.

    `rd_db` is expected to be shaped as [doppler_bin, range_bin] and normalized
    so the strongest cell is near 0 dB. A small velocity exclusion removes the
    zero-Doppler clutter ridge from the measurement.
    """

    if rd_db.size == 0 or len(range_m) == 0 or len(velocity_m_s) == 0:
        return None
    if max_range_m is None:
        max_range_m = float(range_m[-1])

    valid_range = (range_m >= min_range_m) & (range_m <= max_range_m)
    valid_velocity = np.abs(velocity_m_s) >= min_abs_velocity_m_s
    if not np.any(valid_range) or not np.any(valid_velocity):
        return None

    mask = valid_velocity[:, None] & valid_range[None, :]
    masked = np.where(mask, rd_db, -np.inf)
    if not np.isfinite(masked).any():
        return None

    doppler_i, range_i = np.unravel_index(int(np.argmax(masked)), masked.shape)
    return {
        "range_m": float(range_m[range_i]),
        "velocity_m_s": float(velocity_m_s[doppler_i]),
        "level_db": float(rd_db[doppler_i, range_i]),
        "range_bin": int(range_i),
        "doppler_bin": int(doppler_i),
    }


def wrap_phase_delta(delta: float) -> float:
    return (delta + math.pi) % (2.0 * math.pi) - math.pi


def detrend_linear(t: np.ndarray, y: np.ndarray) -> np.ndarray:
    if len(y) < 3:
        return y - np.mean(y)
    tt = t - t[0]
    slope, intercept = np.polyfit(tt, y, 1)
    return y - (slope * tt + intercept)


def _refine_peak_frequency(freqs: np.ndarray, mag: np.ndarray, peak_index: int) -> float:
    if peak_index <= 0 or peak_index >= len(mag) - 1:
        return float(freqs[peak_index])
    y0 = math.log(float(mag[peak_index - 1]) + 1.0e-18)
    y1 = math.log(float(mag[peak_index]) + 1.0e-18)
    y2 = math.log(float(mag[peak_index + 1]) + 1.0e-18)
    denom = y0 - 2.0 * y1 + y2
    if abs(denom) < 1.0e-18:
        return float(freqs[peak_index])
    offset = 0.5 * (y0 - y2) / denom
    offset = max(-0.5, min(0.5, offset))
    return float(freqs[peak_index] + offset * (freqs[1] - freqs[0]))


def band_limited_analysis(
    times_s: np.ndarray,
    displacement_m: np.ndarray,
    band_hz: tuple[float, float],
    window_s: float,
    min_analysis_s: float,
    zeropad: int = 4,
):
    if len(times_s) < 8:
        return None
    t = np.asarray(times_s, dtype=np.float64)
    y_mm = np.asarray(displacement_m, dtype=np.float64) * 1000.0
    keep = t >= (t[-1] - window_s)
    t = t[keep]
    y_mm = y_mm[keep]
    if len(t) < 8 or (t[-1] - t[0]) < min_analysis_s:
        return None

    dt = np.diff(t)
    dt = dt[dt > 0]
    if len(dt) == 0:
        return None
    slow_fs = 1.0 / np.median(dt)
    if slow_fs < 2.0 * band_hz[1]:
        return None

    uniform_t = np.arange(t[0], t[-1], 1.0 / slow_fs)
    if len(uniform_t) < 8:
        return None

    y = np.interp(uniform_t, t, y_mm)
    y = detrend_linear(uniform_t, y)

    fft_len = int(2 ** math.ceil(math.log2(max(len(y), 8))))
    fft_len *= max(1, int(zeropad))
    spec = np.fft.rfft(y * np.hanning(len(y)), n=fft_len)
    freq = np.fft.rfftfreq(fft_len, d=1.0 / slow_fs)
    mag = np.abs(spec)
    band = (freq >= band_hz[0]) & (freq <= band_hz[1])
    if not np.any(band):
        return None
    band_idx = np.flatnonzero(band)
    peak_idx = int(band_idx[np.argmax(mag[band])])
    peak_hz = _refine_peak_frequency(freq, mag, peak_idx)

    filt_spec = np.fft.rfft(y)
    filt_freq = np.fft.rfftfreq(len(y), d=1.0 / slow_fs)
    filt_band = (filt_freq >= band_hz[0]) & (filt_freq <= band_hz[1])
    filt_spec[~filt_band] = 0.0
    wave_mm = np.fft.irfft(filt_spec, n=len(y))

    spec_db = 20.0 * np.log10(mag[band] + 1.0e-12)
    spec_db -= float(np.max(spec_db))
    confidence = float((mag[peak_idx] + 1.0e-12) / (np.median(mag[band]) + 1.0e-12))
    return {
        "t": uniform_t - uniform_t[-1],
        "detrended_mm": y,
        "wave_mm": wave_mm,
        "spectrum_hz": freq[band],
        "spectrum_db": spec_db,
        "peak_hz": peak_hz,
        "rate_per_min": peak_hz * 60.0,
        "confidence": confidence,
        "slow_fs": slow_fs,
    }


class PhaseTracker:
    def __init__(self, config: RadarConfig, max_points: int = 12000):
        self.config = config
        self.max_points = max_points
        self.target_bin = None
        self.last_phase = None
        self.unwrapped_phase = 0.0
        self.times = []
        self.displacement = []

    def reset(self):
        self.last_phase = None
        self.unwrapped_phase = 0.0
        self.times.clear()
        self.displacement.clear()

    def set_target_bin(self, target_bin: int):
        if self.target_bin != target_bin:
            self.target_bin = int(target_bin)
            self.reset()

    def update(self, timestamp_s: float, spectrum: np.ndarray, half_width: int = 0):
        if self.target_bin is None:
            return None
        start = max(0, self.target_bin - half_width)
        stop = min(len(spectrum), self.target_bin + half_width + 1)
        z = np.sum(spectrum[start:stop])
        phase = math.atan2(float(z.imag), float(z.real))
        if self.last_phase is None:
            self.last_phase = phase
            self.unwrapped_phase = 0.0
        else:
            self.unwrapped_phase += wrap_phase_delta(phase - self.last_phase)
            self.last_phase = phase

        motion = self.unwrapped_phase * self.config.wavelength_m / (4.0 * math.pi)
        if self.times and timestamp_s <= self.times[-1]:
            timestamp_s = self.times[-1] + self.config.triangle_period_s
        self.times.append(float(timestamp_s))
        self.displacement.append(float(motion))
        if len(self.times) > self.max_points:
            self.times = self.times[-self.max_points :]
            self.displacement = self.displacement[-self.max_points :]
        return motion

    def arrays(self):
        return np.asarray(self.times, dtype=np.float64), np.asarray(self.displacement, dtype=np.float64)
