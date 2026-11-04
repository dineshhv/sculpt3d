import SwiftUI
import simd

/// View ▸ Show Properties Sidebar: what's selected and how it looks.
struct PropertiesPanel: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController
    @Environment(\.lengthUnit) private var unit

    var body: some View {
        let bodies = store.touchedBodies
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                Text("Properties").font(.headline)
                if bodies.isEmpty {
                    projectSection
                } else {
                    ForEach(bodies, id: \.self) { bodySection($0) }
                }
            }
            .padding(14)
        }
    }

    private var projectSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            row("Project", store.documentName)
            row("Bodies", "\(store.bodyIds.count) (\(store.settings.hiddenBodies.count) hidden)")
            row("Features", "\(store.features.count)")
            Picker("Units", selection: $store.settings.units) {
                ForEach(LengthUnit.allCases) { Text($0.title).tag($0) }
            }
            Text("Select a body, face or edge to see its properties.").font(.caption).foregroundStyle(.secondary)
        }
    }

    private func bodySection(_ id: Int) -> some View {
        let p = store.properties(id)
        let look = store.settings.appearance[id] ?? Appearance()
        return VStack(alignment: .leading, spacing: 8) {
            Text(store.settings.name(of: id)).font(.subheadline.weight(.semibold))
            if let p {
                let size = p.bboxMax - p.bboxMin
                row("Size", "\(formatLength(size.x, unit)) × \(formatLength(size.y, unit)) × \(formatLength(size.z, unit))")
                row("Volume", formatVolume(p.volume, unit))
                row("Surface area", formatArea(p.area, unit))
                row("Faces / edges", "\(p.faceCount) / \(p.edgeCount)")
            }
            ColorPicker("Color", selection: Binding(
                get: { Color(red: Double(look.color.x), green: Double(look.color.y), blue: Double(look.color.z)) },
                set: { c in
                    var a = look
                    a.color = c.rgb
                    a.preset = nil
                    store.setAppearance(a, for: [id])
                }), supportsOpacity: false)
            HStack {
                Text("Opacity").foregroundStyle(.secondary)
                Slider(value: Binding(get: { Double(look.opacity) }, set: { v in
                    var a = look
                    a.opacity = Float(v)
                    store.setAppearance(a, for: [id])
                }), in: 0.05...1)
            }
            Toggle("Visible", isOn: Binding(get: { !store.settings.hiddenBodies.contains(id) }, set: { _ in store.toggleHidden(body: id) }))
            Divider()
        }
        .font(.callout)
    }

    private func row(_ label: String, _ value: String) -> some View {
        HStack(alignment: .firstTextBaseline) {
            Text(label).foregroundStyle(.secondary)
            Spacer()
            Text(value).multilineTextAlignment(.trailing).textSelection(.enabled).font(.callout.monospacedDigit())
        }
        .font(.callout)
    }
}

/// View ▸ Field of View…
struct FieldOfViewSheet: View {
    @ObservedObject var store: ModelStore
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Field of View").font(.headline)
            Toggle("Orthographic (no perspective)", isOn: $store.settings.camera.orthographic)
            HStack {
                Slider(value: $store.settings.camera.fieldOfView, in: 10...100, step: 1)
                Text("\(Int(store.settings.camera.fieldOfView))°").monospacedDigit().frame(width: 44, alignment: .trailing)
            }
            .disabled(store.settings.camera.orthographic)
            Text("Wide angles exaggerate perspective; narrow ones flatten it, like a telephoto lens.")
                .font(.caption).foregroundStyle(.secondary)
            HStack {
                Button("Reset") { store.settings.camera = CameraSettings() }
                Spacer()
                Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
        .frame(width: 360)
    }
}

