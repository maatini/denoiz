#include "nlm_video_tuning.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <ctime>
#include <cstdlib>
#include <sys/stat.h>
#include <dispatch/dispatch.h>

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
    }
}

double parse_time(const std::string& s) {
    auto cols = split(s, ':');
    if (cols.size() == 3) return std::stod(cols[0]) * 3600.0 + std::stod(cols[1]) * 60.0 + std::stod(cols[2]);
    if (cols.size() == 2) return std::stod(cols[0]) * 60.0 + std::stod(cols[1]);
    return std::stod(s);
}

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

// ── Segment Extraction (via ffmpeg CLI) ────────────────────────────────

struct SegmentFrames {
    std::vector<Image> frames;
    int width = 0, height = 0;
    double fps = 30.0;
};

static bool decode_simple_mp4(const std::string& path, SegmentFrames& result, bool verbose) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0)
        { std::cerr << "Tuning: cannot open " << path << "\n"; return false; }
    if (avformat_find_stream_info(fmt, nullptr) < 0)
        { std::cerr << "Tuning: stream info failed\n"; avformat_close_input(&fmt); return false; }

    int vidx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; i++)
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { vidx = (int)i; break; }
    if (vidx < 0) { std::cerr << "Tuning: no video\n"; avformat_close_input(&fmt); return false; }

    AVStream* vs = fmt->streams[vidx];
    result.fps = av_q2d(vs->avg_frame_rate);
    if (result.fps <= 0) result.fps = 30.0;

    const AVCodec* dec = avcodec_find_decoder(vs->codecpar->codec_id);
    AVCodecContext* dctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dctx, vs->codecpar);
    avcodec_open2(dctx, dec, nullptr);

    int W = dctx->width, H = dctx->height;
    result.width = W; result.height = H;

    SwsContext* to_rgb = sws_getContext(W, H, dctx->pix_fmt, W, H, AV_PIX_FMT_RGB24,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
    AVFrame* df = av_frame_alloc();
    AVFrame* rf = av_frame_alloc();
    rf->format = AV_PIX_FMT_RGB24; rf->width = W; rf->height = H;
    av_frame_get_buffer(rf, 0);
    AVPacket* pkt = av_packet_alloc();

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != vidx) { av_packet_unref(pkt); continue; }
        avcodec_send_packet(dctx, pkt);
        av_packet_unref(pkt);
        while (avcodec_receive_frame(dctx, df) == 0) {
            sws_scale(to_rgb, df->data, df->linesize, 0, H, rf->data, rf->linesize);
            Image cur; cur.width = W; cur.height = H; cur.channels = 3;
            cur.data.resize((size_t)W * H * 3);
            for (int y = 0; y < H; y++) {
                const uint8_t* r = rf->data[0] + y * rf->linesize[0];
                for (int x = 0; x < W; x++) {
                    int o = (y * W + x) * 3;
                    cur.data[o] = r[x*3]/255.0f;
                    cur.data[o+1] = r[x*3+1]/255.0f;
                    cur.data[o+2] = r[x*3+2]/255.0f;
                }
            }
            result.frames.push_back(std::move(cur));
            av_frame_unref(df);
        }
    }

    av_frame_free(&df); av_frame_free(&rf); av_packet_free(&pkt);
    sws_freeContext(to_rgb); avcodec_free_context(&dctx); avformat_close_input(&fmt);
    if (verbose) std::cout << "Decoded " << result.frames.size() << " frames\n";
    return !result.frames.empty();
}

static bool extract_segment(const std::string& path, double start_sec, double duration_sec,
                             SegmentFrames& result, bool verbose) {
    char tmpname[1024];
    snprintf(tmpname, sizeof(tmpname), "/tmp/nlm_tuning_segment_%d.mp4", (int)time(nullptr));

    {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
            "ffmpeg -y -ss %.3f -t %.3f -i \"%s\" -c copy -an -loglevel error \"%s\"",
            start_sec, duration_sec, path.c_str(), tmpname);
        int ret = system(cmd);
        if (ret != 0) {
            std::cerr << "Tuning: ffmpeg segment extraction failed\n";
            return false;
        }
    }

    bool ok = decode_simple_mp4(tmpname, result, verbose);
    unlink(tmpname);

    if (verbose && ok)
        std::cout << "Extracted " << result.frames.size() << " frames ("
                  << result.width << "x" << result.height << ")\n";
    return ok;
}

// ── Single Param-Set Evaluation ────────────────────────────────────────

