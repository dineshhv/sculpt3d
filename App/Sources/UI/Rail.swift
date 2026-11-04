import SwiftUI

// MARK: - Building blocks

/// Height of one rail row; icons and their label pills line up on it.
let railRow: CGFloat = 40

/// A tool icon: a dark glyph on a soft tile, highlighted when active.
struct RailIcon: View {
    let symbol: String
    var active = false
    var enabled = true

    var body: some View {
        Group {
            switch symbol {
            case "sculpt3d.project": ProjectGlyph()
            case "sculpt3d.visualize": VisualizeGlyph()
            case "sculpt3d.magnet": MagnetGlyph(active: active)
            default:
                Image(systemName: symbol)
                    .symbolRenderingMode(.hierarchical)
                    .font(.system(size: 17, weight: .medium))
            }
        }
        .frame(width: 34, height: 34)
        .foregroundStyle(active ? Color.white : Color.primary.opacity(0.85))
        .background(active ? AnyShapeStyle(Color.primary.opacity(0.78)) : AnyShapeStyle(.clear),
                    in: RoundedRectangle(cornerRadius: 9, style: .continuous))
        .opacity(enabled ? 1 : 0.5)
    }
}

/// Approved Sculpt3D brand mark for the project button.
struct ProjectGlyph: View {
    var body: some View {
        Image("BrandMark")
            .resizable()
            .scaledToFit()
            .frame(width: 26, height: 26)
            .accessibilityHidden(true)
    }
}

/// A horseshoe magnet with red tips, for snapping.
struct MagnetGlyph: View {
    var active = false

    var body: some View {
        Canvas { context, size in
            let w = size.width, h = size.height
            var arc = Path()
            arc.addArc(center: CGPoint(x: w / 2, y: h * 0.55), radius: w * 0.3, startAngle: .degrees(0), endAngle: .degrees(180),
                       clockwise: false)
            context.stroke(arc, with: .color(active ? .white : .primary.opacity(0.85)), style: StrokeStyle(lineWidth: w * 0.18))
            for x in [w * 0.2, w * 0.8] {
                let leg = Path(CGRect(x: x - w * 0.09, y: h * 0.12, width: w * 0.18, height: h * 0.43))
                context.fill(leg, with: .color(active ? .white : .primary.opacity(0.85)))
                context.fill(Path(CGRect(x: x - w * 0.09, y: h * 0.12, width: w * 0.18, height: h * 0.16)), with: .color(.red))
            }
        }
        .frame(width: 18, height: 18)
    }
}

/// A colour wheel, for Visualize.
struct VisualizeGlyph: View {
    var body: some View {
        Circle()
            .fill(AngularGradient(colors: [.red, .orange, .yellow, .green, .cyan, .blue, .purple, .pink, .red], center: .center))
            .overlay(Circle().fill(RadialGradient(colors: [.white.opacity(0.8), .clear], center: .center, startRadius: 0, endRadius: 10)))
            .frame(width: 22, height: 22)
    }
}

/// Keyboard shortcut drawn as little key caps, e.g. "⌘U" → [⌘][U].
struct KeyCaps: View {
    let keys: String

    var body: some View {
        HStack(spacing: 2) {
            ForEach(Array(keys.enumerated()), id: \.offset) { _, ch in
                Text(String(ch))
                    .font(.system(size: 9, weight: .semibold, design: .rounded))
                    .foregroundStyle(.secondary)
                    .frame(minWidth: 13, minHeight: 14)
                    .background(Color.primary.opacity(0.07), in: RoundedRectangle(cornerRadius: 3))
            }
        }
    }
}

/// The label next to an icon: a separate rounded pill.
struct LabelPill: View {
    let title: String
    var subtitle: String?
    var keys: String?
    var enabled = true
    var dot = false

    var body: some View {
        HStack(spacing: 6) {
            VStack(alignment: .leading, spacing: 0) {
                Text(title).font(.system(size: 13, weight: .semibold))
                if let subtitle { Text(subtitle).font(.system(size: 10)).foregroundStyle(.secondary) }
            }
            if let keys { KeyCaps(keys: keys) }
            if dot { Circle().fill(Color.accentColor).frame(width: 6, height: 6) }
        }
        .padding(.horizontal, 9)
        .padding(.vertical, 5)
        .background(.thickMaterial, in: RoundedRectangle(cornerRadius: 9, style: .continuous))
        // A hairline keeps the pill visible on the light viewport background.
        .overlay(RoundedRectangle(cornerRadius: 9, style: .continuous).strokeBorder(Color.primary.opacity(0.08)))
        .opacity(enabled ? 1 : 0.6)
        .fixedSize()
    }
}

