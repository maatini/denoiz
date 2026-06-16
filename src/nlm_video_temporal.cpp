#include "nlm_video_temporal.h"

#include <arm_neon.h>
#include <dispatch/dispatch.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// ----------------------------------------------------------------
//  NEON-accelerated patch SSD (replicated from nlm_cpu_neon.cpp)
// ----------------------------------------------------------------
// Computes SSD between a patch centered at (cx, cy) in the source
// and a patch centered at (nx, ny) in the same source image.
// Used for comparing the reference frame's patch with a motion-
// compensated patch in a historical frame.
//
// Identical to patch_ssd_neon3 in nlm_cpu_neon.cpp — the function
// accepts arbitrary (cx,cy) vs (nx,ny) positions, which is exactly
// what we need for motion-compensated comparison.
static inline float patch_ssd_neon3(const float* src, int stride,
                                     int cx, int cy, int nx, int ny,
                                     int half_patch, int w, int h) {
    float32x4_t ssd_vec = vdupq_n_f32(0);

    for (int py = -half_patch; py <= half_patch; ++py) {
        int ry = cy + py;
        int sy = ny + py;
        if (ry < 0 || ry >= h || sy < 0 || sy >= h) continue;

        const float* row_r = src + ry * stride;
        const float* row_s = src + sy * stride;

        for (int px = -half_patch; px <= half_patch; ++px) {
            int rx = cx + px;
            int sx = nx + px;
            if (rx < 0 || rx >= w || sx < 0 || sx >= w) continue;

            int ri = rx * 3;
            int si = sx * 3;

            float buf_a[4] = {row_r[ri], row_r[ri + 1], row_r[ri + 2], 0};
            float buf_b[4] = {row_s[si], row_s[si + 1], row_s[si + 2], 0};
            float32x4_t va = vld1q_f32(buf_a);
            float32x4_t vb = vld1q_f32(buf_b);
            float32x4_t d = vsubq_f32(va, vb);
            ssd_vec = vmlaq_f32(ssd_vec, d, d);
        }
    }
    return vaddvq_f32(ssd_vec);
}

// ----------------------------------------------------------------

// Helper: create an identity (zero) motion field for non-MC temporal.
static MotionField zero_motion_field(int w, int h) {
    MotionField mf;
    mf.frame_w = w;
    mf.frame_h = h;
    mf.data.assign((size_t)w * h * 2, 0.0f);
    return mf;
}
//  Context for GCD-parallel motion-compensated temporal NLM
// ----------------------------------------------------------------
struct TemporalNlmContext {
    const Image* reference;              // frame being denoised
    const std::deque<Image>* history;    // past (and optionally future) frames
    const std::deque<MotionField>* flows;// motion: history[i] → reference
    Image* dst;

    int half_patch;
    float h2;
    float temporal_weight;
    int nframes;                         // number of history frames
    float min_confidence;
};

