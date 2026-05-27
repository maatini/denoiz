# NLM Denoise — Fortschritt

## Aktueller Slice: Phase 6 — Wavelet-Domain NLM 🔴

## Status

| Slice | Beschreibung | Status | Datum |
|-------|-------------|--------|-------|
| 1 | Grundgerüst + naive Referenz | 🟢 abgeschlossen | 2026-05-27 |
| 2 | Parallelisierung + NEON | 🟢 abgeschlossen | 2026-05-27 |
| 3 | Metal GPU | 🟢 abgeschlossen | 2026-05-27 |
| 4 | NPU/ANE + Optimierungen | 🟢 abgeschlossen | 2026-05-27 |
| 5 | Finalisierung & Packaging | 🟢 abgeschlossen | 2026-05-27 |
| 6 | Wavelet-Domain NLM | 🔴 nicht begonnen | — |
| 7 | Adaptive h (lokal) | ⚪ offen | — |
| 8 | Overlap + Ensemble | ⚪ offen | — |
| 9 | Coarse-to-Fine | ⚪ offen | — |

## NTIRE 2025 Adaptionsquellen

| Phase | Technik | Team | Erwarteter PSNR-Gewinn |
|---|---|---|---|
| 6 | Wavelet Transform Loss | SRC-B (#1, 31.20 dB) | +0.5–1.5 dB |
| 7 | Data Selection → Adaptives h | SRC-B (#1) | +0.2–0.5 dB |
| 8 | Overlap + Ensemble | Allgemein + SNUCV (#2) | +0.3–0.8 dB |
| 9 | Progressive Learning → Coarse-to-Fine | SRC-B (#1) | +0.2–0.4 dB |
