#!/usr/bin/env python3
"""Live breathing viewer for the FMCW USB stream.

Pipeline:
  USB frames -> one slope direction -> range FFT -> target-bin phase
  -> phase unwrap -> displacement -> breathing-band FFT/visualization.

This is intentionally a live bring-up tool. It drains USB in a background
thread and only processes a decimated slow-time stream, so the GUI should stay
responsive while the firmware continues to stream at full rate.
"""

import argparse
import collections
import math
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
DEFAULT_CENTER_HZ = 5_800_135_803.0


def get_backend():
    if libusb_package is None:
        return libusb1.get_backend()
    return libusb1.get_backend(find_library=libusb_package.find_library)


def list_usb_devices(backend):
    print("Visible USB devices:")
    found = False
    for dev in usb.core.find(find_all=True, backend=backend):
        found = True
        marker = "  <-- FMCW target" if (dev.idVendor, dev.idProduct) == (VID, PID) else ""
        print(f"  {dev.idVendor:04x}:{dev.idProduct:04x}{marker}")
    if not found:
        print("  No USB devices visible through PyUSB/libusb.")


def open_device(backend, timeout_s):
    deadline = time.monotonic() + timeout_s
    last_report = 0.0
    access_hint_printed = False

    while time.monotonic() < deadline:
        dev = usb.core.find(idVendor=VID, idProduct=PID, backend=backend)
        if dev is None:
            now = time.monotonic()
            if now - last_report >= 2.0:
                print("Waiting for FMCW USB HS stream device...")
                list_usb_devices(backend)
                last_report = now
            time.sleep(0.25)
            continue

        try:
            dev.set_configuration()
            return dev
        except NotImplementedError as exc:
            if not access_hint_printed:
                print(f"FMCW device is visible but libusb cannot open it: {exc}")
                print("Bind the FMCW interface to WinUSB with Zadig, then unplug/replug.")
                access_hint_printed = True
            time.sleep(1.0)
        except usb.core.USBError as exc:
            if getattr(exc, "errno", None) == 13 and not access_hint_printed:
                print("FMCW device is visible, but Windows denied access.")
                print("Close other stream/viewer scripts or rebind the interface to WinUSB.")
                access_hint_printed = True
            else:
                print(f"FMCW device appeared but open/config failed: {exc}. Retrying...")
            usb.util.dispose_resources(dev)
            time.sleep(0.5)

    list_usb_devices(backend)
    raise RuntimeError("FMCW USB HS stream device not ready")


def read_exact(dev, size, timeout_ms):
    chunks = []
    remaining = size
    while remaining:
        try:
            chunk = bytes(dev.read(EP_IN, remaining, timeout=timeout_ms))
        except usb.core.USBTimeoutError as exc:
            have = size - remaining
            raise TimeoutError(f"timed out waiting for USB data, have {have}/{size} bytes") from exc
        if not chunk:
            raise TimeoutError("USB read returned no data")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def parse_header(buf, offset):
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


def valid_header(header):
    return (
        header["magic"] == MAGIC
        and header["version"] == 1
        and header["header_len"] == HEADER_LEN
        and header["frame_len"] == FRAME_LEN
        and header["samples_per_slope"] == SAMPLES_PER_SLOPE
        and header["words_per_slope"] == PAYLOAD_WORDS
    )


def sign_extend_12(raw):
    raw = (raw & 0x0FFF).astype(np.int16, copy=False)
    return ((raw ^ 0x0800) - 0x0800).astype(np.int16, copy=False)


def decode_samples(buf, frame_offset):
    words = np.frombuffer(buf, dtype="<u4", count=PAYLOAD_WORDS, offset=frame_offset + HEADER_LEN)
    lo = sign_extend_12(words.astype(np.int32) & 0x0FFF)
    hi = sign_extend_12((words.astype(np.int32) >> 16) & 0x0FFF)
    samples = np.empty(SAMPLES_PER_SLOPE, dtype=np.float32)
    samples[0::2] = lo
    samples[1::2] = hi
    return samples


