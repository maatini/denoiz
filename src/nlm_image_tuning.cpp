#include "nlm_image_tuning.h"

extern "C" {
#include <libvmaf/libvmaf.h>
}

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sys/stat.h>
#include <dispatch/dispatch.h>

// ── String split helper ────────────────────────────────────────────────

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::istringstream ss(s); std::string tok;
    while (std::getline(ss, tok, delim)) {
        while (!tok.empty() && tok.front() == ' ') tok.erase(0, 1);
        while (!tok.empty() && tok.back() == ' ') tok.pop_back();
        if (!tok.empty()) out.push_back(tok);
    }
    return out;
}

// ── Grid parsing (same syntax as video tuning) ────────────────────────

std::vector<ParamAxis> parse_param_grid(const std::string& spec) {
    std::vector<ParamAxis> axes;
    for (const auto& part : split(spec, ';')) {
        auto colon = part.find(':');
        if (colon == std::string::npos) continue;
        std::string name = part.substr(0, colon);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        std::string vals = part.substr(colon + 1);
        ParamAxis axis; axis.name = name;
        auto dash = vals.find('-');
        auto step_word = vals.find(" step ");
        if (dash != std::string::npos) {
            double min_val = std::stod(vals.substr(0, dash));
            double max_val, s = 1.0;
            if (step_word != std::string::npos) {
                max_val = std::stod(vals.substr(dash + 1, step_word - dash - 1));
                s = std::stod(vals.substr(step_word + 6));
            } else { max_val = std::stod(vals.substr(dash + 1)); }
            for (double v = min_val; v <= max_val + 1e-9; v += s)
                axis.values.push_back(v);
        } else {
            for (const auto& item : split(vals, ','))
                axis.values.push_back(std::stod(item));
        }
        axes.push_back(axis);
    }
    return axes;
}

std::vector<std::map<std::string, double>> generate_grid(const std::vector<ParamAxis>& axes) {
    std::vector<std::map<std::string, double>> results;
    if (axes.empty()) return results;
    std::vector<size_t> indices(axes.size(), 0);
    while (true) {
        std::map<std::string, double> combo;
        for (size_t i = 0; i < axes.size(); i++)
            combo[axes[i].name] = axes[i].values[indices[i]];
        results.push_back(combo);
        int i = (int)axes.size() - 1;
        while (i >= 0) { indices[i]++; if (indices[i] < axes[i].values.size()) break; indices[i] = 0; i--; }
        if (i < 0) break;
    }
    return results;
}

void apply_param_set(const std::map<std::string, double>& ps, NlmParams& p) {
    for (const auto& kv : ps) {
        if (kv.first == "patch-size" || kv.first == "patch_size") p.patch_size = (int)kv.second;
        else if (kv.first == "search-window" || kv.first == "search_window") p.search_window = (int)kv.second;
        else if (kv.first == "h") p.h = (float)kv.second;
        else if (kv.first == "sigma") p.sigma = (float)kv.second;
        else if (kv.first == "fast_mode" || kv.first == "prefilter") p.fast_mode = (kv.second > 0.5);
        else if (kv.first == "adaptive_mode") p.adaptive_mode = (kv.second > 0.5);
        else if (kv.first == "wavelet_mode") p.wavelet_mode = (kv.second > 0.5);
        else if (kv.first == "use_gpu") p.use_gpu = (kv.second > 0.5);
        else if (kv.first == "ensemble_mode") p.ensemble_mode = (kv.second > 0.5);
        else if (kv.first == "coarse_to_fine_mode") p.coarse_to_fine_mode = (kv.second > 0.5);
    }
}

// ── Quality Metrics ────────────────────────────────────────────────────

