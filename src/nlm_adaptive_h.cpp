#include "nlm_core.h"

#include <arm_neon.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <Accelerate/Accelerate.h>

#include <cmath>
#include <iostream>
#include <algorithm>
#include <vector>

// Compute per-pixel local variance (sliding window, 1-channel proxy: luminance)
static std::vector<float> compute_local_variance(const float* src, int w, int h, int channels, int radius) {
    std::vector<float> var_map(w * h);
    int stride = w * channels;

    // Compute luminance (mean of channels) as scalar proxy
    std::vector<float> lum(w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0;
            int idx = y * stride + x * channels;
            for (int c = 0; c < channels; ++c) sum += src[idx + c];
            lum[y * w + x] = sum / channels;
        }
    }

    // Sliding window variance
    // Use box-filter approach via vDSP for speed
    std::vector<float> mean(w * h);
    std::vector<float> sqr_mean(w * h);

    // Mean and squared-mean box filter
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int x0 = std::max(0, x - radius);
            int x1 = std::min(w - 1, x + radius);
            int y0 = std::max(0, y - radius);
            int y1 = std::min(h - 1, y + radius);

            float sum = 0, sum_sq = 0;
            int cnt = 0;
            for (int wy = y0; wy <= y1; ++wy) {
                for (int wx = x0; wx <= x1; ++wx) {
                    float v = lum[wy * w + wx];
                    sum += v;
                    sum_sq += v * v;
                    cnt++;
                }
            }
            mean[y * w + x] = sum / cnt;
            sqr_mean[y * w + x] = sum_sq / cnt;
        }
    }

    // variance = E[X²] - E[X]²
    for (int i = 0; i < w * h; ++i) {
        float v = sqr_mean[i] - mean[i] * mean[i];
        var_map[i] = std::max(0.0f, v);
    }

    return var_map;
}

// Convert variance map to per-pixel h values
// h(x,y) = h_base * (1 + α * (v(x)/V_mean - 1))
static std::vector<float> compute_h_map(const std::vector<float>& var_map, float h_base, float alpha) {
    int n = var_map.size();
    float mean_var = 0;
    for (int i = 0; i < n; ++i) mean_var += var_map[i];
    mean_var /= n;
    if (mean_var < 1e-10f) mean_var = 1e-10f;

    std::vector<float> h_map(n);
    float inv_mean = 1.0f / mean_var;
    for (int i = 0; i < n; ++i) {
        float ratio = var_map[i] * inv_mean;
        h_map[i] = h_base * (1.0f + alpha * (ratio - 1.0f));
        if (h_map[i] < 0.01f) h_map[i] = 0.01f;
        if (h_map[i] > 1.0f)  h_map[i] = 1.0f;
    }
    return h_map;
}

// Adaptive NEON path: h varies per pixel
struct AdaptiveContext {
    const float* src_data;
    float* dst_data;
    const float* h_map;
    int w, h, c, stride;
    int half_patch, half_search;
};

static void process_row_adaptive(void* ctx_ptr, size_t y) {
    AdaptiveContext* ctx = static_cast<AdaptiveContext*>(ctx_ptr);
    int w = ctx->w, h = ctx->h, c = ctx->c, stride = ctx->stride;
    int hp = ctx->half_patch, hs = ctx->half_search;

    for (int x = 0; x < w; ++x) {
        float local_h = ctx->h_map[(int)y * w + x];
        float h2 = local_h * local_h;

        float total_weight = 0;
        float accum[4] = {0, 0, 0, 0};

        int sy_min = std::max((int)y - hs, 0);
        int sy_max = std::min((int)y + hs, h - 1);
        int sx_min = std::max(x - hs, 0);
        int sx_max = std::min(x + hs, w - 1);

        for (int sy = sy_min; sy <= sy_max; ++sy) {
            for (int sx = sx_min; sx <= sx_max; ++sx) {
                // Scalar SSD (simplified; NEON inline too complex for adaptive h)
                float ssd = 0;
                int patch_count = 0;
                for (int py = -hp; py <= hp; ++py) {
                    int cy = (int)y + py;
                    int ny = sy + py;
                    if (cy < 0 || cy >= h || ny < 0 || ny >= h) continue;
                    for (int px = -hp; px <= hp; ++px) {
                        int cx = x + px;
                        int nx = sx + px;
                        if (cx < 0 || cx >= w || nx < 0 || nx >= w) continue;
                        for (int ch = 0; ch < c; ++ch) {
                            float diff = ctx->src_data[cy * stride + cx * c + ch]
                                       - ctx->src_data[ny * stride + nx * c + ch];
                            ssd += diff * diff;
                        }
                        patch_count++;
                    }
                }
                if (patch_count > 0) ssd /= patch_count;

                float weight = std::exp(-ssd / h2);

                int si = sy * stride + sx * c;
                for (int ch = 0; ch < c; ++ch) accum[ch] += weight * ctx->src_data[si + ch];
                total_weight += weight;
            }
        }

        int di = (int)y * stride + x * c;
        for (int ch = 0; ch < c; ++ch) {
            if (total_weight > 1e-10f) ctx->dst_data[di + ch] = accum[ch] / total_weight;
            else                        ctx->dst_data[di + ch] = ctx->src_data[di + ch];
        }
    }
}

void nlm_denoise_adaptive(const Image& src, Image& dst, const NlmParams& params) {
    int w = src.width, h = src.height, c = src.channels;

    dst.width = w;
    dst.height = h;
    dst.channels = c;
    dst.data.resize(w * h * c);

    if (params.verbose) {
        std::cout << "NLM Adaptive: " << w << "x" << h << "x" << c
                  << " patch=" << params.patch_size << " search=" << params.search_window
                  << " h=" << params.h << " base\n";
    }

    // Compute local variance and h-map
    uint64_t t0 = mach_absolute_time();

    int var_radius = std::min(params.patch_size, 7);
    auto var_map = compute_local_variance(src.data.data(), w, h, c, var_radius);
    auto h_map = compute_h_map(var_map, params.h, 0.5f);

    uint64_t t1 = mach_absolute_time();
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    double prep_time = (double)(t1 - t0) * timebase.numer / timebase.denom * 1e-9;

    // Adaptive denoising
    AdaptiveContext ctx;
    ctx.src_data = src.data.data();
    ctx.dst_data = dst.data.data();
    ctx.h_map = h_map.data();
    ctx.w = w; ctx.h = h; ctx.c = c;
    ctx.stride = w * c;
    ctx.half_patch = params.patch_size / 2;
    ctx.half_search = params.search_window / 2;

    dispatch_apply_f(h, DISPATCH_APPLY_AUTO, &ctx, process_row_adaptive);

    uint64_t t2 = mach_absolute_time();
    double nlm_time = (double)(t2 - t1) * timebase.numer / timebase.denom * 1e-9;
    double total_time = (double)(t2 - t0) * timebase.numer / timebase.denom * 1e-9;

    if (params.verbose) {
        std::cout << "  prep (variance + h-map): " << prep_time << " s\n"
                  << "  NLM adaptive: " << nlm_time << " s\n"
                  << "  total: " << total_time << " s\n";
    }
}
