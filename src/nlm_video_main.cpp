extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

#include "nlm_core.h"
#include "nlm_video_temporal.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <chrono>
#include <dispatch/dispatch.h>

// ── CLI ────────────────────────────────────────────────────────────────

struct VideoConfig {
    std::string input_path;
    std::string output_path;
    std::string preset = "medium";
    std::string codec = "h264";
    float strength = 0.5f;
    int crf = 18;
    bool verbose = false;
    bool temporal = false;
    int frame_count = 3;
    float temporal_weight = 0.8f;
    bool benchmark = false;
};

static const char* USAGE = R"(Usage: nlm-video input.mp4 output.mp4 [options]

Options:
  --preset PRESET    Quality/speed tradeoff (default: medium)
                       veryslow  - adaptive h, best quality
                       slow      - Metal GPU
                       medium    - CPU NEON+GCD
                       fast      - multi-resolution
                       veryfast  - wavelet, real-time
  --temporal         Multi-frame temporal denoising (reduces flicker)
  --frame-count N     Temporal frames to buffer (default: 3, range: 1-7)
  --temporal-weight FLOAT  Weight decay per frame offset (default: 0.8)
  --strength FLOAT   Denoising strength 0.0-1.0 (default: 0.5)
                       Maps to NLM filter strength h
  --crf N            Output quality, lower=better, 0-51 (default: 18)
                       x264/x265 rate control
  --verbose          Show progress per frame
  --benchmark        Show per-frame timing and fps summary
  --codec CODEC      Output codec: h264, h265, av1 (default: h264)
  --preset PRESET    Quality/speed tradeoff or content type:
                       content presets: film, grain, lowlight, animation
                       speed presets (as above)
  --help             Show this message
)";

static bool parse_video_args(int argc, char* argv[], VideoConfig& config) {
    if (argc < 3) {
        std::cerr << USAGE;
        return false;
    }
    config.input_path = argv[1];
    config.output_path = argv[2];

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--preset" && i + 1 < argc) {
            config.preset = argv[++i];
        } else if (arg == "--strength" && i + 1 < argc) {
            config.strength = std::stof(argv[++i]);
        } else if (arg == "--crf" && i + 1 < argc) {
            config.crf = std::stoi(argv[++i]);
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--temporal") {
            config.temporal = true;
        } else if (arg == "--frame-count" && i + 1 < argc) {
            config.frame_count = std::stoi(argv[++i]);
        } else if (arg == "--temporal-weight" && i + 1 < argc) {
            config.temporal_weight = std::stof(argv[++i]);
        } else if (arg == "--benchmark") {
            config.benchmark = true;
        } else if (arg == "--codec" && i + 1 < argc) {
            config.codec = argv[++i];
        } else if (arg == "--preset" && i + 1 < argc) {
            config.preset = argv[++i];
        } else if (arg == "--help") {
            std::cout << USAGE;
            return false;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n" << USAGE;
            return false;
        }
    }

    if (config.strength < 0.0f || config.strength > 1.0f) {
        std::cerr << "--strength must be 0.0-1.0\n";
        return false;
    }
    if (config.crf < 0 || config.crf > 51) {
        std::cerr << "--crf must be 0-51\n";
        return false;
    }
    if (config.frame_count < 1 || config.frame_count > 7) {
        std::cerr << "--frame-count must be 1-7\n";
        return false;
    }
    if (config.temporal_weight <= 0.0f || config.temporal_weight > 1.0f) {
        std::cerr << "--temporal-weight must be >0.0 and <=1.0\n";
        return false;
    }
    if (config.frame_count > 1) {
        config.temporal = true;
    }
    return true;
}

// ── NLM pipeline dispatch ──────────────────────────────────────────────

using NlmPipelineFn = void (*)(const Image&, Image&, const NlmParams&);

