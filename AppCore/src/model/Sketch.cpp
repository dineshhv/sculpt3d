// Port of Sketch.swift.
#include "sculpt/model/Sketch.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

#include "o3d/SketchSolver.hpp"

namespace sculpt {

SketchPlane SketchPlane::transformed(double offset, double angle) const {
  double t = angle * kPi / 180;
  Vec3d y = yDir();
  Vec3d n = normal * std::cos(t) - y * std::sin(t);
  return SketchPlane{origin + n * offset, normalize(n), xDir};
}

std::optional<std::pair<Vec2d, double>> circleThrough(const Vec2d& a, const Vec2d& b, const Vec2d& c) {
  double d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
  if (!(std::abs(d) > 1e-9)) return std::nullopt;
  double a2 = lengthSquared(a), b2 = lengthSquared(b), c2 = lengthSquared(c);
  Vec2d center((a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d,
               (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d);
  return std::make_pair(center, distance(center, a));
}

std::vector<Vec2d> arcOutline(const Vec2d& a, const Vec2d& b, const Vec2d& m) {
  auto circle = circleThrough(a, b, m);
  if (!circle) return {a, b};
  Vec2d c = circle->first;
  double r = circle->second;
  double t0 = std::atan2(a.y - c.y, a.x - c.x);
  auto ccwSweep = [&](const Vec2d& p) {
    double s = std::atan2(p.y - c.y, p.x - c.x) - t0;
    while (s < 0) s += 2 * kPi;
    return s;
  };
  // Go the way that passes the middle point.
  double sweep = ccwSweep(b);
  if (ccwSweep(m) > sweep) sweep -= 2 * kPi;
  std::vector<Vec2d> out;
  for (int i = 0; i <= 48; ++i) {
    double t = t0 + sweep * double(i) / 48;
    out.push_back(c + Vec2d(std::cos(t), std::sin(t)) * r);
  }
  return out;
}

std::vector<Vec2d> bsplineSamples(const std::vector<Vec2d>& pts, bool closed, int perSpan) {
  if (pts.size() < 3) return pts;
  std::vector<Vec2d> ctrl;
  if (closed) {
    ctrl = pts;
    ctrl.insert(ctrl.end(), pts.begin(), pts.begin() + 3);
  } else {
    ctrl = {pts[0], pts[0]};
    ctrl.insert(ctrl.end(), pts.begin(), pts.end());
    ctrl.push_back(pts.back());
    ctrl.push_back(pts.back());
  }
  std::vector<Vec2d> out;
  for (size_t i = 0; i + 3 < ctrl.size(); ++i) {
    Vec2d p0 = ctrl[i], p1 = ctrl[i + 1], p2 = ctrl[i + 2], p3 = ctrl[i + 3];
    for (int k = (i == 0 ? 0 : 1); k <= perSpan; ++k) {
      double t = double(k) / double(perSpan), t2 = t * t, t3 = t2 * t;
      double b0 = (1 - t) * (1 - t) * (1 - t) / 6;
      double b1 = (3 * t3 - 6 * t2 + 4) / 6;
      double b2 = (-3 * t3 + 3 * t2 + 3 * t + 1) / 6;
      double b3 = t3 / 6;
      out.push_back(p0 * b0 + p1 * b1 + p2 * b2 + p3 * b3);
    }
  }
  return out;
}

std::vector<Vec2d> splineSamples(const std::vector<Vec2d>& pts, bool closed, int perSpan) {
  if (pts.size() < 2) return pts;
  int n = int(pts.size());
  auto p = [&](int i) -> Vec2d {
    if (closed) return pts[((i % n) + n) % n];
    if (i < 0) return pts[0] * 2.0 - pts[1];
    if (i >= n) return pts[n - 1] * 2.0 - pts[n - 2];
    return pts[i];
  };
  auto knot = [](const Vec2d& a, const Vec2d& b) { return std::max(std::pow(distance(a, b), 0.5), 1e-6); };
  std::vector<Vec2d> out{pts[0]};
  for (int i = 0; i < (closed ? n : n - 1); ++i) {
    Vec2d p0 = p(i - 1), p1 = p(i), p2 = p(i + 1), p3 = p(i + 2);
    double t1 = knot(p0, p1), t2 = t1 + knot(p1, p2), t3 = t2 + knot(p2, p3);
    for (int k = 1; k <= perSpan; ++k) {
      double t = t1 + (t2 - t1) * double(k) / double(perSpan);
      Vec2d a1 = p0 * ((t1 - t) / t1) + p1 * (t / t1);
      Vec2d a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
      Vec2d a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
      Vec2d b1 = a1 * ((t2 - t) / t2) + a2 * (t / t2);
      Vec2d b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
      out.push_back(b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1)));
    }
  }
  return out;
}

std::vector<Vec2d> SketchSpline::samples(const std::vector<Vec2d>& all) const {
  std::vector<Vec2d> pts;
  for (int i : points) pts.push_back(all[i]);
  return controlPoints ? bsplineSamples(pts, closed) : splineSamples(pts, closed);
}

std::vector<Vec2d> SketchSpline::fitPoints(const std::vector<Vec2d>& all) const {
  std::vector<Vec2d> pts;
  for (int i : points) pts.push_back(all[i]);
  if (!controlPoints) return pts;
  auto s = bsplineSamples(pts, closed, 8);
  if (closed && s.size() > 1) s.pop_back();
  return s;
}

std::vector<Vec2d> ellipseSamples(const SketchEllipse& e, const Vec2d& c, int count) {
  Vec2d u(std::cos(e.rotation), std::sin(e.rotation)), v(-u.y, u.x);
  std::vector<Vec2d> out;
  for (int k = 0; k <= count; ++k) {
    double t = double(k) / double(count) * 2 * kPi;
    out.push_back(c + u * (std::cos(t) * e.majorRadius) + v * (std::sin(t) * e.minorRadius));
  }
  return out;
}

// MARK: Constraint kinds

using K = SketchConstraint::Kind;

const std::vector<K>& SketchConstraint::allKinds() {
  static const std::vector<K> all = {K::Coincident, K::Horizontal, K::Vertical, K::Parallel, K::Perpendicular,
                                     K::EqualLength, K::EqualRadius, K::PointOnLine, K::PointOnCircle, K::Tangent,
                                     K::Midpoint, K::Fixed, K::Length, K::Distance, K::HorizontalDistance,
                                     K::VerticalDistance, K::Radius, K::Angle, K::Symmetric};
  return all;
}

std::string rawValue(K k) {
  switch (k) {
    case K::Coincident: return "coincident";
    case K::Horizontal: return "horizontal";
    case K::Vertical: return "vertical";
    case K::Parallel: return "parallel";
    case K::Perpendicular: return "perpendicular";
    case K::EqualLength: return "equalLength";
    case K::EqualRadius: return "equalRadius";
    case K::PointOnLine: return "pointOnLine";
    case K::PointOnCircle: return "pointOnCircle";
    case K::Tangent: return "tangent";
    case K::Midpoint: return "midpoint";
    case K::Fixed: return "fixed";
    case K::Length: return "length";
    case K::Distance: return "distance";
    case K::HorizontalDistance: return "horizontalDistance";
    case K::VerticalDistance: return "verticalDistance";
    case K::Radius: return "radius";
    case K::Angle: return "angle";
    case K::Symmetric: return "symmetric";
  }
  return "";
}

std::optional<K> constraintKind(const std::string& raw) {
  for (K k : SketchConstraint::allKinds())
    if (rawValue(k) == raw) return k;
  return std::nullopt;
}

std::pair<SketchEntityKind, std::optional<SketchEntityKind>> arguments(K k) {
  using E = SketchEntityKind;
  switch (k) {
    case K::Coincident: case K::Distance: case K::HorizontalDistance: case K::VerticalDistance: case K::Symmetric:
      return {E::Point, E::Point};
    case K::Horizontal: case K::Vertical: case K::Length: return {E::Line, std::nullopt};
    case K::Parallel: case K::Perpendicular: case K::EqualLength: case K::Angle: return {E::Line, E::Line};
    case K::EqualRadius: return {E::Circle, E::Circle};
    case K::PointOnLine: case K::Midpoint: return {E::Point, E::Line};
    case K::PointOnCircle: return {E::Point, E::Circle};
    case K::Tangent: return {E::Line, E::Circle};
    case K::Fixed: return {E::Point, std::nullopt};
    case K::Radius: return {E::Circle, std::nullopt};
  }
  return {E::Point, std::nullopt};
}

bool isDimension(K k) {
  switch (k) {
    case K::Length: case K::Distance: case K::HorizontalDistance: case K::VerticalDistance: case K::Radius: case K::Angle:
      return true;
    default: return false;
  }
}

std::string title(K k) {
  switch (k) {
    case K::Coincident: return "Coincident";
    case K::Horizontal: return "Horizontal";
    case K::Vertical: return "Vertical";
    case K::Parallel: return "Parallel";
    case K::Perpendicular: return "Perpendicular";
    case K::EqualLength: case K::EqualRadius: return "Equal";
    case K::PointOnLine: case K::PointOnCircle: return "On curve";
    case K::Tangent: return "Tangent";
    case K::Midpoint: return "Midpoint";
    case K::Fixed: return "Lock";
    case K::Symmetric: return "Symmetry";
    case K::Length: return "Length";
    case K::Distance: return "Distance";
    case K::HorizontalDistance: return "Horizontal distance";
    case K::VerticalDistance: return "Vertical distance";
    case K::Radius: return "Radius";
    case K::Angle: return "Angle";
  }
  return "";
}

std::string symbol(K k) {
  switch (k) {
    case K::Coincident: return "smallcircle.filled.circle";
    case K::Horizontal: return "arrow.left.and.right";
    case K::Vertical: return "arrow.up.and.down";
    case K::Parallel: return "equal";
    case K::Perpendicular: return "angle";
    case K::EqualLength: case K::EqualRadius: return "equal.circle";
    case K::PointOnLine: case K::PointOnCircle: return "point.topleft.down.to.point.bottomright.curvepath";
    case K::Tangent: return "circle.and.line.horizontal";
    case K::Midpoint: return "line.diagonal";
    case K::Fixed: return "lock";
    case K::Symmetric: return "arrow.left.and.line.vertical.and.arrow.right";
    case K::Length: case K::Distance: return "ruler";
    case K::HorizontalDistance: return "arrow.left.and.right.square";
    case K::VerticalDistance: return "arrow.up.and.down.square";
    case K::Radius: return "circle.circle";
    case K::Angle: return "angle";
  }
  return "";
}

std::string glyph(K k) {
  switch (k) {
    case K::Horizontal: return "H";
    case K::Vertical: return "V";
    case K::Parallel: return "∥";
    case K::Perpendicular: return "⊥";
    case K::EqualLength: case K::EqualRadius: return "=";
    case K::PointOnLine: case K::PointOnCircle: return "∘";
    case K::Tangent: return "T";
    case K::Midpoint: return "M";
    case K::Fixed: return "⚓︎";
    case K::Symmetric: return "⇹";
    default: return "";
  }
}

static o3d::sketch::ConstraintType bridged(K k) {
  using T = o3d::sketch::ConstraintType;
  switch (k) {
    case K::Coincident: return T::Coincident;
    case K::Horizontal: return T::Horizontal;
    case K::Vertical: return T::Vertical;
    case K::Parallel: return T::Parallel;
    case K::Perpendicular: return T::Perpendicular;
    case K::EqualLength: return T::EqualLength;
    case K::EqualRadius: return T::EqualRadius;
    case K::PointOnLine: return T::PointOnLine;
    case K::PointOnCircle: return T::PointOnCircle;
    case K::Tangent: return T::Tangent;
    case K::Midpoint: return T::Midpoint;
    case K::Fixed: return T::Fixed;
    case K::Length: return T::Length;
    case K::Distance: return T::Distance;
    case K::HorizontalDistance: return T::HorizontalDistance;
    case K::VerticalDistance: return T::VerticalDistance;
    case K::Radius: return T::Radius;
    case K::Angle: return T::Angle;
    case K::Symmetric: return T::Symmetric;
  }
  return T::Coincident;
}

// MARK: Building

int Sketch::addPoint(const Vec2d& p) {
  points.push_back(p);
  return int(points.size()) - 1;
}

std::optional<int> Sketch::addLine(int a, int b) {
  if (a == b || !(distance(points[a], points[b]) > 1e-9)) return std::nullopt;
  lines.push_back(SketchLine{a, b, false});
  int l = int(lines.size()) - 1;
  Vec2d d = points[b] - points[a];
  if (std::abs(d.y) < 1e-9) constraints.push_back(SketchConstraint{K::Horizontal, l});
  else if (std::abs(d.x) < 1e-9) constraints.push_back(SketchConstraint{K::Vertical, l});
  return l;
}

void Sketch::addRectangle(const Vec2d& p0, const Vec2d& p1, std::optional<int> start, std::optional<int> end) {
  if (!(std::abs(p0.x - p1.x) > 1e-9) || !(std::abs(p0.y - p1.y) > 1e-9)) return;
  int c0 = start ? *start : addPoint(p0);
  int c1 = addPoint({p1.x, p0.y});
  int c2 = end ? *end : addPoint(p1);
  int c3 = addPoint({p0.x, p1.y});
  addLine(c0, c1);
  addLine(c1, c2);
  addLine(c2, c3);
  addLine(c3, c0);
}

void Sketch::addCircle(int center, double radius) {
  if (!(radius > 1e-9)) return;
  circles.push_back(SketchCircle{center, radius});
}

void Sketch::addArc(int center, int start, int end) {
  double r = distance(points[center], points[start]);
  if (!(r > 1e-9) || start == end) return;
  SketchCircle c{center, r};
  c.start = start;
  c.end = end;
  circles.push_back(c);
}

void Sketch::addCenterRectangle(const Vec2d& center, const Vec2d& corner, std::optional<int> centerIndex) {
  Vec2d d = corner - center;
  if (!(std::abs(d.x) > 1e-9) || !(std::abs(d.y) > 1e-9)) return;
  int c = centerIndex ? *centerIndex : addPoint(center);
  size_t firstLine = lines.size();
  addRectangle(center - d, center + d, std::nullopt, std::nullopt);
  // Keep the center point in the middle of a diagonal.
  int corner0 = lines[firstLine].a, corner2 = lines[firstLine + 1].b;
  int diagonal = int(lines.size());
  lines.push_back(SketchLine{corner0, corner2, true});
  constraints.push_back(SketchConstraint{K::Midpoint, c, diagonal});
}

void Sketch::addPolygon(const Vec2d& center, const Vec2d& corner, int sides, std::optional<int> centerIndex) {
  double r = distance(center, corner);
  if (!(r > 1e-9) || sides < 3) return;
  int c = centerIndex ? *centerIndex : addPoint(center);
  double start = std::atan2(corner.y - center.y, corner.x - center.x);
  std::vector<int> ids;
  for (int i = 0; i < sides; ++i) {
    double t = start + double(i) / double(sides) * 2 * kPi;
    ids.push_back(addPoint(center + Vec2d(std::cos(t), std::sin(t)) * r));
  }
  circles.push_back(SketchCircle{c, r, true});
  int ring = int(circles.size()) - 1;
  size_t first = lines.size();
  for (int i = 0; i < sides; ++i) addLine(ids[i], ids[(i + 1) % sides]);
  for (int i : ids) constraints.push_back(SketchConstraint{K::PointOnCircle, i, ring});
  for (size_t l = first + 1; l < lines.size(); ++l)
    constraints.push_back(SketchConstraint{K::EqualLength, int(first), int(l)});
}

std::optional<std::pair<double, double>> Sketch::arcAngles(int i) const {
  const auto& c = circles[i];
  if (!c.start || !c.end) return std::nullopt;
  Vec2d center = points[c.center];
  double a0 = std::atan2(points[*c.start].y - center.y, points[*c.start].x - center.x);
  double a1 = std::atan2(points[*c.end].y - center.y, points[*c.end].x - center.x);
  double sweep = a1 - a0;
  while (sweep <= 1e-9) sweep += 2 * kPi;
  return std::make_pair(a0, sweep);
}

// MARK: Constraints

double Sketch::measure(K kind, int a, int b) const {
  switch (kind) {
    case K::Length: return distance(points[lines[a].a], points[lines[a].b]);
    case K::Distance: return distance(points[a], points[b]);
    case K::HorizontalDistance: return points[b].x - points[a].x;
    case K::VerticalDistance: return points[b].y - points[a].y;
    case K::Radius: return circles[a].radius;
    case K::Angle: {
      Vec2d u = direction(a), v = direction(b);
      return std::atan2(u.x * v.y - u.y * v.x, dot(u, v)) * 180 / kPi;
    }
    default: return 0;
  }
}

std::optional<SketchStatus> Sketch::solve(const std::map<int, Vec2d>& drag) {
  namespace sk = o3d::sketch;
  sk::Sketch s;
  for (auto& p : points) s.points.push_back({p.x, p.y});
  for (auto& l : lines) s.lines.push_back({l.a, l.b});
  for (auto& c : circles) s.circles.push_back({c.center, c.radius});
  for (size_t i = 0; i < circles.size(); ++i) {
    const auto& c = circles[i];
    if (!c.start || !c.end) continue;
    s.constraints.push_back({sk::ConstraintType::PointOnCircle, *c.start, int(i), 0, 0});
    s.constraints.push_back({sk::ConstraintType::PointOnCircle, *c.end, int(i), 0, 0});
  }
  for (auto& c : constraints) {
    double value = c.kind == K::Radius ? std::max(c.value, 1e-6) : c.value;
    sk::Constraint k{bridged(c.kind), c.a, c.b, value, c.c ? 0.0 : c.value2};
    if (c.c) k.c = *c.c;
    s.constraints.push_back(k);
  }
  std::vector<sk::DragTarget> targets;
  for (auto& [p, t] : drag) targets.push_back({p, t.x, t.y});  // std::map: sorted by key
  auto result = sk::solve(s, {0}, targets);
  if (result.status != sk::SolveStatus::Solved) return std::nullopt;
  for (size_t i = 1; i < points.size(); ++i) points[i] = {s.points[i].x, s.points[i].y};
  for (size_t i = 0; i < circles.size(); ++i) circles[i].radius = std::max(s.circles[i].radius, 1e-6);
  SketchStatus status;
  status.degreesOfFreedom = result.degreesOfFreedom;
  for (size_t i = 0; i < points.size(); ++i)
    status.fixedPoints.push_back(i < result.pointFixed.size() && result.pointFixed[i]);
  for (size_t i = 0; i < circles.size(); ++i)
    status.fixedCircles.push_back(i < result.circleFixed.size() && result.circleFixed[i]);
  return status;
}

// MARK: Deleting

void Sketch::remove(const std::set<SketchRef>& refs) {
  using R = SketchRef::Kind;
  std::set<int> deadPoints, deadLines, deadCircles, deadConstraints, deadSplines, deadEllipses;
  for (auto& r : refs) {
    switch (r.kind) {
      case R::Point: if (r.index != 0) deadPoints.insert(r.index); break;
      case R::Line: deadLines.insert(r.index); break;
      case R::Circle: deadCircles.insert(r.index); break;
      case R::Constraint: deadConstraints.insert(r.index); break;
      case R::Spline: deadSplines.insert(r.index); break;
      case R::Ellipse: deadEllipses.insert(r.index); break;
    }
  }
  auto dead = [&](int p) { return deadPoints.count(p) > 0; };
  for (size_t i = 0; i < lines.size(); ++i)
    if (dead(lines[i].a) || dead(lines[i].b)) deadLines.insert(int(i));
  for (size_t i = 0; i < circles.size(); ++i) {
    const auto& c = circles[i];
    if (dead(c.center) || (c.start && dead(*c.start)) || (c.end && dead(*c.end))) deadCircles.insert(int(i));
  }
  std::vector<int> liveMarkers;
  for (int m : markers)
    if (!dead(m)) liveMarkers.push_back(m);
  for (size_t i = 0; i < splines.size(); ++i)
    if (std::any_of(splines[i].points.begin(), splines[i].points.end(), dead)) deadSplines.insert(int(i));
  for (size_t i = 0; i < ellipses.size(); ++i)
    if (dead(ellipses[i].center)) deadEllipses.insert(int(i));

  std::set<int> used{0};
  for (size_t i = 0; i < lines.size(); ++i)
    if (!deadLines.count(int(i))) { used.insert(lines[i].a); used.insert(lines[i].b); }
  for (size_t i = 0; i < circles.size(); ++i) {
    if (deadCircles.count(int(i))) continue;
    const auto& c = circles[i];
    used.insert(c.center);
    if (c.start && c.end) { used.insert(*c.start); used.insert(*c.end); }
  }
  used.insert(liveMarkers.begin(), liveMarkers.end());
  for (size_t i = 0; i < splines.size(); ++i)
    if (!deadSplines.count(int(i))) used.insert(splines[i].points.begin(), splines[i].points.end());
  for (size_t i = 0; i < ellipses.size(); ++i)
    if (!deadEllipses.count(int(i))) used.insert(ellipses[i].center);
  deadPoints.clear();
  for (size_t i = 0; i < points.size(); ++i)
    if (!used.count(int(i))) deadPoints.insert(int(i));

  auto remap = [](const std::set<int>& deadSet, size_t count) {
    std::vector<std::optional<int>> map;
    int next = 0;
    for (size_t i = 0; i < count; ++i) {
      if (deadSet.count(int(i))) map.push_back(std::nullopt);
      else map.push_back(next++);
    }
    return map;
  };
  auto pointMap = remap(deadPoints, points.size());
  auto lineMap = remap(deadLines, lines.size());
  auto circleMap = remap(deadCircles, circles.size());
  auto at = [](const std::vector<std::optional<int>>& m, int i) -> std::optional<int> {
    return i >= 0 && i < int(m.size()) ? m[i] : std::nullopt;
  };
  auto map = [&](SketchEntityKind kind, int i) -> std::optional<int> {
    switch (kind) {
      case SketchEntityKind::Point: return at(pointMap, i);
      case SketchEntityKind::Line: return at(lineMap, i);
      case SketchEntityKind::Circle: return at(circleMap, i);
    }
    return std::nullopt;
  };

  std::vector<SketchConstraint> newConstraints;
  for (size_t i = 0; i < constraints.size(); ++i) {
    if (deadConstraints.count(int(i))) continue;
    SketchConstraint c = constraints[i];
    auto [ka, kb] = arguments(c.kind);
    auto a = map(ka, c.a);
    if (!a) continue;
    c.a = *a;
    if (kb) {
      auto b = map(*kb, c.b);
      if (!b) continue;
      c.b = *b;
    }
    if (c.c) {
      auto l = at(lineMap, *c.c);
      if (!l) continue;
      c.c = *l;
    }
    newConstraints.push_back(c);
  }
  constraints = std::move(newConstraints);

  std::vector<SketchLine> newLines;
  for (size_t i = 0; i < lines.size(); ++i) {
    auto a = at(pointMap, lines[i].a), b = at(pointMap, lines[i].b);
    if (!lineMap[i] || !a || !b) continue;
    newLines.push_back(SketchLine{*a, *b, lines[i].construction});
  }
  lines = std::move(newLines);

  std::vector<SketchCircle> newCircles;
  for (size_t i = 0; i < circles.size(); ++i) {
    const auto& c = circles[i];
    auto center = at(pointMap, c.center);
    if (!circleMap[i] || !center) continue;
    SketchCircle out{*center, c.radius, c.construction};
    if (c.start && c.end) {
      auto ns = at(pointMap, *c.start), ne = at(pointMap, *c.end);
      if (!ns || !ne) continue;
      out.start = *ns;
      out.end = *ne;
    }
    newCircles.push_back(out);
  }
  circles = std::move(newCircles);

  std::vector<int> newMarkers;
  for (int m : liveMarkers)
    if (auto p = at(pointMap, m)) newMarkers.push_back(*p);
  markers = std::move(newMarkers);

  std::vector<SketchSpline> newSplines;
  for (size_t i = 0; i < splines.size(); ++i) {
    if (deadSplines.count(int(i))) continue;
    SketchSpline out = splines[i];
    out.points.clear();
    for (int p : splines[i].points)
      if (auto q = at(pointMap, p)) out.points.push_back(*q);
    if (out.points.size() == splines[i].points.size()) newSplines.push_back(out);
  }
  splines = std::move(newSplines);

  std::vector<SketchEllipse> newEllipses;
  for (size_t i = 0; i < ellipses.size(); ++i) {
    auto c = at(pointMap, ellipses[i].center);
    if (deadEllipses.count(int(i)) || !c) continue;
    SketchEllipse out = ellipses[i];
    out.center = *c;
    newEllipses.push_back(out);
  }
  ellipses = std::move(newEllipses);

  std::vector<Vec2d> newPoints;
  for (size_t i = 0; i < points.size(); ++i)
    if (pointMap[i]) newPoints.push_back(points[i]);
  points = std::move(newPoints);
}

// MARK: Profiles

std::vector<int> Sketch::coincidenceRoots() const {
  std::vector<int> parent(points.size());
  for (size_t i = 0; i < parent.size(); ++i) parent[i] = int(i);
  std::function<int(int)> root = [&](int i) {
    while (parent[i] != i) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  };
  int n = int(points.size());
  for (auto& c : constraints)
    if (c.kind == K::Coincident && c.a >= 0 && c.a < n && c.b >= 0 && c.b < n) parent[root(c.a)] = root(c.b);
  std::vector<int> flat(points.size());
  for (int i = 0; i < n; ++i) flat[i] = root(i);
  return flat;
}

std::pair<int, int> Sketch::ends(const Edge& e) const {
  switch (e.kind) {
    case EdgeKind::Line: return {lines[e.index].a, lines[e.index].b};
    case EdgeKind::Arc: return {*circles[e.index].start, *circles[e.index].end};
    case EdgeKind::Spline: return {splines[e.index].points.front(), splines[e.index].points.back()};
  }
  return {0, 0};
}

o3d::Segment Sketch::segment(const Edge& e, int from, const std::vector<int>& root) const {
  auto [a, b] = ends(e);
  bool forward = root[a] == root[from];
  Vec2d p = points[forward ? a : b], q = points[forward ? b : a];
  o3d::Segment s;
  switch (e.kind) {
    case EdgeKind::Line:
      s.kind = o3d::Segment::Kind::Line;
      s.a = toArr(p);
      s.b = toArr(q);
      break;
    case EdgeKind::Arc:
      s.kind = o3d::Segment::Kind::Arc;
      s.a = toArr(p);
      s.b = toArr(q);
      s.center = toArr(points[circles[e.index].center]);
      s.ccw = forward;
      break;
    case EdgeKind::Spline: {
      const auto& sp = splines[e.index];
      auto pts = sp.fitPoints(points);
      // A control-point curve starts and ends exactly on its end points.
      if (sp.controlPoints && !sp.points.empty()) {
        pts[0] = points[sp.points.front()];
        pts[pts.size() - 1] = points[sp.points.back()];
      }
      if (!forward) std::reverse(pts.begin(), pts.end());
      s.kind = o3d::Segment::Kind::Spline;
      for (auto& v : pts) s.points.push_back(toArr(v));
      if (!pts.empty()) {
        s.a = s.points.front();
        s.b = s.points.back();
      }
      s.closed = false;
      break;
    }
  }
  return s;
}

std::vector<Sketch::Chain> Sketch::chains() const {
  auto root = coincidenceRoots();
  std::vector<Edge> edges;
  for (size_t i = 0; i < lines.size(); ++i)
    if (!lines[i].construction) edges.push_back({EdgeKind::Line, int(i)});
  for (size_t i = 0; i < circles.size(); ++i)
    if (circles[i].isArc() && !circles[i].construction) edges.push_back({EdgeKind::Arc, int(i)});
  for (size_t i = 0; i < splines.size(); ++i)
    if (!splines[i].closed && !splines[i].construction && splines[i].points.size() >= 2)
      edges.push_back({EdgeKind::Spline, int(i)});
  std::map<int, std::vector<Edge>> adjacency;
  for (auto& e : edges) {
    auto [a, b] = ends(e);
    if (root[a] == root[b]) continue;
    adjacency[root[a]].push_back(e);
    adjacency[root[b]].push_back(e);
  }
  auto degree = [&](int v) {
    auto it = adjacency.find(v);
    return it == adjacency.end() ? 0 : int(it->second.size());
  };
  // Each connected piece without branches is one chain: open when it has
  // dead ends, a loop otherwise.
  std::vector<Chain> result;
  std::set<int> seen;
  for (auto& [vertex, _] : adjacency) {
    if (seen.count(vertex)) continue;
    std::vector<int> component, stack{vertex};
    seen.insert(vertex);
    while (!stack.empty()) {
      int v = stack.back();
      stack.pop_back();
      component.push_back(v);
      for (auto& e : adjacency[v]) {
        auto [a, b] = ends(e);
        int other = root[a] == v ? root[b] : root[a];
        if (seen.insert(other).second) stack.push_back(other);
      }
    }
    if (!std::all_of(component.begin(), component.end(), [&](int v) { return degree(v) <= 2; })) continue;
    std::vector<int> deadEnds;
    for (int v : component)
      if (degree(v) == 1) deadEnds.push_back(v);
    std::sort(deadEnds.begin(), deadEnds.end());
    int start = deadEnds.empty() ? *std::min_element(component.begin(), component.end()) : deadEnds.front();
    std::vector<Edge> chain;
    std::set<Edge> usedEdges;
    int v = start;
    while (true) {
      const Edge* next = nullptr;
      for (auto& e : adjacency[v])
        if (!usedEdges.count(e)) { next = &e; break; }
      if (!next) break;
      Edge e = *next;
      usedEdges.insert(e);
      chain.push_back(e);
      auto [a, b] = ends(e);
      v = root[a] == v ? root[b] : root[a];
    }
    bool closed = deadEnds.empty() && chain.size() >= 2;
    result.push_back(Chain{chain, start, closed});
  }
  return result;
}

std::vector<o3d::Profile> Sketch::profiles() const {
  auto root = coincidenceRoots();
  std::vector<o3d::Profile> result;
  for (auto& chain : chains()) {
    if (!chain.closed) continue;
    int v = chain.start;
    std::vector<o3d::Segment> segments;
    std::vector<Vec2d> corners;
    bool allLines = true;
    for (auto& e : chain.edges) {
      segments.push_back(segment(e, v, root));
      corners.push_back(points[v]);
      if (e.kind != EdgeKind::Line) allLines = false;
      auto [a, b] = ends(e);
      v = root[a] == v ? root[b] : root[a];
    }
    o3d::Profile p;
    if (allLines) {
      if (corners.size() < 3) continue;
      p.kind = o3d::Profile::Kind::Polygon;
      for (auto& c : corners) p.points.push_back(toArr(c));
    } else {
      p.kind = o3d::Profile::Kind::Wire;
      p.segments = segments;
    }
    result.push_back(p);
  }
  for (auto& c : circles) {
    if (c.construction || c.isArc() || !(c.radius > 1e-9)) continue;
    o3d::Profile p;
    p.kind = o3d::Profile::Kind::Circle;
    p.cu = points[c.center].x;
    p.cv = points[c.center].y;
    p.radius = c.radius;
    result.push_back(p);
  }
  for (auto& sp : splines) {
    if (!sp.closed || sp.construction || sp.points.size() < 3) continue;
    auto pts = sp.fitPoints(points);
    o3d::Segment s;
    s.kind = o3d::Segment::Kind::Spline;
    for (auto& q : pts) s.points.push_back(toArr(q));
    if (!pts.empty()) { s.a = s.points.front(); s.b = s.points.back(); }
    s.closed = true;
    o3d::Profile p;
    p.kind = o3d::Profile::Kind::Wire;
    p.segments = {s};
    result.push_back(p);
  }
  for (auto& e : ellipses) {
    if (e.construction || !(e.majorRadius > 1e-9) || !(e.minorRadius > 1e-9)) continue;
    o3d::Segment s;
    s.kind = o3d::Segment::Kind::Ellipse;
    s.center = toArr(points[e.center]);
    s.majorRadius = e.majorRadius;
    s.minorRadius = e.minorRadius;
    s.rotation = e.rotation;
    o3d::Profile p;
    p.kind = o3d::Profile::Kind::Wire;
    p.segments = {s};
    result.push_back(p);
  }
  return result;
}

std::vector<o3d::Segment> Sketch::path() const {
  auto root = coincidenceRoots();
  auto all = chains();
  const Chain* chain = nullptr;
  for (auto& c : all)
    if (!c.closed) { chain = &c; break; }
  if (!chain && !all.empty()) chain = &all.front();
  if (!chain) return {};
  int v = chain->start;
  std::vector<o3d::Segment> out;
  for (auto& e : chain->edges) {
    out.push_back(segment(e, v, root));
    auto [a, b] = ends(e);
    v = root[a] == v ? root[b] : root[a];
  }
  return out;
}

}  // namespace sculpt
