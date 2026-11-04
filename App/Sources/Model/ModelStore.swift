import Foundation
import simd
import SwiftUI

enum SketchTool: String, CaseIterable, Identifiable {
    case select, line, arc, spline, controlSpline, rectangle, centerRectangle, circle, ellipse, polygon, point, trim

    var id: String { rawValue }

    var title: String {
        switch self {
        case .centerRectangle: return "Center Rect"
        case .controlSpline: return "Control Spline"
        case .arc: return "Arc"
        default: return rawValue.capitalized
        }
    }

    var symbol: String {
        switch self {
        case .select: return "cursorarrow"
        case .line: return "line.diagonal"
        case .arc: return "point.topleft.down.to.point.bottomright.curvepath"
        case .rectangle: return "rectangle"
        case .centerRectangle: return "rectangle.center.inset.filled"
        case .circle: return "circle"
        case .polygon: return "hexagon"
        case .point: return "smallcircle.filled.circle"
        case .trim: return "scissors"
        case .spline: return "scribble"
        case .controlSpline: return "point.topleft.down.to.point.bottomright.filled.curvepath"
        case .ellipse: return "oval"
        }
    }

    /// Tools drawn by dragging; the others place points by tapping.
    var drags: Bool { [.rectangle, .centerRectangle, .circle, .ellipse, .polygon].contains(self) }

    /// Single-key shortcut in the viewport.
    var key: Character? {
        switch self {
        case .select: return "s"
        case .line: return "l"
        case .arc: return "a"
        case .spline: return "i"
        case .rectangle: return "r"
        case .circle: return "c"
        case .ellipse: return "e"
        case .polygon: return "g"
        case .point: return "p"
        case .trim: return "t"
        case .centerRectangle, .controlSpline: return nil
        }
    }
}

/// An in-progress sketch. Geometry is in plane (u, v) coordinates.
struct SketchSession {
    var sketch: Sketch
    var target: Int?
    /// History feature whose sketch is being edited, if any.
    var replacing: UUID?
    var tool: SketchTool = .rectangle
    /// Outline of the rectangle or circle being dragged out.
    var preview: [SIMD2<Double>] = []
    /// Last point of the line chain being drawn.
    var chainEnd: Int?
    /// Points placed so far by a multi-tap tool (the arc's start and end).
    var pending: [SketchSnap] = []
    /// Points of the spline being drawn.
    var splineDraft: [Int] = []
    var polygonSides = 6
    /// Polygon drawn around the dragged circle (the drag point is an edge midpoint) instead of inside it.
    var polygonCircumscribed = false
    /// True while geometry is being dragged, so the drag is one undo step.
    var dragging = false
    /// New geometry is drawn as construction geometry.
    var constructionMode = false
    var offsetDistance: Double = 5
    var filletRadius: Double = 5
    var cursor: SIMD2<Double>?
    /// Snapping feedback for the cursor.
    var snapHint: String?
    var guides: [[SIMD2<Double>]] = []
    var selection: [SketchRef] = []
    var hover: SketchRef?
    var status = SketchStatus()

    var plane: SketchPlane {
        get { sketch.plane }
        set { sketch.plane = newValue }
    }
}

/// An operation being configured. It is applied live so the viewport previews it.
struct Draft {
    var feature: Feature
    var replacing: UUID?
    var sketch: SketchSession?
}

