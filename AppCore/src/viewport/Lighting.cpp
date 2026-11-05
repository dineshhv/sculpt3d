#include "Lighting.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace sculpt::viewport {

void backgroundColors(const EnvironmentSettings& env, Vec3f& bottom, Vec3f& top) {
  Vec3f bg = vclamp(env.background, {0, 0, 0}, {1, 1, 1});
  if (!env.gradient) {
    bottom = top = bg;
    return;
  }
  bottom = bg * 0.94f;
  top = vmin(bg * 1.08f + Vec3f(0.03f, 0.03f, 0.03f), {1, 1, 1});
}

Vec3f keyLight(const Camera& camera, float azimuthDegrees, float elevationDegrees) {
  Vec3f f = camera.forward();
  Vec3f back{-f.x, -f.y, 0};
  if (length(back) < 1e-3f) {
    Vec3f u = camera.up();
    back = {-u.x, -u.y, 0};
  }
  back = normalize(back);
  Vec3f side = normalize(Vec3f(back.y, -back.x, 0));  // viewer's right
  float a = azimuthDegrees * kPi / 180;
  float elevation = std::min(std::max(elevationDegrees, 5.0f), 89.0f) * kPi / 180;
  Vec3f horizontal = back * std::cos(a) + side * std::sin(a);
  return normalize(horizontal * std::cos(elevation) + Vec3f(0, 0, std::sin(elevation)));
}

Mat4f lightMatrix(Vec3f key, Vec3f center, float radius) {
  Vec3f f = -normalize(key);
  Vec3f eye = center - f * radius * 2;
  Vec3f upRef = std::fabs(f.z) > 0.99f ? Vec3f(0, 1, 0) : Vec3f(0, 0, 1);
  Vec3f r = normalize(cross(f, upRef)), u = cross(r, f);
  Mat4f view = Mat4f::fromRows({r.x, r.y, r.z, -dot(r, eye)}, {u.x, u.y, u.z, -dot(u, eye)},
                               {-f.x, -f.y, -f.z, dot(f, eye)}, {0, 0, 0, 1});
  // View-space z runs from -radius (nearest) to -3·radius (farthest).
  float near = radius * 0.9f, far = radius * 3.1f;
  Mat4f proj = Mat4f::fromRows({1 / radius, 0, 0, 0}, {0, 1 / radius, 0, 0},
                               {0, 0, -1 / (far - near), -near / (far - near)}, {0, 0, 0, 1});
  return proj * view;
}

namespace {
struct Key3 {
  int64_t x, y, z;
  bool operator==(const Key3& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct Key3Hash {
  size_t operator()(const Key3& k) const {
    uint64_t h = uint64_t(k.x) * 0x9E3779B97F4A7C15ull;
    h ^= uint64_t(k.y) * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
    h ^= uint64_t(k.z) * 0x165667B19E3779F9ull + (h << 6) + (h >> 2);
    return size_t(h);
  }
};
}  // namespace

std::vector<Vec3f> smoothNormals(const std::vector<Vec3f>& positions, const std::vector<Vec3f>& normals,
                                 float limitDegrees) {
  if (positions.size() != normals.size() || positions.empty()) return normals;
  Vec3f lo{3.4e38f, 3.4e38f, 3.4e38f}, hi{-3.4e38f, -3.4e38f, -3.4e38f};
  for (const Vec3f& p : positions) { lo = vmin(lo, p); hi = vmax(hi, p); }
  float q = std::max(length(hi - lo), 1e-3f) * 2e-5f;
  std::unordered_map<Key3, std::vector<int32_t>, Key3Hash> groups;
  groups.reserve(positions.size() / 2);
  for (size_t i = 0; i < positions.size(); ++i) {
    const Vec3f& p = positions[i];
    Key3 k{int64_t(std::round(p.x / q)), int64_t(std::round(p.y / q)), int64_t(std::round(p.z / q))};
    groups[k].push_back(int32_t(i));
  }
  float cosLimit = std::cos(limitDegrees * kPi / 180);
  std::vector<Vec3f> out = normals;
  for (const auto& entry : groups) {
    const auto& members = entry.second;
    if (members.size() < 2) continue;
    for (int32_t i : members) {
      Vec3f ni = normals[size_t(i)];
      Vec3f sum{0, 0, 0};
      for (int32_t j : members) {
        if (dot(ni, normals[size_t(j)]) > cosLimit) sum += normals[size_t(j)];
      }
      float l = length(sum);
      if (l > 1e-6f) out[size_t(i)] = sum / l;
    }
  }
  return out;
}

Vec4f tileTransform(int x0, int y0, int cw, int ch, int w, int h) {
  float nx0 = float(x0) / float(w) * 2 - 1, nx1 = float(x0 + cw) / float(w) * 2 - 1;
  float ny1 = 1 - float(y0) / float(h) * 2, ny0 = 1 - float(y0 + ch) / float(h) * 2;
  float sx = 2 / (nx1 - nx0), sy = 2 / (ny1 - ny0);
  return {sx, sy, -(nx1 + nx0) / (nx1 - nx0), -(ny1 + ny0) / (ny1 - ny0)};
}

Mat4f tileMatrix(Vec4f t) {
  return Mat4f::fromRows({t.x, 0, 0, t.z}, {0, t.y, 0, t.w}, {0, 0, 1, 0}, {0, 0, 0, 1});
}

}  // namespace sculpt::viewport
