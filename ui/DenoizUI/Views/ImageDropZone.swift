import SwiftUI
import UniformTypeIdentifiers

struct ImageDropZone: View {
    @Binding var image: NSImage?
    @Binding var imageURL: URL?
    @State private var isTargeted: Bool = false

    var body: some View {
        VStack(spacing: 12) {
            if let image = image {
                // Thumbnail + info
                VStack(spacing: 8) {
                    Image(nsImage: image)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .frame(maxHeight: 160)
                        .cornerRadius(8)
                        .shadow(radius: 2)

                    if let url = imageURL {
                        Text(url.lastPathComponent)
                            .font(.caption)
                            .foregroundColor(.secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)

                        if let size = ImageLoader.pixelSize(of: image) {
                            Text("\(Int(size.width)) × \(Int(size.height)) px")
                                .font(.caption2)
                                .foregroundColor(.secondary)
                        }
                    }

                    Button("Choose Different Image…") {
                        openFilePicker()
                    }
                    .buttonStyle(.link)
                    .font(.caption)
                }
            } else {
                // Drop zone placeholder
                VStack(spacing: 12) {
                    Image(systemName: "photo.badge.plus")
                        .font(.system(size: 36))
                        .foregroundColor(isTargeted ? .accentColor : .secondary)

                    Text(isTargeted ? "Drop Image Here" : "Drag & Drop an Image")
                        .font(.headline)
                        .foregroundColor(isTargeted ? .accentColor : .primary)

                    Text("or")
                        .font(.caption)
                        .foregroundColor(.secondary)

                    Button("Choose File…") {
                        openFilePicker()
                    }
                    .buttonStyle(.bordered)

                    Text("PNG, JPEG, TIFF, BMP")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                }
                .padding(24)
                .frame(maxWidth: .infinity)
            }
        }
        .padding(16)
        .background(
            RoundedRectangle(cornerRadius: 12)
                .strokeBorder(
                    isTargeted ? Color.accentColor : Color.secondary.opacity(0.3),
                    style: StrokeStyle(lineWidth: 2, dash: image == nil ? [6, 4] : [])
                )
                .background(
                    RoundedRectangle(cornerRadius: 12)
                        .fill(isTargeted ? Color.accentColor.opacity(0.08) : Color.clear)
                )
        )
        .onDrop(of: [.fileURL], isTargeted: $isTargeted) { providers in
            handleDrop(providers: providers)
        }
        .animation(.easeInOut(duration: 0.15), value: isTargeted)
    }

    // MARK: - File Picker

    private func openFilePicker() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.png, .jpeg, .tiff, .bmp]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false

        if panel.runModal() == .OK, let url = panel.url {
            loadImage(from: url)
        }
    }

    // MARK: - Drag & Drop

    private func handleDrop(providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first else { return false }

        let supportedTypes: [UTType] = [.png, .jpeg, .tiff, .bmp]
        for type in supportedTypes {
            if provider.hasItemConformingToTypeIdentifier(type.identifier) {
                provider.loadItem(forTypeIdentifier: type.identifier, options: nil) { item, error in
                    guard error == nil else { return }
                    if let urlData = item as? Data,
                       let url = URL(dataRepresentation: urlData, relativeTo: nil) {
                        DispatchQueue.main.async {
                            loadImage(from: url)
                        }
                    } else if let url = item as? URL {
                        DispatchQueue.main.async {
                            loadImage(from: url)
                        }
                    }
                }
                return true
            }
        }
        return false
    }

    private func loadImage(from url: URL) {
        do {
            let img = try ImageLoader.loadImage(from: url)
            image = img
            imageURL = url
        } catch {
            NSAlert(error: error).runModal()
        }
    }
}