// Process one row: motion-compensated temporal NLM.
//
// For each pixel (x, y) in the reference frame:
//   1. Reference pixel always contributes with weight 1.0.
//   2. For each history frame i:
//      a. Look up flow at (x, y): how did pixel (x,y) in history[i] move?
//      b. Warped position in history[i]: hx = x - dx, hy = y - dy
//         (reverse the flow to go from reference back to history).
//      c. Compute patch SSD between reference patch at (x,y) and
//         history[i] patch at rounded (hx, hy).
//      d. Weight = temporal_weight^age * exp(-ssd / h²).
//      e. If weight >= min_confidence, accumulate.
//   3. Normalize and write to dst.
static void process_temporal_row(void* ctx_ptr, size_t y) {
    TemporalNlmContext* ctx = static_cast<TemporalNlmContext*>(ctx_ptr);
    const Image& ref = *ctx->reference;
    Image& dst = *ctx->dst;

    int w = ref.width, h = ref.height, c = ref.channels;
    int stride = w * c;
    const float* ref_data = ref.data.data();
    int hp = ctx->half_patch;
    float h2 = ctx->h2;
    int nframes = ctx->nframes;

    // Pre-compute temporal decay weights for each history frame.
    // Frame index 0 = oldest, nframes-1 = newest (reference itself excluded).
    std::vector<float> t_weights(nframes);
    for (int ti = 0; ti < nframes; ++ti) {
        int dist = nframes - ti;  // distance from reference (reference = dist 0)
        t_weights[ti] = std::pow(ctx->temporal_weight, (float)dist);
    }

    for (int x = 0; x < w; ++x) {
        float total_weight = 0.0f;
        float accum[4] = {0, 0, 0, 0};

        // Reference frame always contributes with weight 1.0.
        int ref_idx = (int)y * stride + x * c;
        accum[0] += ref_data[ref_idx];
        accum[1] += ref_data[ref_idx + 1];
        accum[2] += ref_data[ref_idx + 2];
        total_weight += 1.0f;

        // Historical frames: compare at motion-compensated positions.
        for (int ti = 0; ti < nframes; ++ti) {
            const Image& hist = (*ctx->history)[ti];
            const MotionField& mf = (*ctx->flows)[ti];
            const float* hist_data = hist.data.data();

            // Reverse the flow: flow says where history pixel went;
            // we need where the reference pixel came from.
            float dx = mf.dx(x, (int)y);
            float dy = mf.dy(x, (int)y);
            int hx = (int)std::round((float)x - dx);
            int hy = (int)std::round((float)y - dy);

            // Bounds check — the warped position must have room for a full patch.
            if (hx < hp || hx >= w - hp || hy < hp || hy >= h - hp) {
                continue;  // out of bounds → skip this frame's contribution
            }

            // Patch SSD at the motion-compensated position.
            float ssd = patch_ssd_neon3(hist_data, stride,
                                         hx, hy,   // position in history
                                         x, (int)y, // position in reference
                                         hp, w, h);
            int patch_count = (2 * hp + 1) * (2 * hp + 1);
            if (patch_count > 0) ssd /= (float)patch_count;

            float wgt = t_weights[ti] * std::exp(-ssd / h2);

            // Confidence check: skip contributions below threshold.
            // This acts as an implicit occlusion / bad-match detector.
            if (wgt < ctx->min_confidence) continue;

            int hi = hy * stride + hx * c;
            accum[0] += wgt * hist_data[hi];
            accum[1] += wgt * hist_data[hi + 1];
            accum[2] += wgt * hist_data[hi + 2];
            total_weight += wgt;
        }

        // Normalize and write output.
        int di = (int)y * stride + x * c;
        if (total_weight > 1e-10f) {
            dst.data[di]     = accum[0] / total_weight;
            dst.data[di + 1] = accum[1] / total_weight;
            dst.data[di + 2] = accum[2] / total_weight;
        } else {
            // Fallback: use reference pixel as-is.
            dst.data[di]     = ref_data[ref_idx];
            dst.data[di + 1] = ref_data[ref_idx + 1];
            dst.data[di + 2] = ref_data[ref_idx + 2];
        }
    }
}

// ----------------------------------------------------------------
//  Constructor
// ----------------------------------------------------------------
TemporalDenoiser::TemporalDenoiser(const TemporalConfig& tc, const NlmParams& np)
    : config(tc), params(np) {}

// ----------------------------------------------------------------
//  Process a frame (no lookahead, internal helper)
// ----------------------------------------------------------------
Image TemporalDenoiser::process_frame(const Image& reference) {
    int w = reference.width, h = reference.height, c = reference.channels;

    Image dst;
    dst.width = w;
    dst.height = h;
    dst.channels = c;
    dst.data.resize((size_t)w * h * c);

    int nframes = (int)history.size();
    if (nframes == 0) {
        // No history — return reference as-is.
        dst.data = reference.data;
        return dst;
    }

    // --- Setup GCD-parallel processing ---
    TemporalNlmContext ctx;
    ctx.reference = &reference;
    ctx.history = &history;
    ctx.flows = &flows;
    ctx.dst = &dst;
    ctx.half_patch = params.patch_size / 2;
    ctx.h2 = params.h * params.h;
    ctx.temporal_weight = config.temporal_weight;
    ctx.nframes = nframes;
    ctx.min_confidence = config.motion.min_confidence;

    dispatch_apply_f(h, DISPATCH_APPLY_AUTO, &ctx, process_temporal_row);

    return dst;
}

