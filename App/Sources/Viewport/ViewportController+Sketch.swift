import CoreGraphics
import simd

/// Geometry being dragged with the select tool. Each move re-solves from the
/// geometry as it was when the drag began.
struct SketchDrag {
    var ref: SketchRef
    var base: Sketch
    var start: SIMD2<Double>
}

/// A constraint glyph or dimension placed over the viewport.
struct SketchAnnotation: Identifiable {
    var id: String
    var constraint: Int
    var text: String
    var point: CGPoint
    var isDimension: Bool
    var selected: Bool
}

extension ViewportController {
    /// Grid step that keeps roughly 20–200 grid lines on screen.
    var gridStep: Double {
        if let fixedGrid { return fixedGrid }
        let span = Double(camera.distance)
        return pow(10, floor(log10(max(span / 15, 0.01))))
    }

    /// Hit radius in plane units.
    private var pickRadius: Double {
        #if os(iOS)
        let points = 14.0
        #else
        let points = 7.0
        #endif
        return Double(camera.worldPerPoint(size: size)) * points
    }

    /// Unsnapped point on the sketch plane under the pointer.
    func planePoint(at point: CGPoint) -> SIMD2<Double>? {
        guard let plane = store.sketch?.plane else { return nil }
        let (o, d) = camera.ray(at: point, size: size)
        let n = SIMD3<Float>(plane.normal), origin = SIMD3<Float>(plane.origin)
        let denom = simd_dot(d, n)
        guard abs(denom) > 1e-6 else { return nil }
        let t = simd_dot(origin - o, n) / denom
        guard t > 0 else { return nil }
        return plane.local(SIMD3<Double>(o + d * t))
    }

    private func gridSnap(_ p: SIMD2<Double>) -> SIMD2<Double> {
        guard store.settings.snap.grid else { return p }
        let step = gridStep
        return (p / step).rounded(.toNearestOrAwayFromZero) * step
    }

    func sketchSnap(at point: CGPoint) -> SketchSnap? {
        guard let session = store.sketch, let p = planePoint(at: point) else { return nil }
        let sketch = session.sketch, snap = store.settings.snap, r = pickRadius
        if snap.grid || snap.guidePoints || snap.guideLines || snap.points3D,
           let i = nearestPoint(in: sketch, to: p) {
            return SketchSnap(position: sketch.points[i], index: i)
        }
        // Guide points: midpoints, quadrants and body corners.
        var candidates: [(SIMD2<Double>, String)] = []
        if snap.guidePoints {
            for l in sketch.lines { candidates.append(((sketch.points[l.a] + sketch.points[l.b]) / 2, "Midpoint")) }
            for (i, c) in sketch.circles.enumerated() {
                let center = sketch.points[c.center]
                if let (start, sweep) = sketch.arcAngles(i) {
                    let t = start + sweep / 2
                    candidates.append((center + SIMD2(cos(t), sin(t)) * c.radius, "Midpoint"))
                } else {
                    for t in [0.0, .pi / 2, .pi, 3 * .pi / 2] { candidates.append((center + SIMD2(cos(t), sin(t)) * c.radius, "Quadrant")) }
                }
            }
        }
        if snap.points3D {
            for p3 in projectedBodyPoints() { candidates.append((p3, "3D point")) }
        }
        if let best = candidates.min(by: { simd_distance($0.0, p) < simd_distance($1.0, p) }), simd_distance(best.0, p) < r {
            return SketchSnap(position: best.0, index: nil, hint: best.1)
        }
        var g = gridSnap(p)
        // A grid point that already holds a sketch point is that point, so
        // shapes drawn on the grid connect instead of overlapping.
        if let i = sketch.points.indices.first(where: { simd_distance(sketch.points[$0], g) < gridStep * 1e-6 }) {
            return SketchSnap(position: sketch.points[i], index: i)
        }
        // Guide lines: line up with existing points horizontally or vertically.
        var guides: [[SIMD2<Double>]] = [], hint: String?
        if snap.guideLines {
            if let q = sketch.points.min(by: { abs($0.x - p.x) < abs($1.x - p.x) }), abs(q.x - p.x) < r, q != g {
                g.x = q.x
                guides.append([q, [q.x, g.y]])
                hint = "Vertical"
            }
            if let q = sketch.points.min(by: { abs($0.y - p.y) < abs($1.y - p.y) }), abs(q.y - p.y) < r {
                g.y = q.y
                guides.append([q, [g.x, q.y]])
                hint = hint == nil ? "Horizontal" : "Aligned"
            }
        }
        return SketchSnap(position: g, index: nil, hint: hint, guides: guides)
    }

