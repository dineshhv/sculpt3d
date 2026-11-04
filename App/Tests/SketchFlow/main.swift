import Foundation
import simd

func formatted(_ value: Double) -> String { value.formatted(.number.precision(.fractionLength(0...3))) }
var failures = 0
func check(_ ok: Bool, _ what: String) { print(ok ? "ok  " : "FAIL", what); if !ok { failures += 1 } }

@MainActor func run() {
    let store = ModelStore(features: [])
    store.startSketch(on: .xy)
    var s = store.sketch!
    s.sketch.addRectangle([3, 4], [41, 27], start: nil, end: nil)
    s.resolve()
    check(s.sketch.lines.count == 4 && s.sketch.constraints.count == 4, "rectangle has 4 lines and H/V constraints")
    check(s.status.degreesOfFreedom == 4, "rectangle has 4 dof, got \(s.status.degreesOfFreedom)")
    check(s.sketch.profileCount == 1, "rectangle is one profile")

    // Corner on origin, then width and height.
    s.selection = [.point(1), .point(0)]
    check(s.availableConstraints.contains(.coincident), "two points offer coincident")
    check(s.constrain(.coincident) == nil, "coincident applies")
    check(simd_length(s.sketch.points[1]) < 1e-6, "corner moved to origin")
    check(s.sketch.profileCount == 1, "still one profile after coincident")
    s.selection = [.line(0)]
    check(s.constrain(.length) == nil, "length dimension applies")
    let dim = s.selectedDimension!
    check(s.setDimension(dim, to: 50) == nil, "length set to 50")
    s.selection = [.line(1)]
    check(s.constrain(.length) == nil, "height dimension applies")
    check(s.setDimension(s.selectedDimension!, to: 30) == nil, "height set to 30")
    check(s.status.fullyConstrained, "rectangle fully constrained (dof \(s.status.degreesOfFreedom))")
    s.selection = [.line(2)]
    check(s.constrain(.length) != nil, "redundant length is refused")
    s.selection = [.line(0)]
    check(s.constrain(.vertical) != nil, "conflicting vertical is refused")

    // Hole: a circle at the centre with a radius.
    let c = s.sketch.addPoint([25, 15])
    s.sketch.addCircle(center: c, radius: 6)
    s.resolve()
    s.selection = [.circle(0)]
    check(s.constrain(.radius) == nil && s.setDimension(s.selectedDimension!, to: 8) == nil, "radius 8")
    check(s.sketch.profileCount == 2, "rectangle + circle are two profiles")

    // Drag a free point: circle center follows.
    var sk = s.sketch
    check(sk.solve(drag: [c: [20, 12]]) != nil && simd_distance(sk.points[c], [20, 12]) < 1e-3, "free center drags")
    var locked = s.sketch
    let corner = locked.lines[1].b
    let before = locked.points[corner]
    _ = locked.solve(drag: [corner: [80, 80]])
    check(simd_distance(locked.points[corner], before) < 1e-6, "constrained corner can't be dragged")

    // Encode / decode round trip.
    let data = try! JSONEncoder().encode(s.sketch)
    let back = try! JSONDecoder().decode(Sketch.self, from: data)
    check(back == s.sketch, "sketch round-trips through JSON")
    let legacy = #"{"plane":{"origin":[0,0,0],"normal":[0,0,1],"xDir":[1,0,0]},"shapes":[{"rectangle":{"_0":[0,0],"_1":[10,5]}},{"circle":{"center":[20,0],"radius":3}}]}"#
    let old = try! JSONDecoder().decode(Sketch.self, from: Data(legacy.utf8))
    check(old.lines.count == 4 && old.circles.count == 1 && old.profileCount == 2, "legacy shapes load as constrained geometry")

    // Extrude: a 50x30 plate with an 8 mm hole, 10 mm thick.
    store.sketch = s
    store.finishSketch(revolve: false)
    store.updateDraft(store.draft!.feature.op.setting(0, to: 10))
    store.applyDraft()
    let p = store.properties(store.bodyIds.first ?? -1)
    let expected = 50.0 * 30 * 10 - Double.pi * 64 * 10
    check(p != nil && abs(p!.volume - expected) < 1, "plate volume \(p?.volume ?? 0) ≈ \(expected)")

    // Edit the sketch from history: make it 60 wide.
    let fid = store.features[0].id
    store.editSketch(fid)
    check(store.meshes.isEmpty, "editing the first feature's sketch hides its result")
    var e = store.sketch!
    let lengthIndex = e.sketch.constraints.firstIndex { $0.kind == .length && $0.value == 50 }!
    check(e.setDimension(lengthIndex, to: 60) == nil, "width edited to 60")
    store.sketch = e
    store.finishSketch(revolve: false)
    store.applyDraft()
    let q = store.properties(store.bodyIds.first ?? -1)
    check(store.features.count == 1 && q != nil && abs(q!.volume - (60.0 * 30 * 10 - Double.pi * 64 * 10)) < 1, "history updated in place, volume \(q?.volume ?? 0)")

    // Deleting a line drops its constraints and breaks the loop.
    var d = store.features[0].op.sketch!
    let nc = d.constraints.count
    d.delete([.line(0)])
    check(d.lines.count == 3 && d.constraints.count < nc && d.profileCount == 1, "delete line: 3 lines, fewer constraints, only circle profile")

    newFeatures()
}

