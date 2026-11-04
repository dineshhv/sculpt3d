import SwiftUI
#if os(macOS)
import AppKit
#endif

/// The menu bar (Mac, and iPad with a hardware keyboard) for the focused document:
/// File, Edit, Item, Sketch, Insert, Construct, Transform, Tools and View.
struct ModelCommands: Commands {
    @FocusedObject private var store: ModelStore?
    @FocusedObject private var viewport: ViewportController?
    @FocusedObject private var ui: UIState?

    var body: some Commands {
        fileCommands
        editCommands
        itemMenu
        sketchMenu
        insertMenu
        constructMenu
        transformMenu
        toolsMenu
        viewCommands
    }

    // MARK: Helpers

    private var idle: Bool { store.map { $0.draft == nil && $0.sketch == nil } ?? false }
    private var hasDocument: Bool { store != nil }

    /// A menu item that runs a catalog tool, enabled when the tool is.
    @ViewBuilder
    private func tool(_ id: String, _ title: String? = nil, key: KeyboardShortcut? = nil) -> some View {
        let command = ToolCatalog.all.first { $0.id == id }
        Button(title ?? command?.title ?? id) {
            guard let store, let viewport, let ui, let command else { return }
            command.run(store, viewport, ui)
        }
        .disabled(store.map { s in command.map { !$0.isEnabled(s) } ?? true } ?? true)
        .keyboardShortcut(key)
    }

    private func run(_ action: @escaping (ModelStore, ViewportController, UIState) -> Void) -> () -> Void {
        { if let store, let viewport, let ui { action(store, viewport, ui) } }
    }

    private func toggle(_ title: String, _ get: @escaping (ModelStore, ViewportController, UIState) -> Bool,
                        _ set: @escaping (ModelStore, ViewportController, UIState, Bool) -> Void) -> some View {
        Toggle(title, isOn: Binding(
            get: { if let store, let viewport, let ui { return get(store, viewport, ui) } else { return false } },
            set: { v in if let store, let viewport, let ui { set(store, viewport, ui, v) } }))
            .disabled(!hasDocument)
    }

    private static func key(_ c: Character, _ m: EventModifiers = []) -> KeyboardShortcut { KeyboardShortcut(KeyEquivalent(c), modifiers: m) }

    // MARK: File

    @CommandsBuilder
    private var fileCommands: some Commands {
        CommandGroup(replacing: .newItem) {
            Button("New Project") {
                #if os(macOS)
                NSDocumentController.shared.newDocument(nil)
                #endif
            }
            .keyboardShortcut("n")
            Divider()
            Button("New Drawing") { ui?.showDrawing = true }.disabled(!hasDocument)
            Divider()
            Button("Import File…") { ui?.importing = true }.keyboardShortcut("i", modifiers: [.command, .shift]).disabled(!idle)
            Button("Import Project…") { ui?.importingProject = true }.keyboardShortcut("p", modifiers: [.command, .shift]).disabled(!idle)
            Button("Import Material…") { ui?.importingMaterial = true }.disabled(!idle || (store?.bodyIds.isEmpty ?? true))
            Button("Import Image as Decal…") { ui?.importingDecal = true }
                .disabled(!idle || (store?.selectedFaces.isEmpty ?? true))
        }
        CommandGroup(after: .saveItem) {
            Divider()
            Button("View in Browser") { viewInBrowser() }.disabled(!hasDocument)
            Button("Copy Link") { copyLink() }.disabled(store?.documentURL == nil)
            #if os(iOS)
            Button("Share…") { share() }.disabled(!hasDocument)
            #endif
            Button("Publish Current Version…") {
                guard let store, let ui, let page = store.webPage(title: store.documentName) else { return }
                ui.publishName = "\(store.documentName) \(Date().formatted(.dateTime.year().month().day()))"
                ui.publishFile = WebPageFile(data: page)
                ui.publishing = true
            }
            .disabled(!hasDocument)
            Divider()
            Menu("Export to") {
                Button("Export…") { ui?.exportToSlicer = false; ui?.showExport = true }
                    .keyboardShortcut("e", modifiers: [.command, .shift])
                    .disabled(!hasDocument)
                #if os(macOS)
                Button("Export to Slicer…") { ui?.exportToSlicer = true; ui?.showExport = true }
                    .disabled(store?.exportBodies.isEmpty ?? true)
                #endif
                Button("Quick Export to Favorite Formats") { if let store { quickExport(store: store) } }
                    .disabled(!hasDocument || QuickExport.formats.isEmpty)
                tool("view.screenshot", "Open Screenshot Tool…")
                Button("Open Visualization…") { ui?.visualizing = true; viewport?.redraw() }
                    .disabled(!hasDocument || store?.sketch != nil)
                Divider()
                ForEach(ExportKind.menu) { kind in
                    Button("\(kind.title)…") { if let store, let ui { ui.export(kind, from: store) } }
                        .disabled(!hasDocument || !exportable(kind))
                }
            }
        }
    }

