# AGENTS

## Karpathy Guidelines

Befolge die [Karpathy Guidelines](https://x.com/karpathy/status/2015883857489522876) für LLM-Coding:

### 1. Think Before Coding
- Annahmen explizit nennen. Bei Unklarheit nachfragen.
- Mehrere Interpretationsmöglichkeiten aufzeigen, nicht stillschweigend eine wählen.
- Wenn ein einfacherer Ansatz existiert, darauf hinweisen.
- Bei Verwirrung: stoppen, benennen, fragen.

### 2. Simplicity First
- Keine ungefragten Features, keine Abstraktionen für einmaligen Code.
- Keine "Flexibilität" oder "Konfigurierbarkeit" die nicht verlangt wurde.
- Kein Error-Handling für unmögliche Szenarien.
- Wenn 200 Zeilen auch in 50 gehen: umschreiben.

### 3. Surgical Changes
- Nur ändern was nötig ist. Kein angrenzenden Code, keine Kommentare, kein Format "verbessern".
- Kein Refactoring von nicht-kaputtem Code.
- Bestehenden Stil matchen, auch wenn man es anders machen würde.
- Toten Code, den die eigenen Änderungen erzeugen, entfernen; bestehenden toten Code nicht anfassen.

### 4. Goal-Driven Execution
- Tasks in verifizierbare Ziele umwandeln.
- Bei Multi-Step: Plan mit Verify-Schritten angeben.
- Tests schreiben die das Problem reproduzieren, dann fixen.

## Plattform

- **Apple Silicon M1 (arm64)** — alle nativen Binaries, Docker-Images, Builds für `linux/arm64` bzw. `darwin/arm64` auslegen
- **Apple Neural Engine (NPU)** — Code für die Neural Processing Unit des M1 optimieren (ANE/CoreML/BNNS)

## Projekt

Dieses Verzeichnis enthält ein devbox-basiertes Projekt. Vor Arbeiten am Code `devbox.json` prüfen.

### ui/ — DenoizUI macOS App (SwiftUI)

Das `ui/`-Verzeichnis enthält ein **eigenständiges Xcode-Projekt** (nicht CMake-integriert):
- `ui/DenoizUI.xcodeproj` — Xcode-Projekt für die SwiftUI-App
- `ui/DenoizUI/` — Swift-Quellcode (~1.326 Zeilen)
- Build-Reihenfolge: zuerst `cmake --build build` (CLI-Binary), dann Xcode-Projekt ⌘R
- Die App ruft das `denoise`-Binary via `Process` auf (kein direktes C++-Linking)
- **Xcode 26 Build-Fixes**: Bei Build-Fehlern prüfen ob `LD=/usr/bin/clang`, `ENABLE_DEBUG_DYLIB=NO`, `ENABLE_USER_SCRIPT_SANDBOXING=NO` im pbxproj gesetzt sind
- Edit-Tool-Warnung: NIEMALS `replace_all: true` mit `};` im pbxproj verwenden!

DenoizUI-Memories: [[denoiz-macos-ui-app]], [[denoiz-xcode26-build]], [[denoiz-editing-patterns]]

### ui/ — DenoizUI macOS App (SwiftUI)

Das `ui/`-Verzeichnis enthält ein **eigenständiges Xcode-Projekt** (nicht CMake-integriert):
- `ui/DenoizUI.xcodeproj` — Xcode-Projekt für die SwiftUI-App
- `ui/DenoizUI/` — Swift-Quellcode (~1.326 Zeilen)
- Build-Reihenfolge: zuerst `cmake --build build` (CLI-Binary), dann Xcode-Projekt ⌘R
- Die App ruft das `denoise`-Binary via `Process` auf (kein direktes C++-Linking)
- **Xcode 26 Build-Fixes**: Bei Build-Fehlern prüfen ob `LD=/usr/bin/clang`, `ENABLE_DEBUG_DYLIB=NO`, `ENABLE_USER_SCRIPT_SANDBOXING=NO` im pbxproj gesetzt sind
- Edit-Tool-Warnung: NIEMALS `replace_all: true` mit `};` im pbxproj verwenden!

DenoizUI-Memories: [[denoiz-macos-ui-app]], [[denoiz-xcode26-build]], [[denoiz-editing-patterns]]
