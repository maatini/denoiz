import SwiftUI

/// Side-by-side image comparison with synchronized zoom and pan.
struct ImageComparisonView: View {
    let originalImage: NSImage?
    let denoisedImage: NSImage?
    let processingTime: TimeInterval?

    // Shared zoom/scroll state for synchronized panning.
    @State private var originalZoom: CGFloat = 1.0
    @State private var denoisedZoom: CGFloat = 1.0
    @State private var syncZoom: Bool = true

    // Internal scroll position for each view.
    @State private var originalContentOffset: CGPoint = .zero
    @State private var denoisedContentOffset: CGPoint = .zero

    var body: some View {
        GeometryReader { geometry in
            let halfWidth = max((geometry.size.width - 1) / 2, 200)

            HStack(spacing: 0) {
                // ── Original (left) ──────────────────────────────────
                ImagePanel(
                    image: originalImage,
                    label: "Original",
                    badge: nil,
                    zoom: $originalZoom,
                    contentOffset: Binding(
                        get: { originalContentOffset },
                        set: { newValue in
                            originalContentOffset = newValue
                            if syncZoom {
                                denoisedContentOffset = newValue
                            }
                        }
                    ),
                    synchronizeZoom: syncZoom ? $denoisedZoom : nil,
                    synchronizeContentOffset: syncZoom ? $denoisedContentOffset : nil,
                    width: halfWidth
                )

                // Divider
                Rectangle()
                    .fill(Color.secondary.opacity(0.3))
                    .frame(width: 1)

                // ── Denoised (right) ─────────────────────────────────
                ImagePanel(
                    image: denoisedImage,
                    label: "Denoised",
                    badge: processingTime.map { String(format: "%.1fs", $0) },
                    zoom: $denoisedZoom,
                    contentOffset: Binding(
                        get: { denoisedContentOffset },
                        set: { newValue in
                            denoisedContentOffset = newValue
                            if syncZoom {
                                originalContentOffset = newValue
                            }
                        }
                    ),
                    synchronizeZoom: syncZoom ? $originalZoom : nil,
                    synchronizeContentOffset: syncZoom ? $originalContentOffset : nil,
                    width: halfWidth
                )
            }
        }
        .toolbar {
            ToolbarItem(placement: .automatic) {
                Toggle(isOn: $syncZoom) {
                    Image(systemName: "arrow.left.arrow.right")
                }
                .help("Synchronize zoom and pan")
            }
        }
    }
}

// MARK: - Image Panel

private struct ImagePanel: View {
    let image: NSImage?
    let label: String
    let badge: String?
    @Binding var zoom: CGFloat
    @Binding var contentOffset: CGPoint
    var synchronizeZoom: Binding<CGFloat>?
    var synchronizeContentOffset: Binding<CGPoint>?
    let width: CGFloat

    private let minZoom: CGFloat = 0.25
    private let maxZoom: CGFloat = 8.0

    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Text(label)
                    .font(.headline)
                    .foregroundColor(.primary)

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

                Text(zoomText)
                    .font(.caption.monospacedDigit())
                    .foregroundColor(.secondary)
                    .frame(width: 48, alignment: .trailing)

                Button(action: { zoom = 1.0; contentOffset = .zero }) {
                    Image(systemName: "arrow.counterclockwise")
                        .font(.caption)
                }
                .buttonStyle(.borderless)
                .help("Reset zoom to 100%")
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .background(Color.primary.opacity(0.03))

            // Image or placeholder
            if let image = image {
                ZoomableImageView(
                    nsImage: image,
                    zoom: $zoom,
                    contentOffset: $contentOffset,
                    minZoom: minZoom,
                    maxZoom: maxZoom,
                    synchronizeZoom: synchronizeZoom,
                    synchronizeContentOffset: synchronizeContentOffset
                )
            } else {
                placeholderView
            }
        }
        .frame(width: width)
    }

    private var zoomText: String {
        String(format: "%.0f%%", zoom * 100)
    }

    private var placeholderView: some View {
        VStack(spacing: 8) {
            Image(systemName: "photo")
                .font(.system(size: 40))
                .foregroundColor(.secondary.opacity(0.4))
            Text("No Image")
                .font(.body)
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color.primary.opacity(0.02))
    }
}

// MARK: - Zoomable Image (NSViewRepresentable)

private struct ZoomableImageView: NSViewRepresentable {
    let nsImage: NSImage
    @Binding var zoom: CGFloat
    @Binding var contentOffset: CGPoint
    let minZoom: CGFloat
    let maxZoom: CGFloat
    var synchronizeZoom: Binding<CGFloat>?
    var synchronizeContentOffset: Binding<CGPoint>?

    func makeCoordinator() -> Coordinator {
        Coordinator(self)
    }

