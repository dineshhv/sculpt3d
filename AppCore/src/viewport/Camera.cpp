#include "Camera.hpp"

#include <algorithm>
#include <cmath>

namespace sculpt::viewport {

namespace {
constexpr float kPitchLimit = 1.55f;
float clampFov(float degrees) { return std::min(std::max(degrees, 5.0f), 120.0f); }
}  // namespace

CameraSettings Camera::settings() const { return {orthographic, fovY * 180 / kPi}; }

void Camera::setSettings(const CameraSettings& s) {
  orthographic = s.orthographic;
  fovY = clampFov(s.fieldOfView) * kPi / 180;
}

bool Camera::matches(const CameraSettings& s) const {
  return orthographic == s.orthographic && std::fabs(fovY * 180 / kPi - clampFov(s.fieldOfView)) < 0.01f;
}

float Camera::halfHeight() const { return distance * std::tan(fovY / 2); }

Vec3f Camera::forward() const {
  return -Vec3f(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch));
}

Vec3f Camera::eye() const { return target - forward() * distance; }

Vec3f Camera::right() const { return normalize(cross(forward(), {0, 0, 1})); }

Vec3f Camera::up() const { return cross(right(), forward()); }

Mat4f Camera::viewMatrix() const {
  Vec3f f = forward(), r = right(), u = up(), e = eye();
  return Mat4f::fromRows({r.x, r.y, r.z, -dot(r, e)}, {u.x, u.y, u.z, -dot(u, e)},
                         {-f.x, -f.y, -f.z, dot(f, e)}, {0, 0, 0, 1});
}

Mat4f Camera::projection(float aspect) const {
  if (orthographic) {
    // Depth covers geometry behind the eye too, since zooming an
    // orthographic view moves the eye without changing what is seen.
    float near = -(distance * 20 + 2000), far = distance * 50 + 2000;
    float y = 1 / halfHeight(), x = y / aspect;
    float z = -1 / (far - near);
    return Mat4f::fromRows({x, 0, 0, 0}, {0, y, 0, 0}, {0, 0, z, -near / (far - near)}, {0, 0, 0, 1});
  }
  float near = std::max(distance * 0.005f, 0.01f), far = distance * 50 + 2000;
  float y = 1 / std::tan(fovY / 2), x = y / aspect;
  float z = far / (near - far);
  return Mat4f::fromRows({x, 0, 0, 0}, {0, y, 0, 0}, {0, 0, z, z * near}, {0, 0, -1, 0});
}

Mat4f Camera::viewProjection(Size2 size) const {
  float aspect = std::max(size.width, 1.0f) / std::max(size.height, 1.0f);
  return projection(aspect) * viewMatrix();
}

Ray Camera::ray(Vec2f point, Size2 size) const {
  float ndcX = point.x / size.width * 2 - 1;
  float ndcY = 1 - point.y / size.height * 2;
  float aspect = size.width / std::max(size.height, 1.0f);
  if (orthographic) {
    float h = halfHeight();
    // Start well behind the eye so geometry behind it is still hit.
    Vec3f origin = eye() + right() * (ndcX * h * aspect) + up() * (ndcY * h) - forward() * (distance * 20 + 2000);
    return {origin, forward()};
  }
  float t = std::tan(fovY / 2);
  Vec3f dir = normalize(forward() + right() * (ndcX * t * aspect) + up() * (ndcY * t));
  return {eye(), dir};
}

std::optional<Vec2f> Camera::project(Vec3f p, Size2 size) const {
  Vec4f clip = viewProjection(size) * Vec4f(p, 1);
  if (!(clip.w > 0)) return std::nullopt;
  float nx = clip.x / clip.w, ny = clip.y / clip.w;
  return Vec2f((nx + 1) / 2 * size.width, (1 - ny) / 2 * size.height);
}

float Camera::worldPerPoint(Size2 size) const {
  return 2 * distance * std::tan(fovY / 2) / std::max(size.height, 1.0f);
}

void Camera::orbit(float dx, float dy) {
  yaw -= dx * 0.008f;
  pitch = std::min(std::max(pitch + dy * 0.008f, -kPitchLimit), kPitchLimit);
}

void Camera::pan(float dx, float dy, Size2 size) {
  float s = worldPerPoint(size);
  target += (-right() * dx + up() * dy) * s;
}

void Camera::zoom(float factor) { distance = std::min(std::max(distance * factor, 0.5f), 20000.0f); }

void Camera::zoom(float factor, Vec3f towards) {
  target = towards + (target - towards) * factor;
  zoom(factor);
}

void Camera::fit(Vec3f lo, Vec3f hi, float aspect) {
  if (!(lo.x <= hi.x) || !isFinite(lo) || !isFinite(hi)) return;
  // A window that hasn't been laid out yet reports a zero size.
  if (!(std::isfinite(aspect) && aspect > 0.01f)) aspect = 1;
  target = (lo + hi) / 2;
  float radius = std::max(length(hi - lo) / 2, 1.0f);
  if (orthographic) {
    distance = radius * 1.15f / (std::tan(fovY / 2) * std::min(1.0f, aspect));
    return;
  }
  float halfX = std::atan(std::tan(fovY / 2) * aspect);
  distance = radius / std::sin(std::min(fovY / 2, halfX)) * 1.15f;
}

void Camera::look(Preset preset) {
  switch (preset) {
    case Preset::Iso: yaw = -kPi / 4; pitch = 0.55f; break;
    case Preset::Top: yaw = -kPi / 2; pitch = kPitchLimit; break;
    case Preset::Bottom: yaw = -kPi / 2; pitch = -kPitchLimit; break;
    case Preset::Front: yaw = -kPi / 2; pitch = 0; break;
    case Preset::Back: yaw = kPi / 2; pitch = 0; break;
    case Preset::Right: yaw = 0; pitch = 0; break;
    case Preset::Left: yaw = kPi; pitch = 0; break;
  }
}

void Camera::snapToNearestOrtho() {
  if (std::fabs(pitch) > kPi / 4) {
    pitch = pitch > 0 ? kPitchLimit : -kPitchLimit;
  } else {
    pitch = 0;
  }
  // Swift's .rounded() rounds half away from zero, like std::round.
  yaw = std::round(yaw / (kPi / 2)) * (kPi / 2);
}

void Camera::rotate(float yawSteps, float pitchSteps) {
  yaw += yawSteps * kPi / 2;
  pitch = std::min(std::max(pitch + pitchSteps * kPi / 2, -kPitchLimit), kPitchLimit);
}

void Camera::face(Vec3f n, Vec3f origin, Vec3f xDir) {
  float hx = n.x, hy = n.y;
  pitch = std::min(std::max(std::asin(std::max(std::min(n.z, 1.0f), -1.0f)), -kPitchLimit), kPitchLimit);
  if (std::fabs(n.z) > 0.99f) {
    // right = (-sin yaw, cos yaw, 0)
    yaw = std::atan2(-xDir.x, xDir.y);
  } else if (std::sqrt(hx * hx + hy * hy) > 1e-4f) {
    yaw = std::atan2(n.y, n.x);
  }
  target = origin;
}

}  // namespace sculpt::viewport
