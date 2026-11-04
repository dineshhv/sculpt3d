import Foundation
import simd

enum PrimitiveKind: String, Codable, CaseIterable {
    case box, cylinder, sphere, cone, torus

    var title: String { rawValue.capitalized }

    var symbol: String {
        switch self {
        case .box: return "cube"
        case .cylinder: return "cylinder"
        case .sphere: return "globe"
        case .cone: return "cone"
        case .torus: return "smallcircle.circle"
        }
    }

    var defaultDims: SIMD3<Double> {
        switch self {
        case .box: return [40, 40, 40]
        case .cylinder: return [20, 40, 0]
        case .sphere: return [20, 0, 0]
        case .cone: return [20, 5, 40]
        case .torus: return [25, 8, 0]
        }
    }

    var dimLabels: [String] {
        switch self {
        case .box: return ["Width", "Depth", "Height"]
        case .cylinder: return ["Radius", "Height"]
        case .sphere: return ["Radius"]
        case .cone: return ["Bottom radius", "Top radius", "Height"]
        case .torus: return ["Major radius", "Minor radius"]
        }
    }

    var bridged: O3DPrimitive {
        switch self {
        case .box: return .box
        case .cylinder: return .cylinder
        case .sphere: return .sphere
        case .cone: return .cone
        case .torus: return .torus
        }
    }
}

enum CombineMode: String, Codable, CaseIterable {
    case newBody, join, cut

    var title: String {
        switch self {
        case .newBody: return "New body"
        case .join: return "Join"
        case .cut: return "Cut"
        }
    }

    var bridged: O3DCombineMode {
        switch self {
        case .newBody: return .newBody
        case .join: return .join
        case .cut: return .cut
        }
    }
}

enum BooleanKind: String, Codable, CaseIterable {
    case union, subtract, intersect

    var title: String { rawValue.capitalized }

    var symbol: String {
        switch self {
        case .union: return "square.on.square.squareshape.controlhandles"
        case .subtract: return "square.on.square.dashed"
        case .intersect: return "square.on.square.intersection.dashed"
        }
    }

    var bridged: O3DBoolOp {
        switch self {
        case .union: return .union
        case .subtract: return .subtract
        case .intersect: return .intersect
        }
    }
}

/// Options of Transform ▸ Scatter.
struct ScatterSettings: Codable, Equatable {
    var count = 12
    var seed = 1
    var minScale = 0.8
    var maxScale = 1.2
    /// Minimum distance between copies on the ground plan, mm.
    var spacing = 0.0
    /// Steeper ground than this (degrees from flat) takes no copies.
    var maxSlope = 40.0
    /// How far each copy is pushed into the ground, as a fraction of its height.
    var sink = 0.02
    var randomRotation = true
    /// Tilt copies to the surface normal instead of keeping them upright.
    var alignToSurface = false
    var keepOriginal = false
}