    func makeNSView(context: Context) -> NSView {
        let container = NSView(frame: .zero)
        container.wantsLayer = true

        // Scroll view
        let scrollView = NSScrollView(frame: .zero)
        scrollView.hasVerticalScroller = true
        scrollView.hasHorizontalScroller = true
        scrollView.autohidesScrollers = true
        scrollView.borderType = .noBorder
        scrollView.drawsBackground = false
        scrollView.allowsMagnification = true
        scrollView.minMagnification = minZoom
        scrollView.maxMagnification = maxZoom
        scrollView.translatesAutoresizingMaskIntoConstraints = false

        // Image view
        let imageView = NSImageView(frame: .zero)
        imageView.image = nsImage
        imageView.imageScaling = .scaleNone
        imageView.translatesAutoresizingMaskIntoConstraints = false

        // Document view
        let docView = NSView(frame: NSRect(x: 0, y: 0, width: Int(nsImage.size.width), height: Int(nsImage.size.height)))
        docView.translatesAutoresizingMaskIntoConstraints = false
        docView.addSubview(imageView)
        NSLayoutConstraint.activate([
            imageView.leadingAnchor.constraint(equalTo: docView.leadingAnchor),
            imageView.topAnchor.constraint(equalTo: docView.topAnchor),
            imageView.widthAnchor.constraint(equalToConstant: nsImage.size.width),
            imageView.heightAnchor.constraint(equalToConstant: nsImage.size.height),
        ])

        scrollView.documentView = docView
        container.addSubview(scrollView)

        NSLayoutConstraint.activate([
            scrollView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            scrollView.topAnchor.constraint(equalTo: container.topAnchor),
            scrollView.bottomAnchor.constraint(equalTo: container.bottomAnchor),
        ])

        context.coordinator.scrollView = scrollView
        context.coordinator.container = container

        // Listen for magnification changes
        NotificationCenter.default.addObserver(
            context.coordinator,
            selector: #selector(Coordinator.magnificationChanged(_:)),
            name: NSScrollView.didEndLiveMagnifyNotification,
            object: scrollView
        )

        // Listen for scroll changes
        NotificationCenter.default.addObserver(
            context.coordinator,
            selector: #selector(Coordinator.boundsChanged(_:)),
            name: NSView.boundsDidChangeNotification,
            object: scrollView.contentView
        )

        return container
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        context.coordinator.parent = self

        // Update image if changed
        if context.coordinator.currentImage !== nsImage {
            context.coordinator.currentImage = nsImage
            if let imageView = nsView.firstImageView() {
                imageView.image = nsImage
                // Update document view size
                if let docView = imageView.superview {
                    docView.setFrameSize(nsImage.size)
                }
            }
        }

        // Update zoom if changed externally (from sync)
        if let scrollView = context.coordinator.scrollView,
           abs(scrollView.magnification - zoom) > 0.001 {
            scrollView.animator().magnification = zoom
        }
    }

    // MARK: - Coordinator

    class Coordinator: NSObject {
        var parent: ZoomableImageView
        weak var scrollView: NSScrollView?
        weak var container: NSView?
        var currentImage: NSImage?

        init(_ parent: ZoomableImageView) {
            self.parent = parent
            self.currentImage = parent.nsImage
        }

        @objc func magnificationChanged(_ notification: Notification) {
            guard let scrollView = scrollView else { return }
            let newZoom = scrollView.magnification

            DispatchQueue.main.async {
                self.parent.zoom = newZoom
                // Propagate to synchronized view
                self.parent.synchronizeZoom?.wrappedValue = newZoom
            }
        }

        @objc func boundsChanged(_ notification: Notification) {
            guard let scrollView = scrollView else { return }
            let offset = scrollView.contentView.bounds.origin

            DispatchQueue.main.async {
                self.parent.contentOffset = offset
                // Propagate to synchronized view
                if let syncOffset = self.parent.synchronizeContentOffset {
                    let currentSyncOffset = syncOffset.wrappedValue
                    let dx = abs(currentSyncOffset.x - offset.x)
                    let dy = abs(currentSyncOffset.y - offset.y)
                    if dx > 2 || dy > 2 {
                        syncOffset.wrappedValue = offset
                    }
                }
            }
        }
    }
}

// MARK: - Helpers

private extension NSView {
    func firstImageView() -> NSImageView? {
        for subview in subviews {
            if let iv = subview as? NSImageView { return iv }
            if let sv = (subview as? NSScrollView)?.documentView?.subviews.first as? NSImageView {
                return sv
            }
        }
        // Walk through scroll view
        if let sv = (self as? NSScrollView)?.documentView {
            for subview in sv.subviews {
                if let iv = subview as? NSImageView { return iv }
            }
        }
        return nil
    }
}
