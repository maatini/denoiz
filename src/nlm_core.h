#pragma once

#include <cstdint>
#include <vector>
#include <string>

struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<float> data;

    float& at(int x, int y, int c) {
        return data[(y * width + x) * channels + c];
    }
    const float& at(int x, int y, int c) const {
        return data[(y * width + x) * channels + c];
    }
    const float* row(int y) const {
        return data.data() + y * width * channels;
    }
    float* row(int y) {
        return data.data() + y * width * channels;
    }
};

struct NlmParams {
    int patch_size = 7;
    int search_window = 21;
    float h = 0.1f;
    float sigma = 0.0f;
    bool use_gpu = false;
    int threads = 0; // 0 = auto
    bool verbose = false;
    bool benchmark = false;
    bool fast_mode = false;
    bool wavelet_mode = false;
};

Image load_image(const std::string& path);
bool save_image(const std::string& path, const Image& img);
bool parse_args(int argc, char* argv[], NlmParams& params, std::string& input, std::string& output);
void nlm_denoise_cpu(const Image& src, Image& dst, const NlmParams& params);
void nlm_denoise_cpu_neon(const Image& src, Image& dst, const NlmParams& params);
void nlm_denoise_cpu_neon_fast(const Image& src, Image& dst, const NlmParams& params);
void nlm_denoise_metal(const Image& src, Image& dst, const NlmParams& params);
void nlm_denoise_wavelet(const Image& src, Image& dst, const NlmParams& params);
double psnr(const Image& a, const Image& b);