/// Standalone sketches, arcs, sweep, loft, patterns, split and measure, driven
/// through the store the way the tool rail does.
@MainActor func newFeatures() {
    func volume(_ store: ModelStore) -> Double { store.bodyIds.compactMap { store.properties($0)?.volume }.reduce(0, +) }

    // A 20 x 10 slot: two lines and two semicircle arcs, kept as a sketch item.
    let store = ModelStore(features: [])
    store.startSketch(on: .xy)
    var s = store.sketch!
    let a = s.sketch.addPoint([0, 0]), b = s.sketch.addPoint([20, 0]), c = s.sketch.addPoint([20, 10]), d = s.sketch.addPoint([0, 10])
    s.sketch.addLine(a, b)
    s.sketch.addLine(c, d)
    s.sketch.addArc(center: s.sketch.addPoint([20, 5]), start: b, end: c)
    s.sketch.addArc(center: s.sketch.addPoint([0, 5]), start: d, end: a)
    s.resolve()
    check(s.sketch.profileCount == 1, "slot of lines and arcs is one profile")
    store.sketch = s
    store.finishSketchAsItem()
    check(store.referenceItems.count == 1 && store.bodyIds.isEmpty, "Done keeps the sketch as an item")
    let slot = store.referenceItems[0].id
    store.useSketchItem(slot, revolve: false)
    store.updateDraft(store.draft!.feature.op.setting(0, to: 10))
    store.applyDraft()
    let slotVolume = (200 + Double.pi * 25) * 10
    check(abs(volume(store) - slotVolume) < 1, "slot extrudes to \(volume(store)) ≈ \(slotVolume)")

    // Linear pattern of 3, separate bodies.
    store.selection = [.body(store.bodyIds[0])]
    store.linearPatternSelection()
    store.applyDraft()
    check(store.bodyIds.count == 3 && abs(volume(store) - 3 * slotVolume) < 3, "linear pattern makes 3 bodies")

    // Measure between two of them.
    store.selection = [.body(store.bodyIds[0]), .body(store.bodyIds[1])]
    let m = store.measureSelection()
    check(m?.hasDistance == true && (m?.distance ?? 0) > 0, "measure distance between pattern copies: \(m?.distance ?? -1)")

    // Split the first body in half.
    store.selection = [.body(store.bodyIds[0])]
    store.splitSelection()
    store.applyDraft()
    check(store.bodyIds.count == 4, "split adds a body (\(store.bodyIds.count))")

    // Sweep: circle profile on the front plane along a line on the top plane.
    let sweep = ModelStore(features: [])
    sweep.startSketch(on: .xz)
    var p = sweep.sketch!
    p.sketch.addCircle(center: 0, radius: 3)
    sweep.sketch = p
    sweep.finishSketchAsItem()
    sweep.startSketch(on: .xy)
    var q = sweep.sketch!
    q.sketch.addLine(0, q.sketch.addPoint([0, 50]))
    sweep.sketch = q
    sweep.finishSketchAsItem()
    sweep.itemSelection = sweep.referenceItems.map(\.id)
    sweep.sweepSelection()
    sweep.applyDraft()
    check(sweep.errors.isEmpty && abs(volume(sweep) - Double.pi * 9 * 50) < 5, "sweep volume \(volume(sweep))")

    // Loft between a square and a smaller one on an offset plane.
    let loft = ModelStore(features: [])
    loft.startSketch(on: .xy)
    var l1 = loft.sketch!
    l1.sketch.addRectangle([-10, -10], [10, 10], start: nil, end: nil)
    loft.sketch = l1
    loft.finishSketchAsItem()
    loft.addOffsetPlane()
    loft.updateDraft(loft.draft!.feature.op.setting(0, to: 30))
    loft.applyDraft()
    let plane = loft.referenceItems.first(where: \.isPlane)!
    loft.startSketch(on: plane.plane)
    var l2 = loft.sketch!
    l2.sketch.addRectangle([-5, -5], [5, 5], start: nil, end: nil)
    loft.sketch = l2
    loft.finishSketchAsItem()
    loft.itemSelection = loft.referenceItems.filter { !$0.isPlane }.map(\.id)
    loft.loftSelection()
    loft.applyDraft()
    check(loft.errors.isEmpty && abs(volume(loft) - 7000) < 5, "loft volume \(volume(loft)) ≈ 7000")

    // Undo inside a sketch: each edit is one step; a drag is one step.
    let u = ModelStore(features: [])
    u.startSketch(on: .xy)
    var e = u.sketch!
    e.sketch.addRectangle([0, 0], [10, 10], start: nil, end: nil)
    u.sketch = e
    e.sketch.addCircle(center: e.sketch.addPoint([30, 0]), radius: 4)
    u.sketch = e
    u.beginSketchDrag()
    e = u.sketch!
    for x in [31.0, 32, 33] { e.sketch.points[e.sketch.circles[0].center].x = x; u.sketch = e }
    u.endSketchDrag()
    u.undoSketch()
    check(u.sketch?.sketch.points[u.sketch!.sketch.circles[0].center].x == 30, "undo reverts a whole drag")
    u.undoSketch()
    check(u.sketch?.sketch.circles.isEmpty == true && u.sketch?.sketch.lines.count == 4, "undo removes the circle")
    u.redoSketch()
    check(u.sketch?.sketch.circles.count == 1, "redo restores it")

    sketchOps()
    modelingOps()

    // Settings round-trip through the document.
    var settings = ProjectSettings()
    settings.units = .inch
    settings.appearance[1] = Appearance(color: [1, 0, 0], metallic: 1, roughness: 0.2)
    settings.hiddenBodies = [2]
    let data = try! JSONEncoder().encode(settings)
    check(try! JSONDecoder().decode(ProjectSettings.self, from: data) == settings, "project settings round-trip")
}

