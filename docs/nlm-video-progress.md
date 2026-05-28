# nlm-video Progress

## Slice 1: Frame-by-Frame ✓
- FFmpeg decode → NLM pipeline → FFmpeg encode (H.264)
- 5 Presets (veryslow/slow/medium/fast/veryfast) auf bestehende NLM-Pipelines
- Audio-Stream-Copy
- PSNR-Tests: Output valide, Denoising-Effekt messbar

## Slice 2: Temporal Denoising ✓
- `nlm_video_temporal.h/cpp`: Ringbuffer mit N Frames
- Patch-basierte Ähnlichkeitsgewichtung am selben Pixel über alle gepufferten Frames
- `--temporal`, `--frame-count 1-7`, `--temporal-weight 0.0-1.0`
- PSNR ähnlich wie spatial (24.6 dB), Vorteil liegt in zeitlicher Konsistenz

## Slice 3: Performance-Optimierung ✓ (läuft)
- Metal-Threadgroup-Tuning pro GPU-Generation (M1–M4) `nlm_metal.mm`
- GCD-Pipeline-Parallelisierung: NLM asynchron auf Serial-Queue (Doppel-Puffering)
- `--benchmark`-Flag mit fps, wall time, median NLM-Zeit
- **Benchmarks (M2, 320×240 RGB, 25fps)**:
  | Preset    | fps     | NLM median |
  |-----------|---------|------------|
  | medium    | 83.4    | 11.0 ms    |
  | slow      | 83.0    | —          |
  | fast      | 124.4   | —          |
  | veryfast  | 125.1   | —          |
- **640×480 medium**: 20 fps, 44.8 ms median

## Ausstehend
- Slice 4: Szenenerkennung, 10-bit/HDR, Progress/ETA, Content-Presets
- Slice 5: PNG-Export, Filter-Chain, GPU-Fallback, HandBrake-Integration