/// One step of the parametric history. Body ids refer to the ids the kernel
/// assigns during replay, which are deterministic for a given history.
enum Operation: Codable, Equatable {
    case primitive(kind: PrimitiveKind, dims: SIMD3<Double>, position: SIMD3<Double>)
    case extrude(sketch: Sketch, distance: Double, mode: CombineMode, target: Int?)
    case revolve(sketch: Sketch, angle: Double, aroundV: Bool, mode: CombineMode, target: Int?)
    case pushPull(body: Int, face: Int, distance: Double)
    case boolean(kind: BooleanKind, target: Int, tool: Int)
    case fillet(body: Int, edges: [Int], radius: Double)
    case chamfer(body: Int, edges: [Int], distance: Double)
    case shell(body: Int, faces: [Int], thickness: Double)
    case move(body: Int, translation: SIMD3<Double>, axis: SIMD3<Double>, angle: Double)
    case mirror(body: Int, plane: SketchPlane, keepOriginal: Bool)
    case duplicate(body: Int)
    case delete(body: Int)
    case imported(name: String, brep: String)
    /// A standalone sketch, kept for sweeps, lofts and later extrusions.
    case sketch(sketch: Sketch, name: String)
    /// A construction plane: `base` turned by `angle` about its X axis, then
    /// moved `offset` along its normal.
    case plane(base: SketchPlane, offset: Double, angle: Double, name: String)
    case sweep(profile: UUID, path: UUID, mode: CombineMode, target: Int?)
    case loft(sections: [UUID], mode: CombineMode, target: Int?)
    case scale(body: Int, factor: Double, center: SIMD3<Double>)
    case linearPattern(body: Int, direction: SIMD3<Double>, count: Int, spacing: Double, fuse: Bool)
    case circularPattern(body: Int, origin: SIMD3<Double>, axis: SIMD3<Double>, count: Int, angle: Double, fuse: Bool)
    case split(body: Int, plane: SketchPlane, offset: Double)
    case deleteFaces(body: Int, faces: [Int])
    case draft(body: Int, faces: [Int], pull: SIMD3<Double>, angle: Double)
    case offsetFaces(body: Int, faces: [Int], distance: Double)
    /// A construction axis.
    case axis(origin: SIMD3<Double>, direction: SIMD3<Double>, name: String)
    case translate(body: Int, direction: SIMD3<Double>, distance: Double)
    case rotate(body: Int, origin: SIMD3<Double>, axis: SIMD3<Double>, angle: Double)
    /// Moves `body` so its bounding box lines up with `target`'s. Per axis:
    /// 0 leave, 1 minimum, 2 centre, 3 maximum.
    case align(body: Int, target: Int, modes: [Int])
    case image(image: ReferenceImage)
    case replaceFace(body: Int, face: Int, targetBody: Int, targetFace: Int)
    case fill(body: Int, edges: [Int])
    /// Edges of a body projected onto a plane, kept as a sketch.
    case projection(body: Int, plane: SketchPlane, name: String)
    /// Tools ▸ Wrap & Emboss: a sketch projected onto a body and raised or sunk.
    case emboss(sketch: UUID, body: Int, height: Double, deboss: Bool)
    /// Scales along each world axis about `center`; `uniform` keeps the three factors equal.
    case scaleAxes(body: Int, factors: SIMD3<Double>, center: SIMD3<Double>, uniform: Bool)
    /// Randomised copies of `source` placed on the top surface of `target`.
    case scatter(source: Int, target: Int, avoid: [Int], settings: ScatterSettings)
    /// Insert ▸ Terrain: a smooth landscape from a height grid.
    case terrain(settings: TerrainSettings)
}

struct Feature: Codable, Equatable, Identifiable {
    var id = UUID()
    var op: Operation
    var suppressed = false
    /// Parameter ids bound to expressions over project variables.
    var bindings: [Int: String]?

    /// The operation with every bound parameter set from its expression.
    func resolved(_ evaluator: ExpressionEvaluator) -> Operation {
        guard let bindings else { return op }
        return bindings.sorted { $0.key < $1.key }.reduce(op) { op, b in
            evaluator.evaluate(b.value).map { op.setting(b.key, to: $0) } ?? op
        }
    }

    var title: String {
        switch op {
        case let .primitive(kind, _, _): return kind.title
        case let .extrude(_, _, mode, _): return mode == .cut ? "Extrude cut" : "Extrude"
        case .revolve: return "Revolve"
        case .pushPull: return "Push/Pull"
        case let .boolean(kind, _, _): return kind.title
        case let .fillet(_, edges, _): return "Fillet (\(edges.count))"
        case let .chamfer(_, edges, _): return "Chamfer (\(edges.count))"
        case .shell: return "Shell"
        case .move: return "Move"
        case .mirror: return "Mirror"
        case .duplicate: return "Duplicate"
        case .delete: return "Delete body"
        case let .imported(name, _): return "Import \(name)"
        case let .sketch(_, name): return name
        case let .plane(_, _, _, name): return name
        case .sweep: return "Sweep"
        case let .loft(sections, _, _): return "Loft (\(sections.count))"
        case .scale: return "Scale"
        case let .linearPattern(_, _, count, _, _): return "Linear pattern (\(count))"
        case let .circularPattern(_, _, _, count, _, _): return "Circular pattern (\(count))"
        case .split: return "Split body"
        case let .deleteFaces(_, faces): return "Delete faces (\(faces.count))"
        case .draft: return "Draft"
        case .offsetFaces: return "Offset faces"
        case let .axis(_, _, name): return name
        case .translate: return "Translate"
        case .rotate: return "Rotate around axis"
        case .align: return "Align"
        case let .image(image): return image.name
        case .replaceFace: return "Replace face"
        case .fill: return "Fill"
        case let .projection(_, _, name): return name
        case let .emboss(_, _, _, deboss): return deboss ? "Deboss" : "Emboss"
        case let .scaleAxes(_, _, _, uniform): return uniform ? "Scale" : "Scale (per axis)"
        case let .scatter(_, _, _, settings): return "Scatter (\(settings.count))"
        case let .terrain(settings): return settings.name
        }
    }