    /// Corners of visible bodies, projected onto the sketch plane.
    private func projectedBodyPoints() -> [SIMD2<Double>] {
        guard let plane = store.sketch?.plane else { return [] }
        var seen = Set<SIMD2<Int>>(), out: [SIMD2<Double>] = []
        for mesh in store.visibleMeshes.values {
            // Edge polylines end at vertices; only the ends of each edge matter.
            var i = 0
            while i + 1 < mesh.edgePositions.count {
                let startsEdge = i == 0 || mesh.edgeIds[i] != mesh.edgeIds[i - 1]
                let endsEdge = i + 2 >= mesh.edgePositions.count || mesh.edgeIds[i + 1] != mesh.edgeIds[i + 2]
                for (k, take) in [(i, startsEdge), (i + 1, endsEdge)] where take {
                    let q = plane.local(SIMD3<Double>(mesh.edgePositions[k]))
                    if seen.insert(SIMD2<Int>(Int((q.x * 1000).rounded()), Int((q.y * 1000).rounded()))).inserted { out.append(q) }
                }
                i += 2
            }
        }
        return out
    }

    private func nearestPoint(in sketch: Sketch, to p: SIMD2<Double>) -> Int? {
        let r = pickRadius
        return sketch.points.indices
            .map { ($0, simd_distance(sketch.points[$0], p)) }
            .filter { $0.1 < r }
            .min { $0.1 < $1.1 }?.0
    }

    /// Point, line or circle under the pointer; points win over curves.
    func sketchHit(at point: CGPoint) -> SketchRef? {
        guard let sketch = store.sketch?.sketch, let p = planePoint(at: point) else { return nil }
        if let i = nearestPoint(in: sketch, to: p) { return .point(i) }
        let r = pickRadius
        var best: (SketchRef, Double)?
        for (i, l) in sketch.lines.enumerated() {
            let a = sketch.points[l.a], ab = sketch.points[l.b] - a
            let t = simd_clamp(simd_dot(p - a, ab) / max(simd_length_squared(ab), 1e-12), 0, 1)
            let d = simd_distance(p, a + ab * t)
            if d < r, d < (best?.1 ?? .infinity) { best = (.line(i), d) }
        }
        func polylineDistance(_ pts: [SIMD2<Double>]) -> Double {
            zip(pts, pts.dropFirst()).map { a, b in
                let ab = b - a
                let t = simd_clamp(simd_dot(p - a, ab) / max(simd_length_squared(ab), 1e-12), 0, 1)
                return simd_distance(p, a + ab * t)
            }.min() ?? .infinity
        }
        for (i, sp) in sketch.splines.enumerated() {
            let d = polylineDistance(sp.samples(sketch.points))
            if d < r, d < (best?.1 ?? .infinity) { best = (.spline(i), d) }
        }
        for (i, e) in sketch.ellipses.enumerated() {
            let d = polylineDistance(ellipseSamples(e, center: sketch.points[e.center]))
            if d < r, d < (best?.1 ?? .infinity) { best = (.ellipse(i), d) }
        }
        for (i, c) in sketch.circles.enumerated() {
            let center = sketch.points[c.center]
            let d = abs(simd_distance(p, center) - c.radius)
            if let (start, sweep) = sketch.arcAngles(i) {
                var t = atan2(p.y - center.y, p.x - center.x) - start
                while t < 0 { t += 2 * .pi }
                if t > sweep { continue }
            }
            if d < r, d < (best?.1 ?? .infinity) { best = (.circle(i), d) }
        }
        return best?.0
    }

    // MARK: Input