/// Trim, offset, corner fillet, disconnect and symmetry on plain sketches.
@MainActor func sketchOps() {
    // A cross: trimming the right half of the horizontal line at the crossing.
    var t = Sketch(plane: .xy)
    let h0 = t.addPoint([-10, 0]), h1 = t.addPoint([10, 0]), v0 = t.addPoint([0, -10]), v1 = t.addPoint([0, 10])
    t.addLine(h0, h1)
    t.addLine(v0, v1)
    check(t.trim(.line(0), at: [6, 0]), "trim cuts the line")
    let right = t.lines.map { max(t.points[$0.a].x, t.points[$0.b].x) }.max() ?? 99
    check(t.lines.count == 2 && abs(right) < 1e-9, "trimmed line now ends at the crossing (max x \(right))")
    check(t.constraints.contains { $0.kind == .horizontal }, "trimmed piece keeps horizontal")

    // Circle crossed by a line: trimming one side leaves an arc.
    var c = Sketch(plane: .xy)
    c.addCircle(center: c.addPoint([0, 0]), radius: 10)
    c.addLine(c.addPoint([-20, 0]), c.addPoint([20, 0]))
    check(c.trim(.circle(0), at: [0, -10]), "trim a circle")
    let arc = c.arcAngles(0)
    check(c.circles[0].isArc && abs((arc?.sweep ?? 0) - .pi) < 1e-6, "the upper half remains as an arc")

    // Offset a rectangle outward by 2: a 24 x 14 rectangle around 20 x 10.
    var o = Sketch(plane: .xy)
    o.addRectangle([0, 0], [20, 10], start: nil, end: nil)
    let made = o.offset((0..<4).map { .line($0) }, distance: -2)
    let xs = made.compactMap { r -> [Double]? in
        guard case let .line(l) = r else { return nil }
        return [o.points[o.lines[l].a].x, o.points[o.lines[l].b].x]
    }.flatMap { $0 }
    check(made.count == 4 && abs((xs.max() ?? 0) - (xs.min() ?? 0) - 24) < 1e-6, "offset rectangle is 24 wide (\(xs))")
    check(o.profileCount == 2, "inner and outer rectangles are both profiles")

    // Fillet a rectangle corner with radius 3.
    var f = Sketch(plane: .xy)
    f.addRectangle([0, 0], [20, 10], start: nil, end: nil)
    let corner = f.lines[0].b
    check(f.fillet(corner: corner, radius: 3) == nil && f.circles.count == 1, "corner fillet adds an arc")
    check(f.solve() != nil && f.profileCount == 1 && abs(f.circles[0].radius - 3) < 1e-6, "filleted outline is still one profile")

    // Disconnect a shared corner.
    var d = Sketch(plane: .xy)
    d.addRectangle([0, 0], [10, 10], start: nil, end: nil)
    d.disconnect(d.lines[0].b)
    check(d.profileCount == 0, "disconnecting a corner opens the loop")

    // Symmetric points about the Y axis.
    let store = ModelStore(features: [])
    store.startSketch(on: .xy)
    var s = store.sketch!
    let axis0 = s.sketch.addPoint([0, -20]), axis1 = s.sketch.addPoint([0, 20])
    s.sketch.addLine(axis0, axis1)
    let p = s.sketch.addPoint([8, 3]), q = s.sketch.addPoint([-3, 6])
    s.sketch.markers += [p, q]
    s.resolve()
    s.selection = [.point(p), .point(q), .line(0)]
    check(s.availableConstraints == [.symmetric], "two points and a line offer symmetry")
    check(s.constrain(.symmetric) == nil, "symmetry applies")
    // The axis line isn't fixed, so the solver may slide it; mirror about where it is.
    let axisX = s.sketch.points[s.sketch.lines[0].a].x
    check(abs(s.sketch.points[p].x + s.sketch.points[q].x - 2 * axisX) < 1e-6 && abs(s.sketch.points[p].y - s.sketch.points[q].y) < 1e-6,
          "points mirror about the axis line")
}

