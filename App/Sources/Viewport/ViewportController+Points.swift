import CoreGraphics
import simd

/// A point picked on the model: a corner, a spot on an edge, or a spot on a face.
struct PickedPoint: Equatable {
    enum Kind { case vertex, edge, face }
    var position: SIMD3<Double>
    var kind: Kind
    var pick: Pick
    /// Direction of the edge at the point (edge and vertex picks).
    var tangent: SIMD3<Double>?
    /// Surface normal at the point (face picks).
    var normal: SIMD3<Double>?
}

/// Collects points from the viewport for a construction or a measurement,
/// then hands them to `complete`.
struct PointPickSession {
    var title: String
    /// One prompt per point still to pick.
    var prompts: [String]
    /// Kinds accepted for each point; nil accepts any.
    var accepts: [PickedPoint.Kind]? = nil
    var points: [PickedPoint] = []
    var hover: PickedPoint?
    let complete: @MainActor ([PickedPoint]) -> Void

    var needed: Int { prompts.count }
    var prompt: String { points.count < prompts.count ? prompts[points.count] : "" }
}

/// A measurement taken between picked points, shown in the viewport.
struct PointMeasurement: Identifiable, Equatable {
    let id = UUID()
    var points: [SIMD3<Double>]
    var label: String

    static func == (a: PointMeasurement, b: PointMeasurement) -> Bool { a.id == b.id }

    /// Where the label sits: the middle of the distance, or the angle's vertex.
    var anchor: SIMD3<Double> { points.count == 3 ? points[1] : (points[0] + points[1]) / 2 }
}

extension ViewportController {
    /// Starts collecting points; Escape or Cancel ends it.
    func beginPointPick(_ title: String, prompts: [String], accepts: [PickedPoint.Kind]? = nil,
                        complete: @escaping @MainActor ([PickedPoint]) -> Void) {
        measuring = false
        store.selection = []
        pointPick = PointPickSession(title: title, prompts: prompts, accepts: accepts, complete: complete)
    }

    func cancelPointPick() { pointPick = nil }