/// Owns the geometry kernel, the feature history and everything the UI shows.
@MainActor
final class ModelStore: ObservableObject {
    @Published private(set) var features: [Feature]
    @Published private(set) var meshes: [Int: BodyMesh] = [:]
    @Published private(set) var errors: [UUID: String] = [:]
    @Published private(set) var geometryVersion = 0
    @Published var draft: Draft?
    @Published var selection: [Pick] = []
    /// The face or edge under the pointer. Not published: only the viewport
    /// draws it (and redraws itself), so mouse moves don't re-render SwiftUI.
    var hover: Pick?
    @Published var sketch: SketchSession? {
        didSet {
            guard let new = sketch else {
                sketchUndo = []
                sketchRedo = []
                return
            }
            // Every geometry change is one undo step, except the steps of a
            // drag, which beginSketchDrag records once up front.
            if let old = oldValue, old.sketch != new.sketch, !old.dragging, !restoringSketch {
                sketchUndo.append(old.sketch)
                sketchRedo = []
            }
        }
    }
    private(set) var sketchUndo: [Sketch] = []
    private(set) var sketchRedo: [Sketch] = []
    private var restoringSketch = false
    @Published var toast: String?
    /// The document's name and file, for titles, sharing and links.
    @Published var documentName = "Untitled"
    @Published var documentURL: URL?
    /// Hidden bodies from before Item ▸ Isolate, restored by Exit Isolation.
    @Published var isolationRestore: Set<Int>?
    /// Sketches and construction planes in the history, for display and picking.
    @Published private(set) var referenceItems: [ReferenceItem] = []
    /// Selected sketch and plane items, in the order they were picked.
    @Published var itemSelection: [UUID] = []
    @Published var settings: ProjectSettings {
        didSet {
            guard settings != oldValue else { return }
            onSettingsChanged(settings)
            if settings.variables != oldValue.variables { regenerate() } else { geometryVersion += 1 }
        }
    }
    var onSettingsChanged: (ProjectSettings) -> Void = { _ in }
    /// Sketches by feature id while the history replays (for sweep and loft).
    private var replaySketches: [UUID: Sketch] = [:]

    /// The model after each step of the last rebuild, so the next rebuild can
    /// resume after the longest unchanged prefix instead of replaying everything.
    private struct Checkpoint {
        let feature: Feature
        let state: O3DKernelState
        let sketches: [UUID: Sketch]
        let items: [ReferenceItem]
        let errors: [UUID: String]
        let origins: [Int: (source: Int, first: Int)]
    }
    private var checkpoints: [Checkpoint] = []
    /// Meshes of the last rebuild by body, with the shape they were made from;
    /// `meshState` keeps those shapes alive so a key can't be reused by a new shape.
    private var meshKeys: [Int: UInt64] = [:]
    private var meshState: O3DKernelState?
    /// Scatter copies from the last rebuild: copy id → its source and the first copy,
    /// whose appearance new copies take on.
    private(set) var scatterOrigins: [Int: (source: Int, first: Int)] = [:]
    /// Volume, area and bounds by body, with the shape key they were measured
    /// on; integrating them is slow, and panels and MCP replies ask often.
    private var propertiesCache: [Int: (key: UInt64, value: O3DProperties)] = [:]

    let kernel = O3DKernel()
    var onFeaturesChanged: ([Feature]) -> Void = { _ in }
    private var undoStack: [[Feature]] = []
    private var redoStack: [[Feature]] = []

    init(features: [Feature], settings: ProjectSettings = ProjectSettings()) {
        self.features = features
        self.settings = settings
        regenerate()
    }

    var canUndo: Bool { !undoStack.isEmpty }
    var canRedo: Bool { !redoStack.isEmpty }
    var bodyIds: [Int] { meshes.keys.sorted() }

    var draftError: String? { draft.flatMap { errors[$0.feature.id] } }

    // MARK: History

    func commit(_ newFeatures: [Feature]) {
        undoStack.append(features)
        redoStack.removeAll()
        features = newFeatures
        onFeaturesChanged(features)
        regenerate()
    }

    func undo() {
        guard let previous = undoStack.popLast() else { return }
        redoStack.append(features)
        features = previous
        draft = nil
        onFeaturesChanged(features)
        regenerate()
    }

    func redo() {
        guard let next = redoStack.popLast() else { return }
        undoStack.append(features)
        features = next
        draft = nil
        onFeaturesChanged(features)
        regenerate()
    }

    func toggleSuppressed(_ id: UUID) {
        commit(features.map { f in
            var f = f
            if f.id == id { f.suppressed.toggle() }
            return f
        })
    }

    func deleteFeature(_ id: UUID) {
        commit(features.filter { $0.id != id })
    }

