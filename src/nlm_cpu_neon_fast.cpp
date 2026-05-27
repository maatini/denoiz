#include "nlm_core.h"

#include <arm_neon.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <Accelerate/Accelerate.h>

#include <cmath>
#include <iostream>
#include <cstring>
#include <algorithm>

// --- vvExp-based batched weight computation ---
// Replaces scalar exp() calls with vectorized Accelerate exp().

static void compute_weights_batch(const float* ssd_vals, float* weights,
                                   int n, float h2_inv) {
    // weights = exp(-ssd * h2_inv)
    // Step 1: negate and scale: tmp = -ssd * h2_inv
    // Using vDSP_vsmul with negative scale
    float neg_h2_inv = -h2_inv;
    vDSP_vsmul(ssd_vals, 1, &neg_h2_inv, weights, 1, n);

    // Step 2: exp
    int n_int = n;
    vvexpf(weights, weights, &n_int);
}

// --- Downsampling ---

static void downsample_2x(const Image& src, Image& half) {
    int sw = src.width, sh = src.height, c = src.channels;
    int hw = (sw + 1) / 2, hh = (sh + 1) / 2;
    half.width = hw;
    half.height = hh;
    half.channels = c;
    half.data.resize(hw * hh * c);

    for (int y = 0; y < hh; ++y) {
        for (int x = 0; x < hw; ++x) {
            int sx0 = x * 2, sy0 = y * 2;
            for (int ch = 0; ch < c; ++ch) {
                float sum = 0;
                int cnt = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        int sx = std::min(sx0 + dx, sw - 1);
                        int sy = std::min(sy0 + dy, sh - 1);
                        sum += src.at(sx, sy, ch);
                        cnt++;
                    }
                }
                half.at(x, y, ch) = sum / cnt;
            }
        }
    }
}

// --- Upsampling (bilinear) ---

static void upsample_2x(const Image& src, Image& full, int target_w, int target_h) {
    int sw = src.width, sh = src.height, c = src.channels;
    full.width = target_w;
    full.height = target_h;
    full.channels = c;
    full.data.resize(target_w * target_h * c);

    float sx = (float)(sw - 1) / (target_w > 1 ? target_w - 1 : 1);
    float sy = (float)(sh - 1) / (target_h > 1 ? target_h - 1 : 1);

    for (int y = 0; y < target_h; ++y) {
        for (int x = 0; x < target_w; ++x) {
            float gx = x * sx;
            float gy = y * sy;
            int x0 = (int)gx, y0 = (int)gy;
            int x1 = std::min(x0 + 1, sw - 1);
            int y1 = std::min(y0 + 1, sh - 1);
            float fx = gx - x0;
            float fy = gy - y0;

            for (int ch = 0; ch < c; ++ch) {
                float v = src.at(x0, y0, ch) * (1 - fx) * (1 - fy)
                        + src.at(x1, y0, ch) * fx * (1 - fy)
                        + src.at(x0, y1, ch) * (1 - fx) * fy
                        + src.at(x1, y1, ch) * fx * fy;
                full.at(x, y, ch) = v;
            }
        }
    }
}

// --- Fast NLM: denoise at half resolution, then upsample ---

void nlm_denoise_cpu_neon_fast(const Image& src, Image& dst, const NlmParams& params) {
    if (params.verbose) {
        std::cout << "NLM Fast (multi-resolution): " << src.width << "x" << src.height
                  << "x" << src.channels << " -> downsample -> NLM -> upsample\n";
    }

    // Downsample
    Image half;
    downsample_2x(src, half);

    // Denoise at half res (with reduced h proportional to resolution reduction)
    NlmParams half_params = params;
    half_params.h *= 0.85f;  // slightly reduce filter strength for coarser scale
    half_params.verbose = false;

    Image half_dst;
    nlm_denoise_cpu_neon(half, half_dst, half_params);

    // Upsample back
    upsample_2x(half_dst, dst, src.width, src.height);
}
