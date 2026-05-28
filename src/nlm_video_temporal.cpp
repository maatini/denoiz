#include "nlm_video_temporal.h"

#include <cmath>
#include <algorithm>

TemporalDenoiser::TemporalDenoiser(const TemporalConfig& tc, const NlmParams& np)
    : config(tc), params(np) {}

Image TemporalDenoiser::denoise(const Image& current) {
    history.push_back(current);
    while ((int)history.size() > config.frame_count) {
        history.pop_front();
    }

    int w = current.width, h = current.height, c = current.channels;
    size_t n = (size_t)w * h * c;

    Image dst;
    dst.width = w;
    dst.height = h;
    dst.channels = c;
    dst.data.resize(n);

    int nframes = (int)history.size();
    float h2 = params.h * params.h;
    int half_patch = params.patch_size / 2;

    // Pre-compute temporal weights: closer frames → higher weight
    std::vector<float> t_weights(nframes);
    for (int ti = 0; ti < nframes; ti++) {
        int dist = nframes - 1 - ti;  // distance from current frame
        t_weights[ti] = std::pow(config.temporal_weight, (float)dist);
    }

    // For each pixel in the current frame, average the same position
    // across all buffered frames, weighted by patch similarity.
    // This is fast: O(w*h*frames*patch²), no spatial search across frames.
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double total_w = 0.0;
            double accum[4] = {0, 0, 0, 0};

            for (int ti = 0; ti < nframes; ti++) {
                const Image& frame_t = history[ti];
                if (ti == nframes - 1) {
                    // Current frame: always included with weight 1.0
                    accum[0] += frame_t.at(x, y, 0);
                    accum[1] += frame_t.at(x, y, 1);
                    accum[2] += frame_t.at(x, y, 2);
                    total_w += 1.0;
                    continue;
                }

                // Compare patches at same (x,y) between current and frame_t
                double ssd = 0.0;
                int count = 0;
                for (int py = -half_patch; py <= half_patch; py++) {
                    int cy = y + py, ny = y + py;
                    if (cy < 0 || cy >= h || ny < 0 || ny >= h) continue;
                    for (int px = -half_patch; px <= half_patch; px++) {
                        int cx = x + px, nx = x + px;
                        if (cx < 0 || cx >= w || nx < 0 || nx >= w) continue;
                        float d0 = current.at(cx, cy, 0) - frame_t.at(nx, ny, 0);
                        float d1 = current.at(cx, cy, 1) - frame_t.at(nx, ny, 1);
                        float d2 = current.at(cx, cy, 2) - frame_t.at(nx, ny, 2);
                        ssd += d0 * d0 + d1 * d1 + d2 * d2;
                        count++;
                    }
                }
                if (count > 0) ssd /= count;

                double w = t_weights[ti] * std::exp(-ssd / h2);
                accum[0] += w * frame_t.at(x, y, 0);
                accum[1] += w * frame_t.at(x, y, 1);
                accum[2] += w * frame_t.at(x, y, 2);
                total_w += w;
            }

            for (int ch = 0; ch < c; ch++) {
                dst.at(x, y, ch) = (float)(accum[ch] / total_w);
            }
        }
    }

    return dst;
}
