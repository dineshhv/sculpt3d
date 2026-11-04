import SwiftUI
import simd

struct CommandRow: View {
    let command: ToolCommand
    @ObservedObject var store: ModelStore
    let action: () -> Void

    var body: some View {
        let enabled = command.isEnabled(store)
        Button(action: action) {
            HStack(spacing: 10) {
                Image(systemName: command.symbol).frame(width: 22)
                VStack(alignment: .leading, spacing: 1) {
                    Text(command.title)
                    if !command.hint.isEmpty {
                        Text(command.hint).font(.caption2).foregroundStyle(.secondary)
                    }
                }
                Spacer(minLength: 0)
            }
            .padding(.vertical, 5)
            .padding(.horizontal, 6)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.45)
    }
}

// MARK: - Search

/// ⌘F palette that finds and runs any tool by name.
struct SearchPalette: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController
    @ObservedObject var ui: UIState
    @State private var query = ""
    @FocusState private var focused: Bool

    private var results: [ToolCommand] {
        let q = query.trimmingCharacters(in: .whitespaces).lowercased()
        let all = ToolCatalog.all
        guard !q.isEmpty else { return all.filter { $0.isEnabled(store) } }
        return all.filter { "\($0.title) \($0.category.title) \($0.hint)".lowercased().contains(q) }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
                TextField("Search tools", text: $query)
                    .textFieldStyle(.plain)
                    .font(.title3)
                    .focused($focused)
                    .onSubmit { if let first = results.first(where: { $0.isEnabled(store) }) { run(first) } }
            }
            Divider()
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    ForEach(results) { command in
                        HStack {
                            CommandRow(command: command, store: store) { run(command) }
                            Text(command.category.title).font(.caption).foregroundStyle(.tertiary)
                        }
                    }
                    if results.isEmpty {
                        Text("No tool matches “\(query)”").foregroundStyle(.secondary).padding()
                    }
                }
            }
            .frame(maxHeight: 360)
        }
        .padding(14)
        .frame(width: 440)
        .background(.thickMaterial, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
        .shadow(color: .black.opacity(0.2), radius: 24, y: 8)
        .onAppear { focused = true }
        #if os(macOS)
        .onExitCommand { ui.showSearch = false }
        #endif
    }

    private func run(_ command: ToolCommand) {
        ui.showSearch = false
        command.run(store, viewport, ui)
    }
}

// MARK: - Project

struct ProjectPanel: View {
    @ObservedObject var store: ModelStore

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Project").font(.headline)
            Picker("Units", selection: $store.settings.units) {
                ForEach(LengthUnit.allCases) { Text($0.symbol).tag($0) }
            }
            .pickerStyle(.segmented)
            Toggle("Snap to grid", isOn: $store.settings.snap.grid)
            let bodies = store.bodyIds.count, steps = store.features.count
            Text("\(bodies) bod\(bodies == 1 ? "y" : "ies") · \(steps) history step\(steps == 1 ? "" : "s")")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(width: 260)
    }
}

// MARK: - Items

/// Bodies, sketches and planes: select, show/hide, rename, and properties.
struct ItemsPanel: View {
    @ObservedObject var store: ModelStore
    @State private var renaming: String?
    @State private var newName = ""

