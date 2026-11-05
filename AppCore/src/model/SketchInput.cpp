// Port of the model side of ViewportController+Sketch.swift.
#include "sculpt/model/SketchInput.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace sculpt {

double gridStep(double cameraDistance, std::optional<double> fixedGrid) {
  if (fixedGrid) return *fixedGrid;
  return std::pow(10.0, std::floor(std::log10(std::max(cameraDistance / 15, 0.01))));
}

Vec2d polygonCorner(const Vec2d& c, const Vec2d& b, int n, bool circumscribed) {
  if (!circumscribed) return b;
  double half = kPi / double(std::max(n, 3));
  Vec2d d = b - c;
  Vec2d turned(d.x * std::cos(half) - d.y * std::sin(half), d.x * std::sin(half) + d.y * std::cos(half));
  return c + turned / std::cos(half);
}

Vec2d SketchInput::gridSnap(const Vec2d& p) const {
  if (!store_.settings().snap.grid) return p;
  double step = gridStep;
  return Vec2d(std::round(p.x / step), std::round(p.y / step)) * step;
}

std::optional<int> SketchInput::nearestPoint(const Sketch& sketch, const Vec2d& p) const {
  std::optional<int> best;
  double bestD = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < sketch.points.size(); ++i) {
    double d = distance(sketch.points[i], p);
    if (d < pickRadius && d < bestD) {
      bestD = d;
      best = int(i);
    }
  }
  return best;
}

std::vector<Vec2d> SketchInput::projectedBodyPoints() const {
  if (!store_.sketch()) return {};
  const SketchPlane& plane = store_.sketch()->plane();
  std::set<std::pair<long, long>> seen;
  std::vector<Vec2d> out;
  for (auto& [id, mesh] : store_.visibleMeshes()) {
    const auto& ep = mesh->edgePositions;
    const auto& ids = mesh->edgeIds;
    // Edge polylines end at vertices; only the ends of each edge matter.
    for (size_t i = 0; i + 1 < ep.size(); i += 2) {
      bool startsEdge = i == 0 || ids[i] != ids[i - 1];
      bool endsEdge = i + 2 >= ep.size() || ids[i + 1] != ids[i + 2];
      for (auto [k, take] : {std::pair<size_t, bool>{i, startsEdge}, {i + 1, endsEdge}}) {
        if (!take) continue;
        Vec2d q = plane.local(toD(ep[k]));
        if (seen.insert({std::lround(q.x * 1000), std::lround(q.y * 1000)}).second) out.push_back(q);
      }
    }
  }
  return out;
}

std::optional<SketchSnap> SketchInput::snap(const Vec2d& p) const {
  if (!store_.sketch()) return std::nullopt;
  const Sketch& sketch = store_.sketch()->sketch;
  const SnapSettings& snap = store_.settings().snap;
  double r = pickRadius;
  if (snap.grid || snap.guidePoints || snap.guideLines || snap.points3D)
    if (auto i = nearestPoint(sketch, p)) return SketchSnap{sketch.points[size_t(*i)], *i};
  // Guide points: midpoints, quadrants and body corners.
  std::vector<std::pair<Vec2d, std::string>> candidates;
  if (snap.guidePoints) {
    for (auto& l : sketch.lines) candidates.push_back({(sketch.points[l.a] + sketch.points[l.b]) / 2.0, "Midpoint"});
    for (size_t i = 0; i < sketch.circles.size(); ++i) {
      const auto& c = sketch.circles[i];
      Vec2d center = sketch.points[size_t(c.center)];
      if (auto arc = sketch.arcAngles(int(i))) {
        double t = arc->first + arc->second / 2;
        candidates.push_back({center + Vec2d(std::cos(t), std::sin(t)) * c.radius, "Midpoint"});
      } else {
        for (double t : {0.0, kPi / 2, kPi, 3 * kPi / 2})
          candidates.push_back({center + Vec2d(std::cos(t), std::sin(t)) * c.radius, "Quadrant"});
      }
    }
  }
  if (snap.points3D)
    for (auto& q : projectedBodyPoints()) candidates.push_back({q, "3D point"});
  if (!candidates.empty()) {
    auto best = std::min_element(candidates.begin(), candidates.end(),
                                 [&](auto& a, auto& b) { return distance(a.first, p) < distance(b.first, p); });
    if (distance(best->first, p) < r) return SketchSnap{best->first, std::nullopt, best->second};
  }
  Vec2d g = gridSnap(p);
  // A grid point that already holds a sketch point is that point, so shapes
  // drawn on the grid connect instead of overlapping.
  for (size_t i = 0; i < sketch.points.size(); ++i)
    if (distance(sketch.points[i], g) < gridStep * 1e-6) return SketchSnap{sketch.points[i], int(i)};
  // Guide lines: line up with existing points horizontally or vertically.
  std::vector<std::vector<Vec2d>> guides;
  std::optional<std::string> hint;
  if (snap.guideLines && !sketch.points.empty()) {
    auto qx = *std::min_element(sketch.points.begin(), sketch.points.end(),
                                [&](auto& a, auto& b) { return std::abs(a.x - p.x) < std::abs(b.x - p.x); });
    if (std::abs(qx.x - p.x) < r && qx != g) {
      g.x = qx.x;
      guides.push_back({qx, {qx.x, g.y}});
      hint = "Vertical";
    }
    auto qy = *std::min_element(sketch.points.begin(), sketch.points.end(),
                                [&](auto& a, auto& b) { return std::abs(a.y - p.y) < std::abs(b.y - p.y); });
    if (std::abs(qy.y - p.y) < r) {
      g.y = qy.y;
      guides.push_back({qy, {g.x, qy.y}});
      hint = hint ? std::string("Aligned") : std::string("Horizontal");
    }
  }
  return SketchSnap{g, std::nullopt, hint, guides};
}

