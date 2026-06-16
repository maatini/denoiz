import SwiftUI

struct PipelinePicker: View {
    @Binding var pipeline: PipelineMode

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Label("Pipeline", systemImage: "gearshape.2")
                .font(.headline)

            Picker("Pipeline", selection: $pipeline) {
                ForEach(PipelineMode.allCases) { mode in
                    Text(mode.rawValue).tag(mode)
                }
            }
            .pickerStyle(.radioGroup)
            .labelsHidden()

            // Description of selected pipeline
            Text(pipeline.description)
                .font(.caption)
                .foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.top, 2)
        }
    }
}
