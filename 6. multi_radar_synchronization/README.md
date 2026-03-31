When deploying a multi-static system—such as placing four radar units at each corner of a ceiling—the primary challenge is maintaining phase alignment across all nodes. 

### The Hardware Solution: Distributed PLLs with a Shared Master Clock

To make 4 distributed radars act as one giant coherent MIMO array, their internal PLLs must be locked to the **exact same reference frequency and trigger pulse**.

**The Architecture:**

1. **Master Clock Generator:** Place a highly stable crystal oscillator (TCXO 40 MHz) in a central hub.
2. **Clock Distribution Network:** Use identical-length, phase-stable coaxial cables to send this 40 MHz reference clock to the `CLK_IN` pin of all 4 radar chips.
3. **Sync Pulse (SYSREF):** Send a steep-edge trigger pulse (over matched cables) to the `SYNC_IN` pins of all 4 radars to tell their PLLs to start the FMCW frequency ramp at the exact same picosecond.
4. **The PLLs:** The internal Fractional-N PLL inside each radar chip will take the shared 40 MHz clock and multiply it up to 77 GHz. Because they share the same reference, their 77 GHz chirps will be perfectly locked in frequency.

---

### The Software Solution: Distributed PLL Phase Calibration Algorithm

Even with perfectly matched hardware, a 1-millimeter difference in cable length or a slight temperature difference in the PLLs will cause a static phase shift ($\Delta \phi$) between the 4 corners. At 77 GHz, the wavelength $\lambda$ is $3.89$ mm. A $1$ mm cable difference ruins the coherent MIMO beamforming.

### Algorithm Example: Joint MIMO Phase Synchronization

This algorithm is run once at system startup (or periodically) using a "Corner Reflector" (a highly reflective metal object) placed at a known exact location in the room.

**Step 1: Define the Geometry**
Let the 4 radars be at known coordinates $\mathbf{p}_1, \mathbf{p}*2, \mathbf{p}3, \mathbf{p}4$.
Place the calibration target at known coordinate $\mathbf{p}{target}$.
The true geometric distance from radar $i$ to the target and back to radar $j$ (for a Tx-Rx pair) is:
$$ d{ij}^{true} = ||\mathbf{p}*{target} - \mathbf{p}_i||*2 + ||\mathbf{p}*{target} - \mathbf{p}_j||_2 $$

**Step 2: Calculate the Theoretical Phase**
Based on the exact geometry, the theoretical phase of the signal transmitted by radar $i$ and received by radar $j$ should be:
$$ \phi_{ij}^{true} = \mod \left( \frac{2\pi \cdot d_{ij}^{true}}{\lambda} , 2\pi \right) $$

**Step 3: Measure the Actual Phase**
Fire the synchronized FMCW chirps. Run the 1D Range-FFT. Find the peak corresponding to the calibration target. Extract the measured phase for every Tx-Rx combination across the 4 corners:
$$ \phi_{ij}^{measured} = \angle Y_{ij}(k_{target}) $$
*(Where $Y_{ij}(k)$ is the FFT output at the target's range bin).*

**Step 4: Compute the PLL/Cable Phase Residuals**
The difference between the measured phase and the theoretical phase is the synchronization error caused by the distributed PLLs and cables:
$$ \Delta \phi_{ij} = \phi_{ij}^{measured} - \phi_{ij}^{true} $$

**Step 5: Apply the Calibration Matrix (Real-Time Correction)**
Create a complex calibration matrix $\mathbf{C}$ where $C_{ij} = e^{-j \Delta \phi_{ij}}$.
During real-time vital sign monitoring, multiply your incoming raw data matrix $\mathbf{Y}*{raw}$ by this calibration matrix:
$$ \mathbf{Y}*{synced} = \mathbf{Y}_{raw} \odot \mathbf{C} $$
*(Where $\odot$ is the Hadamard/element-wise product).*

Now, your 4 distributed radars are acting as a **single, perfectly synchronized, massive coherent MIMO array**. 