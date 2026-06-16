#include "nlm_core.h"

#include <iostream>
#include <string>

bool parse_args(int argc, char* argv[], NlmParams& params, std::string& input, std::string& output) {
    if (argc < 3) {
        std::cerr << "Usage: denoise input output [options]\n"
                  << "  --patch-size N      Patch size (odd, default: 7)\n"
                  << "  --search-window N   Search window size (odd, default: 21)\n"
                  << "  --h FLOAT           Filter strength (default: 0.1)\n"
                  << "  --sigma FLOAT       Noise sigma (default: 0.0)\n"
                  << "  --use-gpu           Use Metal GPU\n"
                  << "  --threads N         Thread count (0=auto, default: 0)\n"
                  << "  --verbose           Verbose output\n"
                  << "  --benchmark         Run naive reference + comparison\n"
                  << "  --fast              Multi-resolution (2x downsample) NLM\n"
                  << "  --wavelet           Wavelet-domain NLM (DWT + threshold)\n"
                  << "  --adaptive          Adaptive h (local variance)\n"
                  << "  --ensemble          Multi-h ensemble (3 members)\n"
                  << "  --coarse-to-fine   4x downsample → NLM → residual NLM\n";
        return false;
    }

    input = argv[1];
    output = argv[2];

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--patch-size" && i + 1 < argc) {
            params.patch_size = std::stoi(argv[++i]);
        } else if (arg == "--search-window" && i + 1 < argc) {
            params.search_window = std::stoi(argv[++i]);
        } else if (arg == "--h" && i + 1 < argc) {
            params.h = std::stof(argv[++i]);
        } else if (arg == "--sigma" && i + 1 < argc) {
            params.sigma = std::stof(argv[++i]);
        } else if (arg == "--use-gpu") {
            params.use_gpu = true;
        } else if (arg == "--threads" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "auto") params.threads = 0;
            else params.threads = std::stoi(v);
        } else if (arg == "--verbose") {
            params.verbose = true;
        } else if (arg == "--benchmark") {
            params.benchmark = true;
        } else if (arg == "--fast") {
            params.fast_mode = true;
        } else if (arg == "--wavelet") {
            params.wavelet_mode = true;
        } else if (arg == "--adaptive") {
            params.adaptive_mode = true;
        } else if (arg == "--ensemble") {
            params.ensemble_mode = true;
        } else if (arg == "--coarse-to-fine") {
            params.coarse_to_fine_mode = true;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return false;
        }
    }

    // Ensure odd sizes
    if (params.patch_size < 1) params.patch_size = 1;
    if (params.patch_size % 2 == 0) params.patch_size++;
    if (params.search_window < 1) params.search_window = 1;
    if (params.search_window % 2 == 0) params.search_window++;
    if (params.h <= 0.0f) {
        std::cerr << "Error: --h must be positive\n";
        return false;
    }

    return true;
}
