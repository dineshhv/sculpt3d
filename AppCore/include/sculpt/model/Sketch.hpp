// Port of Sketch.swift, SketchOps.swift and SketchTransforms.swift: a
// constrained 2D sketch on a plane, its profiles for the kernel, and the
// topology edits (trim, offset, fillet, disconnect, copy/transform).
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "o3d/Kernel.hpp"
#include "sculpt/model/Equatable.hpp"
#include "sculpt/model/Math.hpp"

namespace sculpt {

/// A plane in world space used for sketches and mirroring.
struct SketchPlane {
  Vec3d origin;
  Vec3d normal{0, 0, 1};
  Vec3d xDir{1, 0, 0};

  SCULPT_FIELDS(origin, normal, xDir)

  Vec3d yDir() const { return cross(normal, xDir); }
  static SketchPlane xy() { return {{0, 0, 0}, {0, 0, 1}, {1, 0, 0}}; }
  static SketchPlane xz() { return {{0, 0, 0}, {0, -1, 0}, {1, 0, 0}}; }
  static SketchPlane yz() { return {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}; }

  Vec3d world(const Vec2d& p) const { return origin + xDir * p.x + yDir() * p.y; }
  Vec2d local(const Vec3d& p) const {
    Vec3d d = p - origin;
    return {dot(d, xDir), dot(d, yDir())};
  }
  o3d::Plane bridged() const { return {toO3d(origin), toO3d(normal), toO3d(xDir)}; }
  /// Turned by `angle` degrees about its X axis, then moved along its normal (Feature.swift).
  SketchPlane transformed(double offset, double angle) const;
};

struct SketchLine {
  int a = 0;
  int b = 0;
  bool construction = false;
  SCULPT_FIELDS(a, b, construction)
};

/// A full circle, or an arc when `start` and `end` are set. Arcs run
/// counter-clockwise from start to end and keep both points on the circle.
struct SketchCircle {
  int center = 0;
  double radius = 0;
  bool construction = false;
  std::optional<int> start;
  std::optional<int> end;
  SCULPT_FIELDS(center, radius, construction, start, end)
  bool isArc() const { return start.has_value() && end.has_value(); }
};

/// A pointer position on the sketch plane, snapped to an existing point or
/// the grid. `index` is set when it landed on an existing point.
struct SketchSnap {
  Vec2d position;
  std::optional<int> index;
  /// What it snapped to ("Midpoint", "Quadrant", …), for the snapping hint.
  std::optional<std::string> hint;
  /// Alignment guides through existing points, as line segments.
  std::vector<std::vector<Vec2d>> guides;
  SCULPT_FIELDS(position, index, hint, guides)
};

/// Circle through three points, or nothing when they are (nearly) collinear.
std::optional<std::pair<Vec2d, double>> circleThrough(const Vec2d& a, const Vec2d& b, const Vec2d& c);
/// Points along the arc from `a` to `b` that passes through `m`.
std::vector<Vec2d> arcOutline(const Vec2d& a, const Vec2d& b, const Vec2d& m);
/// A uniform cubic B-spline; open curves are clamped to their end points.
std::vector<Vec2d> bsplineSamples(const std::vector<Vec2d>& pts, bool closed, int perSpan = 16);
/// Display samples of a spline through `pts` (centripetal Catmull-Rom).
std::vector<Vec2d> splineSamples(const std::vector<Vec2d>& pts, bool closed, int perSpan = 16);

/// A smooth curve through its points (which are ordinary sketch points).
struct SketchSpline {
  std::vector<int> points;
  bool closed = false;
  bool construction = false;
  /// The points are B-spline control points rather than points the curve passes through.
  bool controlPoints = false;
  SCULPT_FIELDS(points, closed, construction, controlPoints)

  std::vector<Vec2d> samples(const std::vector<Vec2d>& all) const;
  std::vector<Vec2d> fitPoints(const std::vector<Vec2d>& all) const;
};

/// An ellipse around a centre point; its radii are plain values.
struct SketchEllipse {
  int center = 0;
  double majorRadius = 0;
  double minorRadius = 0;
  double rotation = 0;
  bool construction = false;
  SCULPT_FIELDS(center, majorRadius, minorRadius, rotation, construction)
};

std::vector<Vec2d> ellipseSamples(const SketchEllipse& e, const Vec2d& center, int count = 96);

enum class SketchEntityKind { Point, Line, Circle };

/// Something selectable inside a sketch.
struct SketchRef {
  enum class Kind { Point, Line, Circle, Constraint, Spline, Ellipse };
  Kind kind = Kind::Point;
  int index = 0;