    /// The snapped point under the pointer: corners first, then edges, then faces.
    func pickPoint(at point: CGPoint) -> PickedPoint? {
        #if os(iOS)
        let tolerance: Float = 18
        #else
        let tolerance: Float = 9
        #endif
        let surface = surfaceHit(at: point)
        let vp = camera.viewProjection(size: size)
        let sz = size
        let (rayOrigin, _) = camera.ray(at: point, size: size)
        func screen(_ p: SIMD3<Float>) -> SIMD2<Float>? {
            let c = vp * SIMD4(p, 1)
            guard c.w > 0 else { return nil }
            return [Float((CGFloat(c.x / c.w) + 1) / 2 * sz.width), Float((1 - CGFloat(c.y / c.w)) / 2 * sz.height)]
        }
        // Points behind the surface under the pointer are hidden.
        func visible(_ p: SIMD3<Float>) -> Bool {
            guard let surface else { return true }
            return simd_distance(p, rayOrigin) <= surface.t + camera.distance * 0.01
        }
        let q = SIMD2<Float>(Float(point.x), Float(point.y))

        var bestVertex: (PickedPoint, Float)?
        var bestEdge: (PickedPoint, Float)?
        for mesh in store.visibleMeshes.values {
            let ep = mesh.edgePositions, ids = mesh.edgeIds
            var i = 0
            while i + 1 < ep.count {
                defer { i += 2 }
                let id = ids[i]
                let dir = SIMD3<Double>(simd_normalize(ep[i + 1] - ep[i]))
                // A corner is where an edge's polyline starts or ends.
                let starts = i == 0 || ids[i - 1] != id || ep[i - 1] != ep[i]
                let ends = i + 2 >= ep.count || ids[i + 2] != id || ep[i + 2] != ep[i + 1]
                for (flag, p) in [(starts, ep[i]), (ends, ep[i + 1])] where flag {
                    guard let s = screen(p), visible(p) else { continue }
                    let dist = simd_distance(s, q)
                    if dist <= tolerance, dist < (bestVertex?.1 ?? .greatestFiniteMagnitude) {
                        bestVertex = (PickedPoint(position: SIMD3<Double>(p), kind: .vertex, pick: .edge(body: mesh.id, index: Int(id)),
                                                  tangent: dir), dist)
                    }
                }
                guard let a = screen(ep[i]), let b = screen(ep[i + 1]) else { continue }
                let ab = b - a
                let t = simd_clamp(simd_dot(q - a, ab) / max(simd_length_squared(ab), 1e-6), 0, 1)
                let dist = simd_distance(q, a + ab * t)
                guard dist <= tolerance, dist < (bestEdge?.1 ?? .greatestFiniteMagnitude) else { continue }
                let world = ep[i] + (ep[i + 1] - ep[i]) * t
                guard visible(world) else { continue }
                bestEdge = (PickedPoint(position: SIMD3<Double>(world), kind: .edge, pick: .edge(body: mesh.id, index: Int(id)),
                                        tangent: dir), dist)
            }
        }
        if let v = bestVertex { return v.0 }
        if let e = bestEdge { return e.0 }
        guard let surface, case let .face(body, face) = surface.pick else { return nil }
        var normal: SIMD3<Double>?
        if let info = store.faceInfo(body, face), info.planar {
            normal = info.normal
        } else if let mesh = store.meshes[body] {
            // Normal of the nearest tessellation vertex on that face.
            var best = Float.greatestFiniteMagnitude
            for (k, p) in mesh.positions.enumerated() where mesh.vertexFace[k] == UInt32(face) {
                let d = simd_distance_squared(p, surface.point)
                if d < best { best = d; normal = SIMD3<Double>(mesh.normals[k]) }
            }
        }
        return PickedPoint(position: SIMD3<Double>(surface.point), kind: .face, pick: surface.pick, normal: normal)
    }

    /// Handles a tap while picking points. Returns true when it was consumed.
    func pointPickTap(at point: CGPoint) -> Bool {
        guard var session = pointPick else { return false }
        guard let p = pickPoint(at: point) else { return true }
        if let accepts = session.accepts, !accepts.isEmpty {
            let kinds = accepts.count > session.points.count ? accepts[session.points.count] : accepts.last!
            // A corner is also a point on its edges.
            let ok = kinds == p.kind || (kinds == .edge && p.kind == .vertex)
            guard ok else {
                store.toast = kinds == .face ? "Pick a point on a face" : "Pick a point on an edge"
                return true
            }
        }
        session.points.append(p)
        if session.points.count >= session.needed {
            pointPick = nil
            session.complete(session.points)
        } else {
            pointPick = session
        }
        redraw()
        return true
    }

    func pointPickHover(at point: CGPoint?) -> Bool {
        guard pointPick != nil else { return false }
        let p = point.flatMap { pickPoint(at: $0) }
        if p != pointPick?.hover {
            pointPick?.hover = p
            redraw()
        }
        return true
    }

    /// Markers for picked points and the point measurements.
    func pointOverlay(_ batch: inout LineBatch) {
        let s = camera.worldPerPoint(size: size)
        func marker(_ p: SIMD3<Double>, _ color: SIMD4<Float>, _ r: Float = 5) {
            let c = SIMD3<Float>(p), rr = camera.right * s * r, uu = camera.up * s * r
            batch.polyline([c - rr - uu, c + rr - uu, c + rr + uu, c - rr + uu, c - rr - uu], color)
        }
        if let session = pointPick {
            let picked = SIMD4<Float>(1.0, 0.55, 0.12, 1)
            for p in session.points { marker(p.position, picked) }
            batch.polyline(session.points.map { SIMD3<Float>($0.position) }, picked)
            if let h = session.hover {
                let color: SIMD4<Float> = h.kind == .vertex ? [0.1, 0.75, 0.45, 1] : [0.25, 0.55, 1.0, 1]
                marker(h.position, color, h.kind == .vertex ? 6 : 4)
                if let last = session.points.last { batch.line(SIMD3<Float>(last.position), SIMD3<Float>(h.position), color * [1, 1, 1, 0.6]) }
            }
        }
        guard showPinnedMeasurements else { return }
        let ink = SIMD4<Float>(0.05, 0.55, 0.45, 1)
        for m in pointMeasurements {
            batch.polyline(m.points.map { SIMD3<Float>($0) }, ink)
            for p in m.points { marker(p, ink, 3) }
        }
    }
}