double compute_ssim(const Image& a, const Image& b) {
    if (a.width != b.width || a.height != b.height || a.channels != b.channels) return 0.0;
    int w = a.width, h = a.height, c = a.channels;
    const double K1 = 0.01, K2 = 0.03, L = 1.0;
    const double C1 = (K1 * L) * (K1 * L), C2 = (K2 * L) * (K2 * L);
    const int win = 8;
    double total = 0; int cnt = 0;
    for (int ch = 0; ch < c; ch++) {
        for (int y = 0; y <= h - win; y += win / 2) {
            for (int x = 0; x <= w - win; x += win / 2) {
                double mx = 0, my = 0; int n = 0;
                for (int dy = 0; dy < win && y + dy < h; dy++)
                    for (int dx = 0; dx < win && x + dx < w; dx++)
                        { mx += a.at(x+dx,y+dy,ch); my += b.at(x+dx,y+dy,ch); n++; }
                if (n < 2) continue;
                mx /= n; my /= n;
                double vx = 0, vy = 0, cxy = 0;
                for (int dy = 0; dy < win && y + dy < h; dy++)
                    for (int dx = 0; dx < win && x + dx < w; dx++) {
                        double dxv = a.at(x+dx,y+dy,ch) - mx;
                        double dyv = b.at(x+dx,y+dy,ch) - my;
                        vx += dxv*dxv; vy += dyv*dyv; cxy += dxv*dyv;
                    }
                vx /= (n-1); vy /= (n-1); cxy /= (n-1);
                total += ((2.0*mx*my+C1)*(2.0*cxy+C2)) / ((mx*mx+my*my+C1)*(vx+vy+C2));
                cnt++;
            }
        }
    }
    return cnt > 0 ? total / cnt : 0.0;
}

double compute_perceptual(const Image& o, const Image& d) {
    int w = o.width, h = o.height, c = o.channels;
    double eo = 0, ed = 0, ns = 0; int cnt = 0;
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            double go = 0, gd = 0;
            for (int ch = 0; ch < c; ch++) {
                double gx_o = o.at(x+1,y,ch) - o.at(x-1,y,ch);
                double gy_o = o.at(x,y+1,ch) - o.at(x,y-1,ch);
                go += std::sqrt(gx_o*gx_o + gy_o*gy_o);
                double gx_d = d.at(x+1,y,ch) - d.at(x-1,y,ch);
                double gy_d = d.at(x,y+1,ch) - d.at(x,y-1,ch);
                gd += std::sqrt(gx_d*gx_d + gy_d*gy_d);
            }
            eo += go/c; ed += gd/c;
            if (go/c < 0.05) {
                double diff = 0;
                for (int ch = 0; ch < c; ch++) diff += std::abs(o.at(x,y,ch) - d.at(x,y,ch));
                ns += diff / c;
            }
            cnt++;
        }
    }
    double es = eo > 0 ? ed / eo : 1.0;
    if (es > 1.5) es = 1.5 - (es - 1.5);
    es = std::max(0.0, std::min(1.0, es));
    double avg_ns = cnt > 0 ? ns / cnt : 0;
    double nsn = std::max(0.0, 1.0 - avg_ns / 0.1);
    return (es + nsn) / 2.0;
}

// ── VMAF (Netflix perceptual quality metric) ──────────────────────────

