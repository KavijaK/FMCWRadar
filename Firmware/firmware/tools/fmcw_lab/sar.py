"""Small SAR experiment helpers.

These routines are intentionally CPU-friendly and conservative. They are meant
for bench experiments after collecting a finite aperture, not for hard real-time
USB streaming.
"""

from __future__ import annotations

import numpy as np

from .dsp import C_M_S


def aperture_fft_image(range_profiles: np.ndarray, remove_static: bool = False) -> np.ndarray:
    data = range_profiles.astype(np.complex64, copy=True)
    if remove_static and data.shape[0] > 1:
        data -= np.mean(data, axis=0, keepdims=True)
    data *= np.hanning(data.shape[0]).astype(np.float32)[:, None]
    image = np.fft.fftshift(np.fft.fft(data, axis=0), axes=0)
    db = 20.0 * np.log10(np.abs(image) + 1.0e-12)
    return db - float(np.max(db))


def backprojection_image(
    range_profiles: np.ndarray,
    range_m: np.ndarray,
    platform_step_m: float,
    center_hz: float,
    x_extent_m: float = 2.0,
    y_min_m: float = 0.3,
    y_max_m: float = 5.0,
    nx: int = 96,
    ny: int = 160,
):
    """Simple monostatic stripmap backprojection.

    `range_profiles` should be complex range FFTs collected at uniform platform
    spacing. The returned image is approximate, but it is useful for checking
    whether aperture motion and phase coherence are present.
    """

    if range_profiles.ndim != 2 or range_profiles.shape[0] < 4:
        raise ValueError("Need at least four aperture profiles")

    x = np.linspace(-x_extent_m / 2.0, x_extent_m / 2.0, nx)
    y = np.linspace(y_min_m, y_max_m, ny)
    xx, yy = np.meshgrid(x, y, indexing="xy")
    image = np.zeros_like(xx, dtype=np.complex128)

    positions = (np.arange(range_profiles.shape[0]) - (range_profiles.shape[0] - 1) / 2.0) * platform_step_m
    phase_k = 4.0 * np.pi * center_hz / C_M_S

    for profile, pos in zip(range_profiles, positions):
        rr = np.sqrt((xx - pos) ** 2 + yy**2)
        real = np.interp(rr.ravel(), range_m, profile.real, left=0.0, right=0.0).reshape(rr.shape)
        imag = np.interp(rr.ravel(), range_m, profile.imag, left=0.0, right=0.0).reshape(rr.shape)
        vals = real + 1j * imag
        image += vals * np.exp(1j * phase_k * rr)

    db = 20.0 * np.log10(np.abs(image) + 1.0e-12)
    db -= float(np.max(db))
    return x, y, db

