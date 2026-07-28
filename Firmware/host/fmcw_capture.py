from __future__ import annotations

import argparse
from pathlib import Path
import sys
import time

try:
    import usb.core
    import usb.util
except ImportError:  # pragma: no cover - exercised only on machines without pyusb
    usb = None

try:
    import numpy as np
except ImportError:  # pragma: no cover
    np = None

from fmcw_protocol import (
    FRAME_SIZE,
    HEADER_SIZE,
    MAGIC_BYTES,
    PAYLOAD_SIZE,
    SAMPLES_PER_SLOPE,
    parse_header,
)


def open_bulk_in(vid: int, pid: int):
    if usb is None:
        raise RuntimeError("pyusb is not installed; run `pip install -r host/requirements.txt`")

    dev = usb.core.find(idVendor=vid, idProduct=pid)
    if dev is None:
        raise RuntimeError(f"USB device {vid:04x}:{pid:04x} not found")

    try:
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
    except (NotImplementedError, usb.core.USBError):
        pass

    dev.set_configuration()
    cfg = dev.get_active_configuration()
    intf = cfg[(0, 0)]
    ep_in = usb.util.find_descriptor(
        intf,
        custom_match=lambda e: (
            usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN
            and usb.util.endpoint_type(e.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK
        ),
    )
    if ep_in is None:
        raise RuntimeError("bulk IN endpoint not found")
    return dev, ep_in


def read_exact(ep_in, size: int, timeout_ms: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = ep_in.read(size - len(data), timeout=timeout_ms)
        data.extend(bytes(chunk))
    return bytes(data)


class FrameReader:
    def __init__(self, ep_in, timeout_ms: int):
        self.ep_in = ep_in
        self.timeout_ms = timeout_ms
        self.buf = bytearray()

    def read_frame(self) -> tuple[object, bytes]:
        while len(self.buf) < FRAME_SIZE:
            self.buf.extend(read_exact(self.ep_in, FRAME_SIZE - len(self.buf), self.timeout_ms))

        while True:
            pos = self.buf.find(MAGIC_BYTES)
            if pos < 0:
                self.buf.clear()
                self.buf.extend(read_exact(self.ep_in, FRAME_SIZE, self.timeout_ms))
                continue
            if pos:
                del self.buf[:pos]
            while len(self.buf) < FRAME_SIZE:
                self.buf.extend(read_exact(self.ep_in, FRAME_SIZE - len(self.buf), self.timeout_ms))
            header = parse_header(self.buf[:HEADER_SIZE])
            try:
                header.validate()
            except ValueError:
                del self.buf[0:1]
                continue
            frame = bytes(self.buf[:FRAME_SIZE])
            del self.buf[:FRAME_SIZE]
            return header, frame


def payload_to_samples(payload: bytes):
    if np is None:
        return None
    return np.frombuffer(payload, dtype="<u2", count=SAMPLES_PER_SLOPE) & 0x0FFF


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Capture STM32 FMCW radar frames over USB HS bulk IN.")
    parser.add_argument("--vid", type=lambda x: int(x, 0), default=0x0483)
    parser.add_argument("--pid", type=lambda x: int(x, 0), default=0x5740)
    parser.add_argument("--frames", type=int, default=0, help="number of frames to capture; 0 means forever")
    parser.add_argument("--timeout-ms", type=int, default=2000)
    parser.add_argument("--raw", type=Path, help="write complete 20,064-byte frames to this file")
    parser.add_argument("--npy", type=Path, help="write uint16 sample matrix to .npy after capture")
    parser.add_argument("--stats", action="store_true", help="print running sequence/flag stats")
    parser.add_argument("--fft", action="store_true", help="print peak FFT bin for each frame")
    args = parser.parse_args(argv)

    if args.npy and np is None:
        raise RuntimeError("numpy is required for --npy")
    if args.fft and np is None:
        raise RuntimeError("numpy is required for --fft")

    _, ep_in = open_bulk_in(args.vid, args.pid)
    reader = FrameReader(ep_in, args.timeout_ms)

    raw_file = args.raw.open("wb") if args.raw else None
    samples = [] if args.npy else None
    frames = 0
    dropped = 0
    last_seq = None
    start = time.monotonic()

    try:
        while args.frames == 0 or frames < args.frames:
            header, frame = reader.read_frame()
            payload = frame[HEADER_SIZE:]
            if raw_file:
                raw_file.write(frame)
            if last_seq is not None and header.slope_seq != last_seq + 1:
                dropped += header.slope_seq - last_seq - 1
            last_seq = header.slope_seq

            if header.flags:
                dropped += 1 if header.flags & 0x01 else 0

            frame_samples = payload_to_samples(payload)
            if samples is not None and frame_samples is not None:
                samples.append(frame_samples.copy())
            if args.fft and frame_samples is not None:
                spectrum = np.fft.rfft(frame_samples.astype(np.float32) - 2048.0)
                peak = int(np.argmax(np.abs(spectrum[1:])) + 1)
                print(f"seq={header.slope_seq} slope={header.slope_id} peak_bin={peak} flags={header.flag_names}")

            frames += 1
            if args.stats and (frames == 1 or frames % 100 == 0):
                elapsed = max(time.monotonic() - start, 1e-9)
                mb_s = (frames * FRAME_SIZE) / elapsed / 1_000_000
                print(
                    f"frames={frames} seq={header.slope_seq} mux={header.muxout_count} "
                    f"flags={header.flag_names or '-'} gaps={dropped} rate={mb_s:.2f} MB/s"
                )
    finally:
        if raw_file:
            raw_file.close()

    if args.npy and samples is not None:
        np.save(args.npy, np.vstack(samples) if samples else np.empty((0, SAMPLES_PER_SLOPE), dtype=np.uint16))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
