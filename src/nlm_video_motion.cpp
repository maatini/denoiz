#include "nlm_video_motion.h"

#include <arm_neon.h>
#include <dispatch/dispatch.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// ----------------------------------------------------------------
//  NEON-accelerated Block SAD (Sum of Absolute Differences)
// ----------------------------------------------------------------
// Computes SAD between a block in image a (at ax, ay) and the
// same-sized block in image b (at bx, by).  Uses NEON vabdq_f32
// for 4-wide absolute difference + accumulation, matching the
// pattern from patch_ssd_neon3 in nlm_cpu_neon.cpp.
static float block_sad_neon3(const float* a_data, int a_stride,
                              const float* b_data, int b_stride,
                              int ax, int ay, int bx, int by,
                              int block_size, int aw, int ah,
                              int bw, int bh) {
    float32x4_t sad_vec = vdupq_n_f32(0);

    for (int dy = 0; dy < block_size; ++dy) {
        int ay_ = ay + dy;
        int by_ = by + dy;
        if (ay_ < 0 || ay_ >= ah || by_ < 0 || by_ >= bh) continue;

        const float* ra = a_data + ay_ * a_stride;
        const float* rb = b_data + by_ * b_stride;

        for (int dx = 0; dx < block_size; ++dx) {
            int ax_ = ax + dx;
            int bx_ = bx + dx;
            if (ax_ < 0 || ax_ >= aw || bx_ < 0 || bx_ >= bw) continue;

            int ai = ax_ * 3;
            int bi = bx_ * 3;

            float buf_a[4] = {ra[ai], ra[ai + 1], ra[ai + 2], 0};
            float buf_b[4] = {rb[bi], rb[bi + 1], rb[bi + 2], 0};
            sad_vec = vaddq_f32(sad_vec, vabdq_f32(vld1q_f32(buf_a), vld1q_f32(buf_b)));
        }
    }
    return vaddvq_f32(sad_vec);
}

// ----------------------------------------------------------------
//  Pyramid Construction
// ----------------------------------------------------------------
ImagePyramid build_pyramid(const Image& src) {
    ImagePyramid pyr;
    int w = src.width, h = src.height, c = src.channels;

    // Level 0: full resolution (copy).
    pyr.level[0].width = w;
    pyr.level[0].height = h;
    pyr.level[0].channels = c;
    pyr.level[0].data = src.data;  // copy

    // Level 1: 1/4 resolution (2× down in each dimension, using 2×2 box filter
    // applied twice).  This matches the box-averaging pattern in
    // nlm_coarse_to_fine.cpp.
    int hw2 = (w + 1) / 2, hh2 = (h + 1) / 2;   // half
    int qw  = (hw2 + 1) / 2, qh = (hh2 + 1) / 2; // quarter

    // Intermediate half-resolution image.
    Image half;
    half.width = hw2;
    half.height = hh2;
    half.channels = c;
    half.data.resize(hw2 * hh2 * c);

    for (int y = 0; y < hh2; ++y) {
        for (int x = 0; x < hw2; ++x) {
            for (int ch = 0; ch < c; ++ch) {
                float sum = 0;
                int cnt = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        int sx = std::min(x * 2 + dx, w - 1);
                        int sy = std::min(y * 2 + dy, h - 1);
                        sum += src.at(sx, sy, ch);
                        ++cnt;
                    }
                }
                half.at(x, y, ch) = sum / (float)cnt;
            }
        }
    }

    // Second downsample: half → quarter.
    pyr.level[1].width = qw;
    pyr.level[1].height = qh;
    pyr.level[1].channels = c;
    pyr.level[1].data.resize(qw * qh * c);

    for (int y = 0; y < qh; ++y) {
        for (int x = 0; x < qw; ++x) {
            for (int ch = 0; ch < c; ++ch) {
                float sum = 0;
                int cnt = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        int sx = std::min(x * 2 + dx, hw2 - 1);
                        int sy = std::min(y * 2 + dy, hh2 - 1);
                        sum += half.at(sx, sy, ch);
                        ++cnt;
                    }
                }
                pyr.level[1].at(x, y, ch) = sum / (float)cnt;
            }
        }
    }

    return pyr;
}

