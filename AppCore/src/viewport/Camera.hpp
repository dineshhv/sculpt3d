// Z-up orbit camera, a port of App/Sources/Viewport/Camera.swift.
//
// Sizes and points are in view points (origin top-left), like the Swift
// CGSize/CGPoint arguments. Matrices use the Metal clip convention (z in
// [0, w]); only the renderer's shaders remap z for OpenGL, so picking and
// projection here match the Apple app exactly.
#pragma once

#include <optional>

#include "Scene.hpp"
#include "ViewMath.hpp"

namespace sculpt::viewport {

struct Size2 {
  float width = 1, height = 1;
};

struct Ray {
  Vec3f origin;
  Vec3f direction;
};

struct Camera {
  Vec3f target{0, 0, 10};
  float distance = 220;
  float yaw = -kPi / 4;   // around Z, 0 looks from +X
  float pitch = 0.55f;    // elevation above the XY plane
  float fovY = 40 * kPi / 180;
  /// Parallel projection; the visible height at the target matches perspective.
  bool orthographic = false;

  enum class Preset { Iso, Front, Back, Top, Bottom, Right, Left };

  CameraSettings settings() const;
  void setSettings(const CameraSettings& s);
  /// Whether the camera already uses `s`.
  bool matches(const CameraSettings& s) const;

  /// Half the visible height at the target distance.
  float halfHeight() const;
  Vec3f forward() const;
  Vec3f eye() const;
  Vec3f right() const;
  Vec3f up() const;

  Mat4f viewMatrix() const;
  Mat4f projection(float aspect) const;
  Mat4f viewProjection(Size2 size) const;

  /// World-space ray through a point in view coordinates (origin top-left).
  Ray ray(Vec2f point, Size2 size) const;
  /// Projects a world point to view coordinates; nullopt when behind the
  /// camera (perspective only).
  std::optional<Vec2f> project(Vec3f p, Size2 size) const;
  /// World units covered by one point at the target distance.
  float worldPerPoint(Size2 size) const;

  void orbit(float dx, float dy);
  void pan(float dx, float dy, Size2 size);
  void zoom(float factor);
  /// Zooms towards a world point (e.g. the surface under the pointer) so it stays put.
  void zoom(float factor, Vec3f towards);
  /// Frames a bounding box; `aspect` is width / height.
  void fit(Vec3f lo, Vec3f hi, float aspect = 1);
  void look(Preset preset);
  /// Snaps to the closest of the six orthographic directions.
  void snapToNearestOrtho();
  /// Turns the view by quarter turns around the vertical (yaw) or sideways (pitch).
  void rotate(float yawSteps, float pitchSteps);
  /// Looks straight at a plane; on up/down planes also aligns its X axis to screen right.
  void face(Vec3f normal, Vec3f origin, Vec3f xDir);
};

}  // namespace sculpt::viewport
