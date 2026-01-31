#include "VuAudioDsp.h"

#include <algorithm>
#include <cmath>

#include "VUBallistics.h"

void processInterleavedFloatAudioToVuDb(const float* data,
                                       unsigned int frames,
                                       unsigned int channels,
                                       float sampleRate,
                                       const VuReferenceOptions& ref,
                                       VUBallistics& ballisticsL,
                                       VUBallistics& ballisticsR,
                                       VuAudioDspState& state,
                                       float minVu,
                                       float maxVu,
                                       float& outVuL,
                                       float& outVuR) {
    if (!data || frames == 0 || channels == 0 || sampleRate <= 0.0f) {
        outVuL = minVu;
        outVuR = minVu;
        return;
    }

    // --- Compute raw RMS for this buffer ---
    double sumL = 0.0;
    double sumR = 0.0;

    for (unsigned int i = 0; i < frames; ++i) {
        const float rawL = data[i * channels + 0];
        const float rawR = (channels > 1) ? data[i * channels + 1] : rawL;

        // Transient pre-emphasis (very subtle)
        const float l = rawL + 0.15f * (rawL - state.prevL);
        const float r = rawR + 0.15f * (rawR - state.prevR);

        state.prevL = rawL;
        state.prevR = rawR;

        sumL += static_cast<double>(l) * static_cast<double>(l);
        sumR += static_cast<double>(r) * static_cast<double>(r);
    }

    const float rmsL = std::sqrt(static_cast<float>(sumL / frames));
    const float rmsR = std::sqrt(static_cast<float>(sumR / frames));

    // --- Vintage VU RMS integration (250 ms) ---
    const float wakeThreshold = 0.002f; // about -54 dBFS

    if (rmsL > wakeThreshold) {
        state.rmsL_smooth = rmsL * rmsL;
    }
    if (rmsR > wakeThreshold) {
        state.rmsR_smooth = rmsR * rmsR;
    }

    const float vuTau = 0.020f;
    float dt = static_cast<float>(frames) / sampleRate;
    dt = std::min(dt, 0.050f); // clamp to 50 ms
    const float alpha = std::exp(-dt / vuTau);

    state.rmsL_smooth = alpha * state.rmsL_smooth + (1.0f - alpha) * (rmsL * rmsL);
    state.rmsR_smooth = alpha * state.rmsR_smooth + (1.0f - alpha) * (rmsR * rmsR);

    float rmsL_vu = std::sqrt(state.rmsL_smooth);
    float rmsR_vu = std::sqrt(state.rmsR_smooth);

    // --- Noise floor applied to smoothed RMS ---
    const float noiseFloor = 0.001f;
    if (rmsL_vu < noiseFloor) {
        rmsL_vu = 0.0f;
    }
    if (rmsR_vu < noiseFloor) {
        rmsR_vu = 0.0f;
    }

    // --- Convert to dBFS ---
    const float eps = 1e-12f;
    const float dbfsL = 20.0f * std::log10(std::max(rmsL_vu, eps));
    const float dbfsR = 20.0f * std::log10(std::max(rmsR_vu, eps));

    // --- Reference level for hi-fi VU behavior ---
    float effectiveRefDbfs;
    if (ref.referenceDbfsOverride) {
        effectiveRefDbfs = static_cast<float>(ref.referenceDbfs);
    } else if (ref.deviceType == 1) {
        // Microphone mode
        effectiveRefDbfs = -0.0f;
    } else {
        // System output mode
        effectiveRefDbfs = -14.0f;
    }

    const float targetVuL = dbfsL - effectiveRefDbfs;
    const float targetVuR = dbfsR - effectiveRefDbfs;

    if (!state.meterAwake && (rmsL_vu > 0.002f || rmsR_vu > 0.002f)) {
        ballisticsL.reset(targetVuL);
        ballisticsR.reset(targetVuR);
        state.meterAwake = true;
    }

    // --- Apply ballistics using per-callback dt ---
    float vuL = ballisticsL.process(targetVuL, dt);
    float vuR = ballisticsR.process(targetVuR, dt);

    // --- Clamp to meter scale ---
    vuL = std::clamp(vuL, minVu, maxVu);
    vuR = std::clamp(vuR, minVu, maxVu);

    outVuL = vuL;
    outVuR = vuR;
}
