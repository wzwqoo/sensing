import math
import cv2
import open3d as o3d
import numpy as np
from tqdm import tqdm

# Set the path to the images captured by the left and right cameras
pathL = "./data/stereoL/"
pathR = "./data/stereoR/"
CHESSBOARD_SIZE = (9, 6)
SQUARE_SIZE = 0.025  # The size of one square in meters (e.g., 25mm)

# Defines the termination criteria for the iterative sub-pixel optimization algorithm.
# The optimization halts either when the corner displacement between iterations is less than ϵ = 0.001 pixels, or when N m a x = 30 iterations are reached.
criteria = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 30, 0.001)

# Prepare object points (0,0,0), (0.025,0,0), (0.05,0,0), ...
objp = np.zeros((CHESSBOARD_SIZE[0] * CHESSBOARD_SIZE[1], 3), np.float32)
objp[:, :2] = np.mgrid[0:CHESSBOARD_SIZE[0], 0:CHESSBOARD_SIZE[1]].T.reshape(-1, 2) * SQUARE_SIZE

obj_pts = [] # 3d points in real world space
img_ptsL = [] # 2d points in left image plane
img_ptsR = [] # 2d points in right image plane

for i in tqdm(range(1, 12)):
    imgL = cv2.imread(pathL + "img%d.png" % i)
    imgR = cv2.imread(pathR + "img%d.png" % i)
    imgL_gray = cv2.imread(pathL + "img%d.png" % i, 0)
    imgR_gray = cv2.imread(pathR + "img%d.png" % i, 0)

    outputL = imgL.copy()
    outputR = imgR.copy()

    retR, cornersR = cv2.findChessboardCorners(outputR, (9, 6), None)
    retL, cornersL = cv2.findChessboardCorners(outputL, (9, 6), None)

    if retR and retL:
        # Refine corner positions for better accuracy
        cv2.cornerSubPix(imgR_gray, cornersR, (11, 11), (-1, -1), criteria)
        cv2.cornerSubPix(imgL_gray, cornersL, (11, 11), (-1, -1), criteria)
        #   optional
        cv2.drawChessboardCorners(outputR, (9, 6), cornersR, retR)
        cv2.drawChessboardCorners(outputL, (9, 6), cornersL, retL)
        cv2.imshow('cornersR', outputR)
        cv2.imshow('cornersL', outputL)
        cv2.waitKey(0)

        obj_pts.append(objp) # Same 3D points for each image
        img_ptsL.append(cornersL) # Different 2D points for each image
        img_ptsR.append(cornersR)

# Calibrating camera
retL, K1, D1, rvecsL, tvecsL = cv2.calibrateCamera(obj_pts, img_ptsL, (640, 480), None, None, None)
retR, K2, D2, rvecsR, tvecsR = cv2.calibrateCamera(obj_pts, img_ptsR, (640, 480), None, None, None)

# Here we fix the intrinsic camara matrixes so that only Rot, Trns, Emat and Fmat are calculated.
# Hence intrinsic parameters are the same
flags = cv2.CALIB_FIX_INTRINSIC
criteria_stereo = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 30, 0.001)

# This step is performed to transformation between the two cameras and calculate Essential and Fundamenatl matrix
ret, K1, D1, K2, D2, R, T, E, F = cv2.stereoCalibrate(obj_pts, img_ptsL, img_ptsR, K1, D1, K2, D2, (640, 480), criteria=criteria_stereo, flags=flags)
# Save the calibration results
np.savez("stereo_calibration.npz", K1=K1, D1=D1, K2=K2, D2=D2, R=R, T=T)
print("Calibration complete and saved!")
##############################################################################################
# 1. Calculate the Rectification Transforms
# This only needs to be done ONCE for your camera setup
# K1, K2: Intrinsic matrices (focal length fx, fy in pixels, optical center cx, cy (principal point)).[1]
    # [[fx  0  cx]
    #  [ 0  fy cy]
    #  [ 0   0   1]]
# D1, D2: Distortion coefficients.
# R, T: Rotation and Translation between the two cameras.
R1, R2, P1, P2, Q, roi1, roi2 = cv2.stereoRectify(
    K1, D1, K2, D2,
    (640, 480), R, T,
    alpha=0 # 0 zooms to remove black edges; 1 keeps all pixels
)

# 2. Compute the Undistort/Rectify Maps
# MapL_x/y tells OpenCV where each pixel in the NEW image comes from in the OLD image
mapL_x, mapL_y = cv2.initUndistortRectifyMap(K1, D1, R1, P1, (640, 480), cv2.CV_32FC1)
mapR_x, mapR_y = cv2.initUndistortRectifyMap(K2, D2, R2, P2, (640, 480), cv2.CV_32FC1)