    var body: some View {
        List {
            Section("Bodies") {
                if store.bodyIds.isEmpty {
                    Text("No bodies yet").font(.callout).foregroundStyle(.secondary)
                }
                ForEach(store.bodyIds, id: \.self) { id in bodyRow(id) }
            }
            Section("Sketches & Planes") {
                if store.referenceItems.isEmpty {
                    Text("Sketches kept with Done and construction planes appear here")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
                ForEach(store.referenceItems) { item in itemRow(item) }
            }
        }
        #if os(macOS)
        .listStyle(.sidebar)
        #else
        .listStyle(.insetGrouped)
        #endif
        .scrollContentBackground(.hidden)
    }

    private func bodyRow(_ id: Int) -> some View {
        let selected = store.selection.contains(.body(id))
        let hidden = store.settings.hiddenBodies.contains(id)
        let color = store.settings.appearance[id]?.color ?? Appearance().color
        return VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 8) {
                Circle().fill(Color(red: Double(color.x), green: Double(color.y), blue: Double(color.z))).frame(width: 10, height: 10)
                if renaming == "body\(id)" {
                    TextField("Name", text: $newName).onSubmit {
                        store.rename(body: id, to: newName)
                        renaming = nil
                    }
                } else {
                    Button { store.selection = selected ? [] : [.body(id)] } label: {
                        Text(store.settings.name(of: id))
                            .foregroundStyle(selected ? Color.accentColor : (hidden ? .secondary : .primary))
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .buttonStyle(.plain)
                }
                Button { store.toggleHidden(body: id) } label: { Image(systemName: hidden ? "eye.slash" : "eye") }
                    .buttonStyle(.plain)
                    .foregroundStyle(.secondary)
                    .help(hidden ? "Show" : "Hide")
            }
            if selected, let p = store.properties(id) {
                let u = store.settings.units
                let size = (p.bboxMax - p.bboxMin) / u.scale
                Group {
                    Text("\(formatted(size.x)) × \(formatted(size.y)) × \(formatted(size.z)) \(u.symbol)")
                    Text("Volume \(formatted(p.volume / pow(u.scale, 3))) \(u.symbol)³")
                    Text("Area \(formatted(p.area / pow(u.scale, 2))) \(u.symbol)² · \(p.faceCount) faces · \(p.edgeCount) edges")
                }
                .font(.caption)
                .foregroundStyle(.secondary)
            }
        }
        .contextMenu {
            Button("Rename") {
                newName = store.settings.name(of: id)
                renaming = "body\(id)"
            }
            Button(hidden ? "Show" : "Hide") { store.toggleHidden(body: id) }
        }
    }

    private func itemRow(_ item: ReferenceItem) -> some View {
        let selected = store.itemSelection.contains(item.id)
        let hidden = store.settings.hiddenItems.contains(item.id)
        return HStack(spacing: 8) {
            Image(systemName: item.isPlane ? "square.on.square.dashed" : "pencil.and.outline")
                .foregroundStyle(.secondary)
                .frame(width: 16)
            if renaming == item.id.uuidString {
                TextField("Name", text: $newName).onSubmit {
                    store.renameItem(item.id, to: newName)
                    renaming = nil
                }
            } else {
                Button { store.toggleItemSelection(item.id) } label: {
                    HStack {
                        Text(item.name).foregroundStyle(selected ? Color.accentColor : (hidden ? .secondary : .primary))
                        if selected, let n = store.itemSelection.firstIndex(of: item.id), store.itemSelection.count > 1 {
                            Text("\(n + 1)").font(.caption2.monospacedDigit()).padding(.horizontal, 5)
                                .background(Color.accentColor.opacity(0.2), in: Capsule())
                        }
                        Spacer(minLength: 0)
                    }
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }
            Button { store.toggleHidden(item: item.id) } label: { Image(systemName: hidden ? "eye.slash" : "eye") }
                .buttonStyle(.plain)
                .foregroundStyle(.secondary)
        }
        .contextMenu {
            Button(item.isPlane ? "Edit Plane" : "Edit Sketch") { store.editItem(item.id) }
            if !item.isPlane {
                Button("Extrude") { store.useSketchItem(item.id, revolve: false) }
                Button("Revolve") { store.useSketchItem(item.id, revolve: true) }
            } else {
                Button("Sketch on Plane") { store.startSketch(on: item.plane) }
            }
            Button("Rename") {
                newName = item.name
                renaming = item.id.uuidString
            }
            Button(hidden ? "Show" : "Hide") { store.toggleHidden(item: item.id) }
            Button("Delete", role: .destructive) { store.deleteFeature(item.id) }
        }
    }
}

// MARK: - History

struct HistoryPanel: View {
    @ObservedObject var store: ModelStore

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("History").font(.headline)
                Spacer()
                // While sketching, undo steps through the sketch's own edits.
                let sketching = store.sketch != nil
                Button { sketching ? store.undoSketch() : store.undo() } label: { Image(systemName: "arrow.uturn.backward") }
                    .disabled(sketching ? !store.canUndoSketch : (!store.canUndo || store.draft != nil))
                    .keyboardShortcut("z", modifiers: .command)
                    .help("Undo")
                    .accessibilityLabel("Undo")
                Button { sketching ? store.redoSketch() : store.redo() } label: { Image(systemName: "arrow.uturn.forward") }
                    .disabled(sketching ? !store.canRedoSketch : (!store.canRedo || store.draft != nil))
                    .keyboardShortcut("z", modifiers: [.command, .shift])
                    .help("Redo")
                    .accessibilityLabel("Redo")
            }
            .buttonStyle(.plain)
            .padding([.horizontal, .top], 14)
            .padding(.bottom, 6)
            if store.features.isEmpty {
                VStack(spacing: 6) {
                    Spacer()
                    Text("Workspace is empty").font(.callout.weight(.semibold))
                    Text("Your modeling steps will show up here").font(.caption).foregroundStyle(.secondary)
                    Spacer()
                }
                .frame(maxWidth: .infinity)
            } else {
                List {
                    ForEach(Array(store.features.enumerated()), id: \.element.id) { index, feature in
                        FeatureRow(store: store, feature: feature, index: index)
                    }
                }
                #if os(macOS)
                .listStyle(.sidebar)
                #else
                .listStyle(.plain)
                #endif
                .scrollContentBackground(.hidden)
            }
        }
    }
}

