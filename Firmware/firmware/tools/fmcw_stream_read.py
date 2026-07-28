#!/usr/bin/env python3
"""Read FMCW radar frames from the custom USB HS bulk endpoint."""

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
FRAME_LEN = 20480
HEADER_LEN = 64
MAGIC = 0x52444152
REQ_START_STREAM = 0x30
HEADER = struct.Struct("<IHHIIIHHIIHHIIIIIII")
CONNECT_TIMEOUT_S = 30.0
FRAMES_PER_READ = 8


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
    driver_hint_printed = False

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
            if not driver_hint_printed:
                print(f"FMCW device is visible but libusb cannot open it: {exc}")
                print("This usually means Windows has not bound WinUSB/libusb to 1209:4158.")
                print("Check Device Manager or use Zadig to bind WinUSB to the FMCW device/interface.")
                driver_hint_printed = True
            time.sleep(1.0)
        except usb.core.USBError as exc:
            print(f"FMCW device appeared but configuration failed: {exc}. Retrying...")
            usb.util.dispose_resources(dev)
            time.sleep(0.5)

    list_usb_devices(backend)
    raise SystemExit("FMCW USB HS stream device not ready")


def read_exact(dev, size):
    chunks = []
    remaining = size
    while remaining:
        try:
            chunk = bytes(dev.read(EP_IN, remaining, timeout=2000))
        except usb.core.USBTimeoutError as exc:
            have = size - remaining
            raise TimeoutError(f"timed out waiting for frame data, have {have}/{size} bytes") from exc
        if not chunk:
            raise TimeoutError("USB read returned no data")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def sign_extend_12(code):
    code &= 0x0FFF
    return code - 0x1000 if code & 0x0800 else code


def main():
    backend = get_backend()
    if backend is None:
        raise SystemExit("No PyUSB/libusb backend found. Install: python -m pip install pyusb libusb-package")

    print(f"Using USB backend: {backend.__class__.__module__}.{backend.__class__.__name__}")

    dev = open_device(backend)
    dev.ctrl_transfer(0x40, REQ_START_STREAM, 0, 0, None)
    print(f"Reading FMCW frames, {FRAMES_PER_READ} frames per USB read. Press Ctrl+C to stop.")

    frames = 0
    bad_magic = 0
    seq_gaps = 0
    last_seq = None
    last_frames = 0
    last_time = time.monotonic()

    while True:
        try:
            batch = read_exact(dev, FRAME_LEN * FRAMES_PER_READ)
        except TimeoutError as exc:
            print(f"USB read timeout: {exc}")
            continue

        for frame_offset in range(0, len(batch), FRAME_LEN):
            header = HEADER.unpack_from(batch, frame_offset)
            magic, version, header_len, frame_len, slope_seq = header[:5]
            triangle_seq, slope_id, flags = header[5:8]
            sample_rate, samples_per_slope, adc_bits, words_per_slope = header[8:12]
            timestamp_us, muxout_count, dropped_frames, dcmi_risr, dma_lisr = header[12:17]

            if magic != MAGIC or header_len != HEADER_LEN or frame_len != FRAME_LEN:
                bad_magic += 1

            if last_seq is not None and slope_seq != ((last_seq + 1) & 0xFFFFFFFF):
                seq_gaps += 1
            last_seq = slope_seq

            word0 = struct.unpack_from("<I", batch, frame_offset + HEADER_LEN)[0]
            sample_a = sign_extend_12(word0)
            sample_b = sign_extend_12(word0 >> 16)
            frames += 1

            now = time.monotonic()
            elapsed = now - last_time
            if elapsed >= 1.0:
                delta_frames = frames - last_frames
                mbps = (delta_frames * FRAME_LEN * 8.0) / elapsed / 1_000_000.0
                print(
                    f"{mbps:8.3f} Mbps frames={frames:8d} seq={slope_seq:8d} "
                    f"slope={slope_id} tri={triangle_seq:8d} flags=0x{flags:04x} "
                    f"mux={muxout_count:8d} dropped={dropped_frames:6d} gaps={seq_gaps:4d} "
                    f"bad={bad_magic:4d} A={sample_a:+5d} B={sample_b:+5d} "
                    f"dcmi=0x{dcmi_risr:08x} dma=0x{dma_lisr:08x}"
                )
                if version != 1 or sample_rate != 10_000_000 or samples_per_slope != 10_000:
                    print(
                        f"  header check: version={version} sample_rate={sample_rate} "
                        f"samples={samples_per_slope} bits={adc_bits} words={words_per_slope} "
                        f"timestamp_us={timestamp_us}"
                    )
                last_frames = frames
                last_time = now


if __name__ == "__main__":
    main()
