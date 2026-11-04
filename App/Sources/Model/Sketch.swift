import Foundation
import simd

/// A plane in world space used for sketches and mirroring.
struct SketchPlane: Codable, Equatable {
    var origin: SIMD3<Double>
    var normal: SIMD3<Double>
    var xDir: SIMD3<Double>

    var yDir: SIMD3<Double> { simd_cross(normal, xDir) }

    static let xy = SketchPlane(origin: .zero, normal: [0, 0, 1], xDir: [1, 0, 0])
    static let xz = SketchPlane(origin: .zero, normal: [0, -1, 0], xDir: [1, 0, 0])
    static let yz = SketchPlane(origin: .zero, normal: [1, 0, 0], xDir: [0, 1, 0])

    func world(_ p: SIMD2<Double>) -> SIMD3<Double> { origin + xDir * p.x + yDir * p.y }

    func local(_ p: SIMD3<Double>) -> SIMD2<Double> {
        let d = p - origin
        return [simd_dot(d, xDir), simd_dot(d, yDir)]
    }

    var bridged: O3DPlane { O3DPlane(origin: origin, normal: normal, xDir: xDir) }
}

struct SketchLine: Codable, Equatable {
    var a: Int
    var b: Int
    var construction = false
}

/// A full circle, or an arc when `start` and `end` are set. Arcs run
/// counter-clockwise from start to end and keep both points on the circle,
/// so every circle constraint (radius, tangent, equal) also applies to arcs.
struct SketchCircle: Codable, Equatable {
    var center: Int
    var radius: Double
    var construction = false
    var start: Int? = nil
    var end: Int? = nil

    var isArc: Bool { start != nil && end != nil }
}

/// A pointer position on the sketch plane, snapped to an existing point or
/// the grid. `index` is set when it landed on an existing point.
struct SketchSnap {
    var position: SIMD2<Double>
    var index: Int?
    /// What it snapped to ("Midpoint", "Quadrant", …), for the snapping hint.
    var hint: String? = nil
    /// Alignment guides through existing points, as line segments.
    var guides: [[SIMD2<Double>]] = []
}

/// Circle through three points, or nil when they are (nearly) collinear.
func circleThrough(_ a: SIMD2<Double>, _ b: SIMD2<Double>, _ c: SIMD2<Double>) -> (center: SIMD2<Double>, radius: Double)? {
    let d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y))
    guard abs(d) > 1e-9 else { return nil }
    let a2 = simd_length_squared(a), b2 = simd_length_squared(b), c2 = simd_length_squared(c)
    let center = SIMD2((a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d,
                       (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d)
    return (center, simd_distance(center, a))
}

/// Points along the arc from `a` to `b` that passes through `through`.
func arcOutline(_ a: SIMD2<Double>, _ b: SIMD2<Double>, through m: SIMD2<Double>) -> [SIMD2<Double>] {
    guard let (c, r) = circleThrough(a, b, m) else { return [a, b] }
    let t0 = atan2(a.y - c.y, a.x - c.x)
    func ccwSweep(to p: SIMD2<Double>) -> Double {
        var s = atan2(p.y - c.y, p.x - c.x) - t0
        while s < 0 { s += 2 * .pi }
        return s
    }
    // Go the way that passes the middle point.
    var sweep = ccwSweep(to: b)
    if ccwSweep(to: m) > sweep { sweep -= 2 * .pi }
    return (0...48).map { i in
        let t = t0 + sweep * Double(i) / 48
        return c + SIMD2(cos(t), sin(t)) * r
    }
}

/// A smooth curve through its points (which are ordinary, constrainable
/// sketch points).
struct SketchSpline: Codable, Equatable {
    var points: [Int]
    var closed = false
    var construction = false
    /// The points are B-spline control points (the curve is pulled towards them)
    /// rather than points the curve passes through.
    var controlPoints = false

    init(points: [Int], closed: Bool = false, construction: Bool = false, controlPoints: Bool = false) {
        self.points = points
        self.closed = closed
        self.construction = construction
        self.controlPoints = controlPoints
    }

    private enum CodingKeys: String, CodingKey { case points, closed, construction, controlPoints }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        points = try c.decode([Int].self, forKey: .points)
        closed = try c.decodeIfPresent(Bool.self, forKey: .closed) ?? false
        construction = try c.decodeIfPresent(Bool.self, forKey: .construction) ?? false
        controlPoints = try c.decodeIfPresent(Bool.self, forKey: .controlPoints) ?? false
    }

    /// The curve as a polyline, for drawing and picking.
    func samples(_ all: [SIMD2<Double>]) -> [SIMD2<Double>] {
        let pts = points.map { all[$0] }
        return controlPoints ? bsplineSamples(pts, closed: closed) : splineSamples(pts, closed: closed)
    }

    /// Points the kernel interpolates: the fit points, or dense samples of the
    /// control-point curve (close enough to be the same curve).
    func fitPoints(_ all: [SIMD2<Double>]) -> [SIMD2<Double>] {
        guard controlPoints else { return points.map { all[$0] } }
        var s = bsplineSamples(points.map { all[$0] }, closed: closed, perSpan: 8)
        if closed, s.count > 1 { s.removeLast() }
        return s
    }
}

