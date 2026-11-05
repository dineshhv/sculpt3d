// Small self-contained vector math for the viewport (no GL, no model types).
//
// Mat4f is column-major (m[col * 4 + row]) so it can be handed straight to
// glUniformMatrix4fv(..., GL_FALSE, m.data()). Matrices built by Camera follow
// the Swift/Metal convention: clip-space z in [0, w]; the shaders remap to
// OpenGL's [-w, w] (see RENDERER.md).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sculpt::viewport {

constexpr float kPi = 3.14159265358979323846f;

struct Vec2f {
  float x = 0, y = 0;
  constexpr Vec2f() = default;
  constexpr Vec2f(float x_, float y_) : x(x_), y(y_) {}
  constexpr Vec2f operator+(Vec2f o) const { return {x + o.x, y + o.y}; }
  constexpr Vec2f operator-(Vec2f o) const { return {x - o.x, y - o.y}; }
  constexpr Vec2f operator*(float s) const { return {x * s, y * s}; }
  constexpr Vec2f operator/(float s) const { return {x / s, y / s}; }
  constexpr Vec2f operator-() const { return {-x, -y}; }
  constexpr bool operator==(Vec2f o) const { return x == o.x && y == o.y; }
};

struct Vec3f {
  float x = 0, y = 0, z = 0;
  constexpr Vec3f() = default;
  constexpr Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  constexpr Vec3f operator+(Vec3f o) const { return {x + o.x, y + o.y, z + o.z}; }
  constexpr Vec3f operator-(Vec3f o) const { return {x - o.x, y - o.y, z - o.z}; }
  constexpr Vec3f operator*(float s) const { return {x * s, y * s, z * s}; }
  constexpr Vec3f operator*(Vec3f o) const { return {x * o.x, y * o.y, z * o.z}; }
  constexpr Vec3f operator/(float s) const { return {x / s, y / s, z / s}; }
  constexpr Vec3f operator-() const { return {-x, -y, -z}; }
  Vec3f& operator+=(Vec3f o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3f& operator-=(Vec3f o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vec3f& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
  constexpr bool operator==(Vec3f o) const { return x == o.x && y == o.y && z == o.z; }
  constexpr bool operator!=(Vec3f o) const { return !(*this == o); }
  float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
  float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};

struct Vec4f {
  float x = 0, y = 0, z = 0, w = 0;
  constexpr Vec4f() = default;
  constexpr Vec4f(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
  constexpr Vec4f(Vec3f v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
  constexpr Vec3f xyz() const { return {x, y, z}; }
  constexpr Vec4f operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
  /// Component-wise, like SIMD4 * SIMD4 in Swift.
  constexpr Vec4f operator*(Vec4f o) const { return {x * o.x, y * o.y, z * o.z, w * o.w}; }
  constexpr bool operator==(Vec4f o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
};

constexpr Vec3f operator*(float s, Vec3f v) { return v * s; }

inline float dot(Vec2f a, Vec2f b) { return a.x * b.x + a.y * b.y; }
inline float dot(Vec3f a, Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float dot(Vec4f a, Vec4f b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Vec3f cross(Vec3f a, Vec3f b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec2f v) { return std::sqrt(dot(v, v)); }
inline float length(Vec3f v) { return std::sqrt(dot(v, v)); }
inline float lengthSquared(Vec2f v) { return dot(v, v); }
inline float lengthSquared(Vec3f v) { return dot(v, v); }
inline float distance(Vec2f a, Vec2f b) { return length(a - b); }
inline float distance(Vec3f a, Vec3f b) { return length(a - b); }
inline Vec2f normalize(Vec2f v) { float l = length(v); return l > 0 ? v / l : v; }
inline Vec3f normalize(Vec3f v) { float l = length(v); return l > 0 ? v / l : v; }
inline Vec3f vmin(Vec3f a, Vec3f b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3f vmax(Vec3f a, Vec3f b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline Vec3f vabs(Vec3f a) { return {std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)}; }
inline Vec3f vclamp(Vec3f v, Vec3f lo, Vec3f hi) { return vmin(vmax(v, lo), hi); }
inline float reduceMax(Vec3f v) { return std::max(v.x, std::max(v.y, v.z)); }
inline float reduceMin(Vec3f v) { return std::min(v.x, std::min(v.y, v.z)); }
inline float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }
inline bool isFinite(Vec3f v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

/// 4×4 float matrix, column-major storage.
struct Mat4f {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  static Mat4f identity() { return Mat4f(); }

  /// Builds from rows, matching `simd_float4x4(rows:)` in Swift.
  static Mat4f fromRows(Vec4f r0, Vec4f r1, Vec4f r2, Vec4f r3) {
    Mat4f a;
    const Vec4f rows[4] = {r0, r1, r2, r3};
    for (int r = 0; r < 4; ++r) {
      a.m[0 * 4 + r] = rows[r].x;
      a.m[1 * 4 + r] = rows[r].y;
      a.m[2 * 4 + r] = rows[r].z;
      a.m[3 * 4 + r] = rows[r].w;
    }
    return a;
  }

  float operator()(int row, int col) const { return m[col * 4 + row]; }
  float& operator()(int row, int col) { return m[col * 4 + row]; }
  const float* data() const { return m; }

  Mat4f operator*(const Mat4f& b) const {
    Mat4f c;
    for (int col = 0; col < 4; ++col) {
      for (int row = 0; row < 4; ++row) {
        float s = 0;
        for (int k = 0; k < 4; ++k) s += (*this)(row, k) * b(k, col);
        c(row, col) = s;
      }
    }
    return c;
  }

  Vec4f operator*(Vec4f v) const {
    return {m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
            m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w};
  }

  /// General inverse (cofactors); returns identity when singular.
  Mat4f inverse() const {
    const float* a = m;
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    double det = double(a[0]) * inv[0] + double(a[1]) * inv[4] + double(a[2]) * inv[8] + double(a[3]) * inv[12];
    Mat4f out;
    if (det == 0 || !std::isfinite(det)) return out;
    float d = float(1.0 / det);
    for (int i = 0; i < 16; ++i) out.m[i] = inv[i] * d;
    return out;
  }
};

}  // namespace sculpt::viewport
