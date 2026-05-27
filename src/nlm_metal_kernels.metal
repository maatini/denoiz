#include <metal_stdlib>
using namespace metal;

kernel void nlm_compute(
    device const float* src    [[buffer(0)]],
    device float* dst          [[buffer(1)]],
    constant int& width        [[buffer(2)]],
    constant int& height       [[buffer(3)]],
    constant int& channels     [[buffer(4)]],
    constant int& half_patch   [[buffer(5)]],
    constant int& half_search  [[buffer(6)]],
    constant float& h2_inv     [[buffer(7)]],  // 1.0 / (h*h)
    uint2 gid [[thread_position_in_grid]]
) {
    int x = int(gid.x);
    int y = int(gid.y);
    if (x >= width || y >= height) return;

    int sy_min = max(y - half_search, 0);
    int sy_max = min(y + half_search, height - 1);
    int sx_min = max(x - half_search, 0);
    int sx_max = min(x + half_search, width - 1);

    int patch_dim = 2 * half_patch + 1;
    int patch_count = patch_dim * patch_dim;

    float total_weight = 0.0;
    float accum[4] = {0, 0, 0, 0};

    for (int sy = sy_min; sy <= sy_max; ++sy) {
        for (int sx = sx_min; sx <= sx_max; ++sx) {
            float ssd = 0.0;

            for (int py = -half_patch; py <= half_patch; ++py) {
                int cy = y + py;
                int ny = sy + py;
                if (cy < 0 || cy >= height || ny < 0 || ny >= height) continue;

                int crow = cy * width * channels;
                int nrow = ny * width * channels;

                for (int px = -half_patch; px <= half_patch; ++px) {
                    int cx = x + px;
                    int nx = sx + px;
                    if (cx < 0 || cx >= width || nx < 0 || nx >= width) continue;

                    int ci = crow + cx * channels;
                    int ni = nrow + nx * channels;

                    for (int c = 0; c < channels; ++c) {
                        float diff = src[ci + c] - src[ni + c];
                        ssd += diff * diff;
                    }
                }
            }

            float weight = exp(-ssd * h2_inv * (1.0f / float(patch_count)));

            int si = sy * width * channels + sx * channels;
            for (int c = 0; c < channels; ++c) {
                accum[c] += weight * src[si + c];
            }
            total_weight += weight;
        }
    }

    int di = y * width * channels + x * channels;
    for (int c = 0; c < channels; ++c) {
        if (total_weight > 1e-10f) {
            dst[di + c] = accum[c] / total_weight;
        } else {
            dst[di + c] = src[di + c];
        }
    }
}
