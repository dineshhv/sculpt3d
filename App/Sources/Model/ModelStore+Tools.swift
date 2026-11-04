import Foundation
import ImageIO
import simd

/// Commands behind the Construct, Transform, Tools and Visualize menus.
extension ModelStore {
    var visibleMeshes: [Int: BodyMesh] { meshes.filter { !settings.hiddenBodies.contains($0.key) } }

    var selectedItems: [ReferenceItem] { itemSelection.compactMap { id in referenceItems.first { $0.id == id } } }

    /// The body the selection is about (a body, or the body of a face or edge).
    var selectedBody: Int? { selection.first?.body }

    private func center(of body: Int) -> SIMD3<Double> {
        guard let m = meshes[body] else { return .zero }
        return SIMD3<Double>((m.boundsMin + m.boundsMax) / 2)
    }

    private func count(of kind: (Operation) -> Bool) -> Int { features.filter { kind($0.op) }.count }

    // MARK: Items

    func toggleItemSelection(_ id: UUID) {
        if let i = itemSelection.firstIndex(of: id) { itemSelection.remove(at: i) } else { itemSelection.append(id) }
    }

    func toggleHidden(body: Int) {
        if settings.hiddenBodies.contains(body) { settings.hiddenBodies.remove(body) } else { settings.hiddenBodies.insert(body) }
        selection = selection.filter { $0.body != body }
    }

    func toggleHidden(item: UUID) {
        if settings.hiddenItems.contains(item) { settings.hiddenItems.remove(item) } else { settings.hiddenItems.insert(item) }
    }

