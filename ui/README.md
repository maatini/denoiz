# DenoizUI — macOS SwiftUI App

Native macOS UI for the [denoiz](https://github.com/maatini/denoiz) image denoising tool.

## Requirements

- macOS 13 (Ventura) or later
- Xcode 15 or later
- The `denoise` CLI binary (built via CMake)

## Build & Run

### 1. Build the denoise binary

```bash
# From the project root:
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces `build/denoise`.

### 2. Open the Xcode project

```bash
open ui/DenoizUI.xcodeproj
```

Select the **DenoizUI** scheme and press ⌘R.

The Xcode project has a build phase that copies `build/denoise` into the app bundle automatically. If the binary is not found during the build, the app will fall back to searching `PATH` at runtime.

## Architecture

```
ui/
├── DenoizUI.xcodeproj/        # Xcode project
├── DenoizUI/
│   ├── DenoizUIApp.swift      # @main entry point
│   ├── ContentView.swift      # Root layout (NavigationSplitView)
│   ├── Info.plist
│   ├── Assets.xcassets/       # App icon
│   ├── Models/
│   │   └── DenoisingParameters.swift
│   ├── Services/
│   │   └── DenoiseService.swift    # Process wrapper
│   ├── Utilities/
│   │   └── ImageLoader.swift       # NSImage ↔ temp file
│   └── Views/
│       ├── SidebarView.swift
│       ├── ImageComparisonView.swift
│       ├── ImageDropZone.swift
│       ├── PipelinePicker.swift
│       └── ParameterControls.swift
```

## How it works

The SwiftUI app calls the existing `denoise` CLI binary via `Process` (subprocess):

1. User drops an image → written to temp file
2. User clicks "Denoise" → `denoise input.png output.png --params...` is spawned
3. Output PNG is read back into an NSImage
4. Original and denoised images are displayed side-by-side

The plan is to upgrade to direct C++ library calls via Swift-C++ interop in Phase 2.
