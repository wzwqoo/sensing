#!/usr/bin/env python3
"""
Generate Vivado FIR Compiler .coe coefficient files for:
  1. 100-tap bandpass FIR  (150 Hz – 10 kHz, Hamming window)
  2. 101-tap Hilbert FIR   (Parks-McClellan equiripple, Type-III)

Usage:
    python3 generate_vivado_coe.py

Outputs:
    fir_bandpass.coe   — load into FIR Compiler IP, Bandpass mode
    hilbert_fir.coe    — load into FIR Compiler IP, Hilbert mode

Vivado FIR Compiler IP settings to match:
    Coefficient type    : Signed
    Quantization        : Integer Coefficients
    Coefficient width   : 18
    Input/output width  : 16 (adjust to your ADC width)
    Number of taps      : 101 (bandpass) / 101 (Hilbert)
    Filter type         : Single Rate
    For Hilbert only    -> set "Hilbert" under Filter Type tab
"""

import numpy as np
from scipy.signal import firwin, remez

# ── User parameters ───────────────────────────────────────────────────────────
FS           = 48000.0   # Hz — match your ADC sample rate
HP_HZ        = 150.0     # bandpass lower edge (Hz)
LP_HZ        = 10000.0   # bandpass upper edge (Hz)
NUM_TAPS     = 101       # bandpass filter length (must be odd)
HILB_TAPS    = 101       # Hilbert filter length  (must be odd)
FRAC_BITS    = 16        # Q-format fractional bits (scale = 2^FRAC_BITS)
COEFF_WIDTH  = 18        # Vivado FIR Compiler coefficient bit-width


# ── Helpers ───────────────────────────────────────────────────────────────────
def float_to_fixed(coeffs, frac_bits, coeff_width):
    """Quantise floating-point FIR coefficients to signed fixed-point integers."""
    scale   = 2 ** frac_bits
    max_val =  (2 ** (coeff_width - 1)) - 1
    min_val = -(2 ** (coeff_width - 1))
    q = np.round(coeffs * scale).astype(int)
    clipped = np.clip(q, min_val, max_val)
    n_clip  = np.sum(np.abs(q) > max_val)
    if n_clip:
        print(f"  [warn] {n_clip} coeff(s) clipped — reduce FRAC_BITS or COEFF_WIDTH")
    return clipped


def write_coe(filename, coeffs_int, radix=10):
    """
    Write a Xilinx .coe file for the Vivado FIR Compiler IP block.

    File format
    -----------
    radix = <10|16>;
    coefdata =
      c0,
      c1,
      ...
      cN;

    Notes
    -----
    * radix=10  → signed decimal  (easiest to debug)
    * radix=16  → unsigned hex    (two's complement, no minus signs)
    * The semicolon on the LAST coefficient terminates the coefdata block.
    * Vivado ignores blank lines and comment lines starting with ';'
    """
    with open(filename, "w") as f:
        f.write("; Xilinx FIR Compiler coefficient file (.coe)\n")
        f.write(f"; Taps: {len(coeffs_int)} | Frac bits: {frac_bits} "
                f"| Coeff width: {coeff_width}\n;\n")
        f.write(f"radix = {radix};\n")
        f.write("coefdata =\n")
        for i, c in enumerate(coeffs_int):
            sep = "," if i < len(coeffs_int) - 1 else ";"
            f.write(f"  {c}{sep}\n")
    print(f"Wrote {filename}  ({len(coeffs_int)} taps)")


# ── 1. Bandpass FIR ───────────────────────────────────────────────────────────
nyq       = FS / 2.0
bp_coeffs = firwin(
    NUM_TAPS,
    [HP_HZ / nyq, LP_HZ / nyq],
    pass_zero=False,   # bandpass (not low-pass)
    window="hamming",
    fs=2.0,            # normalised so Nyquist = 1.0
)
bp_fixed = float_to_fixed(bp_coeffs, FRAC_BITS, COEFF_WIDTH)
write_coe("fir_bandpass.coe", bp_fixed)


# ── 2. Hilbert FIR (Parks-McClellan equiripple, Type-III) ────────────────────
# remez with type="hilbert" gives antisymmetric coefficients with h[n]=0
# on even-indexed taps.  f_lo keeps DC ripple-free; f_hi avoids Nyquist.
hilb_coeffs = remez(
    HILB_TAPS,
    [50.0 / nyq, 0.98],   # passband: 50 Hz to 98% of Nyquist
    [1.0],
    type="hilbert",
    fs=2.0,
)
hilb_fixed = float_to_fixed(hilb_coeffs, FRAC_BITS, COEFF_WIDTH)
write_coe("hilbert_fir.coe", hilb_fixed)


print("\nDone. Load the .coe files in Vivado FIR Compiler IP:")
print("  fir_bandpass.coe  → Filter Type: Single Rate, Bandpass")
print("  hilbert_fir.coe   → Filter Type: Hilbert")
