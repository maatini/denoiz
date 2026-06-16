#pragma once

#include "nlm_core.h"
#include "nlm_video_motion.h"
#include "nlm_video_scene.h"

#include <deque>

// Configuration for temporal (multi-frame) denoising.
struct TemporalConfig {
    int frame_count = 3;               // number of temporal frames (1-7)
    float temporal_weight = 0.8f;      // weight decay per frame offset: w^age
    int lookahead = 0;                 // future frames (0 = causal, 1-3 for offline)
    bool motion_compensated = false;   // enable motion-compensated patch comparison
    MotionEstimationConfig motion;     // block-matching parameters
    SceneDetectionConfig scene_detect; // scene cut thresholds
};

// Motion-compensated temporal denoiser.
//
// Maintains a ring buffer of previous frames and their motion fields
// relative to the reference frame.  Compares patches at motion-
// compensated (warped) positions across frames rather than at the
// same spatial position.  This significantly reduces ghosting on
// scenes with camera or object motion.
//
// Supports two modes:
//   Causal (lookahead = 0): zero extra latency, past frames only.
//   Lookahead (lookahead > 0): buffers future frames for symmetric
//     temporal window.  Adds `lookahead` frames of latency.
class TemporalDenoiser {
public:
    TemporalDenoiser(const TemporalConfig& tc, const NlmParams& np);

    // Feed a new frame.  Returns the denoised result.
    // In lookahead mode, returns an Image with width == 0 when the
    // buffer is not yet full (caller should skip encoding for that cycle).
    Image denoise(const Image& current);

    // Drain remaining frames from the lookahead buffer at end of stream.
    // Call after the last frame has been fed.  Returns an empty Image
    // (width == 0) when the buffer is exhausted.
    Image flush();

private:
    // Run the motion-compensated temporal NLM on `reference` using
    // the frames in `history` and their pre-computed `flows` (motion
    // from each history frame → reference).
    Image process_frame(const Image& reference);

    std::deque<Image> history;       // past frames (oldest first)
    std::deque<MotionField> flows;   // motion: history[i] → reference
    Image previous_frame;            // for scene cut detection
    bool first_frame = true;

    std::deque<Image> pending;       // lookahead buffer (future frames)

    TemporalConfig config;
    NlmParams params;
};