// ----------------------------------------------------------------
//  Context for GCD-parallel block matching
// ----------------------------------------------------------------
struct BlockMatchContext {
    const Image* src_coarse;
    const Image* dst_coarse;
    int coarse_w, coarse_h;
    int block_size;              // block size at coarse resolution
    int search_radius;           // search radius in blocks
    int nbx, nby;                // number of blocks in x and y
    std::vector<float>* coarse_vectors; // nbx * nby * 2 floats (dx, dy per block, in coarse pixels)
};

// Process one row of blocks at the coarse level.
static void match_block_row(void* ctx_ptr, size_t by) {
    BlockMatchContext* ctx = static_cast<BlockMatchContext*>(ctx_ptr);
    const Image& src = *ctx->src_coarse;
    const Image& dst = *ctx->dst_coarse;
    int bs = ctx->block_size;
    int sr = ctx->search_radius;
    int stride = src.width * src.channels;
    const float* sdata = src.data.data();
    const float* ddata = dst.data.data();

    for (int bx = 0; bx < ctx->nbx; ++bx) {
        int ax = bx * bs;
        int ay = (int)by * bs;

        float best_sad = 1e30f;
        int best_dx = 0, best_dy = 0;

        // Search within ±sr blocks of the same position.
        int sx_min = std::max(bx - sr, 0);
        int sx_max = std::min(bx + sr, ctx->nbx - 1);
        int sy_min = std::max((int)by - sr, 0);
        int sy_max = std::min((int)by + sr, ctx->nby - 1);

        for (int sy = sy_min; sy <= sy_max; ++sy) {
            for (int sx = sx_min; sx <= sx_max; ++sx) {
                float sad = block_sad_neon3(sdata, stride, ddata, stride,
                                             ax, ay, sx * bs, sy * bs,
                                             bs, src.width, src.height,
                                             dst.width, dst.height);
                if (sad < best_sad) {
                    best_sad = sad;
                    best_dx = (sx - bx) * bs;  // displacement in coarse pixels
                    best_dy = (sy - (int)by) * bs;
                }
            }
        }

        (*ctx->coarse_vectors)[((int)by * ctx->nbx + bx) * 2]     = (float)best_dx;
        (*ctx->coarse_vectors)[((int)by * ctx->nbx + bx) * 2 + 1] = (float)best_dy;
    }
}

// Context for per-block refinement at full resolution.
struct RefineContext {
    const Image* src;
    const Image* dst;
    int block_size;
    int fine_radius;
    int nbx, nby;                              // fine-grid block count
    int coarse_nbx, coarse_nby;                // coarse-grid block count (for vector indexing)
    const std::vector<float>* coarse_vectors;  // in coarse pixels
    std::vector<float>* fine_vectors;          // per-block dx, dy in full-res pixels
};

// Process one row of blocks at full resolution: refine coarse vectors.
static void refine_block_row(void* ctx_ptr, size_t by) {
    RefineContext* ctx = static_cast<RefineContext*>(ctx_ptr);
    const Image& src = *ctx->src;
    const Image& dst = *ctx->dst;
    int bs = ctx->block_size;
    int fr = ctx->fine_radius;
    int stride = src.width * src.channels;
    const float* sdata = src.data.data();
    const float* ddata = dst.data.data();
    int w = src.width, h = src.height;

    for (int bx = 0; bx < ctx->nbx; ++bx) {
        int ax = bx * bs;
        int ay = (int)by * bs;

        // Map fine-grid block (bx, by) to the nearest coarse-grid block.
        // Coarse grid is at 1/4 resolution; each coarse block covers ~4×4 fine blocks.
        int cbx = std::min(bx * ctx->coarse_nbx / ctx->nbx, ctx->coarse_nbx - 1);
        int cby = std::min((int)by * ctx->coarse_nby / ctx->nby, ctx->coarse_nby - 1);

        // Coarse prediction (scale from coarse pixels → full-res pixels, ×4).
        float cdx = (*ctx->coarse_vectors)[(cby * ctx->coarse_nbx + cbx) * 2];
        float cdy = (*ctx->coarse_vectors)[(cby * ctx->coarse_nbx + cbx) * 2 + 1];
        int pred_x = (int)round((float)ax + cdx * 4.0f);
        int pred_y = (int)round((float)ay + cdy * 4.0f);

        float best_sad = 1e30f;
        int best_dx = pred_x - ax;
        int best_dy = pred_y - ay;

        // Small local search around the coarse prediction.
        for (int dy = -fr; dy <= fr; ++dy) {
            for (int dx = -fr; dx <= fr; ++dx) {
                int tx = pred_x + dx;
                int ty = pred_y + dy;
                // Must stay within the valid search area of dst.
                if (tx < 0 || tx + bs > w || ty < 0 || ty + bs > h) continue;

                float sad = block_sad_neon3(sdata, stride, ddata, stride,
                                             ax, ay, tx, ty,
                                             bs, w, h, w, h);
                if (sad < best_sad) {
                    best_sad = sad;
                    best_dx = tx - ax;
                    best_dy = ty - ay;
                }
            }
        }

        (*ctx->fine_vectors)[((int)by * ctx->nbx + bx) * 2]     = (float)best_dx;
        (*ctx->fine_vectors)[((int)by * ctx->nbx + bx) * 2 + 1] = (float)best_dy;
    }
}

