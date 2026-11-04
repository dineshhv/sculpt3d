import Foundation
import SceneKit
import SwiftUI
import UniformTypeIdentifiers
import simd

/// Everything File ▸ Export to offers.
enum ExportKind: String, CaseIterable, Identifiable {
    case sculpt3d, threeMF, parasolid, step, dxfSketch, dxfDrawing, iges, stl, usdz, jt, glb, obj, dwgSketch, dwgDrawing, svg, pdf, brep

    var id: String { rawValue }

    var title: String {
        switch self {
        case .sculpt3d: return "Sculpt3D"
        case .threeMF: return "3MF"
        case .parasolid: return "Parasolid file"
        case .step: return "STEP"
        case .dxfSketch: return "DXF from Sketch"
        case .dxfDrawing: return "DXF from Drawing"
        case .iges: return "IGES"
        case .stl: return "STL"
        case .usdz: return "USDZ"
        case .jt: return "JT"
        case .glb: return "GLB"
        case .obj: return "OBJ"
        case .dwgSketch: return "DWG from Sketch"
        case .dwgDrawing: return "DWG from Drawing"
        case .svg: return "SVG"
        case .pdf: return "PDF"
        case .brep: return "BREP"
        }
    }

    var detail: String {
        switch self {
        case .sculpt3d: return "Full history, editable in Sculpt3D"
        case .threeMF: return "Mesh with colours, for 3D printing"
        case .parasolid: return "Not available: Parasolid is a licensed format"
        case .step: return "Precise solids for any CAD app"
        case .dxfSketch: return "2D lines of the sketch, for CAD and laser cutting"
        case .dxfDrawing: return "The drawing sheet's views and dimensions"
        case .iges: return "Precise surfaces for older CAD apps"
        case .stl: return "Mesh for 3D printing"
        case .usdz: return "AR Quick Look, Reality Composer"
        case .jt: return "Not available: JT needs a licensed toolkit"
        case .glb: return "glTF binary for the web, games and AR"
        case .obj: return "Mesh with vertex colours"
        case .dwgSketch, .dwgDrawing: return "Not available: use DXF, which every DWG app opens"
        case .svg: return "The sketch, or the drawing sheet, as vector art"
        case .pdf: return "The drawing sheet"
        case .brep: return "OpenCascade's own precise format"
        }
    }

    var fileExtension: String {
        switch self {
        case .sculpt3d: return "sculpt3d"
        case .threeMF: return "3mf"
        case .parasolid: return "x_t"
        case .step: return "step"
        case .dxfSketch, .dxfDrawing: return "dxf"
        case .iges: return "iges"
        case .stl: return "stl"
        case .usdz: return "usdz"
        case .jt: return "jt"
        case .glb: return "glb"
        case .obj: return "obj"
        case .dwgSketch, .dwgDrawing: return "dwg"
        case .svg: return "svg"
        case .pdf: return "pdf"
        case .brep: return "brep"
        }
    }

    var contentType: UTType {
        switch self {
        case .sculpt3d: return .sculpt3DModel
        case .usdz: return .usdz
        case .svg: return .svg
        case .pdf: return .pdf
        default: return UTType(filenameExtension: fileExtension) ?? .data
        }
    }

    /// Formats the app can't write (licensed toolkits); listed so it's clear why.
    var available: Bool { ![.parasolid, .jt, .dwgSketch, .dwgDrawing].contains(self) }
    /// Formats that need a sketch.
    var needsSketch: Bool { self == .dxfSketch }
    var fromDrawing: Bool { self == .dxfDrawing || self == .pdf }
    /// Formats a slicer opens.
    var forSlicer: Bool { self == .threeMF || self == .stl || self == .obj }

    /// The order of the Export to menu.
    static let menu: [ExportKind] = [.sculpt3d, .threeMF, .parasolid, .step, .dxfSketch, .dxfDrawing, .iges, .stl, .usdz, .jt,
                                     .glb, .obj, .dwgSketch, .dwgDrawing, .svg, .pdf]
}

