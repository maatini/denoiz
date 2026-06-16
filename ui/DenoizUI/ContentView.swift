import SwiftUI

struct ContentView: View {
    @EnvironmentObject var params: DenoisingParameters

    @State private var originalImage: NSImage?
    @State private var originalImageURL: URL?
    @State private var denoisedImage: NSImage?
    @State private var processingTime: TimeInterval?
    @State private var isProcessing: Bool = false

    var body: some View {
        NavigationSplitView {
            SidebarView(
                params: params,
                originalImage: $originalImage,
                originalImageURL: $originalImageURL,
                denoisedImage: $denoisedImage,
                processingTime: $processingTime,
                isProcessing: $isProcessing
            )
            .navigationSplitViewColumnWidth(min: 300, ideal: 340, max: 420)
        } detail: {
            detailView
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Color(nsColor: .windowBackgroundColor))
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    @ViewBuilder
    private var detailView: some View {
        if originalImage != nil || denoisedImage != nil {
            ImageComparisonView(
                originalImage: originalImage,
                denoisedImage: denoisedImage,
                processingTime: processingTime
            )
        } else {
            VStack(spacing: 16) {
                Image(systemName: "photo.on.rectangle.angled")
                    .font(.system(size: 56))
                    .foregroundColor(.secondary.opacity(0.5))

                Text("Drop an image to begin")
                    .font(.title2)
                    .foregroundColor(.secondary)

                Text("Drag an image into the sidebar or click \"Choose File\"")
                    .font(.body)
                    .foregroundColor(.secondary.opacity(0.7))
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
}

// MARK: - Preview

#Preview {
    ContentView()
        .environmentObject(DenoisingParameters())
}