    /// Replays the full history (plus the draft) through a fresh kernel.
    func regenerate() {
        draftRebuildPending = false
        var list = features
        // While a feature's sketch is edited, show the model as it was before it.
        if let r = sketch?.replacing, let i = list.firstIndex(where: { $0.id == r }) {
            list = Array(list[..<i])
        }
        if let draft {
            if let r = draft.replacing, let i = list.firstIndex(where: { $0.id == r }) {
                list[i] = draft.feature
            } else {
                list.append(draft.feature)
            }
        }
        var newErrors: [UUID: String] = [:]
        var items: [ReferenceItem] = []
        replaySketches = [:]
        scatterOrigins = [:]
        let evaluator = ExpressionEvaluator(settings.variables)
        let resolved = list.map { f -> Feature in
            var g = f
            g.op = f.resolved(evaluator)
            return g
        }
        var start = 0
        while start < min(resolved.count, checkpoints.count), checkpoints[start].feature == resolved[start] { start += 1 }
        if start > 0 {
            let c = checkpoints[start - 1]
            kernel.restore(c.state)
            replaySketches = c.sketches
            items = c.items
            newErrors = c.errors
            scatterOrigins = c.origins
        } else {
            kernel.reset()
        }
        checkpoints.removeSubrange(start...)
        for f in resolved[start...] {
            defer {
                checkpoints.append(Checkpoint(feature: f, state: kernel.saveState(), sketches: replaySketches, items: items,
                                              errors: newErrors, origins: scatterOrigins))
            }
            if f.suppressed { continue }
            let op = f.op
            switch op {
            case let .sketch(sketch, name):
                replaySketches[f.id] = sketch
                items.append(ReferenceItem(id: f.id, name: name, content: .sketch(sketch)))
            case let .plane(base, offset, angle, name):
                items.append(ReferenceItem(id: f.id, name: name, content: .plane(base.transformed(offset: offset, angle: angle))))
            case let .axis(origin, direction, name):
                items.append(ReferenceItem(id: f.id, name: name, content: .axis(origin: origin, direction: direction)))
            case let .image(image):
                items.append(ReferenceItem(id: f.id, name: image.name, content: .image(image)))
            case let .projection(body, plane, name):
                // Edges as they are at this point in the history, as sketch lines.
                var sketch = Sketch(plane: plane)
                for data in kernel.projectEdges(body, plane: plane.bridged) {
                    let pts = data.withUnsafeBytes { Array($0.bindMemory(to: SIMD2<Double>.self)) }
                    let ids = pts.map { sketch.addPoint($0) }
                    for (a, b) in zip(ids, ids.dropFirst()) { sketch.addLine(a, b) }
                }
                if sketch.isEmpty { newErrors[f.id] = "Nothing to project" }
                replaySketches[f.id] = sketch
                items.append(ReferenceItem(id: f.id, name: name, content: .sketch(sketch)))
            default:
                if let error = apply(op) { newErrors[f.id] = error }
            }
        }
        errors = newErrors
        referenceItems = items
        itemSelection = itemSelection.filter { id in items.contains { $0.id == id } }

        var newMeshes: [Int: BodyMesh] = [:]
        var keys: [Int: UInt64] = [:]
        for id in kernel.bodies.map(\.intValue) {
            let key = kernel.shapeKey(id)
            keys[id] = key
            if key != 0, meshKeys[id] == key, let old = meshes[id] {
                newMeshes[id] = old
            } else if let mesh = kernel.tessellate(id, quality: 1) {
                var m = BodyMesh(id: id, mesh: mesh)
                m.shapeKey = key
                newMeshes[id] = m
            }
        }
        meshKeys = keys
        meshState = kernel.saveState()
        // Only keys of live shapes (kept alive by meshState) stay, so a freed
        // shape's key can't be mistaken for a new one.
        propertiesCache = propertiesCache.filter { keys[$0.key] == $0.value.key }
        meshes = newMeshes
        // Presentation is keyed by body id; forget ids the settled history no
        // longer has, so a later body with the same id starts plain.
        if draft == nil, sketch == nil {
            let live = Set(newMeshes.keys)
            var pruned = settings
            pruned.bodyNames = pruned.bodyNames.filter { live.contains($0.key) }
            for (id, origin) in scatterOrigins where pruned.appearance[id] == nil {
                if let look = pruned.appearance[origin.source] ?? pruned.appearance[origin.first] { pruned.appearance[id] = look }
            }
            pruned.appearance = pruned.appearance.filter { live.contains($0.key) }
            pruned.hiddenBodies = pruned.hiddenBodies.intersection(live)
            if pruned != settings { settings = pruned }
        }
        selection = selection.filter { pick in
            guard let m = newMeshes[pick.body] else { return false }
            switch pick {
            case .body: return true
            case let .face(_, i): return i < m.faceCount
            case let .edge(_, i): return i < m.edgeCount
            }
        }
        hover = nil
        geometryVersion += 1
    }