    func sketchHover(at point: CGPoint?) {
        guard var s = store.sketch else { return }
        if s.tool == .select || s.tool == .trim {
            s.cursor = nil
            s.hover = point.flatMap { sketchHit(at: $0) }
        } else {
            let snap = point.flatMap { sketchSnap(at: $0) }
            s.cursor = snap?.position
            s.hover = snap?.index.map { .point($0) }
            s.snapHint = snap?.hint
            s.guides = snap?.guides ?? []
        }
        // Publishing the sketch re-renders the SwiftUI panels; skip moves
        // that change nothing on screen.
        if let old = store.sketch, old.cursor == s.cursor, old.hover == s.hover,
           old.snapHint == s.snapHint, old.guides == s.guides { return }
        store.sketch = s
        redraw()
    }

    func sketchTap(at point: CGPoint, additive: Bool) {
        guard var s = store.sketch else { return }
        let before = (s.sketch.lines.count, s.sketch.circles.count)
        switch s.tool {
        case .trim:
            guard let hit = sketchHit(at: point), let p = planePoint(at: point) else { return }
            if s.sketch.trim(hit, at: p) {
                s.selection = []
                s.hover = nil
                s.resolve()
            }
        case .line:
            guard let snap = sketchSnap(at: point) else { return }
            let p = snap.index ?? s.sketch.addPoint(snap.position)
            if let end = s.chainEnd {
                s.sketch.addLine(end, p)
                // Landing on an existing point closes the chain.
                s.chainEnd = snap.index != nil ? nil : p
            } else {
                s.chainEnd = p
            }
            s.resolve()
        case .arc:
            // Start, end, then a point the arc passes through.
            guard let snap = sketchSnap(at: point) else { return }
            if s.pending.count < 2 {
                s.pending.append(snap)
            } else {
                let a = s.pending[0], b = s.pending[1]
                if let (c, _) = circleThrough(a.position, b.position, snap.position) {
                    let outline = arcOutline(a.position, b.position, through: snap.position)
                    // Arcs run counter-clockwise; swap ends when the drawn one doesn't.
                    let t0 = atan2(outline[0].y - c.y, outline[0].x - c.x), t1 = atan2(outline[1].y - c.y, outline[1].x - c.x)
                    let ccw = sin(t1 - t0) > 0
                    let ia = a.index ?? s.sketch.addPoint(a.position)
                    let ib = b.index ?? s.sketch.addPoint(b.position)
                    let center = s.sketch.addPoint(c)
                    s.sketch.addArc(center: center, start: ccw ? ia : ib, end: ccw ? ib : ia)
                    s.resolve()
                }
                s.pending = []
            }
        case .spline, .controlSpline:
            guard let snap = sketchSnap(at: point) else { return }
            let draft = s.splineDraft
            let control = s.tool == .controlSpline
            if let first = draft.first, snap.index == first, draft.count >= 3 {
                // Tapping the first point closes the spline.
                s.sketch.splines.append(SketchSpline(points: draft, closed: true, construction: s.constructionMode, controlPoints: control))
                s.splineDraft = []
            } else if let last = draft.last, snap.index == last {
                // Tapping the last point again finishes an open spline.
                if draft.count >= 2 {
                    s.sketch.splines.append(SketchSpline(points: draft, construction: s.constructionMode,
                                                         controlPoints: control && draft.count >= 3))
                }
                s.splineDraft = []
                s.sketch.delete([])
            } else {
                s.splineDraft.append(snap.index ?? s.sketch.addPoint(snap.position))
                s.sketch.markers.append(s.splineDraft.last!)
            }
            s.resolve()
        case .point:
            guard let snap = sketchSnap(at: point), snap.index == nil else { break }
            s.sketch.markers.append(s.sketch.addPoint(snap.position))
            s.resolve()
        case .select, .rectangle, .centerRectangle, .circle, .ellipse, .polygon:
            // Taps select in every tool except Line, so constraints never
            // need a tool switch.
            if let hit = sketchHit(at: point) {
                if let i = s.selection.firstIndex(of: hit) { s.selection.remove(at: i) } else { s.selection.append(hit) }
            } else if !additive {
                s.selection = []
            }
        }
        markConstruction(&s, since: before)
        store.sketch = s
        redraw()
    }

    /// In construction mode, marks geometry added since `before` (line and
    /// circle counts) as construction.
    private func markConstruction(_ s: inout SketchSession, since before: (Int, Int)) {
        guard s.constructionMode else { return }
        for i in before.0..<s.sketch.lines.count { s.sketch.lines[i].construction = true }
        for i in before.1..<s.sketch.circles.count { s.sketch.circles[i].construction = true }
    }

