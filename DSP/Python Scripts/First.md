import serial
import numpy as np
import time
from multiprocessing import Process, Queue
from queue import Full
import matplotlib.pyplot as plt




# physical parameters
C = 3e8                   # Speed of light
FC = 5.8e9                # Carrier frequency (5.8 GHz)
BW = 200e6                # Bandwidth (200 MHz)
TC = 1e-3                 # Chirp time (1 ms)
IDLE_TIME = 0.1e-3
CHIRP_PERIOD = TC + IDLE_TIME
LAMBDA = C / FC           # Wavelength



# Hardware & Radar Constants
SAMPLING_FREQ = 5e6
SAMPLES_PER_CHIRP = int(SAMPLING_FREQ * TC)
CHIRPS_PER_FRAME = 256
BYTES_PER_CHIRP = SAMPLES_PER_CHIRP * 2       # 16 bit samples






def data_producer(queue, port, baud):
    """Thread 1: Hunts for the Sync Header and pushes complete single chirps."""
    
    
    # 1. Define the connection loop inside the producer
    def connect_to_radar():
        while True:
            try:
                # Timeout must stay 0.1 for the sliding window buffer to work smoothly
                s = serial.Serial(port, baud, timeout=0.1) 
                print("\n[PRODUCER] Radar Hardware Detected and Connected.")
                return s
            except serial.SerialException:
                print("[PRODUCER] Waiting for Radar PCB to be plugged in...")
                time.sleep(2)
    
    # 2. Initial Connection
    ser = connect_to_radar()
    
    
    SYNC_WORD = b'\xff\xff\xff\xff' 
    buffer = bytearray()
    
    
    while True:
        try:
            # Try to read incoming bytes
            incoming = ser.read(max(1, ser.in_waiting))
            if incoming:
                buffer.extend(incoming)
            
            # --- The rest of your Sync Hunter logic remains exactly the same ---
            sync_index = buffer.find(SYNC_WORD)
            if sync_index != -1:
                target_length = sync_index + len(SYNC_WORD) + BYTES_PER_CHIRP
                if len(buffer) >= target_length:
                    start_data = sync_index + len(SYNC_WORD)
                    end_data = start_data + BYTES_PER_CHIRP
                    pure_data = buffer[start_data:end_data]
                    
                    try:
                        queue.put_nowait(pure_data)
                    except Full:
                        pass
                    
                    buffer = buffer[end_data:]
            else:
                if len(buffer) > BYTES_PER_CHIRP * 3:
                    buffer = buffer[-BYTES_PER_CHIRP:] 
                    
        # 3. Handle Mid-Operation Disconnections
        except serial.SerialException:
            print("\n[PRODUCER] ERROR: Connection lost! (Cable unplugged?)")
            ser.close()             # Close the broken port
            buffer.clear()          # Delete partial/corrupt data
            ser = connect_to_radar() # Block and wait until it's plugged back in
            
        except KeyboardInterrupt:
            break
            
    if ser.is_open:
        ser.close()
        





