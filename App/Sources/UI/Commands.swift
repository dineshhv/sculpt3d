import SwiftUI
import UniformTypeIdentifiers

/// The groups of the tool rail, menu bar and search palette.
enum ToolCategory: String, CaseIterable, Identifiable {
    case sketch, insert, construct, transform, tools, visualize, view

    var id: String { rawValue }
    var title: String { rawValue.capitalized }

    var symbol: String {
        switch self {
        case .sketch: return "pencil.and.outline"
        case .insert: return "plus.square"
        case .construct: return "square.on.square.dashed"
        case .transform: return "move.3d"
        case .tools: return "wrench.and.screwdriver"
        case .visualize: return "paintpalette"
        case .view: return "eye"
        }
    }

    /// Groups shown on the rail; View lives on the right-hand controls.
    static let rail: [ToolCategory] = [.sketch, .insert, .construct, .transform, .tools, .visualize]
}

/// Window-level UI state shared by the rail, panels and palette.
@MainActor
final class UIState: ObservableObject {
    @Published var openMenu: ToolCategory?
    @Published var showItems = false
    @Published var showHistory = true
    @Published var showSearch = false
    @Published var showProject = false
    @Published var importing = false
    @Published var exporting = false
    @Published var exportFile = ExchangeFile(data: Data())
    @Published var exportFormat = ExchangeFormat.step
    @Published var savingScreenshot = false
    @Published var screenshot = PNGFile(data: Data())
    @Published var importingImage = false
    @Published var importingProject = false
    @Published var showVariables = false
    @Published var showSnapSettings = false
    @Published var showGridSettings = false
    /// Visualize mode: materials, environment and camera, in place of modeling tools.
    @Published var visualizing = false
    @Published var visualizeTab: VisualizationPanel.Tab = .material
    /// The Properties sidebar: the selection's size, material and visibility.
    @Published var showProperties = false
    /// File ▸ New Drawing, and its properties inspector.
    @Published var showDrawing = false
    @Published var drawingProperties = true
    @Published var drawing = DrawingSettings()
    @Published var showFieldOfView = false
    /// Sketch ▸ Scale / Pattern / Rotate: the parameters sheet for that transform.
    @Published var sketchTransform: SketchTransformKind?
    @Published var showSketchText = false
    @Published var importingMaterial = false
    @Published var importingDecal = false
    /// Insert ▸ Terrain ▸ From Height Map.
    @Published var importingHeightMap = false
    /// File ▸ Publish Current Version: a self-contained web page of the model.
    @Published var publishing = false
    @Published var publishFile = WebPageFile(data: Data())
    @Published var publishName = "Model"
    /// File ▸ Export… and Export to Slicer…
    @Published var showExport = false
    @Published var exportToSlicer = false
    /// One export through the save panel, in any format.
    @Published var exportingAny = false
    @Published var anyExportFile = AnyExportFile(data: Data())
    @Published var anyExportType = UTType.data
    @Published var anyExportName = "Model"

    /// Exports `kind` through the save panel.
    func export(_ kind: ExportKind, from store: ModelStore) {
        guard let data = store.exportData(kind, drawing: drawing) else { return }
        anyExportFile = AnyExportFile(data: data)
        anyExportType = kind.contentType
        anyExportName = "\(store.documentName).\(kind.fileExtension)"
        exportingAny = true
    }
}

/// One runnable tool.
struct ToolCommand: Identifiable {
    let id: String
    let title: String
    let symbol: String
    let category: ToolCategory
    var hint = ""
    var isEnabled: (ModelStore) -> Bool = { _ in true }
    let run: (ModelStore, ViewportController, UIState) -> Void
}

@MainActor
enum ToolCatalog {
    static func idle(_ s: ModelStore) -> Bool { s.draft == nil && s.sketch == nil }

    static let all: [ToolCommand] = sketch + insert + construct + transform + tools + view

    static func commands(in category: ToolCategory) -> [ToolCommand] { all.filter { $0.category == category } }

    /// Viewport single-key shortcuts (outside sketches).
    static let keyShortcuts: [Character: String] = [
        "e": "tools.extrude", "v": "tools.revolve", "w": "tools.sweep", "f": "tools.fillet", "h": "tools.shell",
        "m": "transform.move", "n": "transform.translate", "k": "sketch.top",
    ]

    /// The key shown next to a command, if it has one.
    static func shortcut(for id: String) -> String? {
        if let key = keyShortcuts.first(where: { $0.value == id })?.key { return String(key).uppercased() }
        return [
            "tools.union": "⌘U", "tools.subtract": "⌘B", "tools.intersect": "⌘I", "transform.duplicate": "⌘D",
            "view.fit": "⇧⌘F", "view.measure": "⌥⌘M", "view.section": "⌥⌘S", "insert.import": "⇧⌘I",
        ][id]
    }

