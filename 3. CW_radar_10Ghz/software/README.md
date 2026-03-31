
---
## Signal Processing

### 1. Doppler — Speed

A moving ball shifts the frequency of the reflected CW signal by:

$$f_d = \frac{2v \cdot f_0}{c}$$

where $v$ is the ball's velocity along the radar line of sight, $f_0$ is the transmit frequency, and $c$ is the speed of light. Because the ball travels at an angle $\alpha$ to the radar beam, the true speed is corrected by:

$$v_{true} = \frac{v_{measured}}{\sin(\alpha)}$$

The angle $\alpha$ (launch angle/direction) is supplied by the external stereo camera or 24 GHz FMCW radar.

---
### 2. Micro-Doppler — Spin Rate

A spinning ball is **not** a point target. Each surface element has a different radial velocity:

- **Top of ball:** $v_{radial} = v_{ball} + v_{spin}$ (moving toward radar)
- **Bottom of ball:** $v_{radial} = v_{ball} - v_{spin}$ (moving away from radar)

This spreads the Doppler return into a characteristic "hump" rather than a single spike. The half-width of this hump gives the spin surface velocity:

$$V_{spin} = \frac{V_{max} - V_{min}}{2 \cdot \sin(\alpha)}$$

where $\alpha$ is now the angle between the **spin axis** and the **radar line of sight**. When the spin axis is perpendicular to the beam ($\alpha = 90°$), the full spin velocity is observed; when parallel ($\alpha = 0°$), the radar sees no spin at all (the hump collapses to a spike).

Spin rate in RPM:

$$\text{RPM} = \frac{V_{spin}}{\pi \cdot d} \times 60$$

where $d$ is the ball diameter (circumference $= \pi d$).

---
### 3. Interferometry — Spin Axis

At 10 GHz the required antenna spacing for unambiguous interferometry is $\lambda/2 \approx 1.43\ \text{cm}$, which is too small to implement even with patch antennas. Instead, the spin axis is determined by a **single-baseline delta angle-of-arrival (ΔAoA)** technique:

1. The FFT magnitude spectrum across frequency bins traces a **line across the ball's cross-section**. The bin with maximum amplitude corresponds to the ball's center; bins toward the edges have lower amplitude and a measurable phase ripple.
2. For each frequency bin (i.e., each radial slice of the ball), the **vertical and horizontal phase** of that bin's return relative to the center bin gives the angular position $(y, z)$ of that scattering point on the ball's surface.
3. Two key points are identified: the bin where $v_{radial}$ is maximum (top of the spin) and where it is minimum (bottom of the spin).
4. A line connecting these two points is the **velocity gradient axis**. The **spin axis is perpendicular to this line**.

Global launch angle and direction are sourced from the camera/FMCW sensor, not this radar.

# Golf Ball Spin Rate & Axis Detection Using Radar
## Phase-Phase Monopulse Method

> Full derivation from raw phase measurements to a calibrated 3D spin
> axis vector in world coordinates. Based on patent techniques for
> FMCW/CW radar launch monitor systems.

---
## 1. System Overview

### Coordinate System

```
                  Z (Up)
                  │
                  │
                  │
                  └─────── Y (Right)
                 /
                /
               X (Line of sight, away from radar)
```

### Hardware Assumptions

| Parameter | Value |
|---|---|
| Frequency | 10 GHz |
| Wavelength λ | 30 mm |
| Receiver spacing D | 60 mm |
| Number of RX antennas | 4 (2 horizontal pair + 2 vertical pair) |
| Architecture | 1TX, 4RX, coherent I/Q output |

### Why I/Q Is Mandatory

The entire method depends on measuring the **complex phase** of
the received signal at each antenna. With real IF only you
cannot compute phase differences between antennas. I/Q
demodulation gives you:

```
Z_rx = I + jQ
φ = atan2(Q, I)
```

This φ is the quantity measured at each receiver, and the
**difference** between two receivers' φ values is Δφ — the
input to everything below.

---

## 2. Step 1 — Measure Phase Differences

