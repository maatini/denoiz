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