  static SketchRef point(int i) { return {Kind::Point, i}; }
  static SketchRef line(int i) { return {Kind::Line, i}; }
  static SketchRef circle(int i) { return {Kind::Circle, i}; }
  static SketchRef constraint(int i) { return {Kind::Constraint, i}; }
  static SketchRef spline(int i) { return {Kind::Spline, i}; }
  static SketchRef ellipse(int i) { return {Kind::Ellipse, i}; }

  friend bool operator==(const SketchRef& a, const SketchRef& b) { return a.kind == b.kind && a.index == b.index; }
  friend bool operator!=(const SketchRef& a, const SketchRef& b) { return !(a == b); }
  friend bool operator<(const SketchRef& a, const SketchRef& b) {
    return a.kind != b.kind ? a.kind < b.kind : a.index < b.index;
  }
};

struct SketchConstraint {
  /// Raw values are stored in documents; keep them stable.
  enum class Kind {
    Coincident, Horizontal, Vertical, Parallel, Perpendicular, EqualLength, EqualRadius,
    PointOnLine, PointOnCircle, Tangent, Midpoint, Fixed,
    Length, Distance, HorizontalDistance, VerticalDistance, Radius, Angle,
    Symmetric
  };
  static const std::vector<Kind>& allKinds();

  Kind kind = Kind::Coincident;
  int a = 0;
  int b = -1;
  double value = 0;
  double value2 = 0;
  /// Third entity: the mirror line of a symmetric constraint.
  std::optional<int> c;
  SCULPT_FIELDS(kind, a, b, value, value2, c)

  std::string unit() const { return kind == Kind::Angle ? "°" : "mm"; }
};

/// Properties of a constraint kind (SketchConstraint.Kind's computed vars).
std::string rawValue(SketchConstraint::Kind k);
std::optional<SketchConstraint::Kind> constraintKind(const std::string& raw);
/// What `a` and `b` index into.
std::pair<SketchEntityKind, std::optional<SketchEntityKind>> arguments(SketchConstraint::Kind k);
bool isDimension(SketchConstraint::Kind k);
std::string title(SketchConstraint::Kind k);
std::string symbol(SketchConstraint::Kind k);
/// Short glyph drawn next to the geometry it constrains.
std::string glyph(SketchConstraint::Kind k);

/// Result of the last solve, used to colour geometry and report freedom.
struct SketchStatus {
  int degreesOfFreedom = 0;
  std::vector<bool> fixedPoints;
  std::vector<bool> fixedCircles;
  SCULPT_FIELDS(degreesOfFreedom, fixedPoints, fixedCircles)
  bool fullyConstrained() const { return degreesOfFreedom == 0; }
};

/// A similarity transform of the sketch plane: p' = R·S·(p - pivot) + pivot + shift,
/// optionally mirrored across a line through the pivot (SketchTransforms.swift).
struct SketchTransform {
  Vec2d pivot;
  double angle = 0;
  double scale = 1;
  Vec2d shift;
  /// Mirror across the line through `pivot` with this direction (before rotating).
  std::optional<Vec2d> mirrorAxis;

  Vec2d apply(const Vec2d& p) const;
  bool flips() const { return mirrorAxis.has_value(); }
  bool keepsAxes() const;
};

/// A constrained 2D sketch on a plane. Point 0 is the plane origin and never
/// moves; lines and circles reference points by index.
struct Sketch {
  SketchPlane plane;
  std::vector<Vec2d> points{Vec2d{0, 0}};
  std::vector<SketchLine> lines;
  std::vector<SketchCircle> circles;
  std::vector<SketchConstraint> constraints;
  /// Standalone points placed with the Point tool.
  std::vector<int> markers;
  std::vector<SketchSpline> splines;
  std::vector<SketchEllipse> ellipses;

  SCULPT_FIELDS(plane, points, lines, circles, constraints, markers, splines, ellipses)

  Sketch() : plane(SketchPlane::xy()) {}
  explicit Sketch(const SketchPlane& p) : plane(p) {}

  bool isEmpty() const { return lines.empty() && circles.empty() && splines.empty() && ellipses.empty(); }

