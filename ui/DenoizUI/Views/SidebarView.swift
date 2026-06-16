import SwiftUI

struct SidebarView: View {
    @ObservedObject var params: DenoisingParameters

    @Binding var originalImage: NSImage?
    @Binding var originalImageURL: URL?
    @Binding var denoisedImage: NSImage?
    @Binding var processingTime: TimeInterval?
    @Binding var isProcessing: Bool

    @State private var errorMessage: String?

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                // ── Image Input ──────────────────────────────────────
                VStack(alignment: .leading, spacing: 8) {
                    Label("Input Image", systemImage: "photo")
                        .font(.headline)

                    ImageDropZone(
                        image: $originalImage,
                        imageURL: $originalImageURL
                    )
                }

                // ── Pipeline ─────────────────────────────────────────
                PipelinePicker(pipeline: $params.pipeline)

                Divider()

                // ── Parameters ───────────────────────────────────────
                ParameterControls(params: params)

                Divider()

                // ── Actions ──────────────────────────────────────────
                VStack(spacing: 12) {
                    // Denoise button
                    Button(action: runDenoising) {
                        HStack(spacing: 8) {
                            if isProcessing {
                                ProgressView()
                                    .scaleEffect(0.8)
                                    .frame(width: 16, height: 16)
                            } else {
                                Image(systemName: "wand.and.stars")
                            }
                            Text(isProcessing ? "Processing…" : "Denoise")
                        }
                        .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(originalImage == nil || isProcessing)
                    .keyboardShortcut(.return, modifiers: [.command])

                    // Cancel button (only when processing)
                    if isProcessing {
                        Button("Cancel") {
                            isProcessing = false
                        }
                        .buttonStyle(.bordered)
                        .frame(maxWidth: .infinity)
                    }

                    // Save button
                    if denoisedImage != nil {
                        Button(action: openSavePanel) {
                            HStack(spacing: 6) {
                                Image(systemName: "square.and.arrow.down")
                                Text("Save Result…")
                            }
                            .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.bordered)
                        .disabled(isProcessing)
                    }

                    // Reset button
                    if denoisedImage != nil && !isProcessing {
                        Button("Reset") {
                            denoisedImage = nil
                            processingTime = nil
                        }
                        .buttonStyle(.link)
                        .font(.caption)
                        .frame(maxWidth: .infinity)
                    }
                }

                // ── Processing Info ──────────────────────────────────
                if let time = processingTime, denoisedImage != nil {
                    GroupBox {
                        VStack(alignment: .leading, spacing: 4) {
                            Label("Processing Info", systemImage: "info.circle")
                                .font(.caption)
                                .foregroundColor(.secondary)

                            Text(String(format: "Duration: %.2f s", time))
                                .font(.caption.monospacedDigit())

                            if let img = denoisedImage,
                               let size = ImageLoader.pixelSize(of: img) {
                                Text("Output: \(Int(size.width)) × \(Int(size.height)) px")
                                    .font(.caption.monospacedDigit())
                            }
                        }
                        .frame(maxWidth: .infinity, alignment: .leading)
                    }
                }

                // ── Error ────────────────────────────────────────────
                if let error = errorMessage {
                    GroupBox {
                        VStack(alignment: .leading, spacing: 4) {
                            Label("Error", systemImage: "exclamationmark.triangle")
                                .font(.caption)
                                .foregroundColor(.red)
                            Text(error)
                                .font(.caption2)
                                .foregroundColor(.secondary)
                        }
                    }
                }
            }
            .padding(20)
        }
        .frame(minWidth: 300, idealWidth: 340)
        .background(Color.primary.opacity(0.02))
    }

    // MARK: - Actions

    private func runDenoising() {
        guard let url = originalImageURL else { return }

        isProcessing = true
        errorMessage = nil
        processingTime = nil

        let config = DenoiseService.Config(
            inputURL: url,
            params: params
        )

        Task {
            do {
                let result = try await DenoiseService.denoise(config: config)
                await MainActor.run {
                    denoisedImage = result.image
                    processingTime = result.duration
                    isProcessing = false
                }
            } catch {
                await MainActor.run {
                    errorMessage = error.localizedDescription
                    isProcessing = false
                }
            }
        }
    }

    private func openSavePanel() {
        guard denoisedImage != nil else { return }
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.png]
        panel.nameFieldStringValue = "denoised.png"
        panel.canCreateDirectories = true
        panel.begin { response in
            if response == .OK, let url = panel.url {
                saveResult(to: url)
            }
        }
    }

    private func saveResult(to url: URL) {
        guard let image = denoisedImage else { return }
        do {
            try ImageLoader.saveImage(image, to: url)
        } catch {
            errorMessage = error.localizedDescription
        }
    }
}
