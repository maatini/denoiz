# NLM Denoise - Projektplan

## Ziel

Hochperformante, produktionsreife CLI-Anwendung für Non-Local Means (NLM) Denoising, maximal optimiert für Apple Silicon (ARM64 + NEON + Metal + ANE/NPU).

## CLI-Signatur

```
nlm_denoise input.png output.png \
  --patch-size 7 \
  --search-window 21 \
  --h 0.1 \
  --sigma 0.0 \
  [--use-gpu] \
  [--threads auto] \
  [--verbose]
```

## Technische Vorgaben

- **Sprache:** Modernes C++20, Objective-C++ nur wo Metal zwingend nötig
- **Build-System:** CMake, ausschließlich Devbox-Umgebung (`devbox shell`)
- **Target:** `arm64-apple-macos`, `-march=armv8.5-a -mtune=apple-m1`
- **Deps:** Keine schweren externen Libs; `stb_image` + `stb_image_write` (header-only), Apple System-Frameworks (Metal, Accelerate, GCD)
- **Code-Struktur:** Modular (`nlm_cpu.cpp`, `nlm_cpu_neon.cpp`, `nlm_metal.mm`, `nlm_cli.cpp`)
- **Qualität:** Release-Build `-O3 -flto -ffast-math`, PSNR/SSIM-Metriken

## Realisierungsstrategie: Vertikale Splices

Jeder Slice ist vollständig lauffähig, getestet auf Apple Silicon (Devbox), mit messbarer Performance-Verbesserung, dokumentiert und committed.

---

### Slice 1 — Grundgerüst + naive Referenz

**Ziel:** Korrekte, lauffähige Basis.

- [ ] Devbox initialisieren (`devbox.json` mit `clang`, `cmake`, `git`, `stb` via fetch)
- [ ] Projektstruktur anlegen (C++20, CMake)
- [ ] CLI-Parser (einfach, z.B. `argparse` oder eigener)
- [ ] Image-I/O mit `stb_image` + `stb_image_write`
- [ ] Naive NLM: double-loop über alle Pixel + Search-Window, Patch-SSD mit `exp(-d²/h²)`, Weighted Average
- [ ] Test: Noisy-Testbild → CLI-Aufruf → Output-Bild → visuelle Validierung + PSNR-Metrik

**Metriken:** Korrektheit (visuell), PSNR gegen Ground Truth

---

### Slice 2 — Parallelisierung + ARM64 NEON

**Ziel:** Erster großer Performance-Sprung (4–8×).

- [ ] Multi-Threading mit GCD (`dispatch_apply`)
- [ ] ARM NEON Intrinsics für kritische Loops (Patch-Distanz, Weighting, Summation)
- [ ] Timing mit `mach_absolute_time` + Benchmark-Output
- [ ] Test: Laufzeit-Vergleich Slice 1 vs. Slice 2 auf M-Serie-Hardware

**Metriken:** Speedup 4–8×, identische Bildqualität

---

### Slice 3 — Metal GPU-Beschleunigung

**Ziel:** Massive Parallelisierung (10–30×).

- [ ] Metal Compute Pipeline (`MTLDevice`, Command Queue, Buffers mit unified memory)
- [ ] Shader 1: Patch-Distanz-Kernel (tiled, shared/threadgroup memory)
- [ ] Shader 2: Weighting + Accumulation-Kernel
- [ ] CPU-Fallback + `--use-gpu` Flag
- [ ] Speicher-Management (keine unnötigen Copies)
- [ ] Test: GPU vs. CPU-Benchmark

**Metriken:** Speedup 10–30× bei großen Bildern, identische Bildqualität

---

### Slice 4 — NPU/ANE-Exploration + weitere Optimierungen

**Ziel:** ANE-Nutzung und algorithmische Optimierungen.

- [ ] ANE-Analyse (Core ML, BNNS, Custom Compute Graph)
- [ ] Hybride Lösung oder Approximation falls exakter NLM nicht auf ANE abbildbar
- [ ] Hierarchische Suche / Multi-Resolution
- [ ] Pre-Filtering (Box-Filter auf Patches)
- [ ] Accelerate-Framework (`vDSP`, `vImage`) wo sinnvoll
- [ ] Umfassendes Benchmarking (Laufzeit, PSNR/SSIM, Energieverbrauch)

**Metriken:** Energieeffizienz, weiterer Speedup

---

### Slice 5 — Finalisierung, QA & Packaging

