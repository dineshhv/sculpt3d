// SketchTool, SketchSession and Draft from ModelStore.swift, and the
// session's constraint editing (SketchEditing.swift).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sculpt/model/Feature.hpp"
#include "sculpt/model/Sketch.hpp"

namespace sculpt {

enum class SketchTool { select, line, arc, spline, controlSpline, rectangle, centerRectangle, circle, ellipse, polygon, point, trim };

const std::vector<SketchTool>& allSketchTools();
std::string rawValue(SketchTool t);
std::optional<SketchTool> sketchTool(const std::string& raw);
std::string title(SketchTool t);
std::string symbol(SketchTool t);
/// Tools drawn by dragging; the others place points by tapping.
bool drags(SketchTool t);
/// Single-key shortcut in the viewport (0 when none).
char key(SketchTool t);

/// An in-progress sketch. Geometry is in plane (u, v) coordinates.
struct SketchSession {
  Sketch sketch;
  std::optional<int> target;
  /// History feature whose sketch is being edited, if any.
  std::optional<Uuid> replacing;
  SketchTool tool = SketchTool::rectangle;
  /// Outline of the rectangle or circle being dragged out.
  std::vector<Vec2d> preview;
  /// Last point of the line chain being drawn.
  std::optional<int> chainEnd;
  /// Points placed so far by a multi-tap tool (the arc's start and end).
  std::vector<SketchSnap> pending;
  /// Points of the spline being drawn.
  std::vector<int> splineDraft;
  int polygonSides = 6;
  /// Polygon drawn around the dragged circle instead of inside it.
  bool polygonCircumscribed = false;
  /// True while geometry is being dragged, so the drag is one undo step.
  bool dragging = false;
  /// New geometry is drawn as construction geometry.
  bool constructionMode = false;
  double offsetDistance = 5;
  double filletRadius = 5;
  std::optional<Vec2d> cursor;
  /// Snapping feedback for the cursor.
  std::optional<std::string> snapHint;
  std::vector<std::vector<Vec2d>> guides;
  std::vector<SketchRef> selection;
  std::optional<SketchRef> hover;
  SketchStatus status;

  SketchSession() = default;
  SketchSession(Sketch s, std::optional<int> t = std::nullopt, std::optional<Uuid> r = std::nullopt,
                SketchTool tl = SketchTool::rectangle)
      : sketch(std::move(s)), target(t), replacing(r), tool(tl) {}

  const SketchPlane& plane() const { return sketch.plane; }

  // MARK: SketchEditing.swift
  std::vector<int> selectedPoints() const;
  std::vector<int> selectedLines() const;
  std::vector<int> selectedCircles() const;
  std::vector<int> selectedSplines() const;
  std::vector<int> selectedEllipses() const;
  std::vector<int> selectedConstraints() const;
  /// The single selected dimension, which the sketch bar lets you edit.
  std::optional<int> selectedDimension() const;
  /// Constraints that make sense for the current selection, in toolbar order.
  std::vector<SketchConstraint::Kind> availableConstraints() const;
  /// Re-solves after geometry changes, keeping the status current.
  void resolve();
  /// Adds `kind` to the selection; returns an error message when it can't.
  std::optional<std::string> constrain(SketchConstraint::Kind kind);
  std::optional<std::string> setDimension(int index, double value);
  /// Label shown for a dimension in the viewport.
  std::string label(const SketchConstraint& c) const;
  /// Where a constraint's label sits, in plane coordinates, plus the
  /// geometry it measures (drawn as a thin leader).
  struct Anchor {
    Vec2d position;
    std::vector<Vec2d> leader;
  };
  Anchor anchor(const SketchConstraint& c, double offset) const;

  // MARK: SketchBar.swift
  /// The Escape button's title for the current state.
  std::string escapeTitle() const;
};

/// An operation being configured. It is applied live so the viewport previews it.
struct Draft {
  Feature feature;
  std::optional<Uuid> replacing;
  std::optional<SketchSession> sketch;
};

}  // namespace sculpt
