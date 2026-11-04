import CoreGraphics
import CoreText
import Foundation
import simd

/// Sketch ▸ Scale / Pattern / Rotate / Mirror / Text.
enum SketchTransformKind: String, Identifiable {
    case scale, linearPattern, circularPattern, rotate
    var id: String { rawValue }

    var title: String {
        switch self {
        case .scale: return "Scale Sketch"
        case .linearPattern: return "Pattern - Linear"
        case .circularPattern: return "Pattern - Circular"
        case .rotate: return "Rotate Sketch Around Axis"
        }
    }
}

struct SketchTransformParams {
    var factor = 2.0
    var angle = 90.0
    var count = 3
    var spacing = 20.0
    var direction = 0.0     // degrees from the sketch's X axis
    var totalAngle = 360.0
    var keepOriginal = false
}

extension ModelStore {
    /// Selected sketch geometry (not constraints), and a selected point to pivot around.
    private func sketchTargets(_ s: SketchSession) -> (geometry: [SketchRef], pivot: SIMD2<Double>?) {
        let geometry = s.selection.filter {
            switch $0 {
            case .constraint, .point: return false
            default: return true
            }
        }
        let point = s.selection.compactMap { if case let .point(i) = $0 { return i } else { return nil } }.last
        // A lone point is geometry to move; with other geometry it's the pivot.
        if geometry.isEmpty { return (s.selection.filter { if case .point = $0 { return true } else { return false } }, nil) }
        return (geometry, point.map { s.sketch.points[$0] })
    }

    var canTransformSketch: Bool {
        guard let s = sketch else { return false }
        return !sketchTargets(s).geometry.isEmpty
    }

    func transformSketchSelection(_ kind: SketchTransformKind, _ p: SketchTransformParams) {
        guard var s = sketch else { return }
        let (geometry, pivot) = sketchTargets(s)
        guard !geometry.isEmpty else { return toast = "Select sketch geometry first" }
        var created: [SketchRef] = []
        switch kind {
        case .scale:
            guard p.factor > 1e-6 else { return toast = "The scale must be more than 0" }
            let t = SketchTransform(pivot: pivot ?? s.sketch.centroid(of: geometry), scale: p.factor)
            created = p.keepOriginal ? s.sketch.addCopy(of: geometry, t) : s.sketch.transform(geometry, t)
        case .rotate:
            let t = SketchTransform(pivot: pivot ?? .zero, angle: p.angle * .pi / 180)
            created = p.keepOriginal ? s.sketch.addCopy(of: geometry, t) : s.sketch.transform(geometry, t)
        case .linearPattern:
            guard p.count >= 2 else { return toast = "A pattern needs at least 2 instances" }
            let a = p.direction * .pi / 180, d = SIMD2(cos(a), sin(a))
            for k in 1..<p.count {
                created += s.sketch.addCopy(of: geometry, SketchTransform(shift: d * p.spacing * Double(k)))
            }
        case .circularPattern:
            guard p.count >= 2 else { return toast = "A pattern needs at least 2 instances" }
            // A full turn spreads the copies evenly without repeating the original.
            let full = abs(abs(p.totalAngle) - 360) < 1e-6
            let step = p.totalAngle / Double(full ? p.count : p.count - 1) * .pi / 180
            for k in 1..<p.count {
                created += s.sketch.addCopy(of: geometry, SketchTransform(pivot: pivot ?? .zero, angle: step * Double(k)))
            }
        }
        s.selection = created
        s.resolve()
        sketch = s
    }

    /// Mirrors the selection across the last selected line, or the sketch's
    /// vertical axis when no line is picked as the mirror.
    func mirrorSketchSelection() {
        guard var s = sketch else { return }
        var geometry = sketchTargets(s).geometry
        var pivot = SIMD2<Double>(0, 0), axis = SIMD2<Double>(0, 1)
        if geometry.count >= 2, case let .line(l)? = s.selection.last(where: { if case .line = $0 { return true } else { return false } }) {
            geometry.removeAll { $0 == .line(l) }
            let line = s.sketch.lines[l]
            pivot = s.sketch.points[line.a]
            axis = s.sketch.points[line.b] - pivot
        }
        guard !geometry.isEmpty, simd_length(axis) > 1e-9 else { return toast = "Select geometry, then a line to mirror across" }
        let created = s.sketch.addCopy(of: geometry, SketchTransform(pivot: pivot, mirrorAxis: axis))
        s.selection = created
        s.resolve()
        sketch = s
    }

    // MARK: Text