    /// True when a one-finger / left-button drag should draw or move sketch
    /// geometry instead of orbiting.
    func dragDraws(at point: CGPoint) -> Bool {
        guard let s = store.sketch else { return false }
        if s.tool == .select { return sketchHit(at: point) != nil }
        return s.tool.drags
    }

    func drawBegan(at point: CGPoint) {
        guard let s = store.sketch else { return }
        if s.tool == .select {
            guard let hit = sketchHit(at: point), let p = planePoint(at: point) else { return }
            store.beginSketchDrag()
            sketchDrag = SketchDrag(ref: hit, base: s.sketch, start: p)
            if !s.selection.contains(hit) { store.sketch?.selection = [hit] }
            return
        }
        sketchDragStart = sketchSnap(at: point)
    }

    func drawChanged(at point: CGPoint) {
        guard var s = store.sketch else { return }
        if let drag = sketchDrag {
            guard let p = planePoint(at: point) else { return }
            var sk = drag.base
            var targets: [Int: SIMD2<Double>] = [:]
            switch drag.ref {
            case let .point(i):
                targets[i] = gridSnap(p)
            case let .line(i):
                let delta = gridSnap(p - drag.start)
                for e in [sk.lines[i].a, sk.lines[i].b] { targets[e] = drag.base.points[e] + delta }
            case let .circle(i):
                sk.circles[i].radius = max(simd_distance(gridSnap(p), sk.points[sk.circles[i].center]), gridStep)
            case let .spline(i):
                // Move the whole curve.
                let delta = gridSnap(p - drag.start)
                for e in sk.splines[i].points { targets[e] = drag.base.points[e] + delta }
            case let .ellipse(i):
                // Dragging the outline resizes it, keeping its proportions.
                let e = sk.ellipses[i], c = sk.points[e.center]
                let scale = max(simd_distance(p, c), 1e-6) / max(simd_distance(drag.start, c), 1e-6)
                sk.ellipses[i].majorRadius = drag.base.ellipses[i].majorRadius * scale
                sk.ellipses[i].minorRadius = drag.base.ellipses[i].minorRadius * scale
            case .constraint:
                return
            }
            targets[0] = nil
            // A drag the constraints can't follow just leaves things where they were.
            if let status = sk.solve(drag: targets) {
                s.sketch = sk
                s.status = status
            }
            store.sketch = s
            redraw()
            return
        }
        guard let start = sketchDragStart, let end = sketchSnap(at: point) else { return }
        let a = start.position, b = end.position
        switch s.tool {
        case .circle:
            let r = simd_distance(a, b)
            s.preview = (0...64).map { a + SIMD2(cos(Double($0) / 32 * .pi), sin(Double($0) / 32 * .pi)) * r }
        case .centerRectangle:
            let d = b - a
            s.preview = [a - d, [a.x + d.x, a.y - d.y], a + d, [a.x - d.x, a.y + d.y], a - d]
        case .ellipse:
            s.preview = ellipseSamples(SketchEllipse(center: 0, majorRadius: abs(b.x - a.x), minorRadius: abs(b.y - a.y)), center: a)
        case .polygon:
            let corner = polygonCorner(center: a, drag: b, sides: s.polygonSides, circumscribed: s.polygonCircumscribed)
            let r = simd_distance(a, corner), t0 = atan2(corner.y - a.y, corner.x - a.x), n = s.polygonSides
            s.preview = (0...n).map { a + SIMD2(cos(t0 + Double($0) / Double(n) * 2 * .pi), sin(t0 + Double($0) / Double(n) * 2 * .pi)) * r }
        default:
            s.preview = [a, [b.x, a.y], b, [a.x, b.y], a]
        }
        s.cursor = b
        store.sketch = s
        redraw()
    }