    /// Whether File ▸ Export to can make this format from the document now.
    private func exportable(_ kind: ExportKind) -> Bool {
        guard let store, kind.available else { return false }
        switch kind {
        case .dxfSketch: return !store.exportSketches.isEmpty
        case .svg: return !store.exportSketches.isEmpty || !store.exportBodies.isEmpty
        case .sculpt3d: return true
        default: return !store.exportBodies.isEmpty
        }
    }

    private func viewInBrowser() {
        guard let store, let url = store.webPageURL(title: store.documentName) else { return }
        #if os(macOS)
        NSWorkspace.shared.open(url)
        #else
        UIApplication.shared.open(url)
        #endif
    }

    private func copyLink() {
        guard let url = store?.documentURL else { return }
        #if os(macOS)
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(url.absoluteString, forType: .string)
        #else
        UIPasteboard.general.url = url
        #endif
        store?.toast = "Copied the project's link"
    }

    private func share() {
        guard let store else { return }
        var items: [Any] = []
        if let url = store.documentURL { items.append(url) } else if let url = store.webPageURL(title: store.documentName) { items.append(url) }
        guard !items.isEmpty else { return }
        #if os(macOS)
        guard let view = NSApp.keyWindow?.contentView else { return }
        let picker = NSSharingServicePicker(items: items)
        picker.show(relativeTo: CGRect(x: view.bounds.midX, y: view.bounds.maxY - 40, width: 1, height: 1), of: view, preferredEdge: .minY)
        #else
        let controller = UIActivityViewController(activityItems: items, applicationActivities: nil)
        let scene = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.first
        if let root = scene?.keyWindow?.rootViewController {
            controller.popoverPresentationController?.sourceView = root.view
            controller.popoverPresentationController?.sourceRect = CGRect(x: root.view.bounds.midX, y: 60, width: 1, height: 1)
            root.present(controller, animated: true)
        }
        #endif
    }

    // MARK: Edit

    @CommandsBuilder
    private var editCommands: some Commands {
        CommandGroup(replacing: .undoRedo) {
            Button("Undo") {
                guard let store else { return }
                store.sketch != nil ? store.undoSketch() : store.undo()
            }
            .keyboardShortcut("z")
            .disabled(store.map { $0.sketch != nil ? $0.sketchUndo.isEmpty : !$0.canUndo } ?? true)
            Button("Redo") {
                guard let store else { return }
                store.sketch != nil ? store.redoSketch() : store.redo()
            }
            .keyboardShortcut("z", modifiers: [.command, .shift])
            .disabled(store.map { $0.sketch != nil ? $0.sketchRedo.isEmpty : !$0.canRedo } ?? true)
        }
        CommandGroup(after: .pasteboard) {
            Divider()
            tool("transform.duplicate", "Duplicate", key: Self.key("d", .command))
            Button("Delete") {
                guard let store else { return }
                if store.sketch != nil { store.deleteSketchSelection() } else { store.deleteSelection() }
            }
            .keyboardShortcut(.delete, modifiers: [])
            .disabled(store.map { $0.sketch != nil ? ($0.sketch?.selection.isEmpty ?? true) : ($0.selectedBody == nil || $0.draft != nil) } ?? true)
            Button("Select All Bodies") { if let store { store.selection = store.bodyIds.filter { !store.settings.hiddenBodies.contains($0) }.map { .body($0) } } }
                .keyboardShortcut("a")
                .disabled(!idle)
            Button("Deselect All") { store?.selection = []; store?.itemSelection = [] }
                .keyboardShortcut("a", modifiers: [.command, .shift])
                .disabled(!hasDocument)
        }
    }

