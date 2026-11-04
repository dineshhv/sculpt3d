import Foundation
import simd

/// A similarity transform of the sketch plane: p' = R·S·(p - pivot) + pivot + shift,
/// optionally mirrored across a line through the pivot.
struct SketchTransform {
    var pivot = SIMD2<Double>(0, 0)
    var angle = 0.0
    var scale = 1.0
    var shift = SIMD2<Double>(0, 0)
    /// Mirror across the line through `pivot` with this direction (before rotating).
    var mirrorAxis: SIMD2<Double>?

    func apply(_ p: SIMD2<Double>) -> SIMD2<Double> {
        var d = p - pivot
        if let m = mirrorAxis {
            let u = simd_normalize(m)
            d = 2 * simd_dot(d, u) * u - d
        }
        d *= scale
        let c = cos(angle), s = sin(angle)
        return pivot + SIMD2(d.x * c - d.y * s, d.x * s + d.y * c) + shift
    }

    /// Mirroring flips the direction of arcs and ellipse rotations.
    var flips: Bool { mirrorAxis != nil }
    /// Constraints tied to the sketch axes or to fixed positions don't survive turning.
    var keepsAxes: Bool { angle == 0 && mirrorAxis.map { abs($0.x) < 1e-12 || abs($0.y) < 1e-12 } ?? true }
}

extension Sketch {
    /// Every point the referenced geometry uses.
    private func points(of refs: [SketchRef]) -> Set<Int> {
        var used = Set<Int>()
        for r in refs {
            switch r {
            case let .point(i): used.insert(i)
            case let .line(i): used.formUnion([lines[i].a, lines[i].b])
            case let .circle(i):
                used.insert(circles[i].center)
                if let s = circles[i].start, let e = circles[i].end { used.formUnion([s, e]) }
            case let .spline(i): used.formUnion(splines[i].points)
            case let .ellipse(i): used.insert(ellipses[i].center)
            case .constraint: break
            }
        }
        return used
    }

    /// The middle of the referenced geometry's points.
    func centroid(of refs: [SketchRef]) -> SIMD2<Double> {
        let pts = points(of: refs).map { points[$0] }
        guard !pts.isEmpty else { return .zero }
        return pts.reduce(.zero, +) / Double(pts.count)
    }

    /// Adds a transformed copy of the referenced geometry and returns its refs.
    /// Constraints between copied entities come along when the transform keeps them true.
    @discardableResult
    mutating func addCopy(of refs: [SketchRef], _ t: SketchTransform) -> [SketchRef] {
        let geometry = refs.filter { if case .constraint = $0 { return false } else { return true } }
        var map: [Int: Int] = [:]
        for p in points(of: geometry).sorted() { map[p] = addPoint(t.apply(points[p])) }
        var lineMap: [Int: Int] = [:], circleMap: [Int: Int] = [:]
        var created: [SketchRef] = []
        for r in geometry {
            switch r {
            case let .point(i):
                if let n = map[i] {
                    markers.append(n)
                    created.append(.point(n))
                }
            case let .line(i):
                var l = lines[i]
                l.a = map[l.a]!
                l.b = map[l.b]!
                lines.append(l)
                lineMap[i] = lines.count - 1
                created.append(.line(lines.count - 1))
            case let .circle(i):
                var c = circles[i]
                c.center = map[c.center]!
                c.radius *= t.scale
                if let s = c.start, let e = c.end {
                    // A mirrored arc runs the other way round.
                    c.start = map[t.flips ? e : s]
                    c.end = map[t.flips ? s : e]
                }
                circles.append(c)
                circleMap[i] = circles.count - 1
                created.append(.circle(circles.count - 1))
            case let .spline(i):
                var sp = splines[i]
                sp.points = sp.points.map { map[$0]! }
                splines.append(sp)
                created.append(.spline(splines.count - 1))
            case let .ellipse(i):
                var e = ellipses[i]
                e.center = map[e.center]!
                e.majorRadius *= t.scale
                e.minorRadius *= t.scale
                if let m = t.mirrorAxis {
                    e.rotation = 2 * atan2(m.y, m.x) - e.rotation
                }
                e.rotation += t.angle
                ellipses.append(e)
                created.append(.ellipse(ellipses.count - 1))
            case .constraint:
                break
            }
        }
        // Carry over constraints whose arguments were all copied.
        for c in constraints where c.c == nil {
            func remap(_ index: Int, _ kind: SketchEntityKind?) -> Int? {
                guard let kind else { return -1 }
                switch kind {
                case .point: return map[index]
                case .line: return lineMap[index]
                case .circle: return circleMap[index]
                }
            }
            let (ka, kb) = c.kind.arguments
            guard let a = remap(c.a, ka), let b = remap(c.b, kb) else { continue }
            switch c.kind {
            case .horizontal, .vertical, .horizontalDistance, .verticalDistance, .angle:
                if !t.keepsAxes { continue }
            case .fixed:
                continue
            default:
                break
            }
            var copy = c
            copy.a = a
            copy.b = kb == nil ? c.b : b
            if [.length, .distance, .horizontalDistance, .verticalDistance, .radius].contains(c.kind) {
                copy.value *= t.scale
            }
            constraints.append(copy)
        }
        return created
    }

    /// Moves the referenced geometry in place (as a transformed copy that replaces it).
    @discardableResult
    mutating func transform(_ refs: [SketchRef], _ t: SketchTransform) -> [SketchRef] {
        let geometry = refs.filter { if case .constraint = $0 { return false } else { return true } }
        let created = addCopy(of: geometry, t)
        delete(Set(geometry.filter { if case .point = $0 { return false } else { return true } }))
        // Deleting compacts the arrays in order, so the copies are still the
        // last entities of each kind.
        var out: [SketchRef] = []
        let nl = created.filter { if case .line = $0 { return true } else { return false } }.count
        let nc = created.filter { if case .circle = $0 { return true } else { return false } }.count
        let ns = created.filter { if case .spline = $0 { return true } else { return false } }.count
        let ne = created.filter { if case .ellipse = $0 { return true } else { return false } }.count
        out += (lines.count - nl..<lines.count).map { .line($0) }
        out += (circles.count - nc..<circles.count).map { .circle($0) }
        out += (splines.count - ns..<splines.count).map { .spline($0) }
        out += (ellipses.count - ne..<ellipses.count).map { .ellipse($0) }
        return out
    }
}
