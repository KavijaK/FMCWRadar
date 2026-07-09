#!/usr/bin/env python3
"""PyQtGraph Range-Doppler viewer copied from Triangular.py.

The USB acquisition and triangular up/down slope pairing are intentionally the
same as Triangular.py. The display path is different:

  * PyQtGraph GUI instead of Matplotlib.
  * Zero-padded range and Doppler FFTs for a finer visual grid.
  * Smooth bilinear display upsampling.
  * Colorbar legend showing relative magnitude in dB.

This is a visualization tool. Zero-padding and display upsampling make the map
look smoother, but they do not change the physical radar resolution.
"""

import sys
import time
from multiprocessing import Process, Queue
from queue import Empty, Full

import numpy as np
import usb.core
import usb.util
from usb.backend import libusb1

try:
    import libusb_package
except ImportError:
    libusb_package = None

try:
    import pyqtgraph as pg
    from pyqtgraph.Qt import QtCore, QtGui, QtWidgets
except ImportError:
    pg = None
    QtCore = None
    QtGui = None
    QtWidgets = None


# ---------------------------------------------------------
# PHYSICAL & RADAR PARAMETERS
# ---------------------------------------------------------
C = 3e8
FC = 5.8e9
BW = 200e6
TC = 1e-3
IDLE_TIME = 0.1e-3
CHIRP_PERIOD = (2 * TC) + IDLE_TIME
LAMBDA = C / FC

# ---------------------------------------------------------
# HARDWARE & DATA CONSTANTS (10 MHz Triangular)
# ---------------------------------------------------------
SAMPLING_FREQ = 10e6
SAMPLES_PER_CHIRP = int(SAMPLING_FREQ * TC)
BYTES_PER_CHIRP = SAMPLES_PER_CHIRP * 2
BYTES_PER_CYCLE = BYTES_PER_CHIRP * 2
CHIRPS_PER_FRAME = 128

# Display FFT sizes. These are zero-padding sizes for visual smoothness.
RANGE_NFFT = 32768
DOPPLER_NFFT = 512
MAX_DISPLAY_RANGE_M = 100.0
DYNAMIC_RANGE_DB = 65.0
DISPLAY_SMOOTHING = True
DISPLAY_NOISE_REDUCTION = True
DISPLAY_SIGNAL_KNEE_DB = -18.0
DISPLAY_NOISE_GAMMA = 2.2

# ---------------------------------------------------------
# STM32 USB STREAM CONSTANTS
# ---------------------------------------------------------
VID = 0x1209
PID = 0x4158
EP_IN = 0x81
REQ_START_STREAM = 0x30

FRAME_LEN = 20480
HEADER_LEN = 64
PAYLOAD_WORDS = 5000
MAGIC = 0x52444152
FRAMES_PER_READ = 4

HEADER = np.dtype([
    ("magic", "<u4"),
    ("version", "<u2"),
    ("header_len", "<u2"),
    ("frame_len", "<u4"),
    ("slope_seq", "<u4"),
    ("triangle_seq", "<u4"),
    ("slope_id", "<u2"),
    ("flags", "<u2"),
    ("sample_rate_hz", "<u4"),
    ("samples_per_slope", "<u4"),
    ("adc_bits", "<u2"),
    ("words_per_slope", "<u2"),
    ("timestamp_us", "<u4"),
    ("muxout_count", "<u4"),
    ("dropped_frames", "<u4"),
    ("dcmi_risr", "<u4"),
    ("dma_lisr", "<u4"),
    ("reserved0", "<u4"),
    ("reserved1", "<u4"),
])


def get_backend():
    if libusb_package is None:
        return libusb1.get_backend()
    return libusb1.get_backend(find_library=libusb_package.find_library)


