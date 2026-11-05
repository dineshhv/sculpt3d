// Port of SketchEditing.swift and the SketchTool enum from ModelStore.swift.
#include <algorithm>
#include <cmath>

#include "sculpt/model/DisplayFormat.hpp"
#include "sculpt/model/SketchSession.hpp"

namespace sculpt {

using K = SketchConstraint::Kind;
using RK = SketchRef::Kind;

// MARK: SketchTool

const std::vector<SketchTool>& allSketchTools() {
  using T = SketchTool;
  static const std::vector<T> all = {T::select, T::line, T::arc, T::spline, T::controlSpline, T::rectangle,
                                     T::centerRectangle, T::circle, T::ellipse, T::polygon, T::point, T::trim};
  return all;
}

std::string rawValue(SketchTool t) {
  switch (t) {
    case SketchTool::select: return "select";
    case SketchTool::line: return "line";
    case SketchTool::arc: return "arc";
    case SketchTool::spline: return "spline";
    case SketchTool::controlSpline: return "controlSpline";
    case SketchTool::rectangle: return "rectangle";
    case SketchTool::centerRectangle: return "centerRectangle";
    case SketchTool::circle: return "circle";
    case SketchTool::ellipse: return "ellipse";
    case SketchTool::polygon: return "polygon";
    case SketchTool::point: return "point";
    case SketchTool::trim: return "trim";
  }
  return "select";
}

std::optional<SketchTool> sketchTool(const std::string& raw) {
  for (auto t : allSketchTools())
    if (rawValue(t) == raw) return t;
  return std::nullopt;
}

std::string title(SketchTool t) {
  switch (t) {
    case SketchTool::centerRectangle: return "Center Rect";
    case SketchTool::controlSpline: return "Control Spline";
    case SketchTool::arc: return "Arc";
    default: {
      std::string s = rawValue(t);
      s[0] = char(s[0] - 'a' + 'A');
      return s;
    }
  }
}

std::string symbol(SketchTool t) {
  switch (t) {
    case SketchTool::select: return "cursorarrow";
    case SketchTool::line: return "line.diagonal";
    case SketchTool::arc: return "point.topleft.down.to.point.bottomright.curvepath";
    case SketchTool::rectangle: return "rectangle";
    case SketchTool::centerRectangle: return "rectangle.center.inset.filled";
    case SketchTool::circle: return "circle";
    case SketchTool::polygon: return "hexagon";
    case SketchTool::point: return "smallcircle.filled.circle";
    case SketchTool::trim: return "scissors";
    case SketchTool::spline: return "scribble";
    case SketchTool::controlSpline: return "point.topleft.down.to.point.bottomright.filled.curvepath";
    case SketchTool::ellipse: return "oval";
  }
  return "";
}

bool drags(SketchTool t) {
  return t == SketchTool::rectangle || t == SketchTool::centerRectangle || t == SketchTool::circle ||
         t == SketchTool::ellipse || t == SketchTool::polygon;
}

char key(SketchTool t) {
  switch (t) {
    case SketchTool::select: return 's';
    case SketchTool::line: return 'l';
    case SketchTool::arc: return 'a';
    case SketchTool::spline: return 'i';
    case SketchTool::rectangle: return 'r';
    case SketchTool::circle: return 'c';
    case SketchTool::ellipse: return 'e';
    case SketchTool::polygon: return 'g';
    case SketchTool::point: return 'p';
    case SketchTool::trim: return 't';
    default: return 0;
  }
}

// MARK: Selection

static std::vector<int> selected(const std::vector<SketchRef>& selection, RK kind) {
  std::vector<int> out;
  for (auto& r : selection)
    if (r.kind == kind) out.push_back(r.index);
  return out;
}

std::vector<int> SketchSession::selectedPoints() const { return selected(selection, RK::Point); }
std::vector<int> SketchSession::selectedLines() const { return selected(selection, RK::Line); }
std::vector<int> SketchSession::selectedCircles() const { return selected(selection, RK::Circle); }
std::vector<int> SketchSession::selectedSplines() const { return selected(selection, RK::Spline); }
std::vector<int> SketchSession::selectedEllipses() const { return selected(selection, RK::Ellipse); }
std::vector<int> SketchSession::selectedConstraints() const { return selected(selection, RK::Constraint); }

std::optional<int> SketchSession::selectedDimension() const {
  auto cs = selectedConstraints();
  if (selection.size() != 1 || cs.empty()) return std::nullopt;
  int i = cs.front();
  if (i < 0 || i >= int(sketch.constraints.size()) || !isDimension(sketch.constraints[i].kind)) return std::nullopt;
  return i;
}

std::vector<K> SketchSession::availableConstraints() const {
  size_t p = selectedPoints().size(), l = selectedLines().size(), c = selectedCircles().size();
  if (!selectedConstraints().empty() || !selectedSplines().empty() || !selectedEllipses().empty()) return {};
  if (p == 0 && l == 1 && c == 0) return {K::Horizontal, K::Vertical, K::Length};
  if (p == 0 && l == 2 && c == 0) return {K::Parallel, K::Perpendicular, K::EqualLength, K::Angle};
  if (p == 0 && c == 0 && l > 2) return {K::Horizontal, K::Vertical};
  if (p == 2 && l == 0 && c == 0)
    return {K::Coincident, K::Distance, K::HorizontalDistance, K::VerticalDistance, K::Fixed};
  if (p == 2 && l == 1 && c == 0) return {K::Symmetric};
  if (l == 0 && c == 0 && p > 0) return {K::Fixed};
  if (p == 1 && l == 1 && c == 0) return {K::PointOnLine, K::Midpoint};
  if (p == 1 && l == 0 && c == 1) return {K::PointOnCircle};
  if (p == 0 && l == 1 && c == 1) return {K::Tangent};
  if (p == 0 && l == 0 && c == 1) return {K::Radius};
  if (p == 0 && l == 0 && c == 2) return {K::EqualRadius, K::Coincident};
  return {};
}

void SketchSession::resolve() {
  if (auto s = sketch.solve()) status = *s;
}

static std::string lowercased(std::string s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return s;
}

std::optional<std::string> SketchSession::constrain(K kind) {
  auto pts = selectedPoints(), lns = selectedLines(), crs = selectedCircles();
  std::vector<SketchConstraint> added;
  auto make = [](K k, int a, int b = -1) {
    SketchConstraint c;
    c.kind = k;
    c.a = a;
    c.b = b;
    return c;
  };
  switch (kind) {
    case K::Horizontal: case K::Vertical:
      for (int l : lns) added.push_back(make(kind, l));
      break;
    case K::Parallel: case K::Perpendicular: case K::EqualLength: case K::Angle:
      if (lns.size() != 2) return std::nullopt;
      added = {make(kind, lns[0], lns[1])};
      break;
    case K::EqualRadius:
      if (crs.size() != 2) return std::nullopt;
      added = {make(kind, crs[0], crs[1])};
      break;
    case K::Coincident:
      if (crs.size() == 2) added = {make(kind, sketch.circles[crs[0]].center, sketch.circles[crs[1]].center)};
      else if (pts.size() == 2) added = {make(kind, pts[0], pts[1])};
      break;
    case K::PointOnLine: case K::Midpoint:
      if (pts.empty() || lns.empty()) return std::nullopt;
      added = {make(kind, pts[0], lns[0])};
      break;
    case K::PointOnCircle:
      if (pts.empty() || crs.empty()) return std::nullopt;
      added = {make(kind, pts[0], crs[0])};
      break;
    case K::Tangent:
      if (lns.empty() || crs.empty()) return std::nullopt;
      added = {make(kind, lns[0], crs[0])};
      break;
    case K::Fixed:
      for (int p : pts) {
        if (p == 0) continue;
        auto c = make(K::Fixed, p);
        c.value = sketch.points[p].x;
        c.value2 = sketch.points[p].y;
        added.push_back(c);
      }
      break;
    case K::Length:
      if (lns.empty()) return std::nullopt;
      added = {make(kind, lns[0])};
      break;
    case K::Distance: case K::HorizontalDistance: case K::VerticalDistance:
      if (pts.size() != 2) return std::nullopt;
      added = {make(kind, pts[0], pts[1])};
      break;
    case K::Radius:
      if (crs.empty()) return std::nullopt;
      added = {make(kind, crs[0])};
      break;
    case K::Symmetric:
      if (pts.size() != 2 || lns.empty()) return std::nullopt;
      added = {make(kind, pts[0], pts[1])};
      added[0].c = lns[0];
      break;
  }
  added.erase(std::remove_if(added.begin(), added.end(),
                             [&](const SketchConstraint& n) {
                               return std::any_of(sketch.constraints.begin(), sketch.constraints.end(), [&](auto& e) {
                                 return e.kind == n.kind && e.a == n.a && e.b == n.b && e.c == n.c;
                               });
                             }),
              added.end());
  if (added.empty()) return std::string("Already constrained");
  if (isDimension(kind))
    for (auto& c : added) c.value = sketch.measure(kind, c.a, c.b);

  Sketch trial = sketch;
  trial.constraints.insert(trial.constraints.end(), added.begin(), added.end());
  auto newStatus = trial.solve();
  if (!newStatus) return title(kind) + " conflicts with the existing constraints";
  // A constraint that removes no freedom is redundant; it would only conflict
  // later when something else changes.
  Sketch current = sketch;
  if (auto before = current.solve())
    if (newStatus->degreesOfFreedom >= before->degreesOfFreedom)
      return "Already constrained — " + lowercased(title(kind)) + " adds nothing";
  sketch = trial;
  status = *newStatus;
  if (isDimension(kind)) selection = {SketchRef::constraint(int(sketch.constraints.size()) - 1)};
  else selection.clear();
  return std::nullopt;
}

std::optional<std::string> SketchSession::setDimension(int index, double value) {
  if (index < 0 || index >= int(sketch.constraints.size())) return std::nullopt;
  K kind = sketch.constraints[index].kind;
  if ((kind == K::Length || kind == K::Distance || kind == K::Radius) && value <= 0)
    return title(kind) + " must be positive";
  Sketch trial = sketch;
  trial.constraints[index].value = value;
  auto s = trial.solve();
  if (!s) return "Can't make the " + lowercased(title(kind)) + " " + formatted(value);
  sketch = trial;
  status = *s;
  return std::nullopt;
}

std::string SketchSession::label(const SketchConstraint& c) const {
  switch (c.kind) {
    case K::Radius: return "R" + formatted(c.value);
    case K::Angle: return formatted(c.value) + "°";
    default: return formatted(std::abs(c.value));
  }
}

SketchSession::Anchor SketchSession::anchor(const SketchConstraint& c, double offset) const {
  const Sketch& s = sketch;
  auto normal = [&](int line) {
    Vec2d d = s.direction(line);
    double len = length(d);
    return len > 1e-9 ? Vec2d(-d.y, d.x) / len : Vec2d(0, 1);
  };
  switch (c.kind) {
    case K::Length: {
      Vec2d n = normal(c.a) * (offset * 2);
      Vec2d a = s.points[s.lines[c.a].a] + n, b = s.points[s.lines[c.a].b] + n;
      return {(a + b) / 2.0, {a, b}};
    }
    case K::Distance: {
      Vec2d a = s.points[c.a], b = s.points[c.b];
      return {(a + b) / 2.0, {a, b}};
    }
    case K::HorizontalDistance: {
      Vec2d a = s.points[c.a], b = s.points[c.b];
      double y = std::max(a.y, b.y) + offset * 2;
      return {{(a.x + b.x) / 2, y}, {{a.x, y}, {b.x, y}}};
    }
    case K::VerticalDistance: {
      Vec2d a = s.points[c.a], b = s.points[c.b];
      double x = std::max(a.x, b.x) + offset * 2;
      return {{x, (a.y + b.y) / 2}, {{x, a.y}, {x, b.y}}};
    }
    case K::Radius: {
      const auto& circle = s.circles[c.a];
      Vec2d center = s.points[circle.center];
      // On an arc, point the leader at the middle of the arc.
      auto arc = s.arcAngles(c.a);
      double t = arc ? arc->first + arc->second / 2 : kPi / 4;
      Vec2d edge = center + Vec2d(std::cos(t), std::sin(t)) * circle.radius;
      return {center + (edge - center) * 0.55, {center, edge}};
    }
    case K::Angle: return {(s.midpoint(c.a) + s.midpoint(c.b)) / 2.0, {}};
    case K::Horizontal: case K::Vertical: case K::EqualLength: case K::Parallel: case K::Perpendicular: case K::Tangent:
      return {s.midpoint(c.a) + normal(c.a) * offset, {}};
    case K::EqualRadius: {
      const auto& circle = s.circles[c.a];
      return {s.points[circle.center] + Vec2d(0, circle.radius + offset), {}};
    }
    case K::PointOnLine: case K::PointOnCircle: case K::Midpoint: case K::Fixed: case K::Coincident:
      return {s.points[c.a] + Vec2d(offset, offset), {}};
    case K::Symmetric: return {(s.points[c.a] + s.points[c.b]) / 2.0 + Vec2d(offset, offset), {}};
  }
  return {};
}

std::string SketchSession::escapeTitle() const {
  if (chainEnd) return "End line";
  if (!pending.empty()) return "Cancel arc";
  if (!splineDraft.empty()) return "End spline";
  if (!selection.empty()) return "Deselect";
  return "Cancel";
}

}  // namespace sculpt
