# NLM Denoise — Final

## Status

| Slice | Beschreibung | Status | Datum |
|-------|-------------|--------|-------|
| 1 | Grundgerüst + naive Referenz | 🟢 abgeschlossen | 2026-05-27 |
| 2 | Parallelisierung + NEON | 🟢 abgeschlossen | 2026-05-27 |
| 3 | Metal GPU | 🟢 abgeschlossen | 2026-05-27 |
| 4 | NPU/ANE + Optimierungen | 🟢 abgeschlossen | 2026-05-27 |
| 5 | Finalisierung & Packaging | 🟢 abgeschlossen | 2026-05-27 |

## Slice 5 — Abgeschlossen

- **`devbox run build | test | install | clean`** — alle Scripts funktionieren
- **Param-Validierung**: negative/zero Werte, h > 0 enforced
- **Metal-Shader embedded**: als C-String-Literal (`R"METAL(...)METAL"`), kein externes File nötig
- **Release-Build**: `-O3 -flto -ffast-math -march=armv8.5-a -mtune=apple-m1`
- **`make install`**: cmake install target → `install/bin/nlm_denoise`
- **README.md**: Build, Usage, Performance-Tabelle, Architektur, Limitations
- **Tests**: 3 ctest entries (suite, GPU basic, GPU benchmark) → alle grün
- **Binary**: arm64 Mach-O, 193 KB, keine externen Deps außer System-Frameworks

## Finale Metriken (512×512 RGB, patch=7, search=21, h=0.1, Apple M2)

| Pipeline | Laufzeit | vs naive |
|---|---|---|
| naive | 23.3s | 1.0× |
| NEON+GCD | 1.2s | 19× |
| Metal GPU | 0.37s | 63× |
| Fast (multi-res) | 0.30s | 79× |

PSNR: GPU 74.8 dB, Fast 52.9 dB vs naive.

## Dateien

```
src/
  nlm_core.h            Shared types
  nlm_cli.cpp           CLI parser
  nlm_io.cpp            PNG I/O (stb_image)
  nlm_cpu.cpp           Naive reference
  nlm_cpu_neon.cpp      ARM NEON + GCD
  nlm_cpu_neon_fast.cpp Multi-resolution + Accelerate
  nlm_metal.mm          Metal GPU (embedded shader)
  nlm_main.cpp          Entry point
  nlm_metal_kernels.metal  (preserved, not needed — embedded)
  nlm_ane_analysis.txt  ANE feasibility notes
test/
  run_tests.py          Test suite (4 assert groups)
  generate_images.py    Test image generator
  *.png                 Test images
README.md
devbox.json
plan.md
progress.md
```