// ----------------------------------------------------------------
//  Main denoise() entry point
// ----------------------------------------------------------------
Image TemporalDenoiser::denoise(const Image& current) {
    if (first_frame) {
        previous_frame = current;
        first_frame = false;
    }

    // --- Lookahead mode ---
    if (config.lookahead > 0) {
        pending.push_back(current);

        // Need at least lookahead+1 frames in the buffer before we
        // can emit the center frame.
        if ((int)pending.size() < config.lookahead + 1) {
            // Not ready yet — return empty Image as signal to skip encoding.
            return Image{};
        }

        // Reference = oldest frame in the pending window.
        Image reference = pending.front();
        pending.pop_front();

        // Build temporal window:
        //   history = past frames (already in `history` deque)
        //   reference = frame being denoised
        //   future = up to lookahead frames still in `pending`
        //
        // We construct a combined window by temporarily appending
        // future frames to history and their flows.

        // Save current history size for restoration.
        size_t orig_history_size = history.size();
        size_t future_count = std::min((int)pending.size(), config.lookahead);

        // Compute motion from each future frame → reference,
        // and append them (temporarily) to history/flows.
        for (size_t i = 0; i < future_count; ++i) {
            // NOTE: estimate_motion estimates src → dst.
            // Here src = future frame, dst = reference.
            MotionField mf;
            if (config.motion_compensated) {
                mf = estimate_motion(pending[i], reference, config.motion);
            } else {
                mf = zero_motion_field(reference.width, reference.height);
            }
            history.push_back(pending[i]);
            flows.push_back(std::move(mf));
        }

        // Trim combined history to frame_count.
        while ((int)history.size() > config.frame_count - 1) {
            history.pop_front();
            flows.pop_front();
        }

        // Denoise the reference frame using the combined window.
        Image result = process_frame(reference);

        // Restore history: remove the temporarily appended future frames.
        while (history.size() > orig_history_size) {
            history.pop_back();
            flows.pop_back();
        }

        // Add reference to persistent history (for next iteration).
        // Compute motion: reference → ???  We'll compute when needed
        // for the next reference.  For now just store.
        history.push_back(reference);
        flows.push_back(MotionField{});  // placeholder, filled on next use

        // Trim history.
        while ((int)history.size() > config.frame_count - 1) {
            history.pop_front();
            flows.pop_front();
        }

        // Update previous_frame for scene cut detection.
        previous_frame = reference;

        return result;
    }

    // --- Causal mode (lookahead == 0) ---
    // Scene cut detection.
    if (!history.empty()) {
        if (detect_scene_cut(previous_frame, current, config.scene_detect)) {
            // Clear history on scene cut — no valid temporal information.
            history.clear();
            flows.clear();
            history.push_back(current);
            flows.push_back(MotionField{});  // identity flow for current

            // Trim to frame_count.
            while ((int)history.size() > config.frame_count) {
                history.pop_front();
                flows.pop_front();
            }

            previous_frame = current;
            // Return current frame unchanged (spatial-only for first post-cut frame).
            Image result = current;
            return result;
        }
    }

    // Compute motion from each existing history frame → current.
    // When motion compensation is disabled, use identity (zero) flow.
    for (size_t i = 0; i < history.size(); ++i) {
        if (config.motion_compensated) {
            flows[i] = estimate_motion(history[i], current, config.motion);
        } else {
            flows[i] = zero_motion_field(current.width, current.height);
        }
    }

    // Add current frame to history (with placeholder flow).
    history.push_back(current);
    flows.push_back(MotionField{});  // identity — not used

    // Trim to frame_count.
    while ((int)history.size() > config.frame_count) {
        history.pop_front();
        flows.pop_front();
    }

    // The reference is `current` (last in history after push).
    // Remove it from history/flows before processing since the kernel
    // treats history as separate from reference.
    Image reference = current;  // reference is what we denoise
    history.pop_back();         // remove it from history
    flows.pop_back();

    // Denoise.
    Image result = process_frame(reference);

    // Re-add reference to history for the next frame.
    history.push_back(reference);
    flows.push_back(MotionField{});

    previous_frame = current;
    return result;
}

// ----------------------------------------------------------------
//  Flush — drain remaining frames at end of stream (lookahead mode)
// ----------------------------------------------------------------
Image TemporalDenoiser::flush() {
    if (pending.empty()) {
        return Image{};  // nothing left
    }

    Image reference = pending.front();
    pending.pop_front();

    // With no more incoming frames, use whatever is available:
    // history already contains past frames.
    // No future frames are appended (pending only shrinks).

    // Compute motion from existing history frames → reference.
    for (size_t i = 0; i < history.size(); ++i) {
        if (config.motion_compensated) {
            flows[i] = estimate_motion(history[i], reference, config.motion);
        } else {
            flows[i] = zero_motion_field(reference.width, reference.height);
        }
    }

    Image result = process_frame(reference);

    // Add reference to history.
    history.push_back(reference);
    flows.push_back(MotionField{});
    while ((int)history.size() > config.frame_count - 1) {
        history.pop_front();
        flows.pop_front();
    }

    return result;
}
