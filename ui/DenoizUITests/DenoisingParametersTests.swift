import XCTest
@testable import DenoizUI

@MainActor
final class DenoisingParametersTests: XCTestCase {

    var params: DenoisingParameters!

    override func setUp() {
        super.setUp()
        params = DenoisingParameters()
    }

    override func tearDown() {
        params = nil
        super.tearDown()
    }

    // MARK: - Defaults

    func testDefaultValues() {
        XCTAssertEqual(params.patchSize, 7)
        XCTAssertEqual(params.searchWindow, 21)
        XCTAssertEqual(params.h, 0.1, accuracy: 0.001)
        XCTAssertEqual(params.pipeline, .neon)
        XCTAssertEqual(params.threadCount, 0)
        XCTAssertFalse(params.verbose)
    }

    // MARK: - Pipeline Mode

    func testPipelineFlags() {
        params.pipeline = .neon; XCTAssertFalse(params.useGPU)
        params.pipeline = .gpu; XCTAssertTrue(params.useGPU)
        params.pipeline = .fast; XCTAssertTrue(params.useFast)
        params.pipeline = .wavelet; XCTAssertTrue(params.useWavelet)
        params.pipeline = .adaptive; XCTAssertTrue(params.useAdaptive)
        params.pipeline = .ensemble; XCTAssertTrue(params.useEnsemble)
        params.pipeline = .coarseToFine; XCTAssertTrue(params.useCoarseToFine)
    }

    func testAllPipelineModesPresent() {
        XCTAssertEqual(PipelineMode.allCases.count, 7)
    }

    // MARK: - Argument Building (also validates clamping via buildArguments)

    func testBuildArgumentsDefault() {
        let args = params.buildArguments(inputPath: "/tmp/in.png", outputPath: "/tmp/out.png")
        XCTAssertEqual(args[0], "/tmp/in.png")
        XCTAssertEqual(args[1], "/tmp/out.png")
        XCTAssertTrue(args.contains("--patch-size"))
        XCTAssertTrue(args.contains("--h"))
    }

    func testBuildArgumentsWithPipeline() {
        params.pipeline = .gpu
        let args = params.buildArguments(inputPath: "in", outputPath: "out")
        XCTAssertTrue(args.contains("--use-gpu"))
        params.pipeline = .wavelet
        let args2 = params.buildArguments(inputPath: "in", outputPath: "out")
        XCTAssertTrue(args2.contains("--wavelet"))
    }

    func testBuildArgumentsOnlyOnePipelineFlag() {
        let allFlags = ["--use-gpu", "--fast", "--wavelet", "--adaptive", "--ensemble", "--coarse-to-fine"]
        for mode in PipelineMode.allCases {
            params.pipeline = mode
            let args = params.buildArguments(inputPath: "in", outputPath: "out")
            let active = allFlags.filter { args.contains($0) }
            if mode == .neon { XCTAssertEqual(active.count, 0) }
            else { XCTAssertEqual(active.count, 1) }
        }
    }

    func testBuildArgumentsWithVerbose() {
        params.verbose = true
        let args = params.buildArguments(inputPath: "in", outputPath: "out")
        XCTAssertTrue(args.contains("--verbose"))
    }

    // MARK: - Parameter ranges in build output

    func testPatchSizeInArgs() {
        params.setPatchSize(5)
        let args = params.buildArguments(inputPath: "i", outputPath: "o")
        let idx = args.firstIndex(of: "--patch-size")!
        XCTAssertEqual(args[idx + 1], "5")
    }

    func testSearchWindowInArgs() {
        params.setSearchWindow(15)
        let args = params.buildArguments(inputPath: "i", outputPath: "o")
        let idx = args.firstIndex(of: "--search-window")!
        XCTAssertEqual(args[idx + 1], "15")
    }

    func testHInArgs() {
        params.setH(0.3)
        let args = params.buildArguments(inputPath: "i", outputPath: "o")
        let idx = args.firstIndex(of: "--h")!
        XCTAssertEqual(args[idx + 1], "0.3000")
    }
}