    /// Features that only add reference geometry, not bodies.
    var isReference: Bool {
        switch op {
        case .sketch, .plane, .axis, .image, .projection: return true
        default: return false
        }
    }

    var symbol: String {
        switch op {
        case let .primitive(kind, _, _): return kind.symbol
        case .extrude: return "square.stack.3d.up"
        case .revolve: return "arrow.triangle.2.circlepath"
        case .pushPull: return "arrow.up.and.down.square"
        case let .boolean(kind, _, _): return kind.symbol
        case .fillet: return "circle.bottomhalf.filled"
        case .chamfer: return "triangle.bottomhalf.filled"
        case .shell: return "square.dashed"
        case .move: return "move.3d"
        case .mirror: return "arrow.left.and.right.righttriangle.left.righttriangle.right"
        case .duplicate: return "plus.square.on.square"
        case .delete: return "trash"
        case .imported: return "square.and.arrow.down"
        case .sketch: return "pencil.and.outline"
        case .plane: return "square.on.square.dashed"
        case .sweep: return "point.bottomleft.forward.to.point.topright.scurvepath"
        case .loft: return "cone"
        case .scale: return "arrow.up.left.and.arrow.down.right"
        case .linearPattern: return "square.grid.3x1.below.line.grid.1x2"
        case .circularPattern: return "circle.grid.cross"
        case .split: return "square.split.diagonal"
        case .deleteFaces: return "eraser"
        case .draft: return "trapezoid.and.line.vertical"
        case .offsetFaces: return "arrow.up.square"
        case .axis: return "line.diagonal"
        case .translate: return "arrow.right.square"
        case .rotate: return "arrow.clockwise.circle"
        case .align: return "align.horizontal.center"
        case .image: return "photo"
        case .replaceFace: return "square.2.layers.3d.top.filled"
        case .fill: return "square.fill.on.square"
        case .projection: return "rectangle.portrait.on.rectangle.portrait.angled"
        case .emboss: return "textformat"
        case .scaleAxes: return "arrow.up.left.and.arrow.down.right"
        case .scatter: return "circle.dotted"
        case .terrain: return "mountain.2"
        }
    }
}

/// A pick-one option of an operation, for the inspector.
struct Choice: Identifiable {
    let id: Int
    let label: String
    let options: [String]
    let selected: Int
}

/// World axes offered for directions and plane normals.
let worldAxes: [SIMD3<Double>] = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]

func axisIndex(_ v: SIMD3<Double>) -> Int {
    let a = simd_abs(v)
    return a.x >= a.y && a.x >= a.z ? 0 : (a.y >= a.z ? 1 : 2)
}

/// A plane with the given normal axis through `origin`.
func axisPlane(_ axis: Int, origin: SIMD3<Double>) -> SketchPlane {
    switch axis {
    case 0: return SketchPlane(origin: origin, normal: [1, 0, 0], xDir: [0, 1, 0])
    case 1: return SketchPlane(origin: origin, normal: [0, -1, 0], xDir: [1, 0, 0])
    default: return SketchPlane(origin: origin, normal: [0, 0, 1], xDir: [1, 0, 0])
    }
}

