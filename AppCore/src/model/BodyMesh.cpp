// Port of BodyMesh.swift.
#include "sculpt/model/BodyMesh.hpp"

#include <limits>

namespace sculpt {

static std::vector<Vec3f> float3s(const std::vector<float>& flat) {
  std::vector<Vec3f> out;
  out.reserve(flat.size() / 3);
  for (size_t i = 0; i + 2 < flat.size(); i += 3) out.push_back({flat[i], flat[i + 1], flat[i + 2]});
  return out;
}

BodyMesh::BodyMesh(int id_, const o3d::Mesh& mesh)
    : id(id_),
      positions(float3s(mesh.positions)),
      normals(float3s(mesh.normals)),
      vertexFace(mesh.vertexFace),
      indices(mesh.indices),
      edgePositions(float3s(mesh.edgePositions)),
      edgeIds(mesh.edgeIds),
      faceCount(int(mesh.faceCount)),
      edgeCount(int(mesh.edgeCount)) {
  const float big = std::numeric_limits<float>::max();
  boundsMin = {big, big, big};
  boundsMax = {-big, -big, -big};
  for (auto& p : positions) {
    boundsMin = vmin(boundsMin, p);
    boundsMax = vmax(boundsMax, p);
  }
  for (auto& p : edgePositions) {
    boundsMin = vmin(boundsMin, p);
    boundsMax = vmax(boundsMax, p);
  }
}

bool BodyMesh::nearestFaceNormal(int face, const Vec3f& point, Vec3d& normal) const {
  float best = std::numeric_limits<float>::max();
  bool found = false;
  for (size_t k = 0; k < positions.size() && k < vertexFace.size() && k < normals.size(); ++k) {
    if (vertexFace[k] != uint32_t(face)) continue;
    float d = distanceSquared(positions[k], point);
    if (d < best) {
      best = d;
      normal = toD(normals[k]);
      found = true;
    }
  }
  return found;
}

}  // namespace sculpt