double compute_vmaf(const Image& a, const Image& b) {
    // Guard: images must match and have 3 channels
    if (a.width != b.width || a.height != b.height || a.channels != 3 || b.channels != 3)
        return 0.0;
    int W = a.width, H = a.height;

    // Guard: YUV420P requires even dimensions
    if (W % 2 != 0 || H % 2 != 0) return 0.0;

    // ── Init VMAF context ──────────────────────────────────────────
    VmafConfiguration cfg = {};
    cfg.log_level = VMAF_LOG_LEVEL_NONE;
    cfg.n_threads = 1;
    cfg.n_subsample = 1;

    VmafContext* vmaf = nullptr;
    if (vmaf_init(&vmaf, cfg) != 0) {
        std::cerr << "  VMAF: vmaf_init failed\n";
        return 0.0;
    }

    // ── Load model (try version-based auto-search first, then explicit paths) ─
    VmafModel* model = nullptr;
    VmafModelConfig mcfg = {};
    int load_ret = vmaf_model_load(&model, &mcfg, "v0.6.1");
    if (load_ret != 0) {
        // Fall back to explicit paths
        static const char* model_paths[] = {
            "/opt/homebrew/opt/libvmaf/share/libvmaf/model/vmaf_v0.6.1.json",
            "/opt/homebrew/Cellar/libvmaf/3.0.0/share/libvmaf/model/vmaf_v0.6.1.json",
            "/usr/local/opt/libvmaf/share/libvmaf/model/vmaf_v0.6.1.json",
            "/usr/local/share/libvmaf/model/vmaf_v0.6.1.json",
            nullptr
        };
        const char* found_path = nullptr;
        for (int i = 0; model_paths[i]; i++) {
            FILE* f = fopen(model_paths[i], "r");
            if (f) { found_path = model_paths[i]; fclose(f); break; }
        }
        if (!found_path) {
            std::cerr << "  VMAF: model not found\n";
            vmaf_close(vmaf);
            return 0.0;
        }
        load_ret = vmaf_model_load_from_path(&model, &mcfg, found_path);
    }
    if (load_ret != 0) {
        std::cerr << "  VMAF: model load failed\n";
        vmaf_close(vmaf);
        return 0.0;
    }
    if (vmaf_use_features_from_model(vmaf, model) != 0) {
        std::cerr << "  VMAF: use_features_from_model failed\n";
        vmaf_model_destroy(model);
        vmaf_close(vmaf);
        return 0.0;
    }

    // ── Allocate YUV pictures ──────────────────────────────────────
    VmafPicture ref_pic = {}, dist_pic = {};
    if (vmaf_picture_alloc(&ref_pic, VMAF_PIX_FMT_YUV420P, 8, W, H) != 0 ||
        vmaf_picture_alloc(&dist_pic, VMAF_PIX_FMT_YUV420P, 8, W, H) != 0) {
        std::cerr << "  VMAF: picture_alloc failed\n";
        vmaf_picture_unref(&ref_pic);
        vmaf_picture_unref(&dist_pic);
        vmaf_model_destroy(model);
        vmaf_close(vmaf);
        return 0.0;
    }

    // ── RGB float [0..1] → YUV420P 8-bit (BT.601) ─────────────────
    auto rgb_to_yuv = [](const Image& img, VmafPicture* pic) {
        int w = img.width, h = img.height;
        uint8_t* Y = (uint8_t*)pic->data[0];
        uint8_t* U = (uint8_t*)pic->data[1];
        uint8_t* V = (uint8_t*)pic->data[2];
        int ys = (int)pic->stride[0];
        int us = (int)pic->stride[1];
        int vs = (int)pic->stride[2];

        // Y plane (full resolution)
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float r = img.at(x, y, 0) * 255.0f;
                float g = img.at(x, y, 1) * 255.0f;
                float b = img.at(x, y, 2) * 255.0f;
                float yy = 0.299f * r + 0.587f * g + 0.114f * b;
                Y[y * ys + x] = (uint8_t)(yy < 0 ? 0 : (yy > 255 ? 255 : yy + 0.5f));
            }
        }

        // U, V planes (2x subsampled)
        for (int y = 0; y < h / 2; y++) {
            for (int x = 0; x < w / 2; x++) {
                float rs = 0, gs = 0, bs = 0;
                for (int dy = 0; dy < 2; dy++)
                    for (int dx = 0; dx < 2; dx++) {
                        int sx = x * 2 + dx, sy = y * 2 + dy;
                        rs += img.at(sx, sy, 0) * 255.0f;
                        gs += img.at(sx, sy, 1) * 255.0f;
                        bs += img.at(sx, sy, 2) * 255.0f;
                    }
                rs /= 4.0f; gs /= 4.0f; bs /= 4.0f;
                float uu = -0.14713f * rs - 0.28886f * gs + 0.436f * bs + 128.0f;
                float vv = 0.615f * rs - 0.51499f * gs - 0.10001f * bs + 128.0f;
                U[y * us + x] = (uint8_t)(uu < 0 ? 0 : (uu > 255 ? 255 : uu + 0.5f));
                V[y * vs + x] = (uint8_t)(vv < 0 ? 0 : (vv > 255 ? 255 : vv + 0.5f));
            }
        }
    };

    rgb_to_yuv(a, &ref_pic);
    rgb_to_yuv(b, &dist_pic);

    // ── Submit, flush, and score ───────────────────────────────────
    double score = 0.0;
    if (vmaf_read_pictures(vmaf, &ref_pic, &dist_pic, 0) == 0) {
        // Flush to complete feature extraction (required in libvmaf ≥3.0)
        vmaf_read_pictures(vmaf, nullptr, nullptr, 0);
        if (vmaf_score_at_index(vmaf, model, &score, 0) != 0)
            score = 0.0;
    }

    // ── Cleanup ────────────────────────────────────────────────────
    vmaf_picture_unref(&ref_pic);
    vmaf_picture_unref(&dist_pic);
    vmaf_model_destroy(model);
    vmaf_close(vmaf);

    return score;
}

