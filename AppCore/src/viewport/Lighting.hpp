// GL-free pieces of the renderer (static helpers on the Swift Renderer):
// background gradient, key light direction, shadow-map light matrix and
// smoothed normals. Kept separate so they can be unit tested on any host.
#pragma once

#include <vector>

#include "Camera.hpp"
#include "Scene.hpp"
#include "ViewMath.hpp"

namespace sculpt::viewport {

/// Bottom and top of the background gradient.
void backgroundColors(const EnvironmentSettings& env, Vec3f& bottom, Vec3f& top);

/// Unit direction towards the key light: `elevation` degrees above the
/// ground (clamped 5..89), `azimuth` degrees to the right of the viewer.
Vec3f keyLight(const Camera& camera, float azimuthDegrees, float elevationDegrees = 50);

/// Orthographic view from the key light covering a sphere (Metal z convention).
Mat4f lightMatrix(Vec3f key, Vec3f center, float radius);

/// Averages each vertex normal with those of coincident vertices whose
/// normals bend by less than `limitDegrees`, so revolved polylines shade
/// smoothly while sharp creases stay.
std::vector<Vec3f> smoothNormals(const std::vector<Vec3f>& positions, const std::vector<Vec3f>& normals,
                                 float limitDegrees = 42);

/// Clip-space transform that stretches one tile of a larger image over the
/// viewport (screenshot tiling). Image pixels, origin top-left.
/// Returns (sx, sy, ox, oy): clip.xy = clip.xy * s + o * clip.w.
Vec4f tileTransform(int x0, int y0, int tileWidth, int tileHeight, int imageWidth, int imageHeight);

/// The tile transform as a matrix to left-multiply a view-projection by.
Mat4f tileMatrix(Vec4f tile);

}  // namespace sculpt::viewport