    /// Applies one operation to the kernel; returns an error message on failure.
    private func apply(_ op: Operation) -> String? {
        let k = kernel
        let ok: Bool
        switch op {
        case let .primitive(kind, dims, position):
            ok = k.primitive(kind.bridged, dims: dims, position: position) > 0
        case let .extrude(sketch, distance, mode, target):
            // A cut's distance is its depth into the material, against the sketch normal.
            ok = k.extrude(sketch.plane.bridged, profiles: sketch.profiles,
                           distance: mode == .cut ? -distance : distance, mode: mode.bridged, target: target ?? -1) > 0
        case let .revolve(sketch, angle, aroundV, mode, target):
            ok = k.revolve(sketch.plane.bridged, profiles: sketch.profiles,
                           aroundV: aroundV, angle: angle, mode: mode.bridged, target: target ?? -1) > 0
        case let .pushPull(body, face, distance):
            ok = k.pushPull(body, face: face, distance: distance)
        case let .boolean(kind, target, tool):
            ok = k.boolean(target, tool: tool, op: kind.bridged)
        case let .fillet(body, edges, radius):
            ok = k.fillet(body, edges: edges.map { NSNumber(value: $0) }, radius: radius)
        case let .chamfer(body, edges, distance):
            ok = k.chamfer(body, edges: edges.map { NSNumber(value: $0) }, distance: distance)
        case let .shell(body, faces, thickness):
            ok = k.shell(body, removeFaces: faces.map { NSNumber(value: $0) }, thickness: thickness)
        case let .move(body, t, axis, angle):
            ok = k.transform(body, translation: t, axis: axis, angle: angle)
        case let .mirror(body, plane, keep):
            ok = k.mirror(body, plane: plane.bridged, keepOriginal: keep) > 0
        case let .duplicate(body):
            ok = k.duplicate(body) > 0
        case let .delete(body):
            ok = k.remove(body)
        case let .imported(_, brep):
            ok = k.body(fromBrep: brep) > 0
        case .sketch, .plane, .axis, .image, .projection:
            ok = true
        case let .translate(body, direction, distance):
            ok = k.transform(body, translation: simd_normalize(direction) * distance, axis: [0, 0, 1], angle: 0)
        case let .rotate(body, origin, axis, angle):
            ok = k.rotate(body, axisOrigin: origin, axisDir: axis, angle: angle)
        case let .align(body, target, modes):
            guard let a = k.properties(body), let b = k.properties(target) else { return "Align needs two bodies" }
            var t = SIMD3<Double>.zero
            for i in 0..<3 where modes.indices.contains(i) {
                switch modes[i] {
                case 1: t[i] = b.bboxMin[i] - a.bboxMin[i]
                case 2: t[i] = (b.bboxMin[i] + b.bboxMax[i]) / 2 - (a.bboxMin[i] + a.bboxMax[i]) / 2
                case 3: t[i] = b.bboxMax[i] - a.bboxMax[i]
                default: break
                }
            }
            ok = k.transform(body, translation: t, axis: [0, 0, 1], angle: 0)
        case let .replaceFace(body, face, targetBody, targetFace):
            ok = k.replaceFace(body, face: face, withBody: targetBody, face: targetFace)
        case let .fill(body, edges):
            ok = k.fill(body, edges: edges.map { NSNumber(value: $0) }) > 0
        case let .sweep(profile, path, mode, target):
            guard let p = replaySketches[profile], let q = replaySketches[path] else { return "The sweep's sketches are missing" }
            ok = k.sweep(p.plane.bridged, profiles: p.profiles, pathPlane: q.plane.bridged, path: q.path,
                         mode: mode.bridged, target: target ?? -1) > 0
        case let .loft(sections, mode, target):
            let sketches = sections.compactMap { replaySketches[$0] }
            guard sketches.count == sections.count, sketches.count >= 2 else { return "The loft's sketches are missing" }
            let profiles = sketches.compactMap { $0.profiles.first }
            guard profiles.count == sketches.count else { return "Every loft sketch needs a closed profile" }
            ok = k.loft(sketches.map(\.plane.bridged), sections: profiles, mode: mode.bridged, target: target ?? -1) > 0
        case let .scale(body, factor, center):
            ok = k.scale(body, factor: factor, center: center)
        case let .terrain(t):
            let data = t.topHeights.withUnsafeBufferPointer { Data(buffer: $0) }
            ok = k.terrain(t.center, width: t.width, depth: t.depth, nx: t.nx, ny: t.ny, heights: data) > 0
        case let .scaleAxes(body, factors, center, _):
            ok = k.scaleAxes(body, factors: factors, center: center)
        case let .scatter(source, target, avoid, p):
            let ids = k.scatter(source, onto: target, avoid: avoid.map { NSNumber(value: $0) }, count: p.count, seed: p.seed,
                                minScale: p.minScale, maxScale: p.maxScale, spacing: p.spacing, maxSlope: p.maxSlope,
                                sink: p.sink, randomRotation: p.randomRotation, alignToSurface: p.alignToSurface,
                                keepOriginal: p.keepOriginal).map(\.intValue)
            ok = !ids.isEmpty
            if let first = ids.first { for id in ids { scatterOrigins[id] = (source, first) } }
        case let .emboss(sketchId, body, height, deboss):
            guard let sk = replaySketches[sketchId] else { return "The emboss sketch is missing" }
            ok = k.emboss(body, plane: sk.plane.bridged, profiles: sk.profiles, height: height, deboss: deboss)
        case let .linearPattern(body, direction, count, spacing, fuse):
            ok = !k.linearPattern(body, direction: direction, count: count, spacing: spacing, fuse: fuse).isEmpty
        case let .circularPattern(body, origin, axis, count, angle, fuse):
            ok = !k.circularPattern(body, axisOrigin: origin, axisDir: axis, count: count, angle: angle, fuse: fuse).isEmpty
        case let .split(body, plane, offset):
            ok = !k.split(body, plane: plane.transformed(offset: offset, angle: 0).bridged).isEmpty
        case let .deleteFaces(body, faces):
            ok = k.deleteFaces(body, faces: faces.map { NSNumber(value: $0) })
        case let .draft(body, faces, pull, angle):
            ok = k.draft(body, faces: faces.map { NSNumber(value: $0) }, pull: pull, angle: angle)
        case let .offsetFaces(body, faces, distance):
            ok = k.offsetFaces(body, faces: faces.map { NSNumber(value: $0) }, distance: distance)
        }
        return ok ? nil : (k.lastError.isEmpty ? "Operation failed" : k.lastError)
    }

