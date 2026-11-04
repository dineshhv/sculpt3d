import Foundation
import simd

/// CPU copy of a body's tessellation, used for rendering and picking.
struct BodyMesh {
    let id: Int
    let positions: [SIMD3<Float>]
    let normals: [SIMD3<Float>]
    let vertexFace: [UInt32]
    let indices: [UInt32]
    let edgePositions: [SIMD3<Float>]
    let edgeIds: [UInt32]
    let faceCount: Int
    let edgeCount: Int
    let boundsMin: SIMD3<Float>
    let boundsMax: SIMD3<Float>
    /// The kernel shape this mesh was made from (0 unknown); equal keys mean equal geometry.
    var shapeKey: UInt64 = 0

    init(id: Int, mesh: O3DMesh) {
        self.id = id
        positions = Self.float3s(mesh.positions)
        normals = Self.float3s(mesh.normals)
        vertexFace = Self.array(mesh.vertexFace)
        indices = Self.array(mesh.indices)
        edgePositions = Self.float3s(mesh.edgePositions)
        edgeIds = Self.array(mesh.edgeIds)
        faceCount = mesh.faceCount
        edgeCount = mesh.edgeCount
        let all = positions + edgePositions
        boundsMin = all.reduce(SIMD3(repeating: .greatestFiniteMagnitude)) { simd_min($0, $1) }
        boundsMax = all.reduce(SIMD3(repeating: -.greatestFiniteMagnitude)) { simd_max($0, $1) }
    }

    private static func array<T>(_ data: Data) -> [T] {
        data.withUnsafeBytes { Array($0.bindMemory(to: T.self)) }
    }

    private static func float3s(_ data: Data) -> [SIMD3<Float>] {
        let flat: [Float] = array(data)
        return stride(from: 0, to: flat.count - 2, by: 3).map { SIMD3(flat[$0], flat[$0 + 1], flat[$0 + 2]) }
    }
}

enum Pick: Hashable {
    case body(Int)
    case face(body: Int, index: Int)
    case edge(body: Int, index: Int)

    var body: Int {
        switch self {
        case let .body(b), let .face(b, _), let .edge(b, _): return b
        }
    }
}
