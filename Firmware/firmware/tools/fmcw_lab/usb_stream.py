"""USB HS frame acquisition for the STM32 FMCW radar firmware.

This module intentionally keeps the same stream format used by the tested
tools in this repository:

* VID/PID 1209:4158
* bulk IN endpoint 0x81
* vendor request 0x30 starts streaming
* one fixed 20480-byte USB frame per FMCW slope
"""

from __future__ import annotations

import dataclasses
import struct
import time
from typing import Iterable

import numpy as np
import usb.core
import usb.util
from usb.backend import libusb1

try:
    import libusb_package
except ImportError:  # pragma: no cover - optional dependency path
    libusb_package = None


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


@dataclasses.dataclass(frozen=True)
class FrameHeader:
    magic: int
    version: int
    header_len: int
    frame_len: int
    slope_seq: int
    triangle_seq: int
    slope_id: int
    flags: int
    sample_rate_hz: int
    samples_per_slope: int
    adc_bits: int
    words_per_slope: int
    timestamp_us: int
    muxout_count: int
    dropped_frames: int
    dcmi_risr: int
    dma_lisr: int
    reserved0: int
    reserved1: int

    @property
    def valid(self) -> bool:
        return (
            self.magic == MAGIC
            and self.version == 1
            and self.header_len == HEADER_LEN
            and self.frame_len == FRAME_LEN
            and self.samples_per_slope == SAMPLES_PER_SLOPE
            and self.words_per_slope == PAYLOAD_WORDS
        )


@dataclasses.dataclass(frozen=True)
class FrameView:
    header: FrameHeader
    batch: bytes
    offset: int


def get_backend():
    if libusb_package is None:
        return libusb1.get_backend()
    return libusb1.get_backend(find_library=libusb_package.find_library)


def list_usb_devices(backend=None) -> list[tuple[int, int]]:
    backend = backend or get_backend()
    if backend is None:
        return []
    return [(dev.idVendor, dev.idProduct) for dev in usb.core.find(find_all=True, backend=backend)]


def parse_header(buf: bytes | bytearray | memoryview, offset: int = 0) -> FrameHeader:
    return FrameHeader(*HEADER.unpack_from(buf, offset))


def iter_frames(batch: bytes) -> Iterable[FrameView]:
    for offset in range(0, len(batch), FRAME_LEN):
        if offset + FRAME_LEN > len(batch):
            break
        yield FrameView(parse_header(batch, offset), batch, offset)


def _sign_extend_12(raw: np.ndarray) -> np.ndarray:
    raw = (raw & 0x0FFF).astype(np.int16, copy=False)
    return ((raw ^ 0x0800) - 0x0800).astype(np.int16, copy=False)


def decode_samples(frame: FrameView) -> np.ndarray:
    words = np.frombuffer(
        frame.batch,
        dtype="<u4",
        count=PAYLOAD_WORDS,
        offset=frame.offset + HEADER_LEN,
    )
    lo = _sign_extend_12(words.astype(np.int32) & 0x0FFF)
    hi = _sign_extend_12((words.astype(np.int32) >> 16) & 0x0FFF)
    samples = np.empty(SAMPLES_PER_SLOPE, dtype=np.float32)
    samples[0::2] = lo
    samples[1::2] = hi
    return samples


class UsbRadarStream:
    def __init__(self, connect_timeout_s: float = 30.0, read_timeout_ms: int = 2000):
        self.connect_timeout_s = connect_timeout_s
        self.read_timeout_ms = read_timeout_ms
        self.backend = get_backend()
        self.dev = None

    def connect(self):
        if self.backend is None:
            raise RuntimeError("No PyUSB/libusb backend. Install pyusb and libusb-package.")

        deadline = time.monotonic() + self.connect_timeout_s
        last_report = 0.0
        access_hint_printed = False
        last_error = None
        while time.monotonic() < deadline:
            dev = usb.core.find(idVendor=VID, idProduct=PID, backend=self.backend)
            if dev is None:
                now = time.monotonic()
                if now - last_report >= 2.0:
                    visible = " ".join(f"{v:04x}:{p:04x}" for v, p in list_usb_devices(self.backend))
                    print(f"Waiting for FMCW USB device. Visible: {visible or 'none'}")
                    last_report = now
                time.sleep(0.25)
                continue

            try:
                dev.set_configuration()
                self.dev = dev
                return
            except NotImplementedError as exc:
                last_error = exc
                if not access_hint_printed:
                    print("FMCW device is visible but libusb cannot open it.")
                    print("Bind 1209:4158 to WinUSB with Zadig, close other viewers, then unplug/replug.")
                    access_hint_printed = True
                usb.util.dispose_resources(dev)
                time.sleep(0.5)
            except usb.core.USBError as exc:
                last_error = exc
                if getattr(exc, "errno", None) == 13 and not access_hint_printed:
                    print("FMCW device is visible, but Windows denied access.")
                    print("Close other stream/viewer scripts, unplug/replug USB, or rebind WinUSB with Zadig.")
                    access_hint_printed = True
                else:
                    print(f"FMCW USB open/config failed: {exc}. Retrying...")
                usb.util.dispose_resources(dev)
                time.sleep(0.5)

        if last_error is not None:
            raise RuntimeError(f"FMCW USB HS stream device not ready: {last_error!r}")
        raise RuntimeError("FMCW USB HS stream device not found")

    def start_stream(self):
        if self.dev is None:
            raise RuntimeError("USB device is not connected")
        self.dev.ctrl_transfer(0x40, REQ_START_STREAM, 0, 0, None)

    def read_exact(self, size: int) -> bytes:
        if self.dev is None:
            raise RuntimeError("USB device is not connected")

        chunks = []
        remaining = size
        while remaining:
            try:
                chunk = bytes(self.dev.read(EP_IN, remaining, timeout=self.read_timeout_ms))
            except usb.core.USBTimeoutError as exc:
                have = size - remaining
                raise TimeoutError(f"USB read timeout, have {have}/{size} bytes") from exc
            if not chunk:
                raise TimeoutError("USB read returned no data")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def read_frame_batch(self, frames_per_read: int) -> bytes:
        return self.read_exact(FRAME_LEN * frames_per_read)

    def close(self):
        if self.dev is not None:
            usb.util.dispose_resources(self.dev)
            self.dev = None