### What the FFT gives you

After performing a Range FFT and Doppler FFT on the I/Q data
from each receiver, you have a **Range-Doppler map** per
antenna. Each bin in this map contains a complex number.

For a golf ball, the spinning surface produces multiple
Doppler frequency components — the leading edge of the ball
(spinning toward radar) produces a higher Doppler, and the
trailing edge produces a lower Doppler.

### Isolate a specific Doppler bin

Pick a frequency bin `k` of interest (e.g., the highest
Doppler sideband from the spinning ball). Extract the complex
value from that bin for each receiver:

```
Z_A = I_A + jQ_A    ← from receiver A (e.g., top of vertical pair)
Z_B = I_B + jQ_B    ← from receiver B (e.g., bottom of vertical pair)
Z_C = I_C + jQ_C    ← from receiver C (e.g., left of horizontal pair)
Z_D = I_D + jQ_D    ← from receiver D (e.g., right of horizontal pair)
```

### Calculate phase differences

```
Δφ_vert  = angle(Z_A × conj(Z_B))    ← vertical pair
Δφ_horiz = angle(Z_C × conj(Z_D))    ← horizontal pair
```

> **Why `angle(Z1 × conj(Z2))` instead of `atan2(Q1,I1) - atan2(Q2,I2)`?**
> The complex conjugate product handles the ±π phase wrapping
> automatically and is numerically more stable.

---

## 3. Step 2 — Convert Phase to Physical Position on Ball

### The formula

From the radar interferometry equation (patent Column 4,
Equation [1]):

```
Δφ = (2π · sin(α) · D) / λ
```

Rearranged to solve for the angular position of the
reflection point:

```
sin(α) = (Δφ · λ) / (2π · D)
```

The physical position on the ball at range R:

```
Position = R · sin(α)          (small angle approximation)
```

### Vertical position Z

```
sin(αz) = (Δφ_vert · λ) / (2π · D)
Z = R · sin(αz)
```

### Horizontal position Y

```
sin(αy) = (Δφ_horiz · λ) / (2π · D)
Y = R · sin(αy)
```

### Numerical example

**Hardware:** λ = 30 mm, D = 60 mm, R = 4200 mm

**Measured:** Δφ_vert = +10° = 0.1745 rad, Δφ_horiz = −5° = −0.0873 rad

```
Vertical:
  sin(αz) = (0.1745 × 30) / (2π × 60) = 5.235 / 376.99 = 0.01388
  Z = 4200 × 0.01388 = +58.3 mm above radar beam centre

Horizontal:
  sin(αy) = (−0.0873 × 30) / (2π × 60) = −2.619 / 376.99 = −0.00695
  Y = 4200 × −0.00695 = −29.2 mm (to the left)
```

**This frequency bin originates from point [0, −29.2, +58.3] mm on the ball.**

---

## 4. Step 3 — Extract the Principal Axis Vector

### Repeat across the Doppler spectrum

Repeat Step 2 for **multiple Doppler frequency bins** across
the ball's spin-induced spectrum — from the lowest Doppler
sideband (one side of the ball) to the highest (opposite side).

This produces a set of (Y, Z) coordinate pairs:

```
Low freq bin  → Point A:  (y₁, z₁)   ← one pole of the spin axis
Middle bins   → Points along the equator
High freq bin → Point B:  (y₂, z₂)   ← opposite pole
```

Plotting these points in the Y-Z plane produces a roughly
linear pattern. The **Principal Axis Vector n** is the line
of best fit through these points.

### Extract n

```
Δy = y₂ − y₁
Δz = z₂ − z₁

n_raw = [0, Δy, Δz]     ← X = 0 because this is a 2D projection
                            perpendicular to the radar line of sight

Normalise:
|n| = sqrt(Δy² + Δz²)
n = [0, Δy/|n|, Δz/|n|]
```

> **Physical meaning:** `n` is the projection of the spin axis
> onto the plane perpendicular to the radar line of sight.
> The X component (along line of sight) cannot be measured
> directly by the Doppler analysis — this is recovered in
> Step 4 using the No-Gyro assumption.

