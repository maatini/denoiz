# NLM Denoise — Final (NTIRE 2025 Adaptionen abgeschlossen)

## Status

| Slice | Beschreibung | Status | Datum |
|-------|-------------|--------|-------|
| 1–5 | Core Slices | 🟢 abgeschlossen | 2026-05-27 |
| 6 | Wavelet-Domain NLM | 🟢 abgeschlossen | 2026-05-27 |
| 7 | Adaptive h (lokal) | 🟢 abgeschlossen | 2026-05-27 |
| 8 | Overlap + Ensemble | 🟡 dokumentiert | 2026-05-27 |
| 9 | Coarse-to-Fine | 🟢 abgeschlossen | 2026-05-27 |

## Phase 8 — Ensemble (dokumentiert, negatives Resultat)

Multi-h Ensemble (h-δ, h, h+δ) zeigt keinen Qualitätsgewinn für klassisches NLM.
NTIREE-Ensemble-Konzept ist DL-spezifisch; simple Output-Mittelung verwässert bei NLM das Resultat.

## Phase 9 — Ergebnis

- 2× Downsample → NLM coarse → Residual → NLM fine
- 55× schneller als naive (0.1s für 256×256)
- 3× vs NEON
- PSNR 47.3 dB (zwischen Fast 53 dB und Wavelet 52 dB)

## Gesamt-Metrikentabelle (256×256 RGB, patch=7, search=21, h=0.1, Apple M2)

| Pipeline | Laufzeit | vs naive | PSNR |
|---|---|---|---|
| naive | 5.5s | 1× | ∞ |
| NEON+GCD | 0.29s | 20× | **71.6 dB** |
| Adaptive h | 0.62s | 9× | **77.8 dB** |
| Metal GPU | 0.16s | 36× | 71.6 dB |
| Coarse-to-Fine | 0.10s | 55× | 47.3 dB |
| Fast (multi-res) | 0.09s | 60× | 53.0 dB |
| Wavelet | 0.003s | 1705× | 52.3 dB |

## NTIRE 2025 Adaptionen — Fazit

| Phase | NTIRE-Technik | Adaption | Ergebnis |
|---|---|---|---|
| 6 ✅ | Wavelet Transform Loss | DWT-NLM | 1705× schnell, 52.3 dB |
| 7 ✅ | Data Selection | Adaptive h | **Höchste Qualität: 77.8 dB** |
| 8 🟡 | Ensemble | Multi-h Mittelung | Kein Gewinn (DL-spezifisch) |
| 9 ✅ | Progressive Learning | Coarse-to-Fine | 55× schnell, 47.3 dB |

Die wertvollsten Adaptionen aus 2504.12276:
1. **Adaptive h** — PSNR 77.8 dB (+6 dB über Standard NEON)
2. **Wavelet** — Extrem schnell (1705×), gut für Preview
3. **Coarse-to-Fine** — Gute Balance (55× bei 47 dB)
