// Camera, picking, input and keys of the viewport controller
// (ViewportController.swift, +Sketch, +Points, +Keys, ViewportView.swift).
#include "api/ViewportController.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

#include "sculpt/model/DisplayFormat.hpp"
#include "sculpt/model/ToolCatalog.hpp"
#include "sculpt/sculpt_c.h"

namespace sculpt::api {

namespace {
vp::Vec3f toVp(const Vec3f& v) { return {v.x, v.y, v.z}; }
vp::Vec3f toVp(const Vec3d& v) { return {float(v.x), float(v.y), float(v.z)}; }
Vec3d toModel(const vp::Vec3f& v) { return {double(v.x), double(v.y), double(v.z)}; }
Pick fromVp(const vp::Pick& p) {
  switch (p.kind) {
    case vp::Pick::Kind::Body: return Pick::bodyPick(p.body);
    case vp::Pick::Kind::Face: return Pick::face(p.body, p.index);
    case vp::Pick::Kind::Edge: return Pick::edge(p.body, p.index);
  }
  return Pick::bodyPick(p.body);
}
vp::CameraSettings toVp(const CameraSettings& s) { return {s.orthographic, s.fieldOfView}; }
}  // namespace

const std::vector<DisplayModeInfo>& displayModes() {
  static const std::vector<DisplayModeInfo> all = {
      {vp::DisplayMode::Shaded, "shaded", "Shaded", "cube.fill", 's'},
      {vp::DisplayMode::Wireframe, "wireframe", "Wireframe", "cube", 'w'},
      {vp::DisplayMode::XRay, "xray", "X-Ray", "cube.transparent", 'x'},
      {vp::DisplayMode::Visualized, "visualized", "Visualized", "sparkles", 'v'},
      {vp::DisplayMode::Zebra, "zebra", "Zebra Analysis", "line.3.horizontal", 'z'},
      {vp::DisplayMode::Curvature, "curvature", "Curvature Map", "circle.dotted.and.circle", 'c'},
  };
  return all;
}

const DisplayModeInfo& info(vp::DisplayMode m) {
  for (auto& d : displayModes())
    if (d.mode == m) return d;
  return displayModes()[0];
}

std::optional<vp::DisplayMode> displayMode(const std::string& raw) {
  for (auto& d : displayModes())
    if (raw == d.raw) return d.mode;
  return std::nullopt;
}

ViewportController::ViewportController(ModelStore& s) : store(s), sketchInput_(std::make_unique<SketchInput>(s)) {
  clock = [] {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
  };
  camera.setSettings(toVp(store.settings().camera));
}

// MARK: Revisions

void ViewportController::stateChanged() {
  ++stateRevision_;
  redraw();
}

void ViewportController::redraw() {
  ++viewRevision_;
  needsRedraw_ = true;
}

void ViewportController::cameraChanged() { redraw(); }

void ViewportController::setSize(float w, float h) {
  vp::Size2 s{std::max(w, 1.0f), std::max(h, 1.0f)};
  if (s.width == size_.width && s.height == size_.height) return;
  bool first = !sized_;
  size_ = s;
  sized_ = true;
  // A document opened before the view was laid out was framed for a 1×1
  // view; frame it again now that the aspect is known.
  if (first && fittedUnsized_) fitAll();
  redraw();
}

void ViewportController::storeChanged() {
  if (!camera.matches(toVp(store.settings().camera))) camera.setSettings(toVp(store.settings().camera));
  bool sketching = store.sketch().has_value();
  // Frame the model once when a document opens, not when the first preview
  // appears mid-sketch.
  if (sketching || store.draft()) hasFitted_ = true;
  if (!hasFitted_ && !store.meshes().empty()) {
    hasFitted_ = true;
    fittedUnsized_ = !sized_;
    fitAll();
  }
  // ContentView: `.onChange(of: store.sketch != nil) { if $0 { faceSketchPlane() } }`.
  if (sketching && !wasSketching_) faceSketchPlane();
  if (!sketching && wasSketching_) sketchInput_ = std::make_unique<SketchInput>(store);
  wasSketching_ = sketching;
  redraw();
}

// MARK: View state

std::pair<double, double> ViewportController::sectionRange() const {
  auto meshes = store.visibleMeshes();
  if (meshes.empty()) return {-100, 100};
  double lo = std::numeric_limits<double>::infinity(), hi = -lo;
  for (auto& [id, m] : meshes) {
    lo = std::min(lo, double(m->boundsMin[sectionAxis]));
    hi = std::max(hi, double(m->boundsMax[sectionAxis]));
  }
  return {lo, std::max(hi, lo + 1)};
}

void ViewportController::setSectionEnabled(bool on) {
  bool old = sectionEnabled;
  sectionEnabled = on;
  if (on && !old) {
    auto r = sectionRange();
    sectionOffset = r.first / 2 + r.second / 2;
  }
  stateChanged();
}

void ViewportController::setSectionAxis(int axis) {
  axis = std::min(std::max(axis, 0), 2);
  if (axis != sectionAxis) {
    sectionAxis = axis;
    auto r = sectionRange();
    sectionOffset = r.first / 2 + r.second / 2;
  }
  stateChanged();
}

void ViewportController::setSectionOffset(double offset) {
  sectionOffset = offset;
  stateChanged();
}

void ViewportController::setSectionFlipped(bool on) {
  sectionFlipped = on;
  stateChanged();
}

std::optional<vp::Vec4f> ViewportController::sectionPlane() const {
  if (!sectionEnabled) return std::nullopt;
  vp::Vec3f n = toVp(worldAxes()[size_t(sectionAxis)]);
  float d = float(sectionOffset);
  if (sectionFlipped) {
    n = -n;
    d = -d;
  }
  return vp::Vec4f{n.x, n.y, n.z, d};
}

void ViewportController::setMeasuring(bool on) {
  measuring = on;
  stateChanged();
}

void ViewportController::setVisualizing(bool on) {
  visualizing = on;
  stateChanged();
}

void ViewportController::setDisplayMode(vp::DisplayMode m) {
  displayMode = m;
  stateChanged();
}

void ViewportController::requestUi(const std::string& action) {
  uiRequest = action;
  ++uiRequestSerial;
  stateChanged();
}

bool ViewportController::performAction(const std::string& action) {
  if (action.empty()) return true;
  if (action == "view.fit") return fitAll(), true;
  if (action == "view.iso") return look(vp::Camera::Preset::Iso), true;
  if (action == "view.top") return look(vp::Camera::Preset::Top), true;
  if (action == "view.front") return look(vp::Camera::Preset::Front), true;
  if (action == "view.right") return look(vp::Camera::Preset::Right), true;
  if (action == "view.toggleSection") return setSectionEnabled(!sectionEnabled), true;
  if (action == "view.toggleMeasure") return setMeasuring(!measuring), true;
  if (action.rfind("view.mode.", 0) == 0) {
    if (auto m = sculpt::api::displayMode(action.substr(10))) {
      setDisplayMode(*m);
      return true;
    }
  }
  return false;
}

// MARK: Camera

void ViewportController::orbit(float dx, float dy) {
  camera.orbit(dx, dy);
  cameraChanged();
}

void ViewportController::pan(float dx, float dy) {
  camera.pan(dx, dy, size_);
  cameraChanged();
}

void ViewportController::zoom(float factor, std::optional<vp::Vec2f> at) {
  if (!(factor > 0) || !std::isfinite(factor)) return;
  if (at) {
    if (auto hit = surfaceHit(*at)) {
      camera.zoom(factor, hit->point);
      cameraChanged();
      return;
    }
  }
  camera.zoom(factor);
  cameraChanged();
}

void ViewportController::look(vp::Camera::Preset preset) {
  camera.look(preset);
  cameraChanged();
}

void ViewportController::fitAll() {
  const auto& meshes = visibleMeshes();
  if (meshes.empty()) {
    camera = vp::Camera();
    camera.setSettings(toVp(store.settings().camera));
    cameraChanged();
    return;
  }
  vp::Vec3f lo{3.4e38f, 3.4e38f, 3.4e38f}, hi{-3.4e38f, -3.4e38f, -3.4e38f};
  for (auto& [id, m] : meshes) {
    lo = vp::vmin(lo, m->boundsMin);
    hi = vp::vmax(hi, m->boundsMax);
  }
  camera.fit(lo, hi, size_.width / std::max(size_.height, 1.0f));
  cameraChanged();
}

void ViewportController::faceSketchPlane() {
  if (!store.sketch()) return;
  const SketchSession& s = *store.sketch();
  SketchPlane plane = s.plane();
  camera.face(toVp(plane.normal), toVp(plane.origin), toVp(plane.xDir));
  vp::Vec3f r = camera.right();
  Vec3d right(r.x, r.y, r.z);
  bool aligned = dot(right, plane.xDir) > 0.999;
  // When the camera can't line up with the plane's X axis (a tilted face) and
  // the sketch is still empty, turn the axis to match the screen so rectangles
  // come out screen-aligned.
  if (!aligned && s.sketch.isEmpty() && !s.chainEnd) {
    Vec3d n = plane.normal;
    Vec3d x = right - n * dot(right, n);
    if (length(x) > 1e-6) {
      Vec3d xd = normalize(x);
      store.updateSketch([&](SketchSession& session) { session.sketch.plane.xDir = xd; });
    }
  }
  cameraChanged();
}

void ViewportController::zoomToSelection() {
  std::set<int> ids;
  for (auto& p : store.selection()) ids.insert(p.body);
  vp::Vec3f lo{3.4e38f, 3.4e38f, 3.4e38f}, hi{-3.4e38f, -3.4e38f, -3.4e38f};
  bool any = false;
  for (auto& [id, m] : visibleMeshes()) {
    if (!ids.count(id)) continue;
    any = true;
    lo = vp::vmin(lo, m->boundsMin);
    hi = vp::vmax(hi, m->boundsMax);
  }
  if (!any) return fitAll();
  camera.fit(lo, hi, size_.width / std::max(size_.height, 1.0f));
  cameraChanged();
}

void ViewportController::snapToNearestOrtho() {
  camera.snapToNearestOrtho();
  cameraChanged();
}

void ViewportController::rotateView(float yawSteps, float pitchSteps) {
  camera.rotate(yawSteps, pitchSteps);
  cameraChanged();
}

void ViewportController::lookAtSection() {
  if (!sectionEnabled) setSectionEnabled(true);
  vp::Vec3f n = toVp(worldAxes()[size_t(sectionAxis)]) * (sectionFlipped ? -1.0f : 1.0f);
  camera.face(-n, camera.target, sectionAxis == 0 ? vp::Vec3f{0, 1, 0} : vp::Vec3f{1, 0, 0});
  store.updateSettings([](ProjectSettings& s) { s.camera.orthographic = true; });
  camera.setSettings(toVp(store.settings().camera));
  fitAll();
}

void ViewportController::setOrientation(float yaw, float pitch) {
  camera.yaw = yaw;
  camera.pitch = std::min(std::max(pitch, -1.55f), 1.55f);
  cameraChanged();
}

// MARK: Picking

const vp::MeshMap& ViewportController::visibleMeshes() {
  if (meshesRevision_ == store.revision()) return visible_;
  meshesRevision_ = store.revision();
  auto source = store.visibleMeshes();
  vp::MeshMap out;
  std::map<int, std::pair<std::shared_ptr<const BodyMesh>, std::shared_ptr<const vp::BodyMesh>>> converted;
  bool changed = source.size() != visible_.size();
  for (auto& [id, m] : source) {
    std::shared_ptr<const vp::BodyMesh> v;
    auto it = converted_.find(id);
    if (it != converted_.end() && it->second.first == m) {
      v = it->second.second;
    } else {
      auto b = std::make_shared<vp::BodyMesh>();
      b->id = m->id;
      b->positions.reserve(m->positions.size());
      for (auto& p : m->positions) b->positions.push_back(toVp(p));
      b->normals.reserve(m->normals.size());
      for (auto& p : m->normals) b->normals.push_back(toVp(p));
      b->vertexFace = m->vertexFace;
      b->indices = m->indices;
      b->edgePositions.reserve(m->edgePositions.size());
      for (auto& p : m->edgePositions) b->edgePositions.push_back(toVp(p));
      b->edgeIds = m->edgeIds;
      b->faceCount = m->faceCount;
      b->edgeCount = m->edgeCount;
      b->boundsMin = toVp(m->boundsMin);
      b->boundsMax = toVp(m->boundsMax);
      b->shapeKey = m->shapeKey;
      v = b;
    }
    converted[id] = {m, v};
    auto old = visible_.find(id);
    if (old == visible_.end() || old->second != v) changed = true;
    out[id] = v;
  }
  converted_ = std::move(converted);
  visible_ = std::move(out);
  if (changed) ++sceneGeometryVersion_;
  return visible_;
}

std::optional<vp::SurfaceHit> ViewportController::surfaceHit(vp::Vec2f p) {
  return vp::surfaceHit(visibleMeshes(), camera, p, size_);
}

float ViewportController::pickTolerance() const { return vp::defaultEdgeTolerance(lastKind_ == PointerKind::Touch); }
double ViewportController::pickRadiusPoints() const { return lastKind_ == PointerKind::Touch ? 14.0 : 7.0; }
float ViewportController::pointTolerance() const { return lastKind_ == PointerKind::Touch ? 18.0f : 9.0f; }

std::optional<Pick> ViewportController::pick(vp::Vec2f p) {
  auto hit = vp::pick(visibleMeshes(), camera, p, size_, pickTolerance());
  if (!hit) return std::nullopt;
  return fromVp(*hit);
}

double ViewportController::gridStep() const { return sculpt::gridStep(double(camera.distance), fixedGrid); }

std::string ViewportController::gridLabel() const {
  LengthUnit u = store.settings().units;
  return formatted(gridStep() / scale(u)) + "\n" + symbol(u);
}

std::optional<Vec2d> ViewportController::planePoint(vp::Vec2f p) const {
  if (!store.sketch()) return std::nullopt;
  const SketchPlane& plane = store.sketch()->plane();
  vp::Ray ray = camera.ray(p, size_);
  vp::Vec3f n = sculpt::api::toVp(plane.normal), origin = sculpt::api::toVp(plane.origin);
  float denom = vp::dot(ray.direction, n);
  if (std::fabs(denom) <= 1e-6f) return std::nullopt;
  float t = vp::dot(origin - ray.origin, n) / denom;
  if (!(t > 0)) return std::nullopt;
  return plane.local(toModel(ray.origin + ray.direction * t));
}

std::optional<PickedPoint> ViewportController::pickPoint(vp::Vec2f point) {
  float tolerance = pointTolerance();
  auto surface = surfaceHit(point);
  vp::Mat4f vpm = camera.viewProjection(size_);
  vp::Size2 sz = size_;
  vp::Vec3f rayOrigin = camera.ray(point, size_).origin;
  auto screen = [&](vp::Vec3f p) -> std::optional<vp::Vec2f> {
    vp::Vec4f c = vpm * vp::Vec4f{p.x, p.y, p.z, 1};
    if (!(c.w > 0)) return std::nullopt;
    return vp::Vec2f{(c.x / c.w + 1) / 2 * sz.width, (1 - c.y / c.w) / 2 * sz.height};
  };
  // Points behind the surface under the pointer are hidden.
  auto visible = [&](vp::Vec3f p) {
    if (!surface) return true;
    return vp::distance(p, rayOrigin) <= surface->t + camera.distance * 0.01f;
  };
  vp::Vec2f q = point;
  std::optional<std::pair<PickedPoint, float>> bestVertex, bestEdge;
  for (auto& [id, mesh] : visibleMeshes()) {
    const auto& ep = mesh->edgePositions;
    const auto& ids = mesh->edgeIds;
    for (size_t i = 0; i + 1 < ep.size(); i += 2) {
      uint32_t eid = ids[i];
      vp::Vec3f d = vp::normalize(ep[i + 1] - ep[i]);
      Vec3d dir = toModel(d);
      // A corner is where an edge's polyline starts or ends.
      bool starts = i == 0 || ids[i - 1] != eid || ep[i - 1] != ep[i];
      bool ends = i + 2 >= ep.size() || ids[i + 2] != eid || ep[i + 2] != ep[i + 1];
      std::pair<bool, vp::Vec3f> corners[2] = {{starts, ep[i]}, {ends, ep[i + 1]}};
      for (auto& [flag, p] : corners) {
        if (!flag) continue;
        auto s = screen(p);
        if (!s || !visible(p)) continue;
        float dist = vp::distance(*s, q);
        if (dist <= tolerance && (!bestVertex || dist < bestVertex->second)) {
          PickedPoint pp;
          pp.position = toModel(p);
          pp.kind = PickedPoint::Kind::vertex;
          pp.pick = Pick::edge(mesh->id, int(eid));
          pp.tangent = dir;
          bestVertex = std::make_pair(pp, dist);
        }
      }
      auto a = screen(ep[i]), b = screen(ep[i + 1]);
      if (!a || !b) continue;
      vp::Vec2f ab = *b - *a;
      float t = vp::clampf(vp::dot(q - *a, ab) / std::max(vp::lengthSquared(ab), 1e-6f), 0, 1);
      float dist = vp::distance(q, *a + ab * t);
      if (!(dist <= tolerance) || (bestEdge && !(dist < bestEdge->second))) continue;
      vp::Vec3f world = ep[i] + (ep[i + 1] - ep[i]) * t;
      if (!visible(world)) continue;
      PickedPoint pp;
      pp.position = toModel(world);
      pp.kind = PickedPoint::Kind::edge;
      pp.pick = Pick::edge(mesh->id, int(eid));
      pp.tangent = dir;
      bestEdge = std::make_pair(pp, dist);
    }
  }
  if (bestVertex) return bestVertex->first;
  if (bestEdge) return bestEdge->first;
  if (!surface || surface->pick.kind != vp::Pick::Kind::Face) return std::nullopt;
  int body = surface->pick.body, face = surface->pick.index;
  PickedPoint pp;
  pp.position = toModel(surface->point);
  pp.kind = PickedPoint::Kind::face;
  pp.pick = Pick::face(body, face);
  auto info = store.faceInfo(body, face);
  if (info && info->planar) {
    pp.normal = fromO3d(info->normal);
  } else {
    auto it = store.meshes().find(body);
    Vec3d n;
    if (it != store.meshes().end() &&
        it->second->nearestFaceNormal(face, Vec3f(surface->point.x, surface->point.y, surface->point.z), n))
      pp.normal = n;
  }
  return pp;
}

// MARK: Input

void ViewportController::prepareSketchInput() {
  sketchInput_->pickRadius = double(camera.worldPerPoint(size_)) * pickRadiusPoints();
  sketchInput_->gridStep = gridStep();
}

static bool isSelectLike(SketchTool t) {
  switch (t) {
    case SketchTool::select: case SketchTool::rectangle: case SketchTool::centerRectangle: case SketchTool::circle:
    case SketchTool::ellipse: case SketchTool::polygon:
      return true;
    default:
      return false;
  }
}

void ViewportController::sketchTap(vp::Vec2f point, bool additive) {
  if (!store.sketch()) return;
  prepareSketchInput();
  auto p = planePoint(point);
  if (!p) {
    // Off the plane: nothing is hit, so taps that select just clear.
    if (isSelectLike(store.sketch()->tool) && !additive) store.setSketchSelection({});
    return;
  }
  sketchInput_->tap(*p, additive);
  redraw();
}

void ViewportController::tap(vp::Vec2f point, bool additive) {
  if (store.sketch()) return sketchTap(point, additive);
  if (store.draft()) return;
  if (pointPickTap(point)) return;
  if (measuring) {
    // Measure keeps the last two picks.
    auto p = pick(point);
    if (!p) return store.setSelection({});
    std::vector<Pick> sel = store.selection();
    auto it = std::find(sel.begin(), sel.end(), *p);
    if (it != sel.end()) {
      sel.erase(it);
    } else {
      sel.push_back(*p);
      if (sel.size() > 2) sel.erase(sel.begin(), sel.end() - 2);
    }
    store.setSelection(sel);
    redraw();
    return;
  }
  bodiesBeforeTap_.clear();
  for (auto& p : store.selection())
    if (p.kind == Pick::Kind::Body) bodiesBeforeTap_.push_back(p);
  store.select(pick(point), additive);
  redraw();
}

void ViewportController::doubleTap(vp::Vec2f point, bool additive) {
  // In a sketch, quick successive clicks are separate picks.
  if (store.sketch()) return sketchTap(point, additive);
  if (store.draft()) return;
  if (pointPick || measuring) return tap(point, additive);
  auto hit = pick(point);
  if (!hit) return store.select(std::nullopt, false);
  Pick body = Pick::bodyPick(hit->body);
  // The first click of a double click already ran tap(), so use the body
  // selection from before it.
  std::vector<Pick> bodies = bodiesBeforeTap_;
  std::vector<Pick> sel;
  if (std::find(bodies.begin(), bodies.end(), body) != bodies.end()) {
    for (auto& b : bodies)
      if (b != body) sel.push_back(b);
  } else {
    // Keep at most one previous body so two can be combined.
    if (!bodies.empty()) sel.push_back(bodies.back());
    sel.push_back(body);
  }
  store.setSelection(sel);
  redraw();
}

void ViewportController::hover(std::optional<vp::Vec2f> point) {
  if (store.sketch()) {
    prepareSketchInput();
    std::optional<Vec2d> p = point ? planePoint(*point) : std::nullopt;
    if (sketchInput_->hover(p)) redraw();
    return;
  }
  if (pointPickHover(point)) return;
  std::optional<Pick> h = point ? pick(*point) : std::nullopt;
  if (h != store.hover()) {
    store.setHover(h);
    redraw();
  }
}

bool ViewportController::dragDraws(vp::Vec2f point) {
  if (!store.sketch()) return false;
  prepareSketchInput();
  auto p = planePoint(point);
  if (!p) return store.sketch()->tool != SketchTool::select && drags(store.sketch()->tool);
  return sketchInput_->dragDraws(*p);
}

void ViewportController::drawBegan(vp::Vec2f point) {
  prepareSketchInput();
  sketchInput_->drawBegan(planePoint(point));
  redraw();
}

void ViewportController::drawChanged(vp::Vec2f point) {
  prepareSketchInput();
  sketchInput_->drawChanged(planePoint(point));
  redraw();
}

void ViewportController::drawEnded(vp::Vec2f point) {
  prepareSketchInput();
  sketchInput_->drawEnded(planePoint(point));
  redraw();
}

// MARK: Raw pointer recognition

void ViewportController::endGesture(bool completed) {
  if (!gesture_) return;
  Gesture g = *gesture_;
  gesture_.reset();
  if (!completed && g.drawing && !g.cancelled) drawEnded(g.last);
}

void ViewportController::pointer(int id, PointerKind kind, int button, int phase, vp::Vec2f p, int modifiers) {
  bool additive = (modifiers & (SCULPT_MOD_SHIFT | SCULPT_MOD_CONTROL)) != 0;
  switch (phase) {
    case SCULPT_PHASE_HOVER:
      lastKind_ = kind;
      if (!gesture_) hover(p);
      return;
    case SCULPT_PHASE_EXIT:
      if (!gesture_) hover(std::nullopt);
      return;
    case SCULPT_PHASE_DOWN: {
      if (gesture_ || !extraTouches_.empty()) {
        // A second finger: the one-finger drag/tap recogniser gives up (iOS
        // cancels a max-one-touch pan); two-finger pan and pinch are the
        // host's gestures (sculpt_view_pan / sculpt_view_zoom).
        if (kind == PointerKind::Touch && (!gesture_ || id != gesture_->id)) {
          extraTouches_[id] = p;
          if (gesture_ && !gesture_->cancelled) {
            if (gesture_->drawing) drawEnded(gesture_->last);
            gesture_->drawing = false;
            gesture_->cancelled = true;
          }
        }
        return;
      }
      lastKind_ = kind;
      Gesture g;
      g.id = id;
      g.kind = kind;
      g.button = button;
      g.down = g.last = p;
      g.downTime = clock();
      gesture_ = g;
      return;
    }
    case SCULPT_PHASE_MOVE: {
      if (!gesture_) {
        if (kind == PointerKind::Touch) {
          if (extraTouches_.count(id)) extraTouches_[id] = p;
        } else {
          lastKind_ = kind;
          hover(p);
        }
        return;
      }
      Gesture& g = *gesture_;
      if (id != g.id) {
        if (extraTouches_.count(id)) extraTouches_[id] = p;
        return;
      }
      if (g.cancelled) {
        g.last = p;
        return;
      }
      if (!g.dragging) {
        float slop = g.kind == PointerKind::Touch ? 10.0f : 3.0f;
        if (vp::distance(p, g.down) <= slop) return;
        g.dragging = true;
        if (g.button == SCULPT_BUTTON_PRIMARY) {
          g.drawing = dragDraws(g.down);
          if (g.drawing) {
            drawBegan(g.down);
            drawChanged(p);
          }
        }
        g.last = p;
        return;
      }
      vp::Vec2f d = p - g.last;
      g.last = p;
      if (g.drawing) {
        drawChanged(p);
      } else if (g.button == SCULPT_BUTTON_SECONDARY) {
        orbit(d.x, d.y);
      } else if (g.button == SCULPT_BUTTON_MIDDLE) {
        pan(d.x, d.y);
      } else if (g.kind != PointerKind::Touch && (modifiers & (SCULPT_MOD_ALT | SCULPT_MOD_SHIFT))) {
        // ⌥- or ⇧-drag pans on the Mac.
        pan(d.x, d.y);
      } else {
        orbit(d.x, d.y);
      }
      return;
    }
    case SCULPT_PHASE_UP: {
      if (!gesture_ || id != gesture_->id) {
        extraTouches_.erase(id);
        return;
      }
      Gesture g = *gesture_;
      gesture_.reset();
      if (g.cancelled) return;
      if (g.drawing) {
        drawEnded(p);
        return;
      }
      if (g.dragging || g.button != SCULPT_BUTTON_PRIMARY) return;
      double now = clock();
      bool touch = g.kind == PointerKind::Touch;
      double interval = touch ? 0.35 : 0.5;
      float radius = touch ? 30.0f : 6.0f;
      bool isDouble = !lastTapWasDouble_ && lastTapTime_ >= 0 && now - lastTapTime_ <= interval &&
                      vp::distance(p, lastTapPoint_) <= radius;
      if (isDouble) {
        doubleTap(p, additive);
      } else {
        tap(p, additive);
      }
      lastTapWasDouble_ = isDouble;
      lastTapTime_ = now;
      lastTapPoint_ = p;
      return;
    }
    case SCULPT_PHASE_CANCEL:
      if (gesture_ && id == gesture_->id) {
        endGesture(false);
      } else {
        extraTouches_.erase(id);
      }
      return;
    default:
      return;
  }
}

// MARK: Point picks

void ViewportController::beginPointPick(PointPickPurpose purpose) {
  measuring = false;
  pointPick = store.beginPointPick(purpose);
  stateChanged();
}

void ViewportController::cancelPointPick() {
  pointPick.reset();
  stateChanged();
}

void ViewportController::remember(const std::string& text) {
  recentMeasurements.erase(std::remove(recentMeasurements.begin(), recentMeasurements.end(), text),
                           recentMeasurements.end());
  recentMeasurements.insert(recentMeasurements.begin(), text);
  if (recentMeasurements.size() > 8) recentMeasurements.pop_back();
  stateChanged();
}

bool ViewportController::pointPickTap(vp::Vec2f point) {
  if (!pointPick) return false;
  auto p = pickPoint(point);
  if (!p) return true;
  if (auto why = pointPick->rejection(*p)) {
    store.setToast(*why);
    return true;
  }
  pointPick->points.push_back(*p);
  if (pointPick->points.size() >= pointPick->needed()) {
    PointPickSession session = *pointPick;
    pointPick.reset();
    stateChanged();
    if (auto result = store.completePointPick(session.purpose, session.points, measureDeltas)) {
      pointMeasurements.push_back(result->measurement);
      remember(result->recentText);
      showPinnedMeasurements = true;
    }
  }
  stateChanged();
  return true;
}

bool ViewportController::pointPickHover(std::optional<vp::Vec2f> point) {
  if (!pointPick) return false;
  std::optional<PickedPoint> p = point ? pickPoint(*point) : std::nullopt;
  auto same = [](const std::optional<PickedPoint>& a, const std::optional<PickedPoint>& b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a) return true;
    return a->position == b->position && a->kind == b->kind && a->pick == b->pick;
  };
  if (!same(p, pointPick->hover)) {
    pointPick->hover = p;
    redraw();
  }
  return true;
}