---

## 5. Step 4 — Determine the Spin Axis (No-Gyro Method)

### The problem

From Step 3 we have `n = [0, ny, nz]` — a 2D projection.
The full 3D spin axis is `u = [ux, uy, uz]` where `ux` is
unknown.

### The two constraints

**Constraint A — Radar data:**
The spin axis is perpendicular to the principal axis.

```
u · n = 0
```

From this, the Y and Z components of u follow directly by
rotating n by 90° in the Y-Z plane:

```
uy =  nz
uz = −ny
```

**Constraint B — No-Gyro assumption:**
A golf ball in free flight conserves angular momentum. Without
an external torque (gyroscopic precession), the spin axis
stays fixed relative to inertial space. This means the spin
axis is **perpendicular to the velocity vector**:

```
u · V = 0
```

### Solve for ux

Expand the dot product:

```
ux·vx + uy·vy + uz·vz = 0
ux·vx + (nz)·vy + (−ny)·vz = 0
ux·vx = ny·vz − nz·vy

       ny·vz − nz·vy
ux =  ─────────────────
            vx
```

### Alternative method: cross product

Since u must be perpendicular to both n and V simultaneously:

```
u_raw = n × V

ux_raw = ny·vz − nz·vy
uy_raw = nz·vx − 0·vz  =  nz·vx
uz_raw = 0·vy − ny·vx  = −ny·vx
```

Both methods give the same result. The cross product form is
cleaner to implement.

### Normalise

```
|u_raw| = sqrt(ux_raw² + uy_raw² + uz_raw²)

u = u_raw / |u_raw|
```

### Numerical example

```
n = [0, 1, 2]      (from Step 3)
V = [40, 5, 20]    (velocity from ball tracking, m/s)

ux = (1×20 − 2×5) / 40 = (20−10) / 40 = 0.25

u_raw = [0.25, 2, −1]

|u_raw| = sqrt(0.0625 + 4 + 1) = sqrt(5.0625) ≈ 2.25

u = [0.25/2.25, 2/2.25, −1/2.25]
u ≈ [0.11, 0.89, −0.44]
```

---

## 6. Step 5 — Convert to World Coordinates

### Why this is needed

The spin axis vector `u` is currently in **radar coordinates**
where X is the radar's line of sight. If the radar is tilted
up at an angle θ (pitch), the X axis of the radar points
forward and upward in the real world, not horizontally.

### Rotation matrix (pitch only)

For a radar tilted upward by pitch angle θ:

```
         ┌ cos θ   0   −sin θ ┐
R_pitch = │   0     1     0   │
         └ sin θ   0    cos θ ┘
```

### Transform

```
u_world = R_pitch · u_radar
```

Expanded:

```
u_xw = cos(θ)·ux − sin(θ)·uz
u_yw = uy                        ← lateral axis unchanged by pitch
u_zw = sin(θ)·ux + cos(θ)·uz
```

### Full rotation (pitch + yaw)

If the radar is also rotated horizontally by yaw angle ψ:

```
         ┌ cos ψ   −sin ψ   0 ┐
R_yaw  = │ sin ψ    cos ψ   0 │
         └   0        0     1 ┘

R_total = R_yaw · R_pitch

u_world = R_total · u_radar
```

### Numerical example

**Radar pitch θ = 20°**, cos(20°) = 0.940, sin(20°) = 0.342

```
u_radar = [0.11, 0.89, −0.44]

u_xw = 0.940×0.11 + (−0.342)×(−0.44) = 0.1034 + 0.1505 = 0.254
u_yw = 0.89                                                = 0.890
u_zw = 0.342×0.11 + 0.940×(−0.44)    = 0.0376 − 0.4136  = −0.376

u_world ≈ [0.25, 0.89, −0.38]
```

### Temporal averaging for stability

Compute `u_world` at each time step during flight and average:

