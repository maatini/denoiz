#pragma once

#include "nlm_core.h"
#include <deque>

struct TemporalConfig {
    int frame_count = 3;
    float temporal_weight = 0.8f;
};

// Causal temporal denoiser: denoises the current frame using
// a ring buffer of previously seen frames (no look-ahead latency).
class TemporalDenoiser {
public:
    TemporalDenoiser(const TemporalConfig& tc, const NlmParams& np);

    // Feed a new frame, get back the denoised version.
    Image denoise(const Image& current);

private:
    std::deque<Image> history;
    TemporalConfig config;
    NlmParams params;
};