struct FeatureRow: View {
    @ObservedObject var store: ModelStore
    let feature: Feature
    let index: Int

    var body: some View {
        let error = store.errors[feature.id]
        let editing = store.draft?.replacing == feature.id
        let editable = !feature.op.parameters.isEmpty || feature.op.sketch != nil
        Button {
            if editable { store.editItem(feature.id) }
        } label: {
            HStack(spacing: 8) {
                Text("\(index + 1)").font(.caption.monospacedDigit()).foregroundStyle(.tertiary).frame(width: 18)
                Image(systemName: error != nil ? "exclamationmark.triangle.fill" : feature.symbol)
                    .foregroundStyle(error != nil ? .orange : (editing ? Color.accentColor : .secondary))
                    .frame(width: 20)
                VStack(alignment: .leading, spacing: 1) {
                    Text(feature.title)
                        .strikethrough(feature.suppressed)
                        .foregroundStyle(feature.suppressed ? .secondary : .primary)
                    if let error { Text(error).font(.caption2).foregroundStyle(.orange).lineLimit(2) }
                }
                Spacer()
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .help(editable ? "Edit \(feature.title)" : feature.title)
        .contextMenu {
            if !feature.op.parameters.isEmpty { Button("Edit") { store.edit(feature.id) } }
            if feature.op.sketch != nil { Button("Edit Sketch") { store.editSketch(feature.id) } }
            Button(feature.suppressed ? "Unsuppress" : "Suppress") { store.toggleSuppressed(feature.id) }
            Button("Delete", role: .destructive) { store.deleteFeature(feature.id) }
        }
        .swipeActions {
            Button("Delete", role: .destructive) { store.deleteFeature(feature.id) }
            Button(feature.suppressed ? "Show" : "Hide") { store.toggleSuppressed(feature.id) }
        }
    }
}

// MARK: - Visualize

struct AppearancePanel: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController

    var body: some View {
        let targets = store.appearanceTargets
        let current = targets.first.flatMap { store.settings.appearance[$0] } ?? Appearance()
        VStack(alignment: .leading, spacing: 10) {
            Text(store.selection.isEmpty ? "All bodies" : "\(targets.count) selected bod\(targets.count == 1 ? "y" : "ies")")
                .font(.caption)
                .foregroundStyle(.secondary)
            LazyVGrid(columns: Array(repeating: GridItem(.fixed(24), spacing: 8), count: 6), spacing: 8) {
                ForEach(Appearance.palette.indices, id: \.self) { i in
                    let c = Appearance.palette[i]
                    Button {
                        var a = current
                        a.color = c
                        store.setAppearance(a, for: targets)
                    } label: {
                        Circle()
                            .fill(Color(red: Double(c.x), green: Double(c.y), blue: Double(c.z)))
                            .overlay(Circle().strokeBorder(current.color == c ? Color.accentColor : .black.opacity(0.15),
                                                           lineWidth: current.color == c ? 3 : 1))
                            .frame(width: 24, height: 24)
                    }
                    .buttonStyle(.plain)
                }
            }
            Picker("Finish", selection: Binding(get: { current.metallic > 0.5 ? 2 : (current.roughness > 0.7 ? 0 : 1) }, set: {
                var a = current
                (a.metallic, a.roughness) = [(0, 0.9), (0, 0.45), (1, 0.25)][$0]
                store.setAppearance(a, for: targets)
            })) {
                Text("Matte").tag(0)
                Text("Plastic").tag(1)
                Text("Metal").tag(2)
            }
            .pickerStyle(.segmented)
            .disabled(targets.isEmpty)
            Divider()
            Picker("Display", selection: $viewport.displayMode) {
                ForEach(DisplayMode.allCases) { Label($0.title, systemImage: $0.symbol).tag($0) }
            }
        }
    }
}

/// Orientation cube: shows the camera's attitude; click a face to look at it.
struct ViewCube: View {
    @ObservedObject var viewport: ViewportController