```python
import numpy as np

spin_axes = []   # list of u_world vectors at each frame

for frame in radar_frames:
    u = compute_spin_axis(frame)
    u_world = rotate_to_world(u, pitch_deg=20)
    spin_axes.append(u_world)

# Average using vector mean and renormalise
u_mean = np.mean(spin_axes, axis=0)
u_final = u_mean / np.linalg.norm(u_mean)
```

---

## 7. Step 6 — Spin Rate from Doppler Spectrum

### Where spin rate comes from

The Doppler spectrum of the ball contains **sidebands** at
the spin frequency and its harmonics. The frequency spacing
between the main return and the first sideband directly
encodes the spin rate.

For a point on the ball surface at radius r from the spin
axis, the radial velocity contribution seen by the radar is:

```
v_micro = ω × r × sin(angle between spin axis and LOS)
```

The **micro-Doppler bandwidth** (total spread of the
Doppler spectrum) is:

```
f_spread = (4π × r_ball × ω × sin(φ_axis)) / λ

Where:
  r_ball = 21.3 mm (golf ball radius)
  ω      = spin rate in rad/s
  φ_axis = angle between spin axis and radar line of sight
  λ      = radar wavelength
```

Solving for spin rate:

```
ω = (f_spread × λ) / (4π × r_ball × sin(φ_axis))

RPM = ω × (60 / 2π)
```

### Practical extraction

```
1. Take the Doppler FFT magnitude spectrum for the ball range bin
2. Find the extent of the micro-Doppler spread:
     f_spread = f_max_sideband − f_min_sideband
3. Apply correction for spin axis angle to radar LOS
4. Convert to RPM
```

> **Note:** `sin(φ_axis)` is the sine of the angle between the
> spin axis vector u and the radar line of sight (X axis).
> This is simply `sqrt(uy² + uz²)` since u is already a
> unit vector and ux = cos(φ_axis).

```python
import numpy as np

def spin_rate_rpm(f_spread_hz, wavelength_m, r_ball_m, u_vector):
    """
    f_spread_hz : total Doppler bandwidth of micro-Doppler sidebands
    wavelength_m: radar wavelength in metres
    r_ball_m    : ball radius in metres (0.02135 for golf)
    u_vector    : normalised 3D spin axis unit vector [ux, uy, uz]
    """
    # Component of spin axis perpendicular to line of sight
    sin_phi = np.sqrt(u_vector[1]**2 + u_vector[2]**2)

    if sin_phi < 1e-6:
        return 0.0   # spin axis points directly at radar, no visible micro-Doppler

    omega = (f_spread_hz * wavelength_m) / (4 * np.pi * r_ball_m * sin_phi)
    rpm = omega * 60 / (2 * np.pi)
    return rpm
```

---

## 8. Full Numerical Example

### Inputs

```
Radar:   10 GHz, λ = 30 mm, D = 60 mm, R = 4200 mm, θ_pitch = 20°
Ball:    range 4.2 m, velocity V = [40, 5, 20] m/s

Measured phase differences at high Doppler bin:
  Δφ_vert  = +10°  = +0.1745 rad
  Δφ_horiz = −5°   = −0.0873 rad

Measured phase differences at low Doppler bin:
  Δφ_vert  = −10°  = −0.1745 rad
  Δφ_horiz = +5°   = +0.0873 rad

Doppler spectrum spread: f_spread = 800 Hz
```

### Stage 1: Position of high-frequency point (Point B)

```
sin(αz) = (0.1745 × 30) / (2π × 60) = 0.01388  →  Z_B = +58.3 mm
sin(αy) = (−0.0873 × 30) / (2π × 60) = −0.00695 →  Y_B = −29.2 mm
Point B = [0, −29.2, +58.3] mm
```

### Stage 2: Position of low-frequency point (Point A)

```
sin(αz) = −0.01388  →  Z_A = −58.3 mm
sin(αy) = +0.00695  →  Y_A = +29.2 mm
Point A = [0, +29.2, −58.3] mm
```

### Stage 3: Principal axis vector

