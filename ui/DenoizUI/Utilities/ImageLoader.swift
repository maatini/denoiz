import AppKit
import Foundation
import UniformTypeIdentifiers

/// Utilities for loading, saving, and creating temporary image files.
enum ImageLoader {

    // MARK: - Temporary files

    /// Returns a unique temporary file URL with the given path extension.
    static func temporaryURL(ext: String = "png") -> URL {
        let dir = FileManager.default.temporaryDirectory
        let name = "denoizui_\(UUID().uuidString).\(ext)"
        return dir.appendingPathComponent(name)
    }

    /// Writes Data to a temporary file and returns its URL.
    static func writeTemp(data: Data, ext: String = "png") throws -> URL {
        let url = temporaryURL(ext: ext)
        try data.write(to: url)
        return url
    }

    /// Writes an NSImage as PNG to a temporary file and returns its URL.
    /// Converts RGBA → RGB if necessary (stb_image requires 1 or 3 channels).
    static func writeTempPNG(image: NSImage) throws -> URL {
        guard let tiff = image.tiffRepresentation,
              let bitmap = NSBitmapImageRep(data: tiff),
              let pngData = bitmap.representation(using: .png, properties: [:]) else {
            throw ImageLoaderError.imageEncodingFailed
        }
        let url = temporaryURL(ext: "png")
        try pngData.write(to: url)
        return url
    }

    // MARK: - Loading

    /// Loads an NSImage from a file URL.
    static func loadImage(from url: URL) throws -> NSImage {
        guard let image = NSImage(contentsOf: url) else {
            throw ImageLoaderError.invalidImage(url.path)
        }
        // NSImage loaded from file may not have a pixel representation yet.
        // Ensure it has at least one representation so dimensions are available.
        if image.representations.isEmpty {
            throw ImageLoaderError.invalidImage(url.path)
        }
        return image
    }

    // MARK: - Saving

    /// Saves an NSImage as PNG to the given URL.
    static func saveImage(_ image: NSImage, to url: URL) throws {
        guard let tiff = image.tiffRepresentation,
              let bitmap = NSBitmapImageRep(data: tiff),
              let pngData = bitmap.representation(using: .png, properties: [:]) else {
            throw ImageLoaderError.imageEncodingFailed
        }
        try pngData.write(to: url)
    }

    // MARK: - Info

    /// Returns the pixel dimensions of an NSImage.
    static func pixelSize(of image: NSImage) -> CGSize? {
        guard let rep = image.representations.first else { return nil }
        return CGSize(width: rep.pixelsWide, height: rep.pixelsHigh)
    }
}

// MARK: - Errors

enum ImageLoaderError: LocalizedError {
    case invalidImage(String)
    case imageEncodingFailed
    case binaryNotFound

    var errorDescription: String? {
        switch self {
        case .invalidImage(let path):
            return "Could not load image at \(path)"
        case .imageEncodingFailed:
            return "Failed to encode image to PNG"
        case .binaryNotFound:
            return "denoise binary not found.\nBuild the project with CMake first: cmake --build build"
        }
    }
}