/// Variables, translate, rotate, align, projection, axes and splines.
@MainActor func modelingOps() {
    let ev = ExpressionEvaluator([Variable(name: "w", expression: "20"), Variable(name: "h", expression: "w / 2 + 5")])
    check(ev.evaluate("w * 2 + h") == 55, "expressions use variables (\(ev.evaluate("w * 2 + h") ?? -1))")
    check(ev.evaluate("max(w, 3) - sqrt(16)") == 16 && ev.evaluate("2 +") == nil, "functions, and bad input is rejected")

    // A box whose width is bound to a variable follows it.
    let store = ModelStore(features: [])
    store.settings.variables = [Variable(name: "width", expression: "30")]
    store.addPrimitive(.box)
    store.bindDraft(0, to: "width * 2")
    store.applyDraft()
    func size(_ b: Int) -> SIMD3<Double> { let p = store.properties(b)!; return p.bboxMax - p.bboxMin }
    check(abs(size(store.bodyIds[0]).x - 60) < 0.5, "bound width is 60 (\(size(store.bodyIds[0]).x))")
    store.settings.variables[0].expression = "25"
    check(abs(size(store.bodyIds[0]).x - 50) < 0.5, "changing the variable rebuilds to 50 (\(size(store.bodyIds[0]).x))")

    // Translate 15 along Y.
    let b = store.bodyIds[0]
    let y0 = store.properties(b)!.bboxMin.y
    store.selection = [.body(b)]
    store.translateSelection()
    store.updateDraft(store.draft!.feature.op.setting(choice: 0, to: 1).setting(0, to: 15))
    store.applyDraft()
    check(abs(store.properties(b)!.bboxMin.y - y0 - 15) < 0.5, "translate moves 15 along Y")

    // Rotate 90° about the vertical axis through the body: X and Y sizes swap.
    store.selection = [.body(b)]
    store.rotateSelection()
    store.applyDraft()
    check(abs(size(b).y - 50) < 0.5 && abs(size(b).x - 40) < 0.5, "rotate swaps width and depth (\(size(b)))")

    // Align a sphere to the box: centred on X/Y, resting on Z min.
    store.addPrimitive(.sphere)
    store.updateDraft(store.draft!.feature.op.setting(10, to: 200))
    store.applyDraft()
    let sphere = store.bodyIds.last!
    store.selection = [.body(sphere), .body(b)]
    store.alignSelection()
    store.applyDraft()
    let pb = store.properties(b)!, ps = store.properties(sphere)!
    check(abs((pb.bboxMin.x + pb.bboxMax.x) / 2 - (ps.bboxMin.x + ps.bboxMax.x) / 2) < 0.5 && abs(pb.bboxMin.z - ps.bboxMin.z) < 0.5,
          "align centres the sphere on the box")

    // Axis from Z, projection of the box onto the top plane.
    store.selection = []
    store.addAxis()
    check(store.referenceItems.contains(where: \.isAxis), "axis item exists")
    store.selection = [.body(b)]
    store.projectSelection()
    let projection = store.referenceItems.first { $0.name.hasPrefix("Projection") }
    if case let .sketch(sk)? = projection?.content {
        check(!sk.lines.isEmpty, "projection makes sketch lines (\(sk.lines.count))")
    } else {
        check(false, "projection is a sketch item")
    }

    // A closed spline and an ellipse are profiles and extrude.
    var sp = Sketch(plane: .xy)
    let ids = [[0.0, 0], [20, -5], [30, 10], [10, 20]].map { sp.addPoint(SIMD2($0[0], $0[1])) }
    sp.splines.append(SketchSpline(points: ids, closed: true))
    sp.ellipses.append(SketchEllipse(center: sp.addPoint([60, 0]), majorRadius: 10, minorRadius: 5))
    check(sp.profileCount == 2, "closed spline and ellipse are profiles")
    let e = ModelStore(features: [Feature(op: .extrude(sketch: sp, distance: 10, mode: .newBody, target: nil))])
    let vol = e.bodyIds.compactMap { e.properties($0)?.volume }.reduce(0, +)
    check(e.errors.isEmpty && vol > Double.pi * 50 * 10, "spline and ellipse extrude (\(vol))")
}

MainActor.assumeIsolated { run() }
print(failures == 0 ? "\nall sketch flow checks passed" : "\n\(failures) failed")
exit(failures == 0 ? 0 : 1)
