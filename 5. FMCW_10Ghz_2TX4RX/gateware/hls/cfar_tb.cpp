// ============================================================================
//  cfar_tb.cpp
//  Testbench for 2D CA-CFAR golf-target detector
//
//  Injects three synthetic targets into a noise floor and verifies:
//    1. Golf ball target  — weak RCS, fast range-Doppler bin
//    2. Club head target  — medium RCS, adjacent Doppler bin
//    3. Clutter return    — should be suppressed (below threshold)
//
//  Compile (outside Vivado, for quick iteration):
//    g++ -std=c++14 -I. cfar_tb.cpp cfar_detector.cpp -o cfar_tb && ./cfar_tb
// ============================================================================

#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include "cfar_detector.h"

// Pseudo-random noise (LCG) — deterministic for repeatability
static unsigned int lfsr_state = 0xDEADBEEF;
static inline unsigned int lcg_rand() {
    lfsr_state = lfsr_state * 1664525u + 1013904223u;
    return lfsr_state;
}

// Gaussian noise approximation (Box-Muller, single sample)
static float gauss_noise(float sigma) {
    float u1 = ((float)(lcg_rand() & 0xFFFF) + 0.5f) / 65536.0f;
    float u2 = ((float)(lcg_rand() & 0xFFFF) + 0.5f) / 65536.0f;
    float z  = sqrtf(-2.0f * logf(u1 + 1e-9f)) * cosf(2.0f * 3.14159265f * u2);
    return z * sigma;
}