def open_usb_device():
    backend = get_backend()
    if backend is None:
        raise RuntimeError("No PyUSB/libusb backend found. Install pyusb and libusb-package.")

    access_hint_printed = False
    while True:
        dev = usb.core.find(idVendor=VID, idProduct=PID, backend=backend)
        if dev is None:
            print("[PRODUCER] Waiting for FMCW USB HS stream device...")
            time.sleep(0.5)
            continue

        try:
            dev.set_configuration()
            print(f"\n[PRODUCER] FMCW USB Radar Connected: VID=0x{VID:04X} PID=0x{PID:04X}")
            return dev
        except usb.core.USBError as exc:
            if getattr(exc, "errno", None) == 13 and not access_hint_printed:
                print("[PRODUCER] Device visible, but Windows denied access.")
                print("[PRODUCER] Close other USB viewers or bind the interface to WinUSB with Zadig.")
                access_hint_printed = True
            else:
                print(f"[PRODUCER] USB open/config failed: {exc}. Retrying...")
            usb.util.dispose_resources(dev)
            time.sleep(0.5)


def read_exact(dev, size):
    chunks = []
    remaining = size
    while remaining:
        try:
            chunk = bytes(dev.read(EP_IN, remaining, timeout=2000))
        except usb.core.USBTimeoutError as exc:
            raise TimeoutError("USB read timeout") from exc
        if not chunk:
            raise TimeoutError("USB read returned no data")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def decode_usb_slope_payload(frame):
    words = np.frombuffer(frame, dtype="<u4", count=PAYLOAD_WORDS, offset=HEADER_LEN)
    lo = (words & 0x0FFF).astype(np.int16)
    hi = ((words >> 16) & 0x0FFF).astype(np.int16)

    lo = ((lo ^ 0x0800) - 0x0800).astype(np.int16)
    hi = ((hi ^ 0x0800) - 0x0800).astype(np.int16)

    samples = np.empty(SAMPLES_PER_CHIRP, dtype=np.uint16)
    samples[0::2] = (lo.astype(np.int32) + 2048).astype(np.uint16)
    samples[1::2] = (hi.astype(np.int32) + 2048).astype(np.uint16)
    return samples


def data_producer(queue):
    """Reads STM32 USB frames and pushes 40,000-byte UP+DOWN cycles."""

    dev = open_usb_device()
    dev.ctrl_transfer(0x40, REQ_START_STREAM, 0, 0, None)
    print("[PRODUCER] Stream started. Pairing slope_id 0/1 into triangular cycles.")

    current_up_slope = None

    while True:
        try:
            batch = read_exact(dev, FRAME_LEN * FRAMES_PER_READ)

            for offset in range(0, len(batch), FRAME_LEN):
                frame = batch[offset:offset + FRAME_LEN]
                if len(frame) != FRAME_LEN:
                    continue

                header = np.frombuffer(frame, dtype=HEADER, count=1, offset=0)[0]
                if int(header["magic"]) != MAGIC:
                    continue
                if int(header["header_len"]) != HEADER_LEN:
                    continue
                if int(header["frame_len"]) != FRAME_LEN:
                    continue
                if int(header["samples_per_slope"]) != SAMPLES_PER_CHIRP:
                    print("[PRODUCER] Header sample count does not match Triangular_pyqt.py constants.")
                    continue

                slope_samples = decode_usb_slope_payload(frame)
                slope_id = int(header["slope_id"])

                if slope_id == 0:
                    current_up_slope = slope_samples
                elif slope_id == 1 and current_up_slope is not None:
                    pure_data = current_up_slope.tobytes() + slope_samples.tobytes()
                    try:
                        queue.put_nowait(pure_data)
                    except Full:
                        pass
                    current_up_slope = None

        except TimeoutError as exc:
            print(f"[PRODUCER] USB warning: {exc}")
            continue
        except usb.core.USBError as exc:
            print(f"\n[PRODUCER] USB connection lost: {exc}")
            usb.util.dispose_resources(dev)
            dev = open_usb_device()
            dev.ctrl_transfer(0x40, REQ_START_STREAM, 0, 0, None)
            current_up_slope = None
        except KeyboardInterrupt:
            break

    usb.util.dispose_resources(dev)


