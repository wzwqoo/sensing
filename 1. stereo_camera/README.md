# Stereo Vision Ball Tracking System

A high-speed stereo camera system for capturing, tracking, and computing the 3D trajectory of a fast-moving ball (up to 140 mph / 62.58 m/s). The pipeline spans custom FPGA gateware, FX3 USB firmware, and a host-side Python computer vision stack that outputs launch angle, azimuth, and depth.

---
## System Overview

```
[IMX477 Left]  ──MIPI CSI-2──┐
                              ├──[MC20605]──[Artix-7 FPGA]──[Cypress FX3]──USB 3.0──[Host PC]
[IMX477 Right] ──MIPI CSI-2──┘
```
