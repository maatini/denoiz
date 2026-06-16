import SwiftUI

@main
struct DenoizUIApp: App {
    @StateObject private var params = DenoisingParameters()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(params)
                .frame(minWidth: 1200, minHeight: 800)
        }
        .windowResizability(.contentSize)
        .windowToolbarStyle(.unified)
        .commands {
            // Keep standard File > Open etc.
            CommandGroup(replacing: .newItem) {}
        }
    }
}
