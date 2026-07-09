#!/usr/bin/env python3
"""Continuous FMCW live viewer using a USB reader thread and pyqtgraph.

This avoids the failure mode of Matplotlib live plotting: USB reading never
waits for the GUI. If the display is slow, only display frames are skipped.
"""

import argparse
import struct
import sys
import threading
import time

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
    from pyqtgraph.Qt import QtCore, QtWidgets
except ImportError:
    pg = None
    QtCore = None
    QtWidgets = None


VID = 0x1209
PID = 0x4158
EP_IN = 0x81
REQ_START_STREAM = 0x30

FRAME_LEN = 20480
HEADER_LEN = 64
PAYLOAD_WORDS = 5000
SAMPLES_PER_SLOPE = 10000
MAGIC = 0x52444152
HEADER = struct.Struct("<IHHIIIHHIIHHIIIIIII")

LIGHT_SPEED_M_S = 299_792_458.0
DEFAULT_BW_HZ = 200_271_606.0
DEFAULT_CHIRP_S = 1.0e-3


def get_backend():
    if libusb_package is None:
        return libusb1.get_backend()
    return libusb1.get_backend(find_library=libusb_package.find_library)


def open_device(backend, timeout_s=30.0):
    deadline = time.monotonic() + timeout_s
    access_hint_printed = False

    while time.monotonic() < deadline:
        dev = usb.core.find(idVendor=VID, idProduct=PID, backend=backend)
        if dev is None:
            print("Waiting for FMCW USB HS stream device...")
            time.sleep(0.5)
            continue

        try:
            dev.set_configuration()
            return dev
        except usb.core.USBError as exc:
            if getattr(exc, "errno", None) == 13 and not access_hint_printed:
                print("FMCW device is visible, but Windows denied access.")
                print("Close other stream/viewer scripts, unplug/replug USB, or rebind WinUSB with Zadig.")
                access_hint_printed = True
            else:
                print(f"FMCW device open/config failed: {exc}. Retrying...")
            usb.util.dispose_resources(dev)
            time.sleep(0.5)

    raise SystemExit("FMCW USB HS stream device not ready")


def read_exact(dev, size, timeout_ms=2000):
    chunks = []
    remaining = size
    while remaining:
        chunk = bytes(dev.read(EP_IN, remaining, timeout=timeout_ms))
        if not chunk:
            raise TimeoutError("USB read returned no data")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def parse_header(buf, offset=0):
    values = HEADER.unpack_from(buf, offset)
    return {
        "magic": values[0],
        "version": values[1],
        "header_len": values[2],
        "frame_len": values[3],
        "slope_seq": values[4],
        "triangle_seq": values[5],
        "slope_id": values[6],
        "flags": values[7],
        "sample_rate_hz": values[8],
        "samples_per_slope": values[9],
        "adc_bits": values[10],
        "words_per_slope": values[11],
        "timestamp_us": values[12],
        "muxout_count": values[13],
        "dropped_frames": values[14],
        "dcmi_risr": values[15],
        "dma_lisr": values[16],
    }


def sign_extend_12(raw):
    raw = raw.astype(np.int16, copy=False)
    return ((raw ^ 0x0800) - 0x0800).astype(np.int16, copy=False)


def decode_samples(frame):
    words = np.frombuffer(frame, dtype="<u4", count=PAYLOAD_WORDS, offset=HEADER_LEN)
    lo = sign_extend_12((words & 0x0FFF).astype(np.int16))
    hi = sign_extend_12(((words >> 16) & 0x0FFF).astype(np.int16))
    samples = np.empty(SAMPLES_PER_SLOPE, dtype=np.int16)
    samples[0::2] = lo
    samples[1::2] = hi
    return samples


def make_range_axis(sample_rate_hz, nfft, bandwidth_hz, chirp_s):
    slope_hz_s = bandwidth_hz / chirp_s
    freqs = np.fft.rfftfreq(nfft, d=1.0 / sample_rate_hz)
    return LIGHT_SPEED_M_S * freqs / (2.0 * slope_hz_s)


class SharedState:
    def __init__(self):
        self.lock = threading.Lock()
        self.latest_frame = None
        self.latest_header = None
        self.frames = 0
        self.gaps = 0
        self.bad = 0
        self.last_seq = None
        self.running = True
        self.error = None
        self.start_time = time.monotonic()


def reader_thread(state, args):
    backend = get_backend()
    if backend is None:
        state.error = "Install: python -m pip install pyusb libusb-package pyqtgraph PyQt6"
        state.running = False
        return

    dev = None
    try:
        dev = open_device(backend)
        if not args.no_start_command:
            dev.ctrl_transfer(0x40, REQ_START_STREAM, 0, 0, None)

        batch_len = FRAME_LEN * args.frames_per_read
        while state.running:
            batch = read_exact(dev, batch_len)
            chosen_frame = None
            chosen_header = None

            with state.lock:
                for offset in range(0, len(batch), FRAME_LEN):
                    header = parse_header(batch, offset)
                    valid = (
                        header["magic"] == MAGIC
                        and header["header_len"] == HEADER_LEN
                        and header["frame_len"] == FRAME_LEN
                    )
                    if not valid:
                        state.bad += 1
                    else:
                        if state.last_seq is not None and header["slope_seq"] != ((state.last_seq + 1) & 0xFFFFFFFF):
                            state.gaps += 1
                        state.last_seq = header["slope_seq"]

                        slope_ok = args.slope_id == "all" or int(args.slope_id) == header["slope_id"]
                        if slope_ok and (header["slope_seq"] % args.display_decimation) == 0:
                            chosen_frame = bytes(batch[offset:offset + FRAME_LEN])
                            chosen_header = header
                    state.frames += 1

                if chosen_frame is not None:
                    state.latest_frame = chosen_frame
                    state.latest_header = chosen_header
    except Exception as exc:
        state.error = repr(exc)
        state.running = False
    finally:
        if dev is not None:
            usb.util.dispose_resources(dev)