static NlmPipelineFn resolve_pipeline(const VideoConfig& config, NlmParams& params) {
    params.h = config.strength * 0.20f + 0.05f;
    params.patch_size = 7;
    params.search_window = 21;

    // Content presets (override all settings)
    if (config.preset == "film") {
        params.h = 0.08f;
        params.patch_size = 7;
        params.search_window = 15;
        return nlm_denoise_adaptive;
    }
    if (config.preset == "grain") {
        params.h = 0.15f;
        params.patch_size = 5;
        params.search_window = 21;
        return nlm_denoise_metal;
    }
    if (config.preset == "lowlight") {
        params.h = 0.05f;
        params.patch_size = 5;
        params.search_window = 21;
        return nlm_denoise_adaptive;
    }
    if (config.preset == "animation") {
        params.h = 0.05f;
        params.patch_size = 5;
        params.search_window = 15;
        return nlm_denoise_wavelet;
    }

    // Speed presets
    if (config.preset == "veryslow") {
        return nlm_denoise_adaptive;
    } else if (config.preset == "slow") {
        return nlm_denoise_metal;
    } else if (config.preset == "medium") {
        return nlm_denoise_cpu_neon;
    } else if (config.preset == "fast") {
        params.patch_size = 5;
        params.search_window = 15;
        return nlm_denoise_cpu_neon_fast;
    } else if (config.preset == "veryfast") {
        params.patch_size = 5;
        params.search_window = 15;
        return nlm_denoise_wavelet;
    }

    std::cerr << "Unknown preset: " << config.preset << ", using medium\n";
    return nlm_denoise_cpu_neon;
}

