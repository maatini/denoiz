#pragma once

#include "nlm_core.h"
#include <string>
#include <vector>
#include <map>

struct ImageTuningConfig {
    std::string input_path;
    std::string output_dir = "./tuning";
    std::string param_grid;
    std::string metric = "vmaf";
    std::string reference_path;  // optional clean reference image
    int top_n = 5;
    bool verbose = false;
    bool find_best_params = false;
};

struct ParamAxis {
    std::string name;
    std::vector<double> values;
};

struct TuningResult {
    std::map<std::string, double> params;
    double score = 0.0;
    double psnr_val = 0.0;
    double ssim_val = 0.0;
    double perceptual_val = 0.0;
    double vmaf_val = 0.0;
};

// Parse --param-grid syntax: "param1:val1,val2; param2:min-max step s"
std::vector<ParamAxis> parse_param_grid(const std::string& spec);

// Generate all combinations (cartesian product) from axes
std::vector<std::map<std::string, double>> generate_grid(const std::vector<ParamAxis>& axes);

// Apply a param set to NlmParams
void apply_param_set(const std::map<std::string, double>& ps, NlmParams& p);

// SSIM between two images (single-channel or RGB, float 0..1)
double compute_ssim(const Image& a, const Image& b);

// Perceptual metric: edge preservation + noise reduction (0..1, higher=better)
double compute_perceptual(const Image& original, const Image& denoised);

// VMAF perceptual quality metric (0-100, higher=better)
// Uses Netflix libvmaf v3.0 with vmaf_v0.6.1 model
double compute_vmaf(const Image& ref, const Image& dist);

// Main tuning entry point for images
// `src` = noisy input image, `ref` = clean reference (empty if none)
int run_image_tuning(const Image& src, const Image& ref, const ImageTuningConfig& config);