/// Any exported bytes, for SwiftUI's file exporter.
struct AnyExportFile: FileDocument {
    static var readableContentTypes: [UTType] { [.data] }
    static var writableContentTypes: [UTType] { [.data] + ExportKind.allCases.map(\.contentType) }
    var data: Data
    init(data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws { data = configuration.file.regularFileContents ?? Data() }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: data) }
}

/// Favourite formats for File ▸ Quick Export, and where they go.
enum QuickExport {
    private static let formatsKey = "quickExportFormats", folderKey = "quickExportFolder"

    static var formats: [ExportKind] {
        get { (UserDefaults.standard.stringArray(forKey: formatsKey) ?? []).compactMap(ExportKind.init) }
        set { UserDefaults.standard.set(newValue.map(\.rawValue), forKey: formatsKey) }
    }

    static var folder: URL {
        get {
            if let path = UserDefaults.standard.string(forKey: folderKey) { return URL(fileURLWithPath: path) }
            #if os(macOS)
            return FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask)[0]
            #else
            return FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            #endif
        }
        set { UserDefaults.standard.set(newValue.path, forKey: folderKey) }
    }
}

extension ModelStore {
    /// Bodies an export covers: the selection's, or every visible body.
    var exportBodies: [Int] {
        let picked = touchedBodies
        return picked.isEmpty ? visibleMeshes.keys.sorted() : picked
    }

    /// The sketch a 2D export uses: the open one, the selected ones, or every visible sketch.
    var exportSketches: [Sketch] {
        if let s = sketch { return [s.sketch] }
        let items = referenceItems.filter { $0.isSketch && !settings.hiddenItems.contains($0.id) }
        let picked = items.filter { itemSelection.contains($0.id) }
        return (picked.isEmpty ? items : picked).compactMap { if case let .sketch(s) = $0.content { return s } else { return nil } }
    }

    /// The file for one format, or nil (with a toast saying why).
    func exportData(_ kind: ExportKind, drawing: DrawingSettings? = nil) -> Data? {
        guard kind.available else {
            toast = kind.detail
            return nil
        }
        switch kind {
        case .sculpt3d:
            return try? Sculpt3DFile(features: features, settings: settings).encoded()
        case .step: return export(.step)
        case .iges: return export(.iges)
        case .stl: return export(.stl)
        case .brep: return export(.brep)
        case .threeMF: return threeMF()
        case .glb: return glb()
        case .obj: return obj()
        case .usdz: return usdz()
        case .dxfSketch:
            let sketches = exportSketches
            guard !sketches.isEmpty else { toast = "Make or select a sketch to export"; return nil }
            return SketchVector.dxf(sketches)
        case .svg:
            let sketches = exportSketches
            if !sketches.isEmpty { return SketchVector.svg(sketches) }
            return drawingSheet(drawing)?.svg
        case .dxfDrawing: return drawingSheet(drawing).map(SketchVector.dxf(sheet:))
        case .pdf: return drawingSheet(drawing)?.pdf
        case .parasolid, .jt, .dwgSketch, .dwgDrawing: return nil
        }
    }

    private func drawingSheet(_ settings: DrawingSettings?) -> DrawingSheet? {
        let views = drawingProjections()
        guard !views.isEmpty else { toast = "Add a body to make a drawing"; return nil }
        var s = settings ?? DrawingSettings()
        if s.title == "Untitled" { s.title = documentName }
        return DrawingSheet.layout(views, settings: s, units: self.settings.units)
    }

    // MARK: Meshes

