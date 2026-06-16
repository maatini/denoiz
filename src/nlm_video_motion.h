#pragma once

#include "nlm_core.h"

#include <vector>

// Per-pixel motion field.  Stores dx, dy for each pixel in the
// source frame, stored interleaved: data[y*w*2 + x*2 + 0]=dx,
// data[y*w*2 + x*2 + 1]=dy.  Positive dx = right, positive dy = down.
// After bilinear upscaling from block-grid vectors, the field
// covers every pixel of the source frame.
struct MotionField {
    int frame_w = 0;
    int frame_h = 0;
    // frame_w * frame_h * 2 floats (dx, dy interleaved)
    std::vector<float> data;

    float& dx(int x, int y) { return data[(y * frame_w + x) * 2]; }
    float& dy(int x, int y) { return data[(y * frame_w + x) * 2 + 1]; }
    const float& dx(int x, int y) const { return data[(y * frame_w + x) * 2]; }
    const float& dy(int x, int y) const { return data[(y * frame_w + x) * 2 + 1]; }
};

// Configuration for hierarchical block-matching motion estimation.
struct MotionEstimationConfig {
    int block_size = 8;              // block side length in pixels (full-res)
    int coarse_search_radius = 16;   // search radius in blocks at coarse level
    int fine_search_radius = 2;      // ±px refinement at full resolution
    float min_confidence = 0.3f;     // minimum weight to accept a temporal match
};

// 2-level Gaussian-like pyramid for coarse-to-fine motion search.
// level[0] = full resolution, level[1] = 1/4 resolution (2× down in each dim).
struct ImagePyramid {
    Image level[2];
};

// Build a 2-level pyramid: level[0] = original, level[1] = 1/4 resolution.
// Uses 2×2 box-average downsampling (twice), matching the approach in
// nlm_coarse_to_fine.cpp.
ImagePyramid build_pyramid(const Image& src);

// Estimate motion vectors from src → dst using hierarchical block matching.
// 1. Build pyramids for both frames.
// 2. At coarse level (1/4 res): for each block, search ±search_radius blocks
//    for the best SAD match. Uses NEON-accelerated block SAD.
// 3. Refine at full resolution: scale coarse vector, search ±fine_radius px.
// 4. Bilinear upscale block-grid vectors to per-pixel MotionField.
//
// Returns a MotionField where field.at(x, y) tells where pixel (x, y)
// in src moved to in dst: dst_pos ≈ (x + dx, y + dy).
MotionField estimate_motion(const Image& src, const Image& dst,
                            const MotionEstimationConfig& config);

// Warp src frame using a motion field (backward warp).
// For each output pixel (ox, oy), sample src at (ox - dx, oy - dy)
// using bilinear interpolation.  Out-of-bounds samples produce
// a validity_mask entry of 0.0; valid samples produce 1.0.
//
// The flow direction convention matches estimate_motion:
// flow says where src pixels go to.  Warping "reverses" this so
// the output is src aligned to dst's coordinate system.
void warp_image(const Image& src, const MotionField& flow,
                Image& warped, std::vector<float>& validity_mask);
