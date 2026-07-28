#!/usr/bin/env python3
"""FMCW Triangular radar with pyqtgraph live Range-Doppler heatmap."""

import sys
import time
from multiprocessing import Process, Queue
from queue import Full

import numpy as np
from scipy.ndimage import zoom
import pyqtgraph as pg
from pyqtgraph.Qt import QtCore, QtWidgets

import usb.core
import usb.util
from usb.backend import libusb1

try:
    import libusb_package
except ImportError:
    libusb_package = None

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

# ---------------------------------------------------------
# STM32 USB STREAM CONSTANTS
# ---------------------------------------------------------
VID = 0x1209
PID = 0x4158
EP_IN = 0x81
REQ_START_STREAM = 0x30

FRAME_LEN = 20480
HEADER_LEN = 64
PAYLOAD_BYTES = 20000
PAYLOAD_WORDS = 5000
MAGIC = 0x52444152
FRAMES_PER_READ = 8

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
        raise RuntimeError("No PyUSB/libusb backend found.")

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
                    print("[PRODUCER] Header sample count does not match constants.")
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


def process_2d_fft(matrix, range_window, doppler_window):
    matrix_windowed_1d = matrix * range_window
    range_fft = np.fft.fft(matrix_windowed_1d, axis=1)
    range_fft = range_fft[:, :SAMPLES_PER_CHIRP // 2]
    matrix_windowed_2d = range_fft * doppler_window
    doppler_fft = np.fft.fft(matrix_windowed_2d, axis=0)
    range_doppler_map = np.fft.fftshift(doppler_fft, axes=0)
    return np.abs(range_doppler_map)


def make_turbo_colormap():
    n = 256
    r = np.zeros(n, dtype=np.ubyte)
    g = np.zeros(n, dtype=np.ubyte)
    b = np.zeros(n, dtype=np.ubyte)
    for i in range(n):
        t = i / (n - 1)
        if t < 0.25:
            r[i] = 0
            g[i] = int(255 * (t / 0.25))
            b[i] = 255
        elif t < 0.5:
            r[i] = 0
            g[i] = 255
            b[i] = int(255 * (1.0 - (t - 0.25) / 0.25))
        elif t < 0.75:
            r[i] = int(255 * ((t - 0.5) / 0.25))
            g[i] = 255
            b[i] = 0
        else:
            r[i] = 255
            g[i] = int(255 * (1.0 - (t - 0.75) / 0.25))
            b[i] = 0
    colors = np.column_stack([r, g, b, np.full(n, 255, dtype=np.ubyte)])
    return colors


class RadarWindow(QtWidgets.QMainWindow):
    def __init__(self, data_queue):
        super().__init__()
        self.data_queue = data_queue
        self.setWindowTitle("FMCW Triangular - Live Range-Doppler")
        self.resize(900, 700)

        pg.setConfigOptions(antialias=False)

        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        layout = QtWidgets.QVBoxLayout(central)

        self.graphics = pg.GraphicsLayoutWidget()
        layout.addWidget(self.graphics)

        self.rd_plot = self.graphics.addPlot(row=0, col=0, title="Live Range-Doppler Heatmap (UP Ramp)")
        self.rd_plot.setLabel("bottom", "Range", "m")
        self.rd_plot.setLabel("left", "Velocity", "m/s")
        self.rd_plot.setXRange(0, 100)

        self.rd_img = pg.ImageItem()
        self.rd_img.setLookupTable(make_turbo_colormap())
        self.rd_plot.addItem(self.rd_img)

        self.status_label = pg.LabelItem(justify="left")
        self.status_label.setText("Waiting for data...", color="#ffffff")
        self.graphics.addItem(self.status_label, row=1, col=0)

        self.range_window = np.hanning(SAMPLES_PER_CHIRP)
        self.doppler_window = np.hanning(CHIRPS_PER_FRAME).reshape(-1, 1)

        self.rd_plot.setMouseEnabled(x=True, y=True)

        self.up_buffer = []
        self.down_buffer = []

        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.update_plot)
        self.timer.start(16)

    def update_plot(self):
        frames_collected = 0
        while frames_collected < 128:
            try:
                raw_bytes = self.data_queue.get_nowait()
            except Exception:
                break

            signal = np.frombuffer(raw_bytes, dtype=np.uint16).astype(np.float32)
            up_sig = signal[:SAMPLES_PER_CHIRP] - np.mean(signal[:SAMPLES_PER_CHIRP])
            down_sig = signal[SAMPLES_PER_CHIRP:] - np.mean(signal[SAMPLES_PER_CHIRP:])

            self.up_buffer.append(up_sig)
            self.down_buffer.append(down_sig)
            frames_collected += 1

        if len(self.up_buffer) < CHIRPS_PER_FRAME:
            return

        up_matrix = np.vstack(self.up_buffer[:CHIRPS_PER_FRAME])
        down_matrix = np.vstack(self.down_buffer[:CHIRPS_PER_FRAME])

        self.up_buffer = self.up_buffer[CHIRPS_PER_FRAME:]
        self.down_buffer = self.down_buffer[CHIRPS_PER_FRAME:]

        rd_mag_up = process_2d_fft(up_matrix, self.range_window, self.doppler_window)
        rd_mag_down = process_2d_fft(down_matrix, self.range_window, self.doppler_window)

        rd_db = 20 * np.log10(rd_mag_up + 1e-10)
        rd_db_display = rd_db - np.max(rd_db)
        rd_db_display = zoom(rd_db_display, zoom=2, order=3)

        range_step = (SAMPLING_FREQ / SAMPLES_PER_CHIRP) * (C * TC / (2 * BW))
        max_range = (SAMPLES_PER_CHIRP // 2) * range_step
        vel_step = ((1 / CHIRP_PERIOD) / CHIRPS_PER_FRAME) * (LAMBDA / 2)
        max_vel = (CHIRPS_PER_FRAME // 2) * vel_step

        self.rd_img.setImage(
            rd_db_display.T,
            autoLevels=False,
            levels=(-60, 0),
            interpolation='linear',
        )
        self.rd_img.setRect(QtCore.QRectF(0, -max_vel, max_range, 2 * max_vel))

        target_idx_up = np.unravel_index(np.argmax(rd_mag_up), rd_mag_up.shape)
        doppler_bin = target_idx_up[0]
        range_bin_up = target_idx_up[1]

        target_idx_down = np.unravel_index(np.argmax(rd_mag_down), rd_mag_down.shape)
        range_bin_down = target_idx_down[1]

        freq_res = SAMPLING_FREQ / SAMPLES_PER_CHIRP
        f_up = range_bin_up * freq_res
        f_down = range_bin_down * freq_res

        f_true = (f_up + f_down) / 2
        target_range = f_true * (C * TC / (2 * BW))

        doppler_shifted_idx = doppler_bin - (CHIRPS_PER_FRAME // 2)
        prf = 1 / CHIRP_PERIOD
        doppler_freq = doppler_shifted_idx * (prf / CHIRPS_PER_FRAME)
        target_velocity = doppler_freq * (LAMBDA / 2)

        self.status_label.setText(
            f"<span style='color:#ffffff'>"
            f"<b>Target:</b> Range={target_range:.2f} m | Velocity={target_velocity:+.2f} m/s | "
            f"f_up={f_up/1000:.1f} kHz | f_down={f_down/1000:.1f} kHz"
            f"</span>"
        )


def main():
    app = QtWidgets.QApplication(sys.argv)
    data_queue = Queue(maxsize=128)

    producer_process = Process(target=data_producer, args=(data_queue,))
    producer_process.start()

    try:
        window = RadarWindow(data_queue)
        window.show()
        exit_code = app.exec()
    except KeyboardInterrupt:
        pass
    finally:
        producer_process.terminate()
        producer_process.join(timeout=1.0)

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
