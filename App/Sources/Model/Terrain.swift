import CoreGraphics
import ImageIO
import Foundation
import simd

/// Procedural rolling hills: fractal value noise.
struct TerrainNoise: Codable, Equatable {
    var seed = 1
    /// Size of the largest hills, as a fraction of the terrain's longer side.
    var scale = 0.45
    /// Layers of finer detail on top of the big shapes.
    var octaves = 4
    /// How much each finer layer adds (0.3 smooth – 0.7 rugged).
    var roughness = 0.5
}

/// Insert ▸ Terrain: a smooth landscape solid built from a grid of heights.
struct TerrainSettings: Codable, Equatable {
    var name = "Terrain"
    /// Centre of the bottom face.
    var center: SIMD3<Double> = .zero
    var width = 200.0
    var depth = 200.0
    var nx = 48
    var ny = 48
    /// Top surface height above the bottom face, mm, row-major with rows along +Y.
    var heights: [Double] = []
    /// Multiplies the relief above `floor` (the inspector's Height %).
    var heightScale = 1.0
    /// Solid thickness under the lowest point of the relief, mm.
    var floor = 10.0
    /// When set, the heights are this noise and Seed/Roughness can be edited.
    var noise: TerrainNoise?
    /// Relief height of generated noise before heightScale, mm.
    var noiseHeight = 40.0

    /// Absolute top heights handed to the kernel.
    var topHeights: [Double] {
        heights.map { center.z + floor + max($0, 0) * heightScale }
    }

    /// Regenerates `heights` from `noise`.
    mutating func regenerate() {
        guard let noise else { return }
        heights = Terrain.noiseField(nx: nx, ny: ny, aspect: depth / max(width, 1e-6), noise).map { $0 * noiseHeight }
    }
}

enum Terrain {
    // MARK: Noise

    private static func hash(_ x: Int, _ y: Int, _ seed: Int) -> Double {
        var h = UInt64(bitPattern: Int64(x &* 374_761_393 &+ y &* 668_265_263 &+ seed &* 2_147_483_647))
        h = (h ^ (h >> 13)) &* 1_274_126_177
        h ^= h >> 16
        return Double(h & 0xFFFFFF) / Double(0xFFFFFF)
    }

    private static func valueNoise(_ x: Double, _ y: Double, _ seed: Int) -> Double {
        let xi = Int(floor(x)), yi = Int(floor(y))
        let fx = x - floor(x), fy = y - floor(y)
        let sx = fx * fx * fx * (fx * (fx * 6 - 15) + 10), sy = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
        let a = hash(xi, yi, seed), b = hash(xi + 1, yi, seed), c = hash(xi, yi + 1, seed), d = hash(xi + 1, yi + 1, seed)
        return (a + (b - a) * sx) * (1 - sy) + (c + (d - c) * sx) * sy
    }

    /// Fractal noise over the grid, normalised to 0…1.
    static func noiseField(nx: Int, ny: Int, aspect: Double, _ n: TerrainNoise) -> [Double] {
        let freq = 1 / max(n.scale, 0.02)
        var out = [Double](repeating: 0, count: nx * ny)
        for j in 0..<ny {
            for i in 0..<nx {
                let u = Double(i) / Double(max(nx - 1, 1)), v = Double(j) / Double(max(ny - 1, 1)) * aspect
                var amp = 1.0, f = freq, sum = 0.0, norm = 0.0
                for o in 0..<max(n.octaves, 1) {
                    sum += valueNoise(u * f + 17.3 * Double(o), v * f - 9.1 * Double(o), n.seed &+ o * 101) * amp
                    norm += amp
                    amp *= n.roughness
                    f *= 2.03
                }
                out[j * nx + i] = sum / norm
            }
        }
        let lo = out.min() ?? 0, hi = out.max() ?? 1
        return out.map { ($0 - lo) / max(hi - lo, 1e-9) }
    }

    // MARK: Images

