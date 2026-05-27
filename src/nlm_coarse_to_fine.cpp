#include "nlm_core.h"

#include <cmath>
#include <iostream>
#include <vector>
#include <algorithm>

// Coarse-to-Fine: 4× downsample → NLM coarse → residual = input − coarse → NLM on residual → output

void nlm_denoise_coarse_to_fine(const Image& src, Image& dst, const NlmParams& params) {
    int w = src.width, h = src.height, c = src.channels;
    int n = w * h * c;

    dst.width = w;
    dst.height = h;
    dst.channels = c;
    dst.data.resize(n);

    if (params.verbose) {
        std::cout << "NLM Coarse-to-Fine: " << w << "x" << h << "x" << c
                  << " patch=" << params.patch_size << " search=" << params.search_window
                  << " h=" << params.h << "\n";
    }

    // Step 1: 2× downsampling (box average)
    int hw2 = (w + 1) / 2, hh2 = (h + 1) / 2;
    Image coarse_src;
    coarse_src.width = hw2;
    coarse_src.height = hh2;
    coarse_src.channels = c;
    coarse_src.data.resize(hw2 * hh2 * c);

    for (int y = 0; y < hh2; ++y) {
        for (int x = 0; x < hw2; ++x) {
            for (int ch = 0; ch < c; ++ch) {
                float sum = 0;
                int cnt = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        int sx = std::min(x * 2 + dx, w - 1);
                        int sy = std::min(y * 2 + dy, h - 1);
                        sum += src.at(sx, sy, ch);
                        cnt++;
                    }
                }
                coarse_src.at(x, y, ch) = sum / cnt;
            }
        }
    }

    // Step 2: NLM on coarse (strong filter)
    NlmParams coarse_params = params;
    coarse_params.h *= 1.2f;
    coarse_params.patch_size = std::max(3, params.patch_size / 2);
    coarse_params.search_window = std::max(5, params.search_window / 2);
    coarse_params.verbose = false;

    Image coarse_dst;
    nlm_denoise_cpu_neon(coarse_src, coarse_dst, coarse_params);

    // Step 3: Upsample coarse to original (bilinear)
    Image coarse_full;
    coarse_full.width = w;
    coarse_full.height = h;
    coarse_full.channels = c;
    coarse_full.data.resize(n);

    float scale_x = (float)(hw2 - 1) / (w > 1 ? w - 1 : 1);
    float scale_y = (float)(hh2 - 1) / (h > 1 ? h - 1 : 1);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float gx = x * scale_x;
            float gy = y * scale_y;
            int x0 = (int)gx, y0 = (int)gy;
            int x1 = std::min(x0 + 1, hw2 - 1);
            int y1 = std::min(y0 + 1, hh2 - 1);
            float fx = gx - x0, fy = gy - y0;

            for (int ch = 0; ch < c; ++ch) {
                float v = coarse_dst.at(x0, y0, ch) * (1 - fx) * (1 - fy)
                        + coarse_dst.at(x1, y0, ch) * fx * (1 - fy)
                        + coarse_dst.at(x0, y1, ch) * (1 - fx) * fy
                        + coarse_dst.at(x1, y1, ch) * fx * fy;
                coarse_full.at(x, y, ch) = v;
            }
        }
    }

    // Step 4: Residual = src − coarse_full
    Image residual;
    residual.width = w;
    residual.height = h;
    residual.channels = c;
    residual.data.resize(n);

    for (int i = 0; i < n; ++i) {
        residual.data[i] = src.data[i] - coarse_full.data[i];
    }

    // Step 5: NLM on residual
    NlmParams residual_params = params;
    residual_params.h *= 0.8f;
    residual_params.patch_size = std::max(3, params.patch_size / 2);
    residual_params.verbose = false;

    Image residual_dst;
    nlm_denoise_cpu_neon(residual, residual_dst, residual_params);

    // Step 6: Output = coarse_full + refined_residual
    for (int i = 0; i < n; ++i) {
        float v = coarse_full.data[i] + residual_dst.data[i];
        dst.data[i] = std::max(0.0f, std::min(1.0f, v));
    }

    if (params.verbose) {
        std::cout << "  coarse done\n";
    }
}