def make_range_axis(sample_rate_hz, nfft, bandwidth_hz, chirp_s):
    slope_hz_s = bandwidth_hz / chirp_s
    freqs = np.fft.rfftfreq(nfft, d=1.0 / sample_rate_hz)
    return LIGHT_SPEED_M_S * freqs / (2.0 * slope_hz_s)


def wrap_phase_delta(delta):
    return (delta + math.pi) % (2.0 * math.pi) - math.pi


def detrend_linear(t, y):
    if len(y) < 3:
        return y - np.mean(y)
    centered_t = t - t[0]
    slope, intercept = np.polyfit(centered_t, y, 1)
    return y - (slope * centered_t + intercept)


def refine_peak_frequency(freqs, mag, peak_index):
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


def analyze_breathing(times_s, displacement_m, args):
    if len(times_s) < 8:
        return None

    t = np.asarray(times_s, dtype=np.float64)
    y_mm = np.asarray(displacement_m, dtype=np.float64) * 1000.0
    keep = t >= (t[-1] - args.breath_window_s)
    t = t[keep]
    y_mm = y_mm[keep]

    if len(t) < 8 or (t[-1] - t[0]) < args.min_analysis_s:
        return None

    dt = np.diff(t)
    dt = dt[dt > 0.0]
    if len(dt) == 0:
        return None
    slow_fs = 1.0 / np.median(dt)
    if slow_fs < 2.0 * args.breath_max_hz:
        return None

    uniform_t = np.arange(t[0], t[-1], 1.0 / slow_fs)
    if len(uniform_t) < 8:
        return None

    y_uniform = np.interp(uniform_t, t, y_mm)
    y_detrended = detrend_linear(uniform_t, y_uniform)
    fft_window = np.hanning(len(y_detrended))
    fft_len = int(2 ** math.ceil(math.log2(max(len(y_detrended), 8))))
    fft_len *= max(1, args.slow_fft_zeropad)
    spectrum = np.fft.rfft(y_detrended * fft_window, n=fft_len)
    freqs = np.fft.rfftfreq(fft_len, d=1.0 / slow_fs)
    mag = np.abs(spectrum)

    band = (freqs >= args.breath_min_hz) & (freqs <= args.breath_max_hz)
    if not np.any(band):
        return None

    band_indices = np.flatnonzero(band)
    peak_index = int(band_indices[np.argmax(mag[band])])
    breath_hz = refine_peak_frequency(freqs, mag, peak_index)
    breath_bpm = breath_hz * 60.0

    noise_floor = float(np.median(mag[band]) + 1.0e-12)
    confidence = float(mag[peak_index] / noise_floor)

    filtered_spectrum = np.fft.rfft(y_detrended)
    filtered_freqs = np.fft.rfftfreq(len(y_detrended), d=1.0 / slow_fs)
    filtered_band = (filtered_freqs >= args.breath_min_hz) & (filtered_freqs <= args.breath_max_hz)
    filtered_spectrum[~filtered_band] = 0.0
    breath_wave_mm = np.fft.irfft(filtered_spectrum, n=len(y_detrended))

    spectrum_bpm = freqs[band] * 60.0
    spectrum_db = 20.0 * np.log10(mag[band] + 1.0e-12)
    if len(spectrum_db):
        spectrum_db = spectrum_db - np.max(spectrum_db)

    return {
        "t": uniform_t - uniform_t[-1],
        "raw_mm": y_uniform - np.mean(y_uniform),
        "detrended_mm": y_detrended,
        "breath_mm": breath_wave_mm,
        "spectrum_bpm": spectrum_bpm,
        "spectrum_db": spectrum_db,
        "breath_bpm": breath_bpm,
        "confidence": confidence,
        "slow_fs": slow_fs,
    }