    private struct Face {
        let name: String
        let normal: SIMD3<Float>
        let yaw: Float
        let pitch: Float
    }

    private static let faces: [Face] = [
        Face(name: "Top", normal: [0, 0, 1], yaw: -.pi / 2, pitch: 1.55),
        Face(name: "Bottom", normal: [0, 0, -1], yaw: -.pi / 2, pitch: -1.55),
        Face(name: "Front", normal: [0, -1, 0], yaw: -.pi / 2, pitch: 0),
        Face(name: "Back", normal: [0, 1, 0], yaw: .pi / 2, pitch: 0),
        Face(name: "Right", normal: [1, 0, 0], yaw: 0, pitch: 0),
        Face(name: "Left", normal: [-1, 0, 0], yaw: .pi, pitch: 0),
    ]

    private let side: CGFloat = 100

    var body: some View {
        let cam = viewport.camera
        let polys = visibleFaces(cam)
        ZStack {
            Canvas { context, size in
                let center = CGPoint(x: size.width / 2, y: size.height / 2)
                for (face, pts, shade) in polys {
                    var path = Path()
                    path.addLines(pts.map { CGPoint(x: center.x + $0.x, y: center.y + $0.y) })
                    path.closeSubpath()
                    context.fill(path, with: .color(Color(white: 0.97 - Double(shade) * 0.12)))
                    context.stroke(path, with: .color(.black.opacity(0.25)), lineWidth: 1)
                    let mid = pts.reduce(CGPoint.zero) { CGPoint(x: $0.x + $1.x / 4, y: $0.y + $1.y / 4) }
                    context.draw(Text(face.name).font(.system(size: 8, weight: .semibold)).foregroundColor(.secondary),
                                 at: CGPoint(x: center.x + mid.x, y: center.y + mid.y))
                }
                // Axis stubs from the cube's corner.
                let o = project(SIMD3<Float>(-1, -1, -1) * 0.5, cam)
                for (axis, color) in [(SIMD3<Float>(1, 0, 0), Color.red), (SIMD3<Float>(0, 1, 0), Color.green), (SIMD3<Float>(0, 0, 1), Color.blue)] {
                    let e = project(SIMD3<Float>(-1, -1, -1) * 0.5 + axis * 1.35, cam)
                    var p = Path()
                    p.move(to: CGPoint(x: center.x + o.x, y: center.y + o.y))
                    p.addLine(to: CGPoint(x: center.x + e.x, y: center.y + e.y))
                    context.stroke(p, with: .color(color.opacity(0.8)), lineWidth: 1.5)
                }
            }
        }
        .frame(width: side, height: side)
        .contentShape(Rectangle())
        .onTapGesture { location in
            let p = CGPoint(x: location.x - side / 2, y: location.y - side / 2)
            // Front-most face containing the point (faces are drawn back to front).
            for (face, pts, _) in polys.reversed() where contains(pts, p) {
                viewport.camera.yaw = face.yaw
                viewport.camera.pitch = face.pitch
                viewport.redraw()
                return
            }
        }
        .help("Click a face to look at it")
    }

    private func project(_ p: SIMD3<Float>, _ cam: Camera) -> CGPoint {
        let s = Float(side) * 0.38
        return CGPoint(x: CGFloat(simd_dot(p, cam.right) * s), y: CGFloat(-simd_dot(p, cam.up) * s))
    }

