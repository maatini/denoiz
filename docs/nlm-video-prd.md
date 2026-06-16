# v-denoise PRD

## Ziel

`v-denoise` — eine CLI für Videodenoising, die den bestehenden NLM-Algorithmus aus `denoise` auf Videoframes anwendet. Schnell, qualitativ hochwertig, Apple-Silicon-optimiert.

Zusätzlich: **Parameter-Tuning-Modus** (`--find-best-params`), der kurze Testclips mit verschiedenen Parameter-Kombinationen verarbeitet und die besten Einstellungen automatisch ermittelt.

---

## Architekturüberblick

```
v-denoise input.mp4 output.mp4 [options]
      │
      ▼
┌─────────────────────────────────────────────┐
│  FFmpeg (libavcodec/libavformat)            │  decode
│  → RAW-Frames (RGBA/float) im Speicher      │
└──────────────────┬──────────────────────────┘
                   ▼
┌─────────────────────────────────────────────┐
│  NLM Pipeline (aus denoise)             │  denoise
│  pro Frame: nlm_denoise_metal / _wavelet / … │
└──────────────────┬──────────────────────────┘
                   ▼
┌─────────────────────────────────────────────┐
│  FFmpeg (libavcodec → VideoToolbox)         │  encode
│  H.264 / H.265 / AV1                        │
└─────────────────────────────────────────────┘
```

**Designprinzipien:**
- `denoise`-Bibliothek pro Frame aufrufen (kein Duplizieren von NLM-Code)
- FFmpeg nur für I/O (decode frames → raw buffer, encode raw buffer → file)
- CLI-Metapher von `denoise` übernehmen (gleiche Parameterstruktur)
- Pipe-Architektur: Decoder → Filter → Encoder, parallelisiert

---

## Slice 1: Frame-by-Frame-Videopipeline

**Ziel:** Minimal funktionierendes Tool — Video öffnen, jeden Frame einzeln denoisen, als Video speichern.

**CLI:**
```
v-denoise input.mp4 output.mp4 [--preset medium] [--strength 0.8]
```

### Aufgaben

1. **devbox.json: ffmpeg als Dependency hinzufügen**
   - `ffmpeg@latest` (enthält CLI + libs: libavcodec, libavformat, libswscale, libavutil)

2. **CMakeLists.txt: v-denoise Target anlegen**
   - Neues Executable `v-denoise`, linkt gegen bestehende `nlm_core.h` Pipeline-Funktionen
   - Linkt gegen FFmpeg-Libs (avcodec, avformat, swscale, avutil)
   - NLM-Code wird nicht dupliziert — bestehende `.cpp`-Dateien werden mit-kompiliert

3. **src/nlm_video_main.cpp**
   - CLI-Parsing (FFmpeg-Input/Output, Presets, Strength)
   - Video-Input öffnen (avformat_open_input)
   - Codec-Kontext ermitteln (avcodec_find_decoder)
   - Frame-Loop: `av_read_frame` → `avcodec_send_packet` → `avcodec_receive_frame`
   - Konvertierung AVFrame → `Image` (float, 0..1, RGB)
   - NLM-Aufruf: je nach Preset passende Pipeline wählen
   - Encoding: `Image` → AVFrame → Encoder-Codec öffnen → `avcodec_send_frame` → `avcodec_receive_packet` → `av_interleaved_write_frame`

4. **Presets (Mapping auf bestehende Pipelines)**
   - `veryslow` → `--adaptive` (beste Qualität)
   - `slow` → `--use-gpu` (Metal)
   - `medium` → CPU NEON (Default)
   - `fast` → `--fast` (Multi-Resolution)
   - `veryfast` → `--wavelet` (Echtzeit)

5. **Strength-Mapping**
   - `--strength 0.0–1.0` → mappt direkt auf `--h` (Filterstärke)
   - Mapping: `h = strength * 0.25 + 0.05` → h in [0.05, 0.30]

### Verifikation

- Kurzer Testclip (10–30 sec) verarbeiten → Ausgabe ist valides mp4
- Visuell: weniger Rauschen sichtbar
- Metrik: PSNR der Frames vor/nach Denoising (via `ffmpeg -i original -i denoised -lavfi psnr`)
- Alle Presets durchlaufen, alle produzieren valide mp4

