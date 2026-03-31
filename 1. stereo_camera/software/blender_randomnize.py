import bpy
import bpy_extras
import os
import math
import mathutils

# ================= CONFIGURATION =================
OUTPUT_DIR = "/dataset"
BG_IMAGES_DIR = "/backgrounds"

# Object Names
CAM_LEFT_NAME = "Camera_Left"
CAM_RIGHT_NAME = "Camera_Right"
BALL_NAME = "Sphere"
BG_PLANE_NAME = "Plane"
SUN_LIGHT_NAME = "Light"

# Settings
FRAMES_PER_SEQ = 20
FPS = 240
BALL_SPEED_MPH = 140
TEST_ANGLES = [15, 40]
NOISE_FACTOR = 0.15  # Intensity of simulated sensor noise (0.0 to 1.0)
# =================================================

# Ensure directories exist
left_dir = os.path.join(OUTPUT_DIR, "left_frames")
right_dir = os.path.join(OUTPUT_DIR, "right_frames")
os.makedirs(left_dir, exist_ok=True)
os.makedirs(right_dir, exist_ok=True)

scene = bpy.context.scene
ball = bpy.data.objects[BALL_NAME]
ball.animation_data_clear()  # Clear old keyframes


def setup_sensor_noise():
    scene.use_nodes = True
    tree = scene.node_tree

    # Clear default nodes
    for node in tree.nodes:
        tree.nodes.remove(node)

    # Create Nodes
    rl_node = tree.nodes.new(type="CompositorNodeRLayers")
    noise_node = tree.nodes.new(type="CompositorNodeTexture")

    # Create a Noise Texture if it doesn't exist
    if "SensorNoise" not in bpy.data.textures:
        bpy.data.textures.new("SensorNoise", type='NOISE')
    noise_node.texture = bpy.data.textures["SensorNoise"]

    # Mix Node (Overlay noise onto image)
    mix_node = tree.nodes.new(type="CompositorNodeMixRGB")
    mix_node.blend_type = 'MULTIPLY'  # Darken slightly with grain
    mix_node.inputs[0].default_value = NOISE_FACTOR  # Factor

    # Output
    comp_node = tree.nodes.new(type="CompositorNodeComposite")

    # Link Nodes
    tree.links.new(rl_node.outputs['Image'], mix_node.inputs[1])
    tree.links.new(noise_node.outputs['Color'], mix_node.inputs[2])
    tree.links.new(mix_node.outputs['Image'], comp_node.inputs['Image'])


def get_bbox(scene, cam, obj):
    # (Same math as previous step)
    bbox_corners = [obj.matrix_world @ mathutils.Vector(corner) for corner in obj.bound_box]
    co_2d = [bpy_extras.object_utils.world_to_camera_view(scene, cam, c) for c in bbox_corners]

    min_x = max(0.0, min(1.0, min([c.x for c in co_2d])))
    max_x = max(0.0, min(1.0, max([c.x for c in co_2d])))
    min_y = max(0.0, min(1.0, min([c.y for c in co_2d])))
    max_y = max(0.0, min(1.0, max([c.y for c in co_2d])))

    return min_x, max_x, min_y, max_y


def apply_trajectory(angle_deg):
    """Calculates path for 0-20 frames at specific angle and locks it in."""
    ball.animation_data_clear()

    # Physics Constants
    v_total = BALL_SPEED_MPH * 0.44704  # m/s
    g = 9.81
    rad = math.radians(angle_deg)

    v_z = v_total * math.sin(rad)  # Up
    v_y = v_total * math.cos(rad)  # Forward

    # Generate Keyframes
    for f in range(FRAMES_PER_SEQ + 1):
        t = f / FPS

        # Kinematics
        loc_y = v_y * t
        loc_z = (v_z * t) - (0.5 * g * t ** 2)

        ball.location = (0, loc_y, loc_z)  # X is 0 (centered)
        ball.keyframe_insert(data_path="location", frame=f)


def set_background(img_filename):
    """Forces the background plane to use a specific image."""
    bg_obj = bpy.data.objects.get(BG_PLANE_NAME)
    img_path = os.path.join(BG_IMAGES_DIR, img_filename)

    if bg_obj and os.path.exists(img_path):
        mat = bg_obj.active_material
        if mat and mat.use_nodes:
            # Find or Create Texture Node
            tex_node = next((n for n in mat.node_tree.nodes if n.type == 'TEX_IMAGE'), None)
            if not tex_node:
                tex_node = mat.node_tree.nodes.new('ShaderNodeTexImage')
                bsdf = mat.node_tree.nodes.get("Principled BSDF")
                mat.node_tree.links.new(tex_node.outputs['Color'], bsdf.inputs['Base Color'])

            # Load Image
            try:
                img_block = bpy.data.images.load(img_path)
                tex_node.image = img_block
            except:
                print(f"Failed to load {img_filename}")


# ============================================================================================================
setup_sensor_noise()
bg_files = [f for f in os.listdir(BG_IMAGES_DIR) if f.lower().endswith(('jpg', 'png', 'jpeg'))]
cameras = [(CAM_LEFT_NAME, left_dir), (CAM_RIGHT_NAME, right_dir)]

for angle in TEST_ANGLES:
    print(f"--- Setting up Angle: {angle} Degrees ---")
    apply_trajectory(angle)  # Set physics once for this angle

    # --- MIDDLE LOOP: BACKGROUNDS ---
    for bg_idx, bg_file in enumerate(bg_files):
        print(f"   Using Background: {bg_file}")
        set_background(bg_file)

        # --- INNER LOOP: FRAMES ---
        for frame in range(FRAMES_PER_SEQ + 1):
            scene.frame_set(frame)

            for cam_name, save_folder in cameras:
                cam_obj = bpy.data.objects[cam_name]
                scene.camera = cam_obj

                # Naming Convention: angle_15_bg_01_frame_05.jpg
                fname = f"angle_{angle}_bg_{bg_idx:02d}_frame_{frame:02d}"

                # Render
                scene.render.filepath = os.path.join(save_folder, fname + ".jpg")
                scene.render.image_settings.file_format = 'JPEG'
                bpy.ops.render.render(write_still=True)

                # Label
                min_x, max_x, min_y, max_y = get_bbox(scene, cam_obj, ball)
                w, h = max_x - min_x, max_y - min_y

                if w > 0 and h > 0:
                    xc, yc = min_x + w / 2, 1.0 - (min_y + h / 2)
                    with open(os.path.join(save_folder, fname + ".txt"), 'w') as f:
                        f.write(f"0 {xc:.6f} {yc:.6f} {w:.6f} {h:.6f}\n")
