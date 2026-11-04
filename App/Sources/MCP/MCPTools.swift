import ImageIO
import Foundation
import simd
#if os(macOS)
import AppKit
#endif

/// The tools Sculpt3D offers over MCP. Each modeling tool appends one feature
/// to the active document's parametric history.
@MainActor
final class MCPToolbox {
    struct Tool {
        let name: String
        let description: String
        let schema: [String: Any]
        var readOnly = false
        let run: @MainActor (Args) async throws -> [[String: Any]]
    }

    private(set) var tools: [Tool] = []

    init() { tools = makeTools() }

    var definitions: [[String: Any]] {
        tools.map { t in
            ["name": t.name, "description": t.description, "inputSchema": t.schema,
             "annotations": ["readOnlyHint": t.readOnly]]
        }
    }

    func has(_ name: String) -> Bool { tools.contains { $0.name == name } }

    func call(_ name: String, _ arguments: [String: Any]) async throws -> [[String: Any]] {
        guard let tool = tools.first(where: { $0.name == name }) else { throw MCPToolError("Unknown tool: \(name)") }
        return try await tool.run(Args(arguments))
    }

    // MARK: - Tool list

    private func makeTools() -> [Tool] {
        [
            Tool(name: "list_documents", description: "Lists the open Sculpt3D documents. The first one is active; tools act on it.",
                 schema: S.object([:]), readOnly: true) { _ in
                text(DocumentRegistry.shared.open.enumerated().map { i, e in
                    ["id": e.id.uuidString, "name": e.name, "active": i == 0, "features": e.store?.features.count ?? 0]
                })
            },
            Tool(name: "select_document", description: "Makes the document with this id (from list_documents) the active one.",
                 schema: S.object(["id": S.string("Document id")], required: ["id"])) { a in
                guard let id = UUID(uuidString: try a.string("id")), DocumentRegistry.shared.activate(id: id) else {
                    throw MCPToolError("No open document has that id")
                }
                return text("Active document: \(DocumentRegistry.shared.active?.name ?? "")")
            },
            Tool(name: "new_document", description: "Opens a new, empty Sculpt3D document window and makes it active.",
                 schema: S.object([:])) { _ in try await self.newDocument() },
            Tool(name: "get_model",
                 description: "The active document's parametric history (features with their editable parameters), its bodies with volume, area and bounding box, and the project variables.",
                 schema: S.object([:]), readOnly: true) { _ in
                let (store, _) = try self.document(editing: false)
                return text(self.modelSummary(store))
            },
            Tool(name: "get_body_topology",
                 description: "Faces and edges of one body, with the indices that fillet_edges, chamfer_edges, shell_body, measure and sketch planes on faces take.",
                 schema: S.object(["body": S.int("Body id")], required: ["body"]), readOnly: true) { a in
                let (store, _) = try self.document(editing: false)
                return text(try self.topology(store, body: try self.body(a, "body", in: store)))
            },
            Tool(name: "add_primitive",
                 description: """
                 Adds a solid primitive as a new body. Its base sits on `position` (default origin), centred in X and Y, rising along +Z. \
                 Sizes in mm: box width (X), depth (Y), height (Z); cylinder radius, height; sphere radius; cone radius (bottom), \
                 top_radius, height; torus major_radius, minor_radius.
                 """,
                 schema: S.object([
                     "kind": S.string("Primitive", options: PrimitiveKind.allCases.map(\.rawValue)),
                     "width": S.number(), "depth": S.number(), "height": S.number(), "radius": S.number(),
                     "top_radius": S.number(), "major_radius": S.number(), "minor_radius": S.number(),
                     "position": S.vec3("Base centre [x, y, z]"),
                 ], required: ["kind"])) { a in
                let (store, _) = try self.document()
                guard let kind = PrimitiveKind(rawValue: try a.string("kind")) else { throw MCPToolError("Unknown primitive kind") }
                var d = kind.defaultDims
                switch kind {
                case .box: d = [a.double("width") ?? d.x, a.double("depth") ?? d.y, a.double("height") ?? d.z]
                case .cylinder: d = [a.double("radius") ?? d.x, a.double("height") ?? d.y, 0]
                case .sphere: d = [a.double("radius") ?? d.x, 0, 0]
                case .cone: d = [a.double("radius") ?? d.x, a.double("top_radius") ?? d.y, a.double("height") ?? d.z]
                case .torus: d = [a.double("major_radius") ?? d.x, a.double("minor_radius") ?? d.y, 0]
                }
                return try self.apply(store, .primitive(kind: kind, dims: d, position: try a.vec3("position") ?? .zero))
            },
            Tool(name: "sketch_extrude",
                 description: """
                 Draws closed shapes on a sketch plane and extrudes them along the plane normal. Shape coordinates are 2D in the \
                 plane (u along its X direction, v along its Y). Planes: xy (normal +Z), xz (normal -Y), yz (normal +X); a custom \
                 {origin, normal, x_dir}; or a planar face of a body via `face`, whose sketch origin is the face centroid. \
                 For new_body and join, distance runs along the normal (negative goes the other way); for cut it is the depth \
                 into the material, against the normal, so a cut sketched on a top face with distance 5 removes 5 mm downward. \
                 Shapes inside other shapes become holes. mode: new_body, join or cut \
                 (join/cut need target_body unless there is only one body).
                 """,
                 schema: S.object(Self.sketchProperties.merging([
                     "distance": S.number("Extrusion distance in mm (for cut: depth into the material)"),
                     "mode": S.string("new_body, join or cut", options: ["new_body", "join", "cut"]),
                     "target_body": S.int("Body to join to or cut from"),
                 ]) { $1 }, required: ["shapes", "distance"])) { a in
                let (store, _) = try self.document()
                let sketch = try self.sketch(a, store)
                let (mode, target) = try self.combine(a, store)
                return try self.apply(store, .extrude(sketch: sketch, distance: try a.requireDouble("distance"), mode: mode, target: target))
            },
            Tool(name: "sketch_revolve",
                 description: "Draws closed shapes on a sketch plane (as in sketch_extrude) and revolves them about the plane's u or v axis through its origin. The profile must not cross the axis.",
                 schema: S.object(Self.sketchProperties.merging([
                     "angle": S.number("Degrees, default 360"),
                     "axis": S.string("Sketch axis to revolve around", options: ["u", "v"]),
                     "mode": S.string("new_body, join or cut", options: ["new_body", "join", "cut"]),
                     "target_body": S.int("Body to join to or cut from"),
                 ]) { $1 }, required: ["shapes", "axis"])) { a in
                let (store, _) = try self.document()
                let sketch = try self.sketch(a, store)
                let (mode, target) = try self.combine(a, store)
                return try self.apply(store, .revolve(sketch: sketch, angle: a.double("angle") ?? 360, aroundV: try a.string("axis") == "v",
                                                      mode: mode, target: target))
            },
            Tool(name: "boolean", description: "Union, subtract or intersect two bodies. The result keeps target_body's id; tool_body is consumed.",
                 schema: S.object([
                     "operation": S.string("Boolean operation", options: BooleanKind.allCases.map(\.rawValue)),
                     "target_body": S.int("Body that is kept"), "tool_body": S.int("Body that is added, removed or intersected"),
                 ], required: ["operation", "target_body", "tool_body"])) { a in
                let (store, _) = try self.document()
                guard let kind = BooleanKind(rawValue: try a.string("operation")) else { throw MCPToolError("Unknown boolean operation") }
                return try self.apply(store, .boolean(kind: kind, target: try self.body(a, "target_body", in: store),
                                                      tool: try self.body(a, "tool_body", in: store)))
            },
            Tool(name: "fillet_edges", description: "Rounds edges of a body. Pass edge indices from get_body_topology, or all: true.",
                 schema: S.edges(extra: ["radius": S.number("Radius in mm")], required: ["body", "radius"])) { a in
                let (store, _) = try self.document()
                let body = try self.body(a, "body", in: store)
                return try self.apply(store, .fillet(body: body, edges: try self.edges(a, body, store), radius: try a.requireDouble("radius")))
            },
            Tool(name: "chamfer_edges", description: "Bevels edges of a body. Pass edge indices from get_body_topology, or all: true.",
                 schema: S.edges(extra: ["distance": S.number("Chamfer distance in mm")], required: ["body", "distance"])) { a in
                let (store, _) = try self.document()
                let body = try self.body(a, "body", in: store)
                return try self.apply(store, .chamfer(body: body, edges: try self.edges(a, body, store), distance: try a.requireDouble("distance")))
            },
            Tool(name: "shell_body", description: "Hollows a body to the given wall thickness, removing the listed faces to leave openings.",
                 schema: S.object(["body": S.int("Body id"), "faces": S.intArray("Face indices to open"),
                                   "thickness": S.number("Wall thickness in mm")], required: ["body", "thickness"])) { a in
                let (store, _) = try self.document()
                return try self.apply(store, .shell(body: try self.body(a, "body", in: store), faces: a.ints("faces") ?? [],
                                                    thickness: try a.requireDouble("thickness")))
            },
            Tool(name: "push_pull_face", description: "Moves a planar face along its normal, adding material (positive) or removing it (negative).",
                 schema: S.object(["body": S.int("Body id"), "face": S.int("Face index"), "distance": S.number("mm")],
                                  required: ["body", "face", "distance"])) { a in
                let (store, _) = try self.document()
                return try self.apply(store, .pushPull(body: try self.body(a, "body", in: store), face: try a.requireInt("face"),
                                                       distance: try a.requireDouble("distance")))
            },
            Tool(name: "transform_body",
                 description: "Moves and/or rotates a body. rotate turns `angle` degrees about `axis` through `origin` (default the origin) and is applied before translate.",
                 schema: S.object(["body": S.int("Body id"), "translate": S.vec3("Offset [x, y, z] in mm"),
                                   "rotate": S.object(["axis": S.vec3("Axis direction"), "angle": S.number("Degrees"),
                                                       "origin": S.vec3("Point on the axis")], required: ["axis", "angle"])],
                                  required: ["body"])) { a in
                let (store, _) = try self.document()
                let body = try self.body(a, "body", in: store)
                var ops: [Operation] = []
                if let r = a.object("rotate") {
                    guard let axis = try r.vec3("axis"), simd_length(axis) > 1e-9 else { throw MCPToolError("rotate.axis must be a non-zero vector") }
                    ops.append(.rotate(body: body, origin: try r.vec3("origin") ?? .zero, axis: axis, angle: try r.requireDouble("angle")))
                }
                if let t = try a.vec3("translate"), simd_length(t) > 1e-9 {
                    ops.append(.translate(body: body, direction: simd_normalize(t), distance: simd_length(t)))
                }
                guard !ops.isEmpty else { throw MCPToolError("Give translate and/or rotate") }
                return try self.apply(store, ops)
            },
            Tool(name: "mirror_body", description: "Mirrors a body across a plane (xy, xz, yz or custom, optionally offset along its normal).",
                 schema: S.object(["body": S.int("Body id"), "plane": Self.planeSchema, "offset": S.number("Plane offset in mm"),
                                   "keep_original": S.bool("Keep the original body (default true)")], required: ["body", "plane"])) { a in
                let (store, _) = try self.document()
                return try self.apply(store, .mirror(body: try self.body(a, "body", in: store), plane: try self.plane(a, store),
                                                     keepOriginal: a.bool("keep_original") ?? true))
            },
            Tool(name: "pattern_body",
                 description: "Repeats a body. linear: count copies `spacing` mm apart along `direction`. circular: count copies spread over `angle` degrees about `axis` through `origin`. fuse joins the copies into one body.",
                 schema: S.object(["body": S.int("Body id"), "kind": S.string("Pattern kind", options: ["linear", "circular"]),
                                   "count": S.int("Number of instances, including the original"),
                                   "direction": S.vec3("Linear direction"), "spacing": S.number("Linear spacing in mm"),
                                   "axis": S.vec3("Circular axis direction (default Z)"), "origin": S.vec3("Circular axis point"),
                                   "angle": S.number("Circular total angle, default 360"), "fuse": S.bool("Join copies (default false)")],
                                  required: ["body", "kind", "count"])) { a in
                let (store, _) = try self.document()
                let body = try self.body(a, "body", in: store)
                let count = try a.requireInt("count"), fuse = a.bool("fuse") ?? false
                if try a.string("kind") == "linear" {
                    guard let dir = try a.vec3("direction"), simd_length(dir) > 1e-9 else { throw MCPToolError("linear needs a direction") }
                    return try self.apply(store, .linearPattern(body: body, direction: simd_normalize(dir), count: count,
                                                                spacing: try a.requireDouble("spacing"), fuse: fuse))
                }
                return try self.apply(store, .circularPattern(body: body, origin: try a.vec3("origin") ?? .zero, axis: try a.vec3("axis") ?? [0, 0, 1],
                                                              count: count, angle: a.double("angle") ?? 360, fuse: fuse))
            },
            Tool(name: "scale_body",
                 description: "Scales a body about `center` (default its bounding-box centre; \"bottom\" uses the centre of its base). Pass `factor` for uniform scaling or `factors` [x, y, z] to stretch it differently along each world axis, e.g. a sphere into a flattened pebble.",
                 schema: S.object(["body": S.int("Body id"), "factor": S.number("Uniform factor"),
                                   "factors": S.vec3("Per-axis factors [x, y, z]"),
                                   "center": ["description": "Point [x, y, z], or \"bottom\" for the base centre"] as [String: Any]],
                                  required: ["body"])) { a in
                let (store, _) = try self.document()
                let body = try self.body(a, "body", in: store)
                guard let m = store.meshes[body] else { throw MCPToolError("Body \(body) has no geometry") }
                var center = SIMD3<Double>((m.boundsMin + m.boundsMax) / 2)
                if a.raw["center"] as? String == "bottom" { center.z = Double(m.boundsMin.z) } else if let c = try? a.vec3("center") { center = c }
                if let f = try a.vec3("factors") {
                    guard f.x > 0, f.y > 0, f.z > 0 else { throw MCPToolError("factors must be positive") }
                    return try self.apply(store, .scaleAxes(body: body, factors: f, center: center, uniform: f.x == f.y && f.y == f.z))
                }
                guard let f = a.double("factor"), f > 0 else { throw MCPToolError("Pass factor or factors") }
                return try self.apply(store, .scaleAxes(body: body, factors: SIMD3(repeating: f), center: center, uniform: true))
            },
            Tool(name: "scatter_body",
                 description: "Scatters randomised copies of `body` over the upward-facing surface of `target` (trees on terrain, pebbles on a bank). Copies land where a straight-down ray first hits target, not under any `avoid` body (e.g. water), on slopes below max_slope, at least `spacing` mm apart. Each copy gets a random size between min_scale and max_scale and a random turn; the seed makes it repeatable. Copies take the source's appearance. The source is removed unless keep_original.",
                 schema: S.object(["body": S.int("Body to copy"), "target": S.int("Body to scatter onto"),
                                   "count": S.int("Number of copies (fewer if there isn't room)"),
                                   "seed": S.int("Random seed, default 1"),
                                   "min_scale": S.number("Smallest size factor, default 0.8"),
                                   "max_scale": S.number("Largest size factor, default 1.2"),
                                   "spacing": S.number("Minimum distance between copies in mm, default 0"),
                                   "max_slope": S.number("Steepest ground in degrees, default 40"),
                                   "sink": S.number("Fraction of height pushed into the ground, default 0.02"),
                                   "random_rotation": S.bool("Turn each copy about its up axis (default true)"),
                                   "align_to_surface": S.bool("Tilt copies to the surface normal (default false)"),
                                   "keep_original": S.bool("Keep the source body (default false)"),
                                   "avoid": ["type": "array", "items": ["type": "integer"], "description": "Bodies whose footprint takes no copies"] as [String: Any]],
                                  required: ["body", "target", "count"])) { a in
                let (store, _) = try self.document()
                let source = try self.body(a, "body", in: store), target = try self.body(a, "target", in: store)
                var p = ScatterSettings()
                p.count = try a.requireInt("count")
                if let v = a.int("seed") { p.seed = v }
                if let v = a.double("min_scale") { p.minScale = v }
                if let v = a.double("max_scale") { p.maxScale = v }
                if let v = a.double("spacing") { p.spacing = v }
                if let v = a.double("max_slope") { p.maxSlope = v }
                if let v = a.double("sink") { p.sink = v }
                if let v = a.bool("random_rotation") { p.randomRotation = v }
                if let v = a.bool("align_to_surface") { p.alignToSurface = v }
                if let v = a.bool("keep_original") { p.keepOriginal = v }
                let avoid = (a.raw["avoid"] as? [Any])?.compactMap { ($0 as? NSNumber)?.intValue } ?? []
                for b in avoid where !store.bodyIds.contains(b) { throw MCPToolError("avoid body \(b) does not exist") }
                return try self.apply(store, .scatter(source: source, target: target, avoid: avoid, settings: p))
            },
            Tool(name: "create_terrain",
                 description: "Adds a smooth landscape solid: a surface fitted through a grid of heights, with vertical sides and a flat bottom at center z. Relief comes from any mix of: `heights` (rows of mm above the base, first row at -Y), `image_path` (greyscale height map, white high, scaled to image_height mm), `noise` (rolling hills), then `hills` (domes added on top), `terraces` (flat steps), and finally `channels` (river or path beds carved along polylines to `bed` mm above the base, easing back up over `bank` mm). Coordinates are world XY in mm. Returns the new body.",
                 schema: S.object([
                    "name": S.string("Name in the history"),
                    "center": S.vec3("Centre of the bottom face, default [0, 0, 0]"),
                    "width": S.number("Size along X, mm (default 200)"), "depth": S.number("Size along Y, mm (default 200)"),
                    "resolution": S.int("Grid samples along the longer side, 8–129 (default 48)"),
                    "base_thickness": S.number("Solid under the lowest relief, mm (default 10)"),
                    "heights": ["type": "array", "items": ["type": "array", "items": ["type": "number"]],
                                "description": "Rows of heights in mm; resampled to the grid"] as [String: Any],
                    "image_path": S.string("Absolute path to a greyscale height map image"),
                    "image_height": S.number("Relief of white in the image, mm (default 40)"),
                    "noise": ["type": "object", "description": "{seed, height (mm, default 40), scale (hill size as a fraction of the terrain, default 0.45), octaves (4), roughness (0.5)}"] as [String: Any],
                    "hills": ["type": "array", "items": ["type": "object"], "description": "[{x, y, radius, height}] smooth domes, mm"] as [String: Any],
                    "terraces": ["type": "object", "description": "{count, softness 0–1}: flat steps"] as [String: Any],
                    "channels": ["type": "array", "items": ["type": "object"], "description": "[{points: [[x, y], …], width, bed, bank}] carved beds, mm"] as [String: Any],
                 ])) { a in
                let (store, _) = try self.document()
                var t = TerrainSettings(name: (a.raw["name"] as? String) ?? "Terrain")
                if let c = try a.vec3("center") { t.center = c }
                t.width = a.double("width") ?? 200
                t.depth = a.double("depth") ?? 200
                guard t.width > 0, t.depth > 0 else { throw MCPToolError("width and depth must be positive") }
                let n = min(max(a.int("resolution") ?? 48, 8), 129)
                if t.width >= t.depth { t.nx = n; t.ny = max(8, Int((Double(n) * t.depth / t.width).rounded())) }
                else { t.ny = n; t.nx = max(8, Int((Double(n) * t.width / t.depth).rounded())) }
                if let f = a.double("base_thickness") { t.floor = max(f, 0.5) }
                t.heights = [Double](repeating: 0, count: t.nx * t.ny)
                var shaped = false
                if let rows = a.raw["heights"] as? [[Any]], !rows.isEmpty {
                    let grid = rows.map { $0.compactMap { ($0 as? NSNumber)?.doubleValue } }
                    guard let w = grid.first?.count, w > 1, grid.allSatisfy({ $0.count == w }), grid.count > 1 else {
                        throw MCPToolError("heights must be at least 2 × 2 rows of equal length")
                    }
                    for j in 0..<t.ny {
                        for i in 0..<t.nx {
                            // Bilinear resample.
                            let x = Double(i) / Double(t.nx - 1) * Double(w - 1), y = Double(j) / Double(t.ny - 1) * Double(grid.count - 1)
                            let x0 = min(Int(x), w - 2), y0 = min(Int(y), grid.count - 2), fx = x - Double(x0), fy = y - Double(y0)
                            let top = grid[y0][x0] * (1 - fx) + grid[y0][x0 + 1] * fx
                            let bottom = grid[y0 + 1][x0] * (1 - fx) + grid[y0 + 1][x0 + 1] * fx
                            t.heights[j * t.nx + i] += top * (1 - fy) + bottom * fy
                        }
                    }
                    shaped = true
                }
                if let path = a.raw["image_path"] as? String {
                    guard let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil),
                          let image = CGImageSourceCreateImageAtIndex(src, 0, nil),
                          let field = Terrain.field(from: image, nx: t.nx, ny: t.ny) else { throw MCPToolError("Couldn't read the image at \(path)") }
                    let relief = a.double("image_height") ?? 40
                    for k in t.heights.indices { t.heights[k] += field[k] * relief }
                    shaped = true
                }
                if let nz = a.raw["noise"] as? [String: Any] {
                    var noise = TerrainNoise()
                    if let v = (nz["seed"] as? NSNumber)?.intValue { noise.seed = v }
                    if let v = (nz["scale"] as? NSNumber)?.doubleValue { noise.scale = v }
                    if let v = (nz["octaves"] as? NSNumber)?.intValue { noise.octaves = min(max(v, 1), 8) }
                    if let v = (nz["roughness"] as? NSNumber)?.doubleValue { noise.roughness = v }
                    let height = (nz["height"] as? NSNumber)?.doubleValue ?? 40
                    let field = Terrain.noiseField(nx: t.nx, ny: t.ny, aspect: t.depth / t.width, noise)
                    for k in t.heights.indices { t.heights[k] += field[k] * height }
                    if !shaped { t.noise = noise; t.noiseHeight = height }
                }
                func num(_ d: [String: Any], _ k: String) -> Double? { (d[k] as? NSNumber)?.doubleValue }
                if let hills = a.raw["hills"] as? [[String: Any]], !hills.isEmpty {
                    Terrain.add(hills.compactMap { h in
                        guard let x = num(h, "x"), let y = num(h, "y"), let r = num(h, "radius"), let z = num(h, "height") else { return nil }
                        return Terrain.Hill(x: x, y: y, radius: r, height: z)
                    }, to: &t)
                    t.noise = nil
                }
                if let tr = a.raw["terraces"] as? [String: Any], let count = (tr["count"] as? NSNumber)?.intValue {
                    Terrain.terrace(&t, count: count, softness: num(tr, "softness") ?? 0.3)
                    t.noise = nil
                }
                if let chans = a.raw["channels"] as? [[String: Any]], !chans.isEmpty {
                    Terrain.carve(try chans.map { c in
                        let pts = (c["points"] as? [[Any]] ?? []).compactMap { p -> SIMD2<Double>? in
                            let v = p.compactMap { ($0 as? NSNumber)?.doubleValue }
                            return v.count >= 2 ? SIMD2(v[0], v[1]) : nil
                        }
                        guard pts.count >= 2 else { throw MCPToolError("each channel needs at least two points") }
                        return Terrain.Channel(points: pts, width: num(c, "width") ?? 20, bed: num(c, "bed") ?? 0, bank: num(c, "bank") ?? 10)
                    }, into: &t)
                    t.noise = nil
                }
                return try self.apply(store, .terrain(settings: t))
            },
            Tool(name: "duplicate_body", description: "Copies a body in place as a new body.",
                 schema: S.object(["body": S.int("Body id")], required: ["body"])) { a in
                let (store, _) = try self.document()
                return try self.apply(store, .duplicate(body: try self.body(a, "body", in: store)))
            },
            Tool(name: "delete_body", description: "Removes a body (as a history step, so it can be undone).",
                 schema: S.object(["body": S.int("Body id")], required: ["body"])) { a in
                let (store, _) = try self.document()
                return try self.apply(store, .delete(body: try self.body(a, "body", in: store)))
            },
            Tool(name: "measure",
                 description: "Measures one entity (volume/area/length/radius) or the distance and angle between two. An entity is {body} or {body, face} or {body, edge}.",
                 schema: S.object(["a": Self.entitySchema, "b": Self.entitySchema], required: ["a"]), readOnly: true) { a in
                let (store, _) = try self.document(editing: false)
                return text(try self.measure(a, store))
            },
            Tool(name: "update_feature",
                 description: "Changes parameters of a feature in the history and rebuilds everything after it. values maps a parameter id or label (from get_model) to a number, or to an expression string over project variables (e.g. \"width * 2\").",
                 schema: S.object(["feature_id": S.string("Feature id"),
                                   "values": ["type": "object", "description": "Parameter id or label → number or expression",
                                              "additionalProperties": ["type": ["number", "string"]]]],
                                  required: ["feature_id", "values"])) { a in
                let (store, _) = try self.document()
                return try self.updateFeature(a, store)
            },
            Tool(name: "delete_feature", description: "Removes a feature from the history and rebuilds.",
                 schema: S.object(["feature_id": S.string("Feature id")], required: ["feature_id"])) { a in
                let (store, _) = try self.document()
                let f = try self.feature(a, store)
                store.deleteFeature(f.id)
                return text(["deleted": f.title, "model": self.modelSummary(store)])
            },
            Tool(name: "suppress_feature", description: "Turns a feature off (or back on) without deleting it.",
                 schema: S.object(["feature_id": S.string("Feature id"), "suppressed": S.bool("Default true")], required: ["feature_id"])) { a in
                let (store, _) = try self.document()
                let f = try self.feature(a, store)
                if f.suppressed != (a.bool("suppressed") ?? true) { store.toggleSuppressed(f.id) }
                return text(self.modelSummary(store))
            },
            Tool(name: "set_variable",
                 description: "Creates, changes or (with an empty expression) removes a project variable. Feature parameters bound to expressions update with it.",
                 schema: S.object(["name": S.string("Variable name"), "expression": S.string("Number or expression, e.g. \"40\" or \"width / 2\"")],
                                  required: ["name", "expression"])) { a in
                let (store, _) = try self.document()
                let name = try a.string("name"), expr = try a.string("expression").trimmingCharacters(in: .whitespaces)
                var settings = store.settings
                if let i = settings.variables.firstIndex(where: { $0.name == name }) {
                    if expr.isEmpty { settings.variables.remove(at: i) } else { settings.variables[i].expression = expr }
                } else if !expr.isEmpty {
                    settings.variables.append(Variable(name: name, expression: expr))
                }
                store.settings = settings
                return text(self.modelSummary(store))
            },
            Tool(name: "undo", description: "Undoes the last history change.", schema: S.object([:])) { _ in
                let (store, _) = try self.document()
                guard store.canUndo else { throw MCPToolError("Nothing to undo") }
                store.undo()
                return text(self.modelSummary(store))
            },
            Tool(name: "redo", description: "Redoes the last undone change.", schema: S.object([:])) { _ in
                let (store, _) = try self.document()
                guard store.canRedo else { throw MCPToolError("Nothing to redo") }
                store.redo()
                return text(self.modelSummary(store))
            },
            Tool(name: "import_file", description: "Imports a STEP, IGES or BREP file from an absolute path as new bodies.",
                 schema: S.object(["path": S.string("Absolute file path")], required: ["path"])) { a in
                let (store, _) = try self.document()
                let path = (try a.string("path") as NSString).expandingTildeInPath
                guard FileManager.default.fileExists(atPath: path) else { throw MCPToolError("No file at \(path)") }
                let before = Set(store.bodyIds)
                store.importFile(URL(fileURLWithPath: path))
                let added = store.bodyIds.filter { !before.contains($0) }
                guard !added.isEmpty else { throw MCPToolError(store.toast ?? "Nothing was imported") }
                return text(["new_bodies": added.map { self.bodySummary(store, $0) }])
            },
            Tool(name: "export_model",
                 description: "Writes bodies to a file: STEP, IGES, STL, BREP (precise), 3MF, GLB, OBJ, USDZ (meshes with colours), DXF or SVG (the open, selected or visible sketches; \"dxf_drawing\" and \"pdf\" give a drawing sheet of the model), or SCULPT3D (the whole document). Defaults: all bodies, saved to ~/Downloads/<document name>.<extension>.",
                 schema: S.object(["format": S.string("File format", options: Self.exportFormats),
                                   "path": S.string("Absolute output path"), "bodies": S.intArray("Body ids (default all)")],
                                  required: ["format"]), readOnly: true) { a in
                let (store, _) = try self.document(editing: false)
                return try self.export(a, store)
            },
            Tool(name: "set_appearance",
                 description: "Paints bodies: a colour (\"#RRGGBB\"), metallic 0–1, roughness 0–1 and opacity 0–1. Shown in the viewport and in Visualize renders, and saved with the document.",
                 schema: S.object(["bodies": S.intArray("Body ids"), "color": S.string("Hex colour, e.g. #8F9A78"),
                                   "metallic": S.number("0 matte to 1 metal"), "roughness": S.number("0 mirror to 1 matte"),
                                   "opacity": S.number("1 solid, lower is see-through")], required: ["bodies"])) { a in
                let (store, viewport) = try self.document(editing: false)
                let bodies = try (a.ints("bodies") ?? []).map { b -> Int in
                    guard store.bodyIds.contains(b) else { throw MCPToolError("No body \(b)") }
                    return b
                }
                guard !bodies.isEmpty else { throw MCPToolError("bodies is empty") }
                var look = Appearance()
                if let hex = a.raw["color"] as? String {
                    guard let c = Self.color(hex) else { throw MCPToolError("color must be #RRGGBB") }
                    look.color = c
                }
                look.metallic = Float(min(max(a.double("metallic") ?? 0, 0), 1))
                look.roughness = Float(min(max(a.double("roughness") ?? 0.45, 0), 1))
                look.opacity = Float(min(max(a.double("opacity") ?? 1, 0.05), 1))
                var settings = store.settings
                for b in bodies { settings.appearance[b] = look }
                store.settings = settings
                viewport.redraw()
                return text(["painted": bodies])
            },
            Tool(name: "set_environment",
                 description: "Sets the background, lighting and Visualized render options of the active document. Only the given values change. Rendering effects (cast shadows, ambient occlusion, smooth shading) show in the visualized display mode (set_view display_mode) and in Visualize.",
                 schema: S.object([
                    "background": S.string("Background colour #RRGGBB"),
                    "gradient": S.bool("Lighter towards the top"),
                    "show_grid": S.bool("Show the ground grid"),
                    "light_intensity": S.number("Key light multiplier 0–2"),
                    "light_azimuth": S.number("Key light direction, degrees right of the viewer (-180–180)"),
                    "light_elevation": S.number("Key light height above the ground, degrees 10–85"),
                    "ground_shadow": S.bool("Soft contact shadow under each body"),
                    "cast_shadows": S.bool("Bodies cast shadows on each other and on the floor"),
                    "shadow_softness": S.number("0 crisp to 1 very soft"),
                    "ambient_occlusion": S.bool("Darken creases and contacts"),
                    "occlusion_strength": S.number("0–1"),
                    "smooth_shading": S.bool("Blend normals across shallow face joins"),
                 ])) { a in
                let (store, viewport) = try self.document(editing: false)
                var env = store.settings.environment
                if let hex = a.raw["background"] as? String {
                    guard let c = Self.color(hex) else { throw MCPToolError("background must be #RRGGBB") }
                    env.background = c
                }
                func f(_ k: String, _ range: ClosedRange<Double>) -> Float? { a.double(k).map { Float(min(max($0, range.lowerBound), range.upperBound)) } }
                if let v = a.bool("gradient") { env.gradient = v }
                if let v = a.bool("show_grid") { env.showGrid = v }
                if let v = f("light_intensity", 0...2) { env.lightIntensity = v }
                if let v = f("light_azimuth", -180...180) { env.lightAzimuth = v }
                if let v = f("light_elevation", 10...85) { env.lightElevation = v }
                if let v = a.bool("ground_shadow") { env.groundShadow = v }
                if let v = a.bool("cast_shadows") { env.castShadows = v }
                if let v = f("shadow_softness", 0...1) { env.shadowSoftness = v }
                if let v = a.bool("ambient_occlusion") { env.ambientOcclusion = v }
                if let v = f("occlusion_strength", 0...1) { env.occlusionStrength = v }
                if let v = a.bool("smooth_shading") { env.smoothShading = v }
                store.settings.environment = env
                viewport.redraw()
                return text("Environment updated")
            },
            Tool(name: "set_view",
                 description: "Points the document's camera at a standard view, zooms to fit, and/or sets an exact orbit angle. yaw is degrees around Z (0 looks from +X), pitch is degrees above the ground plane, zoom scales the distance (below 1 is closer).",
                 schema: S.object(Self.cameraProperties)) { a in
                let (_, viewport) = try self.document(editing: false)
                self.setView(a, viewport)
                return text("View updated")
            },
            Tool(name: "screenshot",
                 description: "Renders the active document's viewport to a PNG image. Optionally sets a view (iso, top, front, right) and fits first.",
                 schema: S.object(Self.cameraProperties.merging(["scale": S.number("Pixel scale 1–3, default 1.5")]) { $1 }),
                 readOnly: true) { a in
                let (_, viewport) = try self.document(editing: false)
                self.setView(a, viewport)
                guard viewport.size.width > 2,
                      let image = viewport.screenshot(scale: CGFloat(min(max(a.double("scale") ?? 1.5, 1), 3))),
                      let png = PNGFile.encode(image) else {
                    throw MCPToolError("The document window needs to be open and visible to render a screenshot")
                }
                return [["type": "image", "data": png.base64EncodedString(), "mimeType": "image/png"]]
            },
        ]
    }

    // MARK: - Documents

    private func document(editing: Bool = true) throws -> (ModelStore, ViewportController) {
        guard let e = DocumentRegistry.shared.active, let store = e.store, let viewport = e.viewport else {
            throw MCPToolError("No Sculpt3D document is open. Call new_document first.")
        }
        if editing && (store.sketch != nil || store.draft != nil) {
            throw MCPToolError("A sketch or tool is open in the app. Finish or cancel it there first.")
        }
        return (store, viewport)
    }

    private func newDocument() async throws -> [[String: Any]] {
        #if os(macOS)
        let before = DocumentRegistry.shared.registrations
        NSDocumentController.shared.newDocument(nil)
        for _ in 0..<50 where DocumentRegistry.shared.registrations == before {
            try await Task.sleep(nanoseconds: 100_000_000)
        }
        guard DocumentRegistry.shared.registrations != before, let e = DocumentRegistry.shared.active else {
            throw MCPToolError("The new document window did not open")
        }
        return text(["id": e.id.uuidString, "name": e.name])
        #else
        throw MCPToolError("Creating documents is only available on the Mac")
        #endif
    }

    // MARK: - Applying operations

    /// Trial-runs the operations as drafts, so a failing one never enters the
    /// history, then commits them. Returns the new bodies and the model.
    private func apply(_ store: ModelStore, _ op: Operation) throws -> [[String: Any]] { try apply(store, [op]) }

    private func apply(_ store: ModelStore, _ ops: [Operation]) throws -> [[String: Any]] {
        let before = Set(store.bodyIds)
        var added: [Feature] = []
        for op in ops {
            let f = Feature(op: op)
            store.draft = Draft(feature: f)
            store.regenerate()
            let error = store.errors[f.id]
            store.draft = nil
            if let error {
                for _ in added { store.undo() }
                store.regenerate()
                throw MCPToolError(error)
            }
            added.append(f)
            store.commit(store.features + [f])
        }
        store.selection = []
        return text([
            "features": added.map { ["id": $0.id.uuidString, "title": $0.title] },
            "new_bodies": store.bodyIds.filter { !before.contains($0) },
            "bodies": store.bodyIds.map { bodySummary(store, $0) },
        ])
    }

    private func updateFeature(_ a: Args, _ store: ModelStore) throws -> [[String: Any]] {
        var f = try feature(a, store)
        guard let values = a.raw["values"] as? [String: Any], !values.isEmpty else { throw MCPToolError("values is empty") }
        let params = f.op.parameters
        for (key, value) in values {
            guard let p = params.first(where: { String($0.id) == key || $0.label.caseInsensitiveCompare(key) == .orderedSame }) else {
                throw MCPToolError("\(f.title) has no parameter \"\(key)\". Its parameters: \(params.map { "\($0.id) \($0.label)" }.joined(separator: ", "))")
            }
            if let expr = value as? String {
                guard let v = ExpressionEvaluator(store.settings.variables).evaluate(expr) else {
                    throw MCPToolError("Could not evaluate \"\(expr)\"")
                }
                f.op = f.op.setting(p.id, to: v)
                f.bindings = (f.bindings ?? [:]).merging([p.id: expr]) { $1 }
            } else if let v = (value as? NSNumber)?.doubleValue {
                f.op = f.op.setting(p.id, to: v)
                f.bindings?[p.id] = nil
            } else {
                throw MCPToolError("Value for \(key) must be a number or an expression string")
            }
        }
        store.draft = Draft(feature: f, replacing: f.id)
        store.regenerate()
        let error = store.errors[f.id]
        store.draft = nil
        if let error {
            store.regenerate()
            throw MCPToolError(error)
        }
        store.commit(store.features.map { $0.id == f.id ? f : $0 })
        return text(modelSummary(store))
    }

    // MARK: - Arguments

    private func body(_ a: Args, _ key: String, in store: ModelStore) throws -> Int {
        let b = try a.requireInt(key)
        guard store.bodyIds.contains(b) else {
            throw MCPToolError("No body \(b). Bodies: \(store.bodyIds.map(String.init).joined(separator: ", "))")
        }
        return b
    }

    private func feature(_ a: Args, _ store: ModelStore) throws -> Feature {
        let id = try a.string("feature_id")
        guard let f = store.features.first(where: { $0.id.uuidString.caseInsensitiveCompare(id) == .orderedSame }) else {
            throw MCPToolError("No feature with id \(id)")
        }
        return f
    }

    private func edges(_ a: Args, _ body: Int, _ store: ModelStore) throws -> [Int] {
        if a.bool("all") == true, let p = store.properties(body) { return Array(0..<p.edgeCount) }
        guard let edges = a.ints("edges"), !edges.isEmpty else { throw MCPToolError("Give edge indices or all: true") }
        return edges
    }

    private func combine(_ a: Args, _ store: ModelStore) throws -> (CombineMode, Int?) {
        let mode: CombineMode
        switch a.raw["mode"] as? String ?? "new_body" {
        case "join": mode = .join
        case "cut": mode = .cut
        case "new_body": return (.newBody, nil)
        default: throw MCPToolError("mode must be new_body, join or cut")
        }
        if a.raw["target_body"] != nil { return (mode, try body(a, "target_body", in: store)) }
        guard store.bodyIds.count == 1 else { throw MCPToolError("\(mode == .cut ? "cut" : "join") needs target_body") }
        return (mode, store.bodyIds[0])
    }

    private func plane(_ a: Args, _ store: ModelStore) throws -> SketchPlane {
        var p: SketchPlane
        if let face = a.object("face") {
            let b = try body(face, "body", in: store), i = try face.requireInt("face")
            guard let info = store.faceInfo(b, i) else { throw MCPToolError("Body \(b) has no face \(i)") }
            guard info.planar else { throw MCPToolError("Face \(i) of body \(b) is not planar") }
            p = SketchPlane(origin: info.centroid, normal: info.normal, xDir: info.xDir)
        } else if let name = a.raw["plane"] as? String {
            switch name.lowercased() {
            case "xy": p = .xy
            case "xz": p = .xz
            case "yz": p = .yz
            default: throw MCPToolError("plane must be xy, xz, yz or {origin, normal, x_dir}")
            }
        } else if let o = a.object("plane") {
            guard let n = try o.vec3("normal"), simd_length(n) > 1e-9 else { throw MCPToolError("plane.normal must be non-zero") }
            let normal = simd_normalize(n)
            var x = try o.vec3("x_dir") ?? (abs(normal.z) > 0.9 ? [1, 0, 0] : [0, 0, 1])
            x -= normal * simd_dot(x, normal)
            guard simd_length(x) > 1e-9 else { throw MCPToolError("plane.x_dir must not be parallel to the normal") }
            p = SketchPlane(origin: try o.vec3("origin") ?? .zero, normal: normal, xDir: simd_normalize(x))
        } else {
            p = .xy
        }
        if let offset = a.double("offset") { p.origin += p.normal * offset }
        return p
    }

    private func sketch(_ a: Args, _ store: ModelStore) throws -> Sketch {
        var s = Sketch(plane: try plane(a, store))
        guard let shapes = a.raw["shapes"] as? [[String: Any]], !shapes.isEmpty else { throw MCPToolError("shapes is empty") }
        for (i, raw) in shapes.enumerated() {
            let sh = Args(raw)
            switch try sh.string("type") {
            case "rectangle":
                let w = try sh.requireDouble("width"), h = try sh.requireDouble("height")
                var p0 = SIMD2(sh.double("x") ?? 0, sh.double("y") ?? 0)
                if sh.bool("centered") ?? false { p0 -= SIMD2(w, h) / 2 }
                s.addRectangle(p0, p0 + SIMD2(w, h), start: nil, end: nil)
            case "rounded_rectangle":
                let w = try sh.requireDouble("width"), h = try sh.requireDouble("height")
                var p0 = SIMD2(sh.double("x") ?? 0, sh.double("y") ?? 0)
                if sh.bool("centered") ?? false { p0 -= SIMD2(w, h) / 2 }
                let r = min(max(sh.double("radius") ?? 0, 0), min(w, h) / 2 - 1e-6)
                guard r > 1e-6 else { s.addRectangle(p0, p0 + SIMD2(w, h), start: nil, end: nil); continue }
                let (x0, y0, x1, y1) = (p0.x, p0.y, p0.x + w, p0.y + h)
                // Straight runs between the corner arcs; arcs run counter-clockwise.
                let b0 = s.addPoint([x0 + r, y0]), b1 = s.addPoint([x1 - r, y0])
                let r0 = s.addPoint([x1, y0 + r]), r1 = s.addPoint([x1, y1 - r])
                let t0 = s.addPoint([x1 - r, y1]), t1 = s.addPoint([x0 + r, y1])
                let l0 = s.addPoint([x0, y1 - r]), l1 = s.addPoint([x0, y0 + r])
                for (a, b) in [(b0, b1), (r0, r1), (t0, t1), (l0, l1)] where a != b { _ = s.addLine(a, b) }
                s.addArc(center: s.addPoint([x1 - r, y0 + r]), start: b1, end: r0)
                s.addArc(center: s.addPoint([x1 - r, y1 - r]), start: r1, end: t0)
                s.addArc(center: s.addPoint([x0 + r, y1 - r]), start: t1, end: l0)
                s.addArc(center: s.addPoint([x0 + r, y0 + r]), start: l1, end: b0)
            case "circle":
                let c = s.addPoint([sh.double("cx") ?? 0, sh.double("cy") ?? 0])
                s.addCircle(center: c, radius: try sh.requireDouble("radius"))
            case "polygon":
                let c = SIMD2(sh.double("cx") ?? 0, sh.double("cy") ?? 0)
                s.addPolygon(center: c, corner: c + [try sh.requireDouble("radius"), 0], sides: sh.int("sides") ?? 6)
            case "polyline":
                guard let pts = raw["points"] as? [[Any]], pts.count >= 3 else { throw MCPToolError("shape \(i): polyline needs 3+ points") }
                let ids = try pts.map { p -> Int in
                    guard p.count == 2, let x = (p[0] as? NSNumber)?.doubleValue, let y = (p[1] as? NSNumber)?.doubleValue else {
                        throw MCPToolError("shape \(i): points are [u, v] pairs")
                    }
                    return s.addPoint([x, y])
                }
                for (p, q) in zip(ids, ids.dropFirst() + [ids[0]]) { _ = s.addLine(p, q) }
            default:
                throw MCPToolError("shape \(i): type must be rectangle, rounded_rectangle, circle, polygon or polyline")
            }
        }
        guard s.profileCount > 0 else { throw MCPToolError("The shapes make no closed profile") }
        return s
    }

    private func setView(_ a: Args, _ viewport: ViewportController) {
        let presets: [String: Camera.Preset] = ["iso": .iso, "top": .top, "front": .front, "right": .right]
        let preset = (a.raw["view"] as? String).flatMap { presets[$0] }
        if let preset { viewport.look(preset) }
        if a.bool("fit") ?? (preset != nil) { viewport.fitAll() }
        if let yaw = a.double("yaw") { viewport.camera.yaw = Float(yaw * .pi / 180) }
        if let pitch = a.double("pitch") { viewport.camera.pitch = Float(min(max(pitch, -89), 89) * .pi / 180) }
        if let zoom = a.double("zoom"), zoom > 0 { viewport.camera.distance *= Float(zoom) }
        if let mode = (a.raw["display_mode"] as? String).flatMap(DisplayMode.init(rawValue:)) { viewport.displayMode = mode }
        if let projection = a.raw["projection"] as? String {
            viewport.store.settings.camera.orthographic = projection == "orthographic"
        }
        viewport.redraw()
    }

    // MARK: - Reports

    private func modelSummary(_ store: ModelStore) -> [String: Any] {
        [
            "document": DocumentRegistry.shared.active?.name ?? "",
            "features": store.features.enumerated().map { i, f -> [String: Any] in
                var d: [String: Any] = ["index": i, "id": f.id.uuidString, "title": f.title]
                if f.suppressed { d["suppressed"] = true }
                if let e = store.errors[f.id] { d["error"] = e }
                let params = f.op.parameters
                if !params.isEmpty {
                    d["parameters"] = params.map { p -> [String: Any] in
                        var pd: [String: Any] = ["id": p.id, "label": p.label, "value": r(p.value), "unit": p.unit]
                        if let expr = f.bindings?[p.id] { pd["expression"] = expr }
                        return pd
                    }
                }
                return d
            },
            "bodies": store.bodyIds.map { bodySummary(store, $0) },
            "variables": store.settings.variables.map { ["name": $0.name, "expression": $0.expression] },
            "units": "mm",
        ]
    }

    private func bodySummary(_ store: ModelStore, _ id: Int) -> [String: Any] {
        guard let p = store.properties(id) else { return ["id": id] }
        return ["id": id, "volume": r(p.volume), "area": r(p.area), "bbox_min": v(p.bboxMin), "bbox_max": v(p.bboxMax),
                "size": v(p.bboxMax - p.bboxMin), "faces": p.faceCount, "edges": p.edgeCount]
    }

    private func topology(_ store: ModelStore, body: Int) throws -> [String: Any] {
        guard let p = store.properties(body) else { throw MCPToolError("No body \(body)") }
        let limit = 400
        let faces = (0..<min(p.faceCount, limit)).compactMap { i -> [String: Any]? in
            guard let f = store.faceInfo(body, i) else { return nil }
            var d: [String: Any] = ["index": i, "area": r(f.area)]
            if f.planar {
                d["type"] = "plane"; d["centroid"] = v(f.centroid); d["normal"] = v(f.normal)
            } else if f.cylindrical {
                d["type"] = "cylinder"; d["radius"] = r(f.radius); d["axis_origin"] = v(f.axisOrigin); d["axis"] = v(f.axisDir)
            } else {
                d["type"] = f.radius > 0 ? "revolved" : "curved"
                if f.radius > 0 { d["radius"] = r(f.radius); d["axis_origin"] = v(f.axisOrigin); d["axis"] = v(f.axisDir) }
            }
            return d
        }
        let edges = (0..<min(p.edgeCount, limit)).compactMap { i -> [String: Any]? in
            guard let e = store.kernel.edgeInfo(body, edge: i) else { return nil }
            var d: [String: Any] = ["index": i, "length": r(e.length), "start": v(e.start), "end": v(e.end)]
            d["type"] = e.linear ? "line" : e.circular ? "circle" : "curve"
            if e.circular { d["center"] = v(e.center); d["radius"] = r(e.radius) }
            return d
        }
        var out: [String: Any] = ["body": body, "faces": faces, "edges": edges]
        if p.faceCount > limit || p.edgeCount > limit { out["truncated"] = "Only the first \(limit) faces and edges are listed" }
        return out
    }

    private func measure(_ a: Args, _ store: ModelStore) throws -> [String: Any] {
        func entity(_ key: String) throws -> (body: Int, kind: Int, index: Int)? {
            guard let e = a.object(key) else { return nil }
            let b = try body(e, "body", in: store)
            if let f = e.int("face") { return (b, 1, f) }
            if let ed = e.int("edge") { return (b, 2, ed) }
            return (b, 0, 0)
        }
        guard let ea = try entity("a") else { throw MCPToolError("a is required") }
        let eb = try entity("b")
        guard let m = store.kernel.measureBody(ea.body, kind: ea.kind, index: ea.index, againstBody: eb?.body ?? -1,
                                               kind: eb?.kind ?? 0, index: eb?.index ?? 0) else {
            throw MCPToolError(store.kernel.lastError.isEmpty ? "Could not measure that" : store.kernel.lastError)
        }
        var out: [String: Any] = [:]
        if m.hasDistance { out["distance"] = r(m.distance); out["closest_points"] = [v(m.p1), v(m.p2)] }
        if m.hasAngle { out["angle_degrees"] = r(m.angle) }
        if m.hasLength { out["length"] = r(m.length) }
        if m.hasArea { out["area"] = r(m.area) }
        if m.hasRadius { out["radius"] = r(m.radius) }
        if eb == nil && ea.kind == 0, let p = store.properties(ea.body) { out["volume"] = r(p.volume); out["surface_area"] = r(p.area) }
        return out
    }

    private func export(_ a: Args, _ store: ModelStore) throws -> [[String: Any]] {
        let requested = try a.string("format").lowercased()
        guard let format = ExchangeFormat(rawValue: requested) else { return try exportOther(requested, a, store) }
        let bodies = try (a.ints("bodies") ?? store.bodyIds).map { b -> Int in
            guard store.bodyIds.contains(b) else { throw MCPToolError("No body \(b)") }
            return b
        }
        guard !bodies.isEmpty else { throw MCPToolError("The model has no bodies to export") }
        let path: String
        if let given = a.raw["path"] as? String {
            path = (given as NSString).expandingTildeInPath
        } else {
            let name = (DocumentRegistry.shared.active?.name ?? "Sculpt3D") as NSString
            let downloads = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
                ?? FileManager.default.temporaryDirectory
            path = downloads.appendingPathComponent(name.deletingPathExtension).appendingPathExtension(format.rawValue).path
        }
        guard store.kernel.exportFile(path, bodies: bodies.map { NSNumber(value: $0) }) else {
            throw MCPToolError(store.kernel.lastError.isEmpty ? "Export failed" : store.kernel.lastError)
        }
        let size = (try? FileManager.default.attributesOfItem(atPath: path)[.size] as? Int) ?? 0
        return text(["path": path, "bytes": size, "bodies": bodies])
    }

    static let exportFormats = ExchangeFormat.allCases.map(\.rawValue) + ["3mf", "glb", "obj", "usdz", "dxf", "dxf_drawing", "svg", "pdf", "sculpt3d"]

    /// Formats written by the app's own exporters rather than the kernel.
    private func exportOther(_ format: String, _ a: Args, _ store: ModelStore) throws -> [[String: Any]] {
        let kinds: [String: ExportKind] = ["3mf": .threeMF, "glb": .glb, "obj": .obj, "usdz": .usdz, "dxf": .dxfSketch,
                                           "dxf_drawing": .dxfDrawing, "svg": .svg, "pdf": .pdf, "sculpt3d": .sculpt3d]
        guard let kind = kinds[format] else { throw MCPToolError("format must be one of \(Self.exportFormats.joined(separator: ", "))") }
        let saved = store.selection
        defer { store.selection = saved }
        if let bodies = a.ints("bodies") {
            for b in bodies where !store.bodyIds.contains(b) { throw MCPToolError("No body \(b)") }
            store.selection = bodies.map { .body($0) }
        } else {
            store.selection = []
        }
        store.toast = nil
        guard let data = store.exportData(kind) else { throw MCPToolError(store.toast ?? "Export failed") }
        let path: String
        if let given = a.raw["path"] as? String {
            path = (given as NSString).expandingTildeInPath
        } else {
            let downloads = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first ?? FileManager.default.temporaryDirectory
            path = downloads.appendingPathComponent(store.documentName).appendingPathExtension(kind.fileExtension).path
        }
        try data.write(to: URL(fileURLWithPath: path))
        return text(["path": path, "bytes": data.count, "format": kind.title])
    }

    // MARK: - Shared schemas

    static let planeSchema: [String: Any] = [
        "description": "\"xy\", \"xz\", \"yz\", or {origin, normal, x_dir}",
        "oneOf": [
            ["type": "string", "enum": ["xy", "xz", "yz"]],
            S.object(["origin": S.vec3("Plane origin"), "normal": S.vec3("Plane normal"), "x_dir": S.vec3("Sketch u direction")],
                     required: ["normal"]),
        ],
    ]

    static let cameraProperties: [String: Any] = [
        "view": S.string("Camera view", options: ["iso", "top", "front", "right"]),
        "fit": S.bool("Zoom to fit all bodies (default true when view is given)"),
        "yaw": S.number("Degrees around Z; 0 looks from +X"),
        "pitch": S.number("Degrees above the ground plane"),
        "zoom": S.number("Distance multiplier; 0.8 is 20% closer"),
        "display_mode": S.string("Viewport display mode; visualized renders with materials, cast shadows and ambient occlusion and hides CAD edges",
                                 options: DisplayMode.allCases.map(\.rawValue)),
        "projection": S.string("Camera projection", options: ["perspective", "orthographic"]),
    ]

    static func color(_ hex: String) -> SIMD3<Float>? {
        let h = hex.trimmingCharacters(in: CharacterSet(charactersIn: "# "))
        guard h.count == 6, let v = UInt32(h, radix: 16) else { return nil }
        return SIMD3(Float((v >> 16) & 0xFF), Float((v >> 8) & 0xFF), Float(v & 0xFF)) / 255
    }

    static let entitySchema = S.object(["body": S.int("Body id"), "face": S.int("Face index"), "edge": S.int("Edge index")],
                                       required: ["body"])

    static let sketchProperties: [String: Any] = [
        "plane": planeSchema,
        "face": S.object(["body": S.int("Body id"), "face": S.int("Planar face index")], required: ["body", "face"]),
        "offset": S.number("Moves the sketch plane along its normal, mm"),
        "shapes": [
            "type": "array",
            "description": """
            Closed shapes in plane coordinates (mm): {type: rectangle, x, y, width, height, centered?} (x, y is the \
            lower-left corner, or the centre when centered is true); {type: rounded_rectangle, same as rectangle plus \
            radius} for rounded corners; {type: circle, cx, cy, radius}; \
            {type: polygon, cx, cy, radius, sides}; {type: polyline, points: [[u, v], ...]} (closed automatically).
            """,
            "items": ["type": "object", "properties": [
                "type": S.string("Shape", options: ["rectangle", "rounded_rectangle", "circle", "polygon", "polyline"]),
                "x": S.number(), "y": S.number(), "width": S.number(), "height": S.number(), "centered": S.bool(),
                "cx": S.number(), "cy": S.number(), "radius": S.number(), "sides": S.int(),
                "points": ["type": "array", "items": ["type": "array", "items": ["type": "number"], "minItems": 2, "maxItems": 2]],
            ], "required": ["type"]],
        ],
    ]
}