const std::optional<o3d::Measurement>& ViewportController::measurement() {
  if (measureCache_ && measureCache_->selection == store.selection() &&
      measureCache_->version == store.geometryVersion())
    return measureCache_->result;
  measureCache_ = MeasureCache{store.selection(), store.geometryVersion(), store.measureSelection()};
  return measureCache_->result;
}

// MARK: Keys

namespace {
std::string lowerKey(const std::string& k) {
  if (k.size() != 1) return k;
  return std::string(1, char(std::tolower((unsigned char)k[0])));
}
}  // namespace

bool ViewportController::key(const std::string& rawKey, int modifiers) {
  if (rawKey.empty()) return false;
  bool ctrl = modifiers & SCULPT_MOD_CONTROL, shift = modifiers & SCULPT_MOD_SHIFT, alt = modifiers & SCULPT_MOD_ALT;
  std::string k = lowerKey(rawKey);
  bool isEscape = k == "Escape" || k == "\x1b";
  bool isDelete = k == "Delete" || k == "Backspace" || k == "\x7f" || k == "\b";
  bool isReturn = k == "Enter" || k == "Return" || k == "\r" || k == "\n";
  bool idle = !store.draft() && !store.sketch();

  if (isEscape) {
    // +Keys: Escape cancels a point pick; then the bars' Escape buttons.
    if (pointPick) return cancelPointPick(), true;
    if (store.sketch()) return store.sketchEscape(), true;
    if (store.draft()) return store.cancelDraft(), true;
    if (!store.selection().empty()) return store.setSelection({}), true;
    return false;
  }
  if (isReturn && !ctrl && !alt) {
    if (store.draft()) {
      if (!store.draftError()) store.applyDraft();
      return true;
    }
    if (store.sketch()) {
      const SketchSession& s = *store.sketch();
      // Return finishes the sketch, except while a dimension is selected (its field takes it).
      if (s.selectedDimension()) return false;
      if (s.replacing || !s.sketch.isEmpty()) store.finishSketch(false);
      return true;
    }
    return false;
  }

  auto runTool = [&](const std::string& id) {
    auto action = ToolCatalog::run(store, id);
    if (!action) return false;
    if (!performAction(*action)) requestUi(*action);
    return true;
  };

  if (ctrl) {
    if (isDelete) {
      if (store.sketch()) return store.deleteSketchSelection(), true;
      if (store.selectedBody() && !store.draft()) return store.deleteSelection(), true;
      return false;
    }
    if (k.size() != 1) return false;
    char c = k[0];
    if (alt) {
      if (c == 'm') return setMeasuring(!measuring), true;
      if (c == 's') return setSectionEnabled(!sectionEnabled), true;
      return false;  // ⌥⌘P properties, ⌥⌘S items… are the host's
    }
    if (c == 'z') {
      if (store.sketch()) {
        if (shift ? !store.canRedoSketch() : !store.canUndoSketch()) return false;
        shift ? store.redoSketch() : store.undoSketch();
      } else {
        if (shift ? !store.canRedo() : !store.canUndo()) return false;
        if (store.draft()) return false;
        shift ? store.redo() : store.undo();
      }
      return true;
    }
    if (c == 'y' && !shift) return key("z", SCULPT_MOD_CONTROL | SCULPT_MOD_SHIFT);
    if (c == 'a') {
      if (shift) {
        store.setSelection({});
        store.setItemSelection({});
        return true;
      }
      if (!idle) return false;
      std::vector<Pick> sel;
      for (int id : store.bodyIds())
        if (!store.settings().hiddenBodies.count(id)) sel.push_back(Pick::bodyPick(id));
      store.setSelection(sel);
      return true;
    }
    if (c == 'd' && !shift) return runTool("transform.duplicate");
    if (c == 'u' && !shift) return runTool("tools.union");
    if (c == 'b' && !shift) return runTool("tools.subtract");
    if (c == 'i' && !shift) return runTool("tools.intersect");
    if (c == 'f' && shift) return fitAll(), true;
    if (c == 'h' && shift) {
      if (store.selection().empty() && store.itemSelection().empty()) return false;
      store.toggleVisibilityOfSelection();
      return true;
    }
    static const vp::Camera::Preset presets[] = {vp::Camera::Preset::Iso, vp::Camera::Preset::Front,
                                                 vp::Camera::Preset::Back, vp::Camera::Preset::Top,
                                                 vp::Camera::Preset::Bottom, vp::Camera::Preset::Right,
                                                 vp::Camera::Preset::Left};
    if (c >= '1' && c <= '7' && !shift) return look(presets[c - '1']), true;
    if (c == '8' && !shift) return snapToNearestOrtho(), true;
    return false;  // ⌘F search, ⇧⌘E export, ⇧⌘I import… are the host's menus
  }

  if (alt) {
    if (k.size() != 1) return false;
    char c = k[0];
    if (c == 'h') {
      if (shift) {
        if (store.bodyIds().empty()) return false;
        store.invertBodyVisibility();
      } else {
        if (store.settings().hiddenBodies.empty()) return false;
        store.showAllBodies();
      }
      return true;
    }
    if (shift) return false;
    for (auto& d : displayModes())
      if (d.key == c) return setDisplayMode(d.mode), true;
    return false;
  }

  if (store.sketch()) {
    if (shift && k.size() == 1) {
      // The constraint column's ⇧ shortcuts.
      const SketchSession& s = *store.sketch();
      auto kinds = s.availableConstraints();
      auto has = [&](SketchConstraint::Kind kind) { return std::find(kinds.begin(), kinds.end(), kind) != kinds.end(); };
      using K = SketchConstraint::Kind;
      auto constrain = [&](K kind) {
        if (!has(kind)) return false;
        store.constrainSelection(kind);
        return true;
      };
      switch (k[0]) {
        case 'a': return constrain(K::Parallel);
        case 'p': return constrain(K::Perpendicular);
        case 't': return constrain(K::Tangent);
        case 'n':
          if (has(K::Coincident)) return constrain(K::Coincident);
          if (has(K::PointOnLine)) return constrain(K::PointOnLine);
          return constrain(K::PointOnCircle);
        case 'm': return constrain(K::Midpoint);
        case 'c':
          if (s.selectedCircles().size() == 2 && s.selectedPoints().empty() && s.selectedLines().empty()) {
            store.constrainSelection(K::Coincident);
            return true;
          }
          return false;
        case 'v':
          if (!s.selectedLines().empty() && s.selectedPoints().empty() && s.selectedCircles().empty()) {
            store.constrainHorizontalOrVertical();
            return true;
          }
          return false;
        case 'e':
          if (has(K::EqualLength)) return constrain(K::EqualLength);
          return constrain(K::EqualRadius);
        case 's': return constrain(K::Symmetric);
        case 'l': return constrain(K::Fixed);
        default: return false;
      }
    }
    if (k == "o") {
      if (store.sketch()->selection.empty()) return false;
      store.offsetSketchSelection();
      return true;
    }
    auto handled = ToolCatalog::handleKey(store, isDelete ? std::string("Delete") : k);
    return handled.has_value();
  }

  if (isDelete) {
    // Edit ▸ Delete (⌫).
    if (!store.selectedBody() || store.draft()) return false;
    store.deleteSelection();
    return true;
  }
  if (shift || k.size() != 1) return false;
  auto action = ToolCatalog::handleKey(store, k);
  if (!action) return false;
  if (!performAction(*action)) requestUi(*action);
  return true;
}

}  // namespace sculpt::api