    // MARK: Drafts

    func begin(_ op: Operation, sketch: SketchSession? = nil, replacing: UUID? = nil) {
        // Face and edge indices change as soon as the preview alters topology.
        selection = []
        var feature = Feature(op: op)
        if let replacing, let existing = features.first(where: { $0.id == replacing }) {
            feature = existing
            feature.op = op
        }
        draft = Draft(feature: feature, replacing: replacing, sketch: sketch)
        self.sketch = nil
        regenerate()
    }

    func edit(_ id: UUID) {
        guard let f = features.first(where: { $0.id == id }) else { return }
        sketch = nil
        draft = Draft(feature: f, replacing: id)
        regenerate()
    }

    /// Panel sliders send many changes per frame; rebuild once per run-loop
    /// turn with the latest value instead of once per change.
    private var draftRebuildPending = false

    func updateDraft(_ op: Operation) {
        guard draft != nil else { return }
        draft?.feature.op = op
        guard !draftRebuildPending else { return }
        draftRebuildPending = true
        DispatchQueue.main.async { [weak self] in
            guard let self, self.draftRebuildPending else { return }
            self.regenerate()
        }
    }

    func applyDraft() {
        if draftRebuildPending { regenerate() }
        guard let draft else { return }
        if let error = errors[draft.feature.id] {
            toast = error
            return
        }
        var list = features
        if let r = draft.replacing, let i = list.firstIndex(where: { $0.id == r }) {
            list[i] = draft.feature
        } else {
            list.append(draft.feature)
        }
        self.draft = nil
        selection = []
        commit(list)
    }

    func cancelDraft() {
        let restored = draft?.sketch
        draft = nil
        sketch = restored
        regenerate()
    }

    /// Body appearance, with scatter copies shown like their source while
    /// they are still a preview.
    var displayAppearance: [Int: Appearance] {
        var looks = settings.appearance
        for (id, origin) in scatterOrigins where looks[id] == nil {
            if let look = looks[origin.source] ?? looks[origin.first] { looks[id] = look }
        }
        return looks
    }

