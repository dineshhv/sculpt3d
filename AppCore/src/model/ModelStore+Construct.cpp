// Port of ModelStore+Construct.swift (Construct ▸ Plane / Axis variants, Item
// ▸ visibility) and the model side of ViewportController+Points.swift.
#include <algorithm>
#include <cmath>
#include <limits>

#include "sculpt/model/DisplayFormat.hpp"
#include "sculpt/model/ModelStore.hpp"

namespace sculpt {

namespace {

bool isPlaneOp(const Operation& o) { return o.is<op::Plane>(); }
bool isAxisOp(const Operation& o) { return o.is<op::Axis>(); }

/// Any unit vector perpendicular to `n`.
Vec3d perpendicular(const Vec3d& n) {
  return normalize(cross(std::abs(n.z) < 0.9 ? Vec3d(0, 0, 1) : Vec3d(1, 0, 0), n));
}

SketchPlane makePlane(const Vec3d& origin, const Vec3d& normal, std::optional<Vec3d> xDir = std::nullopt) {
  Vec3d n = normalize(normal);
  Vec3d x = xDir ? *xDir - n * dot(*xDir, n) : perpendicular(n);
  if (length(x) < 1e-9) x = perpendicular(n);
  return SketchPlane{origin, n, normalize(x)};
}

}  // namespace

// MARK: Planes

void ModelStore::addPlaneThrough(const std::vector<PickedPoint>& points) {
  if (points.size() != 3) return;
  Vec3d a = points[0].position, b = points[1].position, c = points[2].position;
  Vec3d n = cross(b - a, c - a);
  if (!(length(n) > 1e-9)) return setToast(std::string("The three points are in a line"));
  begin(op::Plane{makePlane(a, n, b - a), 0, 0, "Plane " + std::to_string(count(isPlaneOp) + 1)});
}

void ModelStore::addPlaneNormalToEdge(const PickedPoint& p) {
  if (!p.tangent) return setToast(std::string("Pick a point on an edge"));
  begin(op::Plane{makePlane(p.position, *p.tangent), 0, 0, "Plane " + std::to_string(count(isPlaneOp) + 1)});
}

void ModelStore::addPlaneParallelToFace(const PickedPoint& p) {
  if (!p.normal) return setToast(std::string("Pick a point on a face"));
  std::optional<Vec3d> xDir;
  if (p.pick.kind == Pick::Kind::Face)
    if (auto info = faceInfo(p.pick.body, p.pick.index); info && info->planar) xDir = fromO3d(info->xDir);
  begin(op::Plane{makePlane(p.position, *p.normal, xDir), 0, 0, "Plane " + std::to_string(count(isPlaneOp) + 1)});
}

void ModelStore::addPlaneThroughEdge() {
  auto edges = selectedEdges();
  std::optional<o3d::EdgeInfo> info;
  if (!edges.empty()) info = edgeInfo(edges[0].first, edges[0].second);
  if (!info || !info->linear) return setToast(std::string("Select a straight edge"));
  Vec3d start = fromO3d(info->start), end = fromO3d(info->end);
  Vec3d d = normalize(end - start);
  Vec3d n0;
  auto faces = selectedFaces();
  std::optional<o3d::FaceInfo> fi;
  if (!faces.empty()) fi = faceInfo(faces[0].first, faces[0].second);
  if (fi && fi->planar) {
    n0 = fromO3d(fi->normal);
  } else {
    // The plane through the edge closest to horizontal.
    n0 = cross(d, perpendicular(d));
    Vec3d up = Vec3d(0, 0, 1) - d * d.z;
    if (length(up) > 1e-6) n0 = normalize(up);
  }
  n0 = normalize(n0 - d * dot(n0, d));
  SketchPlane base{(start + end) / 2.0, n0, d};
  selection_.clear();
  begin(op::Plane{base, 0, 30, "Plane " + std::to_string(count(isPlaneOp) + 1)});
}

// MARK: Axes

void ModelStore::addAxisThrough(const std::vector<PickedPoint>& points) {
  if (points.size() != 2) return;
  Vec3d d = points[1].position - points[0].position;
  if (!(length(d) > 1e-9)) return setToast(std::string("Pick two different points"));
  commitImmediately(op::Axis{points[0].position, normalize(d), "Axis " + std::to_string(count(isAxisOp) + 1)});
}

void ModelStore::addAxisThroughTwoPlanes() {
  std::vector<SketchPlane> planes;
  for (auto& item : selectedItems())
    if (item.isPlane()) planes.push_back(item.plane());
  for (auto& f : selectedFaces())
    if (auto info = faceInfo(f.first, f.second); info && info->planar)
      planes.push_back({fromO3d(info->centroid), fromO3d(info->normal), fromO3d(info->xDir)});
  if (planes.size() < 2) return setToast(std::string("Select two planes or flat faces"));
  const SketchPlane &p1 = planes[0], &p2 = planes[1];
  Vec3d d = cross(p1.normal, p2.normal);
  if (!(length(d) > 1e-6)) return setToast(std::string("The planes are parallel"));
  // Point on both planes closest to the first plane's origin.
  Vec3d n1 = p1.normal, n2 = p2.normal;
  double h1 = dot(n1, p1.origin), h2 = dot(n2, p2.origin);
  double dd = dot(d, d);
  Vec3d origin = (cross(d, n2) * h1 + cross(n1, d) * h2) / dd;
  selection_.clear();
  itemSelection_.clear();
  commitImmediately(op::Axis{origin, normalize(d), "Axis " + std::to_string(count(isAxisOp) + 1)});
}

void ModelStore::addAxisOfRoundFace() {
  bool hasRound = false;
  for (auto& f : selectedFaces())
    if (auto info = faceInfo(f.first, f.second); info && info->radius > 0) hasRound = true;
  for (auto& e : selectedEdges())
    if (auto info = edgeInfo(e.first, e.second); info && info->circular) hasRound = true;
  if (!hasRound) return setToast(std::string("Select a cylinder, cone or circular edge"));
  addAxis();
}

void ModelStore::addAxisAlongEdge() {
  bool straight = false;
  for (auto& e : selectedEdges())
    if (auto info = edgeInfo(e.first, e.second); info && info->linear) straight = true;
  if (!straight) return setToast(std::string("Select a straight edge"));
  addAxis();
}

void ModelStore::addAxisNormalToFace(const PickedPoint& p) {
  if (!p.normal) return setToast(std::string("Pick a point on a face"));
  commitImmediately(op::Axis{p.position, normalize(*p.normal), "Axis " + std::to_string(count(isAxisOp) + 1)});
}

// MARK: Wrap & Emboss

void ModelStore::embossSelection() {
  auto sketches = selectedSketchItems();
  if (sketches.empty()) return setToast(std::string("Select a sketch in Items, then the body to emboss"));
  Uuid sketchId = sketches.front();
  std::optional<int> body = selectedBody();
  if (!body) {
    // With no body picked, the body nearest the sketch.
    auto item = std::find_if(referenceItems_.begin(), referenceItems_.end(), [&](auto& i) { return i.id == sketchId; });
    if (item != referenceItems_.end()) {
      Vec3d origin = item->plane().origin;
      double best = std::numeric_limits<double>::max();
      for (auto& [id, m] : visibleMeshes()) {
        double d = distance(m->center(), origin);
        if (d < best) {
          best = d;
          body = id;
        }
      }
    }
  }
  if (!body) return setToast(std::string("Select the body to emboss"));
  selection_.clear();
  begin(op::Emboss{sketchId, *body, 1, false});
}

// MARK: Visibility (Item menu)

std::vector<int> ModelStore::touchedBodies() const {
  std::set<int> b;
  for (auto& p : selection_) b.insert(p.body);
  return {b.begin(), b.end()};
}

void ModelStore::toggleVisibilityOfSelection() {
  auto bodies = touchedBodies();
  if (!bodies.empty()) {
    bool allHidden = std::all_of(bodies.begin(), bodies.end(), [&](int b) { return settings_.hiddenBodies.count(b) > 0; });
    updateSettings([&](ProjectSettings& s) {
      for (int b : bodies) {
        if (allHidden) s.hiddenBodies.erase(b);
        else s.hiddenBodies.insert(b);
      }
    });
    if (!allHidden) setSelection({});
  }
  for (auto& item : selectedItems()) toggleHiddenItem(item.id);
}

void ModelStore::isolateSelection() {
  auto keep = touchedBodies();
  if (keep.empty()) return;
  if (!isolationRestore_) setIsolationRestore(settings_.hiddenBodies);
  updateSettings([&](ProjectSettings& s) {
    s.hiddenBodies.clear();
    for (int id : bodyIds())
      if (std::find(keep.begin(), keep.end(), id) == keep.end()) s.hiddenBodies.insert(id);
  });
}

void ModelStore::exitIsolation() {
  if (!isolationRestore_) return;
  std::set<int> restore = *isolationRestore_;
  setIsolationRestore(std::nullopt);
  updateSettings([&](ProjectSettings& s) {
    s.hiddenBodies.clear();
    for (int id : restore)
      if (meshes_.count(id)) s.hiddenBodies.insert(id);
  });
}

void ModelStore::showAllBodies() {
  setIsolationRestore(std::nullopt);
  updateSettings([](ProjectSettings& s) { s.hiddenBodies.clear(); });
}

void ModelStore::invertBodyVisibility() {
  setIsolationRestore(std::nullopt);
  updateSettings([&](ProjectSettings& s) {
    std::set<int> inverted;
    for (int id : bodyIds())
      if (!s.hiddenBodies.count(id)) inverted.insert(id);
    s.hiddenBodies = inverted;
  });
  std::vector<Pick> kept;
  for (auto& p : selection_)
    if (!settings_.hiddenBodies.count(p.body)) kept.push_back(p);
  setSelection(std::move(kept));
}

// MARK: Point picks (ViewportController+Points.swift)

PointPickSession PointPickSession::make(PointPickPurpose purpose) {
  using K = PickedPoint::Kind;
  using P = PointPickPurpose;
  switch (purpose) {
    case P::planeThrough3Points:
      return {purpose, "Plane Through 3 Points", {"Pick the first point", "Pick the second point", "Pick the third point"}};
    case P::planeOnCurveAtPoint:
      return {purpose, "Plane on Curve at Point", {"Pick a point on a curved edge"}, {K::edge}};
    case P::planePerpendicularToEdgeAtPoint:
      return {purpose, "Plane Perpendicular to Edge at Point", {"Pick a point on an edge"}, {K::edge}};
    case P::planeParallelToFaceAtPoint:
      return {purpose, "Plane Parallel to Face at Point", {"Pick a point on a face"}, {K::face}};
    case P::axisThrough2Points:
      return {purpose, "Axis Through 2 Points", {"Pick the first point", "Pick the second point"}};
    case P::axisPerpendicularToFaceAtPoint:
      return {purpose, "Axis Perpendicular to Face at Point", {"Pick a point on a face"}, {K::face}};
    case P::measurePointToPoint:
      return {purpose, "Point-to-Point Distance", {"Pick the first point", "Pick the second point"}};
    case P::measureThreePointAngle:
      return {purpose,
              "3-Point Angle",
              {"Pick a point on the first arm", "Pick the corner", "Pick a point on the second arm"}};
  }
  return {purpose, "", {}};
}

std::optional<std::string> PointPickSession::rejection(const PickedPoint& p) const {
  if (accepts.empty()) return std::nullopt;
  auto kind = accepts.size() > points.size() ? accepts[points.size()] : accepts.back();
  // A corner is also a point on its edges.
  bool ok = kind == p.kind || (kind == PickedPoint::Kind::edge && p.kind == PickedPoint::Kind::vertex);
  if (ok) return std::nullopt;
  return std::string(kind == PickedPoint::Kind::face ? "Pick a point on a face" : "Pick a point on an edge");
}

PointPickSession ModelStore::beginPointPick(PointPickPurpose purpose) {
  setSelection({});
  return PointPickSession::make(purpose);
}

std::optional<ModelStore::PointPickResult> ModelStore::completePointPick(PointPickPurpose purpose,
                                                                         const std::vector<PickedPoint>& pts,
                                                                         bool measureDeltas) {
  using P = PointPickPurpose;
  switch (purpose) {
    case P::planeThrough3Points: addPlaneThrough(pts); return std::nullopt;
    case P::planeOnCurveAtPoint:
    case P::planePerpendicularToEdgeAtPoint:
      if (!pts.empty()) addPlaneNormalToEdge(pts[0]);
      return std::nullopt;
    case P::planeParallelToFaceAtPoint:
      if (!pts.empty()) addPlaneParallelToFace(pts[0]);
      return std::nullopt;
    case P::axisThrough2Points: addAxisThrough(pts); return std::nullopt;
    case P::axisPerpendicularToFaceAtPoint:
      if (!pts.empty()) addAxisNormalToFace(pts[0]);
      return std::nullopt;
    case P::measurePointToPoint: {
      if (pts.size() != 2) return std::nullopt;
      LengthUnit u = settings_.units;
      Vec3d d = pts[1].position - pts[0].position;
      std::string label = formatLength(length(d), u);
      if (measureDeltas)
        label += "  (ΔX " + formatLength(std::abs(d.x), u) + ", ΔY " + formatLength(std::abs(d.y), u) +
                 ", ΔZ " + formatLength(std::abs(d.z), u) + ")";
      PointMeasurement m;
      m.points = {pts[0].position, pts[1].position};
      m.label = label;
      return PointPickResult{m, "Distance: " + label};
    }
    case P::measureThreePointAngle: {
      if (pts.size() != 3) return std::nullopt;
      Vec3d a = pts[0].position - pts[1].position, b = pts[2].position - pts[1].position;
      if (!(length(a) > 1e-9) || !(length(b) > 1e-9)) {
        setToast(std::string("Pick three different points"));
        return std::nullopt;
      }
      double angle = std::acos(std::min(std::max(dot(normalize(a), normalize(b)), -1.0), 1.0)) * 180 / kPi;
      PointMeasurement m;
      m.points = {pts[0].position, pts[1].position, pts[2].position};
      m.label = formatAngle(angle);
      return PointPickResult{m, "Angle: " + m.label};
    }
  }
  return std::nullopt;
}

}  // namespace sculpt
