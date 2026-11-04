import Foundation
import simd

/// Constraint and dimension editing on the selected sketch geometry.
extension SketchSession {
    var selectedPoints: [Int] { selection.compactMap { if case let .point(i) = $0 { return i } else { return nil } } }
    var selectedLines: [Int] { selection.compactMap { if case let .line(i) = $0 { return i } else { return nil } } }
    var selectedCircles: [Int] { selection.compactMap { if case let .circle(i) = $0 { return i } else { return nil } } }
    var selectedSplines: [Int] { selection.compactMap { if case let .spline(i) = $0 { return i } else { return nil } } }
    var selectedEllipses: [Int] { selection.compactMap { if case let .ellipse(i) = $0 { return i } else { return nil } } }
    var selectedConstraints: [Int] {
        selection.compactMap { if case let .constraint(i) = $0 { return i } else { return nil } }
    }

    /// The single selected dimension, which the sketch bar lets you edit.
    var selectedDimension: Int? {
        guard selection.count == 1, let i = selectedConstraints.first,
              sketch.constraints.indices.contains(i), sketch.constraints[i].kind.isDimension else { return nil }
        return i
    }

    /// Constraints that make sense for the current selection, in toolbar order.
    var availableConstraints: [SketchConstraint.Kind] {
        let p = selectedPoints.count, l = selectedLines.count, c = selectedCircles.count
        guard selectedConstraints.isEmpty, selectedSplines.isEmpty, selectedEllipses.isEmpty else { return [] }
        var kinds: [SketchConstraint.Kind] = []
        switch (p, l, c) {
        case (0, 1, 0): kinds = [.horizontal, .vertical, .length]
        case (0, 2, 0): kinds = [.parallel, .perpendicular, .equalLength, .angle]
        case (0, _, 0) where l > 2: kinds = [.horizontal, .vertical]
        case (2, 0, 0): kinds = [.coincident, .distance, .horizontalDistance, .verticalDistance, .fixed]
        case (2, 1, 0): kinds = [.symmetric]
        case (_, 0, 0) where p > 0: kinds = [.fixed]
        case (1, 1, 0): kinds = [.pointOnLine, .midpoint]
        case (1, 0, 1): kinds = [.pointOnCircle]
        case (0, 1, 1): kinds = [.tangent]
        case (0, 0, 1): kinds = [.radius]
        case (0, 0, 2): kinds = [.equalRadius, .coincident]
        default: break
        }
        return kinds
    }

    /// Re-solves after geometry changes, keeping the status current.
    mutating func resolve() {
        if let status = sketch.solve() { self.status = status }
    }

    /// Adds `kind` to the selection; returns an error message when it can't.
    mutating func constrain(_ kind: SketchConstraint.Kind) -> String? {
        let pts = selectedPoints, lns = selectedLines, crs = selectedCircles
        var added: [SketchConstraint] = []
        switch kind {
        case .horizontal, .vertical:
            added = lns.map { SketchConstraint(kind: kind, a: $0) }
        case .parallel, .perpendicular, .equalLength, .angle:
            guard lns.count == 2 else { return nil }
            added = [SketchConstraint(kind: kind, a: lns[0], b: lns[1])]
        case .equalRadius:
            guard crs.count == 2 else { return nil }
            added = [SketchConstraint(kind: kind, a: crs[0], b: crs[1])]
        case .coincident:
            if crs.count == 2 {
                added = [SketchConstraint(kind: kind, a: sketch.circles[crs[0]].center, b: sketch.circles[crs[1]].center)]
            } else if pts.count == 2 {
                added = [SketchConstraint(kind: kind, a: pts[0], b: pts[1])]
            }
        case .pointOnLine, .midpoint:
            guard let p = pts.first, let l = lns.first else { return nil }
            added = [SketchConstraint(kind: kind, a: p, b: l)]
        case .pointOnCircle:
            guard let p = pts.first, let c = crs.first else { return nil }
            added = [SketchConstraint(kind: kind, a: p, b: c)]
        case .tangent:
            guard let l = lns.first, let c = crs.first else { return nil }
            added = [SketchConstraint(kind: kind, a: l, b: c)]
        case .fixed:
            added = pts.filter { $0 != 0 }.map {
                SketchConstraint(kind: .fixed, a: $0, value: sketch.points[$0].x, value2: sketch.points[$0].y)
            }
        case .length:
            guard let l = lns.first else { return nil }
            added = [SketchConstraint(kind: kind, a: l)]
        case .distance, .horizontalDistance, .verticalDistance:
            guard pts.count == 2 else { return nil }
            added = [SketchConstraint(kind: kind, a: pts[0], b: pts[1])]
        case .radius:
            guard let c = crs.first else { return nil }
            added = [SketchConstraint(kind: kind, a: c)]
        case .symmetric:
            guard pts.count == 2, let l = lns.first else { return nil }
            added = [SketchConstraint(kind: kind, a: pts[0], b: pts[1], c: l)]
        }
        added = added.filter { new in !sketch.constraints.contains { $0.kind == new.kind && $0.a == new.a && $0.b == new.b && $0.c == new.c } }
        guard !added.isEmpty else { return "Already constrained" }
        for i in added.indices where kind.isDimension {
            added[i].value = sketch.measure(kind, added[i].a, added[i].b)
        }

        var trial = sketch
        trial.constraints += added
        guard let status = trial.solve() else { return "\(kind.title) conflicts with the existing constraints" }
        // A constraint that removes no freedom is redundant; it would only
        // conflict later when something else changes.
        var current = sketch
        if let before = current.solve(), status.degreesOfFreedom >= before.degreesOfFreedom {
            return "Already constrained — \(kind.title.lowercased()) adds nothing"
        }
        sketch = trial
        self.status = status
        selection = kind.isDimension ? [.constraint(sketch.constraints.count - 1)] : []
        return nil
    }