// ── Single Param-Set Evaluation ────────────────────────────────────────

static TuningResult evaluate_param_set(
    const std::map<std::string, double>& param_set,
    const Image& src, const Image& ref,
    const std::string& metric_name,
    int test_idx)
{
    NlmParams np;
    np.h = 0.3f;
    apply_param_set(param_set, np);

    // Ensure odd sizes
    if (np.patch_size % 2 == 0) np.patch_size++;
    if (np.search_window % 2 == 0) np.search_window++;

    TuningResult result;
    result.params = param_set;

    auto t0 = std::chrono::high_resolution_clock::now();

    // Select pipeline (same priority as nlm_main.cpp)
    void (*nlm_fn)(const Image&, Image&, const NlmParams&) = nlm_denoise_cpu_neon;
    if (np.adaptive_mode) nlm_fn = nlm_denoise_adaptive;
    else if (np.wavelet_mode) nlm_fn = nlm_denoise_wavelet;
    else if (np.use_gpu) nlm_fn = nlm_denoise_metal;
    else if (np.ensemble_mode) nlm_fn = nlm_denoise_ensemble;
    else if (np.coarse_to_fine_mode) nlm_fn = nlm_denoise_coarse_to_fine;
    else if (np.fast_mode) nlm_fn = nlm_denoise_cpu_neon_fast;

    Image dst;
    nlm_fn(src, dst, np);

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    // Use clean reference if provided, otherwise compare against noisy original
    const Image& ref_img = ref.data.empty() ? src : ref;

    result.ssim_val = compute_ssim(ref_img, dst);
    result.psnr_val = ::psnr(ref_img, dst);
    result.perceptual_val = compute_perceptual(ref_img, dst);

    // VMAF requires 3-channel, even-dimension images
    if (src.channels == 3 && src.width % 2 == 0 && src.height % 2 == 0)
        result.vmaf_val = compute_vmaf(ref_img, dst);
    else
        result.vmaf_val = 0.0;

    if (metric_name == "ssim") result.score = result.ssim_val;
    else if (metric_name == "psnr") result.score = result.psnr_val;
    else if (metric_name == "perceptual") result.score = result.perceptual_val;
    else if (metric_name == "vmaf") result.score = result.vmaf_val;
    else result.score = result.perceptual_val;

    std::ostringstream ss;
    ss << "  [" << std::setw(3) << test_idx << "] ";
    for (const auto& kv : result.params) ss << kv.first << "=" << kv.second << " ";
    ss << "-> " << metric_name << "=" << std::fixed << std::setprecision(4) << result.score
       << "  (" << std::setprecision(2) << elapsed << "s)";
    std::cout << ss.str() << std::endl;

    return result;
}

// ── JSON Output ─────────────────────────────────────────────────────────

static void write_results_json(const std::string& path,
                               const std::vector<TuningResult>& results,
                               const ImageTuningConfig& config,
                               bool has_reference) {
    std::ofstream f(path);
    f << "{\n  \"config\": {\n"
      << "    \"input\": \"" << config.input_path << "\",\n"
      << "    \"reference\": \"" << (has_reference ? config.reference_path : "(none — compared against input)") << "\",\n"
      << "    \"metric\": \"" << config.metric << "\",\n"
      << "    \"grid\": \"" << config.param_grid << "\",\n"
      << "    \"top_n\": " << config.top_n << "\n"
      << "  },\n  \"results\": [\n";
    for (size_t i = 0; i < results.size(); i++) {
        const auto& r = results[i];
        f << "    {\n"
          << "      \"rank\": " << (i+1) << ",\n"
          << "      \"score\": " << r.score << ",\n"
          << "      \"ssim\": " << r.ssim_val << ",\n"
          << "      \"psnr\": " << r.psnr_val << ",\n"
          << "      \"perceptual\": " << r.perceptual_val << ",\n"
          << "      \"vmaf\": " << r.vmaf_val << ",\n"
          << "      \"params\": {";
        bool first = true;
        for (const auto& kv : r.params) {
            if (!first) f << ", ";
            f << "\"" << kv.first << "\": " << kv.second;
            first = false;
        }
        f << "}\n    }" << (i < results.size()-1 ? "," : "") << "\n";
    }
    f << "  ]\n}\n";
}