/// A uniform cubic B-spline. Open curves are clamped so they start and end on
/// the first and last control points.
func bsplineSamples(_ pts: [SIMD2<Double>], closed: Bool, perSpan: Int = 16) -> [SIMD2<Double>] {
    guard pts.count >= 3 else { return pts }
    let ctrl = closed ? pts + pts.prefix(3) : [pts[0], pts[0]] + pts + [pts[pts.count - 1], pts[pts.count - 1]]
    var out: [SIMD2<Double>] = []
    for i in 0..<(ctrl.count - 3) {
        let p0 = ctrl[i], p1 = ctrl[i + 1], p2 = ctrl[i + 2], p3 = ctrl[i + 3]
        for k in (i == 0 ? 0 : 1)...perSpan {
            let t = Double(k) / Double(perSpan), t2 = t * t, t3 = t2 * t
            let b0 = (1 - t) * (1 - t) * (1 - t) / 6
            let b1 = (3 * t3 - 6 * t2 + 4) / 6
            let b2 = (-3 * t3 + 3 * t2 + 3 * t + 1) / 6
            let b3 = t3 / 6
            out.append(p0 * b0 + p1 * b1 + p2 * b2 + p3 * b3)
        }
    }
    return out
}

/// An ellipse around a centre point; its radii are plain values.
struct SketchEllipse: Codable, Equatable {
    var center: Int
    var majorRadius: Double
    var minorRadius: Double
    var rotation: Double = 0
    var construction = false
}

/// Display samples of a spline through `pts` (centripetal Catmull-Rom).
func splineSamples(_ pts: [SIMD2<Double>], closed: Bool, perSpan: Int = 16) -> [SIMD2<Double>] {
    guard pts.count >= 2 else { return pts }
    let n = pts.count
    func p(_ i: Int) -> SIMD2<Double> {
        if closed { return pts[((i % n) + n) % n] }
        if i < 0 { return pts[0] * 2 - pts[1] }
        if i >= n { return pts[n - 1] * 2 - pts[n - 2] }
        return pts[i]
    }
    var out: [SIMD2<Double>] = [pts[0]]
    for i in 0..<(closed ? n : n - 1) {
        let p0 = p(i - 1), p1 = p(i), p2 = p(i + 1), p3 = p(i + 2)
        func knot(_ a: SIMD2<Double>, _ b: SIMD2<Double>) -> Double { max(pow(simd_distance(a, b), 0.5), 1e-6) }
        let t1 = knot(p0, p1), t2 = t1 + knot(p1, p2), t3 = t2 + knot(p2, p3)
        for k in 1...perSpan {
            let t = t1 + (t2 - t1) * Double(k) / Double(perSpan)
            let a1 = p0 * ((t1 - t) / t1) + p1 * (t / t1)
            let a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1))
            let a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2))
            let b1 = a1 * ((t2 - t) / t2) + a2 * (t / t2)
            let b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1))
            out.append(b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1)))
        }
    }
    return out
}

func ellipseSamples(_ e: SketchEllipse, center c: SIMD2<Double>, count: Int = 96) -> [SIMD2<Double>] {
    let u = SIMD2(cos(e.rotation), sin(e.rotation)), v = SIMD2(-u.y, u.x)
    return (0...count).map { k in
        let t = Double(k) / Double(count) * 2 * .pi
        return c + u * (cos(t) * e.majorRadius) + v * (sin(t) * e.minorRadius)
    }
}