// MARK: - Helpers

private func r(_ x: Double) -> Double { (x * 10_000).rounded() / 10_000 }
private func v(_ p: SIMD3<Double>) -> [Double] { [r(p.x), r(p.y), r(p.z)] }

/// One text content block holding `value` as JSON (or a plain string).
private func text(_ value: Any) -> [[String: Any]] {
    if let s = value as? String { return [["type": "text", "text": s]] }
    let data = (try? JSONSerialization.data(withJSONObject: value, options: [.sortedKeys, .withoutEscapingSlashes])) ?? Data()
    return [["type": "text", "text": String(decoding: data, as: UTF8.self)]]
}

/// Typed reads of a tool's JSON arguments.
struct Args {
    let raw: [String: Any]
    init(_ raw: [String: Any]) { self.raw = raw }

    func double(_ k: String) -> Double? { (raw[k] as? NSNumber)?.doubleValue }
    func int(_ k: String) -> Int? { (raw[k] as? NSNumber)?.intValue }
    func bool(_ k: String) -> Bool? { (raw[k] as? NSNumber)?.boolValue }
    func ints(_ k: String) -> [Int]? { (raw[k] as? [Any])?.compactMap { ($0 as? NSNumber)?.intValue } }
    func object(_ k: String) -> Args? { (raw[k] as? [String: Any]).map(Args.init) }