/// One entry of a rail: icon and (optionally) its label.
struct RailItem: Identifiable {
    let id: String
    let title: String
    let symbol: String
    var subtitle: String?
    var keys: String?
    var active = false
    var enabled = true
    var dot = false
    let action: () -> Void
}

/// A group of rail items: icons share one rounded column, labels float beside
/// them as separate pills (on the left of the icons when `trailing`).
struct RailGroup: View {
    let items: [RailItem]
    var showLabels = true
    var trailing = false

    var body: some View {
        HStack(alignment: .top, spacing: 6) {
            if trailing && showLabels { labels(alignment: .trailing) }
            VStack(spacing: 4) {
                ForEach(items) { item in
                    Button(action: item.action) { RailIcon(symbol: item.symbol, active: item.active, enabled: item.enabled) }
                        .buttonStyle(.plain)
                        .frame(height: railRow)
                        .disabled(!item.enabled)
                        .help(item.title)
                        .accessibilityLabel(item.title)
                }
            }
            .padding(.horizontal, 3)
            .background(.thickMaterial, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
            if !trailing && showLabels { labels(alignment: .leading) }
        }
    }

    private func labels(alignment: HorizontalAlignment) -> some View {
        VStack(alignment: alignment, spacing: 4) {
            ForEach(items) { item in
                Button(action: item.action) {
                    LabelPill(title: item.title, subtitle: item.subtitle, keys: item.keys, enabled: item.enabled, dot: item.dot)
                }
                .buttonStyle(.plain)
                .disabled(!item.enabled)
                .frame(height: railRow, alignment: alignment == .leading ? .leading : .trailing)
            }
        }
    }
}

/// Shows content as is when it fits the height on offer, and in a scroll
/// view only when it doesn't, so taps aren't delayed by scrolling when there
/// is nothing to scroll. Never grows its container.
struct FitScroll<Content: View>: View {
    @ViewBuilder let content: Content

    var body: some View {
        ViewThatFits(in: .vertical) {
            content
            ScrollView(.vertical, showsIndicators: false) { content }
        }
        .fixedSize(horizontal: true, vertical: false)
        .frame(minHeight: 0)
    }
}

// MARK: - Main rail

/// Left rail. Its contents follow the mode: modeling, sketching, measuring
/// or visualizing.
struct ToolRail: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController
    @ObservedObject var ui: UIState
    var compact: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            RailGroup(items: [
                RailItem(id: "project", title: "Project", symbol: "sculpt3d.project", subtitle: mode, keys: nil, active: ui.showProject) {
                    ui.showProject.toggle()
                },
                RailItem(id: "items", title: "Items", symbol: "square.3.layers.3d", active: ui.showItems) { ui.showItems.toggle() },
            ], showLabels: !compact)
            .popover(isPresented: $ui.showProject) { ProjectPanel(store: store).padding() }
            Spacer(minLength: 8)
            middle
            Spacer(minLength: 8)
            if !ui.visualizing {
                RailGroup(items: bottomItems, showLabels: !compact)
            }
        }
    }

    private var mode: String {
        if ui.visualizing { return "Visualize" }
        if store.sketch != nil { return "Sketching" }
        return viewport.measuring ? "Measuring" : "Modeling"
    }

    @ViewBuilder
    private var middle: some View {
        if ui.visualizing {
            RailGroup(items: [
                search,
                RailItem(id: "exitViz", title: "Exit Visualization", symbol: "xmark") { ui.visualizing = false },
            ], showLabels: !compact)
        } else if store.sketch != nil {
            FitScroll { SketchRail(store: store, viewport: viewport, ui: ui, compact: compact) }
        } else if viewport.measuring {
            RailGroup(items: [
                RailItem(id: "exitMeasure", title: "Exit Measure", symbol: "xmark") { viewport.measuring = false },
                RailItem(id: "measureMode", title: "Measure mode", symbol: "ruler", subtitle: "Object Measurements", active: true) {},
            ], showLabels: !compact)
        } else {
            HStack(alignment: .top, spacing: 12) {
                RailGroup(items: [search] + ToolCategory.rail.map { c in
                    RailItem(id: c.rawValue, title: c.title, symbol: c == .visualize ? "sculpt3d.visualize" : c.symbol,
                             active: ui.openMenu == c, dot: c == .tools) {
                        if c == .visualize {
                            ui.openMenu = nil
                            ui.visualizing = true
                        } else {
                            ui.openMenu = ui.openMenu == c ? nil : c
                        }
                    }
                }, showLabels: !compact && ui.openMenu == nil)
                .background(GeometryReader { g in
                    Color.clear.preference(key: RailMenuFrameKey.self, value: g.frame(in: .named(OverlaySpace.name)))
                })
            }
        }
    }

    private var search: RailItem {
        RailItem(id: "search", title: "Search", symbol: "magnifyingglass", keys: "⌘F", active: ui.showSearch) { ui.showSearch = true }
    }

    private var bottomItems: [RailItem] {
        [
            RailItem(id: "section", title: "Section View", symbol: "square.split.bottomrightquarter",
                     subtitle: viewport.sectionEnabled ? "On" : "Off", active: viewport.sectionEnabled) { viewport.sectionEnabled.toggle() },
            RailItem(id: "measure", title: "Measure", symbol: "ruler", active: viewport.measuring) { viewport.measuring.toggle() },
        ]
    }
}

