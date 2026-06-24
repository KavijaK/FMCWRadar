#!/usr/bin/env python3
"""Read and verify the FMCW USB HS debug stream.

Install dependencies on the PC side:
  python -m pip install pyusb libusb-package

The firmware exposes a vendor-specific bulk IN endpoint at 0x81. The byte
pattern is 00, 01, 02, ... FF, 00, ...
"""

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
READ_SIZE = 16384


def get_backend():
    if libusb_package is None:
        return libusb1.get_backend()
    return libusb1.get_backend(find_library=libusb_package.find_library)


def list_usb_devices(backend) -> None:
    print("Visible USB devices:")
    found_any = False
    for dev in usb.core.find(find_all=True, backend=backend):
        found_any = True
        print(f"  {dev.idVendor:04x}:{dev.idProduct:04x}")

    if not found_any:
        print("  No USB devices visible through PyUSB/libusb.")


def main() -> None:
    backend = get_backend()
    if backend is None:
        raise SystemExit("No PyUSB/libusb backend found. Install: python -m pip install pyusb libusb-package")

    print(f"Using USB backend: {backend.__class__.__module__}.{backend.__class__.__name__}")

    dev = usb.core.find(idVendor=VID, idProduct=PID, backend=backend)
    if dev is None:
        list_usb_devices(backend)
        raise SystemExit("FMCW USB HS debug device not found")

    dev.set_configuration()

    expected = 0
    errors = 0
    total_bytes = 0
    last_bytes = 0
    start_time = time.monotonic()
    last_time = start_time

    print("Reading endpoint 0x81. Press Ctrl+C to stop.")

    while True:
        data = dev.read(EP_IN, READ_SIZE, timeout=1000)

        for value in data:
            if value != expected:
                errors += 1
                expected = (int(value) + 1) & 0xFF
            else:
                expected = (expected + 1) & 0xFF

        total_bytes += len(data)
        now = time.monotonic()
        elapsed = now - last_time

        if elapsed >= 1.0:
            delta = total_bytes - last_bytes
            mbps = (delta * 8.0) / elapsed / 1_000_000.0
            total_mib = total_bytes / (1024.0 * 1024.0)
            print(f"{mbps:8.3f} Mbps  total={total_mib:10.1f} MiB  errors={errors}")
            last_bytes = total_bytes
            last_time = now


if __name__ == "__main__":
    main()