    static let sketch: [ToolCommand] = [
        ToolCommand(id: "sketch.top", title: "Sketch on Top Plane", symbol: "square.grid.3x3", category: .sketch,
                    hint: "XY", isEnabled: idle) { s, _, _ in s.startSketch(on: .xy) },
        ToolCommand(id: "sketch.front", title: "Sketch on Front Plane", symbol: "square.grid.3x3", category: .sketch,
                    hint: "XZ", isEnabled: idle) { s, _, _ in s.startSketch(on: .xz) },
        ToolCommand(id: "sketch.right", title: "Sketch on Right Plane", symbol: "square.grid.3x3", category: .sketch,
                    hint: "YZ", isEnabled: idle) { s, _, _ in s.startSketch(on: .yz) },
        ToolCommand(id: "sketch.face", title: "Sketch on Face", symbol: "square.and.pencil", category: .sketch,
                    hint: "Select a flat face", isEnabled: { idle($0) && !$0.selectedFaces.isEmpty }) { s, _, _ in s.startSketchOnSelectedFace() },
        ToolCommand(id: "sketch.plane", title: "Sketch on Plane", symbol: "square.on.square.dashed", category: .sketch,
                    hint: "Select a plane in Items", isEnabled: { idle($0) && $0.selectedItems.contains(where: \.isPlane) }) { s, _, _ in
            s.startSketchOnSelectedPlane()
        },
        ToolCommand(id: "sketch.edit", title: "Edit Sketch", symbol: "pencil", category: .sketch,
                    hint: "Select a sketch in Items", isEnabled: { idle($0) && $0.selectedItems.contains(where: \.isSketch) }) { s, _, _ in
            if let item = s.selectedItems.first(where: \.isSketch) { s.editSketch(item.id) }
        },
    ]

    static let insert: [ToolCommand] = [
        ToolCommand(id: "insert.variable", title: "Create Variable", symbol: "function", category: .insert,
                    hint: "Named values for any field") { s, _, ui in
            if s.settings.variables.isEmpty { s.addVariable() }
            ui.showVariables = true
        },
        ToolCommand(id: "insert.image", title: "Image", symbol: "photo.badge.plus", category: .insert,
                    hint: "On a plane or flat face", isEnabled: idle) { _, _, ui in ui.importingImage = true },
        ToolCommand(id: "insert.import", title: "File", symbol: "doc.badge.plus", category: .insert,
                    hint: "STEP, IGES, STL, BREP", isEnabled: idle) { _, _, ui in ui.importing = true },
        ToolCommand(id: "insert.project", title: "Project", symbol: "shippingbox", category: .insert,
                    hint: "Bodies from another Sculpt3D project", isEnabled: idle) { _, _, ui in ui.importingProject = true },
    ] + PrimitiveKind.allCases.map { kind in
        ToolCommand(id: "insert.\(kind.rawValue)", title: kind.title, symbol: kind.symbol, category: .insert, isEnabled: idle) { s, _, _ in
            s.addPrimitive(kind)
        }
    }

    static let construct: [ToolCommand] = [
        ToolCommand(id: "construct.offset", title: "Plane", symbol: "square.stack", category: .construct,
                    hint: "Offset from a face, a plane or Top", isEnabled: idle) { s, _, _ in s.addOffsetPlane() },
        ToolCommand(id: "construct.angled", title: "Angled Plane", symbol: "square.on.square.squareshape.controlhandles", category: .construct,
                    hint: "Turned about the base plane's X axis", isEnabled: idle) { s, _, _ in s.addAngledPlane() },
        ToolCommand(id: "construct.mid", title: "Midplane", symbol: "rectangle.split.2x1", category: .construct,
                    hint: "Select two flat faces", isEnabled: { idle($0) && $0.selectedFaces.count == 2 }) { s, _, _ in s.addMidplane() },
        ToolCommand(id: "construct.axis", title: "Axis", symbol: "line.diagonal", category: .construct,
                    hint: "Through a round face, along an edge, or Z", isEnabled: idle) { s, _, _ in s.addAxis() },
    ]

    static func hasBody(_ s: ModelStore) -> Bool { idle(s) && s.selectedBody != nil }