    func rename(body: Int, to name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespaces)
        settings.bodyNames[body] = trimmed.isEmpty ? nil : trimmed
    }

    func renameItem(_ id: UUID, to name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty else { return }
        commit(features.map { f in
            var f = f
            guard f.id == id else { return f }
            switch f.op {
            case let .sketch(sketch, _): f.op = .sketch(sketch: sketch, name: trimmed)
            case let .plane(base, offset, angle, _): f.op = .plane(base: base, offset: offset, angle: angle, name: trimmed)
            default: break
            }
            return f
        })
    }

    func setAppearance(_ appearance: Appearance, for bodies: [Int]) {
        for b in bodies { settings.appearance[b] = appearance }
    }

    /// Bodies that Visualize acts on: the selection, or everything.
    var appearanceTargets: [Int] {
        let picked = Array(Set(selection.map(\.body))).sorted()
        return picked.isEmpty ? bodyIds : picked
    }

    // MARK: Sketch items

    func editItem(_ id: UUID) {
        guard let f = features.first(where: { $0.id == id }) else { return }
        switch f.op {
        case .sketch: editSketch(id)
        default: edit(id)
        }
    }

    /// Extrudes or revolves a copy of a standalone sketch, then hides the sketch.
    func useSketchItem(_ id: UUID, revolve: Bool) {
        guard let item = referenceItems.first(where: { $0.id == id }), case let .sketch(sk) = item.content else { return }
        guard !sk.profiles.isEmpty else {
            toast = "\(item.name) has no closed profile"
            return
        }
        settings.hiddenItems.insert(id)
        itemSelection = []
        begin(revolve ? .revolve(sketch: sk, angle: 360, aroundV: true, mode: .newBody, target: nil)
              : .extrude(sketch: sk, distance: 20, mode: .newBody, target: nil))
    }

    var selectedSketchItems: [UUID] {
        itemSelection.filter { id in referenceItems.contains { $0.id == id && $0.isSketch } }
    }

    func sweepSelection() {
        let sketches = selectedSketchItems
        guard sketches.count == 2 else {
            toast = "Select two sketches in Items: the profile, then the path"
            return
        }
        begin(.sweep(profile: sketches[0], path: sketches[1], mode: .newBody, target: nil))
    }

    func loftSelection() {
        let sketches = selectedSketchItems
        guard sketches.count >= 2 else {
            toast = "Select two or more sketches in Items, in loft order"
            return
        }
        begin(.loft(sections: sketches, mode: .newBody, target: nil))
    }

    // MARK: Construct

    /// Base for a new plane: a selected flat face, a selected plane item, or the top plane.
    private var planeBase: SketchPlane {
        if let f = selectedFaces.first, let info = faceInfo(f.body, f.index), info.planar {
            return SketchPlane(origin: info.centroid, normal: info.normal, xDir: info.xDir)
        }
        if let item = selectedItems.first { return item.plane }
        return .xy
    }

    func addOffsetPlane() {
        let n = count { if case .plane = $0 { return true } else { return false } }
        begin(.plane(base: planeBase, offset: 10, angle: 0, name: "Plane \(n + 1)"))
    }

    func addAngledPlane() {
        let n = count { if case .plane = $0 { return true } else { return false } }
        begin(.plane(base: planeBase, offset: 0, angle: 45, name: "Plane \(n + 1)"))
    }

    /// Plane halfway between two flat faces.
    func addMidplane() {
        let faces = selectedFaces.compactMap { faceInfo($0.body, $0.index) }.filter(\.planar)
        guard faces.count == 2 else {
            toast = "Select two flat faces"
            return
        }
        let n = count { if case .plane = $0 { return true } else { return false } }
        let base = SketchPlane(origin: (faces[0].centroid + faces[1].centroid) / 2, normal: faces[0].normal, xDir: faces[0].xDir)
        begin(.plane(base: base, offset: 0, angle: 0, name: "Plane \(n + 1)"))
    }

    func startSketchOnSelectedPlane() {
        guard let item = selectedItems.first(where: \.isPlane) else {
            toast = "Select a plane in Items"
            return
        }
        startSketch(on: item.plane)
    }

    // MARK: Transform

    func scaleSelection() {
        guard let b = selectedBody else { return }
        begin(.scaleAxes(body: b, factors: [2, 2, 2], center: center(of: b), uniform: true))
    }

    /// Transform ▸ Scatter: the first selected body is copied onto the last.
    func scatterSelection() {
        let bodies = selection.map(\.body).reduce(into: [Int]()) { if !$0.contains($1) { $0.append($1) } }
        guard bodies.count == 2, let m = meshes[bodies[0]] else {
            toast = "Select the body to scatter, then the body to scatter it on"
            return
        }
        let footprint = Double(max(m.boundsMax.x - m.boundsMin.x, m.boundsMax.y - m.boundsMin.y))
        var p = ScatterSettings()
        p.spacing = (footprint * 0.9).rounded()
        begin(.scatter(source: bodies[0], target: bodies[1], avoid: [], settings: p))
    }

    func linearPatternSelection() {
        guard let b = selectedBody, let m = meshes[b] else { return }
        let size = Double(m.boundsMax.x - m.boundsMin.x)
        begin(.linearPattern(body: b, direction: [1, 0, 0], count: 3, spacing: (size * 1.5).rounded(.up), fuse: false))
    }

    func circularPatternSelection() {
        guard let b = selectedBody else { return }
        begin(.circularPattern(body: b, origin: .zero, axis: [0, 0, 1], count: 6, angle: 360, fuse: false))
    }

    func mirrorSelection() {
        guard let b = selectedBody, let m = meshes[b] else { return }
        begin(.mirror(body: b, plane: axisPlane(0, origin: SIMD3<Double>(m.boundsMax)), keepOriginal: true))
    }

    // MARK: Tools

    func splitSelection() {
        guard let b = selectedBody else { return }
        begin(.split(body: b, plane: axisPlane(2, origin: center(of: b)), offset: 0))
    }

    func deleteFacesSelection() {
        guard let body = selectedFaces.first?.body else {
            toast = "Select the faces to delete"
            return
        }
        begin(.deleteFaces(body: body, faces: selectedFaces.filter { $0.body == body }.map(\.index)))
    }

    func draftSelection() {
        guard let body = selectedFaces.first?.body else {
            toast = "Select the side faces to draft"
            return
        }
        begin(.draft(body: body, faces: selectedFaces.filter { $0.body == body }.map(\.index), pull: [0, 0, 1], angle: 5))
    }

    func offsetFacesSelection() {
        guard let body = selectedFaces.first?.body else {
            toast = "Select the faces to offset"
            return
        }
        begin(.offsetFaces(body: body, faces: selectedFaces.filter { $0.body == body }.map(\.index), distance: 2))
    }

    // MARK: Measure

    /// Measurement of the current selection: one body, face or edge, or the
    /// relation between two.
    func measureSelection() -> O3DMeasurement? {
        func ref(_ p: Pick) -> (Int, Int, Int) {
            switch p {
            case let .body(b): return (b, 0, -1)
            case let .face(b, i): return (b, 1, i)
            case let .edge(b, i): return (b, 2, i)
            }
        }
        guard let first = selection.first else { return nil }
        let a = ref(first)
        let b = selection.count > 1 ? ref(selection[1]) : (-1, 0, -1)
        return kernel.measureBody(a.0, kind: a.1, index: a.2, againstBody: b.0, kind: b.1, index: b.2)
    }
}

