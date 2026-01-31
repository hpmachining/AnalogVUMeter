#pragma once

class VUBallistics;

struct VuReferenceOptions {
    double referenceDbfs = -18.0;
    bool referenceDbfsOverride = false;

    // 0 = system output, 1 = microphone
    int deviceType = 0;
};

struct VuAudioDspState {
    float prevL = 0.0f;
    float prevR = 0.0f;

    float rmsL_smooth = 0.0f;
    float rmsR_smooth = 0.0f;

    bool meterAwake = false;
};

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
                                       float& outVuR);
