import Foundation
import simd

/// A material from the Visualize library.
struct MaterialPreset: Identifiable, Equatable {
    let id: String
    let family: String     // Plastic, Metal, Glass, Rubber, Wood, Ceramic
    let finish: String     // Glossy, Matte, Polished, Brushed, …
    let name: String       // ABS, Iron, Aluminium, …
    let appearance: Appearance

    var title: String { "\(finish) \(name)" }
}

/// Scene background and lighting for Visualize mode and screenshots.
struct EnvironmentSettings: Codable, Equatable {
    var background: SIMD3<Float> = [0.90, 0.91, 0.93]
    /// Blend the background towards a lighter top for a studio look.
    var gradient = true
    var showGrid = true
    var lightIntensity: Float = 1
    /// Key light direction around the vertical axis, in degrees.
    var lightAzimuth: Float = 35
    var groundShadow = true
    /// Key light height above the ground, in degrees.
    var lightElevation: Float = 50
    /// Bodies cast real shadows on each other and on the floor (Visualized).
    var castShadows = true
    /// 0 crisp to 1 very soft shadow edges.
    var shadowSoftness: Float = 0.45
    /// Darkens creases, contacts and crevices (Visualized).
    var ambientOcclusion = true
    var occlusionStrength: Float = 1
    /// Blends normals across faces that meet at shallow angles, so revolved
    /// and lofted shapes read as smooth surfaces (Visualized).
    var smoothShading = true
}

extension EnvironmentSettings {
    /// Older documents lack the newer keys; missing ones keep their defaults.
    init(from decoder: Decoder) throws {
        self.init()
        let c = try decoder.container(keyedBy: CodingKeys.self)
        func get<T: Decodable>(_ key: CodingKeys, _ value: inout T) throws {
            if let v = try c.decodeIfPresent(T.self, forKey: key) { value = v }
        }
        try get(.background, &background); try get(.gradient, &gradient); try get(.showGrid, &showGrid)
        try get(.lightIntensity, &lightIntensity); try get(.lightAzimuth, &lightAzimuth); try get(.groundShadow, &groundShadow)
        try get(.lightElevation, &lightElevation); try get(.castShadows, &castShadows); try get(.shadowSoftness, &shadowSoftness)
        try get(.ambientOcclusion, &ambientOcclusion); try get(.occlusionStrength, &occlusionStrength)
        try get(.smoothShading, &smoothShading)
    }
}

struct CameraSettings: Codable, Equatable {
    var orthographic = false
    /// Vertical field of view in degrees (perspective only).
    var fieldOfView: Float = 40
}

/// A reference image placed on a plane in the scene (Insert ▸ Image).
struct ReferenceImage: Codable, Equatable {
    var name: String
    var png: Data
    var plane: SketchPlane
    var width: Double
    var height: Double
    var opacity: Double = 0.8
    /// A decal: an image stuck on a body face (File ▸ Import Image as Decal),
    /// drawn opaque and shown or hidden with View ▸ Show Decals.
    var decal = false

    init(name: String, png: Data, plane: SketchPlane, width: Double, height: Double, opacity: Double = 0.8, decal: Bool = false) {
        self.name = name
        self.png = png
        self.plane = plane
        self.width = width
        self.height = height
        self.opacity = opacity
        self.decal = decal
    }

    private enum CodingKeys: String, CodingKey { case name, png, plane, width, height, opacity, decal }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        name = try c.decode(String.self, forKey: .name)
        png = try c.decode(Data.self, forKey: .png)
        plane = try c.decode(SketchPlane.self, forKey: .plane)
        width = try c.decode(Double.self, forKey: .width)
        height = try c.decode(Double.self, forKey: .height)
        opacity = try c.decodeIfPresent(Double.self, forKey: .opacity) ?? 0.8
        decal = try c.decodeIfPresent(Bool.self, forKey: .decal) ?? false
    }
}

// MARK: - Material library

extension MaterialPreset {
    /// Families in display order.
    static let families = ["Plastic", "Metal", "Glass", "Rubber", "Ceramic", "Wood"]

    private static func make(_ id: String, _ family: String, _ finish: String, _ name: String,
                             _ color: SIMD3<Float>, metallic: Float = 0, roughness: Float, opacity: Float = 1) -> MaterialPreset {
        MaterialPreset(id: id, family: family, finish: finish, name: name,
                       appearance: Appearance(color: color, metallic: metallic, roughness: roughness, opacity: opacity, preset: id))
    }