/// Construct axis, more transforms, face tools, projection, inserts and variables.
extension ModelStore {
    private func countOf(_ kind: (Operation) -> Bool) -> Int { features.filter { kind($0.op) }.count }

    private var bodyCenter: SIMD3<Double>? {
        guard let b = selectedBody, let m = meshes[b] else { return nil }
        return SIMD3<Double>((m.boundsMin + m.boundsMax) / 2)
    }

    /// The selected axis item, if any.
    var selectedAxis: (origin: SIMD3<Double>, direction: SIMD3<Double>)? {
        for item in selectedItems { if case let .axis(o, d) = item.content { return (o, d) } }
        return nil
    }

    // MARK: Construct

    /// An axis through a round face, along a straight edge, or the world Z axis.
    func addAxis() {
        var origin = SIMD3<Double>.zero, direction = SIMD3<Double>(0, 0, 1)
        if let f = selectedFaces.first, let info = faceInfo(f.body, f.index), info.radius > 0 {
            origin = info.axisOrigin
            direction = info.axisDir
        } else if let e = selectedEdges.first, let info = kernel.edgeInfo(e.body, edge: e.index) {
            if info.linear {
                origin = info.start
                direction = info.end - info.start
            } else if info.circular {
                origin = info.center
                direction = info.axis
            }
        }
        let n = countOf { if case .axis = $0 { return true } else { return false } }
        commitImmediately(.axis(origin: origin, direction: direction, name: "Axis \(n + 1)"))
    }

    // MARK: Transform

    func translateSelection() {
        guard let b = selectedBody else { return }
        begin(.translate(body: b, direction: [1, 0, 0], distance: 10))
    }

    /// Rotates about a selected axis item, or the vertical axis through the body.
    func rotateSelection() {
        guard let b = selectedBody else { return }
        let axis = selectedAxis ?? (bodyCenter ?? .zero, [0, 0, 1])
        begin(.rotate(body: b, origin: axis.origin, axis: axis.direction, angle: 90))
    }

    /// Aligns the first selected body to the second, centring on X and Y and
    /// resting on Z by default.
    func alignSelection() {
        let bodies = selection.map(\.body).reduce(into: [Int]()) { if !$0.contains($1) { $0.append($1) } }
        guard bodies.count == 2 else { return toast = "Select the body to move, then the one to align to" }
        begin(.align(body: bodies[0], target: bodies[1], modes: [2, 2, 1]))
    }

    // MARK: Tools

    /// Moves the first selected face onto the plane of the second.
    func replaceFaceSelection() {
        let faces = selectedFaces
        guard faces.count == 2 else { return toast = "Select the face to move, then the target face" }
        commitImmediately(.replaceFace(body: faces[0].body, face: faces[0].index, targetBody: faces[1].body, targetFace: faces[1].index))
    }

