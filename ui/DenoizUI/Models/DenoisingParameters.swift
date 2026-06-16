import Foundation

/// Denoising pipeline mode — maps to CLI flags (mutually exclusive, first match wins).
enum PipelineMode: String, CaseIterable, Identifiable, Codable {
    case neon = "NEON/GCD (Default)"
    case gpu = "Metal GPU"
    case fast = "Fast (Multi-Res)"
    case wavelet = "Wavelet"
    case adaptive = "Adaptive h"
    case ensemble = "Ensemble"
    case coarseToFine = "Coarse-to-Fine"

    var id: String { rawValue }

    /// CLI flag for this pipeline mode (nil = default/NEON pipeline).
    var cliFlag: String? {
        switch self {
        case .neon:          return nil
        case .gpu:           return "--use-gpu"
        case .fast:          return "--fast"
        case .wavelet:       return "--wavelet"
        case .adaptive:      return "--adaptive"
        case .ensemble:      return "--ensemble"
        case .coarseToFine:  return "--coarse-to-fine"
        }
    }

    /// Short description shown in the picker subtitle.
    var description: String {
        switch self {
        case .neon:          return "ARM NEON + Grand Central Dispatch"
        case .gpu:           return "Metal GPU compute pipeline"
        case .fast:          return "Multi-resolution (2× downsample)"
        case .wavelet:       return "Wavelet-domain (DWT + threshold)"
        case .adaptive:      return "Per-pixel filter strength (local variance)"
        case .ensemble:      return "Multi-h ensemble (3 members)"
        case .coarseToFine:  return "Coarse-to-fine (4× downsample → residual)"
        }
    }
}

/// Observable model holding all denoising parameters.
/// Mirrors the `NlmParams` struct from `src/nlm_core.h`.
final class DenoisingParameters: ObservableObject {
    // ── Core NLM parameters ───────────────────────────────────────────────

    @Published var patchSize: Int = 7 {
        didSet {
            let clamped = clampedOdd(patchSize, min: 3, max: 15)
            if clamped != patchSize { patchSize = clamped }
        }
    }
    @Published var searchWindow: Int = 21 {
        didSet {
            let clamped = clampedOdd(searchWindow, min: 7, max: 51)
            if clamped != searchWindow { searchWindow = clamped }
        }
    }
    @Published var h: Double = 0.1 {
        didSet {
            let clamped = max(0.01, min(1.0, h))
            if clamped != h { h = clamped }
        }
    }

    // ── Pipeline ──────────────────────────────────────────────────────────

    @Published var pipeline: PipelineMode = .neon

    // ── Threading / debug ─────────────────────────────────────────────────

    @Published var threadCount: Int = 0   // 0 = auto (GCD)
    @Published var verbose: Bool = false

    // ── Computed pipeline flags ───────────────────────────────────────────

    var useGPU: Bool           { pipeline == .gpu }
    var useFast: Bool          { pipeline == .fast }
    var useWavelet: Bool       { pipeline == .wavelet }
    var useAdaptive: Bool      { pipeline == .adaptive }
    var useEnsemble: Bool      { pipeline == .ensemble }
    var useCoarseToFine: Bool  { pipeline == .coarseToFine }

    // ── CLI argument builder ──────────────────────────────────────────────

    /// Returns the full argument list for the `denoise` binary.
    func buildArguments(inputPath: String, outputPath: String) -> [String] {
        var args: [String] = [
            inputPath,
            outputPath,
            "--patch-size", "\(patchSize)",
            "--search-window", "\(searchWindow)",
            "--h", String(format: "%.4f", h),
            "--threads", "\(threadCount)",
        ]
        if let flag = pipeline.cliFlag {
            args.append(flag)
        }
        if verbose {
            args.append("--verbose")
        }
        return args
    }

    // ── Helpers ───────────────────────────────────────────────────────────

    /// Direct setters with clamping (bypasses didSet for reliability in tests).
    func setPatchSize(_ value: Int) { patchSize = clampedOdd(value, min: 3, max: 15) }
    func setSearchWindow(_ value: Int) { searchWindow = clampedOdd(value, min: 7, max: 51) }
    func setH(_ value: Double) { h = max(0.01, min(1.0, value)) }

    private func clampedOdd(_ value: Int, min: Int, max: Int) -> Int {
        var v = Swift.max(min, Swift.min(max, value))
        if v % 2 == 0 { v += (v < max ? 1 : -1) }
        return v
    }
}