int main() {
    std::cout << "=== CFAR Golf Target Detector Testbench ===\n\n";

    // ── Allocate range-Doppler map ────────────────────────────────────────
    static cell_t rd_map [DOPPLER_BINS][RANGE_BINS];
    static det_t  det_map[DOPPLER_BINS][RANGE_BINS];

    // ── Noise floor: exponential distribution (Rayleigh envelope → chi² power)
    // Mean noise power = 500 (arbitrary ADC units²).
    // We approximate with a simple uniform + small perturbation.
    const float NOISE_MEAN  = 500.0f;
    const float NOISE_SIGMA = NOISE_MEAN * 0.5f;   // ~50% variation

    std::cout << "Filling " << DOPPLER_BINS << "x" << RANGE_BINS
              << " range-Doppler map with Rayleigh noise...\n";

    for (int d = 0; d < DOPPLER_BINS; d++) {
        for (int r = 0; r < RANGE_BINS; r++) {
            float n = NOISE_MEAN + gauss_noise(NOISE_SIGMA);
            if (n < 0) n = 0;
            if (n > 65535) n = 65535;
            rd_map[d][r] = (cell_t)(unsigned int)n;
        }
    }

    // ── Inject targets ────────────────────────────────────────────────────
    // Target SNR rules-of-thumb for P_d > 0.9 at P_fa = 1e-5:
    //   Swerling 0 (non-fluctuating): SNR > ~13 dB → power ratio > 20
    //   Golf ball (small RCS) : SNR ~14 dB → ×25 over noise mean
    //   Club head (larger RCS): SNR ~17 dB → ×50 over noise mean

    // Target 1: Golf ball — range=512, doppler=64 (fast swing ~55 m/s)
    const int BALL_R = 512;
    const int BALL_D = 64;
    const float BALL_SNR = 25.0f;  // linear
    rd_map[BALL_D][BALL_R] = (cell_t)(unsigned int)(NOISE_MEAN * BALL_SNR);

    // Target 2: Club head — range=510, doppler=65 (similar velocity, larger)
    const int CLUB_R = 510;
    const int CLUB_D = 65;
    const float CLUB_SNR = 50.0f;  // linear (higher RCS)
    rd_map[CLUB_D][CLUB_R] = (cell_t)(unsigned int)(NOISE_MEAN * CLUB_SNR);

    // Target 3: Clutter spike (tree/grass) — only 3 dB above noise, should miss
    const int CLUT_R = 200;
    const int CLUT_D = 5;   // low Doppler = near-static
    const float CLUT_SNR = 2.0f;
    rd_map[CLUT_D][CLUT_R] = (cell_t)(unsigned int)(NOISE_MEAN * CLUT_SNR);

    std::cout << "Targets injected:\n";
    std::cout << "  Golf ball  : range=" << BALL_R << " doppler=" << BALL_D
              << " SNR=" << 10*log10f(BALL_SNR) << " dB\n";
    std::cout << "  Club head  : range=" << CLUB_R << " doppler=" << CLUB_D
              << " SNR=" << 10*log10f(CLUB_SNR) << " dB\n";
    std::cout << "  Clutter    : range=" << CLUT_R << " doppler=" << CLUT_D
              << " SNR=" << 10*log10f(CLUT_SNR) << " dB  (should not detect)\n\n";

    // ── Clear detection map ───────────────────────────────────────────────
    for (int d = 0; d < DOPPLER_BINS; d++)
        for (int r = 0; r < RANGE_BINS; r++)
            det_map[d][r] = 0;

    // ── Run CFAR ──────────────────────────────────────────────────────────
    ap_uint<16> alpha = (ap_uint<16>)ALPHA_INT;   // Q8.8 = 19.7
    std::cout << "Running cfar_detector()  alpha=" << ALPHA_FLOAT
              << " (Q8.8=" << ALPHA_INT << ")...\n";

    cfar_detector(rd_map, det_map, alpha);

    // ── Count detections ──────────────────────────────────────────────────
    int total_dets = 0;
    for (int d = 0; d < DOPPLER_BINS; d++)
        for (int r = 0; r < RANGE_BINS; r++)
            total_dets += (int)det_map[d][r];

    std::cout << "\nTotal detections: " << total_dets << "\n";
    std::cout << "Expected false-alarm rate: ~"
              << (float)total_dets / (float)TOTAL_CELLS * 100.0f << "% \n\n";

    // ── Verify target detections ──────────────────────────────────────────
    int pass = 1;

    auto check_det = [&](const char* name, int d, int r, bool expect) {
        bool got = (bool)det_map[d][r];
        bool ok  = (got == expect);
        std::cout << "  [" << (ok ? "PASS" : "FAIL") << "] "
                  << std::left << std::setw(12) << name
                  << " d=" << std::setw(4) << d << " r=" << std::setw(5) << r
                  << "  expected=" << expect << "  got=" << got << "\n";
        if (!ok) pass = 0;
    };

    std::cout << "Target detection checks:\n";
    check_det("Golf ball",  BALL_D, BALL_R, true);
    check_det("Club head",  CLUB_D, CLUB_R, true);
    check_det("Clutter",    CLUT_D, CLUT_R, false);

    // ── Print local detection map around targets ───────────────────────────
    std::cout << "\nDetection map around golf ball (±3 range, ±2 Doppler):\n";
    std::cout << "  D\\R ";
    for (int r = BALL_R-3; r <= BALL_R+3; r++)
        std::cout << std::setw(5) << r;
    std::cout << "\n";
    for (int d = BALL_D-2; d <= BALL_D+2; d++) {
        std::cout << "  " << std::setw(3) << d << " ";
        for (int r = BALL_R-3; r <= BALL_R+3; r++) {
            char c = det_map[d][r] ? 'X' : '.';
            std::cout << "    " << c;
        }
        std::cout << "\n";
    }

    std::cout << "\nDetection map around club head (±3 range, ±2 Doppler):\n";
    std::cout << "  D\\R ";
    for (int r = CLUB_R-3; r <= CLUB_R+3; r++)
        std::cout << std::setw(5) << r;
    std::cout << "\n";
    for (int d = CLUB_D-2; d <= CLUB_D+2; d++) {
        std::cout << "  " << std::setw(3) << d << " ";
        for (int r = CLUB_R-3; r <= CLUB_R+3; r++) {
            char c = det_map[d][r] ? 'X' : '.';
            std::cout << "    " << c;
        }
        std::cout << "\n";
    }

    // ── Final result ──────────────────────────────────────────────────────
    std::cout << "\n" << std::string(44, '=') << "\n";
    std::cout << "Result: " << (pass ? "ALL TESTS PASSED" : "*** FAILURES ***") << "\n";
    std::cout << std::string(44, '=') << "\n";

    return pass ? 0 : 1;
}