    mutating func setDimension(_ index: Int, to value: Double) -> String? {
        guard sketch.constraints.indices.contains(index) else { return nil }
        let kind = sketch.constraints[index].kind
        if [.length, .distance, .radius].contains(kind), value <= 0 { return "\(kind.title) must be positive" }
        var trial = sketch
        trial.constraints[index].value = value
        guard let status = trial.solve() else { return "Can't make the \(kind.title.lowercased()) \(formatted(value))" }
        sketch = trial
        self.status = status
        return nil
    }

    /// Label shown for a dimension in the viewport.
    func label(for c: SketchConstraint) -> String {
        switch c.kind {
        case .radius: return "R\(formatted(c.value))"
        case .angle: return "\(formatted(c.value))°"
        default: return formatted(abs(c.value))
        }
    }

    /// Where a constraint's label sits, in plane coordinates, plus the
    /// geometry it measures (drawn as a thin leader).
    func anchor(for c: SketchConstraint, offset: Double) -> (position: SIMD2<Double>, leader: [SIMD2<Double>]) {
        let s = sketch
        func normal(_ line: Int) -> SIMD2<Double> {
            let d = s.direction(line)
            let len = simd_length(d)
            return len > 1e-9 ? SIMD2(-d.y, d.x) / len : [0, 1]
        }
        switch c.kind {
        case .length:
            let n = normal(c.a) * offset * 2
            let a = s.points[s.lines[c.a].a] + n, b = s.points[s.lines[c.a].b] + n
            return ((a + b) / 2, [a, b])
        case .distance:
            let a = s.points[c.a], b = s.points[c.b]
            return ((a + b) / 2, [a, b])
        case .horizontalDistance:
            let a = s.points[c.a], b = s.points[c.b]
            let y = max(a.y, b.y) + offset * 2
            return ([(a.x + b.x) / 2, y], [[a.x, y], [b.x, y]])
        case .verticalDistance:
            let a = s.points[c.a], b = s.points[c.b]
            let x = max(a.x, b.x) + offset * 2
            return ([x, (a.y + b.y) / 2], [[x, a.y], [x, b.y]])
        case .radius:
            let circle = s.circles[c.a]
            let center = s.points[circle.center]
            // On an arc, point the leader at the middle of the arc.
            let t = s.arcAngles(c.a).map { $0.start + $0.sweep / 2 } ?? Double.pi / 4
            let edge = center + SIMD2(cos(t), sin(t)) * circle.radius
            return (center + (edge - center) * 0.55, [center, edge])
        case .angle:
            return ((s.midpoint(c.a) + s.midpoint(c.b)) / 2, [])
        case .horizontal, .vertical, .equalLength, .parallel, .perpendicular, .tangent:
            return (s.midpoint(c.a) + normal(c.a) * offset, [])
        case .equalRadius:
            let circle = s.circles[c.a]
            return (s.points[circle.center] + [0, circle.radius + offset], [])
        case .pointOnLine, .pointOnCircle, .midpoint, .fixed, .coincident:
            return (s.points[c.a] + [offset, offset], [])
        case .symmetric:
            return ((s.points[c.a] + s.points[c.b]) / 2 + [offset, offset], [])
        }
    }
}
