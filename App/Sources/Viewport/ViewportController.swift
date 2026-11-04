import Combine
import MetalKit
import simd

/// Platform-neutral viewport logic: camera, picking, sketch input and frame
/// assembly. The platform views translate raw input into these calls.
@MainActor
final class ViewportController: NSObject, ObservableObject, MTKViewDelegate {
    let store: ModelStore
    @Published var camera = Camera()
    /// Fixed grid spacing in mm; nil follows the zoom.
    @Published var fixedGrid: Double? { didSet { redraw() } }
    @Published var showConstraints = true
    @Published var measureDeltas = false
    @Published var pinnedMeasurements: [String] = []
    /// Window UI state, for keyboard shortcuts that open panels or dialogs.
    weak var ui: UIState?
    @Published var displayMode: DisplayMode = .shaded { didSet { redraw() } }
    // View menu toggles.
    @Published var showEdges = true { didSet { redraw() } }
    @Published var showHiddenEdges = false { didSet { redraw() } }
    @Published var showSketches = true { didSet { redraw() } }
    @Published var showDecals = true { didSet { redraw() } }
    @Published var showPinnedMeasurements = true
    @Published var showWorldAxes = true { didSet { redraw() } }
    /// Grid at the model's lowest point instead of at the origin.
    @Published var gridAtModelBottom = false { didSet { redraw() } }
    /// Point picks for construction and measuring (Construct ▸ Plane Through 3 Points, …).
    @Published var pointPick: PointPickSession? { didSet { redraw() } }
    /// Point-to-point and angle measurements, shown in the viewport.
    @Published var pointMeasurements: [PointMeasurement] = []
    /// Recent measurement results, newest first (Tools ▸ Measure ▸ Recent).
    @Published var recentMeasurements: [String] = []
    /// Section view: cuts away everything beyond a plane normal to `sectionAxis`.
    @Published var sectionEnabled = false {
        didSet {
            if sectionEnabled && !oldValue { sectionOffset = sectionRange.lowerBound / 2 + sectionRange.upperBound / 2 }
            redraw()
        }
    }
    @Published var sectionAxis = 2 {
        didSet {
            if sectionAxis != oldValue { sectionOffset = sectionRange.lowerBound / 2 + sectionRange.upperBound / 2 }
            redraw()
        }
    }

    /// Extent of the visible bodies along the section axis.
    var sectionRange: ClosedRange<Double> {
        let meshes = store.visibleMeshes.values
        guard !meshes.isEmpty else { return -100...100 }
        let lo = meshes.map { Double($0.boundsMin[sectionAxis]) }.min()!
        let hi = meshes.map { Double($0.boundsMax[sectionAxis]) }.max()!
        return lo...max(hi, lo + 1)
    }
    @Published var sectionOffset: Double = 0 { didSet { redraw() } }
    @Published var sectionFlipped = false { didSet { redraw() } }
    /// Measure mode: picks feed the measurement panel instead of the tools.
    @Published var measuring = false { didSet { redraw() } }
    private var measureCache: (selection: [Pick], version: Int, result: O3DMeasurement?)?

    /// Measurement of the selection, cached until it or the geometry changes.
    var measurement: O3DMeasurement? {
        if let c = measureCache, c.selection == store.selection, c.version == store.geometryVersion { return c.result }
        let result = store.measureSelection()
        measureCache = (store.selection, store.geometryVersion, result)
        return result
    }
    weak var view: MTKView?
    private var renderer: Renderer?
    private var cancellables: Set<AnyCancellable> = []
    private var hasFitted = false
    /// Where a rectangle or circle drag started, snapped.
    var sketchDragStart: SketchSnap?
    /// Geometry being moved with the select tool.
    var sketchDrag: SketchDrag?
    private var bodiesBeforeTap: [Pick] = []

    init(store: ModelStore) {
        self.store = store
        super.init()
        store.objectWillChange
            .receive(on: RunLoop.main)
            .sink { [weak self] in self?.storeChanged() }
            .store(in: &cancellables)
    }

    func attach(_ view: MTKView) {
        self.view = view
        view.delegate = self
        view.isPaused = true
        view.enableSetNeedsDisplay = true
        renderer = Renderer(view: view)
        redraw()
    }

