import Foundation
import simd

/// Editing operations that change sketch topology: trim, offset, corner
/// fillet and disconnect.
extension Sketch {
    // MARK: Intersections

    /// Parameters (0...1) along line `l` where other curves cross it.
    private func cuts(onLine l: Int) -> [Double] {
        let a = points[lines[l].a], d = points[lines[l].b] - a
        var ts: [Double] = []
        for (j, m) in lines.enumerated() where j != l {
            let p = points[m.a], e = points[m.b] - p
            let den = d.x * e.y - d.y * e.x
            guard abs(den) > 1e-12 else { continue }
            let w = p - a
            let t = (w.x * e.y - w.y * e.x) / den, u = (w.x * d.y - w.y * d.x) / den
            if u >= -1e-9, u <= 1 + 1e-9 { ts.append(t) }
        }
        for (j, c) in circles.enumerated() {
            for t in lineCircle(a, d, j) where onCurve(j, a + d * t) { ts.append(t) }
            _ = c
        }
        return ts.filter { $0 > 1e-6 && $0 < 1 - 1e-6 }.sorted()
    }

    /// Angles where other curves cross circle or arc `i`.
    private func cuts(onCircle i: Int) -> [Double] {
        let c = points[circles[i].center], r = circles[i].radius
        var hits: [SIMD2<Double>] = []
        for (j, m) in lines.enumerated() {
            let a = points[m.a], d = points[m.b] - a
            hits += lineCircle(a, d, i).filter { $0 >= -1e-9 && $0 <= 1 + 1e-9 }.map { a + d * $0 }
            _ = j
        }
        for (j, o) in circles.enumerated() where j != i {
            let c2 = points[o.center], r2 = o.radius
            let dist = simd_distance(c, c2)
            guard dist > 1e-9, dist <= r + r2, dist >= abs(r - r2) else { continue }
            let along = (r * r - r2 * r2 + dist * dist) / (2 * dist)
            let h = sqrt(max(r * r - along * along, 0))
            let base = c + (c2 - c) / dist * along, n = SIMD2(-(c2 - c).y, (c2 - c).x) / dist
            hits += [base + n * h, base - n * h].filter { onCurve(j, $0) }
        }
        return hits.filter { onCurve(i, $0) }.map { atan2($0.y - c.y, $0.x - c.x) }
    }

    /// Parameters t where the infinite line a + d·t meets circle i.
    private func lineCircle(_ a: SIMD2<Double>, _ d: SIMD2<Double>, _ i: Int) -> [Double] {
        let c = points[circles[i].center], r = circles[i].radius
        let f = a - c
        let qa = simd_dot(d, d), qb = 2 * simd_dot(f, d), qc = simd_dot(f, f) - r * r
        let disc = qb * qb - 4 * qa * qc
        guard qa > 1e-12, disc >= 0 else { return [] }
        let s = sqrt(disc)
        return [(-qb - s) / (2 * qa), (-qb + s) / (2 * qa)]
    }

    /// Whether a point on circle i's full circle lies within the arc's span.
    private func onCurve(_ i: Int, _ p: SIMD2<Double>) -> Bool {
        guard let (start, sweep) = arcAngles(i) else { return true }
        let c = points[circles[i].center]
        var t = atan2(p.y - c.y, p.x - c.x) - start
        while t < -1e-9 { t += 2 * .pi }
        return t <= sweep + 1e-9
    }

    // MARK: Trim

