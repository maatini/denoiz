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
#include <sys/stat.h>
#include <dispatch/dispatch.h>

struct VideoConfig {
    std::string input_path, output_path, preset = "medium", codec = "h264", frames_out;
    float strength = 0.5f, sharpen = 0.0f;
    int crf = 18;
    bool verbose = false, temporal = false, benchmark = false, deinterlace = false, use_gpu = false;
    int frame_count = 3;
    float temporal_weight = 0.8f;
};

static const char* USAGE = R"(Usage: nlm-video input.mp4 output.mp4 [options]

Options:
  --preset PRESET    Quality/speed tradeoff (default: medium)
                       veryslow / slow / medium / fast / veryfast
                       film / grain / lowlight / animation (content presets)
  --strength FLOAT   Denoising strength 0.0-1.0 (default: 0.5)
  --crf N            Output quality, lower=better (default: 18)
  --codec CODEC      Output codec: h264, h265, av1 (default: h264)
  --temporal         Multi-frame temporal denoising
  --frame-count N    Temporal frames to buffer (default: 3, 1-7)
  --temporal-weight FLOAT  Weight decay per frame offset (default: 0.8)
  --sharpen FLOAT    Unsharp mask 0.0-2.0 (default: 0.0=off)
  --frames-out DIR   Save denoised frames as PNG sequence
  --deinterlace      Enable YADIF deinterlacing on input
  --use-gpu          Force Metal GPU pipeline
  --verbose          Show progress with ETA
  --benchmark        Per-frame timing + fps summary
  --help             Show this message
)";

static bool parse_video_args(int argc, char* argv[], VideoConfig& c) {
    if (argc < 3) { std::cerr << USAGE; return false; }
    c.input_path = argv[1]; c.output_path = argv[2];
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--preset" && i+1<argc) c.preset = argv[++i];
        else if (a == "--strength" && i+1<argc) c.strength = std::stof(argv[++i]);
        else if (a == "--crf" && i+1<argc) c.crf = std::stoi(argv[++i]);
        else if (a == "--codec" && i+1<argc) c.codec = argv[++i];
        else if (a == "--frame-count" && i+1<argc) c.frame_count = std::stoi(argv[++i]);
        else if (a == "--temporal-weight" && i+1<argc) c.temporal_weight = std::stof(argv[++i]);
        else if (a == "--sharpen" && i+1<argc) c.sharpen = std::stof(argv[++i]);
        else if (a == "--frames-out" && i+1<argc) c.frames_out = argv[++i];
        else if (a == "--temporal") c.temporal = true;
        else if (a == "--deinterlace") c.deinterlace = true;
        else if (a == "--use-gpu") c.use_gpu = true;
        else if (a == "--verbose") c.verbose = true;
        else if (a == "--benchmark") c.benchmark = true;
        else if (a == "--help") { std::cout << USAGE; return false; }
        else { std::cerr << "Unknown: " << a << "\n"; return false; }
    }
    if (c.strength<0||c.strength>1) { std::cerr<<"--strength 0-1\n"; return false; }
    if (c.crf<0||c.crf>51) { std::cerr<<"--crf 0-51\n"; return false; }
    if (c.frame_count<1||c.frame_count>7) { std::cerr<<"--frame-count 1-7\n"; return false; }
    if (c.temporal_weight<=0||c.temporal_weight>1) { std::cerr<<"--temporal-weight >0 <=1\n"; return false; }
    if (c.sharpen<0||c.sharpen>2) { std::cerr<<"--sharpen 0-2\n"; return false; }
    if (c.frame_count>1) c.temporal = true;
    return true;
}

using NlmPipelineFn = void (*)(const Image&, Image&, const NlmParams&);

static NlmPipelineFn resolve_pipeline(const VideoConfig& c, NlmParams& p) {
    p.h = c.strength*0.20f+0.05f; p.patch_size=7; p.search_window=21;
    if (c.preset=="film")        { p.h=0.08f;p.patch_size=7;p.search_window=15;return nlm_denoise_adaptive; }
    if (c.preset=="grain")       { p.h=0.15f;p.patch_size=5;p.search_window=21;return nlm_denoise_metal; }
    if (c.preset=="lowlight")    { p.h=0.05f;p.patch_size=5;p.search_window=21;return nlm_denoise_adaptive; }
    if (c.preset=="animation")   { p.h=0.05f;p.patch_size=5;p.search_window=15;return nlm_denoise_wavelet; }
    if (c.preset=="veryslow")    return nlm_denoise_adaptive;
    if (c.preset=="slow")        return nlm_denoise_metal;
    if (c.preset=="medium")      return nlm_denoise_cpu_neon;
    if (c.preset=="fast")        { p.patch_size=5;p.search_window=15;return nlm_denoise_cpu_neon_fast; }
    if (c.preset=="veryfast")    { p.patch_size=5;p.search_window=15;return nlm_denoise_wavelet; }
    std::cerr<<"Unknown preset: "<<c.preset<<", using medium\n";
    return nlm_denoise_cpu_neon;
}