    static let transform: [ToolCommand] = [
        ToolCommand(id: "transform.move", title: "Move/Rotate", symbol: "move.3d", category: .transform,
                    hint: "Select a body", isEnabled: hasBody) { s, _, _ in s.moveSelection() },
        ToolCommand(id: "transform.scale", title: "Scale", symbol: "arrow.up.left.and.arrow.down.right", category: .transform,
                    hint: "Select a body", isEnabled: hasBody) { s, _, _ in s.scaleSelection() },
        ToolCommand(id: "transform.translate", title: "Translate", symbol: "arrow.right.square", category: .transform,
                    hint: "Along an axis by a distance", isEnabled: hasBody) { s, _, _ in s.translateSelection() },
        ToolCommand(id: "transform.linear", title: "Pattern", symbol: "square.grid.3x1.below.line.grid.1x2", category: .transform,
                    hint: "Linear copies", isEnabled: hasBody) { s, _, _ in s.linearPatternSelection() },
        ToolCommand(id: "transform.scatter", title: "Scatter", symbol: "circle.dotted", category: .transform,
                    hint: "Body to scatter, then the ground", isEnabled: { idle($0) && Set($0.selection.map(\.body)).count == 2 }) { s, _, _ in
            s.scatterSelection()
        },
        ToolCommand(id: "transform.circular", title: "Circular Pattern", symbol: "circle.grid.cross", category: .transform,
                    hint: "Copies around an axis", isEnabled: hasBody) { s, _, _ in s.circularPatternSelection() },
        ToolCommand(id: "transform.rotate", title: "Rotate Around Axis", symbol: "arrow.clockwise.circle", category: .transform,
                    hint: "A selected axis, or vertical", isEnabled: hasBody) { s, _, _ in s.rotateSelection() },
        ToolCommand(id: "transform.align", title: "Align", symbol: "align.horizontal.center", category: .transform,
                    hint: "Body to move, then target", isEnabled: { idle($0) && Set($0.selection.map(\.body)).count == 2 }) { s, _, _ in
            s.alignSelection()
        },
        ToolCommand(id: "transform.mirror", title: "Mirror", symbol: "arrow.left.and.right.righttriangle.left.righttriangle.right",
                    category: .transform, hint: "Select a body", isEnabled: hasBody) { s, _, _ in s.mirrorSelection() },
        ToolCommand(id: "transform.duplicate", title: "Duplicate", symbol: "plus.square.on.square", category: .transform,
                    hint: "Select a body", isEnabled: hasBody) { s, _, _ in s.duplicateSelection() },
    ]

