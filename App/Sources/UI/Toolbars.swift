import SwiftUI
import UniformTypeIdentifiers

/// Context actions for the current selection.
struct SelectionBar: View {
    @ObservedObject var store: ModelStore

    var body: some View {
        let faces = store.selectedFaces, edges = store.selectedEdges, bodies = store.selectedBodies
        HStack(spacing: 6) {
            if store.selection.isEmpty {
                Text(hint)
                    .lineLimit(2)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .padding(.horizontal, 14)
                    .frame(minHeight: 36)
                    .background(.regularMaterial, in: Capsule())
            } else {
                Text(summary).font(.callout.weight(.medium)).padding(.horizontal, 8)
                if !faces.isEmpty {
                    if faces.count == 1 {
                        action("Push/Pull", "arrow.up.and.down.square", store.pushPullSelection)
                        action("Sketch", "pencil.and.outline", store.startSketchOnSelectedFace)
                    }
                    action("Shell", "square.dashed", store.shellSelection)
                }
                if !edges.isEmpty {
                    action("Fillet", "circle.bottomhalf.filled") { store.filletSelection(chamfer: false) }
                    action("Chamfer", "triangle.bottomhalf.filled") { store.filletSelection(chamfer: true) }
                }
                if bodies.count == 2 {
                    ForEach(BooleanKind.allCases, id: \.self) { kind in
                        action(kind.title, kind.symbol) { store.boolean(kind) }
                    }
                }
                if bodies.count == 1 {
                    action("Move", "move.3d", store.moveSelection)
                    action("Shell", "square.dashed", store.shellSelection)
                    action("Mirror", "arrow.left.and.right.righttriangle.left.righttriangle.right", store.mirrorSelection)
                    action("Duplicate", "plus.square.on.square", store.duplicateSelection)
                }
                if !bodies.isEmpty {
                    Button(role: .destructive, action: store.deleteSelection) { Image(systemName: "trash") }
                        .buttonStyle(ToolButtonStyle())
                        .keyboardShortcut(.delete, modifiers: .command)
                        .help("Delete body")
                }
                Button { store.selection = [] } label: { Image(systemName: "xmark") }
                    .buttonStyle(ToolButtonStyle())
                    .keyboardShortcut(.escape, modifiers: [])
                    .help("Clear selection")
            }
        }
        .lineLimit(1)
        .fixedSize(horizontal: !store.selection.isEmpty, vertical: false)
        .padding(6)
        .background(store.selection.isEmpty ? AnyShapeStyle(.clear) : AnyShapeStyle(.thinMaterial),
                    in: RoundedRectangle(cornerRadius: 14, style: .continuous))
        .labelStyle(.titleAndIcon)
    }

    private var hint: String {
        #if os(macOS)
        "Click a face or edge · double-click a body · drag to orbit, ⌥-drag to pan, scroll to orbit, pinch to zoom"
        #else
        "Tap a face or edge · double-tap a body · drag to orbit, two fingers to pan and zoom"
        #endif
    }

    private var summary: String {
        let f = store.selectedFaces.count, e = store.selectedEdges.count, b = store.selectedBodies.count
        var parts: [String] = []
        if b > 0 { parts.append("\(b) bod\(b == 1 ? "y" : "ies")") }
        if f > 0 { parts.append("\(f) face\(f == 1 ? "" : "s")") }
        if e > 0 { parts.append("\(e) edge\(e == 1 ? "" : "s")") }
        return parts.joined(separator: ", ")
    }

    private func action(_ title: String, _ symbol: String, _ run: @escaping () -> Void) -> some View {
        Button(action: run) { Label(title, systemImage: symbol) }
            .buttonStyle(ToolButtonStyle())
            .help(title)
    }
}

func formatted(_ value: Double) -> String {
    value.formatted(.number.precision(.fractionLength(0...3)))
}