static void apply_sharpen(Image& img, float amount) {
    if (amount<=0) return;
    int w=img.width,h=img.height;
    std::vector<float> orig=img.data;
    for (int y=1;y<h-1;y++) for (int x=1;x<w-1;x++) for (int ch=0;ch<3;ch++) {
        int i=(y*w+x)*3+ch;
        float c=orig[i],b=(orig[((y-1)*w+x)*3+ch]+orig[((y+1)*w+x)*3+ch]+
                            orig[(y*w+(x-1))*3+ch]+orig[(y*w+(x+1))*3+ch])/4.0f;
        float r=c+(c-b)*amount;
        img.data[i]=r<0?0:r>1?1:r;
    }
}

struct PendingState { Image src,dst; bool valid=false; double nlm_ms=0; int64_t pts=0; };

int main(int argc, char* argv[]) {
    VideoConfig c;
    if (!parse_video_args(argc, argv, c)) return 1;
    if (!c.frames_out.empty()) mkdir(c.frames_out.c_str(),0755);

    NlmParams np; np.verbose=false;
    if (c.use_gpu) c.preset="slow";
    NlmPipelineFn pipeline = resolve_pipeline(c, np);

    TemporalDenoiser* td=nullptr;
    if (c.temporal) {
        TemporalConfig tc; tc.frame_count=c.frame_count; tc.temporal_weight=c.temporal_weight;
        td = new TemporalDenoiser(tc, np);
    }

    AVFormatContext* in_fmt=nullptr;
    if (avformat_open_input(&in_fmt,c.input_path.c_str(),nullptr,nullptr)<0)
        { std::cerr<<"Error: cannot open "<<c.input_path<<"\n"; return 1; }
    if (avformat_find_stream_info(in_fmt,nullptr)<0)
        { std::cerr<<"Error: stream info\n"; return 1; }

    int video_idx=-1; std::vector<int> aidx;
    for (unsigned i=0;i<in_fmt->nb_streams;i++)
        if (in_fmt->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO&&video_idx<0) video_idx=(int)i;
        else if (in_fmt->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO) aidx.push_back((int)i);
    if (video_idx<0) { std::cerr<<"Error: no video\n"; return 1; }

    AVStream* vs=in_fmt->streams[video_idx];
    const AVCodec* dec=avcodec_find_decoder(vs->codecpar->codec_id);
    if (!dec) { std::cerr<<"Error: no decoder\n"; return 1; }
    AVCodecContext* dctx=avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dctx,vs->codecpar);
    if (avcodec_open2(dctx,dec,nullptr)<0) { std::cerr<<"Error: decoder open\n"; return 1; }
    int W=dctx->width, H=dctx->height; AVPixelFormat dpix=dctx->pix_fmt;
    if (c.verbose) std::cout<<"Input: "<<W<<"x"<<H<<" codec="<<avcodec_get_name(vs->codecpar->codec_id)
        <<" pix_fmt="<<av_get_pix_fmt_name(dpix)<<" fps="<<av_q2d(vs->avg_frame_rate)<<"\n"
        <<"Preset: "<<c.preset<<" strength="<<c.strength<<" h="<<np.h
        <<" patch="<<np.patch_size<<" search="<<np.search_window<<"\n";

    AVFormatContext* ofmt=nullptr; AVCodecContext* ectx=nullptr;
    AVStream* ovs=nullptr; std::vector<int> oaidx;
    if (c.frames_out.empty()) {
        avformat_alloc_output_context2(&ofmt,nullptr,nullptr,c.output_path.c_str());
        if (!ofmt) { std::cerr<<"Error: output\n"; return 1; }
        AVCodecID eid=AV_CODEC_ID_H264;
        if (c.codec=="h265"||c.codec=="hevc") eid=AV_CODEC_ID_HEVC;
        else if (c.codec=="av1") eid=AV_CODEC_ID_AV1;
        const AVCodec* enc=avcodec_find_encoder(eid);
        if (!enc) { std::cerr<<"Codec "<<c.codec<<" not available, h264\n"; enc=avcodec_find_encoder(AV_CODEC_ID_H264); }
        if (!enc) { std::cerr<<"Error: no encoder\n"; return 1; }
        ectx=avcodec_alloc_context3(enc);
        ectx->width=W; ectx->height=H; ectx->time_base=vs->time_base;
        ectx->framerate=vs->avg_frame_rate; ectx->pix_fmt=AV_PIX_FMT_YUV420P;
        ectx->gop_size=12; ectx->max_b_frames=2;
        av_opt_set_int(ectx->priv_data,"crf",c.crf,0);
        av_opt_set(ectx->priv_data,"preset","medium",0);
        if (ofmt->oformat->flags&AVFMT_GLOBALHEADER) ectx->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(ectx,enc,nullptr)<0) { std::cerr<<"Error: encoder open\n"; return 1; }
        ovs=avformat_new_stream(ofmt,nullptr);
        avcodec_parameters_from_context(ovs->codecpar,ectx);
        ovs->time_base=ectx->time_base; ovs->avg_frame_rate=vs->avg_frame_rate;
        for (int ai:aidx) { AVStream* oa=avformat_new_stream(ofmt,nullptr);
            avcodec_parameters_copy(oa->codecpar,in_fmt->streams[ai]->codecpar);
            oa->time_base=in_fmt->streams[ai]->time_base;
            oaidx.push_back((int)(ofmt->nb_streams-1)); }
        if (!(ofmt->oformat->flags&AVFMT_NOFILE)&&avio_open(&ofmt->pb,c.output_path.c_str(),AVIO_FLAG_WRITE)<0)
            { std::cerr<<"Error: open output\n"; return 1; }
        if (avformat_write_header(ofmt,nullptr)<0) { std::cerr<<"Error: header\n"; return 1; }
    }

    SwsContext* to_rgb=sws_getContext(W,H,dpix,W,H,AV_PIX_FMT_RGB24,SWS_BILINEAR,nullptr,nullptr,nullptr);
    SwsContext* from_rgb=ofmt?sws_getContext(W,H,AV_PIX_FMT_RGB24,W,H,AV_PIX_FMT_YUV420P,SWS_BILINEAR,nullptr,nullptr,nullptr):nullptr;
    if (!to_rgb||(!from_rgb&&ofmt)) { std::cerr<<"Error: swscale\n"; return 1; }

    AVFrame* df=av_frame_alloc();
    AVFrame* rf=av_frame_alloc(); rf->format=AV_PIX_FMT_RGB24;rf->width=W;rf->height=H;av_frame_get_buffer(rf,0);
    AVFrame* ef=ofmt?av_frame_alloc():nullptr;
    if (ef) { ef->format=AV_PIX_FMT_YUV420P;ef->width=W;ef->height=H;av_frame_get_buffer(ef,0); }
    AVPacket* ip=av_packet_alloc(); AVPacket* op=ofmt?av_packet_alloc():nullptr;

    dispatch_semaphore_t ready=dispatch_semaphore_create(0);
    dispatch_semaphore_t slot=dispatch_semaphore_create(1);
    dispatch_queue_t nlmq=dispatch_queue_create("nlm.denoise",DISPATCH_QUEUE_SERIAL);

    std::vector<double>* ft= c.benchmark ? new std::vector<double>() : nullptr;
    auto w0=std::chrono::high_resolution_clock::now();
    int64_t fn=0,tf=vs->nb_frames;
    if (tf<=0&&in_fmt->duration>0) tf=(int64_t)(av_q2d(av_make_q(1,AV_TIME_BASE))*in_fmt->duration*av_q2d(vs->avg_frame_rate));

    PendingState* p=new PendingState();

    auto consume = [&]{
        if (!p->valid) return;
        dispatch_semaphore_wait(ready,DISPATCH_TIME_FOREVER);
        p->valid=false;
        if (c.benchmark&&ft) ft->push_back(p->nlm_ms);
        apply_sharpen(p->dst,c.sharpen);
        if (!c.frames_out.empty()) {
            char buf[1024]; snprintf(buf,sizeof(buf),"%s/frame_%06lld.png",c.frames_out.c_str(),(long long)fn);
            save_image(buf,p->dst);
        } else {
            for (int y=0;y<H;y++) { uint8_t* r=rf->data[0]+y*rf->linesize[0];
                for (int x=0;x<W;x++) { int o=(y*W+x)*3; float v;
                    v=p->dst.at(x,y,0);v=v<0?0:v>1?1:v;r[x*3]=(uint8_t)(v*255+0.5f);
                    v=p->dst.at(x,y,1);v=v<0?0:v>1?1:v;r[x*3+1]=(uint8_t)(v*255+0.5f);
                    v=p->dst.at(x,y,2);v=v<0?0:v>1?1:v;r[x*3+2]=(uint8_t)(v*255+0.5f); } }
            sws_scale(from_rgb,rf->data,rf->linesize,0,H,ef->data,ef->linesize);
            ef->pts = p->pts;
            if (avcodec_send_frame(ectx,ef)<0) std::cerr<<"Error: send frame\n";
            while (avcodec_receive_packet(ectx,op)==0) {
                op->stream_index=ovs->index;
                av_packet_rescale_ts(op,ectx->time_base,ovs->time_base);
                av_interleaved_write_frame(ofmt,op);
                av_packet_unref(op); }
        }
    };

    while (av_read_frame(in_fmt,ip)>=0) {
        if (ip->stream_index!=video_idx) {
            auto it=std::find(aidx.begin(),aidx.end(),ip->stream_index);
            if (it!=aidx.end()&&ofmt) { int oi=oaidx[it-aidx.begin()]; ip->stream_index=oi;
                av_packet_rescale_ts(ip,in_fmt->streams[*it]->time_base,ofmt->streams[oi]->time_base);
                av_interleaved_write_frame(ofmt,ip); }
            av_packet_unref(ip); continue; }
        if (avcodec_send_packet(dctx,ip)<0) { av_packet_unref(ip); continue; }
        av_packet_unref(ip);
        while (avcodec_receive_frame(dctx,df)==0) {
            consume();
            dispatch_semaphore_wait(slot,DISPATCH_TIME_FOREVER);
            sws_scale(to_rgb,df->data,df->linesize,0,H,rf->data,rf->linesize);
            Image cur; cur.width=W;cur.height=H;cur.channels=3;cur.data.resize(W*H*3);
            for (int y=0;y<H;y++) { const uint8_t* r=rf->data[0]+y*rf->linesize[0];
                for (int x=0;x<W;x++) { int o=(y*W+x)*3;
                    cur.data[o]=r[x*3]/255.0f;cur.data[o+1]=r[x*3+1]/255.0f;cur.data[o+2]=r[x*3+2]/255.0f; } }
            p->src=cur; p->valid=true; p->pts=df->pts;
            dispatch_async(nlmq,^{ auto t0=std::chrono::high_resolution_clock::now(); Image r;
                if (td) r=td->denoise(p->src); else pipeline(p->src,r,np);
                auto t1=std::chrono::high_resolution_clock::now();
                p->nlm_ms=std::chrono::duration<double,std::milli>(t1-t0).count();
                p->dst=r; dispatch_semaphore_signal(slot); dispatch_semaphore_signal(ready); });
            fn++;
            if (c.verbose) { auto tn=std::chrono::high_resolution_clock::now();
                double el=std::chrono::duration<double>(tn-w0).count(),fps=fn/el;
                if (tf>0) { int pct=(int)(fn*100/tf); double eta=fps>0?(tf-fn)/fps:0;
                    std::cout<<"\r  ["<<pct<<"%] "<<fn<<"/"<<tf<<" @"<<std::fixed<<std::setprecision(1)<<fps<<"fps ETA "<<(int)eta<<"s"<<std::flush; }
                else std::cout<<"\r  frame "<<fn<<std::flush; }
            av_frame_unref(df); }
    }
    consume();
    if (ofmt) { avcodec_send_frame(ectx,nullptr);
        while (avcodec_receive_packet(ectx,op)==0) { op->stream_index=ovs->index;
            av_packet_rescale_ts(op,ectx->time_base,ovs->time_base); av_interleaved_write_frame(ofmt,op); av_packet_unref(op); }
        av_write_trailer(ofmt); }

    if (c.verbose||c.benchmark) { auto w1=std::chrono::high_resolution_clock::now();
        double ws=std::chrono::duration<double>(w1-w0).count(),fps=fn/ws;
        if (c.verbose) std::cout<<"\r  done: "<<fn<<" frames in "<<ws<<"s ("<<std::fixed<<std::setprecision(1)<<fps<<" fps)"<<std::endl;
        if (c.benchmark) { std::cout<<"Benchmark:\n  frames: "<<fn<<"\n  wall:   "<<ws<<" s\n  fps:    "<<fps<<"\n";
            if (ft&&!ft->empty()) { std::sort(ft->begin(),ft->end()); std::cout<<"  median: "<<(*ft)[ft->size()/2]<<" ms/frame (NLM only)\n"; } } }

    delete td; delete p; delete ft;
    av_frame_free(&df); av_frame_free(&rf); if (ef) av_frame_free(&ef);
    av_packet_free(&ip); if (op) av_packet_free(&op);
    sws_freeContext(to_rgb); if (from_rgb) sws_freeContext(from_rgb);
    avcodec_free_context(&dctx); if (ectx) avcodec_free_context(&ectx);
    avformat_close_input(&in_fmt);
    if (ofmt) { if (!(ofmt->oformat->flags&AVFMT_NOFILE)) avio_closep(&ofmt->pb); avformat_free_context(ofmt); }
    return 0;
}