    /// One body's triangles with shared corners merged, so the mesh is watertight.
    private func welded(_ m: BodyMesh) -> (positions: [SIMD3<Float>], triangles: [SIMD3<UInt32>]) {
        var index: [SIMD3<Int32>: UInt32] = [:], positions: [SIMD3<Float>] = [], remap: [UInt32] = []
        remap.reserveCapacity(m.positions.count)
        for p in m.positions {
            let key = SIMD3<Int32>((p * 10_000).rounded(.toNearestOrAwayFromZero))
            if let i = index[key] { remap.append(i) } else {
                index[key] = UInt32(positions.count)
                remap.append(UInt32(positions.count))
                positions.append(p)
            }
        }
        var tris: [SIMD3<UInt32>] = []
        var i = 0
        while i + 2 < m.indices.count {
            let t = SIMD3(remap[Int(m.indices[i])], remap[Int(m.indices[i + 1])], remap[Int(m.indices[i + 2])])
            if t.x != t.y && t.y != t.z && t.x != t.z { tris.append(t) }
            i += 3
        }
        return (positions, tris)
    }

    private func meshes(for bodies: [Int]) -> [BodyMesh]? {
        let list = bodies.compactMap { meshes[$0] }
        guard !list.isEmpty else { toast = "Nothing to export"; return nil }
        return list
    }

    private func hex(_ c: SIMD3<Float>) -> String {
        String(format: "#%02X%02X%02X", Int(min(max(c.x, 0), 1) * 255), Int(min(max(c.y, 0), 1) * 255), Int(min(max(c.z, 0), 1) * 255))
    }

    func threeMF() -> Data? {
        guard let list = meshes(for: exportBodies) else { return nil }
        var model = """
        <?xml version="1.0" encoding="UTF-8"?>
        <model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">
        <metadata name="Application">Sculpt3D</metadata>
        <metadata name="Title">\(xmlEscape(documentName))</metadata>
        <resources>
        <basematerials id="1">

        """
        for m in list {
            let look = settings.appearance[m.id] ?? Appearance()
            model += "<base name=\"\(xmlEscape(settings.name(of: m.id)))\" displaycolor=\"\(hex(look.color))\"/>\n"
        }
        model += "</basematerials>\n"
        for (k, m) in list.enumerated() {
            let w = welded(m)
            model += "<object id=\"\(k + 2)\" type=\"model\" name=\"\(xmlEscape(settings.name(of: m.id)))\" pid=\"1\" pindex=\"\(k)\"><mesh><vertices>\n"
            for p in w.positions { model += String(format: "<vertex x=\"%.4f\" y=\"%.4f\" z=\"%.4f\"/>\n", p.x, p.y, p.z) }
            model += "</vertices><triangles>\n"
            for t in w.triangles { model += "<triangle v1=\"\(t.x)\" v2=\"\(t.y)\" v3=\"\(t.z)\"/>\n" }
            model += "</triangles></mesh></object>\n"
        }
        model += "</resources>\n<build>\n"
        for k in list.indices { model += "<item objectid=\"\(k + 2)\"/>\n" }
        model += "</build>\n</model>\n"
        let types = """
        <?xml version="1.0" encoding="UTF-8"?>
        <Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
        <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
        <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>
        </Types>
        """
        let rels = """
        <?xml version="1.0" encoding="UTF-8"?>
        <Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
        <Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>
        </Relationships>
        """
        return ZipWriter.archive([("[Content_Types].xml", Data(types.utf8)), ("_rels/.rels", Data(rels.utf8)),
                                  ("3D/3dmodel.model", Data(model.utf8))])
    }

    func obj() -> Data? {
        guard let list = meshes(for: exportBodies) else { return nil }
        var out = "# Sculpt3D — \(documentName)\n# Units: millimetres. Vertex colours follow each vertex (x y z r g b).\n"
        var base = 1
        for m in list {
            let w = welded(m)
            let c = (settings.appearance[m.id] ?? Appearance()).color
            out += "o \(settings.name(of: m.id).replacingOccurrences(of: " ", with: "_"))\n"
            for p in w.positions { out += String(format: "v %.4f %.4f %.4f %.3f %.3f %.3f\n", p.x, p.y, p.z, c.x, c.y, c.z) }
            for t in w.triangles { out += "f \(Int(t.x) + base) \(Int(t.y) + base) \(Int(t.z) + base)\n" }
            base += w.positions.count
        }
        return Data(out.utf8)
    }

