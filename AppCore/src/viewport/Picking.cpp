#include "Picking.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sculpt::viewport {

std::optional<float> rayTriangle(Vec3f o, Vec3f d, Vec3f a, Vec3f b, Vec3f c) {
  Vec3f e1 = b - a, e2 = c - a;
  Vec3f p = cross(d, e2);
  float det = dot(e1, p);
  if (!(std::fabs(det) > 1e-9f)) return std::nullopt;
  float inv = 1 / det;
  Vec3f s = o - a;
  float u = dot(s, p) * inv;
  if (!(u >= 0 && u <= 1)) return std::nullopt;
  Vec3f q = cross(s, e1);
  float v = dot(d, q) * inv;
  if (!(v >= 0 && u + v <= 1)) return std::nullopt;
  float t = dot(e2, q) * inv;
  if (t > 1e-5f) return t;
  return std::nullopt;
}

bool rayHitsBox(Vec3f o, Vec3f d, Vec3f lo, Vec3f hi) {
  Vec3f pad = (hi - lo) * 0.01f + Vec3f(0.01f, 0.01f, 0.01f);
  // IEEE division gives ±inf for axis-parallel rays, as in Swift.
  Vec3f inv{1 / d.x, 1 / d.y, 1 / d.z};
  Vec3f t1 = (lo - pad - o) * inv, t2 = (hi + pad - o) * inv;
  float tmin = reduceMax(vmin(t1, t2)), tmax = reduceMin(vmax(t1, t2));
  return tmax >= std::max(tmin, 0.0f);
}

std::optional<SurfaceHit> surfaceHit(const MeshMap& meshes, const Camera& camera, Vec2f point, Size2 size) {
  Ray r = camera.ray(point, size);
  Vec3f o = r.origin, d = r.direction;
  std::optional<SurfaceHit> best;
  for (const auto& [id, mesh] : meshes) {
    if (!mesh || !rayHitsBox(o, d, mesh->boundsMin, mesh->boundsMax)) continue;
    const auto& p = mesh->positions;
    const auto& idx = mesh->indices;
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
      uint32_t ia = idx[i], ib = idx[i + 1], ic = idx[i + 2];
      if (ia >= p.size() || ib >= p.size() || ic >= p.size()) continue;
      auto t = rayTriangle(o, d, p[ia], p[ib], p[ic]);
      if (t && *t < (best ? best->t : std::numeric_limits<float>::max())) {
        int face = ia < mesh->vertexFace.size() ? int(mesh->vertexFace[ia]) : 0;
        best = SurfaceHit{Pick::makeFace(mesh->id, face), o + d * *t, *t};
      }
    }
  }
  return best;
}

std::optional<Pick> edgeHit(const MeshMap& meshes, const Camera& camera, Vec2f point, Size2 size, float tolerance,
                            std::optional<float> maxDepth) {
  Vec3f o = camera.ray(point, size).origin;
  Mat4f vp = camera.viewProjection(size);
  auto screen = [&](Vec3f p, Vec3f& out) {
    Vec4f c = vp * Vec4f(p, 1);
    if (!(c.w > 0)) return false;
    out = {(c.x / c.w + 1) / 2 * size.width, (1 - c.y / c.w) / 2 * size.height, c.w};
    return true;
  };
  Vec2f q = point;
  std::optional<Pick> best;
  float bestDist = std::numeric_limits<float>::max();
  float slack = camera.distance * 0.01f;
  for (const auto& [id, mesh] : meshes) {
    if (!mesh) continue;
    // Skip bodies whose on-screen bounds are nowhere near the pointer.
    Vec3f lo = mesh->boundsMin, hi = mesh->boundsMax;
    int visible = 0;
    float minX = 3.4e38f, maxX = -3.4e38f, minY = 3.4e38f, maxY = -3.4e38f;
    for (int k = 0; k < 8; ++k) {
      Vec3f c;
      if (screen({(k & 1) == 0 ? lo.x : hi.x, (k & 2) == 0 ? lo.y : hi.y, (k & 4) == 0 ? lo.z : hi.z}, c)) {
        ++visible;
        minX = std::min(minX, c.x); maxX = std::max(maxX, c.x);
        minY = std::min(minY, c.y); maxY = std::max(maxY, c.y);
      }
    }
    if (visible == 8) {
      if (q.x < minX - tolerance || q.x > maxX + tolerance || q.y < minY - tolerance || q.y > maxY + tolerance) continue;
      // Entirely behind the surface already hit.
      if (maxDepth && distance(vclamp(o, lo, hi), o) > *maxDepth + slack) continue;
    }
    const auto& ep = mesh->edgePositions;
    for (size_t i = 0; i + 1 < ep.size(); i += 2) {
      Vec3f a, b;
      if (!screen(ep[i], a) || !screen(ep[i + 1], b)) continue;
      Vec2f a2{a.x, a.y}, b2{b.x, b.y}, ab = b2 - a2;
      float t = clampf(dot(q - a2, ab) / std::max(lengthSquared(ab), 1e-6f), 0, 1);
      float dist = distance(q, a2 + ab * t);
      if (!(dist <= tolerance) || !(dist < bestDist)) continue;
      // Hidden edges lie behind the surface hit along the same ray.
      if (maxDepth) {
        Vec3f world = ep[i] + (ep[i + 1] - ep[i]) * t;
        if (distance(world, o) > *maxDepth + slack) continue;
      }
      int edge = i < mesh->edgeIds.size() ? int(mesh->edgeIds[i]) : 0;
      best = Pick::makeEdge(mesh->id, edge);
      bestDist = dist;
    }
  }
  return best;
}

std::optional<Pick> pick(const MeshMap& meshes, const Camera& camera, Vec2f point, Size2 size, float tolerance) {
  auto surface = surfaceHit(meshes, camera, point, size);
  std::optional<float> depth;
  if (surface) depth = surface->t;
  if (auto edge = edgeHit(meshes, camera, point, size, tolerance, depth)) return edge;
  if (surface) return surface->pick;
  return std::nullopt;
}

}  // namespace sculpt::viewport
