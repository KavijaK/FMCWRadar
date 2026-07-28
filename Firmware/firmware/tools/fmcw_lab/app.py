"""Professional-ish live GUI for the STM32 FMCW radar stream."""

from __future__ import annotations

import collections
import dataclasses
import threading
import time
from typing import Optional

import numpy as np

try:
    import pyqtgraph as pg
    from pyqtgraph.Qt import QtCore, QtWidgets
except ImportError:  # pragma: no cover - dependency check happens in main()
    pg = None
    QtCore = None
    QtWidgets = None

from . import dsp, sar, usb_stream


@dataclasses.dataclass
class AppSettings:
    frames_per_read: int = 16
    process_decimation: int = 10
    slope_id: int = 0
    nfft: int = 16384
    max_range_m: float = 20.0
    min_target_range_m: float = 0.3
    max_target_range_m: float = 5.0
    target_range_m: Optional[float] = None
    phase_bin_half_width: int = 0
    cpi_chirps: int = 128
    range_avg_alpha: float = 0.08
    retarget_s: float = 3.0
    retarget_margin_db: float = 6.0
    config: dsp.RadarConfig = dataclasses.field(default_factory=dsp.RadarConfig)


class SharedState:
    def __init__(self):
        self.lock = threading.Lock()
        self.connected = False
        self.running = False
        self.error = None
        self.status = "Idle"
        self.log = collections.deque(maxlen=80)

        self.frames = 0
        self.processed = 0
        self.bad = 0
        self.gaps = 0
        self.timeouts = 0
        self.rx_mbps = 0.0
        self.last_header = None

        self.time_samples = np.array([], dtype=np.float32)
        self.range_m = np.array([], dtype=np.float64)
        self.range_db = np.array([], dtype=np.float64)
        self.peak_range_m = None
        self.peak_db = None
        self.target_range_m = None
        self.target_bin = None

        self.rd_range_m = np.array([], dtype=np.float64)
        self.rd_velocity_m_s = np.array([], dtype=np.float64)
        self.rd_db = np.empty((0, 0), dtype=np.float32)
        self.rd_peak = None
        self.waterfall_db = np.empty((0, 0), dtype=np.float32)

        self.motion_t = np.array([], dtype=np.float64)
        self.motion_mm = np.array([], dtype=np.float64)
        self.breath_t = np.array([], dtype=np.float64)
        self.breath_mm = np.array([], dtype=np.float64)
        self.heart_t = np.array([], dtype=np.float64)
        self.heart_mm = np.array([], dtype=np.float64)
        self.breath_bpm = None
        self.heart_bpm = None
        self.breath_spec_bpm = np.array([], dtype=np.float64)
        self.breath_spec_db = np.array([], dtype=np.float64)
        self.heart_spec_bpm = np.array([], dtype=np.float64)
        self.heart_spec_db = np.array([], dtype=np.float64)
        self.vital_confidence = 0.0

        self.sar_request = None
        self.sar_progress = ""
        self.sar_x = np.array([], dtype=np.float64)
        self.sar_y = np.array([], dtype=np.float64)
        self.sar_db = np.empty((0, 0), dtype=np.float32)
        self.sar_aperture_db = np.empty((0, 0), dtype=np.float32)

    def add_log(self, message: str):
        with self.lock:
            self.log.append(f"{time.strftime('%H:%M:%S')}  {message}")

    def snapshot(self):
        with self.lock:
            return {
                "connected": self.connected,
                "running": self.running,
                "error": self.error,
                "status": self.status,
                "log": list(self.log),
                "frames": self.frames,
                "processed": self.processed,
                "bad": self.bad,
                "gaps": self.gaps,
                "timeouts": self.timeouts,
                "rx_mbps": self.rx_mbps,
                "last_header": self.last_header,
                "time_samples": self.time_samples,
                "range_m": self.range_m,
                "range_db": self.range_db,
                "peak_range_m": self.peak_range_m,
                "peak_db": self.peak_db,
                "target_range_m": self.target_range_m,
                "target_bin": self.target_bin,
                "rd_range_m": self.rd_range_m,
                "rd_velocity_m_s": self.rd_velocity_m_s,
                "rd_db": self.rd_db,
                "rd_peak": dict(self.rd_peak) if self.rd_peak is not None else None,
                "waterfall_db": self.waterfall_db,
                "motion_t": self.motion_t,
                "motion_mm": self.motion_mm,
                "breath_t": self.breath_t,
                "breath_mm": self.breath_mm,
                "heart_t": self.heart_t,
                "heart_mm": self.heart_mm,
                "breath_bpm": self.breath_bpm,
                "heart_bpm": self.heart_bpm,
                "breath_spec_bpm": self.breath_spec_bpm,
                "breath_spec_db": self.breath_spec_db,
                "heart_spec_bpm": self.heart_spec_bpm,
                "heart_spec_db": self.heart_spec_db,
                "vital_confidence": self.vital_confidence,
                "sar_progress": self.sar_progress,
                "sar_x": self.sar_x,
                "sar_y": self.sar_y,
                "sar_db": self.sar_db,
                "sar_aperture_db": self.sar_aperture_db,
            }

    def request_sar(self, request: dict):
        with self.lock:
            self.sar_request = request
            self.sar_progress = "Queued"
            self.sar_db = np.empty((0, 0), dtype=np.float32)
            self.sar_aperture_db = np.empty((0, 0), dtype=np.float32)

    def take_sar_request(self):
        with self.lock:
            req = self.sar_request
            self.sar_request = None
            return req


