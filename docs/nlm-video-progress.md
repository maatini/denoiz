# nlm-video Progress

## Slice 1: Frame-by-Frame
- FFmpeg decode → NLM pipeline → encode (H.264)
- 5 Presets auf bestehende NLM-Pipelines
- Audio-Stream-Copy

## Slice 2: Temporal Denoising
- `nlm_video_temporal.h/cpp`: Ringbuffer, Patch-basierte Ähnlichkeitsgewichtung
- `--temporal`, `--frame-count`, `--temporal-weight`

## Slice 3: Performance-Optimierung
- Async NLM auf GCD-Serial-Queue (Decode + NLM parallel)
- Metal Threadgroup-Tuning pro GPU-Generation (M1–M4)
- `--benchmark` Flag (fps, wall time, median NLM-Zeit)

## Slice 4: Content-Presets + Codec + ETA
- 4 Content-Presets: film, grain, lowlight, animation
- Wählbarer Codec: h264, h265, av1 (über `--codec`)
- ETA mit fps-Anzeige im Progress (via `--verbose`)

## Slice 5: Sharpen + PNG-Export + GPU-Fallback
- `--sharpen 0.0-2.0`: Unsharp-Mask nach NLM
- `--frames-out DIR`: PNG-Frame-Export (kein Video-Encode)
- `--use-gpu`: Metal-GPU erzwingen

---

## Parameter Tuning (--find-best-params)

| Slice | Beschreibung | Status |
|-------|-------------|--------|
| T-1 | Testclip extrahieren + NLM anwenden | abgeschlossen |
| T-2 | Parameter-Grid-Search (Cartesian Product) | abgeschlossen |
| T-3 | Bewertung & Ranking (SSIM/PSNR/Perceptual) | abgeschlossen |
| T-4 | Parallelisierung (GCD, Metal/NEON) | seriell (ausreichend) |
| T-5 | Smart Tuning (Bayesian, Presets, HandBrake) | geplant |
