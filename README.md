
---

# mmWave RF Radar Development Suite

This repository documents a series of iterative projects focused on the design, implementation, and signal processing of millimeter-wave (mmWave) radar systems. The work spans from custom hardware design and FPGA-based data acquisition to synthetic data generation for machine learning applications.

## Project Modules

### 1. Stereo Vision for Ground Truth Localization
* **Purpose:** Depth sensing and spatial localization to provide "ground truth" labels for training RF-based neural networks.
* **Hardware Stack:** Dual Sony **IMX477** sensors, Xilinx **Artix-7 FPGA**, and Cypress **FX3 USB 3.0** controller.
* **Note:** Architecture is heavily influenced by the [CircuitValley USB 3.0 Industrial Camera](https://github.com/circuitvalley/USB_C_Industrial_Camera_FPGA_USB3).

### 2. 24GHz FMCW Radar (K-LC7)
* **Purpose:** An initial exploration into Frequency Modulated Continuous Wave (FMCW) algorithms.
* **Implementation:** Developed using the low-cost **K-LC7 MISO** radar transceiver to validate basic range and velocity processing.

### 3. 10GHz CW Radar (V1)
* **Purpose:** The first custom RF front-end designed from the ground up.
* **Outcome:** Served as a proof-of-concept for discrete RF design. Lessons learned regarding impedance matching and signal integrity are currently being applied to the second iteration (V2).

### 4. 77GHz High-Resolution Sensing (AWR1843BOOST)
* **Purpose:** Implementing high-frequency data capture on the **TI AWR1843BOOST** platform.
* **Key Innovation:** Developed a custom data acquisition interface to bypass the requirement for expensive proprietary capture boards (like the DCA1000EVM) while maintaining high data throughput.

### 5. Multi-Channel FMCW Radar (10GHz 2TX/4RX)
* **Purpose:** Second version of the 10GHz platform featuring **MIMO (Multiple-Input Multiple-Output)** capabilities. WIP
* **Focus:** Enhancing angular resolution and spatial mapping through a 2-Transmit/4-Receive antenna array.

### 6. Multi-Radar Synchronization Framework
* **Purpose:** Theoretical and practical foundation for phase-synchronizing multiple radar units.
* **Applications:** Enables **Static Synthetic Aperture Radar (SAR)** imaging and coherent signal enhancement for increased sensitivity and range.


---

## Technical Skills Demonstrated
* **Hardware:** RF PCB Design, FPGA (Verilog/VHDL), High-speed USB 3.0 Interfacing.
* **Signal Processing:** FMCW Range-Doppler processing, MIMO Beamforming, SAR Imaging.
* **Software/ML:** Synthetic data modeling, Ground truth synchronization for supervised learning.
* **Simulation:** ADS (Advanced Design System) for RF circuit and SI/PI analysis; COMSOL Multiphysics for specialized PCB and electromagnetic simulation.

---

