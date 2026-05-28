#include "nlm_core.h"

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <iostream>
#include <dispatch/dispatch.h>

static id<MTLDevice> g_device = nil;
static id<MTLCommandQueue> g_queue = nil;
static id<MTLComputePipelineState> g_pipeline = nil;
static NSUInteger g_tg_w = 16;
static NSUInteger g_tg_h = 16;

static const char kMetalKernelSource[] = R"METAL(
#include <metal_stdlib>
using namespace metal;
kernel void nlm_compute(
    device const float* src    [[buffer(0)]],
    device float* dst          [[buffer(1)]],
    constant int& width        [[buffer(2)]],
    constant int& height       [[buffer(3)]],
    constant int& channels     [[buffer(4)]],
    constant int& half_patch   [[buffer(5)]],
    constant int& half_search  [[buffer(6)]],
    constant float& h2_inv     [[buffer(7)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int x = int(gid.x);
    int y = int(gid.y);
    if (x >= width || y >= height) return;
    int sy_min = max(y - half_search, 0);
    int sy_max = min(y + half_search, height - 1);
    int sx_min = max(x - half_search, 0);
    int sx_max = min(x + half_search, width - 1);
    int patch_count = (2 * half_patch + 1) * (2 * half_patch + 1);
    float total_weight = 0.0;
    float accum[4] = {0, 0, 0, 0};
    for (int sy = sy_min; sy <= sy_max; ++sy) {
        for (int sx = sx_min; sx <= sx_max; ++sx) {
            float ssd = 0.0;
            for (int py = -half_patch; py <= half_patch; ++py) {
                int cy = y + py;
                int ny = sy + py;
                if (cy < 0 || cy >= height || ny < 0 || ny >= height) continue;
                int crow = cy * width * channels;
                int nrow = ny * width * channels;
                for (int px = -half_patch; px <= half_patch; ++px) {
                    int cx = x + px;
                    int nx = sx + px;
                    if (cx < 0 || cx >= width || nx < 0 || nx >= width) continue;
                    int ci = crow + cx * channels;
                    int ni = nrow + nx * channels;
                    for (int c = 0; c < channels; ++c) {
                        float diff = src[ci + c] - src[ni + c];
                        ssd += diff * diff;
                    }
                }
            }
            float weight = exp(-ssd * h2_inv * (1.0f / float(patch_count)));
            int si = sy * width * channels + sx * channels;
            for (int c = 0; c < channels; ++c) {
                accum[c] += weight * src[si + c];
            }
            total_weight += weight;
        }
    }
    int di = y * width * channels + x * channels;
    for (int c = 0; c < channels; ++c) {
        if (total_weight > 1e-10f) {
            dst[di + c] = accum[c] / total_weight;
        } else {
            dst[di + c] = src[di + c];
        }
    }
}
)METAL";

static bool g_verbose_once = true;

static bool init_metal() {
    if (g_pipeline) return true;

    g_device = MTLCreateSystemDefaultDevice();
    if (!g_device) {
        std::cerr << "Metal: no device found\n";
        return false;
    }

    g_queue = [g_device newCommandQueue];
    if (!g_queue) {
        std::cerr << "Metal: failed to create command queue\n";
        return false;
    }

    NSError* error = nil;

    NSString* source = [NSString stringWithUTF8String:kMetalKernelSource];
    MTLCompileOptions* options = [MTLCompileOptions new];
    options.languageVersion = MTLLanguageVersion3_0;
    id<MTLLibrary> library = [g_device newLibraryWithSource:source options:options error:&error];

    if (!library) {
        std::cerr << "Metal: compile error: " << [[error localizedDescription] UTF8String] << "\n";
        return false;
    }

    id<MTLFunction> func = [library newFunctionWithName:@"nlm_compute"];
    if (!func) {
        std::cerr << "Metal: function 'nlm_compute' not found\n";
        return false;
    }

    g_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
    if (!g_pipeline) {
        std::cerr << "Metal: pipeline error: " << [[error localizedDescription] UTF8String] << "\n";
        return false;
    }

    // Tune threadgroup size per GPU generation
    NSUInteger execWidth = g_pipeline.threadExecutionWidth;
    NSUInteger maxThreads = g_pipeline.maxTotalThreadsPerThreadgroup;

    if ([g_device supportsFamily:MTLGPUFamilyApple9]) {
        g_tg_w = execWidth;
        g_tg_h = maxThreads / execWidth;
        if (g_tg_h > 32) g_tg_h = 32;
        if (g_verbose_once) std::cout << "Metal: Apple GPU family 9 (M4)\n";
    } else if ([g_device supportsFamily:MTLGPUFamilyApple8]) {
        g_tg_w = execWidth;
        g_tg_h = maxThreads / execWidth;
        if (g_tg_h > 24) g_tg_h = 24;
        if (g_verbose_once) std::cout << "Metal: Apple GPU family 8 (M3)\n";
    } else if ([g_device supportsFamily:MTLGPUFamilyApple7]) {
        g_tg_w = execWidth;
        g_tg_h = maxThreads / execWidth;
        if (g_tg_h > 16) g_tg_h = 16;
        if (g_verbose_once) std::cout << "Metal: Apple GPU family 7 (M2)\n";
    } else if ([g_device supportsFamily:MTLGPUFamilyApple6]) {
        g_tg_w = execWidth;
        g_tg_h = maxThreads / execWidth;
        if (g_tg_h > 16) g_tg_h = 16;
        if (g_verbose_once) std::cout << "Metal: Apple GPU family 6 (M1 Pro/Max)\n";
    } else {
        g_tg_w = execWidth;
        g_tg_h = maxThreads / execWidth;
        if (g_tg_h > 16) g_tg_h = 16;
        if (g_verbose_once) std::cout << "Metal: Apple GPU (M1)\n";
    }

    g_verbose_once = false;
    return true;
}

static void nlm_metal_kernel_sync(const Image& src, Image& dst, const NlmParams& params) {
    int w = src.width;
    int h = src.height;
    int c = src.channels;
    int n = w * h * c;

    dst.width = w;
    dst.height = h;
    dst.channels = c;
    dst.data.resize(n);

    int half_patch = params.patch_size / 2;
    int half_search = params.search_window / 2;
    float h2_inv = 1.0f / (params.h * params.h);

    size_t data_size = n * sizeof(float);

    id<MTLBuffer> src_buf = [g_device newBufferWithLength:data_size
                                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> dst_buf = [g_device newBufferWithLength:data_size
                                                   options:MTLResourceStorageModeShared];

    memcpy([src_buf contents], src.data.data(), data_size);

    id<MTLCommandBuffer> cmd = [g_queue commandBuffer];

    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
    [enc setComputePipelineState:g_pipeline];
    [enc setBuffer:src_buf offset:0 atIndex:0];
    [enc setBuffer:dst_buf offset:0 atIndex:1];

    int sw = w, sh = h, sc = c, shp = half_patch, shs = half_search;
    float fh2 = h2_inv;
    [enc setBytes:&sw length:sizeof(int) atIndex:2];
    [enc setBytes:&sh length:sizeof(int) atIndex:3];
    [enc setBytes:&sc length:sizeof(int) atIndex:4];
    [enc setBytes:&shp length:sizeof(int) atIndex:5];
    [enc setBytes:&shs length:sizeof(int) atIndex:6];
    [enc setBytes:&fh2 length:sizeof(float) atIndex:7];

    MTLSize gridSize = MTLSizeMake(w, h, 1);
    MTLSize tgSize = MTLSizeMake(g_tg_w, g_tg_h, 1);

    [enc dispatchThreads:gridSize threadsPerThreadgroup:tgSize];
    [enc endEncoding];

    [cmd commit];
    [cmd waitUntilCompleted];

    if (cmd.error) {
        std::cerr << "Metal error: " << [[cmd.error localizedDescription] UTF8String] << "\n";
        nlm_denoise_cpu_neon(src, dst, params);
        return;
    }

    memcpy(dst.data.data(), [dst_buf contents], data_size);
}

void nlm_denoise_metal(const Image& src, Image& dst, const NlmParams& params) {
    if (!init_metal()) {
        nlm_denoise_cpu_neon(src, dst, params);
        return;
    }

    if (params.verbose) {
        std::cout << "NLM Metal GPU: " << src.width << "x" << src.height << "x" << src.channels
                  << " patch=" << params.patch_size << " search=" << params.search_window
                  << " h=" << params.h
                  << " tg=" << g_tg_w << "x" << g_tg_h << "\n";
    }

    nlm_metal_kernel_sync(src, dst, params);
}