def make_turbo_lut():
    cmap = pg.colormap.get("turbo")
    return cmap.getLookupTable(0.0, 1.0, 256)


def resize_bilinear(image, out_rows, out_cols):
    """Small NumPy bilinear resizer for smooth display only."""
    rows, cols = image.shape
    if rows == out_rows and cols == out_cols:
        return image

    y = np.linspace(0, rows - 1, out_rows)
    x = np.linspace(0, cols - 1, out_cols)
    y0 = np.floor(y).astype(np.int32)
    x0 = np.floor(x).astype(np.int32)
    y1 = np.minimum(y0 + 1, rows - 1)
    x1 = np.minimum(x0 + 1, cols - 1)
    wy = (y - y0).astype(np.float32)
    wx = (x - x0).astype(np.float32)

    top = image[y0[:, None], x0[None, :]] * (1.0 - wx)[None, :] + image[y0[:, None], x1[None, :]] * wx[None, :]
    bottom = image[y1[:, None], x0[None, :]] * (1.0 - wx)[None, :] + image[y1[:, None], x1[None, :]] * wx[None, :]
    return top * (1.0 - wy)[:, None] + bottom * wy[:, None]


def smooth_heatmap(image):
    """Light display-only smoothing to reduce speckle without changing DSP."""
    padded = np.pad(image, ((1, 1), (1, 1)), mode="edge")
    return (
        4.0 * padded[1:-1, 1:-1]
        + 2.0 * padded[:-2, 1:-1]
        + 2.0 * padded[2:, 1:-1]
        + 2.0 * padded[1:-1, :-2]
        + 2.0 * padded[1:-1, 2:]
        + padded[:-2, :-2]
        + padded[:-2, 2:]
        + padded[2:, :-2]
        + padded[2:, 2:]
    ) / 16.0


def reduce_display_noise(rd_rel_db):
    """Compress weak map values toward the purple floor.

    Values above DISPLAY_SIGNAL_KNEE_DB are kept mostly unchanged. Values
    below it are gamma-compressed, so noise becomes quieter visually while
    strong targets remain visible. This is a display filter, not a detection
    filter.
    """
    floor = -DYNAMIC_RANGE_DB
    knee = DISPLAY_SIGNAL_KNEE_DB
    out = np.array(rd_rel_db, dtype=np.float32, copy=True)
    weak = out < knee
    if np.any(weak):
        norm = (out[weak] - floor) / (knee - floor)
        norm = np.clip(norm, 0.0, 1.0) ** DISPLAY_NOISE_GAMMA
        out[weak] = floor + norm * (knee - floor)
    return out


def process_2d_fft(matrix, range_window, doppler_window, range_bins):
    matrix_windowed_1d = matrix * range_window
    range_fft = np.fft.fft(matrix_windowed_1d, n=RANGE_NFFT, axis=1)
    range_fft = range_fft[:, range_bins]

    matrix_windowed_2d = range_fft * doppler_window
    doppler_fft = np.fft.fft(matrix_windowed_2d, n=DOPPLER_NFFT, axis=0)
    range_doppler_map = np.fft.fftshift(doppler_fft, axes=0)

    return np.abs(range_doppler_map)