  // MARK: Building
  int addPoint(const Vec2d& p);
  /// Adds a line and infers horizontal / vertical when it is drawn axis-aligned.
  std::optional<int> addLine(int a, int b);
  /// Four lines held square by horizontal and vertical constraints.
  void addRectangle(const Vec2d& p0, const Vec2d& p1, std::optional<int> start, std::optional<int> end);
  void addCircle(int center, double radius);
  /// Counter-clockwise arc from `start` to `end` around `center`.
  void addArc(int center, int start, int end);
  /// Rectangle drawn from its center to a corner.
  void addCenterRectangle(const Vec2d& center, const Vec2d& corner, std::optional<int> centerIndex = std::nullopt);
  /// Regular polygon: equal sides with every corner on a construction circle.
  void addPolygon(const Vec2d& center, const Vec2d& corner, int sides, std::optional<int> centerIndex = std::nullopt);
  /// Start and sweep angles of an arc, sweep in (0, 2π].
  std::optional<std::pair<double, double>> arcAngles(int i) const;

  // MARK: Constraints
  /// Current measured value of a dimension, used as its initial value.
  double measure(SketchConstraint::Kind kind, int a, int b) const;
  Vec2d direction(int line) const { return points[lines[line].b] - points[lines[line].a]; }
  Vec2d midpoint(int line) const { return (points[lines[line].a] + points[lines[line].b]) / 2.0; }
  /// Solves the constraints, optionally pulling dragged points towards
  /// targets. Returns nothing and leaves the sketch untouched on conflict.
  std::optional<SketchStatus> solve(const std::map<int, Vec2d>& drag = {});

  // MARK: Deleting (Swift `delete(_:)`)
  /// Removes entities and constraints, everything that depends on them, and
  /// points no curve uses any more; indices are compacted.
  void remove(const std::set<SketchRef>& refs);

  // MARK: Profiles
  /// Closed regions for extrude / revolve: every full circle, plus every
  /// closed loop of lines and arcs. Construction geometry is ignored.
  std::vector<o3d::Profile> profiles() const;
  /// The first open chain (or, failing that, loop) of lines and arcs, as a sweep path.
  std::vector<o3d::Segment> path() const;
  int profileCount() const { return int(profiles().size()); }

  // MARK: SketchOps.swift
  /// Removes the piece of a line, circle or arc between the crossings either
  /// side of `point`. Returns false when there was nothing to trim.
  bool trim(const SketchRef& ref, const Vec2d& point);
  /// Parallel copies of lines, circles and arcs `distance` away.
  std::vector<SketchRef> offset(const std::vector<SketchRef>& refs, double distance);
  /// Rounds the corner where two lines meet; returns an error message on failure.
  std::optional<std::string> fillet(int corner, double radius);
  /// Gives every curve that shares `point` its own copy of it.
  void disconnect(int point);

  // MARK: SketchTransforms.swift
  Vec2d centroid(const std::vector<SketchRef>& refs) const;
  /// Adds a transformed copy of the referenced geometry and returns its refs.
  std::vector<SketchRef> addCopy(const std::vector<SketchRef>& refs, const SketchTransform& t);
  /// Moves the referenced geometry in place (as a transformed copy that replaces it).
  std::vector<SketchRef> transform(const std::vector<SketchRef>& refs, const SketchTransform& t);

 private:
  enum class EdgeKind { Line, Arc, Spline };
  struct Edge {
    EdgeKind kind;
    int index;
    bool operator==(const Edge& o) const { return kind == o.kind && index == o.index; }
    bool operator<(const Edge& o) const { return kind != o.kind ? kind < o.kind : index < o.index; }
  };
  struct Chain {
    std::vector<Edge> edges;
    int start;
    bool closed;
  };
  std::vector<int> coincidenceRoots() const;
  std::pair<int, int> ends(const Edge& e) const;
  o3d::Segment segment(const Edge& e, int from, const std::vector<int>& root) const;
  std::vector<Chain> chains() const;

  std::vector<double> cutsOnLine(int l) const;
  std::vector<double> cutsOnCircle(int i) const;
  std::vector<double> lineCircle(const Vec2d& a, const Vec2d& d, int i) const;
  bool onCurve(int i, const Vec2d& p) const;
  std::set<int> pointsOf(const std::vector<SketchRef>& refs) const;
};

}  // namespace sculpt

namespace std {
template <>
struct hash<sculpt::SketchRef> {
  size_t operator()(const sculpt::SketchRef& r) const noexcept { return size_t(r.kind) * 1000003u ^ size_t(r.index); }
};
}  // namespace std
