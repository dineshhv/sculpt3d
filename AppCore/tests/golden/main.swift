// Writes AppCore/tests/fixtures/golden.sculpt3d with the Apple app's own
// encoder (Sculpt3DFile.encoded()), so the C++ port can be checked byte for
// byte. Run with AppCore/tests/golden/make_golden.sh.
import Foundation
import simd

func formatted(_ value: Double) -> String { value.formatted(.number.precision(.fractionLength(0...3))) }

func uuid(_ n: Int) -> UUID { UUID(uuidString: String(format: "00000000-0000-4000-8000-%012X", n))! }

@MainActor func make() -> Data {
    var sk = Sketch(plane: .xy)
    sk.addRectangle([0, 0], [20, 10], start: nil, end: nil)
    let c = sk.addPoint([30, 5])
    sk.addCircle(center: c, radius: 4)
    let a0 = sk.addPoint([50, 0]), a1 = sk.addPoint([60, 10]), ac = sk.addPoint([60, 0])
    sk.addArc(center: ac, start: a0, end: a1)
    let s0 = sk.addPoint([0, 30]), s1 = sk.addPoint([10, 40]), s2 = sk.addPoint([20, 30])
    sk.splines.append(SketchSpline(points: [s0, s1, s2], closed: false, construction: false, controlPoints: true))
    sk.ellipses.append(SketchEllipse(center: sk.addPoint([80, 0]), majorRadius: 10, minorRadius: 5, rotation: 0.25))
    sk.markers.append(c)
    sk.constraints.append(SketchConstraint(kind: .radius, a: 0, value: 4))
    sk.constraints.append(SketchConstraint(kind: .symmetric, a: s0, b: s2, c: 0))
    sk.constraints.append(SketchConstraint(kind: .fixed, a: c, value: 30, value2: 5))
    sk.lines[0].construction = false
    var front = Sketch(plane: .xz)
    front.addCircle(center: 0, radius: 3)
    let yz = SketchPlane.yz
    var terrain = TerrainSettings(name: "Hills")
    terrain.nx = 3; terrain.ny = 2
    terrain.heights = [0, 1.5, 2.25, 3, 0.125, 10]
    terrain.noise = TerrainNoise(seed: 7, scale: 0.45, octaves: 4, roughness: 0.5)
    var scatter = ScatterSettings()
    scatter.count = 20; scatter.spacing = 4.5
    let image = ReferenceImage(name: "Ref", png: Data([0x89, 0x50, 0x4E, 0x47, 1, 2, 3, 250]), plane: .xy, width: 100, height: 50)
    let decal = ReferenceImage(name: "Decal", png: Data([1, 2]), plane: yz, width: 10, height: 5, opacity: 1, decal: true)
    let ops: [Operation] = [
        .primitive(kind: .box, dims: [40, 40, 40], position: [0, 0, 0]),
        .primitive(kind: .cylinder, dims: [20, 40, 0], position: [10.5, -3.25, 0.1]),
        .extrude(sketch: sk, distance: 20, mode: .newBody, target: nil),
        .extrude(sketch: sk, distance: 5, mode: .cut, target: 1),
        .revolve(sketch: front, angle: 360, aroundV: true, mode: .join, target: 2),
        .pushPull(body: 1, face: 3, distance: 10),
        .boolean(kind: .subtract, target: 1, tool: 2),
        .fillet(body: 1, edges: [0, 4, 7], radius: 3),
        .chamfer(body: 1, edges: [2], distance: 2),
        .shell(body: 1, faces: [], thickness: 2),
        .move(body: 1, translation: [1, 2, 3], axis: [0, 0, 1], angle: 45),
        .mirror(body: 1, plane: axisPlane(0, origin: [5, 0, 0]), keepOriginal: true),
        .duplicate(body: 1),
        .delete(body: 3),
        .imported(name: "Part \"A\"/1", brep: "DBRep_DrawableShape\n\nCASCADE Topology V1\n"),
        .sketch(sketch: sk, name: "Sketch 1"),
        .plane(base: .xy, offset: 10, angle: 45, name: "Plane 1"),
        .sweep(profile: uuid(16), path: uuid(15), mode: .newBody, target: nil),
        .loft(sections: [uuid(15), uuid(16)], mode: .join, target: 1),
        .scale(body: 1, factor: 1.5, center: [0, 0, 0]),
        .linearPattern(body: 1, direction: [1, 0, 0], count: 3, spacing: 60, fuse: false),
        .circularPattern(body: 1, origin: [0, 0, 0], axis: [0, 0, 1], count: 6, angle: 360, fuse: true),
        .split(body: 1, plane: axisPlane(2, origin: [0, 0, 20]), offset: 0),
        .deleteFaces(body: 1, faces: [1, 2]),
        .draft(body: 1, faces: [0], pull: [0, 0, 1], angle: 5),
        .offsetFaces(body: 1, faces: [4], distance: 2),
        .axis(origin: [0, 0, 0], direction: [0, 0, 1], name: "Axis 1"),
        .translate(body: 1, direction: [0, 1, 0], distance: 15),
        .rotate(body: 1, origin: [20, 20, 0], axis: [0, 0, 1], angle: 90),
        .align(body: 2, target: 1, modes: [2, 2, 1]),
        .image(image: image),
        .image(image: decal),
        .replaceFace(body: 1, face: 0, targetBody: 2, targetFace: 5),
        .fill(body: 1, edges: [1, 2, 3]),
        .projection(body: 1, plane: .xy, name: "Projection 1"),
        .emboss(sketch: uuid(16), body: 1, height: 1, deboss: true),
        .scaleAxes(body: 1, factors: [2, 1, 0.5], center: [0.1, 0.2, 0.3], uniform: false),
        .scatter(source: 1, target: 2, avoid: [3], settings: scatter),
        .terrain(settings: terrain),
    ]
    var features = ops.enumerated().map { Feature(id: uuid($0.offset + 1), op: $0.element) }
    features[1].suppressed = true
    features[1].bindings = [0: "width * 2", 11: "h", 2: "sqrt(16)"]
    var settings = ProjectSettings()
    settings.units = .inch
    settings.snapping = false
    settings.bodyNames = [1: "Base", 10: "Ten", 2: "Second"]
    settings.appearance = [1: Appearance(color: [0.85, 0.3, 0.28], metallic: 1, roughness: 0.25, opacity: 0.35, preset: "metal.polished.iron"),
                           2: Appearance()]
    settings.hiddenBodies = [2]
    settings.hiddenItems = [uuid(16)]
    settings.environment.lightAzimuth = 12.5
    settings.environment.background = [0.12, 0.13, 0.15]
    settings.camera = CameraSettings(orthographic: true, fieldOfView: 55)
    settings.snap.guideLines = false
    settings.variables = [Variable(id: uuid(100), name: "width", expression: "30"), Variable(id: uuid(101), name: "h", expression: "width / 2 + 5")]
    return try! Sculpt3DFile(features: features, settings: settings).encoded()
}

let out = CommandLine.arguments[1]
let data = MainActor.assumeIsolated { make() }
try! data.write(to: URL(fileURLWithPath: out))
// Also an empty document.
try! Sculpt3DFile().encoded().write(to: URL(fileURLWithPath: out.replacingOccurrences(of: "golden", with: "empty")))
print("wrote \(out)")
