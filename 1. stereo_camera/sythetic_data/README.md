
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