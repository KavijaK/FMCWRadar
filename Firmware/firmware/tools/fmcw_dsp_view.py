#!/usr/bin/env python3
"""Live FMCW stream viewer.

This is a debug/DSP bring-up tool, not the final radar processing chain. It
keeps draining USB frames, decodes a subset of chirps, and displays:

  * time-domain ADC samples
  * one-chirp range FFT
  * scrolling range-time waterfall
"""

import argparse
import struct
import time
from collections import deque

import numpy as np
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
PAYLOAD_WORDS = 5000
SAMPLES_PER_SLOPE = 10000
MAGIC = 0x52444152
HEADER = struct.Struct("<IHHIIIHHIIHHIIIIIII")
CONNECT_TIMEOUT_S = 30.0

LIGHT_SPEED_M_S = 299_792_458.0
DEFAULT_BW_HZ = 200_271_606.0
DEFAULT_CHIRP_S = 1.0e-3


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
            if not driver_hint_printed:
                print(f"FMCW device is visible but libusb cannot open it: {exc}")
                print("This usually means Windows has not bound WinUSB/libusb to 1209:4158.")
                print("Check Device Manager or use Zadig to bind WinUSB to the FMCW device/interface.")
                driver_hint_printed = True
            time.sleep(1.0)
        except usb.core.USBError as exc:
            if getattr(exc, "errno", None) == 13 and not access_hint_printed:
                print("FMCW device is visible, but Windows denied access to it.")
                print("Close any other fmcw_stream_read.py/fmcw_dsp_view.py process, then unplug/replug USB.")
                print("If it still fails, rebind the FMCW interface to WinUSB with Zadig.")
                access_hint_printed = True
            else:
                print(f"FMCW device appeared but configuration failed: {exc}. Retrying...")
            usb.util.dispose_resources(dev)
            time.sleep(0.5)

    list_usb_devices(backend)
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


def sign_extend_12(raw):
    raw = raw.astype(np.int16, copy=False)
    return ((raw ^ 0x0800) - 0x0800).astype(np.int16, copy=False)


def decode_samples(buf, frame_offset):
    words = np.frombuffer(
        buf,
        dtype="<u4",
        count=PAYLOAD_WORDS,
        offset=frame_offset + HEADER_LEN,
    )
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


def range_fft_db(samples, window, nfft):
    x = samples.astype(np.float32)
    x -= np.mean(x)
    spectrum = np.fft.rfft(x * window, n=nfft)
    mag = np.abs(spectrum)
    return 20.0 * np.log10(mag + 1.0e-6)


def build_argparser():
    parser = argparse.ArgumentParser(description="Visualize live FMCW USB frames.")
    parser.add_argument("--frames-per-read", type=int, default=8)
    parser.add_argument("--process-every", type=int, default=10,
                        help="Process one out of N valid frames for plotting.")
    parser.add_argument("--slope-id", choices=["0", "1", "all"], default="0",
                        help="Which alternating slope phase to visualize.")
    parser.add_argument("--history", type=int, default=160,
                        help="Waterfall rows to keep.")
    parser.add_argument("--max-range-m", type=float, default=200.0)
    parser.add_argument("--nfft", type=int, default=16384)
    parser.add_argument("--bandwidth-hz", type=float, default=DEFAULT_BW_HZ)
    parser.add_argument("--chirp-s", type=float, default=DEFAULT_CHIRP_S)
    parser.add_argument("--time-samples", type=int, default=1200)
    parser.add_argument("--save-raw",
                        help="Optional raw frame output file. This can increase PC load.")
    return parser