    /// glTF 2.0 binary: Y up, metres, one PBR material per body.
    func glb() -> Data? {
        guard let list = meshes(for: exportBodies) else { return nil }
        var bin = Data(), accessors: [[String: Any]] = [], views: [[String: Any]] = [], meshesJSON: [[String: Any]] = []
        var materials: [[String: Any]] = [], nodes: [[String: Any]] = []
        func view(_ data: Data, target: Int) -> Int {
            while bin.count % 4 != 0 { bin.append(0) }
            views.append(["buffer": 0, "byteOffset": bin.count, "byteLength": data.count, "target": target])
            bin.append(data)
            return views.count - 1
        }
        for m in list {
            // Z up (mm) to Y up (m).
            let pos = m.positions.map { SIMD3<Float>($0.x, $0.z, -$0.y) * 0.001 }
            let nrm = m.normals.map { SIMD3<Float>($0.x, $0.z, -$0.y) }
            let lo = pos.reduce(SIMD3(repeating: .greatestFiniteMagnitude)) { simd_min($0, $1) }
            let hi = pos.reduce(SIMD3(repeating: -.greatestFiniteMagnitude)) { simd_max($0, $1) }
            let pv = view(pos.withUnsafeBufferPointer { Data(buffer: $0) }.packedFloat3(count: pos.count), target: 34962)
            let nv = view(nrm.withUnsafeBufferPointer { Data(buffer: $0) }.packedFloat3(count: nrm.count), target: 34962)
            let iv = view(m.indices.withUnsafeBufferPointer { Data(buffer: $0) }, target: 34963)
            accessors.append(["bufferView": pv, "componentType": 5126, "count": pos.count, "type": "VEC3",
                              "min": [lo.x, lo.y, lo.z], "max": [hi.x, hi.y, hi.z]])
            accessors.append(["bufferView": nv, "componentType": 5126, "count": nrm.count, "type": "VEC3"])
            accessors.append(["bufferView": iv, "componentType": 5125, "count": m.indices.count, "type": "SCALAR"])
            let look = settings.appearance[m.id] ?? Appearance()
            var mat: [String: Any] = ["name": settings.name(of: m.id),
                                      "pbrMetallicRoughness": ["baseColorFactor": [look.color.x, look.color.y, look.color.z, look.opacity],
                                                               "metallicFactor": look.metallic, "roughnessFactor": look.roughness]]
            if look.opacity < 1 { mat["alphaMode"] = "BLEND" }
            materials.append(mat)
            let a = accessors.count - 3
            meshesJSON.append(["name": settings.name(of: m.id),
                               "primitives": [["attributes": ["POSITION": a, "NORMAL": a + 1], "indices": a + 2, "material": materials.count - 1]]])
            nodes.append(["mesh": meshesJSON.count - 1, "name": settings.name(of: m.id)])
        }
        while bin.count % 4 != 0 { bin.append(0) }
        let json: [String: Any] = [
            "asset": ["version": "2.0", "generator": "Sculpt3D"],
            "scene": 0, "scenes": [["name": documentName, "nodes": Array(nodes.indices)]], "nodes": nodes,
            "meshes": meshesJSON, "materials": materials, "accessors": accessors, "bufferViews": views,
            "buffers": [["byteLength": bin.count]],
        ]
        guard var jsonData = try? JSONSerialization.data(withJSONObject: json) else { return nil }
        while jsonData.count % 4 != 0 { jsonData.append(0x20) }
        var out = Data()
        out.appendLE(UInt32(0x4654_6C67)); out.appendLE(UInt32(2)); out.appendLE(UInt32(12 + 8 + jsonData.count + 8 + bin.count))
        out.appendLE(UInt32(jsonData.count)); out.appendLE(UInt32(0x4E4F_534A)); out.append(jsonData)
        out.appendLE(UInt32(bin.count)); out.appendLE(UInt32(0x004E_4942)); out.append(bin)
        return out
    }