**Ziel:** Release-Ready Binary.

- [ ] Vollständige CLI (Defaults, Validierung, Progress, `--verbose`, `--benchmark`)
- [ ] Error-Handling, saubere Fehlermeldungen
- [ ] Unit-Tests (Catch2 oder simple Test-Harness)
- [ ] Release-Build (`-O3 -flto -ffast-math`)
- [ ] `make install` Target
- [ ] README: Build-Anleitung (Devbox), Usage, Performance-Tabelle (M1–M4), Limitationen

**Metriken:** Alle Tests grün, Build reproduzierbar

---

### Phase 6 — Wavelet-Domain NLM (NTIRE 2025 Adaption)

**Quelle:** SRC-B (Platz 1, 31.20 dB) — Wavelet Transform Loss als Kerninnovation.

**Ziel:** NLM in DWT-Koeffizienten statt Pixel-Domain → bessere Kantenerhaltung.

- [ ] 2-Level DWT (Haar/CDF 9/7) via Accelerate/vDSP
- [ ] NLM auf LL-Subband mit reduziertem search_window
- [ ] Thresholding auf LH, HL, HH (VisuShrink / BayesShrink)
- [ ] Adaptive h aus DWT-Koeffizienten (HH-Subband)
- [ ] IDWT-Inverse → Output
- [ ] `--wavelet` CLI-Flag
- [ ] Test: PSNR-Vergleich Pixel-NLM vs Wavelet-NLM
- [ ] Benchmark: Laufzeit Wavelet vs Standard

**Metriken:** PSNR +0.5–1.5 dB, Kantenerhalt visuell sichtbar

---

### Phase 7 — Adaptive h (lokal, NTIRE 2025 Adaption)

**Quelle:** SRC-B Data Selection → lokale Bildstatistik steuert Filtergrad.

**Ziel:** Globales h durch lokale Schätzung ersetzen.

- [ ] Noise-Level Estimation (MAD auf HH-Koeffizienten)
- [ ] Lokale h-Map: h(x,y) = h_global * (1 + α * variance_patch(x,y))
- [ ] Metal-Integration: h als texture buffer statt scalar
- [ ] `--adaptive` CLI-Flag
- [ ] Test: PSNR-Vergleich global vs adaptiv

**Metriken:** PSNR +0.2–0.5 dB, weniger Detailverlust in glatten Regionen

---

### Phase 8 — Overlapping Patches + Multi-h Ensemble (NTIRE 2025 Adaption)

**Quelle:** Overlapping Patches (allgemein), Ensemble (SNUCV Platz 2).

**Ziel:** Inference-Qualität durch Overlap und Ensemble steigern.

- [ ] Overlapping Search: stride = patch_size / 2 mit Weighted Blending
- [ ] Multi-h Ensemble: 3 Durchläufe mit h-δ, h, h+δ, Output-Mittelung
- [ ] GCD-Parallel über Ensemble-Members
- [ ] `--ensemble` CLI-Flag
- [ ] Test: PSNR single-h vs ensemble

**Metriken:** PSNR +0.3–0.8 dB

---

### Phase 9 — Coarse-to-Fine Refinement (NTIRE 2025 Adaption)

**Quelle:** Progressive Learning Strategy → iterative Verfeinerung.

**Ziel:** Mehrstufige NLM-Anwendung.

- [ ] Stufe 1: 4× Downsample → NLM (grob)
- [ ] Stufe 2: Upsample → Residual = noisy - coarse
- [ ] Stufe 3: NLM auf Residual (feine Details)
- [ ] Output = coarse + refined_residual
- [ ] Test: PSNR single-pass vs multi-pass

**Metriken:** PSNR +0.2–0.4 dB

---

## NTIRE 2025 Adaptionsquellen

| Phase | NTIRE-Technik | Team | Adaption |
|---|---|---|---|
| 6 | Wavelet Transform Loss | SRC-B (#1) | DWT+IDWT NLM |
| 7 | Data Selection | SRC-B (#1) | Adaptive lokale h-Map |
| 8 | Overlapping Patches | Allgemein | Overlap Search + Blend |
| 8 | Model Ensemble | SNUCV (#2) | Multi-h Ensemble |
| 9 | Progressive Learning | SRC-B (#1) | Coarse-to-Fine Pipeline |

Nicht adaptiert (DL-spezifisch): Hybrid Transformer+CNN, Data Selection, Model Ensemble (multi-model).
