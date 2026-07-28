import numpy as np
import time
from multiprocessing import Process, Queue
from queue import Full
import matplotlib.pyplot as plt

# USB stream used by the STM32 FMCW firmware.
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
C = 3e8                   # Speed of light
FC = 5.8e9                # Carrier frequency (5.8 GHz)
BW = 200e6                # Bandwidth (200 MHz)
TC = 1e-3                 # Sweep time (1 ms UP, 1 ms DOWN)
IDLE_TIME = 0.1e-3        # Hardware flyback/reset time
CHIRP_PERIOD = (2 * TC) + IDLE_TIME # Total cycle time (2.1 ms)
LAMBDA = C / FC           # Wavelength

# ---------------------------------------------------------
# HARDWARE & DATA CONSTANTS (10 MHz Triangular)
# ---------------------------------------------------------
SAMPLING_FREQ = 10e6
SAMPLES_PER_CHIRP = int(SAMPLING_FREQ * TC)  # 10,000 samples per sweep
BYTES_PER_CHIRP = SAMPLES_PER_CHIRP * 2      # 20,000 bytes per sweep
BYTES_PER_CYCLE = BYTES_PER_CHIRP * 2        # 40,000 bytes total (UP + DOWN)
CHIRPS_PER_FRAME = 128                       # Number of triangular cycles per batch

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
FRAMES_PER_READ = 4

# Firmware header format from fmcw_stream.c / fmcw_stream.h.
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
        raise RuntimeError("No PyUSB/libusb backend found. Install pyusb and libusb-package.")

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
    """Decode one STM32 USB frame into 10,000 offset-binary uint16 samples.

    Triangular.py originally consumed unsigned 16-bit samples and removed the
    mean before FFT. The LTC1420 stream is signed 12-bit two's-complement, so
    this producer converts signed samples to offset-binary uint16. That keeps
    the original consumer and DSP logic unchanged.
    """
    words = np.frombuffer(frame, dtype="<u4", count=PAYLOAD_WORDS, offset=HEADER_LEN)
    lo = (words & 0x0FFF).astype(np.int16)
    hi = ((words >> 16) & 0x0FFF).astype(np.int16)

    lo = ((lo ^ 0x0800) - 0x0800).astype(np.int16)
    hi = ((hi ^ 0x0800) - 0x0800).astype(np.int16)

    samples = np.empty(SAMPLES_PER_CHIRP, dtype=np.uint16)
    samples[0::2] = (lo.astype(np.int32) + 2048).astype(np.uint16)
    samples[1::2] = (hi.astype(np.int32) + 2048).astype(np.uint16)
    return samples