    /// USDZ through SceneKit: Y up, metres, physically based materials.
    func usdz() -> Data? {
        guard let list = meshes(for: exportBodies) else { return nil }
        let scene = SCNScene()
        for m in list {
            let pos = m.positions.map { SCNVector3(CGFloat($0.x) * 0.001, CGFloat($0.z) * 0.001, CGFloat(-$0.y) * 0.001) }
            let nrm = m.normals.map { SCNVector3(CGFloat($0.x), CGFloat($0.z), CGFloat(-$0.y)) }
            let element = SCNGeometryElement(indices: m.indices.map { Int32($0) }, primitiveType: .triangles)
            let geometry = SCNGeometry(sources: [SCNGeometrySource(vertices: pos), SCNGeometrySource(normals: nrm)], elements: [element])
            let look = settings.appearance[m.id] ?? Appearance()
            let material = SCNMaterial()
            material.lightingModel = .physicallyBased
            #if os(macOS)
            material.diffuse.contents = NSColor(srgbRed: CGFloat(look.color.x), green: CGFloat(look.color.y), blue: CGFloat(look.color.z), alpha: 1)
            #else
            material.diffuse.contents = UIColor(red: CGFloat(look.color.x), green: CGFloat(look.color.y), blue: CGFloat(look.color.z), alpha: 1)
            #endif
            material.metalness.contents = NSNumber(value: look.metallic)
            material.roughness.contents = NSNumber(value: look.roughness)
            material.transparency = CGFloat(look.opacity)
            geometry.materials = [material]
            let node = SCNNode(geometry: geometry)
            node.name = settings.name(of: m.id)
            scene.rootNode.addChildNode(node)
        }
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("sculpt3d-\(UUID().uuidString).usdz")
        defer { try? FileManager.default.removeItem(at: url) }
        guard scene.write(to: url, options: nil, delegate: nil, progressHandler: nil), let data = try? Data(contentsOf: url) else {
            toast = "USDZ export failed"
            return nil
        }
        return data
    }

    /// Writes the file for `kind` into `folder`, named after the document.
    @discardableResult
    func write(_ kind: ExportKind, to folder: URL, name: String? = nil) -> URL? {
        guard let data = exportData(kind) else { return nil }
        let base = (name ?? documentName).replacingOccurrences(of: "/", with: "-")
        let url = folder.appendingPathComponent(base).appendingPathExtension(kind.fileExtension)
        do {
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            try data.write(to: url)
            return url
        } catch {
            toast = "Couldn't write \(url.lastPathComponent)"
            return nil
        }
    }
}

private func xmlEscape(_ s: String) -> String {
    s.replacingOccurrences(of: "&", with: "&amp;").replacingOccurrences(of: "<", with: "&lt;")
        .replacingOccurrences(of: ">", with: "&gt;").replacingOccurrences(of: "\"", with: "&quot;")
}

private extension Data {
    mutating func appendLE<T: FixedWidthInteger>(_ v: T) {
        var le = v.littleEndian
        Swift.withUnsafeBytes(of: &le) { append(contentsOf: $0) }
    }

    /// SIMD3<Float> is padded to 16 bytes; glTF wants 12.
    func packedFloat3(count: Int) -> Data {
        var out = Data(capacity: count * 12)
        withUnsafeBytes { raw in
            let stride = raw.count / Swift.max(count, 1)
            for i in 0..<count { out.append(contentsOf: raw[(i * stride)..<(i * stride + 12)]) }
        }
        return out
    }
}

// MARK: Zip (3MF packages)

/// A minimal zip writer (stored entries, no compression), enough for 3MF.
enum ZipWriter {
    private static let crcTable: [UInt32] = (0..<256).map { n -> UInt32 in
        var c = UInt32(n)
        for _ in 0..<8 { c = (c & 1) != 0 ? 0xEDB8_8320 ^ (c >> 1) : c >> 1 }
        return c
    }