def data_consumer(queue):
    """Thread 2: Stacks chirps into a Matrix and performs the 2D FFT."""
    
    # Pre-compute windows for efficiency
    range_window = np.hanning(SAMPLES_PER_CHIRP)
    doppler_window = np.hanning(CHIRPS_PER_FRAME)
    
    # Reshape doppler window to column vector for matrix multiplication
    doppler_window = doppler_window.reshape(-1, 1) 
    
    
    # ----- NEW CODE: SETUP THE VISUAL MAP (Run once before the loop) ----------------
    plt.ion() 
    fig, ax = plt.subplots(figsize=(8, 6))
    
    range_step = (SAMPLING_FREQ / SAMPLES_PER_CHIRP) * (C * TC / (2 * BW))
    max_range = (SAMPLES_PER_CHIRP // 2) * range_step
    vel_step = ((1 / CHIRP_PERIOD) / CHIRPS_PER_FRAME) * (LAMBDA / 2)
    max_vel = (CHIRPS_PER_FRAME // 2) * vel_step
    
    plot_extent = [0, max_range, -max_vel, max_vel]
    empty_data = np.zeros((CHIRPS_PER_FRAME, SAMPLES_PER_CHIRP // 2))
    
    # --- CHANGED: 'turbo' colormap and 'gaussian' blending for realistic radar look ---
    img = ax.imshow(empty_data, extent=plot_extent, aspect='auto', origin='lower', 
                    cmap='turbo', interpolation='gaussian')
    
    ax.set_title("Live Range-Doppler Heatmap")
    ax.set_xlabel("Range (Meters)")
    ax.set_ylabel("Velocity (m/s)")
    
    # --- LIMIT THE X-AXIS TO 100 METERS ---
    ax.set_xlim(0, 100) 
    
    fig.colorbar(img, ax=ax, label="Magnitude (dB)")
    plt.show()
    # -----------------------------------------------------------------------
    
    
    
    

    while True:
        frame_data = []
        
        # 1. Collect enough chirps to build one Frame (Matrix)
        while len(frame_data) < CHIRPS_PER_FRAME:
            raw_bytes = queue.get()
            # signal = np.frombuffer(raw_bytes, dtype=np.uint16)
            signal = np.frombuffer(raw_bytes, dtype=np.uint16).astype(np.float32)
            
            # Remove DC offset immediately
            signal = signal - np.mean(signal)
            frame_data.append(signal)
            
        # 2. Convert list of arrays into a 2D Numpy Matrix # Shape: (128 rows, 500 columns)
        radar_matrix = np.vstack(frame_data) 
        
        # 3. RANGE FFT (Horizontal) # Apply window to every row, then FFT along the rows (axis=1)
        matrix_windowed_1d = radar_matrix * range_window
        range_fft = np.fft.fft(matrix_windowed_1d, axis=1)
        
        # Take only the positive frequencies (first half of columns)
        range_fft = range_fft[:, :SAMPLES_PER_CHIRP // 2]
        
        # 4. DOPPLER FFT (Vertical) # Apply window to every column, then FFT down the columns (axis=0)
        matrix_windowed_2d = range_fft * doppler_window
        doppler_fft = np.fft.fft(matrix_windowed_2d, axis=0)
        
        # 5. Shift Zero-Velocity to the center
        # By default, FFT puts 0 Hz at index 0. For velocity, we want # negative speeds on the left, 0 in the middle, positive on the right.
        range_doppler_map = np.fft.fftshift(doppler_fft, axes=0)
        
        # 6. Extract Magnitude for Visualization/Detection
        rd_magnitude = np.abs(range_doppler_map)
        
        
        
        
        # -----NEW CODE: PUSH DATA TO THE MAP (Runs every frame) -------------
        rd_db = 20 * np.log10(rd_magnitude + 1e-10) # Convert to decibels
        img.set_data(rd_db)                         # Push to graph
        img.set_clim(vmin=np.max(rd_db) - 60, vmax=np.max(rd_db)) # Adjust colors
        fig.canvas.flush_events()                   # Redraw screen
        #----------------------------------------------------------------------
        
        
        
        
        
        
        # --- DETECTION LOGIC ---
        # Find the coordinates (row, col) of the brightest pixel in the 2D map
        target_idx = np.unravel_index(np.argmax(rd_magnitude), rd_magnitude.shape)
        doppler_bin = target_idx[0]
        range_bin = target_idx[1]
        
        # Convert bins to actual physical units
        range_res = C / (2 * BW)
        range_freq = range_bin * (SAMPLING_FREQ / SAMPLES_PER_CHIRP)
        target_range = range_freq * (C * TC / (2 * BW))
        
        # Velocity = (Lambda / 2) * (Doppler_Freq)  # Doppler bin ranges from -128 to +128 (because of fftshift)
        doppler_shifted_idx = doppler_bin - (CHIRPS_PER_FRAME // 2)
        prf = 1 / CHIRP_PERIOD              # Pulse Repetition Frequency
        doppler_freq = doppler_shifted_idx * (prf / CHIRPS_PER_FRAME)
        target_velocity = doppler_freq * (LAMBDA / 2)
        
        print(f"Target Detected -> Range: {target_range:.2f} m | Velocity: {target_velocity:.2f} m/s")

if __name__ == '__main__':
    data_queue = Queue(maxsize=512) 
    producer_process = Process(target=data_producer, args=(data_queue, 'COM3', 115200))
    producer_process.start()
    
    try:
        data_consumer(data_queue)
    except KeyboardInterrupt:
        producer_process.terminate()
        
        