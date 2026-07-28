import numpy as np
import time
from multiprocessing import Process, Queue
from queue import Full

# Calculate physical parameters for a 5.8GHz radar
C = 3e8                   # Speed of light
FC = 5.8e9                # Carrier frequency (5.8 GHz)
BW = 200e6                # Bandwidth (200 MHz)
TC = 1e-3                 # Chirp time (1 ms)
IDLE_TIME = 0.1e-3
CHIRP_PERIOD = TC + IDLE_TIME
LAMBDA = C / FC           # Wavelength

# Hardware & Radar Constants
SAMPLING_FREQ = 5e6       # Back to your original 5 MHz
SAMPLES_PER_CHIRP = int(SAMPLING_FREQ * TC)
CHIRPS_PER_FRAME = 256
BYTES_PER_CHIRP = SAMPLES_PER_CHIRP * 2 # 16-bit samples

# --- SIMULATION SETTINGS ---
# Change these values to test your FFT detection logic!
TARGET_RANGE = 25.0       # Test Distance in meters
TARGET_VELOCITY = 15.5    # Test Velocity in m/s

def generate_simulated_frame(target_range, target_velocity):
    """Pre-calculates a mathematically perfect radar frame of raw bytes."""
    # Convert range to beat frequency: fb = (2 * BW * R) / (c * Tc)
    f_beat = (2 * BW * target_range) / (C * TC)
    print(f"[SIMULATOR] Mathematical Beat Frequency for {target_range}m is {f_beat:.2f} Hz")
    
    t = np.arange(SAMPLES_PER_CHIRP) / SAMPLING_FREQ
    frame_bytes = bytearray()
    SYNC_WORD = b'\xff\xff\xff\xff'
    
    for m in range(CHIRPS_PER_FRAME):
        # Phase shift due to Doppler
        doppler_phase = 4 * np.pi * target_velocity * (m * CHIRP_PERIOD) / LAMBDA
        
        # Generate signal (12-bit ADC simulation centered at 2048)
        signal = 2048 + 1000 * np.cos(2 * np.pi * f_beat * t + doppler_phase)
        
        # Add noise so the FFT isn't completely artificial
        noise = np.random.normal(0, 100, SAMPLES_PER_CHIRP)
        signal = signal + noise
        
        # Convert to uint16 bytes
        signal = np.clip(signal, 0, 65535).astype(np.uint16)
        
        # Pack header + data exactly as the hardware would
        frame_bytes.extend(SYNC_WORD)
        frame_bytes.extend(signal.tobytes())
        
    return frame_bytes

def simulated_data_producer(queue):
    """Thread 1: Mimics the USB Serial Port feeding data into the buffer."""
    print(f"\n[PRODUCER] Generating simulated data for Range: {TARGET_RANGE}m, Vel: {TARGET_VELOCITY}m/s...")
    
    # Pre-generate the bytes so we don't lag the CPU during the while loop
    mock_hardware_stream = generate_simulated_frame(TARGET_RANGE, TARGET_VELOCITY)
    stream_length = len(mock_hardware_stream)
    
    SYNC_WORD = b'\xff\xff\xff\xff' 
    buffer = bytearray()
    
    # We will simulate reading 4096 bytes at a time (typical USB chunk size)
    chunk_size = 4096 
    read_pointer = 0
    
    print("[PRODUCER] Starting data stream...")
    
    while True:
        try:
            # 1. SIMULATE SERIAL READ (Grab a chunk of bytes)
            end_pointer = read_pointer + chunk_size
            
            # Wrap around if we hit the end of our pre-calculated frame
            if end_pointer >= stream_length:
                incoming = mock_hardware_stream[read_pointer:] + mock_hardware_stream[:end_pointer - stream_length]
                read_pointer = end_pointer - stream_length
            else:
                incoming = mock_hardware_stream[read_pointer:end_pointer]
                read_pointer = end_pointer
                
            if incoming:
                buffer.extend(incoming)
            
            # Throttle slightly to mimic hardware speed (prevents RAM explosion)
            time.sleep(0.0005) 
            
            # 2. YOUR EXACT SYNC HUNTER LOGIC
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
                    
        except KeyboardInterrupt:
            break


def data_consumer(queue):
    """Thread 2: Stacks chirps into a Matrix and performs the 2D FFT."""
    
    # Pre-compute windows for efficiency
    range_window = np.hanning(SAMPLES_PER_CHIRP)
    doppler_window = np.hanning(CHIRPS_PER_FRAME)
    
    # Reshape doppler window to column vector for matrix multiplication
    doppler_window = doppler_window.reshape(-1, 1) 

    while True:
        frame_data = []
        
        # 1. Collect enough chirps to build one Frame (Matrix)
        while len(frame_data) < CHIRPS_PER_FRAME:
            raw_bytes = queue.get()
            signal = np.frombuffer(raw_bytes, dtype=np.uint16).astype(np.float32)
            
            # Remove DC offset immediately
            signal = signal - np.mean(signal)
            frame_data.append(signal)
            
        # 2. Convert list of arrays into a 2D Numpy Matrix
        radar_matrix = np.vstack(frame_data) 
        
        # 3. RANGE FFT (Horizontal)
        matrix_windowed_1d = radar_matrix * range_window
        range_fft = np.fft.fft(matrix_windowed_1d, axis=1)
        
        # Take only the positive frequencies (first half of columns)
        range_fft = range_fft[:, :SAMPLES_PER_CHIRP // 2]
        
        # 4. DOPPLER FFT (Vertical)
        matrix_windowed_2d = range_fft * doppler_window
        doppler_fft = np.fft.fft(matrix_windowed_2d, axis=0)
        
        # 5. Shift Zero-Velocity to the center
        range_doppler_map = np.fft.fftshift(doppler_fft, axes=0)
        
        # 6. Extract Magnitude for Visualization/Detection
        rd_magnitude = np.abs(range_doppler_map)
        
        # --- DETECTION LOGIC ---
        target_idx = np.unravel_index(np.argmax(rd_magnitude), rd_magnitude.shape)
        doppler_bin = target_idx[0]
        range_bin = target_idx[1]
        
        # Convert bins to actual physical units
        range_freq = range_bin * (SAMPLING_FREQ / SAMPLES_PER_CHIRP)
        target_range = range_freq * (C * TC / (2 * BW))
        
        doppler_shifted_idx = doppler_bin - (CHIRPS_PER_FRAME // 2)
        prf = 1 / CHIRP_PERIOD 
        doppler_freq = doppler_shifted_idx * (prf / CHIRPS_PER_FRAME)
        target_velocity = doppler_freq * (LAMBDA / 2)
        
        print(f"Target Detected -> Range: {target_range:.2f} m | Velocity: {target_velocity:.2f} m/s")

if __name__ == '__main__':
    data_queue = Queue(maxsize=512) 
    # Swap out your hardware producer for the simulated producer
    producer_process = Process(target=simulated_data_producer, args=(data_queue,))
    producer_process.start()
    
    try:
        data_consumer(data_queue)
    except KeyboardInterrupt:
        producer_process.terminate()
