// CPU picking over body meshes and a camera, ported from the bottom of
// App/Sources/Viewport/ViewportController.swift. Pure functions: no GL, no
// document store.
#pragma once

#include <optional>

#include "Camera.hpp"
#include "Scene.hpp"

namespace sculpt::viewport {

/// Möller–Trumbore; distance along `d` to the triangle, if hit in front (t > 1e-5).
std::optional<float> rayTriangle(Vec3f o, Vec3f d, Vec3f a, Vec3f b, Vec3f c);

/// Whether the ray hits the (slightly padded) box in front of its origin.
bool rayHitsBox(Vec3f o, Vec3f d, Vec3f lo, Vec3f hi);

struct SurfaceHit {
  Pick pick;     // always a Face pick
  Vec3f point;   // world hit point
  float t = 0;   // distance along the ray
};

/// Nearest face under a view point.
std::optional<SurfaceHit> surfaceHit(const MeshMap& meshes, const Camera& camera, Vec2f point, Size2 size);

/// Nearest edge within `tolerance` points of the pointer. With `maxDepth`
/// (the surface hit distance), edges behind that surface are ignored.
std::optional<Pick> edgeHit(const MeshMap& meshes, const Camera& camera, Vec2f point, Size2 size, float tolerance,
                            std::optional<float> maxDepth);

/// Default edge tolerance in points: 14 on touch screens, 6 with a pointer.
inline float defaultEdgeTolerance(bool touch) { return touch ? 14.0f : 6.0f; }

/// Edge if one is near the pointer and not hidden, else the face under it.
std::optional<Pick> pick(const MeshMap& meshes, const Camera& camera, Vec2f point, Size2 size, float tolerance);

}  // namespace sculpt::viewport