/// Something selectable inside a sketch.
enum SketchRef: Hashable {
    case point(Int), line(Int), circle(Int), constraint(Int), spline(Int), ellipse(Int)
}

enum SketchEntityKind { case point, line, circle }

struct SketchConstraint: Codable, Equatable {
    /// Raw values are stored in documents; keep them stable.
    enum Kind: String, Codable, CaseIterable {
        case coincident, horizontal, vertical, parallel, perpendicular, equalLength, equalRadius
        case pointOnLine, pointOnCircle, tangent, midpoint, fixed
        case length, distance, horizontalDistance, verticalDistance, radius, angle
        case symmetric

        /// What `a` and `b` index into.
        var arguments: (SketchEntityKind, SketchEntityKind?) {
            switch self {
            case .coincident, .distance, .horizontalDistance, .verticalDistance, .symmetric: return (.point, .point)
            case .horizontal, .vertical, .length: return (.line, nil)
            case .parallel, .perpendicular, .equalLength, .angle: return (.line, .line)
            case .equalRadius: return (.circle, .circle)
            case .pointOnLine, .midpoint: return (.point, .line)
            case .pointOnCircle: return (.point, .circle)
            case .tangent: return (.line, .circle)
            case .fixed: return (.point, nil)
            case .radius: return (.circle, nil)
            }
        }

        var isDimension: Bool {
            switch self {
            case .length, .distance, .horizontalDistance, .verticalDistance, .radius, .angle: return true
            default: return false
            }
        }

        var title: String {
            switch self {
            case .coincident: return "Coincident"
            case .horizontal: return "Horizontal"
            case .vertical: return "Vertical"
            case .parallel: return "Parallel"
            case .perpendicular: return "Perpendicular"
            case .equalLength, .equalRadius: return "Equal"
            case .pointOnLine, .pointOnCircle: return "On curve"
            case .tangent: return "Tangent"
            case .midpoint: return "Midpoint"
            case .fixed: return "Lock"
            case .symmetric: return "Symmetry"
            case .length: return "Length"
            case .distance: return "Distance"
            case .horizontalDistance: return "Horizontal distance"
            case .verticalDistance: return "Vertical distance"
            case .radius: return "Radius"
            case .angle: return "Angle"
            }
        }

        var symbol: String {
            switch self {
            case .coincident: return "smallcircle.filled.circle"
            case .horizontal: return "arrow.left.and.right"
            case .vertical: return "arrow.up.and.down"
            case .parallel: return "equal"
            case .perpendicular: return "angle"
            case .equalLength, .equalRadius: return "equal.circle"
            case .pointOnLine, .pointOnCircle: return "point.topleft.down.to.point.bottomright.curvepath"
            case .tangent: return "circle.and.line.horizontal"
            case .midpoint: return "line.diagonal"
            case .fixed: return "lock"
            case .symmetric: return "arrow.left.and.line.vertical.and.arrow.right"
            case .length, .distance: return "ruler"
            case .horizontalDistance: return "arrow.left.and.right.square"
            case .verticalDistance: return "arrow.up.and.down.square"
            case .radius: return "circle.circle"
            case .angle: return "angle"
            }
        }

        /// Short glyph drawn next to the geometry it constrains.
        var glyph: String {
            switch self {
            case .horizontal: return "H"
            case .vertical: return "V"
            case .parallel: return "∥"
            case .perpendicular: return "⊥"
            case .equalLength, .equalRadius: return "="
            case .pointOnLine, .pointOnCircle: return "∘"
            case .tangent: return "T"
            case .midpoint: return "M"
            case .fixed: return "⚓︎"
            case .symmetric: return "⇹"
            default: return ""
            }
        }

        var bridged: O3DConstraintType {
            switch self {
            case .coincident: return .coincident
            case .horizontal: return .horizontal
            case .vertical: return .vertical
            case .parallel: return .parallel
            case .perpendicular: return .perpendicular
            case .equalLength: return .equalLength
            case .equalRadius: return .equalRadius
            case .pointOnLine: return .pointOnLine
            case .pointOnCircle: return .pointOnCircle
            case .tangent: return .tangent
            case .midpoint: return .midpoint
            case .fixed: return .fixed
            case .length: return .length
            case .distance: return .distance
            case .horizontalDistance: return .horizontalDistance
            case .verticalDistance: return .verticalDistance
            case .radius: return .radius
            case .angle: return .angle
            case .symmetric: return .symmetric
            }
        }
    }

