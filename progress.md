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

---

## Phase 10 — nlm-video Parameter Tuning

| Slice | Beschreibung | Status | Datum |
|-------|-------------|--------|-------|
| 10.1 | Testclip + NLM-Anwendung | 🟢 abgeschlossen | 2026-05-29 |
| 10.2 | Parameter-Grid-Search | 🟢 abgeschlossen | 2026-05-29 |
| 10.3 | Bewertung & Ranking | 🟢 abgeschlossen | 2026-05-29 |
| 10.4 | Performance & Parallelisierung | 🟡 seriell (ausreichend) | 2026-05-29 |
| 10.5 | Erweiterte Features (Smart Tuning) | 🔴 geplant | — |

### Slice 10.1–10.3 — Umgesetzt
- `--find-best-params`, `--start`, `--duration`, `--param-grid`, `--metric`, `--top`, `--output-dir`
- FFmpeg-CLI Segment-Extraktion, libav Frame-Decoding
- Grid-Search: Einzelwerte (`patch-size:5,7`) + Range (`h:0.2-0.6 step 0.2`)
- Cartesian Product aller Parameterebenen
- SSIM eigene Implementierung (8x8 sliding window), PSNR via `::psnr()`, Perceptual (Edge+Noise)
- Ranking nach `--metric ssim|psnr|perceptual`, JSON-Output in `best-params.json`

### Test-Ergebnis (eblue53.mp4, 640x480)
- 3 h-Werte (0.1/0.3/0.5) auf 0.5s: 67s → beste SSIM 0.982 (h=0.1)
- 6 Komb. (patch-size:5,7 × h:0.2-0.6) auf 0.3s: 58s → beste PSNR 38.9 dB (h=0.2, patch=5)