extension SketchPlane {
    /// Turned by `angle` degrees about its X axis, then moved along its normal.
    func transformed(offset: Double, angle: Double) -> SketchPlane {
        let t = angle * .pi / 180
        let y = yDir
        let n = normal * cos(t) - y * sin(t)
        return SketchPlane(origin: origin + n * offset, normal: simd_normalize(n), xDir: xDir)
    }
}

/// A named numeric parameter of an operation, for the inspector.
struct Parameter: Identifiable {
    let id: Int
    let label: String
    let value: Double
    var unit: String = "mm"
}

extension Operation {
    var parameters: [Parameter] {
        switch self {
        case let .primitive(kind, dims, position):
            var params = kind.dimLabels.enumerated().map { Parameter(id: $0.offset, label: $0.element, value: dims[$0.offset]) }
            params += ["X", "Y", "Z"].enumerated().map { Parameter(id: 10 + $0.offset, label: $0.element, value: position[$0.offset]) }
            return params
        case let .extrude(_, distance, _, _):
            return [Parameter(id: 0, label: "Distance", value: distance)]
        case let .revolve(_, angle, _, _, _):
            return [Parameter(id: 0, label: "Angle", value: angle, unit: "°")]
        case let .pushPull(_, _, distance):
            return [Parameter(id: 0, label: "Distance", value: distance)]
        case let .fillet(_, _, radius):
            return [Parameter(id: 0, label: "Radius", value: radius)]
        case let .chamfer(_, _, distance):
            return [Parameter(id: 0, label: "Distance", value: distance)]
        case let .shell(_, _, thickness):
            return [Parameter(id: 0, label: "Thickness", value: thickness)]
        case let .move(_, t, _, angle):
            return [Parameter(id: 0, label: "X", value: t.x), Parameter(id: 1, label: "Y", value: t.y),
                    Parameter(id: 2, label: "Z", value: t.z), Parameter(id: 3, label: "Rotate Z", value: angle, unit: "°")]
        case let .plane(_, offset, angle, _):
            return [Parameter(id: 0, label: "Offset", value: offset), Parameter(id: 1, label: "Angle", value: angle, unit: "°")]
        case let .scale(_, factor, _):
            return [Parameter(id: 0, label: "Factor", value: factor, unit: "×")]
        case let .linearPattern(_, _, count, spacing, _):
            return [Parameter(id: 0, label: "Count", value: Double(count), unit: ""), Parameter(id: 1, label: "Spacing", value: spacing)]
        case let .circularPattern(_, _, _, count, angle, _):
            return [Parameter(id: 0, label: "Count", value: Double(count), unit: ""), Parameter(id: 1, label: "Angle", value: angle, unit: "°")]
        case let .split(_, _, offset):
            return [Parameter(id: 0, label: "Offset", value: offset)]
        case let .draft(_, _, _, angle):
            return [Parameter(id: 0, label: "Angle", value: angle, unit: "°")]
        case let .offsetFaces(_, _, distance):
            return [Parameter(id: 0, label: "Distance", value: distance)]
        case let .translate(_, _, distance):
            return [Parameter(id: 0, label: "Distance", value: distance)]
        case let .rotate(_, _, _, angle):
            return [Parameter(id: 0, label: "Angle", value: angle, unit: "°")]
        case let .emboss(_, _, height, _):
            return [Parameter(id: 0, label: "Height", value: height)]
        case let .scaleAxes(_, f, _, uniform):
            if uniform { return [Parameter(id: 0, label: "Factor", value: f.x, unit: "×")] }
            return ["X", "Y", "Z"].enumerated().map { Parameter(id: $0.offset, label: $0.element, value: f[$0.offset], unit: "×") }
        case let .scatter(_, _, _, p):
            return [Parameter(id: 0, label: "Count", value: Double(p.count), unit: ""),
                    Parameter(id: 1, label: "Seed", value: Double(p.seed), unit: ""),
                    Parameter(id: 2, label: "Min size", value: p.minScale * 100, unit: "%"),
                    Parameter(id: 3, label: "Max size", value: p.maxScale * 100, unit: "%"),
                    Parameter(id: 4, label: "Spacing", value: p.spacing),
                    Parameter(id: 5, label: "Max slope", value: p.maxSlope, unit: "°"),
                    Parameter(id: 6, label: "Sink", value: p.sink * 100, unit: "%")]
        case let .terrain(t):
            var params = [Parameter(id: 0, label: "Width", value: t.width), Parameter(id: 1, label: "Depth", value: t.depth),
                          Parameter(id: 2, label: "Height", value: t.heightScale * 100, unit: "%"),
                          Parameter(id: 3, label: "Base thickness", value: t.floor)]
            if let n = t.noise {
                params += [Parameter(id: 4, label: "Seed", value: Double(n.seed), unit: ""),
                           Parameter(id: 5, label: "Hill size", value: n.scale * 100, unit: "%"),
                           Parameter(id: 6, label: "Roughness", value: n.roughness * 100, unit: "%")]
            }
            params += ["X", "Y", "Z"].enumerated().map { Parameter(id: 10 + $0.offset, label: $0.element, value: t.center[$0.offset]) }
            return params
        case let .image(image):
            return [Parameter(id: 0, label: "Width", value: image.width),
                    Parameter(id: 1, label: "Opacity", value: image.opacity * 100, unit: "%")]
        default:
            return []
        }
    }

