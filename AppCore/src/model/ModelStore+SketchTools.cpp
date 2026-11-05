// Port of ModelStore+SketchTools.swift: Sketch ▸ Scale / Pattern / Rotate / Mirror / Text.
#include <cmath>

#include "sculpt/model/ModelStore.hpp"

namespace sculpt {

std::string rawValue(SketchTransformKind k) {
  switch (k) {
    case SketchTransformKind::scale: return "scale";
    case SketchTransformKind::linearPattern: return "linearPattern";
    case SketchTransformKind::circularPattern: return "circularPattern";
    case SketchTransformKind::rotate: return "rotate";
  }
  return "";
}

std::string title(SketchTransformKind k) {
  switch (k) {
    case SketchTransformKind::scale: return "Scale Sketch";
    case SketchTransformKind::linearPattern: return "Pattern - Linear";
    case SketchTransformKind::circularPattern: return "Pattern - Circular";
    case SketchTransformKind::rotate: return "Rotate Sketch Around Axis";
  }
  return "";
}

std::pair<std::vector<SketchRef>, std::optional<Vec2d>> ModelStore::sketchTargets(const SketchSession& s) const {
  std::vector<SketchRef> geometry, points;
  std::optional<int> point;
  for (auto& r : s.selection) {
    if (r.kind == SketchRef::Kind::Point) {
      points.push_back(r);
      point = r.index;
    } else if (r.kind != SketchRef::Kind::Constraint) {
      geometry.push_back(r);
    }
  }
  // A lone point is geometry to move; with other geometry it's the pivot.
  if (geometry.empty()) return {points, std::nullopt};
  std::optional<Vec2d> pivot;
  if (point) pivot = s.sketch.points[size_t(*point)];
  return {geometry, pivot};
}

bool ModelStore::canTransformSketch() const { return sketch_ && !sketchTargets(*sketch_).first.empty(); }

void ModelStore::transformSketchSelection(SketchTransformKind kind, const SketchTransformParams& p) {
  if (!sketch_) return;
  SketchSession s = *sketch_;
  auto [geometry, pivot] = sketchTargets(s);
  if (geometry.empty()) return setToast(std::string("Select sketch geometry first"));
  std::vector<SketchRef> created;
  switch (kind) {
    case SketchTransformKind::scale: {
      if (!(p.factor > 1e-6)) return setToast(std::string("The scale must be more than 0"));
      SketchTransform t;
      t.pivot = pivot ? *pivot : s.sketch.centroid(geometry);
      t.scale = p.factor;
      created = p.keepOriginal ? s.sketch.addCopy(geometry, t) : s.sketch.transform(geometry, t);
      break;
    }
    case SketchTransformKind::rotate: {
      SketchTransform t;
      t.pivot = pivot.value_or(Vec2d());
      t.angle = p.angle * kPi / 180;
      created = p.keepOriginal ? s.sketch.addCopy(geometry, t) : s.sketch.transform(geometry, t);
      break;
    }
    case SketchTransformKind::linearPattern: {
      if (p.count < 2) return setToast(std::string("A pattern needs at least 2 instances"));
      double a = p.direction * kPi / 180;
      Vec2d d(std::cos(a), std::sin(a));
      for (int k = 1; k < p.count; ++k) {
        SketchTransform t;
        t.shift = d * (p.spacing * double(k));
        auto made = s.sketch.addCopy(geometry, t);
        created.insert(created.end(), made.begin(), made.end());
      }
      break;
    }
    case SketchTransformKind::circularPattern: {
      if (p.count < 2) return setToast(std::string("A pattern needs at least 2 instances"));
      // A full turn spreads the copies evenly without repeating the original.
      bool full = std::abs(std::abs(p.totalAngle) - 360) < 1e-6;
      double step = p.totalAngle / double(full ? p.count : p.count - 1) * kPi / 180;
      for (int k = 1; k < p.count; ++k) {
        SketchTransform t;
        t.pivot = pivot.value_or(Vec2d());
        t.angle = step * double(k);
        auto made = s.sketch.addCopy(geometry, t);
        created.insert(created.end(), made.begin(), made.end());
      }
      break;
    }
  }
  s.selection = created;
  s.resolve();
  setSketch(std::move(s));
}

void ModelStore::mirrorSketchSelection() {
  if (!sketch_) return;
  SketchSession s = *sketch_;
  auto geometry = sketchTargets(s).first;
  Vec2d pivot(0, 0), axis(0, 1);
  std::optional<int> mirrorLine;
  for (auto it = s.selection.rbegin(); it != s.selection.rend(); ++it)
    if (it->kind == SketchRef::Kind::Line) { mirrorLine = it->index; break; }
  if (geometry.size() >= 2 && mirrorLine) {
    int l = *mirrorLine;
    geometry.erase(std::remove(geometry.begin(), geometry.end(), SketchRef::line(l)), geometry.end());
    const auto& line = s.sketch.lines[size_t(l)];
    pivot = s.sketch.points[size_t(line.a)];
    axis = s.sketch.points[size_t(line.b)] - pivot;
  }
  if (geometry.empty() || !(length(axis) > 1e-9))
    return setToast(std::string("Select geometry, then a line to mirror across"));
  SketchTransform t;
  t.pivot = pivot;
  t.mirrorAxis = axis;
  s.selection = s.sketch.addCopy(geometry, t);
  s.resolve();
  setSketch(std::move(s));
}

// MARK: Text

static std::vector<Vec2d> quadSamples(const Vec2d& p0, const Vec2d& c, const Vec2d& p1) {
  std::vector<Vec2d> out;
  for (int k = 1; k <= 4; ++k) {
    double t = double(k) / 4, u = 1 - t;
    out.push_back(p0 * (u * u) + c * (2 * u * t) + p1 * (t * t));
  }
  return out;
}

static std::vector<Vec2d> cubicSamples(const Vec2d& p0, const Vec2d& c1, const Vec2d& c2, const Vec2d& p1) {
  std::vector<Vec2d> out;
  for (int k = 1; k <= 5; ++k) {
    double t = double(k) / 5, u = 1 - t;
    out.push_back(p0 * (u * u * u) + c1 * (3 * u * u * t) + c2 * (3 * u * t * t) + p1 * (t * t * t));
  }
  return out;
}

int ModelStore::addOutline(const std::vector<PathElement>& path, Sketch& sketch) const {
  std::optional<int> start, current;
  Vec2d currentPoint;
  int added = 0;
  auto close = [&]() {
    if (current && start && *current != *start) {
      if (distance(sketch.points[size_t(*current)], sketch.points[size_t(*start)]) > 1e-6) {
        sketch.addLine(*current, *start);
        ++added;
      } else {
        SketchConstraint c;
        c.kind = SketchConstraint::Kind::Coincident;
        c.a = *current;
        c.b = *start;
        sketch.constraints.push_back(c);
      }
    }
    start.reset();
    current.reset();
  };
  auto curve = [&](const std::vector<Vec2d>& samples) {
    if (!current) return;
    Vec2d end = samples.back();
    int last;
    if (start && *start != *current && distance(end, sketch.points[size_t(*start)]) < 1e-6) last = *start;
    else last = sketch.addPoint(end);
    SketchSpline sp;
    sp.points.push_back(*current);
    for (size_t i = 0; i + 1 < samples.size(); ++i) sp.points.push_back(sketch.addPoint(samples[i]));
    sp.points.push_back(last);
    sketch.splines.push_back(sp);
    current = last;
    ++added;
  };
  for (auto& e : path) {
    switch (e.kind) {
      case PathElement::Kind::moveTo:
        if (current) close();
        currentPoint = e.p[0];
        start = sketch.addPoint(currentPoint);
        current = start;
        break;
      case PathElement::Kind::lineTo: {
        if (!current) break;
        Vec2d p = e.p[0];
        if (std::hypot(p.x - currentPoint.x, p.y - currentPoint.y) < 1e-6) break;
        // Lines ending on the contour's start close onto it.
        int target;
        if (start && distance(p, sketch.points[size_t(*start)]) < 1e-6) target = *start;
        else target = sketch.addPoint(p);
        sketch.addLine(*current, target);
        current = target;
        currentPoint = p;
        ++added;
        break;
      }
      case PathElement::Kind::quadTo:
        curve(quadSamples(currentPoint, e.p[0], e.p[1]));
        currentPoint = e.p[1];
        break;
      case PathElement::Kind::cubicTo:
        curve(cubicSamples(currentPoint, e.p[0], e.p[1], e.p[2]));
        currentPoint = e.p[2];
        break;
      case PathElement::Kind::close: close(); break;
    }
  }
  if (current) close();
  return added;
}

void ModelStore::addSketchOutlines(const std::vector<PathElement>& path) {
  if (!sketch_ || path.empty()) return;
  SketchSession s = *sketch_;
  size_t lines0 = s.sketch.lines.size(), splines0 = s.sketch.splines.size();
  int added = addOutline(path, s.sketch);
  if (added == 0) return setToast(std::string("That text has no outlines"));
  // Select the new outlines so they can be scaled or moved right away.
  s.selection.clear();
  for (size_t i = lines0; i < s.sketch.lines.size(); ++i) s.selection.push_back(SketchRef::line(int(i)));
  for (size_t i = splines0; i < s.sketch.splines.size(); ++i) s.selection.push_back(SketchRef::spline(int(i)));
  s.resolve();
  setSketch(std::move(s));
}

}  // namespace sculpt