static double segmentDistance(const Vec2d& p, const Vec2d& a, const Vec2d& b) {
  Vec2d ab = b - a;
  double t = clamp(dot(p - a, ab) / std::max(lengthSquared(ab), 1e-12), 0.0, 1.0);
  return distance(p, a + ab * t);
}

static double polylineDistance(const Vec2d& p, const std::vector<Vec2d>& pts) {
  double best = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i + 1 < pts.size(); ++i) best = std::min(best, segmentDistance(p, pts[i], pts[i + 1]));
  return best;
}

std::optional<SketchRef> SketchInput::hit(const Vec2d& p) const {
  if (!store_.sketch()) return std::nullopt;
  const Sketch& sketch = store_.sketch()->sketch;
  if (auto i = nearestPoint(sketch, p)) return SketchRef::point(*i);
  double r = pickRadius;
  std::optional<SketchRef> best;
  double bestD = std::numeric_limits<double>::infinity();
  auto consider = [&](SketchRef ref, double d) {
    if (d < r && d < bestD) {
      best = ref;
      bestD = d;
    }
  };
  for (size_t i = 0; i < sketch.lines.size(); ++i)
    consider(SketchRef::line(int(i)), segmentDistance(p, sketch.points[sketch.lines[i].a], sketch.points[sketch.lines[i].b]));
  for (size_t i = 0; i < sketch.splines.size(); ++i)
    consider(SketchRef::spline(int(i)), polylineDistance(p, sketch.splines[i].samples(sketch.points)));
  for (size_t i = 0; i < sketch.ellipses.size(); ++i)
    consider(SketchRef::ellipse(int(i)),
             polylineDistance(p, ellipseSamples(sketch.ellipses[i], sketch.points[size_t(sketch.ellipses[i].center)])));
  for (size_t i = 0; i < sketch.circles.size(); ++i) {
    const auto& c = sketch.circles[i];
    Vec2d center = sketch.points[size_t(c.center)];
    double d = std::abs(distance(p, center) - c.radius);
    if (auto arc = sketch.arcAngles(int(i))) {
      double t = std::atan2(p.y - center.y, p.x - center.x) - arc->first;
      while (t < 0) t += 2 * kPi;
      if (t > arc->second) continue;
    }
    consider(SketchRef::circle(int(i)), d);
  }
  return best;
}

bool SketchInput::hover(const std::optional<Vec2d>& p) {
  if (!store_.sketch()) return false;
  SketchSession s = *store_.sketch();
  if (s.tool == SketchTool::select || s.tool == SketchTool::trim) {
    s.cursor.reset();
    s.hover = p ? hit(*p) : std::nullopt;
  } else {
    auto sn = p ? snap(*p) : std::nullopt;
    s.cursor = sn ? std::optional<Vec2d>(sn->position) : std::nullopt;
    s.hover = sn && sn->index ? std::optional<SketchRef>(SketchRef::point(*sn->index)) : std::nullopt;
    s.snapHint = sn ? sn->hint : std::nullopt;
    s.guides = sn ? sn->guides : std::vector<std::vector<Vec2d>>{};
  }
  // Skip moves that change nothing on screen.
  const auto& old = *store_.sketch();
  if (old.cursor == s.cursor && old.hover == s.hover && old.snapHint == s.snapHint && old.guides == s.guides) return false;
  store_.setSketch(std::move(s));
  return true;
}

