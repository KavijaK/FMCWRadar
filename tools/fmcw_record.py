#!/usr/bin/env python3
"""Record raw FMCW USB frames to disk.

This script does no plotting. Its job is to drain USB quickly and save the
exact byte stream so DSP can be repeated offline without stressing the live
USB link.
"""

import argparse
import datetime as dt
import os
import struct
import time

import usb.core
import usb.util
from usb.backend import libusb1

try:
    import libusb_package
except ImportError:
    libusb_package = None


VID = 0x1209
PID = 0x4158
EP_IN = 0x81
REQ_START_STREAM = 0x30

FRAME_LEN = 20480
HEADER_LEN = 64
MAGIC = 0x52444152
HEADER = struct.Struct("<IHHIIIHHIIHHIIIIIII")
CONNECT_TIMEOUT_S = 30.0


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


def open_device(backend):
    deadline = time.monotonic() + CONNECT_TIMEOUT_S
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
        except usb.core.USBError as exc:
            if getattr(exc, "errno", None) == 13 and not access_hint_printed:
                print("FMCW device is visible, but Windows denied access.")
                print("Close other stream/viewer scripts, unplug/replug USB, or rebind WinUSB with Zadig.")
                access_hint_printed = True
            else:
                print(f"FMCW device appeared but open/config failed: {exc}. Retrying...")
            usb.util.dispose_resources(dev)
            time.sleep(0.5)
        except NotImplementedError as exc:
            print(f"libusb cannot open the FMCW device: {exc}")
            print("Bind the FMCW interface to WinUSB with Zadig.")
            time.sleep(1.0)

    list_usb_devices(backend)
    raise SystemExit("FMCW USB HS stream device not ready")


def read_exact(dev, size, timeout_ms=3000):
    chunks = []
    remaining = size
    while remaining:
        chunk = bytes(dev.read(EP_IN, remaining, timeout=timeout_ms))
        if not chunk:
            raise TimeoutError("USB read returned no data")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def parse_header(buf, offset):
    header = HEADER.unpack_from(buf, offset)
    return {
        "magic": header[0],
        "header_len": header[2],
        "frame_len": header[3],
        "slope_seq": header[4],
        "flags": header[7],
        "dropped_frames": header[14],
    }


def default_output_path():
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    return os.path.join("captures", f"fmcw_{stamp}.bin")


def build_argparser():
    parser = argparse.ArgumentParser(description="Record raw FMCW USB frames.")
    parser.add_argument("-o", "--output", default=default_output_path())
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--frames-per-read", type=int, default=16)
    parser.add_argument("--no-start-command", action="store_true",
                        help="Do not send the USB vendor stream-start request.")
    return parser


def main():
    args = build_argparser().parse_args()
    backend = get_backend()
    if backend is None:
        raise SystemExit("Install dependencies: python -m pip install pyusb libusb-package")

    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    batch_len = FRAME_LEN * args.frames_per_read

    dev = open_device(backend)
    if not args.no_start_command:
        dev.ctrl_transfer(0x40, REQ_START_STREAM, 0, 0, None)

    print(f"Recording {args.seconds:.1f} s to {args.output}")
    print(f"Frame size={FRAME_LEN} bytes, frames/read={args.frames_per_read}")

    start = time.monotonic()
    last_report = start
    last_report_frames = 0
    frames = 0
    bad = 0
    gaps = 0
    flags_seen = 0
    last_seq = None
    latest_dropped = 0

    try:
        with open(args.output, "wb") as out:
            while (time.monotonic() - start) < args.seconds:
                batch = read_exact(dev, batch_len)
                out.write(batch)

                for offset in range(0, len(batch), FRAME_LEN):
                    header = parse_header(batch, offset)
                    if (
                        header["magic"] != MAGIC
                        or header["header_len"] != HEADER_LEN
                        or header["frame_len"] != FRAME_LEN
                    ):
                        bad += 1
                    else:
                        if last_seq is not None and header["slope_seq"] != ((last_seq + 1) & 0xFFFFFFFF):
                            gaps += 1
                        last_seq = header["slope_seq"]
                        flags_seen |= header["flags"]
                        latest_dropped = header["dropped_frames"]
                    frames += 1

                now = time.monotonic()
                elapsed = now - last_report
                if elapsed >= 1.0:
                    delta = frames - last_report_frames
                    mbps = (delta * FRAME_LEN * 8.0) / elapsed / 1_000_000.0
                    print(
                        f"{mbps:8.3f} Mbps frames={frames:8d} "
                        f"gaps={gaps:5d} bad={bad:4d} "
                        f"fw_dropped={latest_dropped:6d} flags=0x{flags_seen:04x}"
                    )
                    last_report = now
                    last_report_frames = frames
    finally:
        usb.util.dispose_resources(dev)

    size_mib = os.path.getsize(args.output) / (1024.0 * 1024.0)
    print(f"Done: {frames} frames, {size_mib:.1f} MiB written")
    print(f"Final health: gaps={gaps}, bad={bad}, fw_dropped={latest_dropped}, flags=0x{flags_seen:04x}")


if __name__ == "__main__":
    main()
