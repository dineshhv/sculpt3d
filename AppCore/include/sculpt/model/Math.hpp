// Small value types standing in for Swift's simd vectors (SIMD2<Double>,
// SIMD3<Double>, SIMD3<Float>) in the shared model.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>

#include "o3d/Kernel.hpp"

namespace sculpt {

constexpr double kPi = 3.14159265358979323846;

template <class T>
struct Vec2 {
  T x = 0, y = 0;
  constexpr Vec2() = default;
  constexpr Vec2(T x_, T y_) : x(x_), y(y_) {}
  T& operator[](int i) { return i == 0 ? x : y; }
  T operator[](int i) const { return i == 0 ? x : y; }
  Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
  Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
  Vec2& operator*=(T s) { x *= s; y *= s; return *this; }
  Vec2& operator/=(T s) { x /= s; y /= s; return *this; }
  friend Vec2 operator+(Vec2 a, const Vec2& b) { return a += b; }
  friend Vec2 operator-(Vec2 a, const Vec2& b) { return a -= b; }
  friend Vec2 operator-(const Vec2& a) { return {-a.x, -a.y}; }
  friend Vec2 operator*(Vec2 a, T s) { return a *= s; }
  friend Vec2 operator*(T s, Vec2 a) { return a *= s; }
  friend Vec2 operator*(const Vec2& a, const Vec2& b) { return {a.x * b.x, a.y * b.y}; }
  friend Vec2 operator/(Vec2 a, T s) { return a /= s; }
  friend Vec2 operator/(const Vec2& a, const Vec2& b) { return {a.x / b.x, a.y / b.y}; }
  friend bool operator==(const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }
  friend bool operator!=(const Vec2& a, const Vec2& b) { return !(a == b); }
};

template <class T>
struct Vec3 {
  T x = 0, y = 0, z = 0;
  constexpr Vec3() = default;
  constexpr Vec3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}
  T& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
  T operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vec3& operator*=(T s) { x *= s; y *= s; z *= s; return *this; }
  Vec3& operator/=(T s) { x /= s; y /= s; z /= s; return *this; }
  friend Vec3 operator+(Vec3 a, const Vec3& b) { return a += b; }
  friend Vec3 operator-(Vec3 a, const Vec3& b) { return a -= b; }
  friend Vec3 operator-(const Vec3& a) { return {-a.x, -a.y, -a.z}; }
  friend Vec3 operator*(Vec3 a, T s) { return a *= s; }
  friend Vec3 operator*(T s, Vec3 a) { return a *= s; }
  friend Vec3 operator*(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
  friend Vec3 operator/(Vec3 a, T s) { return a /= s; }
  friend bool operator==(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
  friend bool operator!=(const Vec3& a, const Vec3& b) { return !(a == b); }
};

using Vec2d = Vec2<double>;
using Vec3d = Vec3<double>;
using Vec3f = Vec3<float>;
using Vec2i = Vec2<int>;

template <class T> T dot(const Vec2<T>& a, const Vec2<T>& b) { return a.x * b.x + a.y * b.y; }
template <class T> T dot(const Vec3<T>& a, const Vec3<T>& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
template <class T> Vec3<T> cross(const Vec3<T>& a, const Vec3<T>& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
template <class T> T lengthSquared(const Vec2<T>& a) { return dot(a, a); }
template <class T> T lengthSquared(const Vec3<T>& a) { return dot(a, a); }
template <class T> T length(const Vec2<T>& a) { return std::sqrt(dot(a, a)); }
template <class T> T length(const Vec3<T>& a) { return std::sqrt(dot(a, a)); }
template <class T> T distance(const Vec2<T>& a, const Vec2<T>& b) { return length(a - b); }
template <class T> T distance(const Vec3<T>& a, const Vec3<T>& b) { return length(a - b); }
template <class T> T distanceSquared(const Vec3<T>& a, const Vec3<T>& b) { return lengthSquared(a - b); }
// simd_normalize: no guard, a zero vector gives NaNs as in Swift.
template <class T> Vec2<T> normalize(const Vec2<T>& a) { return a / length(a); }
template <class T> Vec3<T> normalize(const Vec3<T>& a) { return a / length(a); }
template <class T> Vec2<T> vmin(const Vec2<T>& a, const Vec2<T>& b) { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
template <class T> Vec2<T> vmax(const Vec2<T>& a, const Vec2<T>& b) { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }
template <class T> Vec3<T> vmin(const Vec3<T>& a, const Vec3<T>& b) {
  return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
template <class T> Vec3<T> vmax(const Vec3<T>& a, const Vec3<T>& b) {
  return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}
template <class T> Vec3<T> vabs(const Vec3<T>& a) { return {std::abs(a.x), std::abs(a.y), std::abs(a.z)}; }
template <class T> T clamp(T v, T lo, T hi) { return std::min(std::max(v, lo), hi); }

inline Vec3d toD(const Vec3f& v) { return {double(v.x), double(v.y), double(v.z)}; }
inline Vec3f toF(const Vec3d& v) { return {float(v.x), float(v.y), float(v.z)}; }
inline o3d::Vec3 toO3d(const Vec3d& v) { return {v.x, v.y, v.z}; }
inline Vec3d fromO3d(const o3d::Vec3& v) { return {v.x, v.y, v.z}; }
inline std::array<double, 2> toArr(const Vec2d& v) { return {v.x, v.y}; }
inline Vec2d fromArr(const std::array<double, 2>& a) { return {a[0], a[1]}; }

}  // namespace sculpt

namespace std {
template <class T>
struct hash<sculpt::Vec2<T>> {
  size_t operator()(const sculpt::Vec2<T>& v) const noexcept {
    size_t h = std::hash<T>()(v.x);
    return h ^ (std::hash<T>()(v.y) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
  }
};
}  // namespace std
