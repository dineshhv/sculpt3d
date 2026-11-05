// Port of ProjectSettings.swift (Appearance lives in Visualization.hpp).
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "sculpt/model/Equatable.hpp"
#include "sculpt/model/Math.hpp"
#include "sculpt/model/Sketch.hpp"
#include "sculpt/model/Uuid.hpp"
#include "sculpt/model/Visualization.hpp"

namespace sculpt {

enum class LengthUnit { mm, cm, m, inch, ft };

const std::vector<LengthUnit>& allLengthUnits();
std::string rawValue(LengthUnit u);
std::optional<LengthUnit> lengthUnit(const std::string& raw);
std::string symbol(LengthUnit u);
std::string title(LengthUnit u);
bool isImperial(LengthUnit u);
/// Millimetres per unit.
double scale(LengthUnit u);

struct Variable {
  Uuid id = Uuid::random();
  std::string name;
  std::string expression;
  SCULPT_FIELDS(id, name, expression)
};

/// What the pointer snaps to while sketching, and which hints show.
struct SnapSettings {
  bool grid = true;
  bool guideLines = true;    // horizontal / vertical alignment with existing points
  bool guidePoints = true;   // midpoints, centres and quadrants
  bool points3D = true;      // body corners projected onto the sketch plane
  bool distantEdges = true;  // body edges as snap targets even when far away
  bool showGuidePoints = true;
  bool showHints = true;
  SCULPT_FIELDS(grid, guideLines, guidePoints, points3D, distantEdges, showGuidePoints, showHints)
};

/// Per-document settings and per-body presentation. Bodies are keyed by the
/// id the kernel gives them on replay, which is stable for a given history.
struct ProjectSettings {
  LengthUnit units = LengthUnit::mm;
  bool snapping = true;
  std::map<int, std::string> bodyNames;
  std::map<int, Appearance> appearance;
  std::set<int> hiddenBodies;
  std::set<Uuid> hiddenItems;
  EnvironmentSettings environment;
  CameraSettings camera;
  SnapSettings snap;
  /// Named values usable in any parameter field, e.g. `width * 2`.
  std::vector<Variable> variables;
  SCULPT_FIELDS(units, snapping, bodyNames, appearance, hiddenBodies, hiddenItems, environment, camera, snap, variables)

  std::string name(int body) const;
};

/// A sketch, construction plane, axis or reference image from the history,
/// shown in the viewport.
struct ReferenceItem {
  struct Axis {
    Vec3d origin, direction;
    SCULPT_FIELDS(origin, direction)
  };
  using Content = std::variant<Sketch, SketchPlane, Axis, ReferenceImage>;

  Uuid id;
  std::string name;
  Content content;

  SketchPlane plane() const;
  bool isPlane() const { return std::holds_alternative<SketchPlane>(content); }
  bool isSketch() const { return std::holds_alternative<Sketch>(content); }
  bool isAxis() const { return std::holds_alternative<Axis>(content); }
  bool isImage() const { return std::holds_alternative<ReferenceImage>(content); }
  const Sketch* sketch() const { return std::get_if<Sketch>(&content); }
  const ReferenceImage* image() const { return std::get_if<ReferenceImage>(&content); }
  const Axis* axis() const { return std::get_if<Axis>(&content); }
};

}  // namespace sculpt