    func commitImmediately(_ op: Operation) {
        // Trial-run as a draft so a failing operation never enters the history.
        let f = Feature(op: op)
        draft = Draft(feature: f)
        regenerate()
        draft = nil
        if let error = errors[f.id] {
            toast = error
            regenerate()
            return
        }
        selection = []
        commit(features + [f])
    }

    // MARK: Selection

    var selectedBodies: [Int] {
        selection.compactMap { if case let .body(b) = $0 { return b } else { return nil } }
    }

    var selectedFaces: [(body: Int, index: Int)] {
        selection.compactMap { if case let .face(b, i) = $0 { return (b, i) } else { return nil } }
    }

    var selectedEdges: [(body: Int, index: Int)] {
        selection.compactMap { if case let .edge(b, i) = $0 { return (b, i) } else { return nil } }
    }

    func select(_ pick: Pick?, additive: Bool) {
        guard let pick else {
            if !additive { selection = [] }
            return
        }
        if let i = selection.firstIndex(of: pick) {
            selection.remove(at: i)
            return
        }
        // Edges and faces of the same body accumulate naturally, which is what
        // fillet and shell need; anything else replaces the selection.
        let sameKind = selection.allSatisfy { existing in
            switch (existing, pick) {
            case (.edge(let b1, _), .edge(let b2, _)), (.face(let b1, _), .face(let b2, _)): return b1 == b2
            case (.body, .body): return additive
            default: return false
            }
        }
        selection = (additive || sameKind) ? selection + [pick] : [pick]
    }

    func faceInfo(_ body: Int, _ face: Int) -> O3DFaceInfo? { kernel.faceInfo(body, face: face) }

    func properties(_ body: Int) -> O3DProperties? {
        let key = kernel.shapeKey(body)
        if key != 0, meshKeys[body] == key, let c = propertiesCache[body], c.key == key { return c.value }
        let p = kernel.properties(body)
        if let p, key != 0, meshKeys[body] == key { propertiesCache[body] = (key, p) }
        return p
    }

    // MARK: Commands

    func addPrimitive(_ kind: PrimitiveKind) {
        selection = []
        begin(.primitive(kind: kind, dims: kind.defaultDims, position: .zero))
    }

    func startSketch(on plane: SketchPlane, target: Int? = nil) {
        draft = nil
        selection = []
        sketch = SketchSession(sketch: Sketch(plane: plane), target: target)
        regenerate()
    }

    func startSketchOnSelectedFace() {
        guard let face = selectedFaces.first, let info = faceInfo(face.body, face.index), info.planar else {
            toast = "Select a flat face to sketch on"
            return
        }
        startSketch(on: SketchPlane(origin: info.centroid, normal: info.normal, xDir: info.xDir), target: face.body)
    }

    /// Reopens the sketch of an extrude or revolve feature for editing.
    func editSketch(_ id: UUID) {
        guard let f = features.first(where: { $0.id == id }), var sk = f.op.sketch else { return }
        draft = nil
        selection = []
        var session = SketchSession(sketch: sk, target: f.op.sketchTarget, replacing: id, tool: .select)
        if let status = sk.solve() {
            session.sketch = sk
            session.status = status
        }
        sketch = session
        regenerate()
    }

    func finishSketch(revolve: Bool) {
        guard var session = sketch else { return }
        session.preview = []
        session.chainEnd = nil
        session.sketch.delete([])
        guard session.sketch.profileCount > 0 else {
            toast = "Draw a closed shape first"
            return
        }
        let sk = session.sketch
        if let r = session.replacing, let existing = features.first(where: { $0.id == r }) {
            if case .sketch = existing.op {
                sketch = nil
                commit(features.map { $0.id == r ? Feature(id: r, op: existing.op.setting(sketch: sk), suppressed: $0.suppressed) : $0 })
                return
            }
            begin(existing.op.setting(sketch: sk), sketch: session, replacing: r)
            return
        }
        let mode: CombineMode = session.target == nil ? .newBody : .join
        if revolve {
            begin(.revolve(sketch: sk, angle: 360, aroundV: true, mode: mode, target: session.target), sketch: session)
        } else {
            begin(.extrude(sketch: sk, distance: 20, mode: mode, target: session.target), sketch: session)
        }
    }