    var kind: Kind
    var a: Int
    var b: Int = -1
    var value: Double = 0
    var value2: Double = 0
    /// Third entity: the mirror line of a symmetric constraint.
    var c: Int?

    var unit: String { kind == .angle ? "°" : "mm" }
}

/// Result of the last solve, used to colour geometry and report freedom.
struct SketchStatus: Equatable {
    var degreesOfFreedom = 0
    var fixedPoints: [Bool] = []
    var fixedCircles: [Bool] = []

    var fullyConstrained: Bool { degreesOfFreedom == 0 }
}

/// A constrained 2D sketch on a plane. Point 0 is the plane origin and never
/// moves; lines and circles reference points by index.
struct Sketch: Codable, Equatable {
    var plane: SketchPlane
    var points: [SIMD2<Double>] = [.zero]
    var lines: [SketchLine] = []
    var circles: [SketchCircle] = []
    var constraints: [SketchConstraint] = []
    /// Standalone points placed with the Point tool.
    var markers: [Int] = []
    var splines: [SketchSpline] = []
    var ellipses: [SketchEllipse] = []

    init(plane: SketchPlane) {
        self.plane = plane
    }

    var isEmpty: Bool { lines.isEmpty && circles.isEmpty && splines.isEmpty && ellipses.isEmpty }

    // MARK: Building

    mutating func addPoint(_ p: SIMD2<Double>) -> Int {
        points.append(p)
        return points.count - 1
    }

    /// Adds a line and infers horizontal / vertical when it is drawn axis-aligned.
    @discardableResult
    mutating func addLine(_ a: Int, _ b: Int) -> Int? {
        guard a != b, simd_distance(points[a], points[b]) > 1e-9 else { return nil }
        lines.append(SketchLine(a: a, b: b))
        let l = lines.count - 1
        let d = points[b] - points[a]
        if abs(d.y) < 1e-9 { constraints.append(.init(kind: .horizontal, a: l)) }
        else if abs(d.x) < 1e-9 { constraints.append(.init(kind: .vertical, a: l)) }
        return l
    }

    /// Four lines held square by horizontal and vertical constraints.
    mutating func addRectangle(_ p0: SIMD2<Double>, _ p1: SIMD2<Double>, start: Int?, end: Int?) {
        guard abs(p0.x - p1.x) > 1e-9, abs(p0.y - p1.y) > 1e-9 else { return }
        let c0 = start ?? addPoint(p0)
        let c1 = addPoint([p1.x, p0.y])
        let c2 = end ?? addPoint(p1)
        let c3 = addPoint([p0.x, p1.y])
        for (a, b) in [(c0, c1), (c1, c2), (c2, c3), (c3, c0)] { addLine(a, b) }
    }

    mutating func addCircle(center: Int, radius: Double) {
        guard radius > 1e-9 else { return }
        circles.append(SketchCircle(center: center, radius: radius))
    }

    /// Counter-clockwise arc from `start` to `end` around `center`.
    mutating func addArc(center: Int, start: Int, end: Int) {
        let r = simd_distance(points[center], points[start])
        guard r > 1e-9, start != end else { return }
        circles.append(SketchCircle(center: center, radius: r, start: start, end: end))
    }

    /// Rectangle drawn from its center to a corner.
    mutating func addCenterRectangle(center: SIMD2<Double>, corner: SIMD2<Double>, centerIndex: Int? = nil) {
        let d = corner - center
        guard abs(d.x) > 1e-9, abs(d.y) > 1e-9 else { return }
        let c = centerIndex ?? addPoint(center)
        let firstLine = lines.count
        addRectangle(center - d, center + d, start: nil, end: nil)
        // Keep the center point in the middle of a diagonal.
        let corner0 = lines[firstLine].a, corner2 = lines[firstLine + 1].b
        let diagonal = lines.count
        lines.append(SketchLine(a: corner0, b: corner2, construction: true))
        constraints.append(SketchConstraint(kind: .midpoint, a: c, b: diagonal))
    }

