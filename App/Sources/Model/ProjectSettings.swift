import Foundation
import simd

enum LengthUnit: String, Codable, CaseIterable, Identifiable {
    case mm, cm, m, inch, ft

    var id: String { rawValue }
    var symbol: String { self == .inch ? "in" : rawValue }
    var title: String {
        switch self {
        case .mm: return "Millimeters"
        case .cm: return "Centimeters"
        case .m: return "Meters"
        case .inch: return "Inches"
        case .ft: return "Feet"
        }
    }
    var imperial: Bool { self == .inch || self == .ft }

    /// Millimetres per unit.
    var scale: Double {
        switch self {
        case .mm: return 1
        case .cm: return 10
        case .m: return 1000
        case .inch: return 25.4
        case .ft: return 304.8
        }
    }
}

/// How a body looks: base colour plus physically based material values.
/// Material presets (see Visualization.swift) fill these in.
struct Appearance: Codable, Equatable {
    var color: SIMD3<Float> = [0.72, 0.75, 0.8]
    var metallic: Float = 0
    var roughness: Float = 0.45
    var opacity: Float = 1
    /// The library preset this came from, for the material list.
    var preset: String?

    init(color: SIMD3<Float> = [0.72, 0.75, 0.8], metallic: Float = 0, roughness: Float = 0.45, opacity: Float = 1,
         preset: String? = nil) {
        self.color = color
        self.metallic = metallic
        self.roughness = roughness
        self.opacity = opacity
        self.preset = preset
    }

    private enum CodingKeys: String, CodingKey { case color, metallic, roughness, opacity, preset, material }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        color = try c.decodeIfPresent(SIMD3<Float>.self, forKey: .color) ?? [0.72, 0.75, 0.8]
        metallic = try c.decodeIfPresent(Float.self, forKey: .metallic) ?? 0
        roughness = try c.decodeIfPresent(Float.self, forKey: .roughness) ?? 0.45
        opacity = try c.decodeIfPresent(Float.self, forKey: .opacity) ?? 1
        preset = try c.decodeIfPresent(String.self, forKey: .preset)
        // Documents from before material values stored a material name.
        switch try c.decodeIfPresent(String.self, forKey: .material) {
        case "matte": roughness = 0.9
        case "metal": metallic = 1; roughness = 0.25
        case "glass": opacity = 0.35; roughness = 0.05
        default: break
        }
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(color, forKey: .color)
        try c.encode(metallic, forKey: .metallic)
        try c.encode(roughness, forKey: .roughness)
        try c.encode(opacity, forKey: .opacity)
        try c.encodeIfPresent(preset, forKey: .preset)
    }

    static let palette: [SIMD3<Float>] = [
        [0.72, 0.75, 0.8], [0.85, 0.3, 0.28], [0.95, 0.6, 0.2], [0.95, 0.82, 0.3],
        [0.4, 0.72, 0.4], [0.3, 0.6, 0.85], [0.5, 0.42, 0.8], [0.25, 0.26, 0.3], [0.95, 0.95, 0.96],
    ]
}

/// Per-document settings and per-body presentation. Bodies are keyed by the
/// id the kernel gives them on replay, which is stable for a given history.
struct ProjectSettings: Codable, Equatable {
    var units: LengthUnit = .mm
    var snapping = true
    var bodyNames: [Int: String] = [:]
    var appearance: [Int: Appearance] = [:]
    var hiddenBodies: Set<Int> = []
    var hiddenItems: Set<UUID> = []
    var environment = EnvironmentSettings()
    var camera = CameraSettings()
    var snap = SnapSettings()
    /// Named values usable in any parameter field, e.g. `width * 2`.
    var variables: [Variable] = []

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        units = try c.decodeIfPresent(LengthUnit.self, forKey: .units) ?? .mm
        snapping = try c.decodeIfPresent(Bool.self, forKey: .snapping) ?? true
        bodyNames = try c.decodeIfPresent([Int: String].self, forKey: .bodyNames) ?? [:]
        appearance = try c.decodeIfPresent([Int: Appearance].self, forKey: .appearance) ?? [:]
        hiddenBodies = try c.decodeIfPresent(Set<Int>.self, forKey: .hiddenBodies) ?? []
        hiddenItems = try c.decodeIfPresent(Set<UUID>.self, forKey: .hiddenItems) ?? []
        environment = try c.decodeIfPresent(EnvironmentSettings.self, forKey: .environment) ?? EnvironmentSettings()
        camera = try c.decodeIfPresent(CameraSettings.self, forKey: .camera) ?? CameraSettings()
        snap = try c.decodeIfPresent(SnapSettings.self, forKey: .snap) ?? SnapSettings()
        variables = try c.decodeIfPresent([Variable].self, forKey: .variables) ?? []
    }

    func name(of body: Int) -> String { bodyNames[body] ?? "Body \(body)" }
}

struct Variable: Codable, Equatable, Identifiable {
    var id = UUID()
    var name: String
    var expression: String
}

/// What the pointer snaps to while sketching, and which hints show.
struct SnapSettings: Codable, Equatable {
    var grid = true
    var guideLines = true    // horizontal / vertical alignment with existing points
    var guidePoints = true   // midpoints, centres and quadrants
    var points3D = true      // body corners projected onto the sketch plane
    var distantEdges = true  // body edges as snap targets even when far away
    var showGuidePoints = true
    var showHints = true
}

/// A sketch, construction plane, axis or reference image from the history,
/// shown in the viewport.
struct ReferenceItem: Identifiable {
    enum Content {
        case sketch(Sketch)
        case plane(SketchPlane)
        case axis(origin: SIMD3<Double>, direction: SIMD3<Double>)
        case image(ReferenceImage)
    }

    let id: UUID
    let name: String
    let content: Content

    var plane: SketchPlane {
        switch content {
        case let .sketch(s): return s.plane
        case let .plane(p): return p
        case let .image(i): return i.plane
        case let .axis(origin, direction):
            // A plane the axis is normal to, for sketching around it.
            let d = simd_normalize(direction)
            let x = simd_normalize(simd_cross(abs(d.z) < 0.9 ? [0, 0, 1] : [1, 0, 0], d))
            return SketchPlane(origin: origin, normal: d, xDir: x)
        }
    }

    var isPlane: Bool { if case .plane = content { return true } else { return false } }
    var isSketch: Bool { if case .sketch = content { return true } else { return false } }
    var isAxis: Bool { if case .axis = content { return true } else { return false } }
}
