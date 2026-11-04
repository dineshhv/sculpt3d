import SwiftUI

/// Sketch mode controls: drawing tools and finishing on the bottom row, and
/// constraints or the selected dimension's value above it.
struct SketchBar: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController

    var body: some View {
        if let session = store.sketch {
            VStack(spacing: 8) {
                if !session.selection.isEmpty {
                    ConstraintBar(store: store, session: session)
                }
                actionRow(session)
            }
        }
    }

    /// Drawing tools. Only the active tool shows its name, so all of them fit.
    private func toolRow(_ session: SketchSession) -> some View {
        HStack(spacing: 4) {
            ForEach(SketchTool.allCases) { tool in
                Button { store.setSketchTool(tool) } label: {
                    if session.tool == tool {
                        Label(tool.title, systemImage: tool.symbol).labelStyle(.titleAndIcon)
                    } else {
                        Image(systemName: tool.symbol)
                    }
                }
                .buttonStyle(ToolButtonStyle(prominent: session.tool == tool))
                .help(tool.title)
                .accessibilityLabel(tool.title)
            }
            if session.tool == .polygon {
                Stepper("\(session.polygonSides) sides", value: Binding(get: { session.polygonSides },
                                                                   set: { store.sketch?.polygonSides = $0 }), in: 3...24)
                    .font(.caption)
                    .fixedSize()
            }
            Divider().frame(height: 24)
            Button { store.sketch?.constructionMode.toggle() } label: { Image(systemName: "square.dashed") }
                .buttonStyle(ToolButtonStyle(prominent: session.constructionMode))
                .help(session.constructionMode ? "Construction mode on" : "Construction mode off")
                .accessibilityLabel("Construction mode")
            Button { viewport.faceSketchPlane() } label: { Image(systemName: "viewfinder") }
                .buttonStyle(ToolButtonStyle())
                .help("Look at sketch plane")
                .accessibilityLabel("Look at sketch plane")
        }
        .fixedSize()
        .padding(6)
        .background(.thickMaterial, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
    }

    /// Status and the ways out of the sketch.
    private func actionRow(_ session: SketchSession) -> some View {
        HStack(spacing: 6) {
            status(session)
                .padding(.horizontal, 6)
            Button(escapeTitle(session)) { store.sketchEscape() }
                .buttonStyle(ToolButtonStyle())
                .keyboardShortcut(.escape, modifiers: [])
            if session.replacing != nil {
                Button { store.finishSketch(revolve: false) } label: { Label("Done", systemImage: "checkmark") }
                    .buttonStyle(ToolButtonStyle(prominent: true))
                    .keyboardShortcut(finishShortcut(session))
            } else {
                Button { store.finishSketchAsItem() } label: { Label("Done", systemImage: "checkmark") }
                    .buttonStyle(ToolButtonStyle())
                    .disabled(session.sketch.isEmpty)
                    .help("Keep the sketch as an item, for sweep, loft or later use")
                Button { store.finishSketch(revolve: true) } label: { Label("Revolve", systemImage: "arrow.triangle.2.circlepath") }
                    .buttonStyle(ToolButtonStyle())
                    .disabled(session.sketch.isEmpty)
                Button { store.finishSketch(revolve: false) } label: { Label("Extrude", systemImage: "square.stack.3d.up") }
                    .buttonStyle(ToolButtonStyle(prominent: true))
                    .disabled(session.sketch.isEmpty)
                    .keyboardShortcut(finishShortcut(session))
            }
        }
        .labelStyle(.titleAndIcon)
        .fixedSize()
        .padding(6)
        .background(.thickMaterial, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
    }

    /// Return finishes the sketch, except while a dimension's value is being
    /// typed, where it only commits the value.
    private func finishShortcut(_ session: SketchSession) -> KeyboardShortcut? {
        session.selectedDimension == nil ? KeyboardShortcut(.return, modifiers: []) : nil
    }

    private func escapeTitle(_ session: SketchSession) -> String {
        if session.chainEnd != nil { return "End line" }
        if !session.pending.isEmpty { return "Cancel arc" }
        if !session.splineDraft.isEmpty { return "End spline" }
        if !session.selection.isEmpty { return "Deselect" }
        return "Cancel"
    }

    @ViewBuilder
    private func status(_ session: SketchSession) -> some View {
        let profiles = session.sketch.profileCount
        VStack(alignment: .leading, spacing: 1) {
            if session.sketch.isEmpty {
                Text("Grid \(formatted(viewport.gridStep)) mm")
            } else if session.status.fullyConstrained {
                Label("Fully constrained", systemImage: "checkmark.seal.fill")
                    .foregroundStyle(.green)
            } else {
                Text("\(session.status.degreesOfFreedom) degree\(session.status.degreesOfFreedom == 1 ? "" : "s") of freedom")
            }
            Text("\(profiles) profile\(profiles == 1 ? "" : "s")")
                .foregroundStyle(.secondary)
        }
        .font(.caption)
        .labelStyle(.titleAndIcon)
        .fixedSize()
    }
}

/// Constraints that fit the selection, or the value of a selected dimension.
private struct ConstraintBar: View {
    @ObservedObject var store: ModelStore
    let session: SketchSession

    var body: some View {
        HStack(spacing: 6) {
            if let i = session.selectedDimension {
                let c = session.sketch.constraints[i]
                ParameterField(param: Parameter(id: i, label: c.kind.title, value: c.value, unit: c.unit)) {
                    store.setDimension(i, to: $0)
                }
                .frame(width: 330)
            } else {
                let kinds = session.availableConstraints
                ForEach(kinds, id: \.self) { kind in
                    Button { store.constrainSelection(kind) } label: {
                        Label(title(kind), systemImage: kind.symbol)
                    }
                    .buttonStyle(ToolButtonStyle())
                    .help(title(kind))
                }
                if kinds.isEmpty && session.selectedConstraints.isEmpty && session.selectedSplines.isEmpty && session.selectedEllipses.isEmpty {
                    Text("No constraint fits this selection")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .padding(.horizontal, 6)
                }
                let curves = !session.selectedLines.isEmpty || !session.selectedCircles.isEmpty
                    || !session.selectedSplines.isEmpty || !session.selectedEllipses.isEmpty
                if curves {
                    Button { store.offsetSketchSelection() } label: { Label("Offset", systemImage: "square.on.square") }
                        .buttonStyle(ToolButtonStyle())
                        .help("Parallel copy of the selected lines, arcs and circles")
                    NumberField(value: Binding(get: { session.offsetDistance }, set: { store.sketch?.offsetDistance = $0 }))
                }
                if session.selectedPoints.count == 1 || (session.selectedLines.count == 2 && session.selectedPoints.isEmpty) {
                    Button { store.filletSketchSelection() } label: { Label("Fillet", systemImage: "arrow.turn.up.right") }
                        .buttonStyle(ToolButtonStyle())
                        .help("Round the corner with a tangent arc")
                    NumberField(value: Binding(get: { session.filletRadius }, set: { store.sketch?.filletRadius = $0 }))
                }
                if session.selectedPoints.count == 1 && session.selection.count == 1 {
                    Button { store.disconnectSketchSelection() } label: { Label("Disconnect", systemImage: "link.badge.plus") }
                        .buttonStyle(ToolButtonStyle())
                        .help("Give each curve meeting here its own end point")
                }
                if curves {
                    Button { store.toggleConstruction() } label: { Label("Make Construction", systemImage: "line.diagonal.arrow") }
                        .buttonStyle(ToolButtonStyle())
                        .help("Construction geometry guides the sketch but makes no profile")
                }
            }
            Divider().frame(height: 24)
            Button(role: .destructive) { store.deleteSketchSelection() } label: { Image(systemName: "trash") }
                .buttonStyle(ToolButtonStyle())
                .keyboardShortcut(.delete, modifiers: .command)
                .help("Delete selection")
        }
        .labelStyle(.titleAndIcon)
        .padding(6)
        .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
    }

    private func title(_ kind: SketchConstraint.Kind) -> String {
        kind == .coincident && session.selectedCircles.count == 2 ? "Concentric" : kind.title
    }
}

/// A compact length field for inline tool values.
struct NumberField: View {
    @Binding var value: Double
    @Environment(\.lengthUnit) private var unit
    @State private var text = ""

    var body: some View {
        TextField("", text: $text)
            .textFieldStyle(.roundedBorder)
            .multilineTextAlignment(.trailing)
            .frame(width: 56)
            .onAppear { text = formatted(value / unit.scale) }
            .onSubmit {
                if let v = Double(text.replacingOccurrences(of: ",", with: ".")), v > 0 { value = v * unit.scale }
                text = formatted(value / unit.scale)
            }
    }
}

/// Constraint glyphs and dimension values pinned over the viewport. Tapping
/// one selects it, so it can be edited or deleted.
struct SketchAnnotationsView: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController

    var body: some View {
        ZStack(alignment: .topLeading) {
            Color.clear
            ForEach(viewport.sketchAnnotations()) { a in
                Button {
                    toggle(a.constraint)
                } label: {
                    Text(a.text)
                        .font(a.isDimension ? .caption.monospacedDigit().weight(.semibold) : .caption2.weight(.bold))
                        .padding(.horizontal, a.isDimension ? 6 : 4)
                        .padding(.vertical, 2)
                        .foregroundStyle(a.selected ? Color.white : Color.primary)
                        .background(a.selected ? AnyShapeStyle(Color.orange) : AnyShapeStyle(.regularMaterial),
                                    in: RoundedRectangle(cornerRadius: 5, style: .continuous))
                }
                .buttonStyle(.plain)
                .position(a.point)
            }
        }
    }

    private func toggle(_ index: Int) {
        guard index >= 0, var s = store.sketch else { return }
        let ref = SketchRef.constraint(index)
        if let i = s.selection.firstIndex(of: ref) { s.selection.remove(at: i) } else { s.selection = [ref] }
        store.sketch = s
    }
}
