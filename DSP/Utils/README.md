#  DSP Utilities & Helper Functions

This directory contains shared mathematical and structural tools utilized across the DSP pipeline. Centralizing these functions ensures consistency between the Range and Doppler processing stages.

## Core Components

### 1. CFAR (Constant False Alarm Rate)
Simple thresholding (e.g., "detect any peak over 50 dB") fails in dynamic environments due to fluctuating noise floors. We implement **CA-CFAR (Cell-Averaging CFAR)**:
* The algorithm slides a window across the FFT output.
* It calculates the average noise level in the "Training Cells" surrounding the "Target Cell."
* A detection is only triggered if the Target Cell exceeds the local noise average by a predefined threshold factor ($\alpha$).

### 2. Windowing Functions
Wrappers for standard DSP windows applied to raw data to mitigate spectral leakage.
* **Hamming Window:** Used for Range processing to suppress strong sidelobes from large targets (e.g., walls) that might mask smaller targets (e.g., pedestrians).

### 3. I/Q Matrix Reshaping
Functions to correctly slice the raw 1D serial byte-stream from the STM32 ADC into the proper $M \times N$ (Chirps $\times$ Samples) matrices required for 2D processing.