    func redraw() {
        #if os(macOS)
        view?.needsDisplay = true
        #else
        view?.setNeedsDisplay()
        #endif
    }

    private func storeChanged() {
        if !camera.matches(store.settings.camera) { camera.settings = store.settings.camera }
        // Frame the model once when a document opens, not when the first
        // preview appears mid-sketch.
        if store.sketch != nil || store.draft != nil { hasFitted = true }
        if !hasFitted, !store.meshes.isEmpty {
            hasFitted = true
            fitAll()
        }
        redraw()
    }

    var size: CGSize { view?.bounds.size ?? CGSize(width: 1, height: 1) }

    // MARK: MTKViewDelegate

    nonisolated func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    nonisolated func draw(in view: MTKView) {
        MainActor.assumeIsolated { render(view) }
    }

    private func render(_ view: MTKView) {
        guard let renderer else { return }
        renderer.sync(meshes: store.visibleMeshes, version: store.geometryVersion)
        renderer.draw(in: view, scene: scene())
    }

    private func scene() -> SceneState {
        var s = SceneState(camera: camera, selection: store.selection, hover: store.hover, ghostBody: nil,
                           grid: (store.settings.environment.showGrid || store.sketch != nil) ? grid() : (showWorldAxes && displayMode != .visualized && ui?.visualizing != true ? axesOnly() : LineBatch()),
                           overlay: overlay(), displayMode: displayMode, clip: sectionPlane, appearance: store.displayAppearance)
        s.environment = store.settings.environment
        // Ground shadows belong to Visualize (and the Visualized display mode); modeling stays crisp.
        if ui?.visualizing != true && displayMode != .visualized { s.environment.groundShadow = false }
        if ui?.visualizing == true && displayMode != .zebra && displayMode != .curvature { s.displayMode = .visualized }
        s.showEdges = showEdges
        s.showHiddenEdges = showHiddenEdges
        s.cameraSettings = store.settings.camera
        s.images = store.referenceItems.compactMap { item in
            guard !store.settings.hiddenItems.contains(item.id), case let .image(image) = item.content else { return nil }
            if image.decal && !showDecals { return nil }
            return image
        }
        return s
    }

    var sectionPlane: SIMD4<Float>? {
        guard sectionEnabled else { return nil }
        var n = SIMD3<Float>(worldAxes[sectionAxis])
        var d = Float(sectionOffset)
        if sectionFlipped { n = -n; d = -d }
        return SIMD4(n, d)
    }

    /// The current view rendered at twice the view's size.
    func screenshot(scale: CGFloat = 2) -> CGImage? {
        guard let renderer else { return nil }
        renderer.sync(meshes: store.visibleMeshes, version: store.geometryVersion)
        return renderer.image(scene: scene(), size: size, scale: scale)
    }

    // MARK: Camera

    func orbit(dx: CGFloat, dy: CGFloat) {
        camera.orbit(dx: Float(dx), dy: Float(dy))
        redraw()
    }

    func pan(dx: CGFloat, dy: CGFloat) {
        camera.pan(dx: Float(dx), dy: Float(dy), size: size)
        redraw()
    }

    /// Zooms towards the point under the pointer so the model stays put.
    func zoom(_ factor: CGFloat, at point: CGPoint? = nil) {
        let f = Float(factor)
        if let point, let hit = surfaceHit(at: point)?.point {
            camera.target = hit + (camera.target - hit) * f
        }
        camera.zoom(f)
        redraw()
    }

    func look(_ preset: Camera.Preset) {
        camera.look(preset)
        redraw()
    }

    func fitAll() {
        let meshes = store.visibleMeshes.values
        guard !meshes.isEmpty else {
            camera = Camera()
            camera.settings = store.settings.camera
            redraw()
            return
        }
        let lo = meshes.map(\.boundsMin).reduce(SIMD3(repeating: .greatestFiniteMagnitude)) { simd_min($0, $1) }
        let hi = meshes.map(\.boundsMax).reduce(SIMD3(repeating: -.greatestFiniteMagnitude)) { simd_max($0, $1) }
        camera.fit(min: lo, max: hi, aspect: Float(size.width / max(size.height, 1)))
        redraw()
    }

