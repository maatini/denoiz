# NLM Denoise — Fortschritt

## Aktueller Slice: Phase 8 — Overlap + Ensemble 🔴

## Status

| Slice | Beschreibung | Status | Datum |
|-------|-------------|--------|-------|
| 1–5 | Core Slices | 🟢 abgeschlossen | 2026-05-27 |
| 6 | Wavelet-Domain NLM | 🟢 abgeschlossen | 2026-05-27 |
| 7 | Adaptive h (lokal) | 🟢 abgeschlossen | 2026-05-27 |
| 8 | Overlap + Ensemble | 🔴 nicht begonnen | — |
| 9 | Coarse-to-Fine | ⚪ offen | — |

## Phase 7 — Ergebnis

- Lokale Varianz-Map (Sliding Window, Radius=min(patch,7))
- h(x,y) = h_base × (1 + 0.5 × (var/mean_var - 1))
- Per-Pixel Filterung: glatte Regionen → kleine h, noisy → große h
- PSNR 77.2–77.8 dB (0.6 dB **besser** als Standard NEON 71.6 dB)
- Laufzeit 0.62s (Scalar-SSD, nicht optimiert) — quality-first pipeline

## Metriken

| Pipeline | 256×256 Zeit | vs naive | vs NEON | PSNR |
|---|---|---|---|---|
| naive | 5.7s | 1× | — | ∞ |
| NEON+GCD | 0.29s | 20× | 1× | 71.6 dB |
| Metal GPU | 0.16s | 36× | 1.9× | 71.6 dB |
| Fast | 0.09s | ~60× | 3× | 53.0 dB |
| Wavelet | 0.003s | 1705× | 88× | 52.3 dB |
| Adaptive | 0.62s | 9× | 0.46× | **77.8 dB** |