    static func crc32(_ data: Data) -> UInt32 {
        var c: UInt32 = 0xFFFF_FFFF
        for b in data { c = crcTable[Int((c ^ UInt32(b)) & 0xFF)] ^ (c >> 8) }
        return c ^ 0xFFFF_FFFF
    }

    static func archive(_ entries: [(String, Data)]) -> Data {
        var out = Data(), central = Data()
        for (name, data) in entries {
            let nameData = Data(name.utf8), crc = crc32(data), offset = UInt32(out.count)
            out.appendLE(UInt32(0x0403_4B50)); out.appendLE(UInt16(20)); out.appendLE(UInt16(0)); out.appendLE(UInt16(0))
            out.appendLE(UInt16(0)); out.appendLE(UInt16(0x21))  // time, date (1980-01-01)
            out.appendLE(crc); out.appendLE(UInt32(data.count)); out.appendLE(UInt32(data.count))
            out.appendLE(UInt16(nameData.count)); out.appendLE(UInt16(0)); out.append(nameData); out.append(data)
            central.appendLE(UInt32(0x0201_4B50)); central.appendLE(UInt16(20)); central.appendLE(UInt16(20)); central.appendLE(UInt16(0))
            central.appendLE(UInt16(0)); central.appendLE(UInt16(0)); central.appendLE(UInt16(0x21))
            central.appendLE(crc); central.appendLE(UInt32(data.count)); central.appendLE(UInt32(data.count))
            central.appendLE(UInt16(nameData.count)); central.appendLE(UInt16(0)); central.appendLE(UInt16(0))
            central.appendLE(UInt16(0)); central.appendLE(UInt16(0)); central.appendLE(UInt32(0)); central.appendLE(offset)
            central.append(nameData)
        }
        let centralOffset = UInt32(out.count)
        out.append(central)
        out.appendLE(UInt32(0x0605_4B50)); out.appendLE(UInt16(0)); out.appendLE(UInt16(0))
        out.appendLE(UInt16(entries.count)); out.appendLE(UInt16(entries.count))
        out.appendLE(UInt32(central.count)); out.appendLE(centralOffset); out.appendLE(UInt16(0))
        return out
    }
}

// MARK: 2D vectors (DXF, SVG)

enum SketchVector {
    /// Every visible curve of the sketches, as polylines (mm, sketch-plane coordinates),
    /// with lines, circles and arcs kept exact for DXF.
    enum Curve {
        case line(SIMD2<Double>, SIMD2<Double>)
        case circle(SIMD2<Double>, Double)
        case arc(SIMD2<Double>, Double, start: Double, sweep: Double)
        case polyline([SIMD2<Double>], closed: Bool)
    }

    static func curves(_ sketches: [Sketch]) -> [Curve] {
        var out: [Curve] = []
        // Several sketches are laid side by side when they sit on different planes.
        var dx = 0.0
        for (k, s) in sketches.enumerated() {
            var local: [Curve] = []
            for l in s.lines where !l.construction { local.append(.line(s.points[l.a], s.points[l.b])) }
            for (i, c) in s.circles.enumerated() where !c.construction {
                if let (start, sweep) = s.arcAngles(i) {
                    local.append(.arc(s.points[c.center], c.radius, start: start, sweep: sweep))
                } else {
                    local.append(.circle(s.points[c.center], c.radius))
                }
            }
            for sp in s.splines where !sp.construction && sp.points.count >= 2 {
                local.append(.polyline(sp.samples(s.points), closed: sp.closed))
            }
            for e in s.ellipses where !e.construction {
                local.append(.polyline(ellipseSamples(e, center: s.points[e.center]), closed: true))
            }
            let pts = local.flatMap(points)
            guard let first = pts.first else { continue }
            let lo = pts.reduce(first) { simd_min($0, $1) }, hi = pts.reduce(first) { simd_max($0, $1) }
            let shift = k == 0 ? SIMD2<Double>(0, 0) : SIMD2(dx - lo.x, 0)
            out += local.map { offset($0, shift) }
            dx = (k == 0 ? hi.x : dx + hi.x - lo.x) + 20
        }
        return out
    }