class AcquisitionWorker(threading.Thread):
    def __init__(self, state: SharedState, settings: AppSettings):
        super().__init__(daemon=True)
        self.state = state
        self.settings = settings
        self.stop_event = threading.Event()

    def stop(self):
        self.stop_event.set()

    def _set_status(self, status: str):
        with self.state.lock:
            self.state.status = status

    def _set_error(self, error: str):
        with self.state.lock:
            self.state.error = error
            self.state.status = "Stopped"
            self.state.running = False
            self.state.connected = False
            self.state.log.append(f"{time.strftime('%H:%M:%S')}  ERROR: {error}")

    def run(self):
        stream = usb_stream.UsbRadarStream(connect_timeout_s=30.0, read_timeout_ms=2000)
        cfg = self.settings.config
        win = dsp.hann_window(cfg.samples_per_slope)
        range_all = dsp.range_axis(cfg, self.settings.nfft)
        display_bins = np.flatnonzero(range_all <= self.settings.max_range_m)
        target_bins = np.flatnonzero(
            (range_all >= self.settings.min_target_range_m)
            & (range_all <= self.settings.max_target_range_m)
        )
        if len(display_bins) == 0:
            self._set_error("No range bins in display range")
            return

        rd_history = collections.deque(maxlen=self.settings.cpi_chirps)
        waterfall = collections.deque(maxlen=240)
        phase_tracker = dsp.PhaseTracker(cfg, max_points=20000)
        range_power_avg = None
        last_retarget_t = 0.0
        selected_seen = 0

        sar_request = None
        sar_profiles = []
        sar_range = range_all[display_bins]

        frames = processed = bad = gaps = timeouts = 0
        last_seq = None
        last_rate_time = time.monotonic()
        last_rate_frames = 0
        rx_mbps = 0.0

        try:
            self._set_status("Connecting")
            self.state.add_log("Opening USB device 1209:4158")
            stream.connect()
            with self.state.lock:
                self.state.connected = True
                self.state.running = True
                self.state.error = None
            self.state.add_log("USB configured, sending stream-start request")
            stream.start_stream()
            self._set_status("Streaming")

            while not self.stop_event.is_set():
                try:
                    batch = stream.read_frame_batch(self.settings.frames_per_read)
                except TimeoutError:
                    timeouts += 1
                    with self.state.lock:
                        self.state.timeouts = timeouts
                    continue

                latest_samples = None
                latest_range_db = None
                latest_peak = None
                latest_header = None
                latest_breath = None
                latest_heart = None

                if sar_request is None:
                    sar_request = self.state.take_sar_request()
                    if sar_request is not None:
                        sar_profiles = []
                        with self.state.lock:
                            self.state.sar_progress = f"Capturing 0/{sar_request['chirps']} aperture chirps"
                        self.state.add_log("SAR aperture capture started")

                for frame in usb_stream.iter_frames(batch):
                    header = frame.header
                    latest_header = dataclasses.asdict(header)
                    frames += 1
                    if not header.valid:
                        bad += 1
                        continue
                    if last_seq is not None and header.slope_seq != ((last_seq + 1) & 0xFFFFFFFF):
                        gaps += 1
                    last_seq = header.slope_seq
                    if int(header.slope_id) != self.settings.slope_id:
                        continue

                    selected_seen += 1
                    if selected_seen % self.settings.process_decimation != 0:
                        continue

                    samples = usb_stream.decode_samples(frame)
                    spec, range_db = dsp.range_profile_dbfs(samples, cfg, self.settings.nfft, win)
                    display_spec = spec[display_bins]
                    display_db = range_db[display_bins]

                    mag = np.abs(spec)
                    if range_power_avg is None:
                        range_power_avg = mag
                    else:
                        range_power_avg = (
                            (1.0 - self.settings.range_avg_alpha) * range_power_avg
                            + self.settings.range_avg_alpha * mag
                        )

                    latest_peak = dsp.estimate_peak(
                        range_all,
                        range_db,
                        self.settings.min_target_range_m,
                        self.settings.max_target_range_m,
                    )

                    target_bin = self._select_target_bin(
                        range_all,
                        range_power_avg,
                        target_bins,
                        phase_tracker.target_bin,
                        last_retarget_t,
                    )
                    if target_bin is not None:
                        if phase_tracker.target_bin != target_bin:
                            phase_tracker.set_target_bin(target_bin)
                            last_retarget_t = time.monotonic()
                        timestamp_s = header.timestamp_us * 1.0e-6
                        phase_tracker.update(timestamp_s, spec, self.settings.phase_bin_half_width)
                        times, displacement = phase_tracker.arrays()
                        latest_breath = dsp.band_limited_analysis(
                            times,
                            displacement,
                            band_hz=(0.10, 0.70),
                            window_s=30.0,
                            min_analysis_s=8.0,
                        )
                        latest_heart = dsp.band_limited_analysis(
                            times,
                            displacement,
                            band_hz=(0.80, 2.50),
                            window_s=20.0,
                            min_analysis_s=8.0,
                        )

                    rd_history.append(display_spec.astype(np.complex64, copy=False))
                    if len(rd_history) >= max(8, min(self.settings.cpi_chirps, len(rd_history))):
                        avg_db = self._integrated_profile_db(np.asarray(rd_history))
                        waterfall.append(avg_db)

                    if sar_request is not None:
                        sar_profiles.append(display_spec.astype(np.complex64, copy=True))
                        with self.state.lock:
                            self.state.sar_progress = (
                                f"Capturing {len(sar_profiles)}/{sar_request['chirps']} aperture chirps"
                            )
                        if len(sar_profiles) >= sar_request["chirps"]:
                            self._finish_sar(sar_request, np.asarray(sar_profiles), sar_range)
                            sar_request = None
                            sar_profiles = []

                    latest_samples = samples[: min(1600, len(samples))].copy()
                    latest_range_db = display_db.copy()
                    processed += 1

                now = time.monotonic()
                elapsed = now - last_rate_time
                if elapsed >= 1.0:
                    delta_frames = frames - last_rate_frames
                    rx_mbps = (delta_frames * usb_stream.FRAME_LEN * 8.0) / elapsed / 1_000_000.0
                    last_rate_frames = frames
                    last_rate_time = now

                self._publish(
                    frames,
                    processed,
                    bad,
                    gaps,
                    timeouts,
                    rx_mbps,
                    latest_header,
                    latest_samples,
                    range_all[display_bins],
                    latest_range_db,
                    latest_peak,
                    phase_tracker,
                    latest_breath,
                    latest_heart,
                    rd_history,
                    waterfall,
                )
        except Exception as exc:  # pragma: no cover - hardware path
            self._set_error(repr(exc))
        finally:
            stream.close()
            with self.state.lock:
                self.state.connected = False
                self.state.running = False
                if self.state.error is None:
                    self.state.status = "Disconnected"
                    self.state.log.append(f"{time.strftime('%H:%M:%S')}  USB disconnected")

    def _select_target_bin(self, range_all, range_power_avg, target_bins, current_bin, last_retarget_t):
        if self.settings.target_range_m is not None:
            return int(np.argmin(np.abs(range_all - self.settings.target_range_m)))
        if len(target_bins) == 0:
            return None
        if current_bin is not None and (time.monotonic() - last_retarget_t) < self.settings.retarget_s:
            return current_bin

        candidate = int(target_bins[int(np.argmax(range_power_avg[target_bins]))])
        if current_bin is None:
            return candidate
        current = float(range_power_avg[current_bin]) + 1.0e-12
        new = float(range_power_avg[candidate]) + 1.0e-12
        if new > current * (10.0 ** (self.settings.retarget_margin_db / 20.0)):
            return candidate
        return current_bin

    @staticmethod
    def _integrated_profile_db(history: np.ndarray) -> np.ndarray:
        power = np.mean(np.abs(history) ** 2, axis=0)
        db = 10.0 * np.log10(power + 1.0e-18)
        return (db - float(np.max(db))).astype(np.float32)

    def _finish_sar(self, request: dict, profiles: np.ndarray, sar_range: np.ndarray):
        with self.state.lock:
            self.state.sar_progress = "Forming SAR image"
        step = max(1.0e-5, float(request["platform_speed_m_s"]) * self.settings.config.triangle_period_s * self.settings.process_decimation)
        aperture_db = sar.aperture_fft_image(profiles, remove_static=False).astype(np.float32)
        try:
            x, y, image_db = sar.backprojection_image(
                profiles,
                sar_range,
                platform_step_m=step,
                center_hz=self.settings.config.center_hz,
                x_extent_m=float(request["x_extent_m"]),
                y_min_m=float(request["y_min_m"]),
                y_max_m=float(request["y_max_m"]),
                nx=int(request["nx"]),
                ny=int(request["ny"]),
            )
            image_db = image_db.astype(np.float32)
        except Exception as exc:
            x = np.array([], dtype=np.float64)
            y = np.array([], dtype=np.float64)
            image_db = np.empty((0, 0), dtype=np.float32)
            self.state.add_log(f"SAR backprojection failed: {exc!r}")

        with self.state.lock:
            self.state.sar_x = x
            self.state.sar_y = y
            self.state.sar_db = image_db
            self.state.sar_aperture_db = aperture_db
            self.state.sar_progress = f"Done: {profiles.shape[0]} aperture chirps"
        self.state.add_log("SAR aperture capture complete")

    def _publish(
        self,
        frames,
        processed,
        bad,
        gaps,
        timeouts,
        rx_mbps,
        latest_header,
        latest_samples,
        range_view,
        latest_range_db,
        latest_peak,
        phase_tracker,
        latest_breath,
        latest_heart,
        rd_history,
        waterfall,
    ):
        with self.state.lock:
            self.state.frames = frames
            self.state.processed = processed
            self.state.bad = bad
            self.state.gaps = gaps
            self.state.timeouts = timeouts
            self.state.rx_mbps = rx_mbps
            if latest_header is not None:
                self.state.last_header = latest_header
            if latest_samples is not None:
                shown = latest_samples.astype(np.float32)
                shown -= float(np.mean(shown))
                self.state.time_samples = shown
            if latest_range_db is not None:
                self.state.range_m = range_view
                self.state.range_db = latest_range_db
            if latest_peak is not None:
                peak_bin, peak_range, peak_db = latest_peak
                self.state.peak_range_m = peak_range
                self.state.peak_db = peak_db
            if phase_tracker.target_bin is not None:
                self.state.target_bin = phase_tracker.target_bin
                if phase_tracker.target_bin < len(range_view):
                    self.state.target_range_m = float(range_view[phase_tracker.target_bin])
            times, disp = phase_tracker.arrays()
            if len(times):
                rel = times - times[-1]
                keep = rel >= -30.0
                self.state.motion_t = rel[keep]
                mm = (disp[keep] - np.mean(disp[keep])) * 1000.0
                self.state.motion_mm = mm
            if latest_breath is not None:
                keep = latest_breath["t"] >= -30.0
                self.state.breath_t = latest_breath["t"][keep]
                self.state.breath_mm = latest_breath["wave_mm"][keep]
                self.state.breath_spec_bpm = latest_breath["spectrum_hz"] * 60.0
                self.state.breath_spec_db = latest_breath["spectrum_db"]
                self.state.breath_bpm = latest_breath["rate_per_min"]
                self.state.vital_confidence = latest_breath["confidence"]
            if latest_heart is not None:
                keep = latest_heart["t"] >= -20.0
                self.state.heart_t = latest_heart["t"][keep]
                self.state.heart_mm = latest_heart["wave_mm"][keep]
                self.state.heart_spec_bpm = latest_heart["spectrum_hz"] * 60.0
                self.state.heart_spec_db = latest_heart["spectrum_db"]
                self.state.heart_bpm = latest_heart["rate_per_min"]
            if len(rd_history) >= self.settings.cpi_chirps:
                stack = np.asarray(rd_history)
                rd = dsp.range_doppler_db(stack)
                self.state.rd_db = rd.astype(np.float32)
                self.state.rd_range_m = range_view
                velocity_axis = dsp.doppler_velocity_axis(
                    self.settings.config,
                    stack.shape[0],
                    self.settings.process_decimation,
                )
                self.state.rd_velocity_m_s = velocity_axis
                self.state.rd_peak = dsp.estimate_range_doppler_peak(
                    rd,
                    range_view,
                    velocity_axis,
                    min_range_m=self.settings.min_target_range_m,
                    max_range_m=self.settings.max_range_m,
                    min_abs_velocity_m_s=0.03,
                )
            if len(waterfall):
                self.state.waterfall_db = np.asarray(waterfall, dtype=np.float32)