# 3. Apply the Warp (Remap)
# This is done for every new frame
rectified_left = cv2.remap(left_img, mapL_x, mapL_y, cv2.INTER_LINEAR)
rectified_right = cv2.remap(right_img, mapR_x, mapR_y, cv2.INTER_LINEAR)

#############################################################################################
# 1. Create Stereo Matcher
stereo = cv2.StereoSGBM().create(
    minDisparity=0,
    numDisparities=64,  # Must be divisible by 16
    blockSize=5
)
# WLS滤波器配置
wls_filter = cv2.ximgproc.createDisparityWLSFilter(stereo)
wls_filter.setLambda(80000)
wls_filter.setSigmaColor(1.3)
right_matcher = cv2.ximgproc.createRightMatcher(stereo)

# Compute both Disparity Maps
# It is critical to compute BOTH to allow the filter to perform a confidence check
disparity_left = stereo.compute(img_left, img_right)
disparity_right = right_matcher.compute(img_right, img_left)

# Apply the Filter
# Parameters: (left_disparity, left_guide_image, output_disparity, right_disparity)
filtered_disp = wls_filter.filter(disparity_left, img_left, disparity_map_right=disparity_right)

# Optional: Visualization (Normalization)
# Filtered disparity is in 16-bit format (scaled by 16)
disp_vis = cv2.normalize(src=filtered_disp, dst=None, alpha=0, beta=255, norm_type=cv2.NORM_MINMAX, dtype=cv2.CV_8U)

cv2.imshow('Filtered Disparity', disp_vis)
cv2.waitKey(0)

# 3. Apply SAM2 Mask
# Assuming 'sam_mask' is a boolean array of (480, 640) from SAM2
object_disparities = filtered_disp[sam_mask]

# 4. Filter outliers (optional but recommended)
# Remove disparities <= 0 (invalid matches)
v_indices, u_indices = np.where(object_disparities > 0)


# 5. open3d Sphere Fitting (Finding the Ball Center)
points_3d = cv2.reprojectImageTo3D(filtered_disp, Q)
pcd = o3d.geometry.PointCloud()
pcd.points = o3d.utility.Vector3dVector(points_3d)
# center_u = int(np.median(u_indices))
# center_v = int(np.median(v_indices))
# x, y, z = points_3d[center_v, center_u]

# 6. Statistical Outlier Removal (Open3D). We remove points that are further away from their neighbors compared to the average.
cl, ind = pcd.remove_statistical_outlier(nb_neighbors=20, std_ratio=2.0)
pcd_clean = pcd.select_by_index(ind)

# 7. Least Squares Sphere Fitting
def fit_sphere_least_squares(pcd_obj):
    """
    Fits a sphere to a set of 3D points using Linear Least Squares.
    Equation: (x-xc)^2 + (y-yc)^2 + (z-zc)^2 = r^2
    """
    points = np.asarray(pcd_obj.points)

    # Construct the design matrix A and response vector b
    # Rewriting sphere equation: 2*x*xc + 2*y*yc + 2*z*zc + (r^2 - xc^2 - yc^2 - zc^2) = x^2 + y^2 + z^2
    N = points.shape[0]
    if N < 4: return None, None  # Not enough points

    X = points[:, 0]
    Y = points[:, 1]
    Z = points[:, 2]

    A = np.zeros((N, 4))
    A[:, 0] = 2 * X
    A[:, 1] = 2 * Y
    A[:, 2] = 2 * Z
    A[:, 3] = 1

    b = (X ** 2 + Y ** 2 + Z ** 2)

    # Solve linear system: C = [xc, yc, zc, m]
    C, residuals, rank, s = np.linalg.lstsq(A, b, rcond=None)

    xc, yc, zc = C[0], C[1], C[2]
    m = C[3]  # m = r^2 - xc^2 - yc^2 - zc^2

    radius = math.sqrt(m + xc ** 2 + yc ** 2 + zc ** 2)

    return (xc, yc, zc), radius


# Execute Fitting
center_coords, radius = fit_sphere_least_squares(pcd_clean)

# 8. get distance, elevation and azimuth
if center_coords is not None:
    x, y, z = center_coords
    distance = math.sqrt(x**2 + y**2 + z**2)
    azimuth = math.degrees(math.atan2(x, z))
    elevation = math.degrees(math.atan2(-y, math.sqrt(x ** 2 + z ** 2)))