    func drawEnded(at point: CGPoint) {
        drawChanged(at: point)
        defer {
            sketchDragStart = nil
            if sketchDrag != nil { store.endSketchDrag() }
            sketchDrag = nil
        }
        guard sketchDrag == nil, var s = store.sketch, let start = sketchDragStart, let end = sketchSnap(at: point) else { return }
        let before = (s.sketch.lines.count, s.sketch.circles.count)
        s.preview = []
        switch s.tool {
        case .rectangle:
            s.sketch.addRectangle(start.position, end.position, start: start.index, end: end.index)
        case .circle:
            let r = simd_distance(start.position, end.position)
            if r > 1e-9 { s.sketch.addCircle(center: start.index ?? s.sketch.addPoint(start.position), radius: r) }
        case .centerRectangle:
            s.sketch.addCenterRectangle(center: start.position, corner: end.position, centerIndex: start.index)
        case .ellipse:
            let rx = abs(end.position.x - start.position.x), ry = abs(end.position.y - start.position.y)
            if rx > 1e-9, ry > 1e-9 {
                let c = start.index ?? s.sketch.addPoint(start.position)
                s.sketch.ellipses.append(SketchEllipse(center: c, majorRadius: rx, minorRadius: ry, construction: s.constructionMode))
            }
        case .polygon:
            let corner = polygonCorner(center: start.position, drag: end.position, sides: s.polygonSides,
                                       circumscribed: s.polygonCircumscribed)
            s.sketch.addPolygon(center: start.position, corner: corner, sides: s.polygonSides, centerIndex: start.index)
        default:
            break
        }
        markConstruction(&s, since: before)
        s.resolve()
        store.sketch = s
        redraw()
    }

    // MARK: Drawing