class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("FMCW Lab - STM32 Radar Workbench")
        self.resize(1450, 920)
        self.state = SharedState()
        self.worker = None
        self.heat_lut = self._make_heat_lut()

        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        layout = QtWidgets.QVBoxLayout(central)
        layout.addLayout(self._build_controls())

        self.tabs = QtWidgets.QTabWidget()
        layout.addWidget(self.tabs, 1)
        self._build_range_tab()
        self._build_rd_tab()
        self._build_vital_tab()
        self._build_sar_tab()
        self._build_log_tab()

        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.update_plots)
        self.timer.start(50)

    @staticmethod
    def _make_heat_lut():
        cmap = pg.ColorMap(
            pos=np.array([0.0, 0.15, 0.35, 0.60, 0.82, 1.0]),
            color=np.array([
                [0, 0, 0, 255],
                [0, 0, 120, 255],
                [0, 190, 255, 255],
                [255, 255, 0, 255],
                [255, 70, 0, 255],
                [255, 255, 255, 255],
            ], dtype=np.ubyte),
        )
        return cmap.getLookupTable(0.0, 1.0, 256)

    def _build_controls(self):
        row = QtWidgets.QHBoxLayout()
        self.connect_btn = QtWidgets.QPushButton("Connect + Start USB")
        self.disconnect_btn = QtWidgets.QPushButton("Disconnect")
        self.disconnect_btn.setEnabled(False)
        self.connect_btn.clicked.connect(self.connect_usb)
        self.disconnect_btn.clicked.connect(self.disconnect_usb)

        self.slope_box = QtWidgets.QComboBox()
        self.slope_box.addItems(["0", "1"])
        self.decim_spin = QtWidgets.QSpinBox()
        self.decim_spin.setRange(1, 200)
        self.decim_spin.setValue(10)
        self.cpi_spin = QtWidgets.QSpinBox()
        self.cpi_spin.setRange(16, 1024)
        self.cpi_spin.setValue(128)
        self.nfft_box = QtWidgets.QComboBox()
        self.nfft_box.addItems(["8192", "16384", "32768"])
        self.nfft_box.setCurrentText("16384")
        self.max_range_edit = QtWidgets.QLineEdit("20.0")
        self.target_range_edit = QtWidgets.QLineEdit("")
        self.status_label = QtWidgets.QLabel("Idle")

        for widget in [
            self.connect_btn,
            self.disconnect_btn,
            QtWidgets.QLabel("Slope"),
            self.slope_box,
            QtWidgets.QLabel("Process decim"),
            self.decim_spin,
            QtWidgets.QLabel("CPI"),
            self.cpi_spin,
            QtWidgets.QLabel("NFFT"),
            self.nfft_box,
            QtWidgets.QLabel("Max range m"),
            self.max_range_edit,
            QtWidgets.QLabel("Target m blank=auto"),
            self.target_range_edit,
        ]:
            row.addWidget(widget)
        row.addStretch(1)
        row.addWidget(self.status_label)
        return row

    def _build_range_tab(self):
        widget = pg.GraphicsLayoutWidget()
        self.tabs.addTab(widget, "Range")
        self.time_plot = widget.addPlot(row=0, col=0, title="ADC Time Domain")
        self.time_plot.setLabel("bottom", "sample")
        self.time_plot.setLabel("left", "ADC", "mV")
        self.time_curve = self.time_plot.plot(pen=pg.mkPen("y", width=1))

        self.range_plot = widget.addPlot(row=1, col=0, title="FMCW Range Profile")
        self.range_plot.setLabel("bottom", "range", "m")
        self.range_plot.setLabel("left", "magnitude", "dBFS")
        self.range_curve = self.range_plot.plot(pen=pg.mkPen("c", width=2))
        self.peak_line = pg.InfiniteLine(angle=90, movable=False, pen=pg.mkPen("g", width=1))
        self.target_line = pg.InfiniteLine(angle=90, movable=False, pen=pg.mkPen("m", width=2))
        self.range_plot.addItem(self.peak_line)
        self.range_plot.addItem(self.target_line)
        self.range_status = pg.LabelItem(justify="left")
        widget.addItem(self.range_status, row=2, col=0)

    def _build_rd_tab(self):
        widget = pg.GraphicsLayoutWidget()
        self.tabs.addTab(widget, "Range-Doppler")
        self.rd_plot = widget.addPlot(row=0, col=0, title="Range-Doppler Map")
        self.rd_plot.setLabel("bottom", "range", "m")
        self.rd_plot.setLabel("left", "velocity", "m/s")
        self.rd_img = pg.ImageItem()
        self.rd_img.setLookupTable(self.heat_lut)
        self.rd_plot.addItem(self.rd_img)
        self.rd_plot.setMouseEnabled(x=True, y=True)
        self.rd_peak_marker = pg.ScatterPlotItem(
            size=12,
            pen=pg.mkPen("w", width=2),
            brush=pg.mkBrush(255, 0, 255, 180),
        )
        self.rd_plot.addItem(self.rd_peak_marker)
        self.waterfall_plot = widget.addPlot(row=1, col=0, title="Integrated Range Waterfall")
        self.waterfall_plot.setLabel("bottom", "range", "m")
        self.waterfall_plot.setLabel("left", "CPI history")
        self.waterfall_img = pg.ImageItem()
        self.waterfall_img.setLookupTable(self.heat_lut)
        self.waterfall_plot.addItem(self.waterfall_img)
        self.rd_status = pg.LabelItem(justify="left")
        widget.addItem(self.rd_status, row=2, col=0)

    def _build_vital_tab(self):
        widget = pg.GraphicsLayoutWidget()
        self.tabs.addTab(widget, "Micromotion + Vitals")
        self.motion_plot = widget.addPlot(row=0, col=0, title="Target Bin Phase Displacement")
        self.motion_plot.setLabel("bottom", "time", "s")
        self.motion_plot.setLabel("left", "displacement", "mm")
        self.motion_plot.setXRange(-30, 0)
        self.motion_curve = self.motion_plot.plot(pen=pg.mkPen(180, 180, 180, width=1))
        self.breath_curve = self.motion_plot.plot(pen=pg.mkPen("g", width=2))
        self.heart_curve = self.motion_plot.plot(pen=pg.mkPen("r", width=1))

        self.breath_spec_plot = widget.addPlot(row=1, col=0, title="Breathing Spectrum")
        self.breath_spec_plot.setLabel("bottom", "breaths/min")
        self.breath_spec_plot.setLabel("left", "relative", "dB")
        self.breath_spec_plot.setXRange(6, 42)
        self.breath_spec_curve = self.breath_spec_plot.plot(pen=pg.mkPen("g", width=2))

        self.heart_spec_plot = widget.addPlot(row=2, col=0, title="Heartbeat Spectrum")
        self.heart_spec_plot.setLabel("bottom", "beats/min")
        self.heart_spec_plot.setLabel("left", "relative", "dB")
        self.heart_spec_plot.setXRange(48, 150)
        self.heart_spec_curve = self.heart_spec_plot.plot(pen=pg.mkPen("r", width=2))
        self.vital_status = pg.LabelItem(justify="left")
        widget.addItem(self.vital_status, row=3, col=0)

    def _build_sar_tab(self):
        tab = QtWidgets.QWidget()
        self.tabs.addTab(tab, "SAR Experiments")
        layout = QtWidgets.QVBoxLayout(tab)
        controls = QtWidgets.QHBoxLayout()
        self.sar_chirps_spin = QtWidgets.QSpinBox()
        self.sar_chirps_spin.setRange(16, 2048)
        self.sar_chirps_spin.setValue(256)
        self.sar_speed_edit = QtWidgets.QLineEdit("0.05")
        self.sar_width_edit = QtWidgets.QLineEdit("1.0")
        self.sar_y_min_edit = QtWidgets.QLineEdit("0.3")
        self.sar_y_max_edit = QtWidgets.QLineEdit("5.0")
        self.sar_capture_btn = QtWidgets.QPushButton("Capture SAR Aperture")
        self.sar_capture_btn.clicked.connect(self.request_sar_capture)
        self.sar_progress_label = QtWidgets.QLabel("Idle")
        for w in [
            QtWidgets.QLabel("Chirps"),
            self.sar_chirps_spin,
            QtWidgets.QLabel("Platform speed m/s"),
            self.sar_speed_edit,
            QtWidgets.QLabel("Image width m"),
            self.sar_width_edit,
            QtWidgets.QLabel("Y min"),
            self.sar_y_min_edit,
            QtWidgets.QLabel("Y max"),
            self.sar_y_max_edit,
            self.sar_capture_btn,
            self.sar_progress_label,
        ]:
            controls.addWidget(w)
        controls.addStretch(1)
        layout.addLayout(controls)

        self.sar_graph = pg.GraphicsLayoutWidget()
        layout.addWidget(self.sar_graph, 1)
        self.sar_plot = self.sar_graph.addPlot(row=0, col=0, title="Backprojection SAR Image")
        self.sar_plot.setLabel("bottom", "cross-range", "m")
        self.sar_plot.setLabel("left", "range", "m")
        self.sar_img = pg.ImageItem()
        self.sar_img.setLookupTable(self.heat_lut)
        self.sar_plot.addItem(self.sar_img)
        self.aperture_plot = self.sar_graph.addPlot(row=0, col=1, title="Aperture FFT Quicklook")
        self.aperture_plot.setLabel("bottom", "range bin")
        self.aperture_plot.setLabel("left", "aperture spatial frequency")
        self.aperture_img = pg.ImageItem()
        self.aperture_img.setLookupTable(self.heat_lut)
        self.aperture_plot.addItem(self.aperture_img)

    def _build_log_tab(self):
        tab = QtWidgets.QWidget()
        self.tabs.addTab(tab, "Health + Log")
        layout = QtWidgets.QVBoxLayout(tab)
        self.log_text = QtWidgets.QPlainTextEdit()
        self.log_text.setReadOnly(True)
        layout.addWidget(self.log_text)

    def build_settings(self) -> AppSettings:
        target = self._optional_float(self.target_range_edit.text())
        cfg = dsp.RadarConfig()
        settings = AppSettings(
            frames_per_read=16,
            process_decimation=int(self.decim_spin.value()),
            slope_id=int(self.slope_box.currentText()),
            nfft=int(self.nfft_box.currentText()),
            max_range_m=float(self.max_range_edit.text()),
            target_range_m=target,
            cpi_chirps=int(self.cpi_spin.value()),
            config=cfg,
        )
        return settings

    @staticmethod
    def _optional_float(text: str):
        text = text.strip()
        if not text:
            return None
        return float(text)

    def connect_usb(self):
        if self.worker is not None and self.worker.is_alive():
            return
        try:
            settings = self.build_settings()
        except ValueError as exc:
            QtWidgets.QMessageBox.warning(self, "Invalid Settings", str(exc))
            return

        self.state = SharedState()
        self.worker = AcquisitionWorker(self.state, settings)
        self.worker.start()
        self.connect_btn.setEnabled(False)
        self.disconnect_btn.setEnabled(True)

    def disconnect_usb(self):
        if self.worker is not None:
            self.worker.stop()
            self.worker.join(timeout=1.0)
        self.connect_btn.setEnabled(True)
        self.disconnect_btn.setEnabled(False)

    def request_sar_capture(self):
        if self.worker is None or not self.worker.is_alive():
            QtWidgets.QMessageBox.information(self, "SAR Capture", "Connect to the radar first.")
            return
        try:
            request = {
                "chirps": int(self.sar_chirps_spin.value()),
                "platform_speed_m_s": float(self.sar_speed_edit.text()),
                "x_extent_m": float(self.sar_width_edit.text()),
                "y_min_m": float(self.sar_y_min_edit.text()),
                "y_max_m": float(self.sar_y_max_edit.text()),
                "nx": 96,
                "ny": 160,
            }
        except ValueError as exc:
            QtWidgets.QMessageBox.warning(self, "Invalid SAR Settings", str(exc))
            return
        self.state.request_sar(request)

    def update_plots(self):
        snap = self.state.snapshot()
        if not snap["running"] and self.worker is not None and not self.worker.is_alive():
            self.connect_btn.setEnabled(True)
            self.disconnect_btn.setEnabled(False)

        self.status_label.setText(
            f"{snap['status']}{' | ' + snap['error'] if snap['error'] else ''} | "
            f"USB {snap['rx_mbps']:.1f} Mbps | "
            f"frames {snap['frames']} | processed {snap['processed']} | "
            f"gaps {snap['gaps']} | bad {snap['bad']}"
        )

        if len(snap["time_samples"]):
            self.time_curve.setData(snap["time_samples"])
        if len(snap["range_m"]) and len(snap["range_db"]):
            self.range_curve.setData(snap["range_m"], snap["range_db"])
            self.range_plot.setXRange(0, max(1.0, float(snap["range_m"][-1])), padding=0)
        if snap["peak_range_m"] is not None:
            self.peak_line.setValue(snap["peak_range_m"])
        if snap["target_range_m"] is not None:
            self.target_line.setValue(snap["target_range_m"])
        self.range_status.setText(
            f"Peak: {snap['peak_range_m'] if snap['peak_range_m'] is not None else 0:.2f} m / "
            f"{snap['peak_db'] if snap['peak_db'] is not None else 0:.1f} dBFS  | "
            f"Target bin: {snap['target_bin']}  target range: "
            f"{snap['target_range_m'] if snap['target_range_m'] is not None else 0:.2f} m",
            color="#ffffff",
        )

        if snap["rd_db"].size and len(snap["rd_range_m"]) and len(snap["rd_velocity_m_s"]):
            self.rd_img.setImage(snap["rd_db"].T, autoLevels=False, levels=(-50, 0))
            x0 = float(snap["rd_velocity_m_s"][0])
            x1 = float(snap["rd_velocity_m_s"][-1])
            y0 = float(snap["rd_range_m"][0])
            y1 = float(snap["rd_range_m"][-1])
            self.rd_img.setRect(QtCore.QRectF(y0, x0, y1 - y0, x1 - x0))
            if snap["rd_peak"] is not None:
                self.rd_peak_marker.setData(
                    [snap["rd_peak"]["range_m"]],
                    [snap["rd_peak"]["velocity_m_s"]],
                )
                self.rd_status.setText(
                    f"2D FFT moving-target peak: range={snap['rd_peak']['range_m']:.2f} m, "
                    f"velocity={snap['rd_peak']['velocity_m_s']:+.3f} m/s, "
                    f"level={snap['rd_peak']['level_db']:.1f} dB. "
                    f"Positive/negative sign depends on selected slope and RF phase convention.",
                    color="#ffffff",
                )
            else:
                self.rd_peak_marker.setData([], [])
                self.rd_status.setText("2D FFT moving-target peak: acquiring", color="#ffffff")
        if snap["waterfall_db"].size and len(snap["rd_range_m"]):
            self.waterfall_img.setImage(snap["waterfall_db"].T, autoLevels=False, levels=(-50, 0))
            y1 = snap["waterfall_db"].shape[0]
            self.waterfall_img.setRect(QtCore.QRectF(float(snap["rd_range_m"][0]), 0.0, float(snap["rd_range_m"][-1]), float(y1)))

        if len(snap["motion_t"]):
            self.motion_curve.setData(snap["motion_t"], snap["motion_mm"])
        if len(snap["breath_t"]):
            self.breath_curve.setData(snap["breath_t"], snap["breath_mm"])
        if len(snap["heart_t"]):
            self.heart_curve.setData(snap["heart_t"], snap["heart_mm"])
        if len(snap["breath_spec_bpm"]):
            self.breath_spec_curve.setData(snap["breath_spec_bpm"], snap["breath_spec_db"])
        if len(snap["heart_spec_bpm"]):
            self.heart_spec_curve.setData(snap["heart_spec_bpm"], snap["heart_spec_db"])
        breath = "acquiring" if snap["breath_bpm"] is None else f"{snap['breath_bpm']:.1f} breaths/min"
        heart = "acquiring" if snap["heart_bpm"] is None else f"{snap['heart_bpm']:.1f} bpm"
        self.vital_status.setText(
            f"Breathing: <b>{breath}</b> &nbsp;&nbsp; Heartbeat: <b>{heart}</b> "
            f"&nbsp;&nbsp; Confidence: {snap['vital_confidence']:.1f}",
            color="#ffffff",
        )

        self.sar_progress_label.setText(snap["sar_progress"])
        if snap["sar_db"].size and len(snap["sar_x"]) and len(snap["sar_y"]):
            self.sar_img.setImage(snap["sar_db"].T, autoLevels=False, levels=(-50, 0))
            x0 = float(snap["sar_x"][0])
            x1 = float(snap["sar_x"][-1])
            y0 = float(snap["sar_y"][0])
            y1 = float(snap["sar_y"][-1])
            self.sar_img.setRect(QtCore.QRectF(x0, y0, x1 - x0, y1 - y0))
        if snap["sar_aperture_db"].size:
            self.aperture_img.setImage(snap["sar_aperture_db"].T, autoLevels=False, levels=(-50, 0))

        self.log_text.setPlainText("\n".join(snap["log"]))

    def closeEvent(self, event):
        self.disconnect_usb()
        super().closeEvent(event)


def main():
    if pg is None:
        raise SystemExit("Install dependencies: python -m pip install pyusb libusb-package numpy pyqtgraph PyQt6")
    app = QtWidgets.QApplication([])
    pg.setConfigOptions(antialias=False)
    win = MainWindow()
    win.show()
    app.exec()


if __name__ == "__main__":
    main()