    /// Brightness of `image` resampled to nx × ny, 0 (black) … 1 (white); the
    /// image's top row is the terrain's far (+Y) edge.
    static func field(from image: CGImage, nx: Int, ny: Int) -> [Double]? {
        var pixels = [UInt8](repeating: 0, count: nx * ny)
        guard let ctx = CGContext(data: &pixels, width: nx, height: ny, bitsPerComponent: 8, bytesPerRow: nx,
                                  space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.none.rawValue) else { return nil }
        ctx.interpolationQuality = .high
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: nx, height: ny))
        // Bitmap memory starts at the top row; terrain rows start at -Y.
        var out = [Double](repeating: 0, count: nx * ny)
        for j in 0..<ny {
            for i in 0..<nx { out[j * nx + i] = Double(pixels[(ny - 1 - j) * nx + i]) / 255 }
        }
        return out
    }

    // MARK: Shaping (MCP)

    struct Hill { var x, y, radius, height: Double }
    /// A river or path bed: the surface within `width` of the polyline is lowered
    /// to `bed` (height above the floor), with banks of `bank` mm easing back up.
    struct Channel { var points: [SIMD2<Double>]; var width, bed, bank: Double }

    /// Distance from p to a polyline.
    static func distance(_ p: SIMD2<Double>, _ line: [SIMD2<Double>]) -> Double {
        guard line.count > 1 else { return line.first.map { simd_distance($0, p) } ?? .infinity }
        var best = Double.infinity
        for (a, b) in zip(line, line.dropFirst()) {
            let ab = b - a, t = min(max(simd_dot(p - a, ab) / max(simd_length_squared(ab), 1e-12), 0), 1)
            best = min(best, simd_distance(p, a + ab * t))
        }
        return best
    }

    /// World XY of grid sample (i, j).
    static func point(_ i: Int, _ j: Int, _ t: TerrainSettings) -> SIMD2<Double> {
        SIMD2(t.center.x - t.width / 2 + t.width * Double(i) / Double(t.nx - 1),
              t.center.y - t.depth / 2 + t.depth * Double(j) / Double(t.ny - 1))
    }

    static func smoothstep(_ e0: Double, _ e1: Double, _ x: Double) -> Double {
        let t = min(max((x - e0) / (e1 - e0), 0), 1)
        return t * t * (3 - 2 * t)
    }

    /// Adds smooth dome-shaped hills (cosine falloff) to the heights.
    static func add(_ hills: [Hill], to t: inout TerrainSettings) {
        for j in 0..<t.ny {
            for i in 0..<t.nx {
                let p = point(i, j, t)
                for h in hills where h.radius > 0 {
                    let r = simd_distance(p, SIMD2(h.x, h.y)) / h.radius
                    if r < 1 { t.heights[j * t.nx + i] += h.height * 0.5 * (1 + cos(.pi * r)) }
                }
            }
        }
    }

    /// Rounds the relief into `count` flat terraces joined by soft steps.
    static func terrace(_ t: inout TerrainSettings, count: Int, softness: Double) {
        guard count > 1, let hi = t.heights.max(), hi > 0 else { return }
        let step = hi / Double(count)
        let s = min(max(softness, 0.02), 1) * 0.5
        t.heights = t.heights.map { h in
            let k = (h / step).rounded(.down), f = h / step - k
            return (k + smoothstep(0.5 - s, 0.5 + s, f)) * step
        }
    }

    static func carve(_ channels: [Channel], into t: inout TerrainSettings) {
        for j in 0..<t.ny {
            for i in 0..<t.nx {
                let p = point(i, j, t)
                var h = t.heights[j * t.nx + i]
                for c in channels {
                    let d = distance(p, c.points)
                    let k = 1 - smoothstep(c.width / 2, c.width / 2 + max(c.bank, 1e-3), d)
                    if k > 0 { h = h + (min(h, c.bed) - h) * k }
                }
                t.heights[j * t.nx + i] = h
            }
        }
    }
}

extension ModelStore {
    /// Insert ▸ Terrain ▸ Rolling Hills.
    func insertRollingHills() {
        var t = TerrainSettings(name: "Rolling Hills")
        t.noise = TerrainNoise(seed: Int.random(in: 1...9999))
        t.regenerate()
        selection = []
        begin(.terrain(settings: t))
    }

    /// Insert ▸ Terrain ▸ From Height Map: white is high, black is low.
    func insertTerrain(heightMap url: URL) {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
            return toast = "Couldn't read that image"
        }
        var t = TerrainSettings(name: url.deletingPathExtension().lastPathComponent)
        let aspect = Double(image.height) / Double(max(image.width, 1))
        t.depth = (t.width * aspect).rounded()
        t.nx = 64
        t.ny = max(8, min(128, Int((64 * aspect).rounded())))
        guard let field = Terrain.field(from: image, nx: t.nx, ny: t.ny) else { return toast = "Couldn't read that image" }
        t.heights = field.map { $0 * 40 }
        selection = []
        begin(.terrain(settings: t))
    }
}

