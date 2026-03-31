import numpy as np


class SpinAxisCalculator:
    def __init__(self, radar_pitch_deg, freq_ghz=10.5, receiver_spacing_mm=60):
        """
        Initialize with hardware constants.
        radar_pitch_deg: Angle radar is tilted UP (degrees).
        """
        self.LAMBDA_MM = (300 / freq_ghz) * 10  # Speed of light / freq in mm
        self.D_MM = receiver_spacing_mm

        # Create Rotation Matrices
        # Assuming Radar is at (0,0,0) tilted UP by pitch_deg.
        rad = np.radians(radar_pitch_deg)
        c, s = np.cos(rad), np.sin(rad)

        # Matrix to convert Radar_Coords -> World_Coords
        self.R_radar_to_world = np.array([
            [c, 0, -s],
            [0, 1, 0],
            [s, 0, c]
        ])

        # Matrix to convert World_Coords -> Radar_Coords (Transpose/Inverse)
        self.R_world_to_radar = self.R_radar_to_world.T

    def phase_to_position(self, phase_diff_rad, ball_range_mm):
        """
        Converts phase difference to a physical Y or Z position on the ball (Monopulse).
        # Vertical Pair Phase Difference
        # Horizontal Pair Phase Difference
        # delta phi local = delta bin - delta center
        # delta phi local = 2pi * sin(alpha) * D / lambda
        Formula: sin(alpha) = (delta phi local * lambda) / (2 * pi * D)
        sin(alpha) * r = delta y
        """
        sine_alpha = (phase_diff_rad * self.LAMBDA_MM) / (2 * np.pi * self.D_MM)

        # Clamp value to [-1, 1] to avoid math domain errors if noise occurs
        sine_alpha = np.clip(sine_alpha, -1.0, 1.0)

        # Position = Range * sin(alpha)
        # (Using Small Angle Approximation, this is effectively Range * alpha)
        return ball_range_mm * sine_alpha

    def calculate_axis(self, velocity_world_vector, doppler_bins):
        """
        Main calculation logic.
        velocity_world_vector: [vx, vy, vz] in m/s (World Frame)
        doppler_samples: List of dicts {'range': mm, 'd_phi_h': rad, 'd_phi_v': rad}
        """

        # --- STEP 1: Transform Velocity World -> Radar ---
        # We need velocity in the Radar Frame to apply the "No Gyro" logic
        # which depends on the Line of Sight (X-axis of radar).
        v_world = np.array(velocity_world_vector)
        v_radar = self.R_world_to_radar @ v_world

        vx_r, vy_r, vz_r = v_radar

        # --- STEP 2: Determine Principal Axis (n) in Radar Frame ---
        # Convert phase data to Y, Z points
        points_yz = []
        # Convert phases to physical offsets
        for bin_data in doppler_bins:
            r = bin_data['range']

            # Calculate Y from Horizontal Pair Phase Diff
            y = self.phase_to_position(bin_data['d_phi_h'], r)

            # Calculate Z from Vertical Pair Phase Diff
            z = self.phase_to_position(bin_data['d_phi_v'], r)

            # Y=Range×sin(Horizontal Angle)
            # Z=Range×sin(Vertical Angle)
            points_yz.append([y, z])

        points_yz = np.array(points_yz)

        # Fit a vector (line) through these points.
        # Ideally use PCA or Linear Regression. For simplicity here:
        # We subtract the last point from the first point to get the direction vector.
        if len(points_yz) < 2:
            return None  # Not enough data

        # Alternatively, simpler approach for 2D line from top of the ball to bottom of the ball in terms of spin:
        dy = points_yz[-1][0] - points_yz[0][0]
        dz = points_yz[-1][1] - points_yz[0][1]
        length = np.sqrt(dy ** 2 + dz ** 2)

        if length == 0: return None

        ny = dy / length
        nz = dz / length

        # The Principal Axis Vector n in 3D (Radar Frame) is [0, ny, nz]
        # (It lies in the plane perpendicular to X)

        # --- STEP 3: "No Gyro" Calculation ---
        # We need vector u (spin axis) perpendicular to n_vec AND perpendicular to v_radar.

        # 3a. Perpendicular to n_vec in YZ plane, YZ spin axis is perpendicular to direction YZ is traveling
        # project 3d to yz plane to understand
        uy = nz
        uz = -ny

        # 3b. Solve for ux using Dot Product (u . v = 0)
        # The "No Gyro" assumption states that the Spin Axis ( u ) must be perpendicular to the Velocity Vector ( V).
        # ux*vx + uy*vy + uz*vz = 0
        # ux = -(uy*vy + uz*vz) / vx

        if abs(vx_r) < 1e-5:
            # Guard against divide by zero (ball moving purely 90 deg to radar)
            ux = 0
        else:
            ux = -1 * (uy * vy_r + uz * vz_r) / vx_r

        u_radar = np.array([ux, uy, uz])

        # Normalize the Spin Axis Vector
        u_radar = u_radar / np.linalg.norm(u_radar)

        # --- STEP 4: Transform Axis Radar -> World ---
        u_world = self.R_radar_to_world @ u_radar

        return u_world

# ==========================================
#  SIMULATION / EXAMPLE USAGE
# ==========================================

if __name__ == "__main__":
    # 1. SETUP: Radar tilted up 20 degrees
    calculator = SpinAxisCalculator(radar_pitch_deg=20.0)

    # 2. INPUT: Velocity from Socket (World Coordinates)
    # Ball moving away (40), slightly right (2), and UP (15) due to launch
    socket_velocity = [40.0, 2.0, 15.0]

    # 3. INPUT: Doppler Phase Data (Simulated)
    # Vertical Pair Phase Difference between receiver pairs
    # Horizontal Pair Phase Difference between receiver pairs
    # delta phi = 2pi * sin(alpha) * D / lambda

    # CRITICAL FIX: The bins must describe a LINE across the ball.
    # Bin 0: Bottom/Left of ball (Low Freq)
    # Bin 4: Top/Right of ball (High Freq)
    
    simulated_bins = [
        # Low Freq (Bottom-Left) -> Negative Phase
        {'range': 4000, 'd_phi_h': np.radians(-2.0), 'd_phi_v': np.radians(-5.0)},
        {'range': 4000, 'd_phi_h': np.radians(-1.0), 'd_phi_v': np.radians(-2.5)},
        # Center Freq -> Zero Phase
        {'range': 4000, 'd_phi_h': np.radians(0.0),  'd_phi_v': np.radians(0.0)},
        # High Freq (Top-Right) -> Positive Phase
        {'range': 4000, 'd_phi_h': np.radians(1.0),  'd_phi_v': np.radians(2.5)},
        {'range': 4000, 'd_phi_h': np.radians(2.0),  'd_phi_v': np.radians(5.0)},
    ]
    # 3. Calculate
    spin_axis_world = calculator.calculate_axis(socket_velocity, simulated_bins)

    # 4. Results
    # Check "No Gyro" Logic (Axis should be perp to Velocity)
    ortho_check = np.dot(spin_axis_world, socket_velocity)
    print(f"\nOrthogonality Check (World): {ortho_check:.6f} (Target: 0.0)")
