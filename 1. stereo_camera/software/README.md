
---
## Host Software

### UVC Frame Capture

```python
cap = cv2.VideoCapture(0)
while True:
    ret, frame = cap.read()   # OpenCV reconstructs the UVC stream
    if ret:
        cv2.imshow('FX3 UVC Stream', frame)
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break
```

---

### Stereo Calibration & Rectification

**Coordinate convention:**
- `objp` — 3D ground-truth corner positions defined *once* outside the loop in world space `(X, Y, 0)`.
- `corners` — 2D pixel observations found *inside* the loop for each calibration image.

**Monocular calibration** (Zhang's method):

```python
retL, K1, D1, rvecsL, tvecsL = cv2.calibrateCamera(
    obj_pts, img_ptsL, (640, 480), None, None, None)
retR, K2, D2, rvecsR, tvecsR = cv2.calibrateCamera(
    obj_pts, img_ptsR, (640, 480), None, None, None)
```

Solves for the **pinhole camera model**:  
`p = K [R | t] P`  
where `K` is the 3×3 intrinsic matrix (focal lengths `fx`, `fy`; principal point `cx`, `cy`) and `D` holds the Brown-Conrady radial + tangential distortion coefficients.

**Stereo calibration** (with `CALIB_FIX_INTRINSIC`):

```python
ret, K1, D1, K2, D2, R, T, E, F = cv2.stereoCalibrate(
    obj_pts, img_ptsL, img_ptsR, K1, D1, K2, D2,
    (640, 480), criteria=criteria, flags=flags)
```

Computes the rigid extrinsic transform between the two cameras:  
`P_R = R · P_L + T`  
Also outputs the Essential matrix `E = [T]× · R` and Fundamental matrix `F`.

**Stereo rectification** (Bouguet's algorithm, `alpha=0` for maximum crop):

```python
R1, R2, P1, P2, Q, roi1, roi2 = cv2.stereoRectify(
    K1, D1, K2, D2, (640, 480), R, T, alpha=0)
```

Computes rectification rotations `R1`, `R2`; projection matrices `P1`, `P2`; and the **disparity-to-depth matrix `Q`** used later in `reprojectImageTo3D`.

**Undistort + rectify maps:**

```python
mapL_x, mapL_y = cv2.initUndistortRectifyMap(K1, D1, R1, P1, (640, 480), cv2.CV_32FC1)
mapR_x, mapR_y = cv2.initUndistortRectifyMap(K2, D2, R2, P2, (640, 480), cv2.CV_32FC1)

rectified_left  = cv2.remap(left_img,  mapL_x, mapL_y, cv2.INTER_LINEAR)
rectified_right = cv2.remap(right_img, mapR_x, mapR_y, cv2.INTER_LINEAR)
```

For every pixel `(u, v)` in the ideal rectified image, the map stores the corresponding sub-pixel location in the original distorted image. `remap` applies the transformation using bilinear interpolation, performing lens undistortion and epipolar rectification in one pass.

---

### Disparity Mapping

```python
stereo = cv2.StereoSGBM_create(...)
disparity_left  = stereo.compute(rectified_left,  rectified_right)
disparity_right = stereo.compute(rectified_right, rectified_left)

wls_filter = cv2.ximgproc.createDisparityWLSFilter(stereo)
disparity_filtered = wls_filter.filter(disparity_left, rectified_left,
                                       disparity_map_right=disparity_right)
```

**SGBM (Semi-Global Block Matching)** can produce noisy maps with holes in uniform or occluded regions. The **WLS (Weighted Least Squares) filter** uses both the left and right disparity maps for a left-right consistency check, suppressing occluded regions, reducing quantisation noise, and removing speckles.

---

### 3D Sphere Fitting

```python
points_3d = cv2.reprojectImageTo3D(disparity_filtered, Q)

pcd = o3d.geometry.PointCloud()
pcd.points = o3d.utility.Vector3dVector(ball_points_3d)
```

Finding the **true 3D centre** rather than the 2D centroid corrects for perspective foreshortening.

**Linear Least Squares sphere fit:**

Sphere equation:  
`(x − xc)² + (y − yc)² + (z − zc)² = r²`

Expanded into linear form:  
`2x·xc + 2y·yc + 2z·zc + (r² − xc² − yc² − zc²) = x² + y² + z²`

Let `m = r² − xc² − yc² − zc²`, then:

```
A = [2x, 2y, 2z, 1]
b = x² + y² + z²

Solve: A · [xc, yc, zc, m]ᵀ = b  (linear least squares, no iteration needed)
```

---

### Statistical Outlier Removal

```python
pcd_clean, ind = pcd.remove_statistical_outlier(
    nb_neighbors=20,    # neighbours to consider per point
    std_ratio=2.0       # lower = more aggressive removal
)
```

Points further from their neighbours than `mean_distance + std_ratio × σ` are removed.

---

### Angle Calculation

From the cleaned ball centre `(xc, yc, zc)`:

```python
depth         = zc
launch_angle  = math.degrees(math.atan2(-yc, zc))   # vertical angle
azimuth_angle = math.degrees(math.atan2( xc, zc))   # horizontal angle
```

---

## Machine Learning

### 1. Synthetic Data Generation (Blender)

Create a labelled training dataset without needing to film thousands of real shots:

1. Model a green field; import a 3D golf ball model; set up a stereo camera pair.
2. Animate the ball at various launch angles and speeds.
3. Script-export frames + auto-generate YOLO bounding box `.txt` files.

**Domain Randomisation pipeline** (to bridge the sim-to-real gap):

| Technique | Implementation |
|---|---|
| Lighting variation | Randomise sun angle every 10 frames |
| Background variation | Randomise background texture (sky, trees, grass, brick) so the model learns to ignore it |
| Sensor noise | Add a Blender composite noise node (simulates ISO grain) |

---

### 2. YOLO + SAM v2 + Kalman Filter

| Component | Role |
|---|---|
| **YOLO + SAHI** | SAHI slices each frame into overlapping patches before YOLO inference, drastically improving detection of small/fast objects |
| **Kalman Filter** | Predicts the ball's position in the next frame even when it is motion-blurred or temporarily occluded |
| **SAM v2** | Deep-learning tracker that produces a tight polygon mask around the ball; handles shape/rotation changes well but is computationally heavy |

---

### 3. TensorRT Inference

Convert trained models to TensorRT `.engine` files for low-latency inference on the host GPU:

```bash
trtexec --onnx=yolo_ball.onnx --saveEngine=yolo_ball.engine --fp16
```

---

**Host PC pipeline:**

```
Stereo Frames
    │
    ├─ YOLO + SAHI + Kalman Filter  ──▶  Ball Detection & Tracking
    │
    ├─ Stereo Rectification & Disparity Mapping  ──▶  Depth Map
    │
    ├─ cv2.reprojectImageTo3D  ──▶  3D Point Cloud
    │
    ├─ Open3D Sphere Fitting + remove_statistical_outlier()  ──▶  Ball Centre (x, y, z)
    │
    └─ Geometry  ──▶  Launch Angle, Azimuth Angle, Depth
```
---