    var choices: [Choice] {
        let axisNames = ["X", "Y", "Z"]
        switch self {
        case let .mirror(_, plane, keep):
            return [Choice(id: 0, label: "Plane normal", options: axisNames, selected: axisIndex(plane.normal)),
                    Choice(id: 1, label: "Original", options: ["Keep", "Remove"], selected: keep ? 0 : 1)]
        case let .linearPattern(_, direction, _, _, fuse):
            return [Choice(id: 0, label: "Direction", options: axisNames, selected: axisIndex(direction)),
                    Choice(id: 1, label: "Result", options: ["Separate", "Join"], selected: fuse ? 1 : 0)]
        case let .circularPattern(_, _, axis, _, _, fuse):
            return [Choice(id: 0, label: "Axis", options: axisNames, selected: axisIndex(axis)),
                    Choice(id: 1, label: "Result", options: ["Separate", "Join"], selected: fuse ? 1 : 0)]
        case let .split(_, plane, _):
            return [Choice(id: 0, label: "Plane normal", options: axisNames, selected: axisIndex(plane.normal))]
        case let .draft(_, _, pull, _):
            return [Choice(id: 0, label: "Pull", options: axisNames, selected: axisIndex(pull))]
        case let .translate(_, direction, _):
            return [Choice(id: 0, label: "Direction", options: axisNames, selected: axisIndex(direction))]
        case let .rotate(_, _, axis, _):
            return [Choice(id: 0, label: "Axis", options: axisNames, selected: axisIndex(axis))]
        case let .emboss(_, _, _, deboss):
            return [Choice(id: 0, label: "Type", options: ["Emboss", "Deboss"], selected: deboss ? 1 : 0)]
        case let .scaleAxes(_, _, _, uniform):
            return [Choice(id: 0, label: "Mode", options: ["Uniform", "Per axis"], selected: uniform ? 0 : 1)]
        case let .scatter(_, _, _, p):
            return [Choice(id: 0, label: "Rotation", options: ["Random", "None"], selected: p.randomRotation ? 0 : 1),
                    Choice(id: 1, label: "Orientation", options: ["Upright", "Follow surface"], selected: p.alignToSurface ? 1 : 0),
                    Choice(id: 2, label: "Original", options: ["Keep", "Remove"], selected: p.keepOriginal ? 0 : 1)]
        case let .align(_, _, modes):
            return axisNames.indices.map {
                Choice(id: $0, label: "Align \(axisNames[$0])", options: ["–", "Min", "Center", "Max"], selected: modes[$0])
            }
        default:
            return []
        }
    }