def main():
    args = build_argparser().parse_args()

    import matplotlib.pyplot as plt

    backend = get_backend()
    if backend is None:
        raise SystemExit("Install dependencies: python -m pip install pyusb libusb-package numpy matplotlib")

    dev = open_device(backend)
    dev.ctrl_transfer(0x40, REQ_START_STREAM, 0, 0, None)

    raw_file = open(args.save_raw, "ab") if args.save_raw else None
    window = np.hanning(SAMPLES_PER_SLOPE).astype(np.float32)
    range_m = make_range_axis(
        sample_rate_hz=10_000_000,
        nfft=args.nfft,
        bandwidth_hz=args.bandwidth_hz,
        chirp_s=args.chirp_s,
    )
    range_mask = range_m <= args.max_range_m
    range_view = range_m[range_mask]
    waterfall = deque(maxlen=args.history)

    fig, (ax_time, ax_fft, ax_waterfall) = plt.subplots(3, 1, figsize=(11, 8))
    fig.canvas.manager.set_window_title("FMCW Live DSP View")

    time_x = np.arange(args.time_samples)
    time_line, = ax_time.plot(time_x, np.zeros(args.time_samples))
    ax_time.set_title("ADC Samples")
    ax_time.set_xlabel("sample index")
    ax_time.set_ylabel("mV")
    ax_time.set_ylim(-2100, 2100)

    fft_line, = ax_fft.plot(range_view, np.zeros_like(range_view))
    ax_fft.set_title("Range FFT")
    ax_fft.set_xlabel("range (m)")
    ax_fft.set_ylabel("dB")
    ax_fft.set_xlim(0, args.max_range_m)
    ax_fft.set_ylim(20, 110)

    image = ax_waterfall.imshow(
        np.zeros((args.history, len(range_view))),
        aspect="auto",
        origin="lower",
        extent=[0, args.max_range_m, 0, args.history],
        vmin=20,
        vmax=110,
    )
    ax_waterfall.set_title("Range-Time Waterfall")
    ax_waterfall.set_xlabel("range (m)")
    ax_waterfall.set_ylabel("recent chirps")
    fig.colorbar(image, ax=ax_waterfall, label="dB")

    plt.tight_layout()
    plt.show(block=False)

    frames = 0
    plotted = 0
    bad = 0
    gaps = 0
    last_seq = None
    last_report_time = time.monotonic()
    last_report_frames = 0

    print("Live viewer started. Close the plot window or press Ctrl+C to stop.")
    print("For lossless capture, use the simpler reader/recorder path; plotting is intentionally decimated.")

    try:
        while plt.fignum_exists(fig.number):
            batch = read_exact(dev, FRAME_LEN * args.frames_per_read)
            if raw_file is not None:
                raw_file.write(batch)

            for frame_offset in range(0, len(batch), FRAME_LEN):
                header = parse_header(batch, frame_offset)
                frames += 1

                if (
                    header["magic"] != MAGIC
                    or header["header_len"] != HEADER_LEN
                    or header["frame_len"] != FRAME_LEN
                ):
                    bad += 1
                    continue

                if last_seq is not None and header["slope_seq"] != ((last_seq + 1) & 0xFFFFFFFF):
                    gaps += 1
                last_seq = header["slope_seq"]

                if args.slope_id != "all" and str(header["slope_id"]) != args.slope_id:
                    continue
                if (frames % args.process_every) != 0:
                    continue

                samples = decode_samples(batch, frame_offset)
                fft_db = range_fft_db(samples, window, args.nfft)[range_mask]
                waterfall.append(fft_db)
                plotted += 1

                shown = samples[: args.time_samples]
                time_line.set_ydata(shown)
                fft_line.set_ydata(fft_db)

                wf = np.zeros((args.history, len(range_view)), dtype=np.float32)
                if waterfall:
                    wf[-len(waterfall):, :] = np.vstack(waterfall)
                image.set_data(wf)

                peak_bin = int(np.argmax(fft_db))
                peak_range = range_view[peak_bin]
                peak_db = fft_db[peak_bin]
                fig.suptitle(
                    f"seq={header['slope_seq']} slope={header['slope_id']} "
                    f"drop={header['dropped_frames']} flags=0x{header['flags']:04x} "
                    f"peak={peak_range:.2f} m / {peak_db:.1f} dB"
                )
                fig.canvas.draw_idle()
                plt.pause(0.001)

            now = time.monotonic()
            elapsed = now - last_report_time
            if elapsed >= 1.0:
                fps = (frames - last_report_frames) / elapsed
                print(
                    f"rx={fps:7.1f} frames/s plotted={plotted:6d} "
                    f"gaps={gaps:5d} bad={bad:4d}"
                )
                last_report_time = now
                last_report_frames = frames
    finally:
        if raw_file is not None:
            raw_file.close()
        usb.util.dispose_resources(dev)


if __name__ == "__main__":
    main()
