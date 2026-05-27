# nlm_denoise — NLM Denoising for Apple Silicon

High-performance Non-Local Means (NLM) image denoising CLI, optimized for Apple Silicon (M1–M4). Metal GPU, ARM NEON + GCD, and multi-resolution fast mode.

## Prerequisites

Devbox only. No system dependencies beyond what devbox provides.

```bash
# Install devbox
curl -fsSL https://www.jetify.com/devbox/install.sh | bash

# Enter devbox shell, build & test
cd nlm
devbox shell
devbox run build
devbox run test
```

## Usage

```
nlm_denoise input.png output.png [options]

Options:
  --patch-size N      Patch size, odd (default: 7)
  --search-window N   Search window size, odd (default: 21)
  --h FLOAT           Filter strength (default: 0.1)
  --sigma FLOAT       Input noise sigma (not applied, default: 0.0)
  --use-gpu           Use Metal GPU acceleration
  --fast              Multi-resolution mode (2x downsample, faster)
  --threads N|auto    Thread count (default: auto)
  --benchmark         Run comparison against naive reference
  --verbose           Show progress and timing

Examples:

  # Standard CPU denoise (NEON + GCD)
  nlm_denoise noisy.jpg clean.png --h 0.15 --verbose

  # GPU-accelerated
  nlm_denoise noisy.jpg clean.png --use-gpu --h 0.12

  # Fast preview mode
  nlm_denoise noisy.jpg clean.png --fast --h 0.1

  # Benchmark all pipelines
  nlm_denoise noisy.jpg /dev/null --use-gpu --benchmark
```

## Performance (Apple M2, 512x512 RGB, patch=7, search=21, h=0.1)

| Pipeline        | Time   | vs naive |
|-----------------|--------|----------|
| Naive           | 23.3s  | 1.0x     |
| NEON + GCD      |  1.2s  | 19x      |
| Metal GPU       |  0.37s | 63x      |
| Fast (multi-res)|  0.30s | 79x      |

PSNR matches naive reference: GPU 74.8 dB, Fast 52.9 dB.

## Install

```bash
devbox run install   # builds and installs to install/bin/nlm_denoise
```

## Architecture

```
src/
  nlm_core.h           Shared types (Image, NlmParams)
  nlm_cli.cpp          CLI argument parsing
  nlm_io.cpp           PNG I/O via stb_image
  nlm_cpu.cpp          Naive reference implementation
  nlm_cpu_neon.cpp     ARM NEON intrinsics + GCD dispatch_apply
  nlm_cpu_neon_fast.cpp  Multi-resolution (downsample -> NLM -> upsample)
  nlm_metal.mm         Metal GPU compute pipeline (shader embedded)
  nlm_main.cpp         Entry point + benchmark runner
test/
  run_tests.py         Comprehensive test suite (ground truth, grayscale, edge cases)
```

## Limitations

- PNG output only (stb_image_write PNG backend)
- Metal GPU: requires macOS with Apple Silicon GPU
- No color management / gamma correction
- `--sigma` parameter reserved for future noise application
- 1-channel (grayscale) uses scalar fallback for patch SSD