/// Sketch ▸ Scale / Pattern / Rotate: the values for the transform.
struct SketchTransformSheet: View {
    let kind: SketchTransformKind
    @ObservedObject var store: ModelStore
    @State private var p = SketchTransformParams()
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(kind.title).font(.headline)
            Text(hint).font(.caption).foregroundStyle(.secondary)
            Form {
                switch kind {
                case .scale:
                    number("Scale factor", $p.factor, "×")
                    Toggle("Keep original", isOn: $p.keepOriginal)
                case .rotate:
                    number("Angle", $p.angle, "°")
                    Toggle("Keep original", isOn: $p.keepOriginal)
                case .linearPattern:
                    Stepper("Instances: \(p.count)", value: $p.count, in: 2...200)
                    number("Spacing", $p.spacing, "mm")
                    number("Direction", $p.direction, "°")
                case .circularPattern:
                    Stepper("Instances: \(p.count)", value: $p.count, in: 2...360)
                    number("Total angle", $p.totalAngle, "°")
                }
            }
            .formStyle(.grouped)
            HStack {
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Spacer()
                Button("Apply") {
                    store.transformSketchSelection(kind, p)
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
        .frame(width: 380)
    }

    private var hint: String {
        switch kind {
        case .scale: return "Scales the selected geometry about a selected point, or its centre."
        case .rotate: return "Turns the selected geometry about a selected point, or the sketch origin."
        case .linearPattern: return "Repeats the selected geometry along a direction (0° is the sketch's X axis)."
        case .circularPattern: return "Repeats the selected geometry around a selected point, or the sketch origin."
        }
    }

    private func number(_ label: String, _ value: Binding<Double>, _ unit: String) -> some View {
        HStack {
            Text(label)
            Spacer()
            TextField("", value: value, format: .number).labelsHidden().multilineTextAlignment(.trailing).frame(width: 90)
            Text(unit).foregroundStyle(.secondary).frame(width: 30, alignment: .leading)
        }
    }
}

/// Sketch ▸ Text…
struct SketchTextSheet: View {
    @ObservedObject var store: ModelStore
    @State private var text = "Sculpt3D"
    @State private var font = "Helvetica-Bold"
    @State private var size = 20.0
    @Environment(\.dismiss) private var dismiss

    private static let fonts = ["Helvetica-Bold", "Helvetica", "Avenir-Heavy", "Avenir-Book", "Futura-Bold", "Georgia-Bold",
                                "Menlo-Bold", "Courier-Bold", "Gill Sans Bold", "Times-Bold"]

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Text").font(.headline)
            Text("Adds the text as closed outlines at the sketch origin, ready to extrude or emboss.")
                .font(.caption).foregroundStyle(.secondary)
            Form {
                TextField("Text", text: $text)
                Picker("Font", selection: $font) {
                    ForEach(Self.fonts, id: \.self) { Text($0).tag($0) }
                }
                HStack {
                    Text("Height")
                    Spacer()
                    TextField("", value: $size, format: .number).labelsHidden().multilineTextAlignment(.trailing).frame(width: 80)
                    Text("mm").foregroundStyle(.secondary)
                }
            }
            .formStyle(.grouped)
            HStack {
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Spacer()
                Button("Add Text") {
                    store.addSketchText(text, fontName: font, size: max(size, 0.1))
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(text.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }
        .padding(20)
        .frame(width: 380)
    }
}

/// The prompt shown while picking points, with a way out.
struct PointPickBanner: View {
    let session: PointPickSession
    let cancel: () -> Void

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "scope").foregroundStyle(Color.accentColor)
            VStack(alignment: .leading, spacing: 1) {
                Text(session.title).font(.callout.weight(.semibold))
                Text("\(session.prompt)  ·  \(session.points.count + 1) of \(session.needed)").font(.caption).foregroundStyle(.secondary)
            }
            Button("Cancel", action: cancel).keyboardShortcut(.cancelAction)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .background(.thickMaterial, in: Capsule())
    }
}

/// Point measurements' labels, placed over the viewport, plus pinned values.
struct MeasurementLabels: View {
    @ObservedObject var viewport: ViewportController

    var body: some View {
        GeometryReader { g in
            ForEach(viewport.pointMeasurements) { m in
                if let p = viewport.camera.project(SIMD3<Float>(m.anchor), size: g.size) {
                    HStack(spacing: 4) {
                        Text(m.label).font(.caption.monospacedDigit().weight(.semibold))
                        Button { viewport.pointMeasurements.removeAll { $0.id == m.id } } label: { Image(systemName: "xmark.circle.fill") }
                            .buttonStyle(.plain)
                            .foregroundStyle(.secondary)
                    }
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .background(.thickMaterial, in: Capsule())
                    .position(x: p.x, y: p.y - 16)
                }
            }
        }
        .allowsHitTesting(true)
    }
}
