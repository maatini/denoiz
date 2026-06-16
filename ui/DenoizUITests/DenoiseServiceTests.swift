import XCTest
@testable import DenoizUI

@MainActor
final class DenoiseServiceTests: XCTestCase {

    var testImageURL: URL?

    override func setUp() {
        super.setUp()
        // Create a small test image that works in all test runner processes
        testImageURL = createTestImage()
    }

    override func tearDown() {
        if let url = testImageURL {
            try? FileManager.default.removeItem(at: url)
        }
        testImageURL = nil
        super.tearDown()
    }

    // MARK: - Binary Discovery

    func testFindBinary() {
        let url = DenoiseService.findBinary()
        XCTAssertNotNil(url, "denoise binary should be discoverable")
        if let url = url {
            XCTAssertTrue(FileManager.default.isExecutableFile(atPath: url.path))
        }
    }

    // MARK: - End-to-End Process Execution

    func testDenoiseEndToEnd() throws {
        let inputURL = try XCTUnwrap(testImageURL, "Test image creation failed")
        let params = DenoisingParameters()
        params.setPatchSize(7)
        params.setSearchWindow(21)
        params.setH(0.1)

        let config = DenoiseService.Config(inputURL: inputURL, params: params, timeoutSeconds: 60)
        let result = try DenoiseService.run(config: config)

        XCTAssertFalse(result.image.representations.isEmpty)
        XCTAssertGreaterThan(result.duration, 0)
        XCTAssertEqual(result.outputSize.width, 64)
        XCTAssertEqual(result.outputSize.height, 64)
    }

    func testDenoiseWithEachPipeline() throws {
        let inputURL = try XCTUnwrap(testImageURL, "Test image creation failed")

        for pipeline in PipelineMode.allCases {
            let params = DenoisingParameters()
            params.pipeline = pipeline
            let config = DenoiseService.Config(inputURL: inputURL, params: params, timeoutSeconds: 60)
            do {
                let result = try DenoiseService.run(config: config)
                XCTAssertFalse(result.image.representations.isEmpty,
                               "Pipeline \(pipeline.rawValue) should produce valid output")
            } catch {
                XCTFail("Pipeline \(pipeline.rawValue) failed: \(error)")
            }
        }
    }

    func testDenoiseWithCustomParams() throws {
        let inputURL = try XCTUnwrap(testImageURL, "Test image creation failed")
        let params = DenoisingParameters()
        params.setPatchSize(5)
        params.setSearchWindow(15)
        params.setH(0.3)

        let config = DenoiseService.Config(inputURL: inputURL, params: params)
        let result = try DenoiseService.run(config: config)
        XCTAssertFalse(result.image.representations.isEmpty)
    }

    // MARK: - Error Handling

    func testDenoiseWithInvalidInput() {
        let badURL = URL(fileURLWithPath: "/nonexistent/image.png")
        let params = DenoisingParameters()
        let config = DenoiseService.Config(inputURL: badURL, params: params)
        XCTAssertThrowsError(try DenoiseService.run(config: config))
    }

    // MARK: - Helpers

    /// Creates a small 64x64 RGB test image and writes it to a temp PNG file.
    private func createTestImage() -> URL? {
        let width = 64
        let height = 64
        let bytesPerPixel = 4  // RGBA
        var data = Data(count: width * height * bytesPerPixel)

        // Fill with simple gradient pattern + some noise
        for y in 0..<height {
            for x in 0..<width {
                let offset = (y * width + x) * bytesPerPixel
                let r = UInt8((x * 4) % 256)
                let g = UInt8((y * 4) % 256)
                let b = UInt8(((x + y) * 2) % 256)
                let a = UInt8(255)
                data[offset] = r
                data[offset + 1] = g
                data[offset + 2] = b
                data[offset + 3] = a
            }
        }

        // Create CGImage
        guard let provider = CGDataProvider(data: data as CFData),
              let colorSpace = CGColorSpace(name: CGColorSpace.sRGB),
              let cgImage = CGImage(
                width: width, height: height,
                bitsPerComponent: 8, bitsPerPixel: 32,
                bytesPerRow: width * bytesPerPixel,
                space: colorSpace,
                bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                provider: provider,
                decode: nil,
                shouldInterpolate: false,
                intent: .defaultIntent
              ) else {
            return nil
        }

        let image = NSImage(cgImage: cgImage, size: NSSize(width: width, height: height))

        // Write as PNG to temp file
        guard let tiff = image.tiffRepresentation,
              let bitmap = NSBitmapImageRep(data: tiff),
              let png = bitmap.representation(using: .png, properties: [:]) else {
            return nil
        }

        let url = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("denoiz_test_image.png")
        do {
            try png.write(to: url)
            return url
        } catch {
            return nil
        }
    }
}