// MARK: Measuring between points

extension ViewportController {
    func remember(_ text: String) {
        recentMeasurements.removeAll { $0 == text }
        recentMeasurements.insert(text, at: 0)
        if recentMeasurements.count > 8 { recentMeasurements.removeLast() }
    }

    func measurePointToPoint() {
        beginPointPick("Point-to-Point Distance", prompts: ["Pick the first point", "Pick the second point"]) { [weak self] pts in
            guard let self, pts.count == 2 else { return }
            let u = store.settings.units
            let d = pts[1].position - pts[0].position
            var label = formatLength(simd_length(d), u)
            if measureDeltas {
                label += "  (ΔX \(formatLength(abs(d.x), u)), ΔY \(formatLength(abs(d.y), u)), ΔZ \(formatLength(abs(d.z), u)))"
            }
            pointMeasurements.append(PointMeasurement(points: pts.map(\.position), label: label))
            remember("Distance: \(label)")
            showPinnedMeasurements = true
        }
    }

    func measureThreePointAngle() {
        beginPointPick("3-Point Angle", prompts: ["Pick a point on the first arm", "Pick the corner", "Pick a point on the second arm"]) { [weak self] pts in
            guard let self, pts.count == 3 else { return }
            let a = pts[0].position - pts[1].position, b = pts[2].position - pts[1].position
            guard simd_length(a) > 1e-9, simd_length(b) > 1e-9 else { return store.toast = "Pick three different points" }
            let angle = acos(min(max(simd_dot(simd_normalize(a), simd_normalize(b)), -1), 1)) * 180 / .pi
            let label = formatAngle(angle)
            pointMeasurements.append(PointMeasurement(points: pts.map(\.position), label: label))
            remember("Angle: \(label)")
            showPinnedMeasurements = true
        }
    }

    /// Frames the selected bodies (Item ▸ Zoom to).
    func zoomToSelection() {
        let ids = Set(store.selection.map(\.body))
        let meshes = store.visibleMeshes.filter { ids.contains($0.key) }.values
        guard !meshes.isEmpty else { return fitAll() }
        let lo = meshes.map(\.boundsMin).reduce(SIMD3(repeating: .greatestFiniteMagnitude)) { simd_min($0, $1) }
        let hi = meshes.map(\.boundsMax).reduce(SIMD3(repeating: -.greatestFiniteMagnitude)) { simd_max($0, $1) }
        camera.fit(min: lo, max: hi, aspect: Float(size.width / max(size.height, 1)))
        redraw()
    }

    func snapToNearestOrtho() {
        camera.snapToNearestOrtho()
        redraw()
    }

    func rotateView(yaw: Float = 0, pitch: Float = 0) {
        camera.rotate(yawSteps: yaw, pitchSteps: pitch)
        redraw()
    }

    /// Tools ▸ 2D Section View: look straight at the section plane, without perspective.
    func lookAtSection() {
        if !sectionEnabled { sectionEnabled = true }
        let n = SIMD3<Float>(worldAxes[sectionAxis]) * (sectionFlipped ? -1 : 1)
        camera.face(normal: -n, origin: camera.target, xDir: sectionAxis == 0 ? [0, 1, 0] : [1, 0, 0])
        store.settings.camera.orthographic = true
        fitAll()
    }
}