    // MARK: Item

    private var itemMenu: some Commands {
        CommandMenu("Item") {
            let hasSelection = store.map { !$0.selection.isEmpty || !$0.itemSelection.isEmpty } ?? false
            let bodies = !(store?.touchedBodies.isEmpty ?? true)
            Button("Show/Hide") { store?.toggleVisibilityOfSelection() }
                .keyboardShortcut("h", modifiers: [.command, .shift])
                .disabled(!hasSelection)
            Divider()
            Button("Isolate") { store?.isolateSelection() }.disabled(!bodies)
            Button("Exit Isolation") { store?.exitIsolation() }.disabled(!(store?.isIsolating ?? false))
            Divider()
            Button("Zoom to") { viewport?.zoomToSelection() }.disabled(!bodies)
            Button("Reveal in Items Sidebar") { ui?.showItems = true; ui?.openMenu = nil }
                .keyboardShortcut("i", modifiers: [.option, .shift])
                .disabled(!hasSelection)
            Divider()
            Button("Show Hidden Bodies") { store?.showAllBodies() }
                .keyboardShortcut("h", modifiers: .option)
                .disabled(store?.settings.hiddenBodies.isEmpty ?? true)
            Button("Invert Body Visibility") { store?.invertBodyVisibility() }
                .keyboardShortcut("h", modifiers: [.option, .shift])
                .disabled(store?.bodyIds.isEmpty ?? true)
        }
    }

    // MARK: Sketch

    /// Picks a sketch tool, starting a sketch first (on the selected face or plane, or Top) when needed.
    private func sketchTool(_ tool: SketchTool, _ title: String, key: Character? = nil,
                            setup: ((inout SketchSession) -> Void)? = nil) -> some View {
        Button(title) {
            guard let store else { return }
            if store.sketch == nil {
                guard store.draft == nil else { return }
                if !store.selectedFaces.isEmpty { store.startSketchOnSelectedFace() }
                else if store.selectedItems.contains(where: \.isPlane) { store.startSketchOnSelectedPlane() }
                else { store.startSketch(on: .xy) }
            }
            store.setSketchTool(tool)
            if let setup, var s = store.sketch {
                setup(&s)
                store.sketch = s
            }
        }
        .keyboardShortcut(key.map { Self.key($0) })
        .disabled(!hasDocument || store?.draft != nil)
    }