    /// The Visualize material library. Ids are stable: documents store them
    /// in `Appearance.preset`.
    static let library: [MaterialPreset] = {
        var list: [MaterialPreset] = []
        // Plastics: glossy and matte ABS in a range of colours.
        let plastics: [(String, String, SIMD3<Float>)] = [
            ("white", "White", [0.92, 0.92, 0.90]),
            ("grey", "Grey", [0.55, 0.57, 0.60]),
            ("black", "Black", [0.06, 0.06, 0.07]),
            ("red", "Red", [0.78, 0.10, 0.09]),
            ("orange", "Orange", [0.95, 0.42, 0.08]),
            ("yellow", "Yellow", [0.96, 0.78, 0.12]),
            ("green", "Green", [0.18, 0.58, 0.26]),
            ("blue", "Blue", [0.10, 0.32, 0.78]),
        ]
        for (key, name, color) in plastics {
            list.append(make("plastic.glossy.abs.\(key)", "Plastic", "Glossy", "\(name) ABS", color, roughness: 0.18))
        }
        for (key, name, color) in plastics where ["white", "grey", "black", "red", "blue"].contains(key) {
            list.append(make("plastic.matte.abs.\(key)", "Plastic", "Matte", "\(name) ABS", color, roughness: 0.75))
        }

        // Metals: base colour is the reflectance (F0) of the metal.
        list += [
            make("metal.polished.iron", "Metal", "Polished", "Iron", [0.56, 0.57, 0.58], metallic: 1, roughness: 0.15),
            make("metal.brushed.iron", "Metal", "Brushed", "Iron", [0.53, 0.54, 0.55], metallic: 1, roughness: 0.45),
            make("metal.polished.steel", "Metal", "Polished", "Steel", [0.66, 0.67, 0.69], metallic: 1, roughness: 0.12),
            make("metal.brushed.steel", "Metal", "Brushed", "Steel", [0.62, 0.63, 0.65], metallic: 1, roughness: 0.4),
            make("metal.polished.aluminium", "Metal", "Polished", "Aluminium", [0.91, 0.92, 0.92], metallic: 1, roughness: 0.14),
            make("metal.brushed.aluminium", "Metal", "Brushed", "Aluminium", [0.86, 0.87, 0.88], metallic: 1, roughness: 0.42),
            make("metal.anodized.aluminium.black", "Metal", "Anodized", "Black Aluminium", [0.10, 0.10, 0.11], metallic: 0.85, roughness: 0.38),
            make("metal.polished.gold", "Metal", "Polished", "Gold", [1.0, 0.77, 0.34], metallic: 1, roughness: 0.12),
            make("metal.polished.copper", "Metal", "Polished", "Copper", [0.95, 0.64, 0.54], metallic: 1, roughness: 0.15),
            make("metal.brushed.copper", "Metal", "Brushed", "Copper", [0.90, 0.60, 0.50], metallic: 1, roughness: 0.42),
            make("metal.polished.brass", "Metal", "Polished", "Brass", [0.91, 0.78, 0.42], metallic: 1, roughness: 0.16),
            make("metal.polished.chrome", "Metal", "Polished", "Chrome", [0.95, 0.95, 0.96], metallic: 1, roughness: 0.04),
        ]

        list += [
            make("glass.clear", "Glass", "Clear", "Glass", [0.92, 0.95, 0.96], roughness: 0.04, opacity: 0.18),
            make("glass.frosted", "Glass", "Frosted", "Glass", [0.90, 0.93, 0.95], roughness: 0.5, opacity: 0.55),
            make("glass.tinted.blue", "Glass", "Tinted", "Blue Glass", [0.35, 0.55, 0.80], roughness: 0.05, opacity: 0.35),
            make("glass.tinted.smoke", "Glass", "Tinted", "Smoke Glass", [0.22, 0.23, 0.25], roughness: 0.05, opacity: 0.45),
            make("glass.tinted.green", "Glass", "Tinted", "Green Glass", [0.40, 0.68, 0.48], roughness: 0.05, opacity: 0.35),

            make("rubber.black", "Rubber", "Matte", "Black Rubber", [0.05, 0.05, 0.055], roughness: 0.9),
            make("rubber.grey", "Rubber", "Matte", "Grey Rubber", [0.32, 0.33, 0.35], roughness: 0.88),
            make("rubber.red", "Rubber", "Matte", "Red Rubber", [0.60, 0.10, 0.08], roughness: 0.85),

            make("ceramic.glossy.white", "Ceramic", "Glossy", "White Ceramic", [0.94, 0.93, 0.90], roughness: 0.08),
            make("ceramic.matte.white", "Ceramic", "Matte", "White Ceramic", [0.90, 0.89, 0.86], roughness: 0.6),
            make("ceramic.glossy.terracotta", "Ceramic", "Glossy", "Terracotta", [0.72, 0.36, 0.22], roughness: 0.25),

            make("wood.oak", "Wood", "Satin", "Oak", [0.70, 0.52, 0.32], roughness: 0.55),
            make("wood.walnut", "Wood", "Satin", "Walnut", [0.36, 0.23, 0.14], roughness: 0.5),
            make("wood.maple", "Wood", "Satin", "Maple", [0.86, 0.72, 0.52], roughness: 0.55),
            make("wood.cherry", "Wood", "Varnished", "Cherry", [0.58, 0.28, 0.16], roughness: 0.3),
        ]
        return list
    }()

    private static let byId: [String: MaterialPreset] = Dictionary(uniqueKeysWithValues: library.map { ($0.id, $0) })

    static func preset(id: String) -> MaterialPreset? { byId[id] }

    /// Library presets of one family, in library order.
    static func presets(in family: String) -> [MaterialPreset] { library.filter { $0.family == family } }
}
