import SwiftUI

/// Floating card that edits the parameters of the operation being drafted.
/// Every change regenerates the model so the viewport previews it live.
struct OperationPanel: View {
    @ObservedObject var store: ModelStore

    var body: some View {
        if let draft = store.draft {
            let op = draft.feature.op
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    Image(systemName: draft.feature.symbol)
                    Text(draft.replacing == nil ? draft.feature.title : "Edit \(draft.feature.title)")
                        .font(.headline)
                    Spacer()
                    if let id = draft.replacing, op.sketch != nil {
                        Button { store.editSketch(id) } label: { Label("Edit Sketch", systemImage: "pencil.and.outline") }
                            .controlSize(.small)
                    }
                }

                ForEach(op.parameters) { param in
                    ParameterField(param: param) { value in
                        store.updateDraft(op.setting(param.id, to: value))
                    }
                }

                ForEach(op.choices) { choice in
                    Picker(choice.label, selection: Binding(get: { choice.selected }, set: {
                        store.updateDraft(op.setting(choice: choice.id, to: $0))
                    })) {
                        ForEach(choice.options.indices, id: \.self) { Text(choice.options[$0]).tag($0) }
                    }
                    .pickerStyle(.segmented)
                }

                if let mode = op.combineMode, op.sketchTarget != nil {
                    Picker("Result", selection: Binding(get: { mode }, set: {
                        store.updateDraft(op.setting(mode: $0, target: op.sketchTarget))
                    })) {
                        ForEach(CombineMode.allCases, id: \.self) { Text($0.title).tag($0) }
                    }
                    .pickerStyle(.segmented)
                }

                if case let .revolve(sketch, angle, aroundV, mode, target) = op {
                    Picker("Axis", selection: Binding(get: { aroundV }, set: {
                        store.updateDraft(.revolve(sketch: sketch, angle: angle, aroundV: $0, mode: mode, target: target))
                    })) {
                        Text("Vertical").tag(true)
                        Text("Horizontal").tag(false)
                    }
                    .pickerStyle(.segmented)
                }

                if let error = store.draftError {
                    Label(error, systemImage: "exclamationmark.triangle.fill")
                        .font(.caption)
                        .foregroundStyle(.orange)
                        .fixedSize(horizontal: false, vertical: true)
                }

                HStack {
                    Button("Cancel", role: .cancel) { store.cancelDraft() }
                        .keyboardShortcut(.escape, modifiers: [])
                    Spacer()
                    Button("Apply") { store.applyDraft() }
                        .buttonStyle(.borderedProminent)
                        .disabled(store.draftError != nil)
                        .keyboardShortcut(.return, modifiers: [])
                }
            }
            .padding(14)
            .frame(width: 300)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
            .shadow(color: .black.opacity(0.12), radius: 12, y: 4)
        }
    }
}

/// A numeric field with ± steppers, suited to touch and mouse. Lengths are
/// shown and typed in the project's unit and stored in millimetres.
struct ParameterField: View {
    let param: Parameter
    let onChange: (Double) -> Void
    @Environment(\.lengthUnit) private var lengthUnit
    @State private var text = ""
    @FocusState private var focused: Bool

    private var isLength: Bool { param.unit == "mm" }
    private var scale: Double { isLength ? lengthUnit.scale : 1 }
    private var shown: Double { param.value / scale }
    private var unitLabel: String { isLength ? lengthUnit.symbol : param.unit }

    var body: some View {
        HStack(spacing: 8) {
            Text(param.label)
                .font(.subheadline)
                .frame(width: 92, alignment: .leading)
            Button { onChange((shown - step) * scale) } label: { Image(systemName: "minus") }
                .buttonStyle(.bordered)
            TextField(param.label, text: $text)
                .textFieldStyle(.roundedBorder)
                .multilineTextAlignment(.trailing)
                .focused($focused)
                #if os(iOS)
                .keyboardType(.numbersAndPunctuation)
                #endif
                .onSubmit(commit)
                .onChange(of: focused) { _, isFocused in if !isFocused { commit() } }
            Text(unitLabel).font(.caption).foregroundStyle(.secondary).frame(width: 22, alignment: .leading)
            Button { onChange((shown + step) * scale) } label: { Image(systemName: "plus") }
                .buttonStyle(.bordered)
        }
        .onAppear { text = formatted(shown) }
        .onChange(of: param.value) { _, _ in if !focused { text = formatted(shown) } }
        .onChange(of: lengthUnit) { _, _ in text = formatted(shown) }
    }

    private var step: Double {
        if param.unit == "°" { return 15 }
        if param.unit == "×" { return 0.5 }
        if param.unit.isEmpty { return 1 }
        return abs(shown) >= 20 ? 5 : 1
    }

    private func commit() {
        let cleaned = text.replacingOccurrences(of: ",", with: ".")
        if let v = Double(cleaned) { onChange(v * scale) } else { text = formatted(shown) }
    }
}