/// Coordinate space of the viewport overlays, for lining the flyout up with the rail.
enum OverlaySpace {
    static let name = "overlays"
}

/// Frame of the rail's tool icons, in the overlay space.
struct RailMenuFrameKey: PreferenceKey {
    static var defaultValue: CGRect = .zero
    static func reduce(value: inout CGRect, nextValue: () -> CGRect) {
        let next = nextValue()
        if next != .zero { value = next }
    }
}

/// Places its one subview at `top`, moved up only as far as it must to stay
/// inside the available height.
struct TopClampedLayout: Layout {
    var top: CGFloat

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let size = subviews.first?.sizeThatFits(proposal) ?? .zero
        return CGSize(width: size.width, height: proposal.height ?? size.height)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        guard let view = subviews.first else { return }
        let size = view.sizeThatFits(ProposedViewSize(width: proposal.width, height: bounds.height))
        // `top` is measured from the top of the overlay row, which is where this column starts too.
        let y = max(0, min(top, bounds.height - size.height))
        view.place(at: CGPoint(x: bounds.minX, y: bounds.minY + y), proposal: ProposedViewSize(width: size.width, height: size.height))
    }
}

/// The tools of one group, as a second icon column with label pills.
struct ToolFlyout: View {
    let category: ToolCategory
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController
    @ObservedObject var ui: UIState
    var compact: Bool

    var body: some View {
        FitScroll {
            RailGroup(items: ToolCatalog.commands(in: category).map { command in
                RailItem(id: command.id, title: command.title, symbol: command.symbol, keys: ToolCatalog.shortcut(for: command.id),
                         enabled: command.isEnabled(store)) {
                    ui.openMenu = nil
                    command.run(store, viewport, ui)
                }
            })
        }

    }
}

// MARK: - Sketch mode

/// Sketching tools down the left side.
struct SketchRail: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController
    @ObservedObject var ui: UIState
    var compact: Bool

    var body: some View {
        let s = store.sketch
        VStack(alignment: .leading, spacing: 10) {
            RailGroup(items: [
                RailItem(id: "search", title: "Search", symbol: "magnifyingglass", keys: "⌘F") { ui.showSearch = true },
                RailItem(id: "exit", title: "Exit Sketching", symbol: "xmark") { store.cancelSketch() },
                RailItem(id: "plane", title: planeName, symbol: "square.grid.3x3") { viewport.faceSketchPlane() },
            ], showLabels: !compact)
            RailGroup(items: SketchTool.allCases.filter { $0 != .select && $0 != .trim }.map { tool in
                RailItem(id: tool.rawValue, title: tool.title, symbol: tool.symbol, subtitle: subtitle(tool),
                         keys: tool.key.map { String($0).uppercased() }, active: s?.tool == tool) { store.setSketchTool(tool) }
            } + [
                RailItem(id: "project", title: "Project", symbol: "rectangle.portrait.on.rectangle.portrait.angled") { store.projectIntoSketch() },
                RailItem(id: "offset", title: "Offset Edge", symbol: "square.on.square", keys: "O",
                         enabled: !(s?.selection.isEmpty ?? true)) { store.offsetSketchSelection() },
                RailItem(id: "fillet", title: "Fillet", symbol: "arrow.turn.up.right", enabled: !(s?.selection.isEmpty ?? true)) {
                    store.filletSketchSelection()
                },
            ], showLabels: !compact)
            RailGroup(items: [
                RailItem(id: "select", title: "Select", symbol: "cursorarrow", keys: "S", active: s?.tool == .select) { store.setSketchTool(.select) },
                RailItem(id: "trim", title: "Trim", symbol: "scissors", keys: "T", active: s?.tool == .trim) { store.setSketchTool(.trim) },
                RailItem(id: "delete", title: "Delete", symbol: "trash", enabled: !(s?.selection.isEmpty ?? true)) { store.deleteSketchSelection() },
            ], showLabels: !compact)
            RailGroup(items: [
                RailItem(id: "construction", title: "Construction", symbol: "square.dashed",
                         subtitle: s?.constructionMode == true ? "On" : "Off", active: s?.constructionMode == true) {
                    store.sketch?.constructionMode.toggle()
                },
            ], showLabels: !compact)
        }
    }

    private var planeName: String {
        guard let p = store.sketch?.plane else { return "No active plane" }
        if p.normal == SketchPlane.xy.normal && p.origin == .zero { return "Top plane" }
        if p.normal == SketchPlane.xz.normal && p.origin == .zero { return "Front plane" }
        if p.normal == SketchPlane.yz.normal && p.origin == .zero { return "Right plane" }
        return "Face plane"
    }

    private func subtitle(_ tool: SketchTool) -> String? {
        switch tool {
        case .rectangle: return "Diagonal"
        case .centerRectangle: return "Center"
        case .spline: return "Fit"
        case .polygon: return "\(store.sketch?.polygonSides ?? 6) sides"
        case .arc: return "3-point"
        default: return nil
        }
    }
}