### Offene Punkte

- **Audio:** Original-Audiospuren übernehmen? (ja, Stream-Copy)
- **Pixelformat:** AVFrame-Pixelformate variieren (yuv420p, yuv444p, nv12…). RGB-Konvertierung via swscale nötig.
- **Codec-Auswahl beim Output:** Input-Codec erkennen und gleichen Codec für Output verwenden? Oder vom Preset abhängig?

---

## Slice 2: Temporal Denoising

**Ziel:** Echte, flimmerfreie Videodenoising-Qualität durch Nutzung mehrerer benachbarter Frames.

### Aufgaben

1. **Multi-Frame NLM**
   - Puffer von N Frames (konfigurierbar, default: 5)
   - NLM-Suchfenster nicht nur räumlich (in einem Frame), sondern auch zeitlich (in Nachbarframes)
   - Patch-Vergleich über 3D-Volumen (x, y, t)
   - Gewichtung: zeitlich weiter entfernte Frames werden schwächer gewichtet (temporal decay)

2. **Motion-Compensated NLM (Alternative/Ergänzung)**
   - Einfaches Block-Matching (16×16-Blöcke) zwischen Frame t und t±1/+2
   - Motion-Vektoren → NLM-Suche entlang der Bewegungstrajektorie
   - Weniger Ghosting bei schnellen Bewegungen

3. **Parameter**
   - `--frame-count N` (default: 5, Werte: 1=spatial only, 3/5/7)
   - `--motion` (aktiviert Motion-Compensated)
   - `--temporal-weight FLOAT` (default: 0.8, Gewichtung Zeit vs. Raum)

### Verifikation

- Vergleich mit HandBrake NLMeans (gleicher Clip, gleiche Einstellungen)
- Flimmern quantifizieren: Unterschied zwischen aufeinanderfolgenden denoisten Frames (temporal variance) sollte geringer sein als bei Slice 1
- Subjektive Bewertung: kein sichtbares Flackern bei statischen Szenen

### Offene Punkte

- **Frame-Puffer:** Speicherverbrauch steigt linear mit frame-count (pro Frame ~8 MB bei 1080p RGB float). Bei 5 Frames = 40 MB → akzeptabel.
- **Frame-Typen im Input:** I/P/B-Frames erfordern korrekte Decodierung. ffmpeg decoded automatisch in korrekter Reihenfolge — kein Problem.
- **OLV-Benchmark:** Brauchen wir ein standardisiertes Noisy-Video-Testset?

---

## Slice 3: Performance-Optimierung

**Ziel:** Echtzeit oder besser bei 1080p für leichte Presets.

### Aufgaben

1. **NLM-Kern in Metal portieren**
   - Existierender Metal-Kernel (`nlm_metal.mm`) auf Videoframes ausrichten
   - Tile-basiertes Processing (16×16 oder 32×32 Threadgruppen)
   - Shared Memory: Suchfenster in Threadgroup-Memory laden (reduziert globalen Speicherzugriff)
   - Patch-SSD per SIMD in Metal (simdgroup_float)

2. **Automatische Tile-Größenanpassung**
   - Device-Erkennung (M1/M2/M3/M4) → optimale Threadgroup-Größe
   - Heuristik: `threadgroup_size = min(32, max(16, gpu_cores / 8))`

3. **NEON-Optimierung für Multi-Frame**
   - `patch_ssd_neon3` (aus `nlm_cpu_neon.cpp`) auf 3D-Volumen erweitern
   - Temporal-SSD parallel zur Spatial-SSD berechnen

4. **Pipeline-Parallelisierung (GCD)**
   - Frame-Decoding, NLM, Encoding als drei Dispatch-Queues
   - Double-Buffering: während Frame n encodiert wird, wird Frame n+1 denoised und n+2 decoded
   - Ziel: GPU und CPU parallel auslasten

### Benchmark-Ziele

| Preset      | Auflösung  | Ziel-FPS auf M2       |
|-------------|------------|-----------------------|
| veryfast    | 1080p      | ≥ 60 fps              |
| fast        | 1080p      | ≥ 30 fps              |
| medium      | 1080p      | ≥ 15 fps              |
| slow        | 1080p      | ≥ 5 fps               |
| veryslow    | 1080p      | ≥ 2 fps               |