void SketchInput::markConstruction(SketchSession& s, size_t lines, size_t circles) const {
  if (!s.constructionMode) return;
  for (size_t i = lines; i < s.sketch.lines.size(); ++i) s.sketch.lines[i].construction = true;
  for (size_t i = circles; i < s.sketch.circles.size(); ++i) s.sketch.circles[i].construction = true;
}

void SketchInput::tap(const Vec2d& p, bool additive) {
  if (!store_.sketch()) return;
  SketchSession s = *store_.sketch();
  size_t lines0 = s.sketch.lines.size(), circles0 = s.sketch.circles.size();
  switch (s.tool) {
    case SketchTool::trim: {
      auto h = hit(p);
      if (!h) return;
      if (s.sketch.trim(*h, p)) {
        s.selection.clear();
        s.hover.reset();
        s.resolve();
      }
      break;
    }
    case SketchTool::line: {
      auto sn = snap(p);
      if (!sn) return;
      int q = sn->index ? *sn->index : s.sketch.addPoint(sn->position);
      if (s.chainEnd) {
        s.sketch.addLine(*s.chainEnd, q);
        // Landing on an existing point closes the chain.
        s.chainEnd = sn->index ? std::nullopt : std::optional<int>(q);
      } else {
        s.chainEnd = q;
      }
      s.resolve();
      break;
    }
    case SketchTool::arc: {
      // Start, end, then a point the arc passes through.
      auto sn = snap(p);
      if (!sn) return;
      if (s.pending.size() < 2) {
        s.pending.push_back(*sn);
      } else {
        SketchSnap a = s.pending[0], b = s.pending[1];
        if (auto circle = circleThrough(a.position, b.position, sn->position)) {
          Vec2d c = circle->first;
          auto outline = arcOutline(a.position, b.position, sn->position);
          // Arcs run counter-clockwise; swap ends when the drawn one doesn't.
          double t0 = std::atan2(outline[0].y - c.y, outline[0].x - c.x);
          double t1 = std::atan2(outline[1].y - c.y, outline[1].x - c.x);
          bool ccw = std::sin(t1 - t0) > 0;
          int ia = a.index ? *a.index : s.sketch.addPoint(a.position);
          int ib = b.index ? *b.index : s.sketch.addPoint(b.position);
          int center = s.sketch.addPoint(c);
          s.sketch.addArc(center, ccw ? ia : ib, ccw ? ib : ia);
          s.resolve();
        }
        s.pending.clear();
      }
      break;
    }
    case SketchTool::spline:
    case SketchTool::controlSpline: {
      auto sn = snap(p);
      if (!sn) return;
      auto draft = s.splineDraft;
      bool control = s.tool == SketchTool::controlSpline;
      if (!draft.empty() && sn->index == draft.front() && draft.size() >= 3) {
        // Tapping the first point closes the spline.
        SketchSpline sp;
        sp.points = draft;
        sp.closed = true;
        sp.construction = s.constructionMode;
        sp.controlPoints = control;
        s.sketch.splines.push_back(sp);
        s.splineDraft.clear();
      } else if (!draft.empty() && sn->index == draft.back()) {
        // Tapping the last point again finishes an open spline.
        if (draft.size() >= 2) {
          SketchSpline sp;
          sp.points = draft;
          sp.construction = s.constructionMode;
          sp.controlPoints = control && draft.size() >= 3;
          s.sketch.splines.push_back(sp);
        }
        s.splineDraft.clear();
        s.sketch.remove({});
      } else {
        s.splineDraft.push_back(sn->index ? *sn->index : s.sketch.addPoint(sn->position));
        s.sketch.markers.push_back(s.splineDraft.back());
      }
      s.resolve();
      break;
    }
    case SketchTool::point: {
      auto sn = snap(p);
      if (!sn || sn->index) break;
      s.sketch.markers.push_back(s.sketch.addPoint(sn->position));
      s.resolve();
      break;
    }
    default: {
      // Taps select in every tool except Line, so constraints never need a tool switch.
      if (auto h = hit(p)) {
        auto it = std::find(s.selection.begin(), s.selection.end(), *h);
        if (it != s.selection.end()) s.selection.erase(it);
        else s.selection.push_back(*h);
      } else if (!additive) {
        s.selection.clear();
      }
      break;
    }
  }
  markConstruction(s, lines0, circles0);
  store_.setSketch(std::move(s));
}