class SharedState:
    def __init__(self):
        self.lock = threading.Lock()
        self.running = True
        self.error = None
        self.start_time = time.monotonic()

        self.frames = 0
        self.processed = 0
        self.bad = 0
        self.gaps = 0
        self.timeouts = 0
        self.last_seq = None
        self.latest_header = None

        self.range_m = np.array([], dtype=np.float64)
        self.range_db = np.array([], dtype=np.float64)
        self.target_range_m = None
        self.target_bin = None
        self.target_db = None

        self.motion_t = np.array([], dtype=np.float64)
        self.motion_raw_mm = np.array([], dtype=np.float64)
        self.motion_breath_mm = np.array([], dtype=np.float64)
        self.spectrum_bpm = np.array([], dtype=np.float64)
        self.spectrum_db = np.array([], dtype=np.float64)
        self.breath_bpm = None
        self.confidence = 0.0
        self.slow_fs = 0.0
        self.rx_mbps = 0.0

    def snapshot(self):
        with self.lock:
            return {
                "running": self.running,
                "error": self.error,
                "frames": self.frames,
                "processed": self.processed,
                "bad": self.bad,
                "gaps": self.gaps,
                "timeouts": self.timeouts,
                "latest_header": dict(self.latest_header) if self.latest_header else None,
                "range_m": self.range_m,
                "range_db": self.range_db,
                "target_range_m": self.target_range_m,
                "target_bin": self.target_bin,
                "target_db": self.target_db,
                "motion_t": self.motion_t,
                "motion_raw_mm": self.motion_raw_mm,
                "motion_breath_mm": self.motion_breath_mm,
                "spectrum_bpm": self.spectrum_bpm,
                "spectrum_db": self.spectrum_db,
                "breath_bpm": self.breath_bpm,
                "confidence": self.confidence,
                "slow_fs": self.slow_fs,
                "rx_mbps": self.rx_mbps,
            }

    def set_error(self, error):
        with self.lock:
            self.error = error
            self.running = False


def choose_target_bin(args, range_m, range_db, range_power_avg, valid_bins):
    if args.target_bin is not None:
        target_bin = int(np.clip(args.target_bin, valid_bins[0], valid_bins[-1]))
        return target_bin

    if args.target_range_m is not None:
        return int(np.argmin(np.abs(range_m - args.target_range_m)))

    if len(valid_bins) == 0:
        return None

    search_power = range_power_avg[valid_bins] if range_power_avg is not None else range_db[valid_bins]
    return int(valid_bins[int(np.argmax(search_power))])


def should_accept_new_target(target_bin, new_bin, range_power_avg, retarget_margin_db):
    if target_bin is None or new_bin is None:
        return new_bin is not None
    if new_bin == target_bin:
        return False
    if range_power_avg is None:
        return True

    current = float(range_power_avg[target_bin]) + 1.0e-12
    candidate = float(range_power_avg[new_bin]) + 1.0e-12
    margin = 10.0 ** (retarget_margin_db / 20.0)
    return candidate > current * margin


