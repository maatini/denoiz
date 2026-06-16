#pragma once

#include "nlm_core.h"

// Configuration for scene cut / shot boundary detection.
// Both MAD (pixel-level mean absolute difference) and histogram
// intersection must cross their respective thresholds for a cut
// to be declared. This dual-criterion approach prevents false
// positives from rapid camera pans (high MAD, similar histogram)
// and from lighting changes (different histogram, moderate MAD).
struct SceneDetectionConfig {
    float mad_threshold = 0.15f;       // mean absolute difference [0,1]
    float histogram_threshold = 0.30f; // histogram intersection minimum
    int histogram_bins = 64;           // bins per channel (fixed-width)
};

// Returns true if a scene cut is detected between consecutive frames.
// Checks both MAD (pixel-level brightness change) and RGB histogram
// intersection (color distribution change). Both criteria must be
// satisfied for a cut to be declared.
bool detect_scene_cut(const Image& prev, const Image& current,
                      const SceneDetectionConfig& config = {});