    /// Keeps the sketch as an item of its own, for sweeps, lofts and later use.
    func finishSketchAsItem() {
        guard var session = sketch else { return }
        session.sketch.delete([])
        guard !session.sketch.isEmpty else { return cancelSketch() }
        if session.replacing != nil { return finishSketch(revolve: false) }
        let count = features.filter { if case .sketch = $0.op { return true } else { return false } }.count
        sketch = nil
        commit(features + [Feature(op: .sketch(sketch: session.sketch, name: "Sketch \(count + 1)"))])
    }

    func cancelSketch() {
        let wasEditing = sketch?.replacing != nil
        sketch = nil
        if wasEditing { regenerate() }
    }

    func setSketchTool(_ tool: SketchTool) {
        guard var s = sketch else { return }
        endChain(&s)
        s.tool = tool
        s.preview = []
        s.cursor = nil
        sketch = s
    }

    /// Escape ends the line being drawn, then clears the selection, then
    /// leaves the sketch.
    func sketchEscape() {
        guard var s = sketch else { return }
        if s.chainEnd != nil || !s.pending.isEmpty || !s.splineDraft.isEmpty {
            endChain(&s)
            sketch = s
        } else if !s.selection.isEmpty {
            s.selection = []
            sketch = s
        } else {
            cancelSketch()
        }
    }

    private func endChain(_ s: inout SketchSession) {
        s.pending = []
        if s.splineDraft.count >= 2 {
            s.sketch.splines.append(SketchSpline(points: s.splineDraft, construction: s.constructionMode))
            s.splineDraft = []
            s.resolve()
        }
        s.splineDraft = []
        guard s.chainEnd != nil else { return }
        s.chainEnd = nil
        // A chain that never got a second point leaves a stray point behind.
        let count = s.sketch.points.count
        s.sketch.delete([])
        if s.sketch.points.count != count {
            s.selection = []
            s.hover = nil
            s.resolve()
        }
    }

    func beginSketchDrag() {
        guard let s = sketch, !s.dragging else { return }
        sketchUndo.append(s.sketch)
        sketchRedo = []
        sketch?.dragging = true
    }

    func endSketchDrag() {
        sketch?.dragging = false
    }

    var canUndoSketch: Bool { sketch != nil && !sketchUndo.isEmpty }
    var canRedoSketch: Bool { sketch != nil && !sketchRedo.isEmpty }

    func undoSketch() {
        guard var s = sketch, let previous = sketchUndo.popLast() else { return }
        sketchRedo.append(s.sketch)
        restore(&s, previous)
    }

    func redoSketch() {
        guard var s = sketch, let next = sketchRedo.popLast() else { return }
        sketchUndo.append(s.sketch)
        restore(&s, next)
    }

    private func restore(_ s: inout SketchSession, _ geometry: Sketch) {
        s.sketch = geometry
        s.selection = []
        s.hover = nil
        s.chainEnd = nil
        s.pending = []
        s.resolve()
        restoringSketch = true
        sketch = s
        restoringSketch = false
    }

    // MARK: Sketch constraints

    /// Applies a constraint to the selected sketch geometry.
    func constrainSelection(_ kind: SketchConstraint.Kind) {
        guard var s = sketch else { return }
        if let error = s.constrain(kind) { toast = error } else { sketch = s }
    }

    func setDimension(_ index: Int, to value: Double) {
        guard var s = sketch else { return }
        if let error = s.setDimension(index, to: value) { toast = error } else { sketch = s }
    }

    func deleteSketchSelection() {
        guard var s = sketch, !s.selection.isEmpty else { return }
        s.sketch.delete(Set(s.selection))
        s.selection = []
        s.hover = nil
        s.chainEnd = nil
        s.resolve()
        sketch = s
    }

    /// Horizontal or vertical for each selected line, whichever it is closer to.
    func constrainHorizontalOrVertical() {
        guard var s = sketch else { return }
        let lines = s.selectedLines
        for l in lines {
            let d = s.sketch.direction(l)
            s.selection = [.line(l)]
            if let error = s.constrain(abs(d.x) >= abs(d.y) ? .horizontal : .vertical) { toast = error }
        }
        s.selection = []
        sketch = s
    }

    func offsetSketchSelection() {
        guard var s = sketch else { return }
        let refs = s.selection.filter { if case .constraint = $0 { return false } else { return true } }
        let created = s.sketch.offset(refs, distance: s.offsetDistance)
        guard !created.isEmpty else { return toast = "Select lines, arcs or circles to offset" }
        s.selection = created
        s.resolve()
        sketch = s
    }

