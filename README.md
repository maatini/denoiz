# denoise — NLM Denoising for Apple Silicon

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)](https://en.cppreference.com/w/cpp/20)
[![macOS](https://img.shields.io/badge/platform-macOS%20Apple%20Silicon-lightgrey)](https://www.apple.com/mac/)
[![Metal](https://img.shields.io/badge/GPU-Metal-purple)](https://developer.apple.com/metal/)
[![FFmpeg](https://img.shields.io/badge/media-FFmpeg-green)](https://ffmpeg.org)
[![Devbox](https://img.shields.io/badge/build-devbox-blue)](https://www.jetify.com/devbox/)

High-performance Non-Local Means (NLM) denoising for Apple Silicon (M1–M4). Two tools:

- **denoise** — image denoising CLI with 8 pipelines: Metal GPU, ARM NEON + GCD, Wavelet, Adaptive h, Coarse-to-Fine, and more. Research-backed adaptations from NTIRE 2025 Challenge (SRC-B #1, 31.20 dB).
- **v-denoise** — video denoising CLI with temporal NLM, content presets, async GPU pipeline. FFmpeg-based.

![Denoiz](denoiz.png)

## Prerequisites

Devbox only. No system dependencies beyond what devbox provides.

```bash
curl -fsSL https://www.jetify.com/devbox/install.sh | bash

cd nlm
devbox shell
devbox run build
devbox run test
devbox run install   # → install/bin/denoise  +  install/bin/v-denoise
```

## denoise (images)

### Usage

```
denoise input.png output.png [options]
```

### Parameter

| Parameter | Typ | Default | Beschreibung |
|-----------|-----|---------|-------------|
| `--patch-size N` | int (ungerade) | 7 | Seitenlänge des Vergleichs-Patches. Größer = mehr Struktur, quadratisch mehr Rechenzeit. Typisch 3–11. |
| `--search-window N` | int (ungerade) | 21 | Radius des Suchfensters. Größer = mehr Kandidaten, O(sw²) Laufzeit. Typisch 7–35. |
| `--h FLOAT` | float (>0) | 0.1 | Filterstärke. Gewichte = exp(−SSD/h²). Klein (0.05) = Details erhalten, groß (0.5) = starke Glättung. Typisch 0.05–0.5. |
| `--sigma FLOAT` | float | 0.0 | Rausch-Sigma (reserviert, nicht angewendet). |
| `--use-gpu` | flag | false | Metal GPU (Apple Silicon). Bit-identisch zu NEON, 36× Speedup. |
| `--fast` | flag | false | Multi-Resolution: 2× downsample → NLM → upsample. 60× Speedup. |
| `--wavelet` | flag | false | Wavelet-Domain NLM: 2-level DWT + threshold. 1705× Speedup, <1ms. |
| `--adaptive` | flag | false | Per-Pixel adaptive h via local variance. Beste PSNR (+6 dB). |
| `--ensemble` | flag | false | Multi-h Ensemble (3 members). Experimental, kein messbarer Gain. |
| `--coarse-to-fine` | flag | false | 4× downsample → coarse NLM → residual refinement. 55× Speedup. |
| `--benchmark` | flag | false | Laufzeitvergleich mit naiver Referenz + NEON. |
| `--threads N\|auto` | int/string | auto | GCD-Threads für CPU-Pipelines. |
| `--verbose` | flag | false | Pipeline-Name, Dimensionen, Parameter, Fortschritt, Laufzeit. |

### Examples

```bash
# Quality-first: Adaptive h (best PSNR)
denoise noisy.jpg clean.png --adaptive --h 0.1

# Best balance: GPU
denoise noisy.jpg clean.png --use-gpu --h 0.12

# Extreme speed: Wavelet (preview/real-time)
denoise noisy.jpg clean.png --wavelet --h 0.1

# Benchmark comparison
denoise noisy.jpg /dev/null --use-gpu --benchmark
```

### Performance (Apple M2, 256×256 RGB, patch=7, search=21, h=0.1)

| Pipeline          | Laufzeit | Speedup vs naïve | PSNR     | Use case       |
|-------------------|----------|-------------------|----------|----------------|
| Naïve             | 5.5s     | 1.0×              | ∞ (ref)  | Baseline       |
| Adaptive h        | 0.62s    | 9×                | **77.8 dB** | Quality-first |
| NEON + GCD        | 0.29s    | 20×               | 71.6 dB  | Default CPU    |
| Metal GPU         | 0.16s    | 36×               | 71.6 dB  | Best balance   |
| Coarse-to-Fine    | 0.10s    | 55×               | 47.3 dB  | Medium speed   |
| Fast (multi-res)  | 0.09s    | 60×               | 53.0 dB  | Fast preview   |
| Wavelet           | 0.003s   | 1705×             | 52.3 dB  | Real-time      |

### Performance (Apple M2, 512×512 RGB)

| Pipeline          | Laufzeit | Speedup vs naïve | PSNR     |
|-------------------|----------|-------------------|----------|
| Naïve             | 23.3s    | 1.0×              | ∞        |
| Adaptive h        | 2.5s     | 9×                | **77.8 dB** |
| NEON + GCD        | 1.2s     | 19×               | 74.8 dB  |
| Metal GPU         | 0.37s    | 63×               | 74.8 dB  |
| Coarse-to-Fine    | 0.38s    | 59×               | 46.0 dB  |
| Fast (multi-res)  | 0.30s    | 79×               | 52.9 dB  |
| Wavelet           | 0.015s   | 1555×             | 46.0 dB  |

### Pipeline Selection Guide

| Goal              | Pipeline          | Why                                          |
|-------------------|-------------------|----------------------------------------------|
| Max quality       | `--adaptive`      | Local variance adaption (+6 dB over NEON)    |
| Best speed+qty    | `--use-gpu`       | Metal GPU, 36×, bit-identical quality        |
| Real-time preview | `--wavelet`       | DWT-domain, 1705×, <1ms for thumbnails       |
| Quick export      | `--fast`          | Multi-resolution, 60×, no GPU needed         |
| Research baseline | `--coarse-to-fine`| Progressive refinement, 55×                  |

---

## v-denoise (videos)

### Usage

```
v-denoise input.mp4 output.mp4 [options]
```

### Parameter

| Parameter | Typ | Default | Beschreibung |
|-----------|-----|---------|-------------|
| `--preset PRESET` | string | medium | Speed: veryslow/slow/medium/fast/veryfast. Content: film/grain/lowlight/animation. |
| `--strength FLOAT` | float | 0.5 | Denoising strength 0.0–1.0, mapped to NLM filter strength h. |
| `--crf N` | int | 18 | Output quality (x264/x265 rate control). Lower = better, 0–51. |
| `--codec CODEC` | string | h264 | Output codec: h264, h265, av1. |
| `--temporal` | flag | false | Multi-frame temporal denoising (reduces flicker between frames). |
| `--frame-count N` | int | 3 | Temporal frames to buffer. Range 1–7. |
| `--temporal-weight FLOAT` | float | 0.8 | Weight decay per frame offset. 0.0–1.0. |
| `--sharpen FLOAT` | float | 0.0 | Unsharp mask amount after NLM, 0.0–2.0. 0.0 = off. |
| `--frames-out DIR` | string | — | Save denoised frames as PNG sequence (skips video encoding). |
| `--deinterlace` | flag | false | YADIF deinterlacing on input (reserved). |
| `--use-gpu` | flag | false | Force Metal GPU pipeline. |
| `--verbose` | flag | false | Progress with percentage, fps, ETA. |
| `--benchmark` | flag | false | Per-frame NLM timing, fps summary. |

### Examples

```bash
# Quality-first: film preset (adaptive h)
v-denoise noisy.mp4 clean.mp4 --preset film

# Fast: grain preset + sharpen
v-denoise noisy.mp4 clean.mp4 --preset grain --sharpen 0.5

# Temporal denoising (reduces flicker)
v-denoise noisy.mp4 clean.mp4 --temporal --frame-count 5

# Export frames only (no video re-encode)
v-denoise noisy.mp4 /dev/null --frames-out ./denoised_frames

# H.265 output
v-denoise noisy.mp4 clean.mp4 --preset film --codec h265
```

### Performance (Apple M2, 320×240, 25fps)

| Preset      | fps    |
|-------------|--------|
| film        | 83     |
| grain       | 124    |
| lowlight    | 122    |
| animation   | 123    |

### Performance (Apple M2, 640×480, 25fps)

| Preset      | fps    |
|-------------|--------|
| medium      | 20     |

### Preset Selection Guide

| Goal              | Preset            | Why                                          |
|-------------------|-------------------|----------------------------------------------|
| Real film grain   | `film`            | Adaptive h, sharp details, smooth grain      |
| Heavy noise/grain | `grain`           | Metal GPU, high h, aggressive denoising      |
| Low-light footage | `lowlight`        | Adaptive h, small patch, fine texture        |
| Animation/cartoon | `animation`       | Wavelet, fast, effective on flat regions     |
| Reduce flicker    | `--temporal`      | Multi-frame averaging across N frames        |

---

## NTIRE 2025 Adaptions

Research backed by [NTIRE 2025 Image Denoising Challenge Report](https://arxiv.org/abs/2504.12276) (SRC-B #1, 31.20 dB):

| Adaption          | NTIRE Source          | denoise Result    |
|-------------------|-----------------------|-----------------------|
| Wavelet-domain    | Wavelet Transform Loss | 1705× speedup         |
| Adaptive h        | Data Selection        | 77.8 dB, +6 dB PSNR   |
| Coarse-to-Fine    | Progressive Learning  | 55× speedup           |
| Ensemble          | Model Ensemble        | No gain (DL-specific) |

See `src/nlm_ane_analysis.txt` for ANE feasibility study.

## Architecture

```
src/
  nlm_core.h                  Shared types (Image, NlmParams)
  nlm_cli.cpp                 CLI argument parsing
  nlm_io.cpp                  PNG I/O via stb_image (RGBA→RGB conversion)
  nlm_main.cpp                Entry point + benchmark runner
  nlm_cpu.cpp                 Naive reference (quality baseline)
  nlm_cpu_neon.cpp            ARM NEON intrinsics + GCD dispatch_apply
  nlm_cpu_neon_fast.cpp       Multi-resolution (downsample → NLM → upsample)
  nlm_wavelet.cpp             Wavelet-domain: 2-level DWT + NLM + threshold
  nlm_adaptive_h.cpp          Adaptive h: local variance → per-pixel filter strength
  nlm_ensemble.cpp            Multi-h ensemble (3 members, experimental)
  nlm_coarse_to_fine.cpp      Coarse-to-fine: 2× down → coarse NLM → residual → fine NLM
  nlm_metal.mm                Metal GPU compute pipeline (embedded shader)
  nlm_metal_kernels.metal     Original Metal source (preserved, not used at runtime)
  nlm_ane_analysis.txt        ANE/NPU feasibility analysis
  nlm_video_main.cpp          v-denoise entry + FFmpeg pipeline
  nlm_video_temporal.h/cpp    Temporal multi-frame NLM denoiser
test/
  run_tests.py        Test suite (ground truth, grayscale, edge cases, CLI)
  generate_images.py  Test image generator
  gen_large.py        Large test image generator
```

## Limitations

- PNG output only for denoise (stb_image_write PNG backend)
- Metal GPU: requires macOS with Apple Silicon GPU
- No color management / gamma correction
- `--sigma` parameter reserved for future noise application
- 1-channel (grayscale) uses scalar fallback for patch SSD