    /// Regular polygon: equal sides with every corner on a construction circle.
    mutating func addPolygon(center: SIMD2<Double>, corner: SIMD2<Double>, sides: Int, centerIndex: Int? = nil) {
        let r = simd_distance(center, corner)
        guard r > 1e-9, sides >= 3 else { return }
        let c = centerIndex ?? addPoint(center)
        let start = atan2(corner.y - center.y, corner.x - center.x)
        let ids = (0..<sides).map { i -> Int in
            let t = start + Double(i) / Double(sides) * 2 * .pi
            return addPoint(center + SIMD2(cos(t), sin(t)) * r)
        }
        circles.append(SketchCircle(center: c, radius: r, construction: true))
        let ring = circles.count - 1
        let first = lines.count
        for (a, b) in zip(ids, ids.dropFirst() + ids.prefix(1)) { addLine(a, b) }
        for i in ids { constraints.append(SketchConstraint(kind: .pointOnCircle, a: i, b: ring)) }
        for l in (first + 1)..<lines.count { constraints.append(SketchConstraint(kind: .equalLength, a: first, b: l)) }
    }

    /// Start and sweep angles of an arc, sweep in (0, 2π].
    func arcAngles(_ i: Int) -> (start: Double, sweep: Double)? {
        let c = circles[i]
        guard let s = c.start, let e = c.end else { return nil }
        let center = points[c.center]
        let a0 = atan2(points[s].y - center.y, points[s].x - center.x)
        let a1 = atan2(points[e].y - center.y, points[e].x - center.x)
        var sweep = a1 - a0
        while sweep <= 1e-9 { sweep += 2 * .pi }
        return (a0, sweep)
    }

    // MARK: Constraints

    /// Current measured value of a dimension, used as its initial value.
    func measure(_ kind: SketchConstraint.Kind, _ a: Int, _ b: Int) -> Double {
        switch kind {
        case .length: return simd_distance(points[lines[a].a], points[lines[a].b])
        case .distance: return simd_distance(points[a], points[b])
        case .horizontalDistance: return points[b].x - points[a].x
        case .verticalDistance: return points[b].y - points[a].y
        case .radius: return circles[a].radius
        case .angle:
            let u = direction(a), v = direction(b)
            return atan2(u.x * v.y - u.y * v.x, simd_dot(u, v)) * 180 / .pi
        default: return 0
        }
    }

    func direction(_ line: Int) -> SIMD2<Double> { points[lines[line].b] - points[lines[line].a] }

    func midpoint(_ line: Int) -> SIMD2<Double> { (points[lines[line].a] + points[lines[line].b]) / 2 }

    /// Solves the constraints, optionally pulling dragged points towards
    /// targets. Returns nil and leaves the sketch untouched on conflict.
    mutating func solve(drag: [Int: SIMD2<Double>] = [:]) -> SketchStatus? {
        let solver = O3DSketchSolver()
        for p in points { solver.addPoint(p) }
        for l in lines { solver.addLine(from: l.a, to: l.b) }
        for c in circles { solver.addCircle(at: c.center, radius: c.radius) }
        for (i, c) in circles.enumerated() {
            guard let start = c.start, let end = c.end else { continue }
            solver.add(.pointOnCircle, a: start, b: i, value: 0, value2: 0)
            solver.add(.pointOnCircle, a: end, b: i, value: 0, value2: 0)
        }
        for c in constraints {
            let value = c.kind == .radius ? max(c.value, 1e-6) : c.value
            if let third = c.c {
                solver.add(c.kind.bridged, a: c.a, b: c.b, c: third, value: value)
            } else {
                solver.add(c.kind.bridged, a: c.a, b: c.b, value: value, value2: c.value2)
            }
        }
        solver.pinPoint(0)
        for (p, target) in drag.sorted(by: { $0.key < $1.key }) { solver.dragPoint(p, to: target) }
        guard solver.solve() == .solved else { return nil }
        for i in points.indices where i != 0 { points[i] = solver.point(i) }
        for i in circles.indices { circles[i].radius = max(solver.radius(i), 1e-6) }
        return SketchStatus(degreesOfFreedom: solver.degreesOfFreedom,
                            fixedPoints: points.indices.map { solver.isPointFixed($0) },
                            fixedCircles: circles.indices.map { solver.isCircleFixed($0) })
    }

    // MARK: Deleting

