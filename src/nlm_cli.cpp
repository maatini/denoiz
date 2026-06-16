#include "nlm_core.h"
#include "nlm_image_tuning.h"

#include <iostream>
#include <string>
#include <cstring>

bool parse_args(int argc, char* argv[], NlmParams& params, std::string& input, std::string& output,
                ImageTuningConfig* tuning) {
    // Detect tuning mode early
    bool tuning_mode = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--find-best-params") == 0) { tuning_mode = true; break; }
    }

    if (argc < 2) {
        std::cerr << "Usage: denoise input [output] [options]\n"
                  << "       denoise input --find-best-params [tuning-options]\n\n"
                  << "Denoising options:\n"
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
                  << "  --coarse-to-fine   4x downsample → NLM → residual NLM\n\n"
                  << "Parameter tuning options (--find-best-params):\n"
                  << "  --find-best-params  Activate parameter tuning mode\n"
                  << "  --param-grid SPEC   Grid, e.g.: \"patch-size:5,7,9; h:0.1-0.5 step 0.1\"\n"
                  << "  --metric METRIC     Quality metric: ssim, psnr, perceptual, vmaf (default: vmaf)\n"
                  << "  --top N             Show top N results (default: 5)\n"
                  << "  --output-dir DIR    Output directory for results (default: ./tuning)\n"
                  << "  --reference PATH    Optional clean reference image for metrics\n";
        return false;
    }

    input = argv[1];

    if (tuning_mode) {
        // In tuning mode, output is optional (used as default if provided)
        if (argc >= 3 && argv[2][0] != '-') output = argv[2];
        else output = "denoised.png"; // placeholder, not saved in tuning mode
    } else {
        if (argc < 3) {
            std::cerr << "Error: output path required (or use --find-best-params for tuning mode)\n";
            return false;
        }
        output = argv[2];
    }

    // Determine where flags start (after positional args)
    int arg_start = 2; // skip argv[0] (program) and argv[1] (input)
    if (!tuning_mode) {
        arg_start = 3; // skip argv[2] (output) too
    } else if (argc >= 3 && argv[2][0] != '-') {
        arg_start = 3; // optional output was provided
    }

    for (int i = arg_start; i < argc; ++i) {
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
        } else if (arg == "--find-best-params") {
            if (tuning) tuning->find_best_params = true;
        } else if (arg == "--param-grid" && i + 1 < argc) {
            if (tuning) tuning->param_grid = argv[++i];
        } else if (arg == "--metric" && i + 1 < argc) {
            if (tuning) tuning->metric = argv[++i];
        } else if (arg == "--top" && i + 1 < argc) {
            if (tuning) tuning->top_n = std::stoi(argv[++i]);
        } else if (arg == "--output-dir" && i + 1 < argc) {
            if (tuning) tuning->output_dir = argv[++i];
        } else if (arg == "--reference" && i + 1 < argc) {
            if (tuning) tuning->reference_path = argv[++i];
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
