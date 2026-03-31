
---
## Mechanical Components

| Parameter | Value |
|---|---|
| Sensor | IMX477, 1/2.3″ (7.857 mm diagonal), crop factor 5.62× |
| Lens focal length | 4 mm |
| Horizontal FOV | ~90° |
| Working range | 0 – 5 m |
| Frame rate | 200 fps |
| Infrared wavelength | 940 nm |
| Mount | CS-mount; Canon C-mount flange focal distance 17.526 mm |

**Why 940 nm IR?**
940 nm illumination flood-lights the indoor environment without saturating the sensor. This allows the exposure time to be kept short enough to sustain ≥ 200 fps and minimise motion blur on a ball travelling at 62.58 m/s.

**Ball preparation:** The ball is painted with IR-reflective paint and taped with retroreflective tape in the pattern described in the Trackman pattern document.

**Frame count check:**
```
Distance = 5 m,  Speed = 62.58 m/s,  FPS = 200
Time in frame = 5 / 62.58 ≈ 0.0799 s
Frames captured = 0.0799 × 200 ≈ 15 frames  ✓
```