/// Constraints down the right side, enabled by what the selection allows.
struct ConstraintColumn: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController

    var body: some View {
        let s = store.sketch
        let kinds = s?.availableConstraints ?? []
        let lines = s?.selectedLines ?? [], points = s?.selectedPoints ?? [], circles = s?.selectedCircles ?? []
        func item(_ id: String, _ title: String, _ symbol: String, _ keys: String?, _ enabled: Bool, _ run: @escaping () -> Void) -> RailItem {
            RailItem(id: id, title: title, symbol: symbol, keys: keys, enabled: enabled, action: run)
        }
        return RailGroup(items: [
            item("settings", viewport.showConstraints ? "Hide Constraints" : "Show Constraints", "gearshape", nil, true) {
                viewport.showConstraints.toggle()
            },
            item("parallel", "Parallel", "equal", "⇧A", kinds.contains(.parallel)) { store.constrainSelection(.parallel) },
            item("perpendicular", "Perpendicular", "angle", "⇧P", kinds.contains(.perpendicular)) { store.constrainSelection(.perpendicular) },
            item("tangent", "Tangent", "circle.and.line.horizontal", "⇧T", kinds.contains(.tangent)) { store.constrainSelection(.tangent) },
            item("coincident", "Coincident", "smallcircle.filled.circle", "⇧N",
                 kinds.contains(.coincident) || kinds.contains(.pointOnLine) || kinds.contains(.pointOnCircle)) {
                store.constrainSelection(kinds.contains(.coincident) ? .coincident : (kinds.contains(.pointOnLine) ? .pointOnLine : .pointOnCircle))
            },
            item("midpoint", "Midpoint", "line.diagonal", "⇧M", kinds.contains(.midpoint)) { store.constrainSelection(.midpoint) },
            item("concentric", "Concentric", "circle.circle", "⇧C", circles.count == 2 && points.isEmpty && lines.isEmpty) {
                store.constrainSelection(.coincident)
            },
            item("hv", "Horizontal/Vertical", "arrow.left.and.right", "⇧V", !lines.isEmpty && points.isEmpty && circles.isEmpty) {
                store.constrainHorizontalOrVertical()
            },
            item("equal", "Equal", "equal.circle", "⇧E", kinds.contains(.equalLength) || kinds.contains(.equalRadius)) {
                store.constrainSelection(kinds.contains(.equalLength) ? .equalLength : .equalRadius)
            },
            item("symmetry", "Symmetry", "arrow.left.and.line.vertical.and.arrow.right", "⇧S", kinds.contains(.symmetric)) {
                store.constrainSelection(.symmetric)
            },
            item("disconnect", "Disconnect", "link.badge.plus", nil, points.count == 1) { store.disconnectSketchSelection() },
            item("lock", "Lock", "lock", "⇧L", kinds.contains(.fixed)) { store.constrainSelection(.fixed) },
            item("construction", "Make Construction", "square.dashed", nil, !(s?.selection.isEmpty ?? true)) { store.toggleConstruction() },
        ], trailing: true)
    }
}