    func sketchOverlay(_ batch: inout LineBatch) {
        guard let s = store.sketch else { return }
        let sketch = s.sketch, plane = s.plane
        func w(_ p: SIMD2<Double>) -> SIMD3<Float> { SIMD3<Float>(plane.world(p)) }

        let free = SIMD4<Float>(0.05, 0.4, 0.95, 1)
        let fixed = SIMD4<Float>(0.08, 0.1, 0.16, 1)
        let construction = SIMD4<Float>(0.45, 0.5, 0.6, 0.9)
        let selected = SIMD4<Float>(1.0, 0.5, 0.1, 1)
        let hovered = SIMD4<Float>(0.35, 0.7, 1.0, 1)
        let draft = SIMD4<Float>(1.0, 0.5, 0.1, 1)
        let pixel = camera.worldPerPoint(size: size)

        func isFixed(_ p: Int) -> Bool { s.status.fixedPoints.indices.contains(p) && s.status.fixedPoints[p] }
        func color(_ ref: SketchRef, determined: Bool, isConstruction: Bool) -> SIMD4<Float> {
            if s.selection.contains(ref) { return selected }
            if s.hover == ref { return hovered }
            if isConstruction { return construction }
            return determined ? fixed : free
        }
        /// Selected and hovered curves are drawn thicker, as offset copies.
        func stroke(_ a: SIMD2<Double>, _ b: SIMD2<Double>, _ c: SIMD4<Float>, thick: Bool, dashed: Bool) {
            let offsets: [SIMD3<Float>] = thick ? [.zero, camera.right * pixel, -camera.right * pixel, camera.up * pixel, -camera.up * pixel] : [.zero]
            if dashed {
                let length = simd_distance(a, b), dash = Double(pixel) * 6
                let count = max(Int(length / dash), 1)
                for i in stride(from: 0, to: count, by: 2) {
                    let p = a + (b - a) * (Double(i) / Double(count)), q = a + (b - a) * (Double(min(i + 1, count)) / Double(count))
                    for o in offsets { batch.line(w(p) + o, w(q) + o, c) }
                }
            } else {
                for o in offsets { batch.line(w(a) + o, w(b) + o, c) }
            }
        }

        // Plane axes.
        let a = gridStep * 200
        batch.line(w([-a, 0]), w([a, 0]), [0.85, 0.25, 0.25, 0.5])
        batch.line(w([0, -a]), w([0, a]), [0.25, 0.7, 0.3, 0.5])

        for (i, l) in sketch.lines.enumerated() {
            let ref = SketchRef.line(i)
            let c = color(ref, determined: isFixed(l.a) && isFixed(l.b), isConstruction: l.construction)
            stroke(sketch.points[l.a], sketch.points[l.b], c, thick: s.selection.contains(ref) || s.hover == ref, dashed: l.construction)
        }
        for (i, circle) in sketch.circles.enumerated() {
            let ref = SketchRef.circle(i)
            let determined = s.status.fixedCircles.indices.contains(i) && s.status.fixedCircles[i]
            let c = color(ref, determined: determined, isConstruction: circle.construction)
            let center = sketch.points[circle.center]
            let (start, sweep) = sketch.arcAngles(i) ?? (0, 2 * .pi)
            let segments = max(8, Int(96 * sweep / (2 * .pi)))
            for k in 0..<segments {
                let t0 = start + Double(k) / Double(segments) * sweep, t1 = start + Double(k + 1) / Double(segments) * sweep
                if circle.construction && k % 2 == 1 { continue }
                stroke(center + SIMD2(cos(t0), sin(t0)) * circle.radius, center + SIMD2(cos(t1), sin(t1)) * circle.radius,
                       c, thick: s.selection.contains(ref) || s.hover == ref, dashed: false)
            }
        }
        for (i, sp) in sketch.splines.enumerated() {
            let ref = SketchRef.spline(i)
            let c = color(ref, determined: sp.points.allSatisfy(isFixed), isConstruction: sp.construction)
            let pts = sp.samples(sketch.points)
            for (a, b) in zip(pts, pts.dropFirst()) { stroke(a, b, c, thick: s.selection.contains(ref) || s.hover == ref, dashed: false) }
        }
        for (i, e) in sketch.ellipses.enumerated() {
            let ref = SketchRef.ellipse(i)
            let c = color(ref, determined: false, isConstruction: e.construction)
            let pts = ellipseSamples(e, center: sketch.points[e.center])
            for (a, b) in zip(pts, pts.dropFirst()) { stroke(a, b, c, thick: s.selection.contains(ref) || s.hover == ref, dashed: false) }
        }
        // Points as small squares; the origin always shows.
        var used = Set<Int>([0])
        for l in sketch.lines { used.formUnion([l.a, l.b]) }
        for c in sketch.circles { used.formUnion([c.center] + [c.start, c.end].compactMap { $0 }) }
        for sp in sketch.splines { used.formUnion(sp.points) }
        for e in sketch.ellipses { used.insert(e.center) }
        used.formUnion(sketch.markers)
        for i in sketch.points.indices where used.contains(i) {
            let ref = SketchRef.point(i)
            let active = s.selection.contains(ref) || s.hover == ref
            let c = color(ref, determined: isFixed(i), isConstruction: false)
            let r = Double(pixel) * (active ? 4 : 2.5), p = sketch.points[i]
            let corners: [SIMD2<Double>] = [p + [-r, -r], p + [r, -r], p + [r, r], p + [-r, r], p + [-r, -r]]
            batch.polyline(corners.map(w), c)
        }

        // Dimension leaders.
        let offset = Double(pixel) * 14
        for (i, c) in sketch.constraints.enumerated() where c.kind.isDimension {
            let leader = s.anchor(for: c, offset: offset).leader
            let ink: SIMD4<Float> = s.selection.contains(.constraint(i)) ? selected : [0.3, 0.32, 0.4, 0.8]
            batch.polyline(leader.map(w), ink)
        }

        // In-progress geometry.
        batch.polyline(s.preview.map(w), draft)
        if !s.splineDraft.isEmpty {
            let pts = s.splineDraft.map { sketch.points[$0] } + (s.cursor.map { [$0] } ?? [])
            let curve = s.tool == .controlSpline ? bsplineSamples(pts, closed: false) : splineSamples(pts, closed: false)
            if s.tool == .controlSpline { batch.polyline(pts.map(w), draft * [1, 1, 1, 0.35]) }
            batch.polyline(curve.map(w), draft)
        }
        if let cursor = s.cursor, !s.pending.isEmpty {
            if s.pending.count == 1 {
                batch.line(w(s.pending[0].position), w(cursor), draft)
            } else {
                batch.polyline(arcOutline(s.pending[0].position, s.pending[1].position, through: cursor).map(w), draft)
            }
        }
        if let end = s.chainEnd, sketch.points.indices.contains(end), let cursor = s.cursor {
            batch.line(w(sketch.points[end]), w(cursor), draft)
        }
        // Alignment guides, dashed.
        for g in s.guides where g.count == 2 {
            let n = max(Int(simd_distance(g[0], g[1]) / (Double(pixel) * 6)), 1)
            for k in stride(from: 0, to: n, by: 2) {
                batch.line(w(g[0] + (g[1] - g[0]) * (Double(k) / Double(n))), w(g[0] + (g[1] - g[0]) * (Double(min(k + 1, n)) / Double(n))),
                           [0.95, 0.45, 0.1, 0.8])
            }
        }
        // Guide points: small diamonds at line midpoints.
        if store.settings.snap.showGuidePoints && store.settings.snap.guidePoints {
            let d = Double(pixel) * 3
            for l in sketch.lines {
                let m = (sketch.points[l.a] + sketch.points[l.b]) / 2
                batch.polyline([m + [d, 0], m + [0, d], m - [d, 0], m - [0, d], m + [d, 0]].map(w), [0.5, 0.55, 0.65, 0.6])
            }
        }
        if let c = s.cursor {
            let r = Double(pixel) * 6
            batch.line(w(c - [r, 0]), w(c + [r, 0]), draft)
            batch.line(w(c - [0, r]), w(c + [0, r]), draft)
        }
    }