    /// Removes entities and constraints, everything that depends on them, and
    /// points no curve uses any more; indices are compacted.
    mutating func delete(_ refs: Set<SketchRef>) {
        var deadPoints = Set<Int>(), deadLines = Set<Int>(), deadCircles = Set<Int>(), deadConstraints = Set<Int>()
        var deadSplines = Set<Int>(), deadEllipses = Set<Int>()
        for r in refs {
            switch r {
            case let .point(i) where i != 0: deadPoints.insert(i)
            case let .line(i): deadLines.insert(i)
            case let .circle(i): deadCircles.insert(i)
            case let .constraint(i): deadConstraints.insert(i)
            case let .spline(i): deadSplines.insert(i)
            case let .ellipse(i): deadEllipses.insert(i)
            default: break
            }
        }
        for (i, l) in lines.enumerated() where deadPoints.contains(l.a) || deadPoints.contains(l.b) { deadLines.insert(i) }
        for (i, c) in circles.enumerated() where deadPoints.contains(c.center) || c.start.map(deadPoints.contains) == true
            || c.end.map(deadPoints.contains) == true { deadCircles.insert(i) }
        let liveMarkers = markers.filter { !deadPoints.contains($0) }
        for (i, sp) in splines.enumerated() where sp.points.contains(where: deadPoints.contains) { deadSplines.insert(i) }
        for (i, e) in ellipses.enumerated() where deadPoints.contains(e.center) { deadEllipses.insert(i) }

        var used = Set<Int>([0])
        for (i, l) in lines.enumerated() where !deadLines.contains(i) { used.formUnion([l.a, l.b]) }
        for (i, c) in circles.enumerated() where !deadCircles.contains(i) {
            used.insert(c.center)
            if let s = c.start, let e = c.end { used.formUnion([s, e]) }
        }
        used.formUnion(liveMarkers)
        for (i, sp) in splines.enumerated() where !deadSplines.contains(i) { used.formUnion(sp.points) }
        for (i, e) in ellipses.enumerated() where !deadEllipses.contains(i) { used.insert(e.center) }
        deadPoints = Set(points.indices).subtracting(used)

        func remap(_ dead: Set<Int>, count: Int) -> [Int?] {
            var map: [Int?] = [], next = 0
            for i in 0..<count {
                if dead.contains(i) { map.append(nil) } else { map.append(next); next += 1 }
            }
            return map
        }
        let pointMap = remap(deadPoints, count: points.count)
        let lineMap = remap(deadLines, count: lines.count)
        let circleMap = remap(deadCircles, count: circles.count)
        func map(_ kind: SketchEntityKind, _ i: Int) -> Int? {
            switch kind {
            case .point: return pointMap[i]
            case .line: return lineMap[i]
            case .circle: return circleMap[i]
            }
        }

        constraints = constraints.enumerated().compactMap { i, c in
            guard !deadConstraints.contains(i) else { return nil }
            var c = c
            let (ka, kb) = c.kind.arguments
            guard let a = map(ka, c.a) else { return nil }
            c.a = a
            if let kb {
                guard let b = map(kb, c.b) else { return nil }
                c.b = b
            }
            if let third = c.c {
                guard let l = lineMap[third] else { return nil }
                c.c = l
            }
            return c
        }
        lines = lines.enumerated().compactMap { i, l in
            guard lineMap[i] != nil, let a = pointMap[l.a], let b = pointMap[l.b] else { return nil }
            return SketchLine(a: a, b: b, construction: l.construction)
        }
        circles = circles.enumerated().compactMap { i, c in
            guard circleMap[i] != nil, let center = pointMap[c.center] else { return nil }
            var out = SketchCircle(center: center, radius: c.radius, construction: c.construction)
            if let s = c.start, let e = c.end {
                guard let ns = pointMap[s], let ne = pointMap[e] else { return nil }
                out.start = ns
                out.end = ne
            }
            return out
        }
        markers = liveMarkers.compactMap { pointMap[$0] }
        splines = splines.enumerated().compactMap { i, sp in
            guard !deadSplines.contains(i) else { return nil }
            var out = sp
            out.points = sp.points.compactMap { pointMap[$0] }
            return out.points.count == sp.points.count ? out : nil
        }
        ellipses = ellipses.enumerated().compactMap { i, e in
            guard !deadEllipses.contains(i), let c = pointMap[e.center] else { return nil }
            var out = e
            out.center = c
            return out
        }
        points = points.enumerated().compactMap { pointMap[$0.offset] != nil ? $0.element : nil }
    }