    private func visibleFaces(_ cam: Camera) -> [(Face, [CGPoint], Float)] {
        let toEye = -cam.forward
        var out: [(face: Face, points: [CGPoint], shade: Float, facing: Float)] = []
        for face in Self.faces {
            let facing = simd_dot(face.normal, toEye)
            guard facing > 0.01 else { continue }
            let n = face.normal
            // Two in-plane axes for the face.
            let u: SIMD3<Float> = abs(n.z) > 0.5 ? [1, 0, 0] : [0, 0, 1]
            let v: SIMD3<Float> = simd_cross(n, u)
            let c: SIMD3<Float> = n * 0.5
            let hu: SIMD3<Float> = u * 0.5, hv: SIMD3<Float> = v * 0.5
            let corners: [SIMD3<Float>] = [c - hu - hv, c + hu - hv, c + hu + hv, c - hu + hv]
            out.append((face, corners.map { project($0, cam) }, 1 - facing, facing))
        }
        out.sort { $0.facing < $1.facing }
        return out.map { ($0.face, $0.points, $0.shade) }
    }

    private func contains(_ poly: [CGPoint], _ p: CGPoint) -> Bool {
        var inside = false
        var j = poly.count - 1
        for i in poly.indices {
            let a = poly[i], b = poly[j]
            if (a.y > p.y) != (b.y > p.y), p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x { inside.toggle() }
            j = i
        }
        return inside
    }
}

// MARK: - Measure and section

struct MeasurePanel: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController

    var body: some View {
        let u = store.settings.units
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Label("Measure", systemImage: "ruler").font(.headline)
                Spacer()
                Button { viewport.measuring = false } label: { Image(systemName: "xmark") }.buttonStyle(.plain)
            }
            if store.selection.isEmpty {
                Text("Select a body, face or edge — or two of them to measure between.")
                    .font(.callout).foregroundStyle(.secondary)
            } else if let m = viewport.measurement {
                if m.hasDistance && store.selection.count == 2 { row("Distance", m.distance / u.scale, u.symbol) }
                if m.hasAngle { row("Angle", m.angle, "°") }
                if m.hasLength { row("Length", m.length / u.scale, u.symbol) }
                if m.hasRadius { row("Radius", m.radius / u.scale, u.symbol) }
                if m.hasArea { row("Area", m.area / pow(u.scale, 2), "\(u.symbol)²") }
                if store.selection.count == 1, case let .body(b) = store.selection[0], let p = store.properties(b) {
                    row("Volume", p.volume / pow(u.scale, 3), "\(u.symbol)³")
                }
            } else {
                Text(store.kernel.lastError.isEmpty ? "Can't measure this selection" : store.kernel.lastError)
                    .font(.callout).foregroundStyle(.secondary)
            }
        }
        .padding(14)
        .frame(width: 300)
        .background(.thickMaterial, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
    }

    private func row(_ label: String, _ value: Double, _ unit: String) -> some View {
        HStack {
            Text(label).foregroundStyle(.secondary)
            Spacer()
            Text("\(formatted(value)) \(unit)").font(.body.monospacedDigit().weight(.medium)).textSelection(.enabled)
        }
    }
}

struct SectionPanel: View {
    @ObservedObject var viewport: ViewportController

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Label("Section View", systemImage: "square.split.bottomrightquarter").font(.headline)
                Spacer()
                Button { viewport.sectionEnabled = false } label: { Image(systemName: "xmark") }.buttonStyle(.plain)
            }
            Picker("Cut along", selection: $viewport.sectionAxis) {
                Text("X").tag(0)
                Text("Y").tag(1)
                Text("Z").tag(2)
            }
            .pickerStyle(.segmented)
            HStack {
                Text("Offset").foregroundStyle(.secondary)
                Slider(value: $viewport.sectionOffset, in: viewport.sectionRange)
                Text(formatted(viewport.sectionOffset.rounded())).font(.caption.monospacedDigit()).frame(width: 40)
            }
            Toggle("Flip side", isOn: $viewport.sectionFlipped)
        }
        .padding(14)
        .frame(width: 300)
        .background(.thickMaterial, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
    }
}
