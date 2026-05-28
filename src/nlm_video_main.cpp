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
#include <vector>
#include <cstring>

// ── CLI ────────────────────────────────────────────────────────────────

struct VideoConfig {
    std::string input_path;
    std::string output_path;
    std::string preset = "medium";
    float strength = 0.5f;
    int crf = 18;
    bool verbose = false;
    bool temporal = false;
    int frame_count = 3;
    float temporal_weight = 0.8f;
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
    // strength → h: linear mapping 0..1 → 0.05..0.25
    params.h = config.strength * 0.20f + 0.05f;
    params.patch_size = 7;
    params.search_window = 21;

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
    nlm_params.verbose = false; // we handle progress ourselves
    NlmPipelineFn nlm_pipeline = resolve_pipeline(config, nlm_params);

    // Temporal denoiser (if enabled)
    TemporalConfig tconfig;
    tconfig.frame_count = config.frame_count;
    tconfig.temporal_weight = config.temporal_weight;
    TemporalDenoiser* tdenoiser = nullptr;
    if (config.temporal) {
        tdenoiser = new TemporalDenoiser(tconfig, nlm_params);
    }

    // --- 1. Open input ---
    AVFormatContext* in_fmt = nullptr;
    if (avformat_open_input(&in_fmt, config.input_path.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Error: cannot open " << config.input_path << "\n";
        return 1;
    }
    if (avformat_find_stream_info(in_fmt, nullptr) < 0) {
        std::cerr << "Error: cannot find stream info\n";
        return 1;
    }

    // Find video stream
    int video_idx = -1;
    std::vector<int> audio_indices;
    for (unsigned i = 0; i < in_fmt->nb_streams; i++) {
        AVMediaType t = in_fmt->streams[i]->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_VIDEO && video_idx < 0) {
            video_idx = (int)i;
        } else if (t == AVMEDIA_TYPE_AUDIO) {
            audio_indices.push_back((int)i);
        }
    }
    if (video_idx < 0) {
        std::cerr << "Error: no video stream found\n";
        return 1;
    }

    // --- 2. Open video decoder ---
    AVStream* in_video_stream = in_fmt->streams[video_idx];
    AVCodecParameters* in_codecpar = in_video_stream->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(in_codecpar->codec_id);
    if (!decoder) {
        std::cerr << "Error: no decoder for video codec\n";
        return 1;
    }
    AVCodecContext* dec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(dec_ctx, in_codecpar);
    if (avcodec_open2(dec_ctx, decoder, nullptr) < 0) {
        std::cerr << "Error: cannot open video decoder\n";
        return 1;
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
                  << " h=" << nlm_params.h << "\n";
    }

    // --- 3. Create output ---
    AVFormatContext* out_fmt = nullptr;
    avformat_alloc_output_context2(&out_fmt, nullptr, nullptr, config.output_path.c_str());
    if (!out_fmt) {
        std::cerr << "Error: cannot create output context for " << config.output_path << "\n";
        return 1;
    }