class RangeDopplerWindow(QtWidgets.QMainWindow):
    def __init__(self, queue):
        super().__init__()
        self.queue = queue
        self.setWindowTitle("Triangular.py PyQt Range-Doppler Heatmap")
        self.resize(1350, 900)

        self.range_window = np.hanning(SAMPLES_PER_CHIRP).astype(np.float32)
        self.doppler_window = np.hanning(CHIRPS_PER_FRAME).astype(np.float32).reshape(-1, 1)
        self.up_frame_data = []
        self.down_frame_data = []
        self.frames_processed = 0
        self.last_update = time.monotonic()
        self.fps = 0.0

        self.range_axis = self.make_range_axis()
        self.range_bins = np.flatnonzero(self.range_axis <= MAX_DISPLAY_RANGE_M)
        self.range_axis = self.range_axis[self.range_bins]
        self.velocity_axis = self.make_velocity_axis()

        self.display_rows = 720
        self.display_cols = 1100

        self._setup_ui()

        self.timer = QtCore.QTimer(self)
        self.timer.timeout.connect(self.update_from_queue)
        self.timer.start(1)

    def make_range_axis(self):
        freq_res = SAMPLING_FREQ / RANGE_NFFT
        freqs = np.arange(RANGE_NFFT // 2, dtype=np.float64) * freq_res
        return freqs * (C * TC / (2 * BW))

    def make_velocity_axis(self):
        prf = 1.0 / CHIRP_PERIOD
        doppler_freq = (np.arange(DOPPLER_NFFT, dtype=np.float64) - (DOPPLER_NFFT // 2)) * (prf / DOPPLER_NFFT)
        return doppler_freq * (LAMBDA / 2.0)

    def _setup_ui(self):
        pg.setConfigOptions(imageAxisOrder="row-major", antialias=True)
        pg.setConfigOption("background", "#101015")
        pg.setConfigOption("foreground", "#dddddd")

        central = pg.GraphicsLayoutWidget()
        self.setCentralWidget(central)

        self.status = pg.LabelItem(justify="left")
        central.addItem(self.status, row=0, col=0, colspan=2)

        self.plot = central.addPlot(row=1, col=0, title="Smooth Range-Doppler Heatmap (UP Ramp)")
        self.plot.setLabel("bottom", "Range", units="m")
        self.plot.setLabel("left", "Velocity", units="m/s")
        self.plot.setXRange(0, MAX_DISPLAY_RANGE_M)
        self.plot.setYRange(float(self.velocity_axis[0]), float(self.velocity_axis[-1]))
        self.plot.showGrid(x=True, y=True, alpha=0.18)

        self.image = pg.ImageItem(axisOrder="row-major")
        self.image.setLookupTable(make_turbo_lut())
        self.image.setLevels([-DYNAMIC_RANGE_DB, 0.0])
        self.plot.addItem(self.image)

        self.target_marker = pg.ScatterPlotItem(
            size=11,
            brush=pg.mkBrush(255, 255, 255, 220),
            pen=pg.mkPen("k", width=1.5),
        )
        self.plot.addItem(self.target_marker)

        colorbar = pg.ColorBarItem(
            values=(-DYNAMIC_RANGE_DB, 0.0),
            colorMap=pg.colormap.get("turbo"),
            label="Relative intensity (dB): yellow/red = strong, blue/purple = weak",
        )
        colorbar.setImageItem(self.image, insert_in=self.plot)

        notes = pg.LabelItem(justify="left")
        notes.setText(
            "Color legend:<br>"
            "<span style='color:#fff36a'>yellow/red</span>: strongest return near 0 dB<br>"
            "<span style='color:#30b8ff'>cyan/blue</span>: weaker return<br>"
            "<span style='color:#4b1f9f'>purple</span>: noise floor / very weak<br><br>"
            "Display uses range/Doppler zero-padding and bilinear upsampling for a smooth view.",
            color="#dddddd",
        )
        central.addItem(notes, row=1, col=1)

    def update_from_queue(self):
        drained = 0
        while drained < 12:
            try:
                raw_bytes = self.queue.get_nowait()
            except Empty:
                break
            self.add_cycle(raw_bytes)
            drained += 1

    def add_cycle(self, raw_bytes):
        signal = np.frombuffer(raw_bytes, dtype=np.uint16).astype(np.float32)
        up_sig = signal[:SAMPLES_PER_CHIRP]
        down_sig = signal[SAMPLES_PER_CHIRP:]

        up_sig = up_sig - np.mean(up_sig)
        down_sig = down_sig - np.mean(down_sig)

        self.up_frame_data.append(up_sig)
        self.down_frame_data.append(down_sig)

        if len(self.up_frame_data) >= CHIRPS_PER_FRAME:
            self.process_frame()
            self.up_frame_data.clear()
            self.down_frame_data.clear()

    def process_frame(self):
        up_matrix = np.vstack(self.up_frame_data)
        down_matrix = np.vstack(self.down_frame_data)

        rd_mag_up = process_2d_fft(up_matrix, self.range_window, self.doppler_window, self.range_bins)
        rd_mag_down = process_2d_fft(down_matrix, self.range_window, self.doppler_window, self.range_bins)

        rd_db = 20.0 * np.log10(rd_mag_up + 1.0e-10)
        peak_db = float(np.max(rd_db))
        rd_rel_db = np.clip(rd_db - peak_db, -DYNAMIC_RANGE_DB, 0.0).astype(np.float32)
        if DISPLAY_NOISE_REDUCTION:
            rd_rel_db = reduce_display_noise(rd_rel_db)
        if DISPLAY_SMOOTHING:
            rd_rel_db = smooth_heatmap(rd_rel_db).astype(np.float32)

        smooth = resize_bilinear(rd_rel_db, self.display_rows, self.display_cols).astype(np.float32)
        self.image.setImage(smooth, autoLevels=False)
        self.image.setRect(QtCore.QRectF(
            0.0,
            float(self.velocity_axis[0]),
            MAX_DISPLAY_RANGE_M,
            float(self.velocity_axis[-1] - self.velocity_axis[0]),
        ))

        target_idx_up = np.unravel_index(np.argmax(rd_mag_up), rd_mag_up.shape)
        doppler_bin = int(target_idx_up[0])
        range_bin_up = int(self.range_bins[int(target_idx_up[1])])

        target_idx_down = np.unravel_index(np.argmax(rd_mag_down), rd_mag_down.shape)
        range_bin_down = int(self.range_bins[int(target_idx_down[1])])

        freq_res = SAMPLING_FREQ / RANGE_NFFT
        f_up = range_bin_up * freq_res
        f_down = range_bin_down * freq_res
        f_true = (f_up + f_down) / 2.0
        target_range = f_true * (C * TC / (2 * BW))

        doppler_shifted_idx = doppler_bin - (DOPPLER_NFFT // 2)
        prf = 1.0 / CHIRP_PERIOD
        doppler_freq = doppler_shifted_idx * (prf / DOPPLER_NFFT)
        target_velocity = doppler_freq * (LAMBDA / 2.0)
        self.target_marker.setData([target_range], [target_velocity])

        self.frames_processed += 1
        now = time.monotonic()
        dt = now - self.last_update
        if dt > 0.0:
            self.fps = 0.85 * self.fps + 0.15 * (1.0 / dt)
        self.last_update = now

        self.status.setText(
            f"<span style='color:#ffffff'>"
            f"Range-Doppler frame={self.frames_processed} &nbsp; GUI FPS={self.fps:.1f} &nbsp; "
            f"Target range={target_range:.2f} m &nbsp; velocity={target_velocity:.2f} m/s &nbsp; "
            f"peak={peak_db:.1f} dB &nbsp; color scale=[-{DYNAMIC_RANGE_DB:.0f}, 0] dB relative &nbsp; "
            f"noise filter={'on' if DISPLAY_NOISE_REDUCTION else 'off'}"
            f"</span>"
        )


def main():
    if pg is None:
        raise SystemExit("Install dependencies: python -m pip install pyusb libusb-package numpy pyqtgraph PyQt6")

    data_queue = Queue(maxsize=128)
    producer_process = Process(target=data_producer, args=(data_queue,))
    producer_process.start()

    app = QtWidgets.QApplication([])
    win = RangeDopplerWindow(data_queue)
    win.show()

    try:
        exit_code = app.exec()
    finally:
        producer_process.terminate()
        producer_process.join(timeout=1.0)
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
