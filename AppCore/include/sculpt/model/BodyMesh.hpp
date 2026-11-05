// Port of BodyMesh.swift: the CPU copy of a body's tessellation, and picks.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "o3d/Kernel.hpp"
#include "sculpt/model/Math.hpp"

namespace sculpt {

/// CPU copy of a body's tessellation, used for rendering and picking.
/// Meshes are immutable once built and shared between rebuilds
/// (ModelStore::meshes() hands out shared pointers), like Swift's value copies.
struct BodyMesh {
  int id = -1;
  std::vector<Vec3f> positions;
  std::vector<Vec3f> normals;
  std::vector<uint32_t> vertexFace;
  std::vector<uint32_t> indices;
  std::vector<Vec3f> edgePositions;
  std::vector<uint32_t> edgeIds;
  int faceCount = 0;
  int edgeCount = 0;
  Vec3f boundsMin, boundsMax;
  /// The kernel shape this mesh was made from (0 unknown); equal keys mean equal geometry.
  uint64_t shapeKey = 0;

  BodyMesh() = default;
  BodyMesh(int id, const o3d::Mesh& mesh);

  Vec3d center() const { return toD((boundsMin + boundsMax) / 2.0f); }
  /// Normal of the tessellation vertex of `face` nearest `point`
  /// (ViewportController+Points.swift's fallback for curved faces).
  bool nearestFaceNormal(int face, const Vec3f& point, Vec3d& normal) const;
};

/// A selected body, or one of its faces or edges.
struct Pick {
  enum class Kind { Body, Face, Edge };
  Kind kind = Kind::Body;
  int body = 0;
  int index = 0;  // face or edge index; unused for bodies

  static Pick bodyPick(int b) { return {Kind::Body, b, 0}; }
  static Pick face(int b, int i) { return {Kind::Face, b, i}; }
  static Pick edge(int b, int i) { return {Kind::Edge, b, i}; }

  friend bool operator==(const Pick& a, const Pick& b) {
    return a.kind == b.kind && a.body == b.body && (a.kind == Kind::Body || a.index == b.index);
  }
  friend bool operator!=(const Pick& a, const Pick& b) { return !(a == b); }
  friend bool operator<(const Pick& a, const Pick& b) {
    if (a.kind != b.kind) return a.kind < b.kind;
    if (a.body != b.body) return a.body < b.body;
    return a.kind != Kind::Body && a.index < b.index;
  }
};

}  // namespace sculpt
