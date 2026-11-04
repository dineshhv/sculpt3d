import Foundation
import ImageIO
import simd

/// File ▸ Import Material and Import Image as Decal.
extension ModelStore {
    /// Reads a material file and applies it to the selected bodies (or all of them).
    /// JSON: {"name": "Brushed Steel", "color": "#c8ccd2" or [r, g, b], "metallic": 1, "roughness": 0.3, "opacity": 1}
    func importMaterial(_ url: URL) {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        guard let data = try? Data(contentsOf: url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return toast = "That isn't a Sculpt3D material file (JSON)"
        }
        var look = Appearance()
        if let hex = json["color"] as? String, let c = Self.color(hex: hex) {
            look.color = c
        } else if let rgb = json["color"] as? [Double], rgb.count >= 3 {
            let scale = rgb.prefix(3).contains { $0 > 1 } ? 255.0 : 1
            look.color = SIMD3(Float(rgb[0] / scale), Float(rgb[1] / scale), Float(rgb[2] / scale))
        }
        func number(_ keys: String...) -> Float? {
            for k in keys { if let v = json[k] as? Double { return Float(v) } }
            return nil
        }
        look.metallic = number("metallic", "metalness") ?? look.metallic
        look.roughness = number("roughness") ?? look.roughness
        look.opacity = number("opacity") ?? look.opacity
        let bodies = selection.isEmpty ? bodyIds : touchedBodies
        guard !bodies.isEmpty else { return toast = "Add a body to apply the material to" }
        setAppearance(look, for: bodies)
        let name = (json["name"] as? String) ?? url.deletingPathExtension().lastPathComponent
        toast = "Applied \(name) to \(bodies.count == 1 ? "the body" : "\(bodies.count) bodies")"
    }

    static func color(hex: String) -> SIMD3<Float>? {
        var s = hex.trimmingCharacters(in: .whitespaces)
        if s.hasPrefix("#") { s.removeFirst() }
        guard s.count == 6, let v = Int(s, radix: 16) else { return nil }
        return SIMD3(Float((v >> 16) & 255) / 255, Float((v >> 8) & 255) / 255, Float(v & 255) / 255)
    }

    /// Sticks an image on the selected flat face, as an opaque decal.
    func insertDecal(_ url: URL) {
        guard let f = selectedFaces.first, let info = faceInfo(f.body, f.index), info.planar else {
            return toast = "Select a flat face for the decal"
        }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil), let png = PNGFile.encode(image) else {
            return toast = "Couldn't read that image"
        }
        // Sized to most of the face, never larger than it.
        let side = max(sqrt(info.area) * 0.6, 1)
        let aspect = Double(image.height) / Double(max(image.width, 1))
        let plane = SketchPlane(origin: info.centroid + info.normal * 0.02, normal: info.normal, xDir: info.xDir)
        let ref = ReferenceImage(name: url.deletingPathExtension().lastPathComponent, png: png, plane: plane,
                                 width: side, height: side * aspect, opacity: 1, decal: true)
        selection = []
        begin(.image(image: ref))
    }
}