// MARK: - Right-hand view controls

struct ViewControlsColumn: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController
    @ObservedObject var ui: UIState
    var compact = false

    var body: some View {
        VStack(alignment: .trailing, spacing: 10) {
            HStack(alignment: .top, spacing: 8) {
                ViewCube(viewport: viewport)
                VStack(spacing: 4) {
                    Button { ui.showSnapSettings.toggle() } label: { RailIcon(symbol: "sculpt3d.magnet", active: store.settings.snap.grid) }
                        .buttonStyle(.plain)
                        .help("Snapping")
                        .accessibilityLabel("Snapping")
                        .popover(isPresented: $ui.showSnapSettings) { SnapSettingsPanel(store: store).padding() }
                    Button { ui.showGridSettings.toggle() } label: {
                        Text(gridLabel)
                            .font(.system(size: 10, weight: .bold, design: .rounded).monospacedDigit())
                            .multilineTextAlignment(.center)
                            .frame(width: 34, height: 34)
                    }
                    .buttonStyle(.plain)
                    .help("Grid size")
                    .popover(isPresented: $ui.showGridSettings) { GridSettingsPanel(store: store, viewport: viewport).padding() }
                    Button { store.settings.environment.showGrid.toggle() } label: {
                        RailIcon(symbol: "move.3d", active: !store.settings.environment.showGrid)
                    }
                    .buttonStyle(.plain)
                    .help(store.settings.environment.showGrid ? "Hide grid and axes" : "Show grid and axes")
                    .accessibilityLabel("Grid and axes")
                }
                .padding(3)
                .background(.thickMaterial, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
            }
            RailGroup(items: [
                RailItem(id: "display", title: "Display Modes", symbol: viewport.displayMode.symbol, subtitle: viewport.displayMode.title) {
                    let all = DisplayMode.allCases
                    viewport.displayMode = all[(all.firstIndex(of: viewport.displayMode)! + 1) % all.count]
                },
                RailItem(id: "shot", title: "Screenshot", symbol: "camera") {
                    ToolCatalog.view.first { $0.id == "view.screenshot" }?.run(store, viewport, ui)
                },
                RailItem(id: "export", title: "Export STEP", symbol: "square.and.arrow.up") {
                    ToolCatalog.view.first { $0.id == "export.step" }?.run(store, viewport, ui)
                },
                RailItem(id: "history", title: "History", symbol: "clock.arrow.circlepath", keys: "⌥⌘P", active: ui.showHistory) {
                    ui.showHistory.toggle()
                },
            ], showLabels: !compact, trailing: true)
        }
    }

    private var gridLabel: String {
        let u = store.settings.units
        return "\(formatted(viewport.gridStep / u.scale))\n\(u.symbol)"
    }
}

// MARK: - Popovers and side panels

struct SnapSettingsPanel: View {
    @ObservedObject var store: ModelStore

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Snap to").font(.headline)
            Toggle(isOn: $store.settings.snap.grid) { Label("Grid", systemImage: "grid") }
            Toggle(isOn: $store.settings.snap.guideLines) { Label("Sketch Guide Lines", systemImage: "squareshape.split.2x2.dotted") }
            Toggle(isOn: $store.settings.snap.guidePoints) { Label("Sketch Guide Points", systemImage: "circle.dotted") }
            Toggle(isOn: $store.settings.snap.points3D) { Label("3D Guide Points", systemImage: "cube") }
            Toggle(isOn: $store.settings.snap.distantEdges) { Label("Distant Edges", systemImage: "cube.transparent") }
            Divider()
            Text("Show").font(.headline)
            Toggle(isOn: $store.settings.snap.showGuidePoints) { Label("Guide Points", systemImage: "line.diagonal") }
            Toggle(isOn: $store.settings.snap.showHints) { Label("Snapping Hints", systemImage: "text.bubble") }
        }
        .toggleStyle(.switch)
        .frame(width: 280)
    }
}

struct GridSettingsPanel: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController

    var body: some View {
        let u = store.settings.units
        VStack(alignment: .leading, spacing: 8) {
            Text("Grid").font(.headline)
            Picker("Spacing", selection: $viewport.fixedGrid) {
                Text("Automatic").tag(Double?.none)
                ForEach([0.1, 0.5, 1, 5, 10, 50, 100] as [Double], id: \.self) { v in
                    Text("\(formatted(v)) \(u.symbol)").tag(Double?.some(v * u.scale))
                }
            }
            .pickerStyle(.inline)
            .labelsHidden()
        }
        .frame(width: 180)
    }
}