def usb_dsp_thread(state, args):
    backend = get_backend()
    if backend is None:
        state.set_error("Install: python -m pip install pyusb libusb-package numpy pyqtgraph PyQt6")
        return

    dev = None
    sample_rate_hz = 10_000_000
    range_m = make_range_axis(sample_rate_hz, args.nfft, args.bandwidth_hz, args.chirp_s)
    valid_bins = np.flatnonzero((range_m >= args.min_range_m) & (range_m <= args.max_range_m))
    display_bins = np.flatnonzero(range_m <= args.max_range_m)
    window = np.hanning(SAMPLES_PER_SLOPE).astype(np.float32)
    wavelength_m = LIGHT_SPEED_M_S / args.center_hz

    target_bin = None
    last_retarget_time = 0.0
    range_power_avg = None
    selected_seen = 0
    estimated_slow_fs = 1.0 / max(args.chirp_s * 2.0 * args.process_decimation, 1.0e-9)
    history_len = max(32, int(args.history_s * max(200.0, estimated_slow_fs * 1.25)))
    phase_times = collections.deque(maxlen=history_len)
    displacement_m = collections.deque(maxlen=history_len)
    last_phase = None
    unwrapped_phase = 0.0

    local_frames = 0
    local_processed = 0
    local_bad = 0
    local_gaps = 0
    local_timeouts = 0
    local_last_seq = None
    last_rate_time = time.monotonic()
    last_rate_frames = 0
    rx_mbps = 0.0

    try:
        dev = open_device(backend, args.connect_timeout_s)
        if not args.no_start_command:
            dev.ctrl_transfer(0x40, REQ_START_STREAM, 0, 0, None)

        batch_len = FRAME_LEN * args.frames_per_read
        while state.running:
            try:
                batch = read_exact(dev, batch_len, args.usb_timeout_ms)
            except TimeoutError:
                local_timeouts += 1
                with state.lock:
                    state.timeouts = local_timeouts
                continue

            latest_header = None
            latest_range_db = None
            latest_target_db = None
            latest_analysis = None
            target_range_m = range_m[target_bin] if target_bin is not None else None

            for frame_offset in range(0, len(batch), FRAME_LEN):
                header = parse_header(batch, frame_offset)
                latest_header = header
                local_frames += 1

                if not valid_header(header):
                    local_bad += 1
                    continue

                if local_last_seq is not None and header["slope_seq"] != ((local_last_seq + 1) & 0xFFFFFFFF):
                    local_gaps += 1
                local_last_seq = header["slope_seq"]

                if int(header["slope_id"]) != args.slope_id:
                    continue

                selected_seen += 1
                if selected_seen % args.process_decimation != 0:
                    continue

                samples = decode_samples(batch, frame_offset)
                samples -= np.mean(samples)
                spectrum = np.fft.rfft(samples * window, n=args.nfft)
                mag = np.abs(spectrum)
                range_db = 20.0 * np.log10(mag + 1.0e-9)
                latest_range_db = range_db[display_bins]

                if range_power_avg is None:
                    range_power_avg = mag.copy()
                else:
                    range_power_avg = (
                        (1.0 - args.range_avg_alpha) * range_power_avg
                        + args.range_avg_alpha * mag
                    )

                now = time.monotonic()
                should_retarget = (
                    target_bin is None
                    or (
                        args.target_bin is None
                        and args.target_range_m is None
                        and now - last_retarget_time >= args.retarget_s
                    )
                )
                if should_retarget:
                    new_bin = choose_target_bin(args, range_m, range_db, range_power_avg, valid_bins)
                    if should_accept_new_target(
                        target_bin,
                        new_bin,
                        range_power_avg,
                        args.retarget_margin_db,
                    ):
                        target_bin = new_bin
                        last_phase = None
                        unwrapped_phase = 0.0
                        phase_times.clear()
                        displacement_m.clear()
                    last_retarget_time = now

                if target_bin is None:
                    continue

                half = max(0, args.phase_bin_half_width)
                start_bin = max(0, target_bin - half)
                stop_bin = min(len(spectrum), target_bin + half + 1)
                target_complex = np.sum(spectrum[start_bin:stop_bin])
                target_phase = math.atan2(float(target_complex.imag), float(target_complex.real))
                if last_phase is None:
                    last_phase = target_phase
                    unwrapped_phase = 0.0
                else:
                    unwrapped_phase += wrap_phase_delta(target_phase - last_phase)
                    last_phase = target_phase

                target_range_m = float(range_m[target_bin])
                latest_target_db = float(range_db[target_bin])
                motion_m = unwrapped_phase * wavelength_m / (4.0 * math.pi)

                frame_time_s = float(header["timestamp_us"]) * 1.0e-6
                if phase_times and frame_time_s <= phase_times[-1]:
                    frame_time_s = phase_times[-1] + args.chirp_s * 2.0 * args.process_decimation
                phase_times.append(frame_time_s)
                displacement_m.append(motion_m)
                local_processed += 1

                latest_analysis = analyze_breathing(
                    list(phase_times),
                    list(displacement_m),
                    args,
                )

            now = time.monotonic()
            elapsed = now - last_rate_time
            if elapsed >= 1.0:
                delta_frames = local_frames - last_rate_frames
                rx_mbps = (delta_frames * FRAME_LEN * 8.0) / elapsed / 1_000_000.0
                last_rate_frames = local_frames
                last_rate_time = now

            with state.lock:
                state.frames = local_frames
                state.processed = local_processed
                state.bad = local_bad
                state.gaps = local_gaps
                state.timeouts = local_timeouts
                state.last_seq = local_last_seq
                state.latest_header = latest_header
                state.rx_mbps = rx_mbps
                if latest_range_db is not None:
                    state.range_m = range_m[display_bins]
                    state.range_db = latest_range_db
                    state.target_bin = target_bin
                    state.target_range_m = target_range_m
                    state.target_db = latest_target_db
                if latest_analysis is not None:
                    t = latest_analysis["t"]
                    show = t >= -args.display_window_s
                    state.motion_t = t[show]
                    state.motion_raw_mm = latest_analysis["detrended_mm"][show]
                    state.motion_breath_mm = latest_analysis["breath_mm"][show]
                    state.spectrum_bpm = latest_analysis["spectrum_bpm"]
                    state.spectrum_db = latest_analysis["spectrum_db"]
                    state.breath_bpm = latest_analysis["breath_bpm"]
                    state.confidence = latest_analysis["confidence"]
                    state.slow_fs = latest_analysis["slow_fs"]
    except Exception as exc:
        state.set_error(repr(exc))
    finally:
        if dev is not None:
            usb.util.dispose_resources(dev)