    /// Looks straight at the sketch plane. When the camera can't line up with
    /// the plane's X axis (a tilted face) and the sketch is still empty, the
    /// axis is turned to match the screen so rectangles come out screen-aligned.
    func faceSketchPlane() {
        guard let s = store.sketch else { return }
        camera.face(normal: SIMD3<Float>(s.plane.normal), origin: SIMD3<Float>(s.plane.origin), xDir: SIMD3<Float>(s.plane.xDir))
        let aligned = simd_dot(SIMD3<Double>(camera.right), s.plane.xDir) > 0.999
        if !aligned && s.sketch.isEmpty && s.chainEnd == nil {
            let n = s.plane.normal, r = SIMD3<Double>(camera.right)
            let x = r - n * simd_dot(r, n)
            if simd_length(x) > 1e-6 { store.sketch?.plane.xDir = simd_normalize(x) }
        }
        redraw()
    }

    // MARK: Picking

    func surfaceHit(at point: CGPoint) -> (pick: Pick, point: SIMD3<Float>, t: Float)? {
        let (o, d) = camera.ray(at: point, size: size)
        var best: (Pick, SIMD3<Float>, Float)?
        for mesh in store.visibleMeshes.values {
            guard rayHitsBox(o, d, mesh.boundsMin, mesh.boundsMax) else { continue }
            let p = mesh.positions, idx = mesh.indices
            var i = 0
            while i + 2 < idx.count {
                let a = p[Int(idx[i])], b = p[Int(idx[i + 1])], c = p[Int(idx[i + 2])]
                if let t = rayTriangle(o, d, a, b, c), t < (best?.2 ?? .greatestFiniteMagnitude) {
                    best = (.face(body: mesh.id, index: Int(mesh.vertexFace[Int(idx[i])])), o + d * t, t)
                }
                i += 3
            }
        }
        return best.map { ($0.0, $0.1, $0.2) }
    }

    /// Nearest visible edge within `tolerance` points of the pointer.
    private func edgeHit(at point: CGPoint, tolerance: CGFloat, maxDepth: Float?) -> Pick? {
        let (o, _) = camera.ray(at: point, size: size)
        let vp = camera.viewProjection(size: size)
        let sz = size
        func screen(_ p: SIMD3<Float>) -> SIMD3<Float>? {
            let c = vp * SIMD4(p, 1)
            guard c.w > 0 else { return nil }
            return [Float((CGFloat(c.x / c.w) + 1) / 2 * sz.width), Float((1 - CGFloat(c.y / c.w)) / 2 * sz.height), c.w]
        }
        let q = SIMD2<Float>(Float(point.x), Float(point.y))
        var best: (Pick, Float)?
        let tol = Float(tolerance)
        for mesh in store.visibleMeshes.values {
            // Skip bodies whose on-screen bounds are nowhere near the pointer,
            // so hovering a big scene doesn't project every edge on each move.
            let lo = mesh.boundsMin, hi = mesh.boundsMax
            var corners: [SIMD3<Float>] = []
            for k in 0..<8 {
                if let c = screen(SIMD3(k & 1 == 0 ? lo.x : hi.x, k & 2 == 0 ? lo.y : hi.y, k & 4 == 0 ? lo.z : hi.z)) {
                    corners.append(c)
                }
            }
            if corners.count == 8 {
                let minX = corners.map(\.x).min()!, maxX = corners.map(\.x).max()!
                let minY = corners.map(\.y).min()!, maxY = corners.map(\.y).max()!
                if q.x < minX - tol || q.x > maxX + tol || q.y < minY - tol || q.y > maxY + tol { continue }
                // Entirely behind the surface already hit.
                if let maxDepth, simd_distance(simd_clamp(o, lo, hi), o) > maxDepth + camera.distance * 0.01 { continue }
            }
            let ep = mesh.edgePositions
            var i = 0
            while i + 1 < ep.count {
                defer { i += 2 }
                guard let a = screen(ep[i]), let b = screen(ep[i + 1]) else { continue }
                let a2 = SIMD2(a.x, a.y), b2 = SIMD2(b.x, b.y), ab = b2 - a2
                let t = simd_clamp(simd_dot(q - a2, ab) / max(simd_length_squared(ab), 1e-6), 0, 1)
                let dist = simd_distance(q, a2 + ab * t)
                guard dist <= Float(tolerance), dist < (best?.1 ?? .greatestFiniteMagnitude) else { continue }
                // Hidden edges lie behind the surface hit along the same ray.
                if let maxDepth {
                    let world = ep[i] + (ep[i + 1] - ep[i]) * t
                    if simd_distance(world, o) > maxDepth + camera.distance * 0.01 { continue }
                }
                best = (.edge(body: mesh.id, index: Int(mesh.edgeIds[i])), dist)
            }
        }
        return best?.0
    }