    /// Adds `text` as closed outlines (lines and splines) with its baseline at
    /// the sketch origin, ready to extrude.
    func addSketchText(_ text: String, fontName: String, size: Double) {
        guard var s = sketch, !text.isEmpty else { return }
        let font = CTFontCreateWithName(fontName as CFString, CGFloat(size), nil)
        let attributed = NSAttributedString(string: text, attributes: [NSAttributedString.Key(kCTFontAttributeName as String): font])
        let line = CTLineCreateWithAttributedString(attributed)
        let runs = CTLineGetGlyphRuns(line) as! [CTRun]
        var added = 0
        // Centred on the sketch origin: half the advance left, half the cap height down.
        let width = CGFloat(CTLineGetTypographicBounds(line, nil, nil, nil))
        let shift = CGPoint(x: -width / 2, y: -CTFontGetCapHeight(font) / 2)
        let before = (lines: s.sketch.lines.count, splines: s.sketch.splines.count)
        for run in runs {
            let count = CTRunGetGlyphCount(run)
            var glyphs = [CGGlyph](repeating: 0, count: count), positions = [CGPoint](repeating: .zero, count: count)
            CTRunGetGlyphs(run, CFRange(location: 0, length: count), &glyphs)
            CTRunGetPositions(run, CFRange(location: 0, length: count), &positions)
            let attrs = CTRunGetAttributes(run) as NSDictionary
            let runFont = (attrs[kCTFontAttributeName as String] as! CTFont?) ?? font
            for (g, pos) in zip(glyphs, positions) {
                var transform = CGAffineTransform(translationX: pos.x + shift.x, y: pos.y + shift.y)
                guard let path = CTFontCreatePathForGlyph(runFont, g, &transform) else { continue }
                added += addOutline(path, to: &s.sketch)
            }
        }
        guard added > 0 else { return toast = "That text has no outlines" }
        // Select the new outlines so they can be scaled or moved right away.
        s.selection = (before.lines..<s.sketch.lines.count).map { .line($0) } + (before.splines..<s.sketch.splines.count).map { .spline($0) }
        s.resolve()
        sketch = s
    }

    /// Converts a glyph path into sketch geometry. Curves become splines through
    /// samples of the curve; their ends are shared with the neighbouring segments.
    private func addOutline(_ path: CGPath, to sketch: inout Sketch) -> Int {
        var start: Int?, current: Int?, currentPoint = CGPoint.zero, added = 0
        func point(_ p: CGPoint) -> SIMD2<Double> { [Double(p.x), Double(p.y)] }
        func close() {
            if let c = current, let s = start, c != s {
                if simd_distance(sketch.points[c], sketch.points[s]) > 1e-6 {
                    _ = sketch.addLine(c, s)
                    added += 1
                } else {
                    sketch.constraints.append(SketchConstraint(kind: .coincident, a: c, b: s))
                }
            }
            start = nil
            current = nil
        }
        func curve(_ samples: [CGPoint]) {
            guard let c = current else { return }
            let end = point(samples.last!)
            let last: Int
            if let st = start, st != c, simd_distance(end, sketch.points[st]) < 1e-6 { last = st } else { last = sketch.addPoint(end) }
            let ids = [c] + samples.dropLast().map { sketch.addPoint(point($0)) } + [last]
            sketch.splines.append(SketchSpline(points: ids))
            current = ids.last
            added += 1
        }
        path.applyWithBlock { element in
            let e = element.pointee
            switch e.type {
            case .moveToPoint:
                if current != nil { close() }
                currentPoint = e.points[0]
                start = sketch.addPoint(point(currentPoint))
                current = start
            case .addLineToPoint:
                guard let c = current else { return }
                let p = e.points[0]
                if hypot(p.x - currentPoint.x, p.y - currentPoint.y) < 1e-6 { return }
                // Lines ending on the contour's start close onto it.
                let target: Int
                if let st = start, simd_distance(point(p), sketch.points[st]) < 1e-6 { target = st } else { target = sketch.addPoint(point(p)) }
                _ = sketch.addLine(c, target)
                current = target
                currentPoint = p
                added += 1
            case .addQuadCurveToPoint:
                let p1 = e.points[1]
                curve(quadSamples(currentPoint, e.points[0], p1))
                currentPoint = p1
            case .addCurveToPoint:
                let p1 = e.points[2]
                curve(cubicSamples(currentPoint, e.points[0], e.points[1], p1))
                currentPoint = p1
            case .closeSubpath:
                close()
            @unknown default:
                break
            }
        }
        if current != nil { close() }
        return added
    }
}

private func quadSamples(_ p0: CGPoint, _ c: CGPoint, _ p1: CGPoint) -> [CGPoint] {
    (1...4).map { k -> CGPoint in
        let t = CGFloat(k) / 4, u = 1 - t
        let a = u * u, b = 2 * u * t, d = t * t
        return CGPoint(x: a * p0.x + b * c.x + d * p1.x, y: a * p0.y + b * c.y + d * p1.y)
    }
}

private func cubicSamples(_ p0: CGPoint, _ c1: CGPoint, _ c2: CGPoint, _ p1: CGPoint) -> [CGPoint] {
    (1...5).map { k -> CGPoint in
        let t = CGFloat(k) / 5, u = 1 - t
        let a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t
        let x = a * p0.x + b * c1.x + c * c2.x + d * p1.x
        let y = a * p0.y + b * c1.y + c * c2.y + d * p1.y
        return CGPoint(x: x, y: y)
    }
}
