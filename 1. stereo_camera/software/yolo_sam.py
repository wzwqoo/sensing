import os
import cv2
import numpy as np
import torch
from ultralytics import YOLOWorld
from sam2.sam2.build_sam import build_sam2_video_predictor

# --- 1. SETUP & PATHS ---
video_path = "golf_swing.mp4"
frames_dir = "video_frames"  # SAM 2 requires frames extracted to a folder
os.makedirs(frames_dir, exist_ok=True)

# 2. 训练模型 (关键步骤)
model = YOLOWorld("yolo11s-world.pt")
results = model.train(
    data='your_golfball_dataset/data.yaml',  # 数据集配置文件路径[citation:1]
    epochs=100,                              # 训练轮次，小物体可能需要更多轮次[citation:1]
    imgsz=640,                               # 增大图像尺寸，帮助网络看到更清晰的小球[citation:1]
    batch=16,                                # 根据GPU显存调整
    device=0,                                # 0 for GPU, 'cpu' for CPU
    patience=50,                             # 早停耐心值，防止过拟合
    project='golf_detection',                # 项目名称
    name='yolo11n_exp1',                     # 实验名称
    pretrained=True,                          # 使用预训练权重（强烈建议开启）[citation:3]
    save=True,           # Save the best weights
    plots=True           # Generate training charts
)


# 3. 使用训练好的模型进行检测
detector = YOLOWorld("runs/detect/train/weights/best.pt")
detector.set_classes(["golf ball"])
detector.save("golf_ball_final.pt")
# SAM 2/3 for high-precision masking
checkpoint = "sam2.1_hiera_large.pt"
model_cfg = "configs/sam2.1/sam2.1_hiera_l.yaml" # Path to the yaml in the sam2 repo

# --- 4. EXTRACT ALL FRAMES (Required for SAM 2/3 Tracking) ---
cap = cv2.VideoCapture("golf_swing.mp4")
frame_names = []
frame_idx = 0
while cap.isOpened():
    ret, frame = cap.read()
    if not ret: break
    frame_name = f"{frame_idx:05d}.jpg"
    cv2.imwrite(os.path.join(frames_dir, frame_name), frame)
    frame_names.append(frame_name)
    frame_idx += 1
cap.release()

# --- 5. RUN YOLO11 ON FRAME 1 ONLY ---
first_frame_path = os.path.join(frames_dir, "00000.jpg")
results = detector.predict(first_frame_path, conf=0.2, verbose=False)
if len(results[0].boxes) == 0:
    raise ValueError("Golf ball not found in first frame. Try lowering 'conf' or changing prompt.")

# Get the first box found by YOLO [x1, y1, x2, y2]
input_box = results[0].boxes.xyxy[0].cpu().numpy()

# --- 6. INITIALIZE SAM 2 TRACKER ---
# This is a deep-learning-based visual tracker. It uses GPU memory and image features to "remember" what the object looks like.
# It does not use a Kalman Filter (which uses math/physics to predict velocity and trajectory).
masker = build_sam2_video_predictor(model_cfg, checkpoint)
with torch.inference_mode(), torch.autocast("cuda", dtype=torch.bfloat16):
    inference_state = masker.init_state(video_path=frames_dir)

    # Add the YOLO box as the "starting prompt" for Object ID 0
    _, out_obj_ids, out_mask_logits = masker.add_new_points_or_box(
        inference_state=inference_state,
        frame_idx=0,
        obj_id=0,
        box=input_box,
    )

    # --- 7. PROPAGATE (THE FAST PART) ---
    # This loop runs the SAM 2 memory-tracking algorithm.
    # Create output video from tracked segments
    output_video = cv2.VideoWriter("tracked_golf_ball.mp4", cv2.VideoWriter_fourcc(*'mp4v'), 200,
                                   (frame.shape[1], frame.shape[0]))
    # It does NOT run YOLO anymore. It uses the "memory" of the ball.
    video_segments = {}
    for out_frame_idx, out_obj_ids, out_mask_logits in masker.propagate_in_video(inference_state):
        # Load the original frame
        frame = cv2.imread(os.path.join(frames_dir, frame_names[out_frame_idx]))

        # Process each object mask (we only have one: ID 0)
        for i, obj_id in enumerate(out_obj_ids):
            # Convert logits to a binary mask (True/False)
            mask = (out_mask_logits[i] > 0.0).cpu().numpy().squeeze()

            # Create a colored overlay (e.g., Green)
            color_mask = np.zeros_like(frame, dtype=np.uint8)
            color_mask[mask] = [0, 255, 0]  # Green in BGR

            # Blend the mask with the frame (Alpha blending)
            alpha = 0.4
            frame = cv2.addWeighted(frame, 1.0, color_mask, alpha, 0)

            # Optional: Draw the tracking box based on the mask
            # y, x = np.where(mask)
            # if len(x) > 0:
            #    cv2.rectangle(frame, (x.min(), y.min()), (x.max(), y.max()), (0, 255, 0), 2)

        output_video.write(frame)

    output_video.release()
    print("Success! Video saved as tracked_golf_ball.mp4")

