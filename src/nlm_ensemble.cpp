#include "nlm_core.h"

#include <dispatch/dispatch.h>
#include <mach/mach_time.h>

#include <cmath>
#include <iostream>
#include <vector>
#include <cstring>

// Ensemble: run NLM with 3 h values (h-δ, h, h+δ) and average

struct EnsembleMember {
    const Image* src;
    Image* dst;
    NlmParams params;
};

static void ensemble_worker(void* ctx, size_t idx) {
    EnsembleMember* em = static_cast<EnsembleMember*>(ctx);
    nlm_denoise_cpu_neon(*em[idx].src, *em[idx].dst, em[idx].params);
}

void nlm_denoise_ensemble(const Image& src, Image& dst, const NlmParams& params) {
    int w = src.width, h = src.height, c = src.channels;
    int n_pixels = w * h * c;

    dst.width = w;
    dst.height = h;
    dst.channels = c;
    dst.data.resize(n_pixels);

    float h_vals[3] = {
        std::max(0.01f, params.h - 0.02f),
        params.h,
        params.h + 0.02f
    };
    EnsembleMember members[3];
    Image results[3];

    for (int i = 0; i < 3; ++i) {
        results[i].width = w;
        results[i].height = h;
        results[i].channels = c;
        results[i].data.resize(n_pixels);

        members[i].src = &src;
        members[i].dst = &results[i];
        members[i].params = params;
        members[i].params.h = h_vals[i];
        members[i].params.verbose = false;
    }

    if (params.verbose) {
        std::cout << "NLM Ensemble: " << w << "x" << h << "x" << c
                  << " patch=" << params.patch_size << " search=" << params.search_window
                  << " h=[" << members[0].params.h << "," << members[1].params.h
                  << "," << members[2].params.h << "]\n";
    }

    dispatch_apply_f(3, DISPATCH_APPLY_AUTO, members, ensemble_worker);

    // Average
    for (int i = 0; i < n_pixels; ++i) {
        dst.data[i] = (results[0].data[i] + results[1].data[i] + results[2].data[i]) / 3.0f;
    }

    if (params.verbose) {
        std::cout << "  ensemble average complete\n";
    }
}
