import XCTest
@testable import DenoizUI

final class ImageLoaderTests: XCTestCase {

    // MARK: - Temporary URLs

    func testTemporaryURLIsUnique() {
        let url1 = ImageLoader.temporaryURL(ext: "png")
        let url2 = ImageLoader.temporaryURL(ext: "png")
        XCTAssertNotEqual(url1, url2, "Temporary URLs should be unique")
        XCTAssertTrue(url1.path.hasSuffix(".png"))
        XCTAssertTrue(url2.path.hasSuffix(".png"))
    }

    func testTemporaryURLInTempDirectory() {
        let url = ImageLoader.temporaryURL(ext: "jpg")
        XCTAssertTrue(url.path.contains(NSTemporaryDirectory().trimmingCharacters(in: CharacterSet(charactersIn: "/"))))
        XCTAssertTrue(url.path.hasSuffix(".jpg"))
    }

    // MARK: - Write temp Data

    func testWriteTempData() throws {
        let testData = "test content".data(using: .utf8)!
        let url = try ImageLoader.writeTemp(data: testData, ext: "txt")
        XCTAssertTrue(FileManager.default.fileExists(atPath: url.path))

        let readBack = try Data(contentsOf: url)
        XCTAssertEqual(readBack, testData)

        // Cleanup
        try? FileManager.default.removeItem(at: url)
    }

    // MARK: - Load image

    func testLoadImageInvalidPath() {
        let badURL = URL(fileURLWithPath: "/nonexistent/image.png")
        XCTAssertThrowsError(try ImageLoader.loadImage(from: badURL)) { error in
            XCTAssertTrue(error is ImageLoaderError)
        }
    }

    // MARK: - Pixel Size

    func testPixelSizeWithNoRepresentations() {
        let emptyImage = NSImage(size: NSSize(width: 100, height: 100))
        // NSImage created with size only has no representations initially
        let size = ImageLoader.pixelSize(of: emptyImage)
        // Without representations, pixelSize returns nil
        XCTAssertNil(size)
    }

    // MARK: - Save image

    func testSaveImageToFile() throws {
        // Create a simple 10x10 RGB image
        let image = NSImage(size: NSSize(width: 10, height: 10))
        let url = ImageLoader.temporaryURL(ext: "png")

        // NSImage created with size only may not have a representation
        // that can be encoded as PNG. This tests the error path.
        XCTAssertThrowsError(try ImageLoader.saveImage(image, to: url))
    }
}