def build_argparser():
    parser = argparse.ArgumentParser(description="Live FMCW breathing viewer.")
    parser.add_argument("--frames-per-read", type=int, default=16)
    parser.add_argument("--connect-timeout-s", type=float, default=30.0)
    parser.add_argument("--usb-timeout-ms", type=int, default=2000)
    parser.add_argument("--no-start-command", action="store_true")

    parser.add_argument("--slope-id", type=int, choices=[0, 1], default=0,
                        help="Process only one triangle slope direction.")
    parser.add_argument("--process-decimation", type=int, default=10,
                        help="Process every Nth matching slope. 10 gives about 50 Hz from one slope.")

    parser.add_argument("--nfft", type=int, default=32768)
    parser.add_argument("--bandwidth-hz", type=float, default=DEFAULT_BW_HZ)
    parser.add_argument("--chirp-s", type=float, default=DEFAULT_CHIRP_S)
    parser.add_argument("--center-hz", type=float, default=DEFAULT_CENTER_HZ)
    parser.add_argument("--min-range-m", type=float, default=0.3)
    parser.add_argument("--max-range-m", type=float, default=5.0)
    parser.add_argument("--target-range-m", type=float, default=None,
                        help="Lock to the nearest FFT bin to this range.")
    parser.add_argument("--target-bin", type=int, default=None,
                        help="Lock to a specific FFT bin. Overrides automatic target selection.")
    parser.add_argument("--phase-bin-half-width", type=int, default=0,
                        help="Sum +/- this many bins around the tracked bin before phase extraction.")
    parser.add_argument("--retarget-s", type=float, default=3.0,
                        help="Auto-select strongest target again every N seconds.")
    parser.add_argument("--retarget-margin-db", type=float, default=6.0,
                        help="Auto-target only jumps if the new bin is this much stronger.")
    parser.add_argument("--range-avg-alpha", type=float, default=0.08)

    parser.add_argument("--history-s", type=float, default=60.0)
    parser.add_argument("--display-window-s", type=float, default=30.0)
    parser.add_argument("--breath-window-s", type=float, default=30.0)
    parser.add_argument("--min-analysis-s", type=float, default=8.0)
    parser.add_argument("--breath-min-hz", type=float, default=0.10)
    parser.add_argument("--breath-max-hz", type=float, default=0.70)
    parser.add_argument("--slow-fft-zeropad", type=int, default=4,
                        help="Zero-padding factor for the breathing-rate FFT display.")
    parser.add_argument("--fps", type=float, default=20.0)
    return parser


