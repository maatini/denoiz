#include "nlm_video_scene.h"

#include <arm_neon.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// Compute the mean absolute difference (MAD) between two images.
// Returns the average |a - b| across all channels, in [0,1].
// Uses NEON vabdq_f32 (absolute difference) + vaddvq_f32 (horizontal sum)
// for the inner loop over 3-channel pixels.
static float compute_mad(const Image& a, const Image& b) {
    int w = a.width, h = a.height, c = a.channels;
    size_t n = (size_t)w * h * c;
    const float* ad = a.data.data();
    const float* bd = b.data.data();

    // NEON loop processes 4 floats at a time (4-wide SIMD).
    float32x4_t sum_vec = vdupq_n_f32(0);
    size_t i = 0;
    size_t vec_end = (n / 4) * 4;
    for (; i < vec_end; i += 4) {
        float32x4_t va = vld1q_f32(ad + i);
        float32x4_t vb = vld1q_f32(bd + i);
        sum_vec = vaddq_f32(sum_vec, vabdq_f32(va, vb));
    }
    float sum = vaddvq_f32(sum_vec);

    // Scalar tail (up to 3 remaining values).
    for (; i < n; ++i) {
        sum += std::fabs(ad[i] - bd[i]);
    }

    return sum / (float)n;
}

// Build per-channel histograms with fixed bin width.
// For values in [0,1] with `bins` bins, bin width = 1.0/bins.
// Returns a flat array of bins * 3 ints (R, G, B concatenated).
static std::vector<int> build_histogram(const Image& img, int bins) {
    std::vector<int> hist(bins * 3, 0);
    int w = img.width, h = img.height;
    float bin_width = 1.0f / bins;
    const float* data = img.data.data();
    size_t n = (size_t)w * h * 3;

    for (size_t i = 0; i < n; i += 3) {
        for (int ch = 0; ch < 3; ++ch) {
            float v = data[i + ch];
            int b = (int)(v / bin_width);
            if (b < 0) b = 0;
            if (b >= bins) b = bins - 1;
            hist[ch * bins + b]++;
        }
    }
    return hist;
}

// Compute histogram intersection: sum(min(ha[i], hb[i])) / total_pixels.
// Range [0, 1] — 1.0 means identical histograms, 0.0 means disjoint.
static float histogram_intersection(const std::vector<int>& ha,
                                     const std::vector<int>& hb,
                                     int total_pixels) {
    long long inter = 0;
    for (size_t i = 0; i < ha.size(); ++i) {
        inter += std::min(ha[i], hb[i]);
    }
    // Each pixel contributes to 3 channels in the histogram.
    return (float)inter / (float)(total_pixels * 3);
}

bool detect_scene_cut(const Image& prev, const Image& current,
                      const SceneDetectionConfig& config) {
    // Guard: dimensions must match.
    if (prev.width != current.width || prev.height != current.height ||
        prev.channels != current.channels || prev.channels < 3) {
        return true; // treat mismatch as a cut
    }

    // 1. Mean absolute difference check.
    float mad = compute_mad(prev, current);
    if (mad < config.mad_threshold) {
        return false; // not enough pixel-level change
    }

    // 2. Histogram intersection check.
    // MAD was high — verify with histogram to rule out fast motion.
    int total_pixels = prev.width * prev.height;
    auto hp = build_histogram(prev, config.histogram_bins);
    auto hc = build_histogram(current, config.histogram_bins);
    float inter = histogram_intersection(hp, hc, total_pixels);

    return inter < config.histogram_threshold;
}