    private static func points(_ c: Curve) -> [SIMD2<Double>] {
        switch c {
        case let .line(a, b): return [a, b]
        case let .circle(c, r), let .arc(c, r, _, _): return [c - SIMD2(r, r), c + SIMD2(r, r)]
        case let .polyline(p, _): return p
        }
    }

    private static func offset(_ c: Curve, _ d: SIMD2<Double>) -> Curve {
        switch c {
        case let .line(a, b): return .line(a + d, b + d)
        case let .circle(p, r): return .circle(p + d, r)
        case let .arc(p, r, s, w): return .arc(p + d, r, start: s, sweep: w)
        case let .polyline(p, closed): return .polyline(p.map { $0 + d }, closed: closed)
        }
    }

    private static func f(_ v: Double) -> String { String(format: "%.6f", v) }

    private static func header() -> String {
        "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n" +
            "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLTYPE\n70\n2\n" +
            "0\nLTYPE\n2\nCONTINUOUS\n70\n0\n3\nSolid line\n72\n65\n73\n0\n40\n0.0\n" +
            "0\nLTYPE\n2\nHIDDEN\n70\n0\n3\nHidden __ __ __\n72\n65\n73\n2\n40\n2.6\n49\n1.6\n74\n0\n49\n-1.0\n74\n0\n" +
            "0\nENDTAB\n0\nTABLE\n2\nLAYER\n70\n3\n" +
            "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n" +
            "0\nLAYER\n2\nHIDDEN\n70\n0\n62\n8\n6\nHIDDEN\n" +
            "0\nLAYER\n2\nANNOTATION\n70\n0\n62\n5\n6\nCONTINUOUS\n" +
            "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
    }

    private static let footer = "0\nENDSEC\n0\nEOF\n"

    private static func polyline(_ pts: [SIMD2<Double>], closed: Bool, layer: String = "0") -> String {
        var s = "0\nLWPOLYLINE\n8\n\(layer)\n90\n\(pts.count)\n70\n\(closed ? 1 : 0)\n"
        for p in pts { s += "10\n\(f(p.x))\n20\n\(f(p.y))\n" }
        return s
    }

    /// DXF (R2000, millimetres) of the sketches' curves.
    static func dxf(_ sketches: [Sketch]) -> Data {
        var s = header()
        for c in curves(sketches) {
            switch c {
            case let .line(a, b):
                s += "0\nLINE\n8\n0\n10\n\(f(a.x))\n20\n\(f(a.y))\n30\n0\n11\n\(f(b.x))\n21\n\(f(b.y))\n31\n0\n"
            case let .circle(p, r):
                s += "0\nCIRCLE\n8\n0\n10\n\(f(p.x))\n20\n\(f(p.y))\n30\n0\n40\n\(f(r))\n"
            case let .arc(p, r, start, sweep):
                // DXF arcs run counter-clockwise from start to end angle.
                var a0 = start, a1 = start + sweep
                if sweep < 0 { swap(&a0, &a1) }
                s += "0\nARC\n8\n0\n10\n\(f(p.x))\n20\n\(f(p.y))\n30\n0\n40\n\(f(r))\n50\n\(f(a0 * 180 / .pi))\n51\n\(f(a1 * 180 / .pi))\n"
            case let .polyline(p, closed):
                s += polyline(p, closed: closed)
            }
        }
        return Data((s + footer).utf8)
    }

