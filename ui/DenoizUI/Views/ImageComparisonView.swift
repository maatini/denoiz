import AppKit
import SwiftUI

/// Side-by-side image comparison.
struct ImageComparisonView: View {
    let originalImage: NSImage?
    let denoisedImage: NSImage?
    let processingTime: TimeInterval?

    var body: some View {
        HStack(spacing: 0) {
            ImagePanel(
                image: originalImage,
                label: "Original",
                badge: nil
            )

            Rectangle()
                .fill(Color.secondary.opacity(0.3))
                .frame(width: 1)

            ImagePanel(
                image: denoisedImage,
                label: "Denoised",
                badge: processingTime.map { String(format: "%.1fs", $0) }
            )
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

// MARK: - Image Panel

private struct ImagePanel: View {
    let image: NSImage?
    let label: String
    let badge: String?

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text(label)
                    .font(.headline)

                if let badge = badge {
                    Text(badge)
                        .font(.caption.monospacedDigit())
                        .foregroundColor(.secondary)
                        .padding(.horizontal, 6)
                        .padding(.vertical, 2)
                        .background(Color.secondary.opacity(0.12))
                        .cornerRadius(4)
                }

                Spacer()

                if let image = image, let size = ImageLoader.pixelSize(of: image) {
                    Text("\(Int(size.width)) × \(Int(size.height))")
                        .font(.caption.monospacedDigit())
                        .foregroundColor(.secondary)
                }
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .background(Color.primary.opacity(0.03))

            ZStack {
                if let image = image {
                    DisplayImageView(image: image)
                } else {
                    VStack(spacing: 8) {
                        Image(systemName: "photo")
                            .font(.system(size: 40))
                            .foregroundColor(.secondary.opacity(0.4))
                        Text("No Image")
                            .font(.body)
                            .foregroundColor(.secondary)
                    }
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Color.primary.opacity(0.02))
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

// MARK: - AppKit image view (reliable display on macOS)

private struct DisplayImageView: NSViewRepresentable {
    let image: NSImage

    func makeNSView(context: Context) -> NSImageView {
        let imageView = NSImageView()
        imageView.imageScaling = .scaleProportionallyUpOrDown
        imageView.imageAlignment = .alignCenter
        imageView.setContentHuggingPriority(.defaultLow, for: .horizontal)
        imageView.setContentHuggingPriority(.defaultLow, for: .vertical)
        imageView.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        imageView.setContentCompressionResistancePriority(.defaultLow, for: .vertical)
        imageView.image = image
        return imageView
    }

    func updateNSView(_ imageView: NSImageView, context: Context) {
        imageView.image = image
    }
}