bool SketchInput::dragDraws(const Vec2d& p) const {
  if (!store_.sketch()) return false;
  if (store_.sketch()->tool == SketchTool::select) return hit(p).has_value();
  return drags(store_.sketch()->tool);
}

void SketchInput::drawBegan(const std::optional<Vec2d>& p) {
  if (!store_.sketch()) return;
  if (store_.sketch()->tool == SketchTool::select) {
    if (!p) return;
    auto h = hit(*p);
    if (!h) return;
    Sketch base = store_.sketch()->sketch;
    store_.beginSketchDrag();
    drag_ = Drag{*h, base, *p};
    const auto& sel = store_.sketch()->selection;
    if (std::find(sel.begin(), sel.end(), *h) == sel.end()) store_.setSketchSelection({*h});
    return;
  }
  dragStart_ = p ? snap(*p) : std::nullopt;
}

void SketchInput::drawChanged(const std::optional<Vec2d>& p) {
  if (!store_.sketch()) return;
  SketchSession s = *store_.sketch();
  if (drag_) {
    if (!p) return;
    Sketch sk = drag_->base;
    std::map<int, Vec2d> targets;
    switch (drag_->ref.kind) {
      case SketchRef::Kind::Point: targets[drag_->ref.index] = gridSnap(*p); break;
      case SketchRef::Kind::Line: {
        Vec2d delta = gridSnap(*p - drag_->start);
        const auto& l = sk.lines[size_t(drag_->ref.index)];
        for (int e : {l.a, l.b}) targets[e] = drag_->base.points[size_t(e)] + delta;
        break;
      }
      case SketchRef::Kind::Circle: {
        auto& c = sk.circles[size_t(drag_->ref.index)];
        c.radius = std::max(distance(gridSnap(*p), sk.points[size_t(c.center)]), gridStep);
        break;
      }
      case SketchRef::Kind::Spline: {
        // Move the whole curve.
        Vec2d delta = gridSnap(*p - drag_->start);
        for (int e : sk.splines[size_t(drag_->ref.index)].points) targets[e] = drag_->base.points[size_t(e)] + delta;
        break;
      }
      case SketchRef::Kind::Ellipse: {
        // Dragging the outline resizes it, keeping its proportions.
        auto& e = sk.ellipses[size_t(drag_->ref.index)];
        Vec2d c = sk.points[size_t(e.center)];
        double scale = std::max(distance(*p, c), 1e-6) / std::max(distance(drag_->start, c), 1e-6);
        e.majorRadius = drag_->base.ellipses[size_t(drag_->ref.index)].majorRadius * scale;
        e.minorRadius = drag_->base.ellipses[size_t(drag_->ref.index)].minorRadius * scale;
        break;
      }
      case SketchRef::Kind::Constraint: return;
    }
    targets.erase(0);
    // A drag the constraints can't follow just leaves things where they were.
    if (auto status = sk.solve(targets)) {
      s.sketch = sk;
      s.status = *status;
    }
    store_.setSketch(std::move(s));
    return;
  }
  if (!dragStart_ || !p) return;
  auto end = snap(*p);
  if (!end) return;
  Vec2d a = dragStart_->position, b = end->position;
  s.preview.clear();
  switch (s.tool) {
    case SketchTool::circle: {
      double r = distance(a, b);
      for (int i = 0; i <= 64; ++i)
        s.preview.push_back(a + Vec2d(std::cos(double(i) / 32 * kPi), std::sin(double(i) / 32 * kPi)) * r);
      break;
    }
    case SketchTool::centerRectangle: {
      Vec2d d = b - a;
      s.preview = {a - d, {a.x + d.x, a.y - d.y}, a + d, {a.x - d.x, a.y + d.y}, a - d};
      break;
    }
    case SketchTool::ellipse: {
      SketchEllipse e;
      e.majorRadius = std::abs(b.x - a.x);
      e.minorRadius = std::abs(b.y - a.y);
      s.preview = ellipseSamples(e, a);
      break;
    }
    case SketchTool::polygon: {
      Vec2d corner = polygonCorner(a, b, s.polygonSides, s.polygonCircumscribed);
      double r = distance(a, corner), t0 = std::atan2(corner.y - a.y, corner.x - a.x);
      int n = s.polygonSides;
      for (int i = 0; i <= n; ++i) {
        double t = t0 + double(i) / double(n) * 2 * kPi;
        s.preview.push_back(a + Vec2d(std::cos(t), std::sin(t)) * r);
      }
      break;
    }
    default: s.preview = {a, {b.x, a.y}, b, {a.x, b.y}, a}; break;
  }
  s.cursor = b;
  store_.setSketch(std::move(s));
}