    /// DXF of a drawing sheet: visible lines, hidden lines on their own layer, and text.
    static func dxf(sheet: DrawingSheet) -> Data {
        var s = header()
        let h = Double(sheet.size.height)
        for stroke in sheet.strokes where stroke.points.count >= 2 {
            let pts = stroke.points.map { SIMD2(Double($0.x), h - Double($0.y)) }
            s += polyline(pts, closed: false, layer: stroke.dashed ? "HIDDEN" : "0")
        }
        for l in sheet.labels {
            let text = l.text.replacingOccurrences(of: "\n", with: " ")
            s += "0\nTEXT\n8\nANNOTATION\n10\n\(f(Double(l.at.x)))\n20\n\(f(h - Double(l.at.y)))\n30\n0\n40\n\(f(Double(l.size)))\n1\n\(text)\n"
            s += "72\n1\n73\n2\n11\n\(f(Double(l.at.x)))\n21\n\(f(h - Double(l.at.y)))\n31\n0\n"
        }
        return Data((s + footer).utf8)
    }

    /// SVG (millimetres, y up flipped to SVG's y down) of the sketches.
    static func svg(_ sketches: [Sketch]) -> Data {
        let list = curves(sketches)
        let pts = list.flatMap(points)
        let lo = pts.reduce(SIMD2(repeating: Double.greatestFiniteMagnitude)) { simd_min($0, $1) } - 2
        let hi = pts.reduce(SIMD2(repeating: -Double.greatestFiniteMagnitude)) { simd_max($0, $1) } + 2
        let size = simd_max(hi - lo, SIMD2(1, 1))
        func sp(_ p: SIMD2<Double>) -> String { String(format: "%.4f,%.4f", p.x - lo.x, hi.y - p.y) }
        var out = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"\(f(size.x))mm\" height=\"\(f(size.y))mm\" viewBox=\"0 0 \(f(size.x)) \(f(size.y))\">\n"
        out += "<g fill=\"none\" stroke=\"#000\" stroke-width=\"0.25\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n"
        for c in list {
            switch c {
            case let .line(a, b): out += "<polyline points=\"\(sp(a)) \(sp(b))\"/>\n"
            case let .circle(p, r): out += String(format: "<circle cx=\"%.4f\" cy=\"%.4f\" r=\"%.4f\"/>\n", p.x - lo.x, hi.y - p.y, r)
            case let .arc(p, r, start, sweep):
                let n = max(8, Int(abs(sweep) / (2 * .pi) * 96))
                let arc = (0...n).map { k -> SIMD2<Double> in
                    let t = start + sweep * Double(k) / Double(n)
                    return p + SIMD2(cos(t), sin(t)) * r
                }
                out += "<polyline points=\"\(arc.map(sp).joined(separator: " "))\"/>\n"
            case let .polyline(p, closed):
                out += "<\(closed ? "polygon" : "polyline") points=\"\(p.map(sp).joined(separator: " "))\"/>\n"
            }
        }
        out += "</g>\n</svg>\n"
        return Data(out.utf8)
    }
}

// MARK: Slicers

/// 3D-printing slicers that can open the model directly.
struct Slicer: Identifiable, Hashable {
    let name: String
    let url: URL
    var id: URL { url }

    static let known = ["BambuStudio", "Bambu Studio", "OrcaSlicer", "PrusaSlicer", "UltiMaker Cura", "Ultimaker Cura", "Cura",
                        "Creality Print", "CrealityPrint", "AnycubicSlicerNext", "ElegooSlicer", "SuperSlicer", "Simplify3D",
                        "IdeaMaker", "FlashPrint", "Lychee Slicer", "ChituBox", "Preform"]

    /// Installed slicers, found in the Applications folders.
    static var installed: [Slicer] {
        #if os(macOS)
        let fm = FileManager.default
        let dirs = [URL(fileURLWithPath: "/Applications"), fm.homeDirectoryForCurrentUser.appendingPathComponent("Applications")]
        var found: [Slicer] = []
        for dir in dirs {
            let apps = (try? fm.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)) ?? []
            for app in apps where app.pathExtension == "app" {
                let name = app.deletingPathExtension().lastPathComponent
                if known.contains(where: { name.localizedCaseInsensitiveContains($0) }) && !found.contains(where: { $0.name == name }) {
                    found.append(Slicer(name: name, url: app))
                }
            }
        }
        return found.sorted { $0.name < $1.name }
        #else
        return []
        #endif
    }
}
