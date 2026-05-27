#include "nlm_wavelet.h"

#include <arm_neon.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <Accelerate/Accelerate.h>

#include <cmath>
#include <iostream>
#include <algorithm>
#include <vector>

// --- 2D Haar Wavelet ---

static void haar_2d_forward(const float* input, int w, int h, int stride,
                             float* LL, float* LH, float* HL, float* HH,
                             int hw, int hh) {
    // Row transform then column transform
    std::vector<float> temp(w * h);

    // Row-wise: [L, H] = [a+b, a-b] / sqrt(2)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < hw; ++x) {
            float a = input[y * stride + x * 2];
            float b = input[y * stride + x * 2 + 1];
            temp[y * w + x]       = (a + b) * 0.70710678f;
            temp[y * w + x + hw]  = (a - b) * 0.70710678f;
        }
    }

    // Column-wise
    for (int y = 0; y < hh; ++y) {
        for (int x = 0; x < w; ++x) {
            float a = temp[(y * 2)     * w + x];
            float b = temp[(y * 2 + 1) * w + x];
            float L = (a + b) * 0.70710678f;
            float H = (a - b) * 0.70710678f;

            if (x < hw) {
                LL[y * hw + x] = L;
                LH[y * hw + x] = H;
            } else {
                HL[y * hw + x - hw] = L;
                HH[y * hw + x - hw] = H;
            }
        }
    }
}

static void haar_2d_inverse(const float* LL, const float* LH,
                             const float* HL, const float* HH,
                             int hw, int hh,
                             float* output, int w, int h, int stride) {
    // Column-wise synthesis
    std::vector<float> temp(w * h);

    for (int y = 0; y < hh; ++y) {
        for (int x = 0; x < hw; ++x) {
            float L = LL[y * hw + x];
            float H = LH[y * hw + x];
            float a = (L + H) * 0.70710678f;
            float b = (L - H) * 0.70710678f;
            temp[(y * 2)     * w + x] = a;
            temp[(y * 2 + 1) * w + x] = b;
        }
        for (int x = 0; x < hw; ++x) {
            float L = HL[y * hw + x];
            float H = HH[y * hw + x];
            float a = (L + H) * 0.70710678f;
            float b = (L - H) * 0.70710678f;
            temp[(y * 2)     * w + x + hw] = a;
            temp[(y * 2 + 1) * w + x + hw] = b;
        }
    }

    // Row-wise synthesis
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < hw; ++x) {
            float L = temp[y * w + x];
            float H = temp[y * w + x + hw];
            float a = (L + H) * 0.70710678f;
            float b = (L - H) * 0.70710678f;
            output[y * stride + x * 2]     = a;
            output[y * stride + x * 2 + 1] = b;
        }
    }
}

void nlm_denoise_wavelet(const Image& src, Image& dst, const NlmParams& params) {
    int w = src.width;
    int h = src.height;
    int c = src.channels;
    int stride = w * c;

    dst.width = w;
    dst.height = h;
    dst.channels = c;
    dst.data.resize(w * h * c);

    if (params.verbose) {
        std::cout << "NLM Wavelet: " << w << "x" << h << "x" << c
                  << " patch=" << params.patch_size << " search=" << params.search_window
                  << " h=" << params.h << "\n";
    }

    // Pad dimensions to even
    int pw = (w % 2 == 0) ? w : w + 1;
    int ph = (h % 2 == 0) ? h : h + 1;
    int hw = pw / 2, hh = ph / 2;

    // Per-channel processing
    for (int ch = 0; ch < c; ++ch) {
        std::vector<float> chan(pw * ph);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                chan[y * pw + x] = src.data[(y * w + x) * c + ch];
        for (int y = 0; y < h; ++y)
            for (int x = w; x < pw; ++x)
                chan[y * pw + x] = chan[y * pw + w - 1];
        for (int y = h; y < ph; ++y)
            for (int x = 0; x < pw; ++x)
                chan[y * pw + x] = chan[(h - 1) * pw + x];

        // Level 1 DWT
        std::vector<float> LL1(hw * hh), LH1(hw * hh), HL1(hw * hh), HH1(hw * hh);
        haar_2d_forward(chan.data(), pw, ph, pw, LL1.data(), LH1.data(), HL1.data(), HH1.data(), hw, hh);

        // Level 2 DWT on LL1
        int hw2 = hw / 2, hh2 = hh / 2;
        std::vector<float> LL2(hw2 * hh2), LH2(hw2 * hh2), HL2(hw2 * hh2), HH2(hw2 * hh2);
        haar_2d_forward(LL1.data(), hw, hh, hw, LL2.data(), LH2.data(), HL2.data(), HH2.data(), hw2, hh2);

        // Noise estimation from HH1
        float noise_sigma = 0;
        for (int i = 0; i < hw * hh; ++i) noise_sigma = std::max(noise_sigma, std::fabs(HH1[i]));
        noise_sigma /= 0.6745f;  // MAD approximation

        if (params.verbose) {
            std::cout << "  channel " << ch << ": σ_est=" << noise_sigma << "\n";
        }

        // NLM on LL2 (coarsest approximation)
        Image ll2_img;
        ll2_img.width = hw2;
        ll2_img.height = hh2;
        ll2_img.channels = 1;
        ll2_img.data.assign(LL2.begin(), LL2.end());

        NlmParams ll2_params = params;
        ll2_params.verbose = false;
        ll2_params.patch_size = std::max(3, params.patch_size / 2);
        ll2_params.search_window = std::max(5, params.search_window / 2);
        ll2_params.h *= 0.7f;

        Image ll2_denoised;
        nlm_denoise_cpu_neon(ll2_img, ll2_denoised, ll2_params);

        // Threshold detail subbands (proven wavelet denoising)
        float thresh1 = noise_sigma * 2.5f;
        float thresh2 = noise_sigma * 2.0f;

        for (int i = 0; i < hw2 * hh2; ++i) {
            LH2[i] = (std::fabs(LH2[i]) > thresh2) ? LH2[i] : 0;
            HL2[i] = (std::fabs(HL2[i]) > thresh2) ? HL2[i] : 0;
            HH2[i] = (std::fabs(HH2[i]) > thresh2) ? HH2[i] : 0;
        }
        for (int i = 0; i < hw * hh; ++i) {
            LH1[i] = (std::fabs(LH1[i]) > thresh1) ? LH1[i] : 0;
            HL1[i] = (std::fabs(HL1[i]) > thresh1) ? HL1[i] : 0;
            HH1[i] = (std::fabs(HH1[i]) > thresh1) ? HH1[i] : 0;
        }

        // Level 2 IDWT
        std::vector<float> LL1r(hw * hh);
        haar_2d_inverse(ll2_denoised.data.data(), LH2.data(), HL2.data(), HH2.data(),
                        hw2, hh2, LL1r.data(), hw, hh, hw);

        // Level 1 IDWT
        std::vector<float> rec(pw * ph);
        haar_2d_inverse(LL1r.data(), LH1.data(), HL1.data(), HH1.data(),
                        hw, hh, rec.data(), pw, ph, pw);

        // Copy back (crop to original size)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                dst.data[(y * w + x) * c + ch] = std::max(0.0f, std::min(1.0f, rec[y * pw + x]));
    }

    if (params.verbose) {
        std::cout << "  wavelet NLM complete\n";
    }
}