    // MARK: Profiles

    /// A line or arc as an edge of the profile graph.
    private enum Edge: Hashable { case line(Int), arc(Int), spline(Int) }

    private func coincidenceRoots() -> (Int) -> Int {
        var parent = Array(points.indices)
        func root(_ i: Int) -> Int {
            var i = i
            while parent[i] != i { parent[i] = parent[parent[i]]; i = parent[i] }
            return i
        }
        for c in constraints where c.kind == .coincident && points.indices.contains(c.a) && points.indices.contains(c.b) {
            parent[root(c.a)] = root(c.b)
        }
        let flat = points.indices.map(root)
        return { flat[$0] }
    }

    private func ends(_ e: Edge) -> (Int, Int) {
        switch e {
        case let .line(i): return (lines[i].a, lines[i].b)
        case let .arc(i): return (circles[i].start!, circles[i].end!)
        case let .spline(i): return (splines[i].points.first!, splines[i].points.last!)
        }
    }

    /// The segment for an edge walked from point `from`.
    private func segment(_ e: Edge, from: Int, root: (Int) -> Int) -> O3DSegment {
        let (a, b) = ends(e)
        let forward = root(a) == root(from)
        let p = points[forward ? a : b], q = points[forward ? b : a]
        switch e {
        case .line: return O3DSegment.line(from: p, to: q)
        case let .arc(i): return O3DSegment.arc(from: p, to: q, center: points[circles[i].center], ccw: forward)
        case let .spline(i):
            var pts = splines[i].fitPoints(points)
            // A control-point curve starts and ends exactly on its end points.
            if splines[i].controlPoints, let f = splines[i].points.first, let l = splines[i].points.last {
                pts[0] = points[f]
                pts[pts.count - 1] = points[l]
            }
            if !forward { pts.reverse() }
            return O3DSegment.splineThrough(pts, count: pts.count, closed: false)
        }
    }

    /// Connected chains of non-construction lines and arcs, each as ordered
    /// edges with the start vertex; `closed` when the chain is a loop.
    private func chains() -> [(edges: [Edge], start: Int, closed: Bool)] {
        let root = coincidenceRoots()
        var edges: [Edge] = lines.indices.filter { !lines[$0].construction }.map { .line($0) }
        edges += circles.indices.filter { circles[$0].isArc && !circles[$0].construction }.map { .arc($0) }
        edges += splines.indices.filter { !splines[$0].closed && !splines[$0].construction && splines[$0].points.count >= 2 }.map { .spline($0) }
        var adjacency: [Int: [Edge]] = [:]
        for e in edges {
            let (a, b) = ends(e)
            guard root(a) != root(b) else { continue }
            adjacency[root(a), default: []].append(e)
            adjacency[root(b), default: []].append(e)
        }
        // Each connected piece without branches is one chain: open when it
        // has dead ends, a loop otherwise.
        var result: [(edges: [Edge], start: Int, closed: Bool)] = []
        var seen = Set<Int>()
        for vertex in adjacency.keys.sorted() where !seen.contains(vertex) {
            var component: [Int] = [], stack = [vertex]
            seen.insert(vertex)
            while let v = stack.popLast() {
                component.append(v)
                for e in adjacency[v] ?? [] {
                    let (a, b) = ends(e)
                    let other = root(a) == v ? root(b) : root(a)
                    if seen.insert(other).inserted { stack.append(other) }
                }
            }
            guard component.allSatisfy({ (adjacency[$0]?.count ?? 0) <= 2 }) else { continue }
            let deadEnds = component.filter { adjacency[$0]?.count == 1 }.sorted()
            let start = deadEnds.first ?? component.min()!
            var chain: [Edge] = [], used = Set<Edge>(), v = start
            while let e = adjacency[v]?.first(where: { !used.contains($0) }) {
                used.insert(e)
                chain.append(e)
                let (a, b) = ends(e)
                v = root(a) == v ? root(b) : root(a)
            }
            result.append((chain, start, deadEnds.isEmpty && chain.count >= 2))
        }
        return result
    }

