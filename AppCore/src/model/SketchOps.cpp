// Port of SketchOps.swift: trim, offset, corner fillet and disconnect.
#include <algorithm>
#include <cmath>

#include "sculpt/model/Sketch.hpp"

namespace sculpt {

using K = SketchConstraint::Kind;

// MARK: Intersections

std::vector<double> Sketch::cutsOnLine(int l) const {
  Vec2d a = points[lines[l].a], d = points[lines[l].b] - a;
  std::vector<double> ts;
  for (size_t j = 0; j < lines.size(); ++j) {
    if (int(j) == l) continue;
    Vec2d p = points[lines[j].a], e = points[lines[j].b] - p;
    double den = d.x * e.y - d.y * e.x;
    if (!(std::abs(den) > 1e-12)) continue;
    Vec2d w = p - a;
    double t = (w.x * e.y - w.y * e.x) / den, u = (w.x * d.y - w.y * d.x) / den;
    if (u >= -1e-9 && u <= 1 + 1e-9) ts.push_back(t);
  }
  for (size_t j = 0; j < circles.size(); ++j)
    for (double t : lineCircle(a, d, int(j)))
      if (onCurve(int(j), a + d * t)) ts.push_back(t);
  std::vector<double> out;
  for (double t : ts)
    if (t > 1e-6 && t < 1 - 1e-6) out.push_back(t);
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<double> Sketch::cutsOnCircle(int i) const {
  Vec2d c = points[circles[i].center];
  double r = circles[i].radius;
  std::vector<Vec2d> hits;
  for (auto& m : lines) {
    Vec2d a = points[m.a], d = points[m.b] - a;
    for (double t : lineCircle(a, d, i))
      if (t >= -1e-9 && t <= 1 + 1e-9) hits.push_back(a + d * t);
  }
  for (size_t j = 0; j < circles.size(); ++j) {
    if (int(j) == i) continue;
    Vec2d c2 = points[circles[j].center];
    double r2 = circles[j].radius;
    double dist = distance(c, c2);
    if (!(dist > 1e-9) || !(dist <= r + r2) || !(dist >= std::abs(r - r2))) continue;
    double along = (r * r - r2 * r2 + dist * dist) / (2 * dist);
    double h = std::sqrt(std::max(r * r - along * along, 0.0));
    Vec2d base = c + (c2 - c) / dist * along, n = Vec2d(-(c2 - c).y, (c2 - c).x) / dist;
    for (Vec2d q : {base + n * h, base - n * h})
      if (onCurve(int(j), q)) hits.push_back(q);
  }
  std::vector<double> out;
  for (auto& h : hits)
    if (onCurve(i, h)) out.push_back(std::atan2(h.y - c.y, h.x - c.x));
  return out;
}

std::vector<double> Sketch::lineCircle(const Vec2d& a, const Vec2d& d, int i) const {
  Vec2d c = points[circles[i].center];
  double r = circles[i].radius;
  Vec2d f = a - c;
  double qa = dot(d, d), qb = 2 * dot(f, d), qc = dot(f, f) - r * r;
  double disc = qb * qb - 4 * qa * qc;
  if (!(qa > 1e-12) || !(disc >= 0)) return {};
  double s = std::sqrt(disc);
  return {(-qb - s) / (2 * qa), (-qb + s) / (2 * qa)};
}

bool Sketch::onCurve(int i, const Vec2d& p) const {
  auto angles = arcAngles(i);
  if (!angles) return true;
  Vec2d c = points[circles[i].center];
  double t = std::atan2(p.y - c.y, p.x - c.x) - angles->first;
  while (t < -1e-9) t += 2 * kPi;
  return t <= angles->second + 1e-9;
}

// MARK: Trim

bool Sketch::trim(const SketchRef& ref, const Vec2d& point) {
  if (ref.kind == SketchRef::Kind::Line) {
    int l = ref.index;
    Vec2d a = points[lines[l].a], d = points[lines[l].b] - a;
    double t = clamp(dot(point - a, d) / std::max(lengthSquared(d), 1e-12), 0.0, 1.0);
    auto ts = cutsOnLine(l);
    double lo = 0, hi = 1;
    for (double x : ts)
      if (x < t) lo = x;
    for (double x : ts)
      if (x > t) { hi = x; break; }
    if (lo == 0 && hi == 1) {
      remove({SketchRef::line(l)});
      return true;
    }
    SketchLine original = lines[l];
    // Constraints that only describe direction still hold for both pieces.
    std::vector<SketchConstraint> directional;
    for (auto& c : constraints)
      if ((c.kind == K::Horizontal || c.kind == K::Vertical) && c.a == l) directional.push_back(c);
    if (lo > 0) {
      int p = addPoint(a + d * lo);
      lines[l].b = p;
      if (hi < 1) {
        int q = addPoint(a + d * hi);
        lines.push_back(SketchLine{q, original.b, original.construction});
        for (auto& c : directional) constraints.push_back(SketchConstraint{c.kind, int(lines.size()) - 1});
      }
    } else {
      int p = addPoint(a + d * hi);
      lines[l].a = p;
    }
    // Lengths and other measures of the old line no longer make sense.
    constraints.erase(std::remove_if(constraints.begin(), constraints.end(),
                                     [&](const SketchConstraint& c) {
                                       auto args = arguments(c.kind);
                                       bool touches = (args.first == SketchEntityKind::Line && c.a == l) ||
                                                      (args.second == SketchEntityKind::Line && c.b == l);
                                       bool isDirectional = std::find(directional.begin(), directional.end(), c) !=
                                                            directional.end();
                                       return touches && !isDirectional;
                                     }),
                      constraints.end());
    remove({});
    return true;
  }
  if (ref.kind == SketchRef::Kind::Circle) {
    int i = ref.index;
    Vec2d c = points[circles[i].center];
    auto angles = cutsOnCircle(i);
    double click = std::atan2(point.y - c.y, point.x - c.x);
    auto at = [&](double t) { return c + Vec2d(std::cos(t), std::sin(t)) * circles[i].radius; };
    if (auto arc = arcAngles(i)) {
      double start = arc->first, sweep = arc->second;
      auto along = [&](double t) {
        double u = t - start;
        while (u < 0) u += 2 * kPi;
        return u;
      };
      std::vector<double> us;
      for (double a : angles) {
        double u = along(a);
        if (u > 1e-6 && u < sweep - 1e-6) us.push_back(u);
      }
      std::sort(us.begin(), us.end());
      double u = along(click);
      double lo = 0, hi = sweep;
      for (double x : us)
        if (x < u) lo = x;
      for (double x : us)
        if (x > u) { hi = x; break; }
      if (lo == 0 && hi == sweep) {
        remove({SketchRef::circle(i)});
        return true;
      }
      SketchCircle original = circles[i];
      if (lo > 0) {
        int p = addPoint(at(start + lo));
        circles[i].end = p;
        if (hi < sweep) {
          int q = addPoint(at(start + hi));
          SketchCircle piece{original.center, original.radius, original.construction};
          piece.start = q;
          piece.end = original.end;
          circles.push_back(piece);
        }
      } else {
        int p = addPoint(at(start + hi));
        circles[i].start = p;
      }
      remove({});
      return true;
    }
    // A full circle needs two crossings; the rest becomes an arc.
    std::vector<double> sorted = angles;
    std::sort(sorted.begin(), sorted.end());
    if (sorted.size() < 2) {
      remove({SketchRef::circle(i)});
      return true;
    }
    double hi = sorted[0], lo = sorted.back();
    for (double x : sorted)
      if (x > click) { hi = x; break; }
    for (double x : sorted)
      if (x < click) lo = x;
    int s = addPoint(at(hi));
    circles[i].start = s;
    int e = addPoint(at(lo));
    circles[i].end = e;
    return true;
  }
  return false;
}

// MARK: Offset

std::vector<SketchRef> Sketch::offset(const std::vector<SketchRef>& refs, double dist) {
  std::vector<SketchRef> created;
  for (auto& ref : refs) {
    if (ref.kind != SketchRef::Kind::Circle) continue;
    int i = ref.index;
    SketchCircle c = circles[i];
    double r = c.radius + dist;
    if (!(r > 1e-6)) continue;
    if (auto arc = arcAngles(i)) {
      Vec2d center = points[c.center];
      double start = arc->first, sweep = arc->second;
      c.start = addPoint(center + Vec2d(std::cos(start), std::sin(start)) * r);
      c.end = addPoint(center + Vec2d(std::cos(start + sweep), std::sin(start + sweep)) * r);
    }
    c.radius = r;
    circles.push_back(c);
    created.push_back(SketchRef::circle(int(circles.size()) - 1));
  }

  // Order the selected lines into chains through shared points.
  std::vector<int> remaining;
  for (auto& r : refs)
    if (r.kind == SketchRef::Kind::Line) remaining.push_back(r.index);
  struct Item { int line; bool forward; };
  auto itemPoint = [&](const Item& i, bool end) { const auto& l = lines[i.line]; return i.forward != end ? l.a : l.b; };
  while (!remaining.empty()) {
    std::vector<Item> chain{{remaining.front(), true}};
    remaining.erase(remaining.begin());
    auto extend = [&](bool atEnd) {
      while (true) {
        int tip = atEnd ? (chain.back().forward ? lines[chain.back().line].b : lines[chain.back().line].a)
                        : (chain.front().forward ? lines[chain.front().line].a : lines[chain.front().line].b);
        auto it = std::find_if(remaining.begin(), remaining.end(),
                               [&](int k) { return lines[k].a == tip || lines[k].b == tip; });
        if (it == remaining.end()) return;
        int l = *it;
        remaining.erase(it);
        bool forward = atEnd ? lines[l].a == tip : lines[l].b == tip;
        if (atEnd) chain.push_back({l, forward});
        else chain.insert(chain.begin(), {l, forward});
      }
    };
    extend(true);
    extend(false);

    using Seg = std::pair<Vec2d, Vec2d>;
    std::vector<Seg> segs;
    for (auto& item : chain) {
      const auto& l = lines[item.line];
      Vec2d p = points[item.forward ? l.a : l.b], q = points[item.forward ? l.b : l.a];
      Vec2d d = normalize(q - p);
      Vec2d n = Vec2d(-d.y, d.x) * dist;
      segs.push_back({p + n, q + n});
    }
    int startPoint = itemPoint(chain.front(), false);
    int endPoint = itemPoint(chain.back(), true);
    bool closed = chain.size() > 2 && startPoint == endPoint;
    auto meet = [](const Seg& s, const Seg& t) {
      Vec2d d = s.second - s.first, e = t.second - t.first;
      double den = d.x * e.y - d.y * e.x;
      if (!(std::abs(den) > 1e-12)) return s.second;
      Vec2d w = t.first - s.first;
      return s.first + d * ((w.x * e.y - w.y * e.x) / den);
    };
    std::vector<int> corners;
    size_t n = segs.size();
    for (size_t k = 0; k <= n; ++k) {
      if (k == 0) corners.push_back(addPoint(closed ? meet(segs[n - 1], segs[0]) : segs[0].first));
      else if (k == n) corners.push_back(closed ? corners[0] : addPoint(segs[k - 1].second));
      else corners.push_back(addPoint(meet(segs[k - 1], segs[k])));
    }
    for (size_t k = 0; k < n; ++k) {
      lines.push_back(SketchLine{corners[k], corners[k + 1], false});
      constraints.push_back(SketchConstraint{K::Parallel, chain[k].line, int(lines.size()) - 1});
      created.push_back(SketchRef::line(int(lines.size()) - 1));
    }
  }
  return created;
}

// MARK: Corner fillet

std::optional<std::string> Sketch::fillet(int corner, double radius) {
  std::vector<int> usingLines;
  for (size_t i = 0; i < lines.size(); ++i)
    if (lines[i].a == corner || lines[i].b == corner) usingLines.push_back(int(i));
  if (usingLines.size() != 2) return std::string("Pick a corner where exactly two lines meet");
  Vec2d p = points[corner];
  auto away = [&](int l) { return points[lines[l].a == corner ? lines[l].b : lines[l].a]; };
  Vec2d u = normalize(away(usingLines[0]) - p), v = normalize(away(usingLines[1]) - p);
  double angle = std::acos(clamp(dot(u, v), -1.0, 1.0));
  if (!(angle > 1e-3) || !(angle < kPi - 1e-3)) return std::string("The lines are parallel");
  double reach = radius / std::tan(angle / 2);
  if (!(reach < distance(p, away(usingLines[0]))) || !(reach < distance(p, away(usingLines[1]))))
    return std::string("Radius too large for these lines");
  int t1 = addPoint(p + u * reach), t2 = addPoint(p + v * reach);
  int center = addPoint(p + normalize(u + v) * (radius / std::sin(angle / 2)));
  std::pair<int, int> pairs[2] = {{usingLines[0], t1}, {usingLines[1], t2}};
  for (auto& [l, t] : pairs) {
    if (lines[l].a == corner) lines[l].a = t;
    else lines[l].b = t;
  }
  // Counter-clockwise from one tangent point to the other.
  Vec2d d1 = points[t1] - points[center], d2 = points[t2] - points[center];
  bool ccw = d1.x * d2.y - d1.y * d2.x > 0;
  addArc(center, ccw ? t1 : t2, ccw ? t2 : t1);
  int arc = int(circles.size()) - 1;
  constraints.push_back(SketchConstraint{K::Tangent, usingLines[0], arc});
  constraints.push_back(SketchConstraint{K::Tangent, usingLines[1], arc});
  SketchConstraint r{K::Radius, arc};
  r.value = radius;
  constraints.push_back(r);
  remove({});
  return std::nullopt;
}

// MARK: Disconnect

void Sketch::disconnect(int point) {
  bool first = true;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].a != point && lines[i].b != point) continue;
    if (first) { first = false; continue; }
    int copy = addPoint(points[point]);
    if (lines[i].a == point) lines[i].a = copy;
    else lines[i].b = copy;
  }
  for (size_t i = 0; i < circles.size(); ++i) {
    if (circles[i].start != point && circles[i].end != point) continue;
    if (first) { first = false; continue; }
    int copy = addPoint(points[point]);
    if (circles[i].start == point) circles[i].start = copy;
    else circles[i].end = copy;
  }
}

}  // namespace sculpt
