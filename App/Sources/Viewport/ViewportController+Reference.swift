import simd

/// Sketch and plane items from the history, and the measurement line.
extension ViewportController {
    func referenceOverlay(_ batch: inout LineBatch) {
        let selected = SIMD4<Float>(1.0, 0.5, 0.1, 1)
        for item in store.referenceItems where !store.settings.hiddenItems.contains(item.id) {
            if item.isSketch && !showSketches && !store.itemSelection.contains(item.id) { continue }
            let isSelected = store.itemSelection.contains(item.id)
            let plane = item.plane
            func w(_ p: SIMD2<Double>) -> SIMD3<Float> { SIMD3<Float>(plane.world(p)) }
            switch item.content {
            case let .sketch(sketch):
                let ink: SIMD4<Float> = isSelected ? selected : [0.2, 0.3, 0.55, 0.9]
                for l in sketch.lines where !l.construction {
                    batch.line(w(sketch.points[l.a]), w(sketch.points[l.b]), ink)
                }
                for (i, c) in sketch.circles.enumerated() where !c.construction {
                    let (start, sweep) = sketch.arcAngles(i) ?? (0, 2 * .pi)
                    let center = sketch.points[c.center]
                    let n = max(8, Int(64 * sweep / (2 * .pi)))
                    batch.polyline((0...n).map { k in
                        let t = start + sweep * Double(k) / Double(n)
                        return w(center + SIMD2(cos(t), sin(t)) * c.radius)
                    }, ink)
                }
                for sp in sketch.splines where !sp.construction && sp.points.count >= 2 {
                    batch.polyline(sp.samples(sketch.points).map(w), ink)
                }
                for e in sketch.ellipses where !e.construction {
                    batch.polyline(ellipseSamples(e, center: sketch.points[e.center]).map(w), ink)
                }
            case .plane:
                let ink: SIMD4<Float> = isSelected ? selected : [0.45, 0.35, 0.8, 0.8]
                let r = 40.0
                let corners: [SIMD2<Double>] = [[-r, -r], [r, -r], [r, r], [-r, r], [-r, -r]]
                batch.polyline(corners.map(w), ink)
                // A light hatch makes the plane readable from any side.
                for k in stride(from: -r * 0.75, through: r * 0.75, by: r / 4) {
                    batch.line(w([k, -r]), w([k, r]), ink * SIMD4(1, 1, 1, 0.25))
                }
            case let .axis(origin, direction):
                let ink: SIMD4<Float> = isSelected ? selected : [0.85, 0.45, 0.1, 0.9]
                let d = simd_normalize(direction) * 200, o = origin
                // Dash-dot centre line.
                for k in stride(from: -1.0, to: 1.0, by: 0.05) where Int((k + 1) * 20) % 3 != 2 {
                    batch.line(SIMD3<Float>(o + d * k), SIMD3<Float>(o + d * (k + 0.04)), ink)
                }
            case let .image(image):
                // The picture itself is drawn by the renderer; outline it here.
                let hw = image.width / 2, hh = image.height / 2
                let ink: SIMD4<Float> = isSelected ? selected : [0.4, 0.42, 0.5, 0.6]
                batch.polyline([[-hw, -hh], [hw, -hh], [hw, hh], [-hw, hh], [-hw, -hh]].map(w), ink)
            }
        }
    }

    func measureOverlay(_ batch: inout LineBatch) {
        guard measuring, store.selection.count == 2, let m = measurement, m.hasDistance, m.distance > 1e-9 else { return }
        let a = SIMD3<Float>(m.p1), b = SIMD3<Float>(m.p2)
        let ink = SIMD4<Float>(0.1, 0.6, 0.35, 1)
        batch.line(a, b, ink)
        let s = camera.worldPerPoint(size: size) * 4
        for p in [a, b] {
            batch.line(p - camera.right * s, p + camera.right * s, ink)
            batch.line(p - camera.up * s, p + camera.up * s, ink)
        }
    }
}