    func requireDouble(_ k: String) throws -> Double {
        guard let d = double(k) else { throw MCPToolError("\(k) is required (a number)") }
        return d
    }

    func requireInt(_ k: String) throws -> Int {
        guard let i = int(k) else { throw MCPToolError("\(k) is required (an integer)") }
        return i
    }

    func string(_ k: String) throws -> String {
        guard let s = raw[k] as? String else { throw MCPToolError("\(k) is required (a string)") }
        return s
    }

    func vec3(_ k: String) throws -> SIMD3<Double>? {
        guard let a = raw[k] else { return nil }
        guard let arr = a as? [Any], arr.count == 3 else { throw MCPToolError("\(k) must be [x, y, z]") }
        let n = arr.compactMap { ($0 as? NSNumber)?.doubleValue }
        guard n.count == 3 else { throw MCPToolError("\(k) must be [x, y, z]") }
        return SIMD3(n[0], n[1], n[2])
    }
}

/// JSON Schema builders.
private enum S {
    static func object(_ props: [String: Any], required: [String] = []) -> [String: Any] {
        var o: [String: Any] = ["type": "object", "properties": props]
        if !required.isEmpty { o["required"] = required }
        return o
    }

    static func number(_ d: String? = nil) -> [String: Any] { described(["type": "number"], d) }
    static func int(_ d: String? = nil) -> [String: Any] { described(["type": "integer"], d) }
    static func bool(_ d: String? = nil) -> [String: Any] { described(["type": "boolean"], d) }

    static func string(_ d: String? = nil, options: [String]? = nil) -> [String: Any] {
        var o: [String: Any] = described(["type": "string"], d)
        if let options { o["enum"] = options }
        return o
    }

    static func vec3(_ d: String) -> [String: Any] {
        ["type": "array", "items": ["type": "number"], "minItems": 3, "maxItems": 3, "description": d]
    }

    static func intArray(_ d: String) -> [String: Any] { ["type": "array", "items": ["type": "integer"], "description": d] }

    static func edges(extra: [String: Any], required: [String]) -> [String: Any] {
        object(["body": int("Body id"), "edges": intArray("Edge indices"), "all": bool("Use every edge of the body")]
            .merging(extra) { $1 }, required: required)
    }

    private static func described(_ o: [String: Any], _ d: String?) -> [String: Any] {
        guard let d else { return o }
        return o.merging(["description": d]) { $1 }
    }
}
