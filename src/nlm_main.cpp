#include "nlm_core.h"

#include <iostream>
#include <chrono>
#include <cmath>

int main(int argc, char* argv[]) {
    NlmParams params;
    std::string input_path, output_path;

    if (!parse_args(argc, argv, params, input_path, output_path)) {
        return 1;
    }

    Image src = load_image(input_path);
    if (src.data.empty()) {
        std::cerr << "Error: could not load image\n";
        return 1;
    }

    if (params.sigma > 0.0f && params.verbose) {
        std::cout << "Note: sigma=" << params.sigma << " (noise not applied)\n";
    }

    Image dst;

    auto t0 = std::chrono::high_resolution_clock::now();
    if (params.use_gpu) {
        if (params.verbose) std::cout << "Pipeline: Metal GPU\n";
        nlm_denoise_metal(src, dst, params);
    } else if (params.fast_mode) {
        if (params.verbose) std::cout << "Pipeline: Fast (multi-resolution)\n";
        nlm_denoise_cpu_neon_fast(src, dst, params);
    } else if (params.wavelet_mode) {
        if (params.verbose) std::cout << "Pipeline: Wavelet\n";
        nlm_denoise_wavelet(src, dst, params);
    } else {
        if (params.verbose) std::cout << "Pipeline: CPU NEON+GCD\n";
        nlm_denoise_cpu_neon(src, dst, params);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    if (params.verbose) {
        std::cout << "Wall time: " << elapsed << " s\n";
    }

    if (params.benchmark) {
        NlmParams ref_params = params;
        ref_params.verbose = false;

        // Naive reference
        Image dst_naive;
        auto tn0 = std::chrono::high_resolution_clock::now();
        nlm_denoise_cpu(src, dst_naive, ref_params);
        auto tn1 = std::chrono::high_resolution_clock::now();
        double t_naive = std::chrono::duration<double>(tn1 - tn0).count();

        // NEON+GCD reference
        Image dst_neon;
        auto tc0 = std::chrono::high_resolution_clock::now();
        nlm_denoise_cpu_neon(src, dst_neon, ref_params);
        auto tc1 = std::chrono::high_resolution_clock::now();
        double t_neon = std::chrono::duration<double>(tc1 - tc0).count();

        double psnr_primary = psnr(dst_naive, dst);

        std::cout << "Benchmark:\n"
                  << "  naive : " << t_naive << " s (baseline)\n"
                  << "  NEON  : " << t_neon << " s (" << t_naive / t_neon << "x)\n";
        if (params.use_gpu) {
            std::cout << "  GPU   : " << elapsed << " s (" << t_naive / elapsed << "x vs naive, "
                      << t_neon / elapsed << "x vs NEON)\n";
        }
        if (params.fast_mode) {
            std::cout << "  Fast  : " << elapsed << " s (" << t_naive / elapsed << "x vs naive, "
                      << t_neon / elapsed << "x vs NEON)\n";
        }
        if (params.wavelet_mode) {
            std::cout << "  Wavelet: " << elapsed << " s (" << t_naive / elapsed << "x vs naive, "
                      << t_neon / elapsed << "x vs NEON)\n";
        }
        std::cout << "  PSNR  : " << psnr_primary << " dB vs naive\n";
    }

    if (!save_image(output_path, dst)) {
        std::cerr << "Error: could not save image\n";
        return 1;
    }

    return 0;
}
