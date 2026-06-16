import AppKit
import Foundation
import OSLog

private let logger = Logger(subsystem: "com.denoiz.ui", category: "DenoiseService")

/// Wraps the `denoise` CLI binary via Process (subprocess).
/// Handles binary discovery, argument construction, execution, and result loading.
enum DenoiseService {

    /// Configuration for the denoise operation.
    struct Config {
        let inputURL: URL
        let params: DenoisingParameters
        let timeoutSeconds: Double

        init(inputURL: URL, params: DenoisingParameters, timeoutSeconds: Double = 300) {
            self.inputURL = inputURL
            self.params = params
            self.timeoutSeconds = timeoutSeconds
        }
    }

    /// Result of a denoising run.
    struct Result {
        let image: NSImage
        let inputSize: CGSize
        let outputSize: CGSize
        let duration: TimeInterval
        let stdout: String
        let stderr: String
    }

    // MARK: - Public API

    /// Runs denoising asynchronously on a background queue.
    /// - Parameter config: Input file and parameter configuration.
    /// - Returns: The denoised image along with metadata.
    /// - Throws: `DenoiseError` or `ImageLoaderError`.
    static func denoise(config: Config) async throws -> Result {
        return try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                do {
                    let result = try run(config: config)
                    continuation.resume(returning: result)
                } catch {
                    continuation.resume(throwing: error)
                }
            }
        }
    }

    /// Synchronous version — call only from background queue.
    static func run(config: Config) throws -> Result {
        // 1. Locate binary
        guard let binaryURL = findBinary() else {
            throw ImageLoaderError.binaryNotFound
        }

        // 2. Prepare temp file for output
        let outputURL = ImageLoader.temporaryURL(ext: "png")

        // 3. Build arguments
        var args = config.params.buildArguments(
            inputPath: config.inputURL.path,
            outputPath: outputURL.path
        )

        // 4. Launch process
        let process = Process()
        process.executableURL = binaryURL
        process.arguments = args

        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = stderrPipe

        let startTime = Date()

        do {
            try process.run()
        } catch {
            throw DenoiseError.processLaunchFailed(error)
        }

        // 5. Wait with timeout
        let deadline = DispatchTime.now() + .seconds(Int(config.timeoutSeconds))
        let semaphore = DispatchSemaphore(value: 0)

        let timeoutQueue = DispatchQueue.global()
        timeoutQueue.async {
            process.waitUntilExit()
            semaphore.signal()
        }

        let waitResult = semaphore.wait(timeout: deadline)

        if waitResult == .timedOut {
            process.terminate()
            throw DenoiseError.timeout(config.timeoutSeconds)
        }

        let duration = Date().timeIntervalSince(startTime)

        // 6. Read captured output
        let stdoutData = stdoutPipe.fileHandleForReading.readDataToEndOfFile()
        let stderrData = stderrPipe.fileHandleForReading.readDataToEndOfFile()
        let stdoutStr = String(data: stdoutData, encoding: .utf8) ?? ""
        let stderrStr = String(data: stderrData, encoding: .utf8) ?? ""

        // 7. Check exit status
        if process.terminationStatus != 0 {
            throw DenoiseError.processFailed(
                exitCode: process.terminationStatus,
                stderr: stderrStr
            )
        }

        // 8. Load result image
        let resultImage: NSImage
        do {
            resultImage = try ImageLoader.loadImage(from: outputURL)
        } catch {
            throw DenoiseError.outputLoadFailed(stderrStr.isEmpty ? error.localizedDescription : stderrStr)
        }

        // 9. Clean up temp file
        try? FileManager.default.removeItem(at: outputURL)

        let inputSize = ImageLoader.pixelSize(of: NSImage(contentsOf: config.inputURL)!) ?? .zero
        let outputSize = ImageLoader.pixelSize(of: resultImage) ?? .zero

        return Result(
            image: resultImage,
            inputSize: inputSize,
            outputSize: outputSize,
            duration: duration,
            stdout: stdoutStr,
            stderr: stderrStr
        )
    }

    // MARK: - Binary Discovery

    /// Finds the `denoise` binary.
    /// Looks first in the app bundle, then falls back to PATH.
    static func findBinary() -> URL? {
        // 1. App bundle resource
        if let bundled = Bundle.main.url(forResource: "denoise", withExtension: nil) {
            if FileManager.default.isExecutableFile(atPath: bundled.path) {
                return bundled
            }
        }

        // 2. Relative to build directory (for development)
        //    <project-root>/build/bin/denoise
        let devPath = URL(fileURLWithPath: #file)
            .deletingLastPathComponent()  // Services/
            .deletingLastPathComponent()  // DenoizUI/
            .deletingLastPathComponent()  // ui/
            .deletingLastPathComponent()  // project root
            .appendingPathComponent("build/denoise")
        if FileManager.default.isExecutableFile(atPath: devPath.path) {
            return devPath
        }

        // 3. PATH search (installed via `make install`)
        let which = Process()
        which.executableURL = URL(fileURLWithPath: "/usr/bin/which")
        which.arguments = ["denoise"]
        let pipe = Pipe()
        which.standardOutput = pipe
        which.standardError = FileHandle.nullDevice
        do {
            try which.run()
            which.waitUntilExit()
            let data = pipe.fileHandleForReading.readDataToEndOfFile()
            let path = String(data: data, encoding: .utf8)?
                .trimmingCharacters(in: .whitespacesAndNewlines)
            if let path = path, !path.isEmpty,
               FileManager.default.isExecutableFile(atPath: path) {
                return URL(fileURLWithPath: path)
            }
        } catch {}

        return nil
    }
}

// MARK: - Errors

enum DenoiseError: LocalizedError {
    case processLaunchFailed(Error)
    case processFailed(exitCode: Int32, stderr: String)
    case outputLoadFailed(String)
    case timeout(Double)

    var errorDescription: String? {
        switch self {
        case .processLaunchFailed(let error):
            return "Failed to launch denoise: \(error.localizedDescription)"
        case .processFailed(let code, let stderr):
            return "denoise exited with code \(code)\n\n\(stderr)"
        case .outputLoadFailed(let detail):
            return "Failed to load denoised output:\n\(detail)"
        case .timeout(let seconds):
            return "Denoising timed out after \(Int(seconds)) seconds"
        }
    }
}