    private var sketchMenu: some Commands {
        CommandMenu("Sketch") {
            let inSketch = store?.sketch != nil
            let sketchSelection = !(store?.sketch?.selection.isEmpty ?? true)
            let canTransform = store?.canTransformSketch ?? false
            sketchTool(.line, "Line", key: "l")
            sketchTool(.arc, "Arc", key: "a")
            Menu("Spline") {
                sketchTool(.spline, "Fit Point Spline")
                sketchTool(.controlSpline, "Control Point Spline")
            }
            Menu("Rectangle") {
                sketchTool(.rectangle, "2-Point Rectangle")
                sketchTool(.centerRectangle, "Center Rectangle")
            }
            sketchTool(.circle, "Circle", key: "c")
            sketchTool(.ellipse, "Ellipse")
            Menu("Polygon") {
                sketchTool(.polygon, "Inscribed Polygon") { $0.polygonCircumscribed = false }
                sketchTool(.polygon, "Circumscribed Polygon") { $0.polygonCircumscribed = true }
                Divider()
                ForEach([3, 4, 5, 6, 8, 10, 12], id: \.self) { n in
                    sketchTool(.polygon, "\(n) Sides") { $0.polygonSides = n }
                }
            }
            sketchTool(.point, "Point")
            Divider()
            Button("Project as Sketch") { store?.projectIntoSketch() }.disabled(!inSketch)
            Button("Offset Edge") { store?.offsetSketchSelection() }.disabled(!sketchSelection)
            Button("Fillet Sketch") { store?.filletSketchSelection() }.disabled(!sketchSelection)
            Button("Text…") { ui?.showSketchText = true }.disabled(!inSketch)
            Divider()
            Button("Scale Sketch") { ui?.sketchTransform = .scale }.keyboardShortcut(Self.key("s")).disabled(!canTransform)
            Button("Pattern - Linear") { ui?.sketchTransform = .linearPattern }.disabled(!canTransform)
            Button("Pattern - Circular") { ui?.sketchTransform = .circularPattern }.disabled(!canTransform)
            Button("Rotate Sketch Around Axis") { ui?.sketchTransform = .rotate }.disabled(!canTransform)
            Button("Mirror Sketch") { store?.mirrorSketchSelection() }.disabled(!canTransform)
            Divider()
            sketchTool(.trim, "Trim", key: "t")
            Divider()
            Button("Finish Sketch") { store?.finishSketchAsItem() }.disabled(!inSketch)
            Button("Cancel Sketch") { store?.cancelSketch() }.disabled(!inSketch)
        }
    }

    // MARK: Insert

    private var insertMenu: some Commands {
        CommandMenu("Insert") {
            tool("insert.variable", "Variable…", key: Self.key("v", [.command, .shift]))
            tool("insert.image", "Image…")
            tool("insert.import", "File…")
            tool("insert.project", "Import Project…")
            Divider()
            Menu("Primitive") {
                ForEach(PrimitiveKind.allCases, id: \.self) { kind in tool("insert.\(kind.rawValue)", kind.title) }
            }
            Menu("Terrain") {
                Button("Rolling Hills") { store?.insertRollingHills() }.disabled(!idle)
                Button("From Height Map…") { ui?.importingHeightMap = true }.disabled(!idle)
            }
        }
    }

    // MARK: Construct

