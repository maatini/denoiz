import SwiftUI

struct ParameterControls: View {
    @ObservedObject var params: DenoisingParameters

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Label("Parameters", systemImage: "slider.horizontal.3")
                .font(.headline)

            // ── Patch Size ────────────────────────────────────────────
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text("Patch Size")
                        .font(.caption)
                    Spacer()
                    Text("\(params.patchSize) px")
                        .font(.caption.monospacedDigit())
                        .foregroundColor(.secondary)
                }
                Slider(
                    value: Binding<Double>(
                        get: { Double(params.patchSize) },
                        set: { params.patchSize = Int($0) }
                    ),
                    in: 3...15,
                    step: 2  // only odd values
                )
            }

            // ── Search Window ─────────────────────────────────────────
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text("Search Window")
                        .font(.caption)
                    Spacer()
                    Text("\(params.searchWindow) px")
                        .font(.caption.monospacedDigit())
                        .foregroundColor(.secondary)
                }
                Slider(
                    value: Binding<Double>(
                        get: { Double(params.searchWindow) },
                        set: { params.searchWindow = Int($0) }
                    ),
                    in: 7...51,
                    step: 2  // only odd values
                )
            }

            // ── Filter Strength (h) ───────────────────────────────────
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text("Filter Strength (h)")
                        .font(.caption)
                    Spacer()
                    Text(String(format: "%.3f", params.h))
                        .font(.caption.monospacedDigit())
                        .foregroundColor(.secondary)
                }
                Slider(value: $params.h, in: 0.01...1.0)
            }

            Divider()

            // ── Threads ───────────────────────────────────────────────
            HStack {
                Text("Threads")
                    .font(.caption)
                Spacer()
                Picker("", selection: $params.threadCount) {
                    Text("Auto").tag(0)
                    Text("1").tag(1)
                    Text("2").tag(2)
                    Text("4").tag(4)
                    Text("8").tag(8)
                }
                .pickerStyle(.menu)
                .frame(width: 80)
            }

            // ── Verbose ───────────────────────────────────────────────
            Toggle("Verbose Output", isOn: $params.verbose)
                .font(.caption)
        }
    }
}