static TuningResult evaluate_param_set(
    const std::map<std::string, double>& param_set,
    const SegmentFrames& segment, const std::string& metric_name,
    int test_idx, const std::string& /*output_dir*/)
{
    NlmParams np; np.h = 0.3f;
    apply_param_set(param_set, np);

    TuningResult result;
    result.params = param_set;

    auto t0 = std::chrono::high_resolution_clock::now();

    double ssim_total = 0, psnr_total = 0, perc_total = 0;

    for (size_t i = 0; i < segment.frames.size(); i++) {
        Image dst;
        void (*nlm_fn)(const Image&, Image&, const NlmParams&) = nlm_denoise_cpu_neon;
        if (np.adaptive_mode) nlm_fn = nlm_denoise_adaptive;
        else if (np.wavelet_mode) nlm_fn = nlm_denoise_wavelet;
        else if (np.use_gpu) nlm_fn = nlm_denoise_metal;
        else if (np.fast_mode) nlm_fn = nlm_denoise_cpu_neon_fast;
        nlm_fn(segment.frames[i], dst, np);

        ssim_total += compute_ssim(segment.frames[i], dst);
        psnr_total += ::psnr(segment.frames[i], dst);
        perc_total += compute_perceptual(segment.frames[i], dst);
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    size_t nf = segment.frames.size();
    result.ssim_val = nf > 0 ? ssim_total / nf : 0;
    result.psnr_val = nf > 0 ? psnr_total / nf : 0;
    result.perceptual_val = nf > 0 ? perc_total / nf : 0;

    if (metric_name == "ssim") result.score = result.ssim_val;
    else if (metric_name == "psnr") result.score = result.psnr_val;
    else result.score = result.perceptual_val;

    char buf[1024];
    snprintf(buf, sizeof(buf), "%03d", test_idx);
    result.clip_path = buf;

    std::ostringstream ss;
    ss << "  [" << std::setw(3) << test_idx << "] ";
    for (const auto& kv : result.params) ss << kv.first << "=" << kv.second << " ";
    ss << "-> " << metric_name << "=" << std::fixed << std::setprecision(4) << result.score
       << "  (" << std::setprecision(2) << elapsed << "s)";
    std::cout << ss.str() << std::endl;

    return result;
}

// ── JSON ────────────────────────────────────────────────────────────────

static void write_results_json(const std::string& path,
                               const std::vector<TuningResult>& results,
                               const TuningConfig& config) {
    std::ofstream f(path);
    f << "{\n  \"config\": {\n"
      << "    \"input\": \"" << config.input_path << "\",\n"
      << "    \"start\": " << config.start_sec << ",\n"
      << "    \"duration\": " << config.duration_sec << ",\n"
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

// ── Main ────────────────────────────────────────────────────────────────

int run_tuning(const TuningConfig& config) {
    mkdir(config.output_dir.c_str(), 0755);
    auto t_start = std::chrono::high_resolution_clock::now();

    std::cout << "\n=== Phase 1: Extract test segment ===\n";
    SegmentFrames segment;
    if (!extract_segment(config.input_path, config.start_sec, config.duration_sec,
                         segment, config.verbose)) {
        std::cerr << "Tuning: failed to extract segment\n";
        return 1;
    }
    if (segment.frames.empty()) {
        std::cerr << "Tuning: no frames extracted\n";
        return 1;
    }
    std::cout << "Extracted " << segment.frames.size() << " frames ("
              << segment.width << "x" << segment.height << ")\n";

    std::cout << "\n=== Phase 2: Generate parameter grid ===\n";
    auto axes = parse_param_grid(config.param_grid);
    auto grid = generate_grid(axes);
    std::cout << "Parameters: ";
    for (const auto& ax : axes)
        std::cout << ax.name << "=[" << ax.values.size() << " values] ";
    std::cout << "--> " << grid.size() << " combinations\n";
    if (grid.empty()) { std::cerr << "No parameter combinations generated\n"; return 1; }

    std::cout << "\n=== Phase 3: Evaluate (" << grid.size() << " tests) ===\n";
    std::vector<TuningResult> results;
    results.reserve(grid.size());
    for (size_t i = 0; i < grid.size(); i++)
        results.push_back(evaluate_param_set(grid[i], segment, config.metric,
                                              (int)i + 1, config.output_dir));

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
                  << "  perceptual=" << results[i].perceptual_val << "\n";
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
    write_results_json(json_path, results, config);
    std::cout << "\nResults written to " << json_path << "\n";

    auto t_end = std::chrono::high_resolution_clock::now();
    double total = std::chrono::duration<double>(t_end - t_start).count();
    std::cout << "\nTotal: " << std::fixed << std::setprecision(1) << total
              << "s for " << grid.size() << " tests ("
              << std::setprecision(2) << (total/grid.size()) << "s avg)\n";

    return 0;
}