```
Δy = −29.2 − 29.2 = −58.4
Δz = +58.3 − (−58.3) = +116.6

n_raw = [0, −58.4, 116.6]

Simplify (divide by 58.4):
n = [0, −1, 2]

Normalise:
|n| = sqrt(0 + 1 + 4) = sqrt(5) = 2.236
n = [0, −0.447, 0.894]
```

### Stage 4: Spin axis (radar frame)

```
Using simplified n = [0, −1, 2], V = [40, 5, 20]:

uy =  nz = 2
uz = −ny = 1

ux = (ny·vz − nz·vy) / vx
   = ((−1)×20 − 2×5) / 40
   = (−20 − 10) / 40
   = −0.75

u_raw = [−0.75, 2, 1]
|u_raw| = sqrt(0.5625 + 4 + 1) = sqrt(5.5625) ≈ 2.358
u_radar = [−0.318, 0.848, 0.424]
```

### Stage 5: World coordinates (θ_pitch = 20°)

```
cos(20°) = 0.940, sin(20°) = 0.342

u_xw = 0.940×(−0.318) + (−0.342)×0.424 = −0.299 − 0.145 = −0.444
u_yw = 0.848
u_zw = 0.342×(−0.318) + 0.940×0.424 = −0.109 + 0.399 = +0.290

u_world = [−0.444, 0.848, 0.290]

Re-normalise (should already be ~1 but floating point):
|u_world| ≈ 1.0
u_world ≈ [−0.44, 0.85, 0.29]
```

### Stage 6: Spin rate

```
sin(φ_axis) = sqrt(uy² + uz²) = sqrt(0.848² + 0.290²)
            = sqrt(0.719 + 0.084) = sqrt(0.803) ≈ 0.896

ω = (f_spread × λ) / (4π × r_ball × sin_phi)
  = (800 × 0.030) / (4π × 0.02135 × 0.896)
  = 24 / 0.2406
  ≈ 99.7 rad/s

RPM = 99.7 × 60 / (2π) ≈ 952 RPM
```

---

## 9. Implementation Notes

### Signal processing pipeline

```
Per chirp (or CW frame):
  ├── Range FFT on I+jQ for each of 4 RX channels
  ├── Doppler FFT across N chirps (or STFT for CW)
  └── For each frame output:

Per frame:
  ├── Find ball range bin (CFAR or peak detection)
  ├── For each Doppler bin k in micro-Doppler band:
  │     ├── Extract complex values Z_A, Z_B, Z_C, Z_D
  │     ├── Δφ_vert  = angle(Z_A × conj(Z_B))
  │     ├── Δφ_horiz = angle(Z_C × conj(Z_D))
  │     ├── Y_k = R × sin((Δφ_horiz × λ) / (2π × D))
  │     └── Z_k = R × sin((Δφ_vert  × λ) / (2π × D))
  │
  ├── Fit line through (Y_k, Z_k) points → principal axis n
  ├── Compute spin axis u via cross product: u = n × V
  ├── Normalise u
  ├── Rotate to world frame: u_world = R_pitch · u
  ├── Measure f_spread from Doppler spectrum
  └── Compute RPM
```

### Key limitations

| Issue | Effect | Mitigation |
|---|---|---|
| No-Gyro assumes zero precession | Error if ball wobbles | Average over multiple frames |
| 2 RX only gives 1D phase per axis | Need 2 pairs (4 RX) for Y and Z | Minimum 4 RX antennas |
| Small angle approximation | Error at large off-axis angles | Use arcsin not small-angle |
| I/Q imbalance | Phase measurement error | Digital Gram-Schmidt correction |
| vx ≈ 0 causes division by zero | Unstable ux calculation | Use cross product method instead |
| Low SNR at high spin rate | Noisy phase estimates | Increase integration time |


---

*Method based on Trackman patent techniques for phase-phase monopulse
spin axis extraction. No-Gyro assumption valid for non-spinning
projectiles where aerodynamic forces dominate gyroscopic
precession within the measurement window.*