void SketchInput::drawEnded(const std::optional<Vec2d>& p) {
  drawChanged(p);
  bool wasDragging = drag_.has_value();
  auto start = dragStart_;
  dragStart_.reset();
  drag_.reset();
  if (wasDragging) {
    store_.endSketchDrag();
    return;
  }
  if (!store_.sketch() || !start || !p) return;
  auto end = snap(*p);
  if (!end) return;
  SketchSession s = *store_.sketch();
  size_t lines0 = s.sketch.lines.size(), circles0 = s.sketch.circles.size();
  s.preview.clear();
  switch (s.tool) {
    case SketchTool::rectangle: s.sketch.addRectangle(start->position, end->position, start->index, end->index); break;
    case SketchTool::circle: {
      double r = distance(start->position, end->position);
      if (r > 1e-9) s.sketch.addCircle(start->index ? *start->index : s.sketch.addPoint(start->position), r);
      break;
    }
    case SketchTool::centerRectangle:
      s.sketch.addCenterRectangle(start->position, end->position, start->index);
      break;
    case SketchTool::ellipse: {
      double rx = std::abs(end->position.x - start->position.x), ry = std::abs(end->position.y - start->position.y);
      if (rx > 1e-9 && ry > 1e-9) {
        int c = start->index ? *start->index : s.sketch.addPoint(start->position);
        SketchEllipse e;
        e.center = c;
        e.majorRadius = rx;
        e.minorRadius = ry;
        e.construction = s.constructionMode;
        s.sketch.ellipses.push_back(e);
      }
      break;
    }
    case SketchTool::polygon: {
      Vec2d corner = polygonCorner(start->position, end->position, s.polygonSides, s.polygonCircumscribed);
      s.sketch.addPolygon(start->position, corner, s.polygonSides, start->index);
      break;
    }
    default: break;
  }
  markConstruction(s, lines0, circles0);
  s.resolve();
  store_.setSketch(std::move(s));
}

std::vector<SketchAnnotation> SketchInput::annotations(double offset) const {
  std::vector<SketchAnnotation> out;
  if (!store_.sketch()) return out;
  const SketchSession& s = *store_.sketch();
  // One glyph per kind and line, however many constraints share it.
  std::set<std::string> shown;
  auto firstTime = [&](SketchConstraint::Kind kind, int line) { return shown.insert(glyph(kind) + std::to_string(line)).second; };
  for (size_t i = 0; i < s.sketch.constraints.size(); ++i) {
    const auto& c = s.sketch.constraints[i];
    if (c.kind == SketchConstraint::Kind::Coincident) continue;
    // Points held on a construction circle (a polygon's corners) would only add clutter.
    if (c.kind == SketchConstraint::Kind::PointOnCircle && c.b >= 0 && c.b < int(s.sketch.circles.size()) &&
        s.sketch.circles[size_t(c.b)].construction)
      continue;
    bool selected = std::find(s.selection.begin(), s.selection.end(), SketchRef::constraint(int(i))) != s.selection.end();
    bool dim = isDimension(c.kind);
    std::string text = dim ? s.label(c) : glyph(c.kind);
    auto args = arguments(c.kind);
    bool onLine = !dim && args.first == SketchEntityKind::Line;
    if (!onLine || firstTime(c.kind, c.a))
      out.push_back({std::to_string(i), int(i), text, s.anchor(c, offset).position, dim, selected});
    // Two-line relations are marked on both lines.
    if (!dim && args.first == SketchEntityKind::Line && args.second == SketchEntityKind::Line) {
      SketchConstraint mirrored = c;
      mirrored.a = c.b;
      if (firstTime(c.kind, c.b))
        out.push_back({std::to_string(i) + "b", int(i), text, s.anchor(mirrored, offset).position, false, selected});
    }
  }
  // Snapping hint next to the cursor.
  if (store_.settings().snap.showHints && s.snapHint && s.cursor)
    out.push_back({"hint", -1, *s.snapHint, *s.cursor, false, true, true});
  return out;
}

std::vector<Vec2d> SketchInput::spreadAnnotations(std::vector<Vec2d> screen) {
  std::vector<Vec2d> taken;
  for (auto& p : screen) {
    while (std::any_of(taken.begin(), taken.end(), [&](auto& t) { return std::hypot(t.x - p.x, t.y - p.y) < 16; })) p.x += 18;
    taken.push_back(p);
  }
  return screen;
}

}  // namespace sculpt