    /// Removes the piece of a line, circle or arc between the crossings either
    /// side of `point`. Returns false when there was nothing to trim.
    mutating func trim(_ ref: SketchRef, at point: SIMD2<Double>) -> Bool {
        switch ref {
        case let .line(l):
            let a = points[lines[l].a], d = points[lines[l].b] - a
            let t = simd_clamp(simd_dot(point - a, d) / max(simd_length_squared(d), 1e-12), 0, 1)
            let ts = cuts(onLine: l)
            let lo = ts.last { $0 < t } ?? 0, hi = ts.first { $0 > t } ?? 1
            if lo == 0 && hi == 1 {
                delete([.line(l)])
                return true
            }
            let original = lines[l]
            // Constraints that only describe direction still hold for both pieces.
            let directional = constraints.filter { [.horizontal, .vertical].contains($0.kind) && $0.a == l }
            if lo > 0 {
                lines[l].b = addPoint(a + d * lo)
                if hi < 1 {
                    lines.append(SketchLine(a: addPoint(a + d * hi), b: original.b, construction: original.construction))
                    for c in directional { constraints.append(SketchConstraint(kind: c.kind, a: lines.count - 1)) }
                }
            } else {
                lines[l].a = addPoint(a + d * hi)
            }
            // Lengths and other measures of the old line no longer make sense.
            constraints.removeAll { c in
                (c.kind.arguments.0 == .line && c.a == l || c.kind.arguments.1 == .line && c.b == l) && !directional.contains(c)
            }
            delete([])
            return true
        case let .circle(i):
            let c = points[circles[i].center]
            let angles = cuts(onCircle: i)
            let click = atan2(point.y - c.y, point.x - c.x)
            func at(_ t: Double) -> SIMD2<Double> { c + SIMD2(cos(t), sin(t)) * circles[i].radius }
            if let (start, sweep) = arcAngles(i) {
                // Positions along the arc, from its start.
                func along(_ t: Double) -> Double {
                    var u = t - start
                    while u < 0 { u += 2 * .pi }
                    return u
                }
                let us = angles.map(along).filter { $0 > 1e-6 && $0 < sweep - 1e-6 }.sorted()
                let u = along(click)
                let lo = us.last { $0 < u } ?? 0, hi = us.first { $0 > u } ?? sweep
                if lo == 0 && hi == sweep {
                    delete([.circle(i)])
                    return true
                }
                let arc = circles[i]
                if lo > 0 {
                    circles[i].end = addPoint(at(start + lo))
                    if hi < sweep {
                        circles.append(SketchCircle(center: arc.center, radius: arc.radius, construction: arc.construction,
                                                    start: addPoint(at(start + hi)), end: arc.end))
                    }
                } else {
                    circles[i].start = addPoint(at(start + hi))
                }
                delete([])
                return true
            }
            // A full circle needs two crossings; the rest becomes an arc.
            let sorted = angles.sorted()
            guard sorted.count >= 2 else {
                delete([.circle(i)])
                return true
            }
            let hi = sorted.first { $0 > click } ?? sorted[0]
            let lo = sorted.last { $0 < click } ?? sorted[sorted.count - 1]
            circles[i].start = addPoint(at(hi))
            circles[i].end = addPoint(at(lo))
            return true
        default:
            return false
        }
    }

    // MARK: Offset

    /// Parallel copies of lines, circles and arcs `distance` away. Connected
    /// lines stay connected: neighbouring offsets meet at their intersection.
    mutating func offset(_ refs: [SketchRef], distance: Double) -> [SketchRef] {
        var created: [SketchRef] = []
        for ref in refs {
            guard case let .circle(i) = ref else { continue }
            var c = circles[i]
            let r = c.radius + distance
            guard r > 1e-6 else { continue }
            if let (start, sweep) = arcAngles(i) {
                let center = points[c.center]
                c.start = addPoint(center + SIMD2(cos(start), sin(start)) * r)
                c.end = addPoint(center + SIMD2(cos(start + sweep), sin(start + sweep)) * r)
            }
            c.radius = r
            circles.append(c)
            created.append(.circle(circles.count - 1))
        }

        // Order the selected lines into chains through shared points.
        var remaining = refs.compactMap { if case let .line(l) = $0 { return l } else { return nil } }
        while let first = remaining.first {
            var chain = [(line: first, forward: true)]
            remaining.removeFirst()
            func extend(atEnd: Bool) {
                while true {
                    let tip = atEnd ? (chain.last!.forward ? lines[chain.last!.line].b : lines[chain.last!.line].a)
                                    : (chain.first!.forward ? lines[chain.first!.line].a : lines[chain.first!.line].b)
                    guard let k = remaining.firstIndex(where: { lines[$0].a == tip || lines[$0].b == tip }) else { return }
                    let l = remaining.remove(at: k)
                    let forward = atEnd ? lines[l].a == tip : lines[l].b == tip
                    if atEnd { chain.append((l, forward)) } else { chain.insert((l, forward), at: 0) }
                }
            }
            extend(atEnd: true)
            extend(atEnd: false)

            let segs = chain.map { item -> (SIMD2<Double>, SIMD2<Double>) in
                let l = lines[item.line]
                let p = points[item.forward ? l.a : l.b], q = points[item.forward ? l.b : l.a]
                let d = simd_normalize(q - p)
                let n = SIMD2(-d.y, d.x) * distance
                return (p + n, q + n)
            }
            let startPoint = item(chain.first!)
            let endPoint = item(chain.last!, end: true)
            let closed = chain.count > 2 && startPoint == endPoint
            func meet(_ s: (SIMD2<Double>, SIMD2<Double>), _ t: (SIMD2<Double>, SIMD2<Double>)) -> SIMD2<Double> {
                let d = s.1 - s.0, e = t.1 - t.0
                let den = d.x * e.y - d.y * e.x
                guard abs(den) > 1e-12 else { return s.1 }
                let w = t.0 - s.0
                return s.0 + d * ((w.x * e.y - w.y * e.x) / den)
            }
            var corners: [Int] = []
            for k in 0...segs.count {
                if k == 0 {
                    corners.append(addPoint(closed ? meet(segs[segs.count - 1], segs[0]) : segs[0].0))
                } else if k == segs.count {
                    corners.append(closed ? corners[0] : addPoint(segs[k - 1].1))
                } else {
                    corners.append(addPoint(meet(segs[k - 1], segs[k])))
                }
            }
            for k in 0..<segs.count {
                lines.append(SketchLine(a: corners[k], b: corners[k + 1]))
                constraints.append(SketchConstraint(kind: .parallel, a: chain[k].line, b: lines.count - 1))
                created.append(.line(lines.count - 1))
            }
        }
        return created
    }