    func fillSelection() {
        guard let body = selectedEdges.first?.body else { return toast = "Select a closed loop of edges" }
        commitImmediately(.fill(body: body, edges: selectedEdges.filter { $0.body == body }.map(\.index)))
    }

    /// Projects the selected body's edges onto a selected plane or flat face
    /// (or the top plane), as a sketch item.
    func projectSelection() {
        guard let b = selectedBody else { return toast = "Select a body to project" }
        var plane = SketchPlane.xy
        if let item = selectedItems.first(where: \.isPlane) {
            plane = item.plane
        } else if let f = selectedFaces.first(where: { $0.body != b }), let info = faceInfo(f.body, f.index), info.planar {
            plane = SketchPlane(origin: info.centroid, normal: info.normal, xDir: info.xDir)
        }
        let n = countOf { if case .projection = $0 { return true } else { return false } }
        commitImmediately(.projection(body: b, plane: plane, name: "Projection \(n + 1)"))
    }

    /// In a sketch: brings the visible bodies' edges onto the sketch plane.
    func projectIntoSketch() {
        guard var s = sketch else { return }
        let bodies = selection.isEmpty ? Array(visibleMeshes.keys) : Array(Set(selection.map(\.body)))
        for b in bodies {
            for data in kernel.projectEdges(b, plane: s.plane.bridged) {
                let pts = data.withUnsafeBytes { Array($0.bindMemory(to: SIMD2<Double>.self)) }
                let ids = pts.map { s.sketch.addPoint($0) }
                for (a, c) in zip(ids, ids.dropFirst()) { s.sketch.addLine(a, c) }
            }
        }
        s.resolve()
        sketch = s
    }

    // MARK: Insert

    /// Places an image on the selected plane or flat face (or the top plane).
    func insertImage(_ url: URL) {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil), let png = PNGFile.encode(image) else {
            return toast = "Couldn't read that image"
        }
        var plane = selectedItems.first(where: \.isPlane)?.plane ?? .xy
        if let f = selectedFaces.first, let info = faceInfo(f.body, f.index), info.planar {
            plane = SketchPlane(origin: info.centroid + info.normal * 0.01, normal: info.normal, xDir: info.xDir)
        }
        let width = 100.0
        let ref = ReferenceImage(name: url.deletingPathExtension().lastPathComponent, png: png, plane: plane,
                                 width: width, height: width * Double(image.height) / Double(max(image.width, 1)))
        selection = []
        begin(.image(image: ref))
    }

    /// Adds the bodies of another Sculpt3D project as imported bodies.
    func insertProject(_ url: URL) {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        guard let data = try? Data(contentsOf: url), let file = try? Sculpt3DFile(data: data) else {
            return toast = "Couldn't open that project"
        }
        let other = ModelStore(features: file.features, settings: file.settings)
        let name = url.deletingPathExtension().lastPathComponent
        let added = other.bodyIds.compactMap { other.kernel.brep(forBody: $0) }.map { Feature(op: .imported(name: name, brep: $0)) }
        guard !added.isEmpty else { return toast = "\(name) has no bodies" }
        commit(features + added)
    }

    // MARK: Variables

    func addVariable() {
        var n = settings.variables.count + 1
        while settings.variables.contains(where: { $0.name.lowercased() == "v\(n)" }) { n += 1 }
        settings.variables.append(Variable(name: "v\(n)", expression: "10"))
    }

    var variableValues: [String: Double] { ExpressionEvaluator(settings.variables).variables }

    /// Binds a parameter of the draft to an expression, or clears the binding.
    func bindDraft(_ parameter: Int, to expression: String?) {
        guard var d = draft else { return }
        var bindings = d.feature.bindings ?? [:]
        bindings[parameter] = expression
        d.feature.bindings = bindings.isEmpty ? nil : bindings
        if let expression, let v = ExpressionEvaluator(settings.variables).evaluate(expression) {
            d.feature.op = d.feature.op.setting(parameter, to: v)
        }
        draft = d
        regenerate()
    }
}