struct VariablesPanel: View {
    @ObservedObject var store: ModelStore
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        let values = store.variableValues
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("Variables").font(.headline)
                Spacer()
                Button { store.addVariable() } label: { Label("Add", systemImage: "plus") }
                Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
            }
            Text("Type a variable's name, or an expression like `width * 2`, into any length or angle field.")
                .font(.caption)
                .foregroundStyle(.secondary)
            ForEach($store.settings.variables) { $v in
                HStack {
                    TextField("name", text: $v.name).frame(width: 110)
                    Text("=")
                    TextField("expression", text: $v.expression)
                    Text(values[v.name.lowercased()].map(formatted) ?? "—")
                        .font(.callout.monospacedDigit())
                        .foregroundStyle(values[v.name.lowercased()] == nil ? .red : .secondary)
                        .frame(width: 70, alignment: .trailing)
                    Button(role: .destructive) { store.settings.variables.removeAll { $0.id == v.id } } label: { Image(systemName: "trash") }
                        .buttonStyle(.plain)
                }
                .textFieldStyle(.roundedBorder)
            }
        }
        .padding()
        .frame(minWidth: 460)
    }
}

/// Measure mode's side panel: the current measurement, deltas and pins.
struct MeasureSidePanel: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController

    var body: some View {
        let u = store.settings.units
        let rows = currentRows(u)
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("Measure").font(.headline)
                Spacer()
                Button { viewport.measuring = false } label: { Image(systemName: "xmark.circle.fill") }.buttonStyle(.plain)
            }
            Toggle("Include X, Y, Z Deltas", isOn: $viewport.measureDeltas).toggleStyle(.switch)
            if rows.isEmpty {
                Text(store.selection.isEmpty ? "Select any item to measure — or two to measure between them." : "Can't measure this selection")
                    .font(.callout).foregroundStyle(.secondary)
            } else {
                ForEach(rows, id: \.0) { row in
                    HStack {
                        Text(row.0).foregroundStyle(.secondary)
                        Spacer()
                        Text(row.1).font(.body.monospacedDigit().weight(.medium)).textSelection(.enabled)
                        Button { viewport.pinnedMeasurements.append("\(row.0): \(row.1)") } label: { Image(systemName: "pin") }
                            .buttonStyle(.plain)
                            .help("Pin to always show")
                    }
                }
            }
            Divider()
            Text("Pinned").font(.caption.weight(.semibold)).foregroundStyle(.secondary)
            if viewport.pinnedMeasurements.isEmpty {
                Text("Pin measurements to keep them visible.").font(.caption).foregroundStyle(.secondary)
            }
            ForEach(Array(viewport.pinnedMeasurements.enumerated()), id: \.offset) { i, text in
                HStack {
                    Text(text).font(.callout.monospacedDigit())
                    Spacer()
                    Button { viewport.pinnedMeasurements.remove(at: i) } label: { Image(systemName: "pin.slash") }.buttonStyle(.plain)
                }
            }
            Spacer()
        }
        .padding(14)
    }

    private func currentRows(_ u: LengthUnit) -> [(String, String)] {
        guard !store.selection.isEmpty, let m = viewport.measurement else { return [] }
        func len(_ v: Double) -> String { formatLength(v, u) }
        var rows: [(String, String)] = []
        if m.hasDistance && store.selection.count == 2 {
            rows.append(("Distance", len(m.distance)))
            if viewport.measureDeltas {
                let d = m.p2 - m.p1
                rows += [("ΔX", len(abs(d.x))), ("ΔY", len(abs(d.y))), ("ΔZ", len(abs(d.z)))]
            }
        }
        if m.hasAngle { rows.append(("Angle", formatAngle(m.angle))) }
        if m.hasLength { rows.append(("Length", len(m.length))) }
        if m.hasRadius { rows.append(("Radius", len(m.radius))); rows.append(("Diameter", len(m.radius * 2))) }
        if m.hasArea { rows.append(("Area", formatArea(m.area, u))) }
        if store.selection.count == 1, case let .body(b) = store.selection[0], let p = store.properties(b) {
            rows.append(("Volume", formatVolume(p.volume, u)))
            if viewport.measureDeltas {
                let size = p.bboxMax - p.bboxMin
                rows += [("Size X", len(size.x)), ("Size Y", len(size.y)), ("Size Z", len(size.z))]
            }
        }
        return rows
    }
}