    private func item(_ i: (line: Int, forward: Bool), end: Bool = false) -> Int {
        let l = lines[i.line]
        return i.forward != end ? l.a : l.b
    }

    // MARK: Corner fillet

    /// Rounds the corner where two lines meet at `corner` with an arc of
    /// `radius`, tangent to both.
    mutating func fillet(corner: Int, radius: Double) -> String? {
        let using = lines.indices.filter { lines[$0].a == corner || lines[$0].b == corner }
        guard using.count == 2 else { return "Pick a corner where exactly two lines meet" }
        let p = points[corner]
        func away(_ l: Int) -> SIMD2<Double> { points[lines[l].a == corner ? lines[l].b : lines[l].a] }
        let u = simd_normalize(away(using[0]) - p), v = simd_normalize(away(using[1]) - p)
        let angle = acos(simd_clamp(simd_dot(u, v), -1, 1))
        guard angle > 1e-3, angle < .pi - 1e-3 else { return "The lines are parallel" }
        let reach = radius / tan(angle / 2)
        guard reach < simd_distance(p, away(using[0])), reach < simd_distance(p, away(using[1])) else {
            return "Radius too large for these lines"
        }
        let t1 = addPoint(p + u * reach), t2 = addPoint(p + v * reach)
        let center = addPoint(p + simd_normalize(u + v) * (radius / sin(angle / 2)))
        for (l, t) in [(using[0], t1), (using[1], t2)] {
            if lines[l].a == corner { lines[l].a = t } else { lines[l].b = t }
        }
        // Counter-clockwise from one tangent point to the other.
        let ccw = (points[t1] - points[center]).x * (points[t2] - points[center]).y
            - (points[t1] - points[center]).y * (points[t2] - points[center]).x > 0
        addArc(center: center, start: ccw ? t1 : t2, end: ccw ? t2 : t1)
        let arc = circles.count - 1
        constraints.append(SketchConstraint(kind: .tangent, a: using[0], b: arc))
        constraints.append(SketchConstraint(kind: .tangent, a: using[1], b: arc))
        constraints.append(SketchConstraint(kind: .radius, a: arc, value: radius))
        delete([])
        return nil
    }

    // MARK: Disconnect

    /// Gives every curve that shares `point` its own copy of it.
    mutating func disconnect(_ point: Int) {
        var first = true
        for i in lines.indices where lines[i].a == point || lines[i].b == point {
            if first { first = false; continue }
            let copy = addPoint(points[point])
            if lines[i].a == point { lines[i].a = copy } else { lines[i].b = copy }
        }
        for i in circles.indices where circles[i].start == point || circles[i].end == point {
            if first { first = false; continue }
            let copy = addPoint(points[point])
            if circles[i].start == point { circles[i].start = copy } else { circles[i].end = copy }
        }
    }
}
