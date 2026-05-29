#pragma once

#include "nlm_core.h"
#include <string>
#include <vector>
#include <map>
#include <functional>

struct TuningConfig {
    std::string input_path;
    std::string output_dir = "./tuning";
    std::string param_grid;
    std::string metric = "ssim";
    double start_sec = 0.0;
    double duration_sec = 20.0;
    int top_n = 5;
    bool verbose = false;
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
    std::string clip_path;
};

// Parse --param-grid syntax: "param1:val1,val2; param2:min-max step s"
std::vector<ParamAxis> parse_param_grid(const std::string& spec);

// Generate all combinations (cartesian product) from axes
std::vector<std::map<std::string, double>> generate_grid(const std::vector<ParamAxis>& axes);

// Apply a param set to NlmParams
void apply_param_set(const std::map<std::string, double>& ps, NlmParams& p);

// Parse --start time (HH:MM:SS or seconds)
double parse_time(const std::string& s);

// SSIM between two images (single-channel or RGB, float 0..1)
double compute_ssim(const Image& a, const Image& b);

// Perceptual metric: edge preservation + noise reduction (0..1, higher=better)
double compute_perceptual(const Image& original, const Image& denoised);

// Main tuning entry point
int run_tuning(const TuningConfig& config);