    /// Closed regions for extrude / revolve: every full circle, plus every
    /// closed loop of lines and arcs. Construction geometry is ignored.
    var profiles: [O3DProfile] {
        let root = coincidenceRoots()
        var result: [O3DProfile] = []
        for chain in chains() where chain.closed {
            var v = chain.start
            var segments: [O3DSegment] = []
            var corners: [SIMD2<Double>] = []
            var allLines = true
            for e in chain.edges {
                segments.append(segment(e, from: v, root: root))
                corners.append(points[v])
                if case .line = e {} else { allLines = false }
                let (a, b) = ends(e)
                v = root(a) == v ? root(b) : root(a)
            }
            if allLines {
                guard corners.count >= 3 else { continue }
                result.append(O3DProfile.polygon(withPoints: corners, count: corners.count))
            } else {
                result.append(O3DProfile.wire(with: segments))
            }
        }
        for c in circles where !c.construction && !c.isArc && c.radius > 1e-9 {
            result.append(O3DProfile.circle(at: points[c.center], radius: c.radius))
        }
        for sp in splines where sp.closed && !sp.construction && sp.points.count >= 3 {
            let pts = sp.fitPoints(points)
            result.append(O3DProfile.wire(with: [O3DSegment.splineThrough(pts, count: pts.count, closed: true)]))
        }
        for e in ellipses where !e.construction && e.majorRadius > 1e-9 && e.minorRadius > 1e-9 {
            result.append(O3DProfile.wire(with: [O3DSegment.ellipse(at: points[e.center], majorRadius: e.majorRadius,
                                                                    minorRadius: e.minorRadius, rotation: e.rotation)]))
        }
        return result
    }

    /// The first open chain (or, failing that, loop) of lines and arcs, as a
    /// sweep path.
    var path: [O3DSegment] {
        let root = coincidenceRoots()
        let all = chains()
        guard let chain = all.first(where: { !$0.closed }) ?? all.first else { return [] }
        var v = chain.start
        return chain.edges.map { e in
            defer { let (a, b) = ends(e); v = root(a) == v ? root(b) : root(a) }
            return segment(e, from: v, root: root)
        }
    }

    var profileCount: Int { profiles.count }

    // MARK: Coding

    private enum CodingKeys: String, CodingKey { case plane, points, lines, circles, constraints, markers, splines, ellipses, shapes }

    /// The first file format stored unconstrained shapes; those become
    /// constrained geometry on load.
    private enum LegacyShape: Codable {
        case rectangle(SIMD2<Double>, SIMD2<Double>)
        case circle(center: SIMD2<Double>, radius: Double)
        case polygon([SIMD2<Double>])
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        plane = try c.decode(SketchPlane.self, forKey: .plane)
        if let legacy = try c.decodeIfPresent([LegacyShape].self, forKey: .shapes) {
            for shape in legacy {
                switch shape {
                case let .rectangle(a, b): addRectangle(a, b, start: nil, end: nil)
                case let .circle(center, radius): addCircle(center: addPoint(center), radius: radius)
                case let .polygon(pts):
                    let ids = pts.map { addPoint($0) }
                    for (a, b) in zip(ids, ids.dropFirst() + ids.prefix(1)) { addLine(a, b) }
                }
            }
            return
        }
        points = try c.decodeIfPresent([SIMD2<Double>].self, forKey: .points) ?? [.zero]
        lines = try c.decodeIfPresent([SketchLine].self, forKey: .lines) ?? []
        circles = try c.decodeIfPresent([SketchCircle].self, forKey: .circles) ?? []
        constraints = try c.decodeIfPresent([SketchConstraint].self, forKey: .constraints) ?? []
        markers = try c.decodeIfPresent([Int].self, forKey: .markers) ?? []
        splines = try c.decodeIfPresent([SketchSpline].self, forKey: .splines) ?? []
        ellipses = try c.decodeIfPresent([SketchEllipse].self, forKey: .ellipses) ?? []
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(plane, forKey: .plane)
        try c.encode(points, forKey: .points)
        try c.encode(lines, forKey: .lines)
        try c.encode(circles, forKey: .circles)
        try c.encode(constraints, forKey: .constraints)
        try c.encode(markers, forKey: .markers)
        try c.encode(splines, forKey: .splines)
        try c.encode(ellipses, forKey: .ellipses)
    }
}
