import cv2
import numpy as np
import torch
from ultralytics import YOLO
from sahi import AutoDetectionModel
from sahi.predict import get_sliced_prediction
from boxmot import ByteTrack

# --- CONFIGURATION ---
VIDEO_PATH = "path/to/your/video.mp4"  # Or use your frames directory logic
MODEL_PATH = "yolo11n.pt"  # Your YOLO model
OUTPUT_PATH = "sahi_kalman_golf.mp4"

# Detection Thresholds
CROP_SIZE = 640       # Size of the "Search Region" around the predicted ball
CONF_THRESH = 0.2     # Detection confidence
SAHI_SLICE = 512      # Slice size for the fallback full scan

# --- 1. INITIALIZE MODELS ---
# A. Standard YOLO (For the specific crop search)
yolo_model = YOLO(MODEL_PATH)

# B. SAHI Model (For the full frame recovery)
sahi_model = AutoDetectionModel.from_pretrained(
    model_type='yolov8',
    model_path=MODEL_PATH,
    confidence_threshold=CONF_THRESH,
    device="cuda" if torch.cuda.is_available() else "cpu"
)

# C. ByteTrack
tracker = ByteTrack(
    track_thresh=0.25,
    match_thresh=0.8,
    track_buffer=30,
    frame_rate=200
)

# --- 2. HELPER: SMART CROP ---
def get_crop_coords(frame_width, frame_height, center_x, center_y, size):
    """
    Calculates coordinates to crop the image around a center point,
    ensuring we don't go outside image boundaries.
    """
    half = size // 2
    x1 = max(0, int(center_x - half))
    y1 = max(0, int(center_y - half))
    x2 = min(frame_width, int(center_x + half))
    y2 = min(frame_height, int(center_y + half))
    return x1, y1, x2, y2

# --- 3. VIDEO LOOP ---
cap = cv2.VideoCapture(VIDEO_PATH)
w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
fps = cap.get(cv2.CAP_PROP_FPS)
out = cv2.VideoWriter(OUTPUT_PATH, cv2.VideoWriter_fourcc(*'mp4v'), fps, (w, h))

# State variable to store the last known position of the ball
# Format: [x1, y1, x2, y2]
last_known_box = None

print("Starting Smart ROI + SAHI + ByteTrack...")

while cap.isOpened():
    ret, frame = cap.read()
    if not ret:
        break

    detections = []

    # ==========================================================
    # STRATEGY A: PREDICTED SEARCH REGION (Fast & High Res)
    # ==========================================================
    search_success = False

    if last_known_box is not None:
        # Calculate center of the previous known position
        lx1, ly1, lx2, ly2 = last_known_box
        cx, cy = (lx1 + lx2) / 2, (ly1 + ly2) / 2

        # Crop the frame around this predicted location
        c_x1, c_y1, c_x2, c_y2 = get_crop_coords(w, h, cx, cy, CROP_SIZE)
        crop_img = frame[c_y1:c_y2, c_x1:c_x2]

        # Run Standard YOLO on just this small crop
        if crop_img.size > 0:
            results = yolo_model.predict(crop_img, conf=CONF_THRESH, verbose=False)

            if len(results[0].boxes) > 0:
                # We found it in the crop!
                box = results[0].boxes.data[0].cpu().numpy()  # [x1, y1, x2, y2, conf, cls]

                # IMPORTANT: Convert crop coordinates back to Global Frame coordinates
                global_x1 = box[0] + c_x1
                global_y1 = box[1] + c_y1
                global_x2 = box[2] + c_x1
                global_y2 = box[3] + c_y1

                detections = np.array([[global_x1, global_y1, global_x2, global_y2, box[4], box[5]]])
                search_success = True

                # Optional Visualization: Draw the search area being used
                # cv2.rectangle(frame, (c_x1, c_y1), (c_x2, c_y2), (0, 255, 255), 1)

    # ==========================================================
    # STRATEGY B: SAHI FALLBACK (If Strategy A failed or no history)
    # ==========================================================
    if not search_success:
        # print("Predicted crop failed or track lost. Running full SAHI...")

        sahi_result = get_sliced_prediction(
            frame,
            sahi_model,
            slice_height=SAHI_SLICE,
            slice_width=SAHI_SLICE,
            overlap_height_ratio=0.2,
            overlap_width_ratio=0.2,
            verbose=0
        )

        sahi_dets = []
        for obj in sahi_result.object_prediction_list:
            bbox = obj.bbox.to_xyxy()
            sahi_dets.append([bbox[0], bbox[1], bbox[2], bbox[3], obj.score.value, obj.category.id])

        if len(sahi_dets) > 0:
            detections = np.array(sahi_dets)

    # ==========================================================
    # UPDATE TRACKER (ByteTrack / Kalman)
    # ==========================================================
    if len(detections) == 0:
        detections = np.empty((0, 6))

    # This updates the internal Kalman Filter state
    tracks = tracker.update(detections, frame)

    # ==========================================================
    # ENFORCE A SINGLE TRACK
    # ==========================================================
    best_track = None
    if len(tracks) > 0:
        if len(tracks) > 1:
            id_to_history = {}
            for t in tracker.tracked_stracks:
                # Checks for 'tracklet_len' first, then 'track_len', defaults to 0.
                history = getattr(t, 'tracklet_len', getattr(t, 'track_len', 0))
                id_to_history[t.track_id] = history

            max_history = -1
            for track_out in tracks:
                track_id = int(track_out[4])

                history_len = id_to_history.get(track_id, 0)

                if history_len > max_history:
                    max_history = history_len
                    best_track = track_out
        else:
            # If there's only one track, it's the best one.
            best_track = tracks[0]

    # Update our "last_known_box" for the next frame logic
    # We reset it to None, and if we find an active track, we set it.
    last_known_box = None
    if best_track is not None:
        tx1, ty1, tx2, ty2 = map(int, best_track[:4])
        tid = int(best_track[4])

        # Save this position for the next frame's "Predicted Crop"
        last_known_box = [tx1, ty1, tx2, ty2]

        # Draw
        cv2.rectangle(frame, (tx1, ty1), (tx2, ty2), (0, 255, 0), 2)
        cv2.putText(frame, f"ID:{tid}", (tx1, ty1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

    out.write(frame)

cap.release()
out.release()
print("Done.")