    private var constructMenu: some Commands {
        CommandMenu("Construct") {
            let faces = store?.selectedFaces.count ?? 0
            Menu("Plane") {
                tool("construct.offset", "Offset Plane")
                Button("Midplane") { store?.addMidplane() }.disabled(!idle || faces != 2)
                Button("Plane Through 3 Points") {
                    viewport?.beginPointPick("Plane Through 3 Points", prompts: ["Pick the first point", "Pick the second point", "Pick the third point"]) {
                        store?.addPlane(through: $0)
                    }
                }
                .disabled(!idle)
                Button("Plane on Curve at Point") {
                    viewport?.beginPointPick("Plane on Curve at Point", prompts: ["Pick a point on a curved edge"], accepts: [.edge]) {
                        if let p = $0.first { store?.addPlane(normalToEdgeAt: p) }
                    }
                }
                .disabled(!idle)
                Button("Plane Perpendicular to Edge at Point") {
                    viewport?.beginPointPick("Plane Perpendicular to Edge at Point", prompts: ["Pick a point on an edge"], accepts: [.edge]) {
                        if let p = $0.first { store?.addPlane(normalToEdgeAt: p) }
                    }
                }
                .disabled(!idle)
                Button("Plane Through Edge at Angle") { store?.addPlaneThroughEdge() }
                    .disabled(!idle || (store?.selectedEdges.isEmpty ?? true))
                Button("Plane Parallel to Face at Point") {
                    viewport?.beginPointPick("Plane Parallel to Face at Point", prompts: ["Pick a point on a face"], accepts: [.face]) {
                        if let p = $0.first { store?.addPlane(parallelToFaceAt: p) }
                    }
                }
                .disabled(!idle)
                Divider()
                tool("construct.angled", "Angled Plane")
            }
            Menu("Axis") {
                Button("Axis Through 2 Points") {
                    viewport?.beginPointPick("Axis Through 2 Points", prompts: ["Pick the first point", "Pick the second point"]) {
                        store?.addAxis(through: $0)
                    }
                }
                .disabled(!idle)
                Button("Axis Through 2 Planes") { store?.addAxisThroughTwoPlanes() }
                    .disabled(!idle || (faces + (store?.selectedItems.filter(\.isPlane).count ?? 0)) < 2)
                Button("Axis of Cylinder/Cone") { store?.addAxisOfRoundFace() }
                    .disabled(!idle || (faces == 0 && (store?.selectedEdges.isEmpty ?? true)))
                Button("Axis Along Edge") { store?.addAxisAlongEdge() }.disabled(!idle || (store?.selectedEdges.isEmpty ?? true))
                Button("Axis Perpendicular to Face at Point") {
                    viewport?.beginPointPick("Axis Perpendicular to Face at Point", prompts: ["Pick a point on a face"], accepts: [.face]) {
                        if let p = $0.first { store?.addAxis(normalToFaceAt: p) }
                    }
                }
                .disabled(!idle)
            }
        }
    }

    // MARK: Transform

    private var transformMenu: some Commands {
        CommandMenu("Transform") {
            tool("transform.move", "Move/Rotate", key: Self.key("m"))
            tool("transform.scale", "Scale")
            tool("transform.translate", "Translate", key: Self.key("n"))
            Menu("Pattern") {
                tool("transform.linear", "Pattern - Linear")
                tool("transform.circular", "Pattern - Circular")
                tool("transform.scatter", "Scatter")
            }
            tool("transform.rotate", "Rotate Around Axis")
            tool("transform.align", "Align")
            tool("transform.mirror", "Mirror")
        }
    }

    // MARK: Tools

    private var toolsMenu: some Commands {
        CommandMenu("Tools") {
            tool("tools.offset", "Offset Face")
            tool("tools.fillet", "Fillet", key: Self.key("f"))
            tool("tools.chamfer", "Chamfer")
            tool("tools.extrude", "Extrude", key: Self.key("e"))
            tool("tools.shell", "Shell", key: Self.key("h"))
            tool("tools.loft", "Loft")
            Divider()
            tool("tools.union", "Union", key: Self.key("u", .command))
            tool("tools.subtract", "Subtract", key: Self.key("b", .command))
            tool("tools.intersect", "Intersect", key: Self.key("i", .command))
            Divider()
            tool("tools.split", "Split Body")
            tool("tools.revolve", "Revolve", key: Self.key("v"))
            tool("tools.sweep", "Sweep", key: Self.key("w"))
            tool("tools.fill", "Fill (Beta)")
            tool("tools.replaceFace", "Replace Face")
            tool("tools.project", "Project as Edge")
            Button("Wrap & Emboss") { store?.embossSelection() }
                .disabled(!idle || (store?.selectedSketchItems.isEmpty ?? true))
            Divider()
            Button(ui?.visualizing == true ? "Exit Visualize" : "Visualize") { ui?.visualizing.toggle(); viewport?.redraw() }
                .disabled(!hasDocument || store?.sketch != nil)
            Divider()
            Button(viewport?.sectionEnabled == true ? "Hide Section View" : "Section View") { viewport?.sectionEnabled.toggle() }
                .disabled(!hasDocument)
            Button("Edit Section View") { viewport?.sectionEnabled = true }.disabled(!(viewport?.sectionEnabled ?? false))
            Button("2D Section View") { viewport?.lookAtSection() }.disabled(!hasDocument)
            Divider()
            Menu("Measure") {
                Menu("Recent") {
                    if let recent = viewport?.recentMeasurements, !recent.isEmpty {
                        ForEach(recent, id: \.self) { text in
                            Button(text) {
                                #if os(macOS)
                                NSPasteboard.general.clearContents()
                                NSPasteboard.general.setString(text, forType: .string)
                                #else
                                UIPasteboard.general.string = text
                                #endif
                            }
                        }
                        Divider()
                        Button("Clear Measurements") { viewport?.pointMeasurements = []; viewport?.recentMeasurements = [] }
                    } else {
                        Text("No recent measurements")
                    }
                }
                Divider()
                Button("Object Measurements") { viewport?.measuring = true }.disabled(!idle)
                Button("Point-to-Point Distance") { viewport?.measurePointToPoint() }.disabled(!idle)
                Button("3-Point Angle") { viewport?.measureThreePointAngle() }.disabled(!idle)
            }
            Divider()
            Button("Sketch") {
                guard let store else { return }
                if !store.selectedFaces.isEmpty { store.startSketchOnSelectedFace() }
                else if store.selectedItems.contains(where: \.isPlane) { store.startSketchOnSelectedPlane() }
                else { store.startSketch(on: .xy) }
            }
            .disabled(!idle)
        }
    }

