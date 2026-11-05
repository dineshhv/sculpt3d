// Port of SketchTransforms.swift.
#include <algorithm>
#include <cmath>

#include "sculpt/model/Sketch.hpp"

namespace sculpt {

using K = SketchConstraint::Kind;
using RK = SketchRef::Kind;

Vec2d SketchTransform::apply(const Vec2d& p) const {
  Vec2d d = p - pivot;
  if (mirrorAxis) {
    Vec2d u = normalize(*mirrorAxis);
    d = u * (2 * dot(d, u)) - d;
  }
  d *= scale;
  double c = std::cos(angle), s = std::sin(angle);
  return pivot + Vec2d(d.x * c - d.y * s, d.x * s + d.y * c) + shift;
}

bool SketchTransform::keepsAxes() const {
  if (angle != 0) return false;
  if (!mirrorAxis) return true;
  return std::abs(mirrorAxis->x) < 1e-12 || std::abs(mirrorAxis->y) < 1e-12;
}

std::set<int> Sketch::pointsOf(const std::vector<SketchRef>& refs) const {
  std::set<int> used;
  for (auto& r : refs) {
    switch (r.kind) {
      case RK::Point: used.insert(r.index); break;
      case RK::Line: used.insert(lines[r.index].a); used.insert(lines[r.index].b); break;
      case RK::Circle: {
        const auto& c = circles[r.index];
        used.insert(c.center);
        if (c.start && c.end) { used.insert(*c.start); used.insert(*c.end); }
        break;
      }
      case RK::Spline: used.insert(splines[r.index].points.begin(), splines[r.index].points.end()); break;
      case RK::Ellipse: used.insert(ellipses[r.index].center); break;
      case RK::Constraint: break;
    }
  }
  return used;
}

Vec2d Sketch::centroid(const std::vector<SketchRef>& refs) const {
  auto used = pointsOf(refs);
  if (used.empty()) return {0, 0};
  Vec2d sum;
  for (int i : used) sum += points[i];
  return sum / double(used.size());
}

std::vector<SketchRef> Sketch::addCopy(const std::vector<SketchRef>& refs, const SketchTransform& t) {
  std::vector<SketchRef> geometry;
  for (auto& r : refs)
    if (r.kind != RK::Constraint) geometry.push_back(r);
  std::map<int, int> map;
  for (int p : pointsOf(geometry)) map[p] = addPoint(t.apply(points[p]));
  std::map<int, int> lineMap, circleMap;
  std::vector<SketchRef> created;
  for (auto& r : geometry) {
    switch (r.kind) {
      case RK::Point: {
        auto it = map.find(r.index);
        if (it != map.end()) {
          markers.push_back(it->second);
          created.push_back(SketchRef::point(it->second));
        }
        break;
      }
      case RK::Line: {
        SketchLine l = lines[r.index];
        l.a = map.at(l.a);
        l.b = map.at(l.b);
        lines.push_back(l);
        lineMap[r.index] = int(lines.size()) - 1;
        created.push_back(SketchRef::line(int(lines.size()) - 1));
        break;
      }
      case RK::Circle: {
        SketchCircle c = circles[r.index];
        c.center = map.at(c.center);
        c.radius *= t.scale;
        if (c.start && c.end) {
          // A mirrored arc runs the other way round.
          int s = *c.start, e = *c.end;
          c.start = map.at(t.flips() ? e : s);
          c.end = map.at(t.flips() ? s : e);
        }
        circles.push_back(c);
        circleMap[r.index] = int(circles.size()) - 1;
        created.push_back(SketchRef::circle(int(circles.size()) - 1));
        break;
      }
      case RK::Spline: {
        SketchSpline sp = splines[r.index];
        for (auto& p : sp.points) p = map.at(p);
        splines.push_back(sp);
        created.push_back(SketchRef::spline(int(splines.size()) - 1));
        break;
      }
      case RK::Ellipse: {
        SketchEllipse e = ellipses[r.index];
        e.center = map.at(e.center);
        e.majorRadius *= t.scale;
        e.minorRadius *= t.scale;
        if (t.mirrorAxis) e.rotation = 2 * std::atan2(t.mirrorAxis->y, t.mirrorAxis->x) - e.rotation;
        e.rotation += t.angle;
        ellipses.push_back(e);
        created.push_back(SketchRef::ellipse(int(ellipses.size()) - 1));
        break;
      }
      case RK::Constraint: break;
    }
  }
  // Carry over constraints whose arguments were all copied.
  std::vector<SketchConstraint> existing = constraints;
  for (auto& c : existing) {
    if (c.c) continue;
    auto remap = [&](int index, std::optional<SketchEntityKind> kind) -> std::optional<int> {
      if (!kind) return -1;
      const std::map<int, int>* m = nullptr;
      switch (*kind) {
        case SketchEntityKind::Point: m = &map; break;
        case SketchEntityKind::Line: m = &lineMap; break;
        case SketchEntityKind::Circle: m = &circleMap; break;
      }
      auto it = m->find(index);
      if (it == m->end()) return std::nullopt;
      return it->second;
    };
    auto [ka, kb] = arguments(c.kind);
    auto a = remap(c.a, ka), b = remap(c.b, kb);
    if (!a || !b) continue;
    switch (c.kind) {
      case K::Horizontal: case K::Vertical: case K::HorizontalDistance: case K::VerticalDistance: case K::Angle:
        if (!t.keepsAxes()) continue;
        break;
      case K::Fixed: continue;
      default: break;
    }
    SketchConstraint copy = c;
    copy.a = *a;
    copy.b = kb ? *b : c.b;
    if (c.kind == K::Length || c.kind == K::Distance || c.kind == K::HorizontalDistance ||
        c.kind == K::VerticalDistance || c.kind == K::Radius)
      copy.value *= t.scale;
    constraints.push_back(copy);
  }
  return created;
}

std::vector<SketchRef> Sketch::transform(const std::vector<SketchRef>& refs, const SketchTransform& t) {
  std::vector<SketchRef> geometry;
  for (auto& r : refs)
    if (r.kind != RK::Constraint) geometry.push_back(r);
  auto created = addCopy(geometry, t);
  std::set<SketchRef> doomed;
  for (auto& r : geometry)
    if (r.kind != RK::Point) doomed.insert(r);
  remove(doomed);
  // Deleting compacts the arrays in order, so the copies are still the last
  // entities of each kind.
  auto count = [&](RK k) { return int(std::count_if(created.begin(), created.end(), [&](auto& r) { return r.kind == k; })); };
  int nl = count(RK::Line), nc = count(RK::Circle), ns = count(RK::Spline), ne = count(RK::Ellipse);
  std::vector<SketchRef> out;
  for (int i = int(lines.size()) - nl; i < int(lines.size()); ++i) out.push_back(SketchRef::line(i));
  for (int i = int(circles.size()) - nc; i < int(circles.size()); ++i) out.push_back(SketchRef::circle(i));
  for (int i = int(splines.size()) - ns; i < int(splines.size()); ++i) out.push_back(SketchRef::spline(i));
  for (int i = int(ellipses.size()) - ne; i < int(ellipses.size()); ++i) out.push_back(SketchRef::ellipse(i));
  return out;
}

}  // namespace sculpt