def main():
    args = build_argparser().parse_args()
    if pg is None:
        raise SystemExit("Install live-view dependencies: python -m pip install pyqtgraph PyQt6")
    if args.min_range_m >= args.max_range_m:
        raise SystemExit("--min-range-m must be lower than --max-range-m")
    if args.process_decimation < 1:
        raise SystemExit("--process-decimation must be >= 1")

    state = SharedState()
    worker = threading.Thread(target=usb_dsp_thread, args=(state, args), daemon=True)
    worker.start()

    app = QtWidgets.QApplication([])
    pg.setConfigOptions(antialias=False)

    win = pg.GraphicsLayoutWidget(show=True, title="FMCW Breathing Viewer")
    win.resize(1250, 850)

    status = pg.LabelItem(justify="left")
    win.addItem(status, row=0, col=0, colspan=2)

    range_plot = win.addPlot(row=1, col=0, title="Range FFT")
    range_plot.setLabel("bottom", "range", "m")
    range_plot.setLabel("left", "magnitude", "dB")
    range_plot.setXRange(args.min_range_m, args.max_range_m)
    range_curve = range_plot.plot(pen=pg.mkPen("c", width=1))
    target_line = pg.InfiniteLine(angle=90, movable=False, pen=pg.mkPen("m", width=2))
    range_plot.addItem(target_line)

    motion_plot = win.addPlot(row=1, col=1, title="Chest Motion")
    motion_plot.setLabel("bottom", "time", "s")
    motion_plot.setLabel("left", "displacement", "mm")
    motion_plot.setXRange(-args.display_window_s, 0)
    raw_curve = motion_plot.plot(pen=pg.mkPen(120, 120, 120, width=1), name="detrended")
    breath_curve = motion_plot.plot(pen=pg.mkPen("g", width=2), name="breathing")

    spectrum_plot = win.addPlot(row=2, col=0, title="Breathing Spectrum")
    spectrum_plot.setLabel("bottom", "rate", "breaths/min")
    spectrum_plot.setLabel("left", "relative magnitude", "dB")
    spectrum_plot.setXRange(args.breath_min_hz * 60.0, args.breath_max_hz * 60.0)
    spectrum_plot.setYRange(-50, 3)
    spectrum_curve = spectrum_plot.plot(pen=pg.mkPen("y", width=2))
    bpm_line = pg.InfiniteLine(angle=90, movable=False, pen=pg.mkPen("m", width=2))
    spectrum_plot.addItem(bpm_line)

    notes = pg.LabelItem(justify="left")
    notes.setText(
        "Bring-up hints:<br>"
        "1. Stand still in the range window.<br>"
        "2. Use --target-range-m if auto target locks to clutter.<br>"
        "3. Breathing BPM appears after enough slow-time data is collected.",
        color="#aaaaaa",
    )
    win.addItem(notes, row=2, col=1)

    def update():
        snap = state.snapshot()
        header = snap["latest_header"]

        if len(snap["range_m"]) and len(snap["range_db"]):
            range_curve.setData(snap["range_m"], snap["range_db"])
        if snap["target_range_m"] is not None:
            target_line.setValue(snap["target_range_m"])
        if len(snap["motion_t"]):
            raw_curve.setData(snap["motion_t"], snap["motion_raw_mm"])
            breath_curve.setData(snap["motion_t"], snap["motion_breath_mm"])
        if len(snap["spectrum_bpm"]):
            spectrum_curve.setData(snap["spectrum_bpm"], snap["spectrum_db"])
        if snap["breath_bpm"] is not None:
            bpm_line.setValue(snap["breath_bpm"])

        if snap["error"]:
            status.setText(f"<span style='color:#ff6060'>Reader stopped: {snap['error']}</span>")
            return

        bpm_text = "acquiring"
        if snap["breath_bpm"] is not None:
            bpm_text = f"{snap['breath_bpm']:.1f} breaths/min"

        target_text = "auto-searching"
        if snap["target_range_m"] is not None:
            target_text = f"{snap['target_range_m']:.2f} m"
            if snap["target_db"] is not None:
                target_text += f" / {snap['target_db']:.1f} dB"

        seq = header["slope_seq"] if header else "-"
        flags = header["flags"] if header else 0
        fw_drop = header["dropped_frames"] if header else "-"
        status.setText(
            f"<span style='color:#ffffff'>"
            f"Breathing: <b>{bpm_text}</b> &nbsp; "
            f"target={target_text} &nbsp; "
            f"slow_fs={snap['slow_fs']:.1f} Hz &nbsp; confidence={snap['confidence']:.1f}<br>"
            f"USB={snap['rx_mbps']:.1f} Mbps &nbsp; frames={snap['frames']} &nbsp; "
            f"processed={snap['processed']} &nbsp; seq={seq} &nbsp; gaps={snap['gaps']} &nbsp; "
            f"bad={snap['bad']} &nbsp; timeouts={snap['timeouts']} &nbsp; "
            f"fw_drop={fw_drop} &nbsp; flags=0x{flags:04x}"
            f"</span>"
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