    // MARK: View

    @CommandsBuilder
    private var viewCommands: some Commands {
        CommandGroup(before: .toolbar) {
            toggle("Show History Sidebar", { _, _, ui in ui.showHistory }) { _, _, ui, v in ui.showHistory = v }
            toggle("Show Items Sidebar", { _, _, ui in ui.showItems }) { _, _, ui, v in ui.showItems = v }
                .keyboardShortcut("s", modifiers: [.command, .option])
            Divider()
            toggle("Show Properties Sidebar", { _, _, ui in ui.showProperties }) { _, _, ui, v in ui.showProperties = v }
                .keyboardShortcut("p", modifiers: [.command, .option])
            Divider()
            let visualizing = ui?.visualizing ?? false
            Button("Material Properties") { ui?.visualizeTab = .material }.disabled(!visualizing)
            Button("Environment Properties") { ui?.visualizeTab = .environment }.disabled(!visualizing)
            Button("Camera Properties") { ui?.visualizeTab = .camera }.disabled(!visualizing)
            Button("Drawing Properties") { ui?.drawingProperties = true }.disabled(!(ui?.showDrawing ?? false))
            Divider()
            Button("Collapse All") {
                ui?.showHistory = false
                ui?.showItems = false
                ui?.showProperties = false
                ui?.openMenu = nil
            }
            .keyboardShortcut("l", modifiers: .option)
            .disabled(!hasDocument)
            Divider()
            view("Default View", .iso, "1")
            view("Front", .front, "2")
            view("Back", .back, "3")
            view("Top", .top, "4")
            view("Bottom", .bottom, "5")
            view("Right", .right, "6")
            view("Left", .left, "7")
            Button("Nearest Ortho View") { viewport?.snapToNearestOrtho() }.keyboardShortcut("8").disabled(!hasDocument)
            Menu("Rotate View") {
                Button("Rotate Left 90°") { viewport?.rotateView(yaw: -1) }
                Button("Rotate Right 90°") { viewport?.rotateView(yaw: 1) }
                Button("Rotate Up 90°") { viewport?.rotateView(pitch: 1) }
                Button("Rotate Down 90°") { viewport?.rotateView(pitch: -1) }
                Divider()
                Button("Zoom to Fit") { viewport?.fitAll() }.keyboardShortcut("f", modifiers: [.command, .shift])
            }
            .disabled(!hasDocument)
            Divider()
            Menu("Grid Position") {
                toggle("At Origin", { _, v, _ in !v.gridAtModelBottom }) { _, v, _, on in if on { v.gridAtModelBottom = false } }
                toggle("Under the Model", { _, v, _ in v.gridAtModelBottom }) { _, v, _, on in if on { v.gridAtModelBottom = true } }
            }
            .disabled(!hasDocument)
            toggle("Show Grid", { s, _, _ in s.settings.environment.showGrid }) { s, _, _, v in s.settings.environment.showGrid = v }
            toggle("Show World Axes", { _, v, _ in v.showWorldAxes }) { _, v, _, on in v.showWorldAxes = on }
            Divider()
            Button("Field of View…") { ui?.showFieldOfView = true }.disabled(!hasDocument)
            Divider()
            ForEach([DisplayMode.wireframe, .xray, .shaded, .visualized, .zebra, .curvature]) { mode in
                toggle(mode.title, { _, v, _ in v.displayMode == mode }) { _, v, _, _ in v.displayMode = mode }
                    .keyboardShortcut(KeyEquivalent(mode.key), modifiers: .option)
            }
            Divider()
            toggle("Show Edges", { _, v, _ in v.showEdges }) { _, v, _, on in v.showEdges = on }
            toggle("Show Sketches", { _, v, _ in v.showSketches }) { _, v, _, on in v.showSketches = on }
            toggle("Show Hidden Edges", { _, v, _ in v.showHiddenEdges }) { _, v, _, on in v.showHiddenEdges = on }
            toggle("Show Decals", { _, v, _ in v.showDecals }) { _, v, _, on in v.showDecals = on }
            Divider()
            toggle("Show Pinned Measurements", { _, v, _ in v.showPinnedMeasurements }) { _, v, _, on in v.showPinnedMeasurements = on }
            Divider()
            Button("Go to Modeling") {
                ui?.visualizing = false
                ui?.showDrawing = false
                if viewport?.displayMode == .visualized { viewport?.displayMode = .shaded }
                viewport?.redraw()
            }
            .disabled(!(ui?.visualizing ?? false) && !(ui?.showDrawing ?? false) && viewport?.displayMode != .visualized)
            Divider()
            Menu("Units") {
                ForEach(LengthUnit.allCases) { unit in
                    toggle(unit.title, { s, _, _ in s.settings.units == unit }) { s, _, _, _ in s.settings.units = unit }
                }
            }
            .disabled(!hasDocument)
            Menu("Foot & Inch Format") {
                ForEach(FootInchFormat.allCases) { f in
                    toggle(f.title, { _, _, _ in DisplayFormat.footInch == f }) { s, v, _, _ in
                        DisplayFormat.footInch = f
                        s.objectWillChange.send()
                        v.objectWillChange.send()
                    }
                }
            }
            .disabled(!(store?.settings.units.imperial ?? false))
            Menu("Degree Format") {
                ForEach(DegreeFormat.allCases) { f in
                    toggle(f.title, { _, _, _ in DisplayFormat.degrees == f }) { s, v, _, _ in
                        DisplayFormat.degrees = f
                        s.objectWillChange.send()
                        v.objectWillChange.send()
                    }
                }
            }
            .disabled(!hasDocument)
            let step = viewport.map { $0.fixedGrid ?? $0.gridStep } ?? 5
            let unit = store?.settings.units ?? .mm
            toggle("Lock Grid Size (\(formatLength(step, unit)))", { _, v, _ in v.fixedGrid != nil }) { _, v, _, on in
                v.fixedGrid = on ? v.gridStep : nil
            }
            Button("Tip: Adjust grid size by zooming") {}.disabled(true)
            Divider()
        }
    }

    private func view(_ title: String, _ preset: Camera.Preset, _ key: Character) -> some View {
        Button(title) { viewport?.look(preset) }
            .keyboardShortcut(KeyEquivalent(key), modifiers: .command)
            .disabled(!hasDocument)
    }
}