### Verifikation

- `--benchmark`-Flag: fps-Ausgabe pro Frame + Gesamt-Statistik
- Vergleich mit HandBrake (gleicher Clip, NLMeans-Filter, medium-Preset)

---

## Slice 4: Vollständiges Video-Tool

**Ziel:** Produktionstaugliches CLI mit allen Komfort-Features.

### Aufgaben

1. **Szenenerkennung (Shot Detection)**
   - Histogramm-Differenz zwischen aufeinanderfolgenden Frames
   - Schwellwert: wenn >30% Pixel-Differenz → neue Szene
   - Bei Szenenwechsel: Parameter zurücksetzen (keine zeitliche Glättung über Schnittgrenzen)
   - Adaptive-Parameter pro Szene: Helligkeit, Rauschlevel, Bewegung

2. **Progress-Anzeige**
   - Frame-Count aus Input-Video ermitteln (avformat → duration / time_base)
   - Aktuellen Fortschritt in % ausgeben
   - Geschätzte Restzeit (ETA) basierend auf durchschnittlicher Frame-Zeit
   - `\r`-basiertes In-Place-Updating (wie `denoise`)

3. **10-bit / HDR / Farbraum-Unterstützung**
   - 10-bit Input → 16-bit float Zwischenrepräsentation (statt 8-bit → 0..1)
   - HDR (PQ/HLG) → lineare Umrechnung vor NLM, Rückrechnung vor Encode
   - Farbraum-Passthrough: BT.709 / BT.2020 / DCI-P3 aus Input übernehmen
   - Encode-Codec entsprechend wählen (H.265 10-bit für HDR)

4. **Presets (Content-Adaptive)**
   - `--preset film` → adaptive, h=0.08, patch=7, search=15
   - `--preset grain` → wavelet, h=0.15, Denoise auf Luminanz-Kanal
   - `--preset lowlight` → adaptive, h=0.05, patch=5, search=21
   - `--preset animation` → wavelet, h=0.05, patch=5, search=15

### Verifikation

- Komplette Filme (30–120 Min) innerhalb akzeptabler Zeit verarbeiten
- Szenenerkennung: Log-Ausgabe zeigt korrekte Shot-Grenzen
- Progress-Bar aktualisiert sich während der Verarbeitung
- 10-bit HDR → Output ist valides HDR (via ffprobe Pixelformat prüfen)

---

## Slice 5: Erweiterte Features

**Ziel:** Ökosystem-Integration und Power-User-Features.

### Aufgaben

1. **Nur-Denoising-Modus (ohne Re-Encoding)**
   - `--frames-out DIR/` → denoiste Frames als PNG-Sequenz speichern
   - `--raw-out FILE` → denoiste Frames als raw float binary speichern
   - Use Case: "Denoise mit v-denoise → dann mit HandBrake encodieren"

2. **Filter-Chain (Denoise + Sharpen + Deinterlace)**
   - `--sharpen FLOAT` → Unsharp Mask (USM) nach NLM anwenden
   - `--deinterlace` → YADIF-Deinterlacing auf Interlaced-Video anwenden
   - Konfigurierbare Reihenfolge: `--filter-chain denoise,sharpen,deinterlace`

3. **GPU/CPU-Fallback + `--use-gpu`-Flag**
   - `--use-gpu` → Metal-Pipeline erzwingen
   - Ohne Flag: Auto-Select (Metal prüfen → verfügbar? Metal : NEON)
   - Fallback-Logik: Metal-Init schlägt fehl → "Metal not available, falling back to NEON"

4. **HandBrake-Integration**
   - README: Empfohlenen Workflow dokumentieren
   - Beispiel-Skript: `denoise_and_encode.sh` (v-denoise → HandBrake CLI)
   - FAQ: Wann v-denoise + HandBrake vs. v-denoise allein?

### Verifikation

- PNG-Frame-Export: Frames sind valide PNGs, Anzahl = Input-Frame-Count
- Sharpen visuell prüfen (USM-Parameter 0.5–2.0)
- Deinterlace: Input=Interlaced → Output=Progressive (ffprobe bestätigt)
- Fallback: Auf Mac ohne GPU (CI/VM) → NEON-Pipeline wird gewählt