    func setting(choice id: Int, to index: Int) -> Operation {
        switch self {
        case let .scaleAxes(body, f, center, _):
            let uniform = index == 0
            return .scaleAxes(body: body, factors: uniform ? SIMD3(repeating: f.x) : f, center: center, uniform: uniform)
        case .scatter(let source, let target, let avoid, var p):
            switch id {
            case 0: p.randomRotation = index == 0
            case 1: p.alignToSurface = index == 1
            default: p.keepOriginal = index == 0
            }
            return .scatter(source: source, target: target, avoid: avoid, settings: p)
        case let .emboss(sketch, body, height, _):
            return .emboss(sketch: sketch, body: body, height: height, deboss: index == 1)
        case let .mirror(body, plane, keep):
            if id == 0 { return .mirror(body: body, plane: axisPlane(index, origin: plane.origin), keepOriginal: keep) }
            return .mirror(body: body, plane: plane, keepOriginal: index == 0)
        case let .linearPattern(body, direction, count, spacing, fuse):
            return .linearPattern(body: body, direction: id == 0 ? worldAxes[index] : direction, count: count, spacing: spacing,
                                  fuse: id == 1 ? index == 1 : fuse)
        case let .circularPattern(body, origin, axis, count, angle, fuse):
            return .circularPattern(body: body, origin: origin, axis: id == 0 ? worldAxes[index] : axis, count: count, angle: angle,
                                    fuse: id == 1 ? index == 1 : fuse)
        case let .split(body, plane, offset):
            return .split(body: body, plane: axisPlane(index, origin: plane.origin), offset: offset)
        case let .draft(body, faces, _, angle):
            return .draft(body: body, faces: faces, pull: worldAxes[index], angle: angle)
        case let .translate(body, _, distance):
            return .translate(body: body, direction: worldAxes[index], distance: distance)
        case let .rotate(body, origin, _, angle):
            return .rotate(body: body, origin: origin, axis: worldAxes[index], angle: angle)
        case .align(let body, let target, var modes):
            modes[id] = index
            return .align(body: body, target: target, modes: modes)
        default:
            return self
        }
    }

    func setting(_ id: Int, to v: Double) -> Operation {
        switch self {
        case .terrain(var t):
            switch id {
            case 0: t.width = max(v, 1)
            case 1: t.depth = max(v, 1)
            case 2: t.heightScale = max(v / 100, 0)
            case 3: t.floor = max(v, 0.5)
            case 4: t.noise?.seed = max(Int(v.rounded()), 0)
            case 5: t.noise?.scale = min(max(v / 100, 0.02), 4)
            case 6: t.noise?.roughness = min(max(v / 100, 0), 0.95)
            case 10...12: t.center[id - 10] = v
            default: break
            }
            if (0...1).contains(id) || (4...6).contains(id) { t.regenerate() }
            return .terrain(settings: t)
        case .scaleAxes(let body, var f, let center, let uniform):
            let v = max(v, 0.001)
            if uniform { f = SIMD3(repeating: v) } else { f[min(max(id, 0), 2)] = v }
            return .scaleAxes(body: body, factors: f, center: center, uniform: uniform)
        case .scatter(let source, let target, let avoid, var p):
            switch id {
            case 0: p.count = min(max(Int(v.rounded()), 1), 2000)
            case 1: p.seed = max(Int(v.rounded()), 0)
            case 2: p.minScale = max(v / 100, 0.01); p.maxScale = max(p.maxScale, p.minScale)
            case 3: p.maxScale = max(v / 100, 0.01); p.minScale = min(p.minScale, p.maxScale)
            case 4: p.spacing = max(v, 0)
            case 5: p.maxSlope = min(max(v, 0), 90)
            default: p.sink = min(max(v / 100, -1), 1)
            }
            return .scatter(source: source, target: target, avoid: avoid, settings: p)
        case let .emboss(sketch, body, _, deboss):
            return .emboss(sketch: sketch, body: body, height: v, deboss: deboss)
        case .primitive(let kind, var dims, var position):
            if id < 10 { dims[id] = v } else { position[id - 10] = v }
            return .primitive(kind: kind, dims: dims, position: position)
        case let .extrude(sketch, _, mode, target):
            return .extrude(sketch: sketch, distance: v, mode: mode, target: target)
        case let .revolve(sketch, _, aroundV, mode, target):
            return .revolve(sketch: sketch, angle: v, aroundV: aroundV, mode: mode, target: target)
        case let .pushPull(body, face, _):
            return .pushPull(body: body, face: face, distance: v)
        case let .fillet(body, edges, _):
            return .fillet(body: body, edges: edges, radius: v)
        case let .chamfer(body, edges, _):
            return .chamfer(body: body, edges: edges, distance: v)
        case let .shell(body, faces, _):
            return .shell(body: body, faces: faces, thickness: v)
        case .move(let body, var t, let axis, var angle):
            if id < 3 { t[id] = v } else { angle = v }
            return .move(body: body, translation: t, axis: axis, angle: angle)
        case let .plane(base, offset, angle, name):
            return .plane(base: base, offset: id == 0 ? v : offset, angle: id == 1 ? v : angle, name: name)
        case let .scale(body, _, center):
            return .scale(body: body, factor: max(v, 0.001), center: center)
        case let .linearPattern(body, direction, count, spacing, fuse):
            return .linearPattern(body: body, direction: direction, count: id == 0 ? max(Int(v.rounded()), 2) : count,
                                  spacing: id == 1 ? v : spacing, fuse: fuse)
        case let .circularPattern(body, origin, axis, count, angle, fuse):
            return .circularPattern(body: body, origin: origin, axis: axis, count: id == 0 ? max(Int(v.rounded()), 2) : count,
                                    angle: id == 1 ? v : angle, fuse: fuse)
        case let .split(body, plane, _):
            return .split(body: body, plane: plane, offset: v)
        case let .draft(body, faces, pull, _):
            return .draft(body: body, faces: faces, pull: pull, angle: v)
        case let .offsetFaces(body, faces, _):
            return .offsetFaces(body: body, faces: faces, distance: v)
        case let .translate(body, direction, _):
            return .translate(body: body, direction: direction, distance: v)
        case let .rotate(body, origin, axis, _):
            return .rotate(body: body, origin: origin, axis: axis, angle: v)
        case .image(var image):
            if id == 0, v > 0 {
                image.height *= v / image.width
                image.width = v
            } else if id == 1 {
                image.opacity = min(max(v / 100, 0.05), 1)
            }
            return .image(image: image)
        default:
            return self
        }
    }

