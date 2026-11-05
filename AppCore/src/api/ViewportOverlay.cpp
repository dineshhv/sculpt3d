// Frame assembly of the viewport controller: ViewportController.scene(),
// grid(), overlay(), highlightEdges, referenceOverlay / measureOverlay
// (+Reference), pointOverlay (+Points), sketchOverlay and sketchAnnotations
// (+Sketch), and the SwiftUI overlays' label positions (SketchAnnotationsView,
// MeasurementLabels) as JSON for native text.
#include <algorithm>
#include <cmath>

#include "api/ViewportController.hpp"
#include "sculpt/model/DisplayFormat.hpp"
#include "viewport/Overlays.hpp"

namespace sculpt::api {

using json = nlohmann::json;

namespace {
vp::Vec3f V(const Vec3d& v) { return {float(v.x), float(v.y), float(v.z)}; }
vp::Vec3f V(const Vec3f& v) { return {v.x, v.y, v.z}; }
vp::Vec4f C(float r, float g, float b, float a) { return {r, g, b, a}; }

uint64_t fnv(const std::vector<uint8_t>& bytes) {
  uint64_t h = 1469598103934665603ULL;
  // Sampling keeps this cheap for big images; size and both ends always count.
  size_t step = std::max<size_t>(1, bytes.size() / 65536);
  for (size_t i = 0; i < bytes.size(); i += step) h = (h ^ bytes[i]) * 1099511628211ULL;
  if (!bytes.empty()) h = (h ^ bytes.back()) * 1099511628211ULL;
  return (h ^ bytes.size()) * 1099511628211ULL;
}

vp::Pick toVpPick(const Pick& p) {
  switch (p.kind) {
    case Pick::Kind::Body: return vp::Pick::makeBody(p.body);
    case Pick::Kind::Face: return vp::Pick::makeFace(p.body, p.index);
    case Pick::Kind::Edge: return vp::Pick::makeEdge(p.body, p.index);
  }
  return vp::Pick::makeBody(p.body);
}
}  // namespace

const vp::Scene& ViewportController::scene() {
  if (sceneKey_[0] == store.revision() && sceneKey_[1] == viewRevision_) return scene_;
  const vp::MeshMap& meshes = visibleMeshes();
  vp::Scene s;
  s.meshes = meshes;
  s.geometryVersion = sceneGeometryVersion_;
  for (auto& p : store.selection()) s.selection.push_back(toVpPick(p));
  if (store.hover()) s.hover = toVpPick(*store.hover());
  s.ghostBody = -1;
  const ProjectSettings& settings = store.settings();
  if (settings.environment.showGrid || store.sketch()) {
    s.grid = gridBatch();
  } else if (showWorldAxes && displayMode != vp::DisplayMode::Visualized && !visualizing) {
    s.grid = vp::axesOnly(camera, gridStep());
  }
  s.overlay = overlay();
  s.displayMode = displayMode;
  s.clip = sectionPlane();
  for (auto& [id, a] : store.displayAppearance()) s.appearance[id] = vp::Appearance{V(a.color), a.metallic, a.roughness, a.opacity};
  const EnvironmentSettings& e = settings.environment;
  s.environment = vp::EnvironmentSettings{V(e.background), e.gradient,       e.showGrid,         e.lightIntensity,
                                          e.lightAzimuth,  e.groundShadow,   e.lightElevation,   e.castShadows,
                                          e.shadowSoftness, e.ambientOcclusion, e.occlusionStrength, e.smoothShading};
  // Ground shadows belong to Visualize (and the Visualized display mode); modeling stays crisp.
  if (!visualizing && displayMode != vp::DisplayMode::Visualized) s.environment.groundShadow = false;
  if (visualizing && displayMode != vp::DisplayMode::Zebra && displayMode != vp::DisplayMode::Curvature)
    s.displayMode = vp::DisplayMode::Visualized;
  s.showEdges = showEdges;
  s.showHiddenEdges = showHiddenEdges;
  s.cameraSettings = vp::CameraSettings{settings.camera.orthographic, settings.camera.fieldOfView};
  std::map<Uuid, ImageCache> kept;
  for (auto& item : store.referenceItems()) {
    const ReferenceImage* image = item.image();
    if (!image || settings.hiddenItems.count(item.id)) continue;
    if (image->decal && !showDecals) continue;
    ImageCache cache;
    auto it = images_.find(item.id);
    if (it != images_.end() && it->second.size == image->png.size() && it->second.bytes &&
        *it->second.bytes == image->png) {
      cache = it->second;
    } else {
      cache.size = image->png.size();
      cache.bytes = std::make_shared<const std::vector<uint8_t>>(image->png);
      cache.key = fnv(image->png);
    }
    kept[item.id] = cache;
    vp::ReferenceImage r;
    r.png = cache.bytes;
    r.contentKey = cache.key;
    r.plane = vp::Plane{V(image->plane.origin), V(image->plane.normal), V(image->plane.xDir)};
    r.width = float(image->width);
    r.height = float(image->height);
    r.opacity = float(image->opacity);
    r.decal = image->decal;
    s.images.push_back(r);
  }
  images_ = std::move(kept);
  scene_ = std::move(s);
  sceneKey_[0] = store.revision();
  sceneKey_[1] = viewRevision_;
  return scene_;
}

vp::LineBatch ViewportController::gridBatch() {
  bool sketching = store.sketch().has_value();
  vp::Plane plane = vp::Plane::xy();
  if (sketching) {
    const SketchPlane& p = store.sketch()->plane();
    plane = vp::Plane{V(p.origin), V(p.normal), V(p.xDir)};
  } else if (gridAtModelBottom && !visibleMeshes().empty()) {
    float floor = 3.4e38f;
    for (auto& [id, m] : visibleMeshes()) floor = std::min(floor, m->boundsMin.z);
    plane.origin.z = floor;
  }
  return vp::grid(camera, plane, gridStep(), sketching, showWorldAxes);
}

vp::LineBatch ViewportController::overlay() {
  vp::LineBatch batch;
  std::vector<vp::Pick> sel;
  for (auto& p : store.selection()) sel.push_back(toVpPick(p));
  std::optional<vp::Pick> h;
  if (store.hover()) h = toVpPick(*store.hover());
  vp::highlightEdges(batch, visibleMeshes(), sel, h, camera, size_);
  referenceOverlay(batch);
  measureOverlay(batch);
  pointOverlay(batch);
  sketchOverlay(batch);
  return batch;
}

void ViewportController::referenceOverlay(vp::LineBatch& batch) {
  const vp::Vec4f selected = C(1.0f, 0.5f, 0.1f, 1);
  const auto& sel = store.itemSelection();
  for (auto& item : store.referenceItems()) {
    if (store.settings().hiddenItems.count(item.id)) continue;
    bool isSelected = std::find(sel.begin(), sel.end(), item.id) != sel.end();
    if (item.isSketch() && !showSketches && !isSelected) continue;
    SketchPlane plane = item.plane();
    auto w = [&](const Vec2d& p) { return V(plane.world(p)); };
    if (auto sketch = item.sketch()) {
      vp::Vec4f ink = isSelected ? selected : C(0.2f, 0.3f, 0.55f, 0.9f);
      for (auto& l : sketch->lines)
        if (!l.construction) batch.line(w(sketch->points[size_t(l.a)]), w(sketch->points[size_t(l.b)]), ink);
      for (size_t i = 0; i < sketch->circles.size(); ++i) {
        const auto& c = sketch->circles[i];
        if (c.construction) continue;
        auto arc = sketch->arcAngles(int(i));
        double start = arc ? arc->first : 0, sweep = arc ? arc->second : 2 * kPi;
        Vec2d center = sketch->points[size_t(c.center)];
        int n = std::max(8, int(64 * sweep / (2 * kPi)));
        std::vector<vp::Vec3f> pts;
        for (int k = 0; k <= n; ++k) {
          double t = start + sweep * double(k) / double(n);
          pts.push_back(w(center + Vec2d(std::cos(t), std::sin(t)) * c.radius));
        }
        batch.polyline(pts, ink);
      }
      for (auto& sp : sketch->splines) {
        if (sp.construction || sp.points.size() < 2) continue;
        std::vector<vp::Vec3f> pts;
        for (auto& p : sp.samples(sketch->points)) pts.push_back(w(p));
        batch.polyline(pts, ink);
      }
      for (auto& e : sketch->ellipses) {
        if (e.construction) continue;
        std::vector<vp::Vec3f> pts;
        for (auto& p : ellipseSamples(e, sketch->points[size_t(e.center)])) pts.push_back(w(p));
        batch.polyline(pts, ink);
      }
    } else if (item.isPlane()) {
      vp::Vec4f ink = isSelected ? selected : C(0.45f, 0.35f, 0.8f, 0.8f);
      double r = 40;
      batch.polyline({w({-r, -r}), w({r, -r}), w({r, r}), w({-r, r}), w({-r, -r})}, ink);
      // A light hatch makes the plane readable from any side.
      for (double k = -r * 0.75; k <= r * 0.75 + 1e-9; k += r / 4)
        batch.line(w({k, -r}), w({k, r}), ink * C(1, 1, 1, 0.25f));
    } else if (auto axis = item.axis()) {
      vp::Vec4f ink = isSelected ? selected : C(0.85f, 0.45f, 0.1f, 0.9f);
      Vec3d d = normalize(axis->direction) * 200.0, o = axis->origin;
      // Dash-dot centre line.
      for (int step = 0; step < 40; ++step) {
        double k = -1.0 + 0.05 * step;
        if (int((k + 1) * 20 + 1e-9) % 3 == 2) continue;
        batch.line(V(o + d * k), V(o + d * (k + 0.04)), ink);
      }
    } else if (auto image = item.image()) {
      // The picture itself is drawn by the renderer; outline it here.
      double hw = image->width / 2, hh = image->height / 2;
      vp::Vec4f ink = isSelected ? selected : C(0.4f, 0.42f, 0.5f, 0.6f);
      batch.polyline({w({-hw, -hh}), w({hw, -hh}), w({hw, hh}), w({-hw, hh}), w({-hw, -hh})}, ink);
    }
  }
}

void ViewportController::measureOverlay(vp::LineBatch& batch) {
  if (!measuring || store.selection().size() != 2) return;
  const auto& m = measurement();
  if (!m || !m->hasDistance || !(m->distance > 1e-9)) return;
  vp::Vec3f a = V(fromO3d(m->p1)), b = V(fromO3d(m->p2));
  vp::Vec4f ink = C(0.1f, 0.6f, 0.35f, 1);
  batch.line(a, b, ink);
  float s = camera.worldPerPoint(size_) * 4;
  for (vp::Vec3f p : {a, b}) {
    batch.line(p - camera.right() * s, p + camera.right() * s, ink);
    batch.line(p - camera.up() * s, p + camera.up() * s, ink);
  }
}

void ViewportController::pointOverlay(vp::LineBatch& batch) {
  float s = camera.worldPerPoint(size_);
  auto marker = [&](const Vec3d& p, vp::Vec4f color, float r) {
    vp::Vec3f c = V(p), rr = camera.right() * (s * r), uu = camera.up() * (s * r);
    batch.polyline({c - rr - uu, c + rr - uu, c + rr + uu, c - rr + uu, c - rr - uu}, color);
  };
  if (pointPick) {
    vp::Vec4f picked = C(1.0f, 0.55f, 0.12f, 1);
    std::vector<vp::Vec3f> line;
    for (auto& p : pointPick->points) {
      marker(p.position, picked, 5);
      line.push_back(V(p.position));
    }
    batch.polyline(line, picked);
    if (auto& h = pointPick->hover) {
      bool vertex = h->kind == PickedPoint::Kind::vertex;
      vp::Vec4f color = vertex ? C(0.1f, 0.75f, 0.45f, 1) : C(0.25f, 0.55f, 1.0f, 1);
      marker(h->position, color, vertex ? 6 : 4);
      if (!pointPick->points.empty())
        batch.line(V(pointPick->points.back().position), V(h->position), color * C(1, 1, 1, 0.6f));
    }
  }
  if (!showPinnedMeasurements) return;
  vp::Vec4f ink = C(0.05f, 0.55f, 0.45f, 1);
  for (auto& m : pointMeasurements) {
    std::vector<vp::Vec3f> pts;
    for (auto& p : m.points) pts.push_back(V(p));
    batch.polyline(pts, ink);
    for (auto& p : m.points) marker(p, ink, 3);
  }
}

void ViewportController::sketchOverlay(vp::LineBatch& batch) {
  if (!store.sketch()) return;
  const SketchSession& s = *store.sketch();
  const Sketch& sketch = s.sketch;
  const SketchPlane& plane = s.plane();
  auto w = [&](const Vec2d& p) { return V(plane.world(p)); };

  const vp::Vec4f freeInk = C(0.05f, 0.4f, 0.95f, 1);
  const vp::Vec4f fixedInk = C(0.08f, 0.1f, 0.16f, 1);
  const vp::Vec4f constructionInk = C(0.45f, 0.5f, 0.6f, 0.9f);
  const vp::Vec4f selectedInk = C(1.0f, 0.5f, 0.1f, 1);
  const vp::Vec4f hoveredInk = C(0.35f, 0.7f, 1.0f, 1);
  const vp::Vec4f draftInk = C(1.0f, 0.5f, 0.1f, 1);
  float pixel = camera.worldPerPoint(size_);
  auto isSelected = [&](const SketchRef& r) { return std::find(s.selection.begin(), s.selection.end(), r) != s.selection.end(); };
  auto isActive = [&](const SketchRef& r) { return isSelected(r) || s.hover == r; };
  auto isFixed = [&](int p) { return p >= 0 && size_t(p) < s.status.fixedPoints.size() && s.status.fixedPoints[size_t(p)]; };
  auto color = [&](const SketchRef& r, bool determined, bool construction) {
    if (isSelected(r)) return selectedInk;
    if (s.hover == r) return hoveredInk;
    if (construction) return constructionInk;
    return determined ? fixedInk : freeInk;
  };
  std::vector<vp::Vec3f> thick = {{0, 0, 0}, camera.right() * pixel, -camera.right() * pixel, camera.up() * pixel,
                                  -camera.up() * pixel};
  std::vector<vp::Vec3f> thin = {{0, 0, 0}};
  // Selected and hovered curves are drawn thicker, as offset copies.
  auto stroke = [&](const Vec2d& a, const Vec2d& b, vp::Vec4f c, bool isThick, bool dashed) {
    const auto& offsets = isThick ? thick : thin;
    if (dashed) {
      double len = distance(a, b), dash = double(pixel) * 6;
      int count = std::max(int(len / dash), 1);
      for (int i = 0; i < count; i += 2) {
        Vec2d p = a + (b - a) * (double(i) / count), q = a + (b - a) * (double(std::min(i + 1, count)) / count);
        for (auto& o : offsets) batch.line(w(p) + o, w(q) + o, c);
      }
    } else {
      for (auto& o : offsets) batch.line(w(a) + o, w(b) + o, c);
    }
  };

  // Plane axes.
  double ax = gridStep() * 200;
  batch.line(w({-ax, 0}), w({ax, 0}), C(0.85f, 0.25f, 0.25f, 0.5f));
  batch.line(w({0, -ax}), w({0, ax}), C(0.25f, 0.7f, 0.3f, 0.5f));

  for (size_t i = 0; i < sketch.lines.size(); ++i) {
    const auto& l = sketch.lines[i];
    auto ref = SketchRef::line(int(i));
    vp::Vec4f c = color(ref, isFixed(l.a) && isFixed(l.b), l.construction);
    stroke(sketch.points[size_t(l.a)], sketch.points[size_t(l.b)], c, isActive(ref), l.construction);
  }
  for (size_t i = 0; i < sketch.circles.size(); ++i) {
    const auto& circle = sketch.circles[i];
    auto ref = SketchRef::circle(int(i));
    bool determined = i < s.status.fixedCircles.size() && s.status.fixedCircles[i];
    vp::Vec4f c = color(ref, determined, circle.construction);
    Vec2d center = sketch.points[size_t(circle.center)];
    auto arc = sketch.arcAngles(int(i));
    double start = arc ? arc->first : 0, sweep = arc ? arc->second : 2 * kPi;
    int segments = std::max(8, int(96 * sweep / (2 * kPi)));
    for (int k = 0; k < segments; ++k) {
      if (circle.construction && k % 2 == 1) continue;
      double t0 = start + double(k) / segments * sweep, t1 = start + double(k + 1) / segments * sweep;
      stroke(center + Vec2d(std::cos(t0), std::sin(t0)) * circle.radius,
             center + Vec2d(std::cos(t1), std::sin(t1)) * circle.radius, c, isActive(ref), false);
    }
  }
  for (size_t i = 0; i < sketch.splines.size(); ++i) {
    const auto& sp = sketch.splines[i];
    auto ref = SketchRef::spline(int(i));
    bool determined = std::all_of(sp.points.begin(), sp.points.end(), isFixed);
    vp::Vec4f c = color(ref, determined, sp.construction);
    auto pts = sp.samples(sketch.points);
    for (size_t k = 0; k + 1 < pts.size(); ++k) stroke(pts[k], pts[k + 1], c, isActive(ref), false);
  }
  for (size_t i = 0; i < sketch.ellipses.size(); ++i) {
    const auto& e = sketch.ellipses[i];
    auto ref = SketchRef::ellipse(int(i));
    vp::Vec4f c = color(ref, false, e.construction);
    auto pts = ellipseSamples(e, sketch.points[size_t(e.center)]);
    for (size_t k = 0; k + 1 < pts.size(); ++k) stroke(pts[k], pts[k + 1], c, isActive(ref), false);
  }
  // Points as small squares; the origin always shows.
  std::set<int> used = {0};
  for (auto& l : sketch.lines) used.insert({l.a, l.b});
  for (auto& c : sketch.circles) {
    used.insert(c.center);
    if (c.start) used.insert(*c.start);
    if (c.end) used.insert(*c.end);
  }
  for (auto& sp : sketch.splines) used.insert(sp.points.begin(), sp.points.end());
  for (auto& e : sketch.ellipses) used.insert(e.center);
  used.insert(sketch.markers.begin(), sketch.markers.end());
  for (size_t i = 0; i < sketch.points.size(); ++i) {
    if (!used.count(int(i))) continue;
    auto ref = SketchRef::point(int(i));
    bool active = isActive(ref);
    vp::Vec4f c = color(ref, isFixed(int(i)), false);
    double r = double(pixel) * (active ? 4 : 2.5);
    Vec2d p = sketch.points[i];
    batch.polyline({w(p + Vec2d(-r, -r)), w(p + Vec2d(r, -r)), w(p + Vec2d(r, r)), w(p + Vec2d(-r, r)), w(p + Vec2d(-r, -r))}, c);
  }

  // Dimension leaders.
  double offset = double(pixel) * 14;
  for (size_t i = 0; i < sketch.constraints.size(); ++i) {
    const auto& c = sketch.constraints[i];
    if (!isDimension(c.kind)) continue;
    auto leader = s.anchor(c, offset).leader;
    vp::Vec4f ink = isSelected(SketchRef::constraint(int(i))) ? selectedInk : C(0.3f, 0.32f, 0.4f, 0.8f);
    std::vector<vp::Vec3f> pts;
    for (auto& p : leader) pts.push_back(w(p));
    batch.polyline(pts, ink);
  }

  // In-progress geometry.
  {
    std::vector<vp::Vec3f> pts;
    for (auto& p : s.preview) pts.push_back(w(p));
    batch.polyline(pts, draftInk);
  }
  if (!s.splineDraft.empty()) {
    std::vector<Vec2d> pts;
    for (int i : s.splineDraft) pts.push_back(sketch.points[size_t(i)]);
    if (s.cursor) pts.push_back(*s.cursor);
    bool control = s.tool == SketchTool::controlSpline;
    auto curve = control ? bsplineSamples(pts, false) : splineSamples(pts, false);
    if (control) {
      std::vector<vp::Vec3f> cage;
      for (auto& p : pts) cage.push_back(w(p));
      batch.polyline(cage, draftInk * C(1, 1, 1, 0.35f));
    }
    std::vector<vp::Vec3f> cw;
    for (auto& p : curve) cw.push_back(w(p));
    batch.polyline(cw, draftInk);
  }
  if (s.cursor && !s.pending.empty()) {
    if (s.pending.size() == 1) {
      batch.line(w(s.pending[0].position), w(*s.cursor), draftInk);
    } else {
      std::vector<vp::Vec3f> pts;
      for (auto& p : arcOutline(s.pending[0].position, s.pending[1].position, *s.cursor)) pts.push_back(w(p));
      batch.polyline(pts, draftInk);
    }
  }
  if (s.chainEnd && *s.chainEnd >= 0 && size_t(*s.chainEnd) < sketch.points.size() && s.cursor)
    batch.line(w(sketch.points[size_t(*s.chainEnd)]), w(*s.cursor), draftInk);
  // Alignment guides, dashed.
  for (auto& g : s.guides) {
    if (g.size() != 2) continue;
    int n = std::max(int(distance(g[0], g[1]) / (double(pixel) * 6)), 1);
    for (int k = 0; k < n; k += 2)
      batch.line(w(g[0] + (g[1] - g[0]) * (double(k) / n)), w(g[0] + (g[1] - g[0]) * (double(std::min(k + 1, n)) / n)),
                 C(0.95f, 0.45f, 0.1f, 0.8f));
  }
  // Guide points: small diamonds at line midpoints.
  const SnapSettings& snap = store.settings().snap;
  if (snap.showGuidePoints && snap.guidePoints) {
    double d = double(pixel) * 3;
    for (auto& l : sketch.lines) {
      Vec2d m = (sketch.points[size_t(l.a)] + sketch.points[size_t(l.b)]) / 2.0;
      batch.polyline({w(m + Vec2d(d, 0)), w(m + Vec2d(0, d)), w(m - Vec2d(d, 0)), w(m - Vec2d(0, d)), w(m + Vec2d(d, 0))},
                     C(0.5f, 0.55f, 0.65f, 0.6f));
    }
  }
  if (s.cursor) {
    double r = double(pixel) * 6;
    Vec2d c = *s.cursor;
    batch.line(w(c - Vec2d(r, 0)), w(c + Vec2d(r, 0)), draftInk);
    batch.line(w(c - Vec2d(0, r)), w(c + Vec2d(0, r)), draftInk);
  }
}

// MARK: Annotations

json ViewportController::annotations() {
  auto arr = [](vp::Vec3f v) { return json::array({v.x, v.y, v.z}); };
  json out;
  out["viewRevision"] = viewRevision_;
  out["size"] = json::array({size_.width, size_.height});
  out["camera"] = {{"yaw", camera.yaw},
                   {"pitch", camera.pitch},
                   {"distance", camera.distance},
                   {"target", arr(camera.target)},
                   {"right", arr(camera.right())},
                   {"up", arr(camera.up())},
                   {"forward", arr(camera.forward())},
                   {"orthographic", camera.orthographic},
                   {"fieldOfView", camera.fovY * 180 / vp::kPi},
                   {"worldPerPoint", camera.worldPerPoint(size_)}};
  out["gridStep"] = gridStep();
  out["gridLabel"] = gridLabel();
  json list = json::array();
  if (store.sketch() && showConstraints) {
    const SketchSession& s = *store.sketch();
    LengthUnit u = store.settings().units;
    double offset = double(camera.worldPerPoint(size_)) * 14;
    auto anns = sketchInput_->annotations(offset);
    std::vector<SketchAnnotation> shown;
    std::vector<Vec2d> screen;
    for (auto& a : anns) {
      auto p = camera.project(V(s.plane().world(a.position)), size_);
      if (!p) continue;
      Vec2d q(p->x, p->y);
      if (a.isHint) q = q + Vec2d(34, -16);
      shown.push_back(a);
      screen.push_back(q);
    }
    screen = SketchInput::spreadAnnotations(screen);
    for (size_t i = 0; i < shown.size(); ++i) {
      const auto& a = shown[i];
      json j = {{"id", a.id},
                {"kind", a.isHint ? "hint" : (a.isDimension ? "dimension" : "constraint")},
                {"text", a.text},
                {"x", screen[i].x},
                {"y", screen[i].y},
                {"selected", a.selected},
                {"constraint", a.constraint}};
      if (a.constraint >= 0 && size_t(a.constraint) < s.sketch.constraints.size()) {
        const auto& c = s.sketch.constraints[size_t(a.constraint)];
        j["constraintKind"] = rawValue(c.kind);
        if (a.isDimension) {
          bool isLength = c.unit() == "mm";
          j["value"] = c.value;
          j["editValue"] = formatted(isLength ? c.value / scale(u) : c.value);
          j["unit"] = isLength ? symbol(u) : c.unit();
        }
      }
      list.push_back(j);
    }
  }
  out["annotations"] = list;
  json labels = json::array();
  if (showPinnedMeasurements) {
    for (auto& m : pointMeasurements) {
      auto p = camera.project(V(m.anchor()), size_);
      if (!p) continue;
      labels.push_back({{"id", m.id.string()}, {"text", m.label}, {"x", p->x}, {"y", p->y - 16}});
    }
  }
  out["measurements"] = labels;
  return out;
}

}  // namespace sculpt::api
