import Foundation
import simd

/// Construct ▸ Plane / Axis variants and Item ▸ visibility commands.
extension ModelStore {
    private var planeCount: Int { features.filter { if case .plane = $0.op { return true } else { return false } }.count }
    private var axisCount: Int { features.filter { if case .axis = $0.op { return true } else { return false } }.count }

    /// Any unit vector perpendicular to `n`.
    private func perpendicular(to n: SIMD3<Double>) -> SIMD3<Double> {
        simd_normalize(simd_cross(abs(n.z) < 0.9 ? [0, 0, 1] : [1, 0, 0], n))
    }

    private func plane(origin: SIMD3<Double>, normal: SIMD3<Double>, xDir: SIMD3<Double>? = nil) -> SketchPlane {
        let n = simd_normalize(normal)
        var x = xDir.map { $0 - n * simd_dot($0, n) } ?? perpendicular(to: n)
        if simd_length(x) < 1e-9 { x = perpendicular(to: n) }
        return SketchPlane(origin: origin, normal: n, xDir: simd_normalize(x))
    }

    // MARK: Planes

    func addPlane(through points: [PickedPoint]) {
        guard points.count == 3 else { return }
        let a = points[0].position, b = points[1].position, c = points[2].position
        let n = simd_cross(b - a, c - a)
        guard simd_length(n) > 1e-9 else { return toast = "The three points are in a line" }
        begin(.plane(base: plane(origin: a, normal: n, xDir: b - a), offset: 0, angle: 0, name: "Plane \(planeCount + 1)"))
    }

    /// A plane through a point on an edge, normal to the edge there.
    func addPlane(normalToEdgeAt p: PickedPoint) {
        guard let t = p.tangent else { return toast = "Pick a point on an edge" }
        begin(.plane(base: plane(origin: p.position, normal: t), offset: 0, angle: 0, name: "Plane \(planeCount + 1)"))
    }

    /// A plane parallel to the picked face, through the picked point.
    func addPlane(parallelToFaceAt p: PickedPoint) {
        guard let n = p.normal else { return toast = "Pick a point on a face" }
        var xDir: SIMD3<Double>?
        if case let .face(b, f) = p.pick, let info = faceInfo(b, f), info.planar { xDir = info.xDir }
        begin(.plane(base: plane(origin: p.position, normal: n, xDir: xDir), offset: 0, angle: 0, name: "Plane \(planeCount + 1)"))
    }

    /// A plane containing the selected straight edge, turned about it from the
    /// selected face (or from vertical). The angle is editable.
    func addPlaneThroughEdge() {
        guard let e = selectedEdges.first, let info = kernel.edgeInfo(e.body, edge: e.index), info.linear else {
            return toast = "Select a straight edge"
        }
        let d = simd_normalize(info.end - info.start)
        var n0: SIMD3<Double>
        if let f = selectedFaces.first, let fi = faceInfo(f.body, f.index), fi.planar {
            n0 = fi.normal
        } else {
            // The plane through the edge closest to horizontal.
            n0 = simd_cross(d, perpendicular(to: d))
            let up = SIMD3<Double>(0, 0, 1) - d * d.z
            if simd_length(up) > 1e-6 { n0 = simd_normalize(up) }
        }
        n0 = simd_normalize(n0 - d * simd_dot(n0, d))
        let base = SketchPlane(origin: (info.start + info.end) / 2, normal: n0, xDir: d)
        selection = []
        begin(.plane(base: base, offset: 0, angle: 30, name: "Plane \(planeCount + 1)"))
    }

    // MARK: Axes

    func addAxis(through points: [PickedPoint]) {
        guard points.count == 2 else { return }
        let d = points[1].position - points[0].position
        guard simd_length(d) > 1e-9 else { return toast = "Pick two different points" }
        commitImmediately(.axis(origin: points[0].position, direction: simd_normalize(d), name: "Axis \(axisCount + 1)"))
    }