    func pick(at point: CGPoint) -> Pick? {
        let surface = surfaceHit(at: point)
        #if os(iOS)
        let tolerance: CGFloat = 14
        #else
        let tolerance: CGFloat = 6
        #endif
        if let edge = edgeHit(at: point, tolerance: tolerance, maxDepth: surface?.t) { return edge }
        return surface?.pick
    }

    // MARK: Input

    func tap(at point: CGPoint, additive: Bool) {
        if store.sketch != nil {
            sketchTap(at: point, additive: additive)
            return
        }
        guard store.draft == nil else { return }
        if pointPickTap(at: point) { return }
        if measuring {
            // Measure keeps the last two picks.
            guard let p = pick(at: point) else { return store.selection = [] }
            store.selection = store.selection.contains(p) ? store.selection.filter { $0 != p } : Array((store.selection + [p]).suffix(2))
            redraw()
            return
        }
        bodiesBeforeTap = store.selection.filter { if case .body = $0 { return true } else { return false } }
        store.select(pick(at: point), additive: additive)
        redraw()
    }

    /// Double tap selects a whole body (used for booleans, move, delete).
    func doubleTap(at point: CGPoint, additive: Bool) {
        // In a sketch, quick successive clicks are separate picks.
        if store.sketch != nil { return sketchTap(at: point, additive: additive) }
        guard store.draft == nil else { return }
        guard let hit = pick(at: point) else { return store.select(nil, additive: false) }
        let body = Pick.body(hit.body)
        // The first click of a double click already ran tap(), so use the body
        // selection from before it.
        let bodies = bodiesBeforeTap
        if bodies.contains(body) {
            store.selection = bodies.filter { $0 != body }
        } else {
            // Keep at most one previous body so two can be combined.
            store.selection = Array(bodies.suffix(1)) + [body]
        }
        redraw()
    }

    func hover(at point: CGPoint?) {
        if store.sketch != nil {
            sketchHover(at: point)
            return
        }
        if pointPickHover(at: point) { return }
        let newHover = point.flatMap { pick(at: $0) }
        if newHover != store.hover {
            store.hover = newHover
            redraw()
        }
    }

    // MARK: Frame geometry

    func grid() -> LineBatch {
        var batch = LineBatch()
        var plane = store.sketch?.plane ?? .xy
        if store.sketch == nil, gridAtModelBottom, let floor = store.visibleMeshes.values.map(\.boundsMin.z).min() {
            plane.origin.z = Double(floor)
        }
        let step = store.sketch != nil ? gridStep : max(gridStep, 1) * 10
        // Enough lines to reach past the edges of the view at any zoom.
        let reach = Double(camera.distance) * 1.5 / step
        let count = reach.isFinite ? min(max(20, Int(reach.rounded(.up))), 4000) : 20
        let extent = Double(count) * step
        let center = SIMD2<Double>(0, 0)
        for i in -count...count {
            let v = Double(i) * step
            let major = i % 10 == 0
            let alpha: Float = store.sketch != nil ? (major ? 0.35 : 0.16) : (i == 0 ? 0 : 0.14)
            let color = SIMD4<Float>(0.3, 0.33, 0.4, alpha)
            batch.line(SIMD3<Float>(plane.world(center + [v, -extent])), SIMD3<Float>(plane.world(center + [v, extent])), color)
            batch.line(SIMD3<Float>(plane.world(center + [-extent, v])), SIMD3<Float>(plane.world(center + [extent, v])), color)
        }
        if store.sketch == nil && showWorldAxes {
            let e = Float(extent)
            batch.line([-e, 0, 0], [e, 0, 0], [0.85, 0.25, 0.25, 0.6])
            batch.line([0, -e, 0], [0, e, 0], [0.25, 0.7, 0.3, 0.6])
            batch.line([0, 0, 0], [0, 0, e / 2], [0.25, 0.4, 0.9, 0.6])
        }
        return batch
    }

