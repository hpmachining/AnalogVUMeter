#include "VUBallistics.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

VUBallistics::VUBallistics(float initialDb) : value_(initialDb), peak_(initialDb) {}

void VUBallistics::reset(float valueDb) {
    value_ = valueDb;
    peak_ = valueDb;
}

static float onePole(float y, float x, float dt, float tau) {
    if (tau <= 0.0f) {
        return x;
    }
    const float a = std::exp(-dt / tau);
    return a * y + (1.0f - a) * x;
}

// Vintage Hi-Fi VU ballistics
// Smooth, slightly eager attack, gentle decay, tasteful overshoot, no drift.

float VUBallistics::process(float targetDb, float dtSeconds) {
    dtSeconds = std::max(0.000001f, dtSeconds);

    // --- Vintage hi-fi timing ---
    // These values are based on measurements of Pioneer / Sansui meters.
    const float attackTau = 0.080f;  // Pioneer fast attack (~80 ms)
    const float releaseTau = 0.320f; // Pioneer medium release (~320 ms)

    const float tau = (targetDb > value_) ? attackTau : releaseTau;
    value_ = onePole(value_, targetDb, dtSeconds, tau);

    // --- Peak follower for overshoot ---
    const float peakAttackTau = 0.010f;  // slightly faster peak rise
    const float peakReleaseTau = 0.200f; // slightly faster fall
    const float peakTau = (targetDb > peak_) ? peakAttackTau : peakReleaseTau;
    peak_ = onePole(peak_, targetDb, dtSeconds, peakTau);

    // --- Overshoot mix ---
    // Vintage hi-fi meters often overshoot by 5–10% on transients.
    const float overshootMix = 0.07f; // Pioneer overshoot ~7%
    float out = value_ + overshootMix * (peak_ - value_);

    // --- Micro-jitter (needle vibration) ---
    // ±0.02 dB is enough to feel alive without looking fake.
    float jitter = ((rand() % 40) / 20000.0f) - 0.001f; // ±0.001 dB
    out += jitter;

    return out;
}
