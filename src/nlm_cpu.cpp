#include "nlm_core.h"

#include <cmath>
#include <iostream>
#include <vector>

void nlm_denoise_cpu(const Image& src, Image& dst, const NlmParams& params) {
    int w = src.width;
    int h = src.height;
    int c = src.channels;

    dst.width = w;
    dst.height = h;
    dst.channels = c;
    dst.data.resize(w * h * c);

    int half_patch = params.patch_size / 2;
    int half_search = params.search_window / 2;
    float h2 = params.h * params.h;

    if (params.verbose) {
        std::cout << "NLM CPU: " << w << "x" << h << "x" << c
                  << " patch=" << params.patch_size << " search=" << params.search_window
                  << " h=" << params.h << "\n";
    }

    // Color images: combine channels into patch distance
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double total_weight = 0.0;
            std::vector<double> accum(c, 0.0);

            int sy_min = std::max(y - half_search, 0);
            int sy_max = std::min(y + half_search, h - 1);
            int sx_min = std::max(x - half_search, 0);
            int sx_max = std::min(x + half_search, w - 1);

            for (int sy = sy_min; sy <= sy_max; ++sy) {
                for (int sx = sx_min; sx <= sx_max; ++sx) {
                    // Compute patch SSD
                    double ssd = 0.0;
                    int patch_count = 0;

                    for (int py = -half_patch; py <= half_patch; ++py) {
                        int cy = y + py;
                        int ny = sy + py;
                        if (cy < 0 || cy >= h || ny < 0 || ny >= h) continue;
                        for (int px = -half_patch; px <= half_patch; ++px) {
                            int cx = x + px;
                            int nx = sx + px;
                            if (cx < 0 || cx >= w || nx < 0 || nx >= w) continue;
                            for (int ch = 0; ch < c; ++ch) {
                                float diff = src.at(cx, cy, ch) - src.at(nx, ny, ch);
                                ssd += diff * diff;
                            }
                            patch_count++;
                        }
                    }

                    if (patch_count > 0) {
                        ssd /= patch_count;
                    }

                    double weight = std::exp(-ssd / h2);

                    for (int ch = 0; ch < c; ++ch) {
                        accum[ch] += weight * src.at(sx, sy, ch);
                    }
                    total_weight += weight;
                }
            }

            for (int ch = 0; ch < c; ++ch) {
                if (total_weight > 1e-10) {
                    dst.at(x, y, ch) = static_cast<float>(accum[ch] / total_weight);
                } else {
                    dst.at(x, y, ch) = src.at(x, y, ch);
                }
            }
        }
        if (params.verbose && (y + 1) % 10 == 0) {
            std::cout << "\r  progress: " << (y + 1) << "/" << h << " rows" << std::flush;
        }
    }
    if (params.verbose) {
        std::cout << "\r  done: " << h << "/" << h << " rows\n";
    }
}