    /// The line where two selected planes or flat faces meet.
    func addAxisThroughTwoPlanes() {
        var planes: [SketchPlane] = selectedItems.filter(\.isPlane).map(\.plane)
        planes += selectedFaces.compactMap { faceInfo($0.body, $0.index) }.filter(\.planar).map {
            SketchPlane(origin: $0.centroid, normal: $0.normal, xDir: $0.xDir)
        }
        guard planes.count >= 2 else { return toast = "Select two planes or flat faces" }
        let p1 = planes[0], p2 = planes[1]
        let d = simd_cross(p1.normal, p2.normal)
        guard simd_length(d) > 1e-6 else { return toast = "The planes are parallel" }
        // Point on both planes closest to the first plane's origin.
        let n1 = p1.normal, n2 = p2.normal
        let h1 = simd_dot(n1, p1.origin), h2 = simd_dot(n2, p2.origin)
        let dd = simd_dot(d, d)
        let origin = (simd_cross(d, n2) * h1 + simd_cross(n1, d) * h2) / dd
        selection = []
        itemSelection = []
        commitImmediately(.axis(origin: origin, direction: simd_normalize(d), name: "Axis \(axisCount + 1)"))
    }

    /// The axis of the selected cylindrical, conical or spherical face (or circular edge).
    func addAxisOfRoundFace() {
        let hasRound = selectedFaces.contains { (faceInfo($0.body, $0.index)?.radius ?? 0) > 0 }
            || selectedEdges.contains { kernel.edgeInfo($0.body, edge: $0.index)?.circular == true }
        guard hasRound else { return toast = "Select a cylinder, cone or circular edge" }
        addAxis()
    }

    func addAxisAlongEdge() {
        guard selectedEdges.contains(where: { kernel.edgeInfo($0.body, edge: $0.index)?.linear == true }) else {
            return toast = "Select a straight edge"
        }
        addAxis()
    }

    func addAxis(normalToFaceAt p: PickedPoint) {
        guard let n = p.normal else { return toast = "Pick a point on a face" }
        commitImmediately(.axis(origin: p.position, direction: simd_normalize(n), name: "Axis \(axisCount + 1)"))
    }

    // MARK: Wrap & Emboss

    /// Projects the selected sketch onto the selected body and raises it.
    func embossSelection() {
        guard let sketchId = selectedSketchItems.first else { return toast = "Select a sketch in Items, then the body to emboss" }
        let body = selectedBody ?? {
            // With no body picked, the body nearest the sketch.
            guard let plane = referenceItems.first(where: { $0.id == sketchId })?.plane else { return nil }
            return visibleMeshes.min { a, b in
                simd_distance(SIMD3<Double>((a.value.boundsMin + a.value.boundsMax) / 2), plane.origin)
                    < simd_distance(SIMD3<Double>((b.value.boundsMin + b.value.boundsMax) / 2), plane.origin)
            }?.key
        }()
        guard let body else { return toast = "Select the body to emboss" }
        selection = []
        begin(.emboss(sketch: sketchId, body: body, height: 1, deboss: false))
    }

    // MARK: Visibility (Item menu)

    /// Bodies of everything selected: whole bodies, and the bodies of faces and edges.
    var touchedBodies: [Int] { Array(Set(selection.map(\.body))).sorted() }

    func toggleVisibilityOfSelection() {
        let bodies = touchedBodies
        if !bodies.isEmpty {
            let allHidden = bodies.allSatisfy { settings.hiddenBodies.contains($0) }
            for b in bodies { if allHidden { settings.hiddenBodies.remove(b) } else { settings.hiddenBodies.insert(b) } }
            if !allHidden { selection = [] }
        }
        for item in selectedItems { toggleHidden(item: item.id) }
    }

    /// Shows only the selected bodies until Exit Isolation.
    func isolateSelection() {
        let keep = Set(touchedBodies)
        guard !keep.isEmpty else { return }
        if isolationRestore == nil { isolationRestore = settings.hiddenBodies }
        settings.hiddenBodies = Set(bodyIds).subtracting(keep)
    }

    var isIsolating: Bool { isolationRestore != nil }

    func exitIsolation() {
        guard let restore = isolationRestore else { return }
        isolationRestore = nil
        settings.hiddenBodies = restore.intersection(Set(bodyIds))
    }

    func showAllBodies() {
        isolationRestore = nil
        settings.hiddenBodies = []
    }

    func invertBodyVisibility() {
        isolationRestore = nil
        settings.hiddenBodies = Set(bodyIds).subtracting(settings.hiddenBodies)
        selection = selection.filter { !settings.hiddenBodies.contains($0.body) }
    }
}
