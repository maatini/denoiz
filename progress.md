# NLM Denoise — Fortschritt

## Aktueller Slice: Phase 6 — Wavelet-Domain NLM 🟢

## Status

| Slice | Beschreibung | Status | Datum |
|-------|-------------|--------|-------|
| 1 | Grundgerüst + naive Referenz | 🟢 abgeschlossen | 2026-05-27 |
| 2 | Parallelisierung + NEON | 🟢 abgeschlossen | 2026-05-27 |
| 3 | Metal GPU | 🟢 abgeschlossen | 2026-05-27 |
| 4 | NPU/ANE + Optimierungen | 🟢 abgeschlossen | 2026-05-27 |
| 5 | Finalisierung & Packaging | 🟢 abgeschlossen | 2026-05-27 |
| 6 | Wavelet-Domain NLM | 🟢 abgeschlossen | 2026-05-27 |
| 7 | Adaptive h (lokal) | 🔴 nicht begonnen | — |
| 8 | Overlap + Ensemble | ⚪ offen | — |
| 9 | Coarse-to-Fine | ⚪ offen | — |

## Phase 6 — Ergebnis

- **2-Level Haar DWT:** Forward-Decomposition in LL, LH, HL, HH (Level 1 + 2)
- **NLM auf LL2:** Coarsest Approximation bekommt volles NLM (reduzierter patch/search)
- **Hard Threshold auf Details:** Rausch-Schätzung via max |HH1| / 0.6745 → Schwellwert
- **IDWT-Rekonstruktion:** Level 2 → Level 1 → Output
- **`--wavelet` CLI-Flag**

## Metriken — Wavelet vs. Standard

| Metrik | 256×256 | 512×512 |
|--------|---------|---------|
| Laufzeit Wavelet | 0.0032s | 0.015s |
| Speedup vs naive | **1705×** | **1555×** |
| Speedup vs NEON | **88×** | **78×** |
| PSNR vs naive | 52.3 dB | 46.0 dB |

Wavelet ist das schnellste Pipeline (3–15 ms). Qualität auf Niveau von `--fast` (multi-resolution).
Für Preview/Rapid-Denoising ideal.

## NTIRE 2025 Adaptionsquellen

| Phase | Technik | Team | Ergebnis |
|---|---|---|---|
| 6 ✅ | Wavelet Transform Loss | SRC-B (#1, 31.20 dB) | DWT-NLM: 88× vs NEON, 52.3 dB |
| 7 | Data Selection → Adaptives h | SRC-B (#1) | offen |
| 8 | Overlap + Ensemble | Allgemein + SNUCV (#2) | offen |
| 9 | Progressive Learning → Coarse-to-Fine | SRC-B (#1) | offen |