// ── Main Tuning Entry Point ─────────────────────────────────────────────

int run_image_tuning(const Image& src, const Image& ref, const ImageTuningConfig& config) {
    mkdir(config.output_dir.c_str(), 0755);
    auto t_start = std::chrono::high_resolution_clock::now();

    bool has_reference = !ref.data.empty();
    const Image& ref_img = has_reference ? ref : src;

    // ── Phase 1: Image info ─────────────────────────────────────────────
    std::cout << "\n=== Phase 1: Input Image ===\n"
              << "Input:  " << config.input_path << " (" << src.width << "x" << src.height
              << ", " << src.channels << " channels)\n";
    if (has_reference) {
        std::cout << "Ref:    " << config.reference_path << " (" << ref.width << "x" << ref.height
                  << ", " << ref.channels << " channels)\n";
    } else {
        std::cout << "Ref:    (none — comparing against noisy input)\n";
    }

    if (config.metric == "vmaf" && src.channels != 3) {
        std::cerr << "Warning: VMAF requires 3-channel RGB images (got " << src.channels
                  << " channels). VMAF scores will be 0.\n";
    }
    if (config.metric == "vmaf" && (src.width % 2 != 0 || src.height % 2 != 0)) {
        std::cerr << "Warning: VMAF requires even dimensions (got " << src.width << "x"
                  << src.height << "). VMAF scores will be 0.\n";
    }

    // ── Phase 2: Generate parameter grid ─────────────────────────────────
    std::cout << "\n=== Phase 2: Generate parameter grid ===\n";
    auto axes = parse_param_grid(config.param_grid);
    auto grid = generate_grid(axes);
    std::cout << "Parameters: ";
    for (const auto& ax : axes)
        std::cout << ax.name << "=[" << ax.values.size() << " values] ";
    std::cout << "--> " << grid.size() << " combinations\n";
    if (grid.empty()) { std::cerr << "No parameter combinations generated\n"; return 1; }

    // ── Phase 3: Evaluate ────────────────────────────────────────────────
    std::cout << "\n=== Phase 3: Evaluate (" << grid.size() << " tests) ===\n";
    std::vector<TuningResult> results;
    results.reserve(grid.size());
    for (size_t i = 0; i < grid.size(); i++)
        results.push_back(evaluate_param_set(grid[i], src, ref, config.metric, (int)i + 1));

    // ── Phase 4: Ranking ─────────────────────────────────────────────────
    std::cout << "\n=== Phase 4: Ranking ===\n";
    std::sort(results.begin(), results.end(),
              [](const TuningResult& a, const TuningResult& b) { return a.score > b.score; });
    if ((int)results.size() > config.top_n) results.resize(config.top_n);

    std::cout << "\n=== Top " << config.top_n << " Results ===\n"
              << std::fixed << std::setprecision(4);
    for (size_t i = 0; i < results.size(); i++) {
        std::cout << "  #" << (i+1) << "  score=" << results[i].score
                  << "  ssim=" << results[i].ssim_val
                  << "  psnr=" << results[i].psnr_val << " dB"
                  << "  perceptual=" << results[i].perceptual_val
                  << "  vmaf=" << results[i].vmaf_val << "\n";
        std::cout << "       params: {";
        bool first = true;
        for (const auto& kv : results[i].params) {
            if (!first) std::cout << ", ";
            std::cout << kv.first << "=" << kv.second;
            first = false;
        }
        std::cout << "}\n";
    }

    std::string json_path = config.output_dir + "/best-params.json";
    write_results_json(json_path, results, config, has_reference);
    std::cout << "\nResults written to " << json_path << "\n";

    auto t_end = std::chrono::high_resolution_clock::now();
    double total = std::chrono::duration<double>(t_end - t_start).count();
    std::cout << "\nTotal: " << std::fixed << std::setprecision(1) << total
              << "s for " << grid.size() << " tests ("
              << std::setprecision(2) << (total/grid.size()) << "s avg)\n";

    return 0;
}
