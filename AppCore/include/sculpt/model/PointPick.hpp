// Model side of ViewportController+Points.swift: picked points, point-pick
// sessions (whose completion runs a store command instead of a closure) and
// point measurements. Finding the point under the pointer (screen-space
// snapping to corners, edges and faces) stays in the viewport.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sculpt/model/BodyMesh.hpp"
#include "sculpt/model/Math.hpp"
#include "sculpt/model/Uuid.hpp"

namespace sculpt {

/// A point picked on the model: a corner, a spot on an edge, or a spot on a face.
struct PickedPoint {
  enum class Kind { vertex, edge, face };
  Vec3d position;
  Kind kind = Kind::face;
  Pick pick;
  /// Direction of the edge at the point (edge and vertex picks).
  std::optional<Vec3d> tangent;
  /// Surface normal at the point (face picks).
  std::optional<Vec3d> normal;
};

/// What a point pick is for (Swift passed a completion closure).
enum class PointPickPurpose {
  planeThrough3Points,
  planeOnCurveAtPoint,
  planePerpendicularToEdgeAtPoint,
  planeParallelToFaceAtPoint,
  axisThrough2Points,
  axisPerpendicularToFaceAtPoint,
  measurePointToPoint,
  measureThreePointAngle,
};

/// Collects points from the viewport for a construction or a measurement.
struct PointPickSession {
  PointPickPurpose purpose;
  std::string title;
  /// One prompt per point still to pick.
  std::vector<std::string> prompts;
  /// Kinds accepted for each point; empty accepts any.
  std::vector<PickedPoint::Kind> accepts;
  std::vector<PickedPoint> points;
  std::optional<PickedPoint> hover;

  /// The session for a menu command (title, prompts and accepted kinds as in ModelCommands.swift).
  static PointPickSession make(PointPickPurpose purpose);
  size_t needed() const { return prompts.size(); }
  std::string prompt() const { return points.size() < prompts.size() ? prompts[points.size()] : ""; }
  /// Error toast when `p` is the wrong kind for the next point, else nothing.
  std::optional<std::string> rejection(const PickedPoint& p) const;
};

/// A measurement taken between picked points, shown in the viewport.
struct PointMeasurement {
  Uuid id = Uuid::random();
  std::vector<Vec3d> points;
  std::string label;
  /// Where the label sits: the middle of the distance, or the angle's vertex.
  Vec3d anchor() const { return points.size() == 3 ? points[1] : (points[0] + points[1]) / 2.0; }
};

}  // namespace sculpt