---

## Abhängigkeiten zwischen Slices

```
Slice 1 (Frame-by-Frame)        ← Basis, kein anderer Slice wartet
    ├── Slice 2 (Temporal)      ← baut auf Slice-1-Decode/Encode auf
    ├── Slice 3 (Performance)   ← optimiert Slice 1 + 2
    └── Slice 4 (Full Tool)     ← baut auf Slice 1 + 2 + 3 auf
Slime 5 (Extended)                ← parallel zu 2/3/4 entwickelbar
```

## Risiken

- **FFmpeg-Linking:** libav*-Versionen müssen mit C++20 kompatibel sein. C-API via `extern "C"` → kein Problem.
- **VideoToolbox-Hardware-Encoding:** CMake muss `-framework VideoToolbox` linken. Nur auf macOS verfügbar → kein Cross-Platform.
- **Speicher bei langen Videos:** Frame-by-Frame verarbeitet jeweils nur einen Frame → kein Speicherproblem. Temporal (Slice 2) hält N Frames im Speicher.
- **Farbräume (Slice 4):** swscale-Farbkonvertierung kann Genauigkeitsverlust verursachen. float-RGB als Zwischenformat minimiert dies.

## Offene Fragen

1. **Audio-Stream:** Kopieren wir Audio-Streams unverändert (Stream Copy)? → Ja, Slice 1.
2. **Untertitel:** Stream-Copy für Subtitles? → Optional, Slice 4.
3. **Output-Codec wählbar?** → Ja, `--codec h264|h265|av1` via VideoToolbox. Slice 1.
4. **CRF/Qualitäts-Steuerung beim Encode?** → `--crf N` (default: 18). Slice 1.
5. **Test-Videos:** Woher bekommen wir reproduzierbare Noisy-Videos? → Eigenes Testset generieren? Oder öffentliche Datasets (Derf's, Xiph)?

---

## Parameter Tuning (--find-best-params)

### Ziel

Der User soll schnell gute NLM-Einstellungen für ein bestimmtes Video finden, ohne das gesamte Video mehrfach zu verarbeiten.

### CLI-Signatur

```
v-denoise input.mp4 \
  --find-best-params \
  --start 00:12:45 \
  --duration 20 \
  --param-grid "patch-size:5,7,9; h:0.4-1.2 step 0.2; temporal:1,2,3; prefilter:0,1" \
  --metric ssim \
  --output-dir ./tuning-results \
  --top 5
```

### Optionen

| Option | Bedeutung | Default |
|--------|----------|--------|
| `--find-best-params` | Aktiviert den Tuning-Modus | — |
| `--start` | Startzeit (HH:MM:SS oder Sekunden) | `00:00:00` |
| `--duration` | Länge des Testclips in Sekunden | `20` |
| `--param-grid` | Zu testende Parameter (Grid Search) | — (erforderlich) |
| `--metric` | Bewertungsmetrik (`ssim`, `psnr`, `perceptual`) | `ssim` |
| `--top` | Anzahl der besten Ergebnisse anzeigen | `5` |
| `--output-dir` | Ordner für Ergebnisse und Clips | `./tuning` |

### Param-Grid-Syntax

```
"param1:val1,val2,val3; param2:min-max step s; param3:val1,val2"
```

- Einzelwerte: `patch-size:5,7,9`
- Range: `h:0.4-1.2 step 0.2` → 5 Werte (0.4, 0.6, 0.8, 1.0, 1.2)
- Alle Parameter werden als Cartesian Product kombiniert
- Beispiel: `patch-size:5,7,9; h:0.4,0.7; temporal:1,2` → 3×2×2 = 12 Tests

### Wichtige Parameter für Grid Search

| Parameter | Bedeutung | Typische Werte |
|-----------|----------|---------------|
| `h` | Filterstärke (Hauptparameter) | 0.05–1.5 |
| `patch-size` | Patch-Größe (Detailerhalt) | 3, 5, 7, 9 |
| `search-window` | Suchfenstergröße | 11, 15, 21, 31 |
| `temporal` | Anzahl temporal benachbarter Frames | 1, 2, 3, 5 |
| `prefilter` | Vorfiltern aktiviert (0/1) | 0, 1 |

### Metriken

#### SSIM (Structural Similarity Index)
- Wahrnehmungsnahe Qualitätsmetrik (0..1, höher = besser)
- Berechnung: Frame-weise SSIM zwischen Original und Denoised
- FFmpeg-basiert: `ffmpeg -i original -i denoised -lavfi ssim`

#### PSNR (Peak Signal-to-Noise Ratio)
- Klassische Signalmetrik (dB, höher = besser)
- Nur relevant wenn Ground-Truth-Referenzclip verfügbar
- FFmpeg-basiert: `ffmpeg -i original -i denoised -lavfi psnr`

#### Perceptual Metric
- Edge Preservation Score: Verhältnis Kantenstärke vor/nach Denoising
- Noise Reduction Score: Varianzreduktion in homogenen Regionen
- Kombinierter Score = (Edge + NoiseRed) / 2

### Architektur

```
Input Video
    ↓
[FFmpeg] → Test-Segment extrahieren (--start + --duration)
    ↓
Parameter-Generator (Grid oder Smart)
    ↓
┌──────────────────────────────┐
│   Parallel Worker Pool       │
│   (Metal + NEON beschleunigt)│
│   - Worker 1: Param-Set A    │
│   - Worker 2: Param-Set B    │
│   - Worker 3: Param-Set C    │
└──────────────────────────────┘
    ↓
Metrik-Berechnung (SSIM etc.)
    ↓
Ranking + Top-Ergebnisse
    ↓
Ausgabe:
  - best-params.json
  - Top-5 Denoised Clips
  - Vergleichs-Video (optional)
```

### Output-Struktur

```
output-dir/
├── test-segment.mp4          # extrahierter Original-Clip
├── best-params.json           # Top-N Parameter-Sets + Scores
├── result_001.mp4             # Denoised Clip #1
├── result_002.mp4             # Denoised Clip #2
├── ...
├── result_NNN.mp4
├── comparison.mp4             # Side-by-Side Top-3 (optional)
└── preset_v-denoise.json      # Bestes Parameter-Set als Preset (optional)
```

### Umsetzungsslices

#### Tuning-Slice 1: Testclip + Basis-NLM
- `--start` und `--duration` implementieren
- FFmpeg-Segment-Extraktion (ohne Re-Encoding)
- Bestehende NLM-Pipeline auf Clip-Frames anwenden
- CLI-Grundstruktur für `--find-best-params`

#### Tuning-Slice 2: Parameter-Grid-Search
- `--param-grid` Parser (Einzelwerte + Range-Syntax)
- Cartesian-Product-Generator
- Alle Kombinationen nacheinander ausführen
- Ergebnisse als JSON speichern

#### Tuning-Slice 3: Bewertung & Ranking
- SSIM/PSNR via FFmpeg-Lavfi implementieren
- Perceptual Metric (Edge + Noise Score)
- Ranking nach gewählter Metrik
- Top-5 Ausgabe mit Parametern + Scores

#### Tuning-Slice 4: Performance & Parallelisierung
- GCD Concurrent Queue für parallele Testläufe
- Metal/NEON-Lastverteilung
- Fortschrittsanzeige mit ETA

#### Tuning-Slice 5: Smart Tuning & Presets
- Bayesian Optimization / Hill-Climbing statt Grid
- Rauscherkennung (Film Grain vs. Digital Noise)
- Preset speichern/laden (`--save-preset`, `--preset`)
- HandBrake-Vergleich (`--compare-with-handbrake`)

### Abhängigkeiten

```
Slice 1 (Frame-by-Frame) ← Basis für alle Video-Operationen
    └── Tuning-Slice 1 (Testclip + NLM) ← baut auf Slice 1 auf
             └── Tuning-Slice 2 (Grid Search) ← baut auf Tuning-Slice 1
                      ├── Tuning-Slice 3 (Bewertung & Ranking) ← parallel zu 2
                      └── Tuning-Slice 4 (Parallelisierung) ← baut auf 2+3
Tuning-Slice 5 (Smart Tuning) ← baut auf Tuning-Slice 2+3+4
```