// ── main ───────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    VideoConfig config;
    if (!parse_video_args(argc, argv, config)) return 1;

    NlmParams nlm_params;
    nlm_params.verbose = false;
    NlmPipelineFn nlm_pipeline = resolve_pipeline(config, nlm_params);

    TemporalConfig tconfig;
    tconfig.frame_count = config.frame_count;
    tconfig.temporal_weight = config.temporal_weight;
    TemporalDenoiser* tdenoiser = nullptr;
    if (config.temporal) {
        tdenoiser = new TemporalDenoiser(tconfig, nlm_params);
    }

    // --- 1-6. Open input, decoder, output, encoder, audio copy ---
    AVFormatContext* in_fmt = nullptr;
    if (avformat_open_input(&in_fmt, config.input_path.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Error: cannot open " << config.input_path << "\n"; return 1;
    }
    if (avformat_find_stream_info(in_fmt, nullptr) < 0) {
        std::cerr << "Error: cannot find stream info\n"; return 1;
    }

    int video_idx = -1;
    std::vector<int> audio_indices;
    for (unsigned i = 0; i < in_fmt->nb_streams; i++) {
        AVMediaType t = in_fmt->streams[i]->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_VIDEO && video_idx < 0) video_idx = (int)i;
        else if (t == AVMEDIA_TYPE_AUDIO) audio_indices.push_back((int)i);
    }
    if (video_idx < 0) { std::cerr << "Error: no video stream found\n"; return 1; }

    AVStream* in_video_stream = in_fmt->streams[video_idx];
    AVCodecParameters* in_codecpar = in_video_stream->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(in_codecpar->codec_id);
    if (!decoder) { std::cerr << "Error: no decoder\n"; return 1; }
    AVCodecContext* dec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(dec_ctx, in_codecpar);
    if (avcodec_open2(dec_ctx, decoder, nullptr) < 0) {
        std::cerr << "Error: cannot open video decoder\n"; return 1;
    }

    int width = dec_ctx->width;
    int height = dec_ctx->height;
    AVPixelFormat dec_pix_fmt = dec_ctx->pix_fmt;

    if (config.verbose) {
        std::cout << "Input: " << width << "x" << height
                  << " codec=" << avcodec_get_name(in_codecpar->codec_id)
                  << " pix_fmt=" << av_get_pix_fmt_name(dec_pix_fmt)
                  << " fps=" << av_q2d(in_video_stream->avg_frame_rate) << "\n";
        std::cout << "Preset: " << config.preset
                  << " strength=" << config.strength
                  << " h=" << nlm_params.h
                  << " patch=" << nlm_params.patch_size
                  << " search=" << nlm_params.search_window << "\n";
    }

    AVFormatContext* out_fmt = nullptr;
    avformat_alloc_output_context2(&out_fmt, nullptr, nullptr, config.output_path.c_str());
    if (!out_fmt) { std::cerr << "Error: cannot create output\n"; return 1; }

    // Codec selection
    AVCodecID enc_id = AV_CODEC_ID_H264;
    if (config.codec == "h265" || config.codec == "hevc") {
        enc_id = AV_CODEC_ID_HEVC;
    } else if (config.codec == "av1") {
        enc_id = AV_CODEC_ID_AV1;
    }
    const AVCodec* encoder = avcodec_find_encoder(enc_id);
    if (!encoder) {
        std::cerr << "Error: encoder for " << config.codec << " not available, falling back to h264\n";
        encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        enc_id = AV_CODEC_ID_H264;
    }
    if (!encoder) { std::cerr << "Error: no encoder available\n"; return 1; }
    AVCodecContext* enc_ctx = avcodec_alloc_context3(encoder);
    enc_ctx->width = width;
    enc_ctx->height = height;
    enc_ctx->time_base = in_video_stream->time_base;
    enc_ctx->framerate = in_video_stream->avg_frame_rate;
    enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    enc_ctx->gop_size = 12;
    enc_ctx->max_b_frames = 2;
    av_opt_set_int(enc_ctx->priv_data, "crf", config.crf, 0);
    av_opt_set(enc_ctx->priv_data, "preset", "medium", 0);
    if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER)
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(enc_ctx, encoder, nullptr) < 0) {
        std::cerr << "Error: cannot open encoder\n"; return 1;
    }

    AVStream* out_video_stream = avformat_new_stream(out_fmt, nullptr);
    avcodec_parameters_from_context(out_video_stream->codecpar, enc_ctx);
    out_video_stream->time_base = enc_ctx->time_base;
    out_video_stream->avg_frame_rate = in_video_stream->avg_frame_rate;

    std::vector<int> out_audio_indices;
    for (int ai : audio_indices) {
        AVStream* in_audio = in_fmt->streams[ai];
        AVStream* out_audio = avformat_new_stream(out_fmt, nullptr);
        avcodec_parameters_copy(out_audio->codecpar, in_audio->codecpar);
        out_audio->time_base = in_audio->time_base;
        out_audio_indices.push_back((int)(out_fmt->nb_streams - 1));
    }

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt->pb, config.output_path.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Error: cannot open output file\n"; return 1;
        }
    }
    if (avformat_write_header(out_fmt, nullptr) < 0) {
        std::cerr << "Error: cannot write header\n"; return 1;
    }

    // --- 7. swscale contexts ---
    SwsContext* to_rgb = sws_getContext(width, height, dec_pix_fmt,
                                        width, height, AV_PIX_FMT_RGB24,
                                        SWS_BILINEAR, nullptr, nullptr, nullptr);
    SwsContext* from_rgb = sws_getContext(width, height, AV_PIX_FMT_RGB24,
                                          width, height, AV_PIX_FMT_YUV420P,
                                          SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!to_rgb || !from_rgb) {
        std::cerr << "Error: cannot create swscale contexts\n"; return 1;
    }

    if (config.verbose) {
        std::cout << "Output codec: " << config.codec << "\n";
    }
    AVFrame* dec_frame = av_frame_alloc();
    AVFrame* rgb_frame = av_frame_alloc();
    rgb_frame->format = AV_PIX_FMT_RGB24;
    rgb_frame->width = width; rgb_frame->height = height;
    av_frame_get_buffer(rgb_frame, 0);

    AVFrame* enc_frame = av_frame_alloc();
    enc_frame->format = AV_PIX_FMT_YUV420P;
    enc_frame->width = width; enc_frame->height = height;
    av_frame_get_buffer(enc_frame, 0);

    AVPacket* in_pkt = av_packet_alloc();
    AVPacket* out_pkt = av_packet_alloc();

    // --- 9. Async pipeline: NLM on serial queue, main thread decodes+encodes ---
    dispatch_semaphore_t frame_ready = dispatch_semaphore_create(0);
    dispatch_semaphore_t slot_free   = dispatch_semaphore_create(1);
    dispatch_queue_t nlm_q = dispatch_queue_create("nlm.denoise", DISPATCH_QUEUE_SERIAL);

    std::vector<double>* frame_times_ptr = config.benchmark ? new std::vector<double>() : nullptr;
    auto wall_start = std::chrono::high_resolution_clock::now();

    int64_t frame_num = 0;
    int64_t total_frames = in_video_stream->nb_frames;
    if (total_frames <= 0 && in_fmt->duration > 0) {
        total_frames = (int64_t)(av_q2d(av_make_q(1, AV_TIME_BASE)) *
                                 (double)in_fmt->duration *
                                 av_q2d(in_video_stream->avg_frame_rate));
    }

    double etime_sum = 0;
    int etime_samples = 0;

    // Scene detection: histogram difference between consecutive frames
    int prev_frame_hist[3] = {0, 0, 0};
    bool prev_hist_valid = false;
    auto detect_scene = [&](const uint8_t* rgb_data) {
        int hist[3] = {0, 0, 0};
        for (int i = 0; i < width * height; i++) {
            hist[0] += rgb_data[i * 3];
            hist[1] += rgb_data[i * 3 + 1];
            hist[2] += rgb_data[i * 3 + 2];
        }
        if (!prev_hist_valid) {
            for (int c = 0; c < 3; c++) prev_frame_hist[c] = hist[c];
            prev_hist_valid = true;
            return false;
        }
        double diff = 0;
        for (int c = 0; c < 3; c++) {
            double d = (double)(hist[c] - prev_frame_hist[c]);
            diff += d * d;
        }
        diff = std::sqrt(diff) / (width * height);
        for (int c = 0; c < 3; c++) prev_frame_hist[c] = hist[c];
        return diff > 5.0;
    };

    __block Image pending_src, pending_dst;
    __block bool pending_valid = false;
    __block double pending_nlm_ms = 0;

    while (av_read_frame(in_fmt, in_pkt) >= 0) {
        if (in_pkt->stream_index != video_idx) {
            auto it = std::find(audio_indices.begin(), audio_indices.end(), in_pkt->stream_index);
            if (it != audio_indices.end()) {
                int out_idx = out_audio_indices[it - audio_indices.begin()];
                AVStream* out_st = out_fmt->streams[out_idx];
                AVStream* in_st = in_fmt->streams[in_pkt->stream_index];
                in_pkt->stream_index = out_idx;
                av_packet_rescale_ts(in_pkt, in_st->time_base, out_st->time_base);
                av_interleaved_write_frame(out_fmt, in_pkt);
            }
            av_packet_unref(in_pkt);
            continue;
        }

        if (avcodec_send_packet(dec_ctx, in_pkt) < 0) {
            av_packet_unref(in_pkt); continue;
        }
        av_packet_unref(in_pkt);

        while (avcodec_receive_frame(dec_ctx, dec_frame) == 0) {
            // Wait for previous NLM to finish, then encode
            if (pending_valid) {
                dispatch_semaphore_wait(frame_ready, DISPATCH_TIME_FOREVER);
                pending_valid = false;

                if (config.benchmark && frame_times_ptr) {
                    frame_times_ptr->push_back(pending_nlm_ms);
                }

                // Image→RGB24
                for (int y = 0; y < height; y++) {
                    uint8_t* row = rgb_frame->data[0] + y * rgb_frame->linesize[0];
                    for (int x = 0; x < width; x++) {
                        int off = (y * width + x) * 3;
                        float v;
                        v = pending_dst.at(x, y, 0); v = v < 0 ? 0 : v > 1 ? 1 : v;
                        row[x * 3]     = (uint8_t)(v * 255.0f + 0.5f);
                        v = pending_dst.at(x, y, 1); v = v < 0 ? 0 : v > 1 ? 1 : v;
                        row[x * 3 + 1] = (uint8_t)(v * 255.0f + 0.5f);
                        v = pending_dst.at(x, y, 2); v = v < 0 ? 0 : v > 1 ? 1 : v;
                        row[x * 3 + 2] = (uint8_t)(v * 255.0f + 0.5f);
                    }
                }

                sws_scale(from_rgb,
                          rgb_frame->data, rgb_frame->linesize, 0, height,
                          enc_frame->data, enc_frame->linesize);

                if (avcodec_send_frame(enc_ctx, enc_frame) < 0) {
                    std::cerr << "Error sending frame to encoder\n";
                }
                while (avcodec_receive_packet(enc_ctx, out_pkt) == 0) {
                    out_pkt->stream_index = out_video_stream->index;
                    av_packet_rescale_ts(out_pkt, enc_ctx->time_base, out_video_stream->time_base);
                    av_interleaved_write_frame(out_fmt, out_pkt);
                    av_packet_unref(out_pkt);
                }
            }

            // Decode current frame
            dispatch_semaphore_wait(slot_free, DISPATCH_TIME_FOREVER);

            sws_scale(to_rgb,
                      dec_frame->data, dec_frame->linesize, 0, height,
                      rgb_frame->data, rgb_frame->linesize);

            Image cur_src;
            cur_src.width = width; cur_src.height = height; cur_src.channels = 3;
            cur_src.data.resize(width * height * 3);
            for (int y = 0; y < height; y++) {
                const uint8_t* row = rgb_frame->data[0] + y * rgb_frame->linesize[0];
                for (int x = 0; x < width; x++) {
                    int off = (y * width + x) * 3;
                    cur_src.data[off]     = row[x * 3]     / 255.0f;
                    cur_src.data[off + 1] = row[x * 3 + 1] / 255.0f;
                    cur_src.data[off + 2] = row[x * 3 + 2] / 255.0f;
                }
            }

            // Kick off async NLM
            pending_src = cur_src;
            pending_valid = true;

            dispatch_async(nlm_q, ^{
                auto t0 = std::chrono::high_resolution_clock::now();
                Image result;
                if (tdenoiser) {
                    result = tdenoiser->denoise(pending_src);
                } else {
                    nlm_pipeline(pending_src, result, nlm_params);
                }
                auto t1 = std::chrono::high_resolution_clock::now();
                pending_nlm_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                pending_dst = result;
                dispatch_semaphore_signal(slot_free);
                dispatch_semaphore_signal(frame_ready);
            });

            frame_num++;
            if (config.verbose) {
                etime_samples++;
                auto tnow = std::chrono::high_resolution_clock::now();
                double elapsed_s = std::chrono::duration<double>(tnow - wall_start).count();
                double fps = frame_num / elapsed_s;
                double eta = (total_frames > 0 && etime_samples > 1)
                    ? (total_frames - frame_num) / fps : 0;

                if (total_frames > 0) {
                    int pct = (int)(frame_num * 100 / total_frames);
                    std::cout << "\r  [" << pct << "%] frame " << frame_num << "/" << total_frames
                              << " @ " << std::fixed << std::setprecision(1) << fps << " fps";
                    if (eta > 0) {
                        std::cout << " ETA " << (int)eta << "s";
                    }
                } else {
                    std::cout << "\r  frame " << frame_num;
                }

                // Scene detection log
                const uint8_t* rgb_data = rgb_frame->data[0];
                if (detect_scene(rgb_data)) {
                    std::cout << " [new scene]\n";
                }

                std::cout << std::flush;
            }

            av_frame_unref(dec_frame);
        }
    }

    // Drain final pending frame
    if (pending_valid) {
        dispatch_semaphore_wait(frame_ready, DISPATCH_TIME_FOREVER);
        if (config.benchmark && frame_times_ptr) {
            frame_times_ptr->push_back(pending_nlm_ms);
        }

        for (int y = 0; y < height; y++) {
            uint8_t* row = rgb_frame->data[0] + y * rgb_frame->linesize[0];
            for (int x = 0; x < width; x++) {
                int off = (y * width + x) * 3;
                float v;
                v = pending_dst.at(x, y, 0); v = v < 0 ? 0 : v > 1 ? 1 : v;
                row[x * 3]     = (uint8_t)(v * 255.0f + 0.5f);
                v = pending_dst.at(x, y, 1); v = v < 0 ? 0 : v > 1 ? 1 : v;
                row[x * 3 + 1] = (uint8_t)(v * 255.0f + 0.5f);
                v = pending_dst.at(x, y, 2); v = v < 0 ? 0 : v > 1 ? 1 : v;
                row[x * 3 + 2] = (uint8_t)(v * 255.0f + 0.5f);
            }
        }
        sws_scale(from_rgb,
                  rgb_frame->data, rgb_frame->linesize, 0, height,
                  enc_frame->data, enc_frame->linesize);

        if (avcodec_send_frame(enc_ctx, enc_frame) < 0) {
            std::cerr << "Error sending frame to encoder\n";
        }
        while (avcodec_receive_packet(enc_ctx, out_pkt) == 0) {
            out_pkt->stream_index = out_video_stream->index;
            av_packet_rescale_ts(out_pkt, enc_ctx->time_base, out_video_stream->time_base);
            av_interleaved_write_frame(out_fmt, out_pkt);
            av_packet_unref(out_pkt);
        }
    }

    // Flush encoder
    avcodec_send_frame(enc_ctx, nullptr);
    while (avcodec_receive_packet(enc_ctx, out_pkt) == 0) {
        out_pkt->stream_index = out_video_stream->index;
        av_packet_rescale_ts(out_pkt, enc_ctx->time_base, out_video_stream->time_base);
        av_interleaved_write_frame(out_fmt, out_pkt);
        av_packet_unref(out_pkt);
    }
    av_write_trailer(out_fmt);

    if (config.verbose || config.benchmark) {
        auto wall_end = std::chrono::high_resolution_clock::now();
        double wall_s = std::chrono::duration<double>(wall_end - wall_start).count();
        double fps = frame_num / wall_s;

        if (config.verbose) {
            std::cout << "\r  done: " << frame_num << " frames"
                      << " in " << wall_s << " s"
                      << " (" << fps << " fps)" << std::endl;
        }
        if (config.benchmark) {
            std::cout << "Benchmark:\n"
                      << "  frames: " << frame_num << "\n"
                      << "  wall:   " << wall_s << " s\n"
                      << "  fps:    " << fps << "\n";
            if (frame_times_ptr && !frame_times_ptr->empty()) {
                std::sort(frame_times_ptr->begin(), frame_times_ptr->end());
                double median = (*frame_times_ptr)[frame_times_ptr->size() / 2];
                std::cout << "  median: " << median << " ms/frame (NLM only)\n";
            }
        }
    }

    // --- Cleanup ---
    delete tdenoiser;
    delete frame_times_ptr;
    av_frame_free(&dec_frame);
    av_frame_free(&rgb_frame);
    av_frame_free(&enc_frame);
    av_packet_free(&in_pkt);
    av_packet_free(&out_pkt);
    sws_freeContext(to_rgb);
    sws_freeContext(from_rgb);
    avcodec_free_context(&dec_ctx);
    avcodec_free_context(&enc_ctx);
    avformat_close_input(&in_fmt);
    if (out_fmt && !(out_fmt->oformat->flags & AVFMT_NOFILE))
        avio_closep(&out_fmt->pb);
    avformat_free_context(out_fmt);

    return 0;
}