    func filletSketchSelection() {
        guard var s = sketch else { return }
        // A selected corner point, or the point two selected lines share.
        var corner = s.selectedPoints.first
        if corner == nil, s.selectedLines.count == 2 {
            let a = s.sketch.lines[s.selectedLines[0]], b = s.sketch.lines[s.selectedLines[1]]
            corner = [a.a, a.b].first { $0 == b.a || $0 == b.b }
        }
        guard let corner else { return toast = "Select a corner, or two lines that meet" }
        if let error = s.sketch.fillet(corner: corner, radius: s.filletRadius) { return toast = error }
        s.selection = []
        s.resolve()
        sketch = s
    }

    func disconnectSketchSelection() {
        guard var s = sketch, let p = s.selectedPoints.first else { return }
        s.sketch.disconnect(p)
        s.selection = []
        s.resolve()
        sketch = s
    }

    func toggleConstruction() {
        guard var s = sketch else { return }
        for r in s.selection {
            switch r {
            case let .line(i): s.sketch.lines[i].construction.toggle()
            case let .circle(i): s.sketch.circles[i].construction.toggle()
            case let .spline(i): s.sketch.splines[i].construction.toggle()
            case let .ellipse(i): s.sketch.ellipses[i].construction.toggle()
            default: break
            }
        }
        sketch = s
    }

    func pushPullSelection() {
        guard let f = selectedFaces.first else { return }
        guard faceInfo(f.body, f.index)?.planar == true else {
            toast = "Push/pull needs a flat face"
            return
        }
        begin(.pushPull(body: f.body, face: f.index, distance: 10))
    }

    func filletSelection(chamfer: Bool) {
        let edges = selectedEdges
        guard let body = edges.first?.body else { return }
        let ids = edges.filter { $0.body == body }.map(\.index)
        begin(chamfer ? .chamfer(body: body, edges: ids, distance: 2) : .fillet(body: body, edges: ids, radius: 3))
    }

    func shellSelection() {
        if let body = selectedFaces.first?.body {
            begin(.shell(body: body, faces: selectedFaces.filter { $0.body == body }.map(\.index), thickness: 2))
        } else if let body = selectedBodies.first {
            begin(.shell(body: body, faces: [], thickness: 2))
        }
    }

    func moveSelection() {
        guard let body = selection.first?.body else { return }
        begin(.move(body: body, translation: .zero, axis: [0, 0, 1], angle: 0))
    }

    func duplicateSelection() {
        guard let body = selection.first?.body else { return }
        commitImmediately(.duplicate(body: body))
    }

    func deleteSelection() {
        let bodies = Set(selection.map(\.body))
        guard !bodies.isEmpty else { return }
        let list = features + bodies.sorted().map { Feature(op: .delete(body: $0)) }
        selection = []
        commit(list)
    }

    func boolean(_ kind: BooleanKind) {
        let bodies = selection.map(\.body).reduce(into: [Int]()) { if !$0.contains($1) { $0.append($1) } }
        guard bodies.count == 2 else {
            toast = "Select two bodies"
            return
        }
        commitImmediately(.boolean(kind: kind, target: bodies[0], tool: bodies[1]))
    }

    // MARK: Exchange

    func importFile(_ url: URL) {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        let scratch = O3DKernel()
        let ids = scratch.importFile(url.path)
        guard !ids.isEmpty else {
            toast = scratch.lastError.isEmpty ? "Import failed" : scratch.lastError
            return
        }
        let name = url.deletingPathExtension().lastPathComponent
        let imported = ids.compactMap { scratch.brep(forBody: $0.intValue) }.map { Feature(op: .imported(name: name, brep: $0)) }
        commit(features + imported)
    }

    func export(_ format: ExchangeFormat) -> Data? {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("sculpt3d-export.\(format.rawValue)")
        try? FileManager.default.removeItem(at: url)
        let bodies = selection.isEmpty ? bodyIds : Array(Set(selection.map(\.body))).sorted()
        guard !bodies.isEmpty else {
            toast = "Nothing to export"
            return nil
        }
        guard kernel.exportFile(url.path, bodies: bodies.map { NSNumber(value: $0) }) else {
            toast = kernel.lastError.isEmpty ? "Export failed" : kernel.lastError
            return nil
        }
        return try? Data(contentsOf: url)
    }
}
