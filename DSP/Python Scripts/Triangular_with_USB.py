#!/usr/bin/env python3
import struct
import time
import numpy as np
import matplotlib.pyplot as plt
from multiprocessing import Process, Queue
from queue import Full

# --- USB Imports ---
import usb.core
import usb.util
from usb.backend import libusb1
try:
    import libusb_package
except ImportError:
    libusb_package = None

# ---------------------------------------------------------
# PHYSICAL & RADAR PARAMETERS (Updated from screenshots)
# ---------------------------------------------------------
C = 3e8                    
FC = 5.800135803e9         # Center frequency
BW = 200271606.0           # One-way bandwidth (200.271606 MHz)
TC = 1e-3                  # Slope duration (1.000 ms)
IDLE_TIME = 0.0            # Assuming continuous slopes based on 2.000ms triangle period
CHIRP_PERIOD = 2 * TC      # Total cycle time (2.0 ms)
LAMBDA = C / FC            

# ---------------------------------------------------------
# HARDWARE & DATA CONSTANTS 
# ---------------------------------------------------------
SAMPLING_FREQ = 10e6
SAMPLES_PER_CHIRP = int(SAMPLING_FREQ * TC)  # 10,000 samples
CHIRPS_PER_FRAME = 128                       # Number of triangular cycles per batch

# --- USB Constants ---
VID = 0x1209
PID = 0x4158
EP_IN = 0x81
FRAME_LEN = 20480
HEADER_LEN = 64
MAGIC = 0x52444152
REQ_START_STREAM = 0x30
HEADER = struct.Struct("<IHHIIIHHIIHHIIIIIII")
FRAMES_PER_READ = 8


# ---------------------------------------------------------
# USB HARDWARE HELPERS
# ---------------------------------------------------------
def get_backend():
    if libusb_package is None:
        return libusb1.get_backend()
    return libusb1.get_backend(find_library=libusb_package.find_library)

def open_device(backend):
    print("[PRODUCER] Searching for FMCW USB HS stream device...")
    while True:
        dev = usb.core.find(idVendor=VID, idProduct=PID, backend=backend)
        if dev:
            try:
                dev.set_configuration()
                print(f"[PRODUCER] Radar PCB Connected! VID:0x{VID:04x} PID:0x{PID:04x}")
                return dev
            except usb.core.USBError as exc:
                print(f"[PRODUCER] Found device but configuration failed: {exc}. Retrying...")
                usb.util.dispose_resources(dev)
        time.sleep(1.0)

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

def extract_samples(payload_bytes):
    """Fast vectorized extraction of two 12-bit samples from 32-bit words."""
    words = np.frombuffer(payload_bytes, dtype=np.uint32)
    
    # Extract bottom 12 bits and top 12 bits
    s_a = (words & 0x0FFF).astype(np.int16)
    s_b = ((words >> 16) & 0x0FFF).astype(np.int16)
    
    # Fast 12-bit two's complement sign extension via arithmetic shifts
    s_a = np.right_shift(np.left_shift(s_a, 4), 4).astype(np.float32)
    s_b = np.right_shift(np.left_shift(s_b, 4), 4).astype(np.float32)
    
    # Interleave arrays (sample_a, sample_b, sample_a, sample_b...)
    samples = np.empty(10000, dtype=np.float32)
    samples[0::2] = s_a
    samples[1::2] = s_b
    return samples


# ---------------------------------------------------------
# THREAD 1: DATA PRODUCER (USB Bulk Stream)
# ---------------------------------------------------------
def data_producer(queue):
    backend = get_backend()
    dev = open_device(backend)
    
    # Send start stream command
    dev.ctrl_transfer(0x40, REQ_START_STREAM, 0, 0, None)
    print("[PRODUCER] Stream started. Reading frames...")

    current_up_slope = None

    while True:
        try:
            batch = read_exact(dev, FRAME_LEN * FRAMES_PER_READ)
        except TimeoutError as exc:
            print(f"[PRODUCER] USB warning: {exc}")
            continue
        except Exception as e:
            print(f"[PRODUCER] Fatal USB Error: {e}")
            break

        # Parse the frames chunk by chunk
        for offset in range(0, len(batch), FRAME_LEN):
            frame = batch[offset : offset + FRAME_LEN]
            header = HEADER.unpack_from(frame, 0)
            
            magic = header[0]
            slope_id = header[6] 
            
            if magic != MAGIC:
                continue # Bad frame, skip

            # Extract 20,000 byte ADC payload (Bytes 64 to 20,064)
            payload = frame[HEADER_LEN : HEADER_LEN + 20000]
            samples = extract_samples(payload)

            # The docs say slope_id is just an alternating 0/1 phase. 
            # We assume 0 = UP, 1 = DOWN. 
            if slope_id == 0:
                current_up_slope = samples
            elif slope_id == 1 and current_up_slope is not None:
                # We have a matching UP / DOWN pair! Push to DSP queue.
                try:
                    queue.put_nowait((current_up_slope, samples))
                except Full:
                    pass # Drop frame if DSP is falling behind
                current_up_slope = None # Reset for next pair


# ---------------------------------------------------------
# THREAD 2: DSP CONSUMER (2D FFT Processing)
# ---------------------------------------------------------
def process_2d_fft(matrix, range_window, doppler_window):
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
    ax.set_xlim(0, 100)
    fig.colorbar(img, ax=ax, label="Magnitude (dB)")
    plt.show()

    while True:
        up_frame_data = []
        down_frame_data = []
        
        # 1. Collect enough cycles to build the Matrices
        while len(up_frame_data) < CHIRPS_PER_FRAME:
            # We now receive a cleanly packaged tuple from the USB thread
            up_sig, down_sig = queue.get()
            
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
        target_idx_up = np.unravel_index(np.argmax(rd_mag_up), rd_mag_up.shape)
        doppler_bin = target_idx_up[0] 
        range_bin_up = target_idx_up[1]
        
        target_idx_down = np.unravel_index(np.argmax(rd_mag_down), rd_mag_down.shape)
        range_bin_down = target_idx_down[1]
        
        freq_res = SAMPLING_FREQ / SAMPLES_PER_CHIRP
        f_up = range_bin_up * freq_res
        f_down = range_bin_down * freq_res
        
        f_true = (f_up + f_down) / 2
        target_range = f_true * (C * TC / (2 * BW))
        
        doppler_shifted_idx = doppler_bin - (CHIRPS_PER_FRAME // 2)
        prf = 1 / CHIRP_PERIOD 
        doppler_freq = doppler_shifted_idx * (prf / CHIRPS_PER_FRAME)
        target_velocity = doppler_freq * (LAMBDA / 2)
        
        print(f"Target -> Range: {target_range:.2f} m | Velocity: {target_velocity:.2f} m/s | "
              f"(f_up:{f_up/1000:.1f}kHz, f_down:{f_down/1000:.1f}kHz)")


if __name__ == '__main__':
    # Queue passes numpy array tuples (UP, DOWN)
    data_queue = Queue(maxsize=32) 
    
    producer_process = Process(target=data_producer, args=(data_queue,))
    producer_process.start()
    
    try:
        data_consumer(data_queue)
    except KeyboardInterrupt:
        print("\nShutting down...")
        producer_process.terminate()