    /// World axes on their own, when the grid is hidden.
    private func axesOnly() -> LineBatch {
        var batch = LineBatch()
        let e = max(Float(gridStep) * 200, camera.distance * 1.5)
        batch.line([-e, 0, 0], [e, 0, 0], [0.85, 0.25, 0.25, 0.6])
        batch.line([0, -e, 0], [0, e, 0], [0.25, 0.7, 0.3, 0.6])
        batch.line([0, 0, 0], [0, 0, e / 2], [0.25, 0.4, 0.9, 0.6])
        return batch
    }

    private func overlay() -> LineBatch {
        var batch = LineBatch()
        highlightEdges(&batch)
        referenceOverlay(&batch)
        measureOverlay(&batch)
        pointOverlay(&batch)
        sketchOverlay(&batch)
        return batch
    }
}

extension ViewportController {
    /// Metal lines are one pixel wide, so selected and hovered edges are drawn
    /// again as a small bundle of offset lines to read as a thick stroke.
    func highlightEdges(_ batch: inout LineBatch) {
        var picks: [(Pick, SIMD4<Float>)] = store.selection.compactMap {
            if case .edge = $0 { return ($0, SIMD4<Float>(1.0, 0.55, 0.12, 1)) } else { return nil }
        }
        if let h = store.hover, case .edge = h, !store.selection.contains(h) { picks.append((h, [0.25, 0.55, 1.0, 1])) }
        guard !picks.isEmpty else { return }
        let s = camera.worldPerPoint(size: view?.bounds.size ?? .init(width: 1, height: 1)) * 0.75
        let offsets: [SIMD3<Float>] = [.zero, camera.right * s, -camera.right * s, camera.up * s, -camera.up * s]
        for (pick, color) in picks {
            guard case let .edge(body, index) = pick, let mesh = store.meshes[body] else { continue }
            var i = 0
            while i + 1 < mesh.edgePositions.count {
                if mesh.edgeIds[i] == UInt32(index) {
                    for o in offsets { batch.line(mesh.edgePositions[i] + o, mesh.edgePositions[i + 1] + o, color) }
                }
                i += 2
            }
        }
    }
}

private func rayTriangle(_ o: SIMD3<Float>, _ d: SIMD3<Float>, _ a: SIMD3<Float>, _ b: SIMD3<Float>, _ c: SIMD3<Float>) -> Float? {
    let e1 = b - a, e2 = c - a
    let p = simd_cross(d, e2)
    let det = simd_dot(e1, p)
    guard abs(det) > 1e-9 else { return nil }
    let inv = 1 / det
    let s = o - a
    let u = simd_dot(s, p) * inv
    guard u >= 0, u <= 1 else { return nil }
    let q = simd_cross(s, e1)
    let v = simd_dot(d, q) * inv
    guard v >= 0, u + v <= 1 else { return nil }
    let t = simd_dot(e2, q) * inv
    return t > 1e-5 ? t : nil
}

private func rayHitsBox(_ o: SIMD3<Float>, _ d: SIMD3<Float>, _ lo: SIMD3<Float>, _ hi: SIMD3<Float>) -> Bool {
    let pad = (hi - lo) * 0.01 + 0.01
    let inv = 1 / d
    let t1 = (lo - pad - o) * inv, t2 = (hi + pad - o) * inv
    let tmin = simd_reduce_max(simd_min(t1, t2)), tmax = simd_reduce_min(simd_max(t1, t2))
    return tmax >= max(tmin, 0)
}