// ----------------------------------------------------------------
//  Main Motion Estimation Entry Point
// ----------------------------------------------------------------
MotionField estimate_motion(const Image& src, const Image& dst,
                            const MotionEstimationConfig& config) {
    int w = src.width, h = src.height;
    int bs = config.block_size;

    // --- Step 1: Build pyramids ---
    ImagePyramid pyr_src = build_pyramid(src);
    ImagePyramid pyr_dst = build_pyramid(dst);

    int cw = pyr_src.level[1].width;   // coarse width  (~ w/4)
    int ch = pyr_src.level[1].height;  // coarse height (~ h/4)

    // Block grid at coarse resolution (same block_size in coarse pixels).
    int nbx = std::max(1, cw / bs);
    int nby = std::max(1, ch / bs);

    // --- Step 2: Coarse search (GCD-parallel over block rows) ---
    std::vector<float> coarse_vecs(nbx * nby * 2, 0.0f);

    BlockMatchContext bctx;
    bctx.src_coarse = &pyr_src.level[1];
    bctx.dst_coarse = &pyr_dst.level[1];
    bctx.coarse_w = cw;
    bctx.coarse_h = ch;
    bctx.block_size = bs;
    bctx.search_radius = config.coarse_search_radius;
    bctx.nbx = nbx;
    bctx.nby = nby;
    bctx.coarse_vectors = &coarse_vecs;

    dispatch_apply_f(nby, DISPATCH_APPLY_AUTO, &bctx, match_block_row);

    // --- Step 3: Refine at full resolution ---
    // Block grid at full resolution (same block_size, on original frame).
    int fnbx = std::max(1, w / bs);
    int fnby = std::max(1, h / bs);
    std::vector<float> fine_vecs(fnbx * fnby * 2, 0.0f);

    RefineContext rctx;
    rctx.src = &src;
    rctx.dst = &dst;
    rctx.block_size = bs;
    rctx.fine_radius = config.fine_search_radius;
    rctx.nbx = fnbx;
    rctx.nby = fnby;
    rctx.coarse_nbx = nbx;
    rctx.coarse_nby = nby;
    rctx.coarse_vectors = &coarse_vecs;
    rctx.fine_vectors = &fine_vecs;

    dispatch_apply_f(fnby, DISPATCH_APPLY_AUTO, &rctx, refine_block_row);

    // --- Step 4: Bilinear upscale block vectors → per-pixel flow field ---
    MotionField mf;
    mf.frame_w = w;
    mf.frame_h = h;
    mf.data.resize(w * h * 2, 0.0f);

    // Use the bilinear upscale pattern from nlm_coarse_to_fine.cpp.
    float scale_x = (float)(fnbx > 1 ? fnbx - 1 : 0) / (w > 1 ? w - 1 : 1);
    float scale_y = (float)(fnby > 1 ? fnby - 1 : 0) / (h > 1 ? h - 1 : 1);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // Map pixel (x,y) to block-grid continuous coordinates.
            float gx = x * scale_x;
            float gy = y * scale_y;

            int bx0 = (int)gx;
            int by0 = (int)gy;
            int bx1 = std::min(bx0 + 1, fnbx - 1);
            int by1 = std::min(by0 + 1, fnby - 1);
            float fx = gx - (float)bx0;
            float fy = gy - (float)by0;

            // Bilinear interpolation of 4 neighboring block vectors.
            int i00 = (by0 * fnbx + bx0) * 2;
            int i10 = (by0 * fnbx + bx1) * 2;
            int i01 = (by1 * fnbx + bx0) * 2;
            int i11 = (by1 * fnbx + bx1) * 2;

            float dx = fine_vecs[i00]     * (1.0f - fx) * (1.0f - fy)
                     + fine_vecs[i10]     * fx * (1.0f - fy)
                     + fine_vecs[i01]     * (1.0f - fx) * fy
                     + fine_vecs[i11]     * fx * fy;

            float dy = fine_vecs[i00 + 1] * (1.0f - fx) * (1.0f - fy)
                     + fine_vecs[i10 + 1] * fx * (1.0f - fy)
                     + fine_vecs[i01 + 1] * (1.0f - fx) * fy
                     + fine_vecs[i11 + 1] * fx * fy;

            mf.dx(x, y) = dx;
            mf.dy(x, y) = dy;
        }
    }

    return mf;
}