    /// Combine mode for sketch features, when a target body is available.
    var combineMode: CombineMode? {
        switch self {
        case let .extrude(_, _, mode, _), let .revolve(_, _, _, mode, _), let .sweep(_, _, mode, _), let .loft(_, mode, _): return mode
        default: return nil
        }
    }

    func setting(mode: CombineMode, target: Int?) -> Operation {
        switch self {
        case let .extrude(sketch, distance, _, _):
            return .extrude(sketch: sketch, distance: distance, mode: target == nil ? .newBody : mode, target: target)
        case let .revolve(sketch, angle, aroundV, _, _):
            return .revolve(sketch: sketch, angle: angle, aroundV: aroundV, mode: target == nil ? .newBody : mode, target: target)
        case let .sweep(profile, path, _, _):
            return .sweep(profile: profile, path: path, mode: target == nil ? .newBody : mode, target: target)
        case let .loft(sections, _, _):
            return .loft(sections: sections, mode: target == nil ? .newBody : mode, target: target)
        default:
            return self
        }
    }

    var sketch: Sketch? {
        switch self {
        case let .extrude(sketch, _, _, _), let .revolve(sketch, _, _, _, _), let .sketch(sketch, _): return sketch
        default: return nil
        }
    }

    func setting(sketch: Sketch) -> Operation {
        switch self {
        case let .extrude(_, distance, mode, target):
            return .extrude(sketch: sketch, distance: distance, mode: mode, target: target)
        case let .revolve(_, angle, aroundV, mode, target):
            return .revolve(sketch: sketch, angle: angle, aroundV: aroundV, mode: mode, target: target)
        case let .sketch(_, name):
            return .sketch(sketch: sketch, name: name)
        default:
            return self
        }
    }

    var sketchTarget: Int? {
        switch self {
        case let .extrude(_, _, _, target), let .revolve(_, _, _, _, target), let .sweep(_, _, _, target), let .loft(_, _, target):
            return target
        default: return nil
        }
    }
}