    static let tools: [ToolCommand] = [
        ToolCommand(id: "tools.offset", title: "Offset Face", symbol: "arrow.up.square", category: .tools,
                    hint: "Select faces", isEnabled: { idle($0) && !$0.selectedFaces.isEmpty }) { s, _, _ in s.offsetFacesSelection() },
        ToolCommand(id: "tools.fillet", title: "Fillet", symbol: "circle.bottomhalf.filled", category: .tools,
                    hint: "Select edges", isEnabled: { idle($0) && !$0.selectedEdges.isEmpty }) { s, _, _ in s.filletSelection(chamfer: false) },
        ToolCommand(id: "tools.chamfer", title: "Chamfer", symbol: "triangle.bottomhalf.filled", category: .tools,
                    hint: "Select edges", isEnabled: { idle($0) && !$0.selectedEdges.isEmpty }) { s, _, _ in s.filletSelection(chamfer: true) },
        ToolCommand(id: "tools.extrude", title: "Extrude", symbol: "square.stack.3d.up", category: .tools,
                    hint: "A sketch in Items, or a flat face", isEnabled: { idle($0) && ($0.selectedItems.contains(where: \.isSketch) || $0.selectedFaces.count == 1) }) { s, _, _ in
            if let item = s.selectedItems.first(where: \.isSketch) { s.useSketchItem(item.id, revolve: false) } else { s.pushPullSelection() }
        },
        ToolCommand(id: "tools.shell", title: "Shell", symbol: "square.dashed", category: .tools,
                    hint: "Select a body or faces to open", isEnabled: hasBody) { s, _, _ in s.shellSelection() },
        ToolCommand(id: "tools.loft", title: "Loft", symbol: "cone", category: .tools,
                    hint: "Select 2+ sketches in order", isEnabled: idle) { s, _, _ in s.loftSelection() },
    ] + BooleanKind.allCases.map { kind in
        ToolCommand(id: "tools.\(kind.rawValue)", title: kind.title, symbol: kind.symbol, category: .tools,
                    hint: "Select two bodies", isEnabled: { idle($0) && Set($0.selection.map(\.body)).count == 2 }) { s, _, _ in s.boolean(kind) }
    } + [
        ToolCommand(id: "tools.split", title: "Split Body", symbol: "square.split.diagonal", category: .tools,
                    hint: "Select a body", isEnabled: hasBody) { s, _, _ in s.splitSelection() },
        ToolCommand(id: "tools.revolve", title: "Revolve", symbol: "arrow.triangle.2.circlepath", category: .tools,
                    hint: "Select a sketch in Items", isEnabled: { idle($0) && $0.selectedItems.contains(where: \.isSketch) }) { s, _, _ in
            if let item = s.selectedItems.first(where: \.isSketch) { s.useSketchItem(item.id, revolve: true) }
        },
        ToolCommand(id: "tools.sweep", title: "Sweep", symbol: "point.bottomleft.forward.to.point.topright.scurvepath", category: .tools,
                    hint: "Select profile, then path sketch", isEnabled: idle) { s, _, _ in s.sweepSelection() },
        ToolCommand(id: "tools.fill", title: "Fill", symbol: "square.fill.on.square", category: .tools,
                    hint: "Select a closed loop of edges", isEnabled: { idle($0) && !$0.selectedEdges.isEmpty }) { s, _, _ in s.fillSelection() },
        ToolCommand(id: "tools.replaceFace", title: "Replace Face", symbol: "square.2.layers.3d.top.filled", category: .tools,
                    hint: "Face to move, then target face", isEnabled: { idle($0) && $0.selectedFaces.count == 2 }) { s, _, _ in
            s.replaceFaceSelection()
        },
        ToolCommand(id: "tools.project", title: "Project as Edge", symbol: "rectangle.portrait.on.rectangle.portrait.angled", category: .tools,
                    hint: "A body onto a plane or face", isEnabled: hasBody) { s, _, _ in s.projectSelection() },
        ToolCommand(id: "tools.pushpull", title: "Push/Pull", symbol: "arrow.up.and.down.square", category: .tools,
                    hint: "Select a flat face", isEnabled: { idle($0) && $0.selectedFaces.count == 1 }) { s, _, _ in s.pushPullSelection() },
        ToolCommand(id: "tools.draft", title: "Draft", symbol: "trapezoid.and.line.vertical", category: .tools,
                    hint: "Select side faces", isEnabled: { idle($0) && !$0.selectedFaces.isEmpty }) { s, _, _ in s.draftSelection() },
        ToolCommand(id: "tools.deleteFaces", title: "Delete Faces", symbol: "eraser", category: .tools,
                    hint: "Heals the body around them", isEnabled: { idle($0) && !$0.selectedFaces.isEmpty }) { s, _, _ in
            s.deleteFacesSelection()
        },
        ToolCommand(id: "tools.deleteBody", title: "Delete Body", symbol: "trash", category: .tools,
                    hint: "Select bodies", isEnabled: hasBody) { s, _, _ in s.deleteSelection() },
    ]

    static let view: [ToolCommand] = [
        ToolCommand(id: "view.fit", title: "Zoom to Fit", symbol: "arrow.up.left.and.arrow.down.right", category: .view) { _, v, _ in v.fitAll() },
        ToolCommand(id: "view.iso", title: "Isometric View", symbol: "cube", category: .view) { _, v, _ in v.look(.iso) },
        ToolCommand(id: "view.top", title: "Top View", symbol: "square", category: .view) { _, v, _ in v.look(.top) },
        ToolCommand(id: "view.front", title: "Front View", symbol: "square", category: .view) { _, v, _ in v.look(.front) },
        ToolCommand(id: "view.right", title: "Right View", symbol: "square", category: .view) { _, v, _ in v.look(.right) },
        ToolCommand(id: "view.section", title: "Section View", symbol: "square.split.bottomrightquarter", category: .view) { _, v, _ in
            v.sectionEnabled.toggle()
        },
        ToolCommand(id: "view.measure", title: "Measure", symbol: "ruler", category: .view) { _, v, _ in v.measuring.toggle() },
        ToolCommand(id: "view.screenshot", title: "Screenshot", symbol: "camera", category: .view) { _, v, ui in
            guard let image = v.screenshot(), let data = PNGFile.encode(image) else { return }
            ui.screenshot = PNGFile(data: data)
            ui.savingScreenshot = true
        },
    ] + DisplayMode.allCases.map { mode in
        ToolCommand(id: "view.\(mode.rawValue)", title: mode.title, symbol: mode.symbol, category: .view) { _, v, _ in v.displayMode = mode }
    } + ExchangeFormat.allCases.map { format in
        ToolCommand(id: "export.\(format.rawValue)", title: "Export \(format.title)…", symbol: "square.and.arrow.up", category: .view) { s, _, ui in
            guard let data = s.export(format) else { return }
            ui.exportFormat = format
            ui.exportFile = ExchangeFile(data: data)
            ui.exporting = true
        }
    }
}