// ----------------------------------------------------------------
//  Image Warping
// ----------------------------------------------------------------
// Backward warp: for each output pixel (ox, oy), sample src at
// (ox - flow.dx(ox, oy), oy - flow.dy(ox, oy)).  This reverses
// the forward flow (src → dst) so the warped src aligns with dst.
//
// Uses bilinear interpolation.  Sets validity_mask to 0 for
// out-of-bounds samples, 1 for valid samples.
//
// GCD-parallel over rows (matching dispatch_apply_f pattern).
struct WarpContext {
    const Image* src;
    const MotionField* flow;
    Image* warped;
    std::vector<float>* mask;
};

static void warp_row(void* ctx_ptr, size_t y) {
    WarpContext* ctx = static_cast<WarpContext*>(ctx_ptr);
    const Image& src = *ctx->src;
    const MotionField& mf = *ctx->flow;
    Image& dst = *ctx->warped;
    int w = src.width, h = src.height, c = src.channels;

    for (int x = 0; x < w; ++x) {
        // Backward warp: reverse the flow direction.
        float sx = (float)x - mf.dx(x, (int)y);
        float sy = (float)y - mf.dy(x, (int)y);

        int ix = (int)sx;
        int iy = (int)sy;

        // Check bounds.
        if (ix < 0 || ix >= w - 1 || iy < 0 || iy >= h - 1) {
            ctx->mask->at(y * w + x) = 0.0f;
            for (int ch = 0; ch < c; ++ch) dst.at(x, (int)y, ch) = 0.0f;
            continue;
        }

        float fx = sx - (float)ix;
        float fy = sy - (float)iy;

        for (int ch = 0; ch < c; ++ch) {
            float v = src.at(ix,     iy,     ch) * (1.0f - fx) * (1.0f - fy)
                    + src.at(ix + 1, iy,     ch) * fx * (1.0f - fy)
                    + src.at(ix,     iy + 1, ch) * (1.0f - fx) * fy
                    + src.at(ix + 1, iy + 1, ch) * fx * fy;
            dst.at(x, (int)y, ch) = v;
        }
        ctx->mask->at(y * w + x) = 1.0f;
    }
}

void warp_image(const Image& src, const MotionField& flow,
                Image& warped, std::vector<float>& validity_mask) {
    int w = src.width, h = src.height, c = src.channels;

    warped.width = w;
    warped.height = h;
    warped.channels = c;
    warped.data.resize(w * h * c);

    validity_mask.assign(w * h, 0.0f);

    WarpContext ctx;
    ctx.src = &src;
    ctx.flow = &flow;
    ctx.warped = &warped;
    ctx.mask = &validity_mask;

    dispatch_apply_f(h, DISPATCH_APPLY_AUTO, &ctx, warp_row);
}
