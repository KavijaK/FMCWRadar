import serial
import numpy as np
import time
from multiprocessing import Process, Queue
from queue import Full
import matplotlib.pyplot as plt

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

def data_producer(queue, port, baud):
    """Thread 1: Hunts for the Sync Header and pushes 40,000-byte cycle payloads."""
    
    def connect_to_radar():
        while True:
            try:
                s = serial.Serial(port, baud, timeout=0.1) 
                print("\n[PRODUCER] Radar Hardware Detected and Connected.")
                return s
            except serial.SerialException:
                print("[PRODUCER] Waiting for Radar PCB to be plugged in...")
                time.sleep(2)
    
    ser = connect_to_radar()
    SYNC_WORD = b'\xff\xff\xff\xff' 
    buffer = bytearray()
    
    while True:
        try:
            incoming = ser.read(max(1, ser.in_waiting))
            if incoming:
                buffer.extend(incoming)
            
            sync_index = buffer.find(SYNC_WORD)
            if sync_index != -1:
                # We expect the header + the FULL triangular cycle payload
                target_length = sync_index + len(SYNC_WORD) + BYTES_PER_CYCLE
                
                if len(buffer) >= target_length:
                    start_data = sync_index + len(SYNC_WORD)
                    end_data = start_data + BYTES_PER_CYCLE
                    pure_data = buffer[start_data:end_data]
                    
                    try:
                        queue.put_nowait(pure_data)
                    except Full:
                        pass
                    
                    buffer = buffer[end_data:]
            else:
                # Prevent buffer from growing infinitely if no sync word is found
                if len(buffer) > BYTES_PER_CYCLE * 3:
                    buffer = buffer[-BYTES_PER_CYCLE:] 
                    
        except serial.SerialException:
            print("\n[PRODUCER] ERROR: Connection lost! (Cable unplugged?)")
            ser.close()             
            buffer.clear()          
            ser = connect_to_radar() 
            
        except KeyboardInterrupt:
            break
            
    if ser.is_open:
        ser.close()

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
    producer_process = Process(target=data_producer, args=(data_queue, 'COM3', 115200))
    producer_process.start()
    
    try:
        data_consumer(data_queue)
    except KeyboardInterrupt:
        producer_process.terminate()