def build_argparser():
    parser = argparse.ArgumentParser(description="Continuous live FMCW viewer.")
    parser.add_argument("--frames-per-read", type=int, default=16)
    parser.add_argument("--display-decimation", type=int, default=10,
                        help="Only make every Nth matching slope available to the GUI.")
    parser.add_argument("--slope-id", choices=["0", "1", "all"], default="0")
    parser.add_argument("--fps", type=float, default=20.0)
    parser.add_argument("--time-samples", type=int, default=1200)
    parser.add_argument("--nfft", type=int, default=16384)
    parser.add_argument("--max-range-m", type=float, default=200.0)
    parser.add_argument("--bandwidth-hz", type=float, default=DEFAULT_BW_HZ)
    parser.add_argument("--chirp-s", type=float, default=DEFAULT_CHIRP_S)
    parser.add_argument("--no-start-command", action="store_true")
    return parser


def main():
    args = build_argparser().parse_args()
    if pg is None:
        raise SystemExit("Install live-view dependencies: python -m pip install pyqtgraph PyQt6")

    state = SharedState()
    worker = threading.Thread(target=reader_thread, args=(state, args), daemon=True)
    worker.start()

    app = QtWidgets.QApplication([])
    pg.setConfigOptions(antialias=False)

    win = pg.GraphicsLayoutWidget(show=True, title="FMCW Live View")
    win.resize(1100, 750)

    time_plot = win.addPlot(row=0, col=0, title="ADC Samples")
    time_plot.setLabel("bottom", "sample")
    time_plot.setLabel("left", "mV")
    time_plot.setYRange(-2100, 2100)
    time_curve = time_plot.plot(np.zeros(args.time_samples), pen="y")

    range_plot = win.addPlot(row=1, col=0, title="Range FFT")
    range_plot.setLabel("bottom", "range", "m")
    range_plot.setLabel("left", "magnitude", "dB")
    range_plot.setXRange(0, args.max_range_m)
    range_curve = range_plot.plot(pen="c")

    status = pg.LabelItem(justify="left")
    win.addItem(status, row=2, col=0)

    sample_rate_hz = 10_000_000
    range_m = make_range_axis(sample_rate_hz, args.nfft, args.bandwidth_hz, args.chirp_s)
    range_mask = range_m <= args.max_range_m
    range_view = range_m[range_mask]
    window = np.hanning(SAMPLES_PER_SLOPE).astype(np.float32)

    latest_seen_seq = None
    last_fps_time = time.monotonic()
    last_frames = 0
    rx_fps = 0.0

    def update():
        nonlocal latest_seen_seq, last_fps_time, last_frames, rx_fps

        with state.lock:
            frame = state.latest_frame
            header = dict(state.latest_header) if state.latest_header is not None else None
            frames = state.frames
            gaps = state.gaps
            bad = state.bad
            error = state.error

        now = time.monotonic()
        elapsed = now - last_fps_time
        if elapsed >= 1.0:
            rx_fps = (frames - last_frames) / elapsed
            last_frames = frames
            last_fps_time = now

        if error is not None:
            status.setText(f"Reader stopped: {error}", color="r")
            return

        if frame is not None and header is not None and header["slope_seq"] != latest_seen_seq:
            latest_seen_seq = header["slope_seq"]
            samples = decode_samples(frame)
            shown = samples[: args.time_samples].astype(np.float32)
            shown -= np.mean(shown)
            time_curve.setData(shown)

            x = samples.astype(np.float32)
            x -= np.mean(x)
            mag = np.abs(np.fft.rfft(x * window, n=args.nfft))
            db = 20.0 * np.log10(mag + 1.0e-6)
            db_view = db[range_mask]
            range_curve.setData(range_view, db_view)

            peak_idx = int(np.argmax(db_view))
            peak_range = range_view[peak_idx]
            peak_db = db_view[peak_idx]
        else:
            peak_range = 0.0
            peak_db = 0.0

        status.setText(
            f"rx={rx_fps:7.1f} frames/s  total={frames}  gaps={gaps}  bad={bad}  "
            f"seq={latest_seen_seq}  slope={header['slope_id'] if header else '-'}  "
            f"fw_drop={header['dropped_frames'] if header else '-'}  "
            f"flags=0x{header['flags']:04x}  " if header else
            f"rx={rx_fps:7.1f} frames/s  total={frames}  gaps={gaps}  bad={bad}  waiting...",
            color="w",
        )
        if header is not None:
            status.setText(
                f"rx={rx_fps:7.1f} frames/s  total={frames}  gaps={gaps}  bad={bad}  "
                f"seq={latest_seen_seq}  slope={header['slope_id']}  "
                f"fw_drop={header['dropped_frames']}  flags=0x{header['flags']:04x}  "
                f"peak={peak_range:.2f} m / {peak_db:.1f} dB",
                color="w",
            )

    timer = QtCore.QTimer()
    timer.timeout.connect(update)
    timer.start(max(1, int(1000.0 / args.fps)))

    try:
        exit_code = app.exec()
    finally:
        state.running = False
        worker.join(timeout=1.0)
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