    /// Constraint glyphs and dimension values, in view coordinates.
    func sketchAnnotations() -> [SketchAnnotation] {
        guard let s = store.sketch, showConstraints else { return [] }
        let offset = Double(camera.worldPerPoint(size: size)) * 14
        var out: [SketchAnnotation] = []
        func screen(_ p: SIMD2<Double>) -> CGPoint? { camera.project(SIMD3<Float>(s.plane.world(p)), size: size) }
        // One glyph per kind and line, however many constraints share it.
        var shown = Set<String>()
        func firstTime(_ kind: SketchConstraint.Kind, _ line: Int) -> Bool { shown.insert("\(kind.glyph)\(line)").inserted }
        for (i, c) in s.sketch.constraints.enumerated() {
            guard c.kind != .coincident else { continue }
            // Points held on a construction circle (a polygon's corners) would
            // only add clutter.
            if c.kind == .pointOnCircle, s.sketch.circles.indices.contains(c.b), s.sketch.circles[c.b].construction { continue }
            let selected = s.selection.contains(.constraint(i))
            let text = c.kind.isDimension ? s.label(for: c) : c.kind.glyph
            let onLine = !c.kind.isDimension && c.kind.arguments.0 == .line
            if !onLine || firstTime(c.kind, c.a), let p = screen(s.anchor(for: c, offset: offset).position) {
                out.append(SketchAnnotation(id: "\(i)", constraint: i, text: text, point: p, isDimension: c.kind.isDimension, selected: selected))
            }
            // Two-line relations are marked on both lines.
            if !c.kind.isDimension, c.kind.arguments.0 == .line, c.kind.arguments.1 == .line {
                var mirrored = c
                mirrored.a = c.b
                if firstTime(c.kind, c.b), let p = screen(s.anchor(for: mirrored, offset: offset).position) {
                    out.append(SketchAnnotation(id: "\(i)b", constraint: i, text: text, point: p, isDimension: false, selected: selected))
                }
            }
        }
        // Snapping hint next to the cursor.
        if store.settings.snap.showHints, let hint = s.snapHint, let c = s.cursor, let p = screen(c) {
            out.append(SketchAnnotation(id: "hint", constraint: -1, text: hint, point: CGPoint(x: p.x + 34, y: p.y - 16),
                                        isDimension: false, selected: true))
        }
        // Spread glyphs that would sit on top of each other.
        var taken: [CGPoint] = []
        for i in out.indices {
            while taken.contains(where: { hypot($0.x - out[i].point.x, $0.y - out[i].point.y) < 16 }) { out[i].point.x += 18 }
            taken.append(out[i].point)
        }
        return out
    }
}

/// A polygon's first corner. Inscribed: the drag point is a corner. Circumscribed:
/// it's the middle of a side, so the polygon wraps around the dragged circle.
func polygonCorner(center c: SIMD2<Double>, drag b: SIMD2<Double>, sides n: Int, circumscribed: Bool) -> SIMD2<Double> {
    guard circumscribed else { return b }
    let half = Double.pi / Double(max(n, 3))
    let d = b - c
    let turned = SIMD2(d.x * cos(half) - d.y * sin(half), d.x * sin(half) + d.y * cos(half))
    return c + turned / cos(half)
}