    // --- 4. Video encoder setup ---
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!encoder) {
        std::cerr << "Error: H.264 encoder not available\n";
        return 1;
    }
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
        std::cerr << "Error: cannot open encoder\n";
        return 1;
    }

    AVStream* out_video_stream = avformat_new_stream(out_fmt, nullptr);
    avcodec_parameters_from_context(out_video_stream->codecpar, enc_ctx);
    out_video_stream->time_base = enc_ctx->time_base;
    out_video_stream->avg_frame_rate = in_video_stream->avg_frame_rate;

    // --- 5. Audio stream copy ---
    std::vector<int> out_audio_indices;
    for (int ai : audio_indices) {
        AVStream* in_audio = in_fmt->streams[ai];
        AVStream* out_audio = avformat_new_stream(out_fmt, nullptr);
        avcodec_parameters_copy(out_audio->codecpar, in_audio->codecpar);
        out_audio->time_base = in_audio->time_base;
        out_audio_indices.push_back((int)(out_fmt->nb_streams - 1));
    }

    // --- 6. Open output file ---
    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt->pb, config.output_path.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Error: cannot open output file\n";
            return 1;
        }
    }

    if (avformat_write_header(out_fmt, nullptr) < 0) {
        std::cerr << "Error: cannot write header\n";
        return 1;
    }

    // --- 7. swscale contexts ---
    // Decoder output → RGB24 (for NLM input)
    SwsContext* to_rgb = sws_getContext(width, height, dec_pix_fmt,
                                        width, height, AV_PIX_FMT_RGB24,
                                        SWS_BILINEAR, nullptr, nullptr, nullptr);
    // RGB24 → YUV420P (for encoder input)
    SwsContext* from_rgb = sws_getContext(width, height, AV_PIX_FMT_RGB24,
                                          width, height, AV_PIX_FMT_YUV420P,
                                          SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!to_rgb || !from_rgb) {
        std::cerr << "Error: cannot create swscale contexts\n";
        return 1;
    }

    // --- 8. Frame buffers ---
    AVFrame* dec_frame = av_frame_alloc();
    AVFrame* rgb_frame = av_frame_alloc();
    rgb_frame->format = AV_PIX_FMT_RGB24;
    rgb_frame->width = width;
    rgb_frame->height = height;
    av_frame_get_buffer(rgb_frame, 0);

    AVFrame* enc_frame = av_frame_alloc();
    enc_frame->format = AV_PIX_FMT_YUV420P;
    enc_frame->width = width;
    enc_frame->height = height;
    av_frame_get_buffer(enc_frame, 0);

    AVPacket* in_pkt = av_packet_alloc();
    AVPacket* out_pkt = av_packet_alloc();

    Image src_img, dst_img;

    // --- 9. Processing loop ---
    int64_t frame_num = 0;
    int64_t total_frames = in_video_stream->nb_frames;
    if (total_frames <= 0 && in_fmt->duration > 0) {
        total_frames = (int64_t)(av_q2d(av_make_q(1, AV_TIME_BASE)) *
                                 (double)in_fmt->duration *
                                 av_q2d(in_video_stream->avg_frame_rate));
    }

    while (av_read_frame(in_fmt, in_pkt) >= 0) {
        // Audio: stream copy
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

        // Video: decode
        if (avcodec_send_packet(dec_ctx, in_pkt) < 0) {
            av_packet_unref(in_pkt);
            continue;
        }
        av_packet_unref(in_pkt);

        while (avcodec_receive_frame(dec_ctx, dec_frame) == 0) {
            // Convert decoder frame → RGB24
            sws_scale(to_rgb,
                      dec_frame->data, dec_frame->linesize, 0, height,
                      rgb_frame->data, rgb_frame->linesize);

            // Build Image from RGB24 data
            src_img.width = width;
            src_img.height = height;
            src_img.channels = 3;
            src_img.data.resize(width * height * 3);
            for (int y = 0; y < height; y++) {
                const uint8_t* row = rgb_frame->data[0] + y * rgb_frame->linesize[0];
                for (int x = 0; x < width; x++) {
                    int off = (y * width + x) * 3;
                    src_img.data[off]     = row[x * 3]     / 255.0f;
                    src_img.data[off + 1] = row[x * 3 + 1] / 255.0f;
                    src_img.data[off + 2] = row[x * 3 + 2] / 255.0f;
                }
            }

            // NLM denoise (temporal or spatial)
            if (tdenoiser) {
                dst_img = tdenoiser->denoise(src_img);
            } else {
                nlm_pipeline(src_img, dst_img, nlm_params);
            }

            // Convert denoised Image → RGB24 buffer (reuse rgb_frame)
            for (int y = 0; y < height; y++) {
                uint8_t* row = rgb_frame->data[0] + y * rgb_frame->linesize[0];
                for (int x = 0; x < width; x++) {
                    int off = (y * width + x) * 3;
                    float v;
                    v = dst_img.at(x, y, 0); v = v < 0 ? 0 : v > 1 ? 1 : v;
                    row[x * 3]     = (uint8_t)(v * 255.0f + 0.5f);
                    v = dst_img.at(x, y, 1); v = v < 0 ? 0 : v > 1 ? 1 : v;
                    row[x * 3 + 1] = (uint8_t)(v * 255.0f + 0.5f);
                    v = dst_img.at(x, y, 2); v = v < 0 ? 0 : v > 1 ? 1 : v;
                    row[x * 3 + 2] = (uint8_t)(v * 255.0f + 0.5f);
                }
            }

            // Convert denoised RGB24 → YUV420P
            sws_scale(from_rgb,
                      rgb_frame->data, rgb_frame->linesize, 0, height,
                      enc_frame->data, enc_frame->linesize);

            enc_frame->pts = dec_frame->pts;

            // Encode
            if (avcodec_send_frame(enc_ctx, enc_frame) < 0) {
                std::cerr << "Error sending frame to encoder\n";
            }

            while (avcodec_receive_packet(enc_ctx, out_pkt) == 0) {
                out_pkt->stream_index = out_video_stream->index;
                av_packet_rescale_ts(out_pkt, enc_ctx->time_base, out_video_stream->time_base);
                av_interleaved_write_frame(out_fmt, out_pkt);
                av_packet_unref(out_pkt);
            }

            frame_num++;
            if (config.verbose) {
                if (total_frames > 0) {
                    int pct = (int)(frame_num * 100 / total_frames);
                    std::cout << "\r  frame " << frame_num << "/" << total_frames
                              << " (" << pct << "%)" << std::flush;
                } else {
                    std::cout << "\r  frame " << frame_num << std::flush;
                }
            }

            av_frame_unref(dec_frame);
        }
    }

    // --- 10. Flush encoder ---
    avcodec_send_frame(enc_ctx, nullptr);
    while (avcodec_receive_packet(enc_ctx, out_pkt) == 0) {
        out_pkt->stream_index = out_video_stream->index;
        av_packet_rescale_ts(out_pkt, enc_ctx->time_base, out_video_stream->time_base);
        av_interleaved_write_frame(out_fmt, out_pkt);
        av_packet_unref(out_pkt);
    }

    av_write_trailer(out_fmt);

    if (config.verbose) {
        std::cout << "\r  done: " << frame_num << " frames\n";
    }

    // --- Cleanup ---
    delete tdenoiser;
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
