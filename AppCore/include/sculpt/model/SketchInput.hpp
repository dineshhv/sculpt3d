// Model side of ViewportController+Sketch.swift: snapping, hit testing and
// the sketch tools' tap/drag state machine, in sketch-plane coordinates.
//
// The viewport turns a pointer position into a plane point (ray/plane
// intersection) and supplies the pick radius and grid step in plane units
// (both depend on the camera); everything else happens here.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sculpt/model/ModelStore.hpp"

namespace sculpt {

/// Grid step that keeps roughly 20–200 grid lines on screen for a camera at
/// `cameraDistance` (ViewportController.gridStep), unless the grid is fixed.
double gridStep(double cameraDistance, std::optional<double> fixedGrid = std::nullopt);

/// A polygon's first corner. Inscribed: the drag point is a corner.
/// Circumscribed: it's the middle of a side.
Vec2d polygonCorner(const Vec2d& center, const Vec2d& drag, int sides, bool circumscribed);

/// A constraint glyph or dimension label, at its plane position. The viewport
/// projects `position` to the screen and calls `spreadAnnotations`.
struct SketchAnnotation {
  std::string id;
  int constraint = -1;  // -1 for the snapping hint
  std::string text;
  Vec2d position;
  bool isDimension = false;
  bool selected = false;
  /// The snapping hint sits at a screen offset (+34, -16 points) from `position`.
  bool isHint = false;
};

/// Pointer-driven sketch editing for one viewport.
class SketchInput {
 public:
  explicit SketchInput(ModelStore& store) : store_(store) {}

  /// Set by the viewport before each event: hit radius and grid step, in plane units.
  double pickRadius = 1;
  double gridStep = 1;

  /// Snapped plane position for `p` (existing points, guide points, 3D
  /// points, grid, alignment guides).
  std::optional<SketchSnap> snap(const Vec2d& p) const;
  /// Point, line, spline, ellipse or circle under `p`; points win over curves.
  std::optional<SketchRef> hit(const Vec2d& p) const;
  Vec2d gridSnap(const Vec2d& p) const;
  /// Corners of visible bodies projected onto the sketch plane (3D-point snapping).
  std::vector<Vec2d> projectedBodyPoints() const;

  /// Pointer moved (nothing: it left the view). Returns true when the sketch changed.
  bool hover(const std::optional<Vec2d>& p);
  void tap(const Vec2d& p, bool additive);
  /// True when a one-finger / left-button drag at `p` should draw or move
  /// sketch geometry instead of orbiting.
  bool dragDraws(const Vec2d& p) const;
  /// Drags: `p` is nothing when the pointer is off the plane.
  void drawBegan(const std::optional<Vec2d>& p);
  void drawChanged(const std::optional<Vec2d>& p);
  void drawEnded(const std::optional<Vec2d>& p);
  bool isDragging() const { return drag_.has_value(); }

  /// Constraint glyphs and dimension values (`offset` is 14 points in plane units).
  std::vector<SketchAnnotation> annotations(double offset) const;
  /// Spreads labels that would overlap on screen (`screen[i]` is annotation i's
  /// screen point in view points; returns adjusted points).
  static std::vector<Vec2d> spreadAnnotations(std::vector<Vec2d> screen);

 private:
  struct Drag {
    SketchRef ref;
    Sketch base;
    Vec2d start;
  };
  std::optional<int> nearestPoint(const Sketch& sketch, const Vec2d& p) const;
  void markConstruction(SketchSession& s, size_t lines, size_t circles) const;

  ModelStore& store_;
  std::optional<Drag> drag_;
  std::optional<SketchSnap> dragStart_;
};

}  // namespace sculpt