def data_producer(queue, port=None, baud=None):
    """Thread 1: Reads STM32 USB frames and pushes 40,000-byte UP+DOWN cycles."""

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
                    print("[PRODUCER] Header sample count does not match Triangular.py constants.")
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
    """Helper function to run the 2D FFT pipeline on a given matrix"""
    # 1. RANGE FFT (Horizontal)
    matrix_windowed_1d = matrix * range_window
    range_fft = np.fft.fft(matrix_windowed_1d, axis=1)
    range_fft = range_fft[:, :SAMPLES_PER_CHIRP // 2]
    
    # 2. DOPPLER FFT (Vertical)
    matrix_windowed_2d = range_fft * doppler_window
    doppler_fft = np.fft.fft(matrix_windowed_2d, axis=0)
    
    # 3. Shift Zero-Velocity to the center
    range_doppler_map = np.fft.fftshift(doppler_fft, axes=0)
    
    return np.abs(range_doppler_map)


def data_consumer(queue):
    """Thread 2: Splits data, performs Dual 2D FFT, and averages Range."""
    
    # Pre-compute windows
    range_window = np.hanning(SAMPLES_PER_CHIRP)
    doppler_window = np.hanning(CHIRPS_PER_FRAME).reshape(-1, 1)
    
    # ----- SETUP THE VISUAL MAP ----------------
    plt.ion() 
    fig, ax = plt.subplots(figsize=(8, 6))
    
    range_step = (SAMPLING_FREQ / SAMPLES_PER_CHIRP) * (C * TC / (2 * BW))
    max_range = (SAMPLES_PER_CHIRP // 2) * range_step
    vel_step = ((1 / CHIRP_PERIOD) / CHIRPS_PER_FRAME) * (LAMBDA / 2)
    max_vel = (CHIRPS_PER_FRAME // 2) * vel_step
    
    plot_extent = [0, max_range, -max_vel, max_vel]
    empty_data = np.zeros((CHIRPS_PER_FRAME, SAMPLES_PER_CHIRP // 2))
    
    img = ax.imshow(empty_data, extent=plot_extent, aspect='auto', origin='lower', 
                    cmap='turbo', interpolation='gaussian')
    
    ax.set_title("Live Range-Doppler Heatmap (UP Ramp view)")
    ax.set_xlabel("Range (Meters)")
    ax.set_ylabel("Velocity (m/s)")
    ax.set_xlim(0, 100) # Limit X-axis to 100 meters
    fig.colorbar(img, ax=ax, label="Magnitude (dB)")
    plt.show()

    while True:
        up_frame_data = []
        down_frame_data = []
        
        # 1. Collect enough cycles to build the Matrices
        while len(up_frame_data) < CHIRPS_PER_FRAME:
            raw_bytes = queue.get()
            signal = np.frombuffer(raw_bytes, dtype=np.uint16).astype(np.float32)
            
            # SPLIT THE DATA (First half is UP, Second half is DOWN)
            up_sig = signal[:SAMPLES_PER_CHIRP]
            down_sig = signal[SAMPLES_PER_CHIRP:]
            
            # Remove DC offsets
            up_sig = up_sig - np.mean(up_sig)
            down_sig = down_sig - np.mean(down_sig)
            
            up_frame_data.append(up_sig)
            down_frame_data.append(down_sig)
            
        # 2. Build the Matrices
        up_matrix = np.vstack(up_frame_data)
        down_matrix = np.vstack(down_frame_data)
        
        # 3. Process Dual 2D FFTs
        rd_mag_up = process_2d_fft(up_matrix, range_window, doppler_window)
        rd_mag_down = process_2d_fft(down_matrix, range_window, doppler_window)
        
        # ----- VISUALIZATION (Plotting the UP map) -------------
        rd_db = 20 * np.log10(rd_mag_up + 1e-10) 
        img.set_data(rd_db)                         
        img.set_clim(vmin=np.max(rd_db) - 60, vmax=np.max(rd_db)) 
        fig.canvas.flush_events()                   
        
        # --- TRUE DETECTION LOGIC (Triangular Averaging) ---
        # Find peak in UP map
        target_idx_up = np.unravel_index(np.argmax(rd_mag_up), rd_mag_up.shape)
        doppler_bin = target_idx_up[0] # Velocity is identical in both maps
        range_bin_up = target_idx_up[1]
        
        # Find peak in DOWN map
        target_idx_down = np.unravel_index(np.argmax(rd_mag_down), rd_mag_down.shape)
        range_bin_down = target_idx_down[1]
        
        # Calculate individual frequencies
        freq_res = SAMPLING_FREQ / SAMPLES_PER_CHIRP
        f_up = range_bin_up * freq_res
        f_down = range_bin_down * freq_res
        
        # AVERAGE THE FREQUENCIES (Cancels out Range-Doppler coupling)
        f_true = (f_up + f_down) / 2
        
        # Calculate Final True Distance
        target_range = f_true * (C * TC / (2 * BW))
        
        # Calculate Final Velocity (from the shared Doppler bin)
        doppler_shifted_idx = doppler_bin - (CHIRPS_PER_FRAME // 2)
        prf = 1 / CHIRP_PERIOD 
        doppler_freq = doppler_shifted_idx * (prf / CHIRPS_PER_FRAME)
        target_velocity = doppler_freq * (LAMBDA / 2)
        
        print(f"Target -> Range: {target_range:.2f} m | Velocity: {target_velocity:.2f} m/s | (f_up:{f_up/1000:.1f}kHz, f_down:{f_down/1000:.1f}kHz)")

if __name__ == '__main__':
    data_queue = Queue(maxsize=128) # Kept small to prevent memory backup
    producer_process = Process(target=data_producer, args=(data_queue,))
    producer_process.start()
    
    try:
        data_consumer(data_queue)
    except KeyboardInterrupt:
        producer_process.terminate()
