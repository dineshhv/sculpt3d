// Port of ModelStore+Tools.swift (Construct, Transform, Tools, Visualize
// commands, measure, inserts and variables) and the ModelStore extension in
// Terrain.swift.
#include <algorithm>
#include <cmath>
#include <random>

#include "sculpt/model/DisplayFormat.hpp"
#include "sculpt/model/Expression.hpp"
#include "sculpt/model/Image.hpp"
#include "sculpt/model/ModelStore.hpp"
#include "sculpt/model/Sculpt3DFile.hpp"

namespace sculpt {

namespace {
bool isPlaneOp(const Operation& o) { return o.is<op::Plane>(); }
bool isAxisOp(const Operation& o) { return o.is<op::Axis>(); }
bool isProjectionOp(const Operation& o) { return o.is<op::Projection>(); }

std::string trimmed(const std::string& s) {
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return s;
}

std::vector<int> uniqueBodies(const std::vector<Pick>& selection) {
  std::vector<int> out;
  for (auto& p : selection)
    if (std::find(out.begin(), out.end(), p.body) == out.end()) out.push_back(p.body);
  return out;
}
}  // namespace

std::map<int, std::shared_ptr<const BodyMesh>> ModelStore::visibleMeshes() const {
  std::map<int, std::shared_ptr<const BodyMesh>> out;
  for (auto& [id, m] : meshes_)
    if (!settings_.hiddenBodies.count(id)) out[id] = m;
  return out;
}

std::vector<ReferenceItem> ModelStore::selectedItems() const {
  std::vector<ReferenceItem> out;
  for (auto& id : itemSelection_)
    for (auto& item : referenceItems_)
      if (item.id == id) {
        out.push_back(item);
        break;
      }
  return out;
}

std::optional<int> ModelStore::selectedBody() const {
  if (selection_.empty()) return std::nullopt;
  return selection_.front().body;
}

// MARK: Items

void ModelStore::toggleItemSelection(const Uuid& id) {
  auto s = itemSelection_;
  auto it = std::find(s.begin(), s.end(), id);
  if (it != s.end()) s.erase(it);
  else s.push_back(id);
  setItemSelection(std::move(s));
}

void ModelStore::toggleHiddenBody(int body) {
  updateSettings([&](ProjectSettings& s) {
    if (s.hiddenBodies.count(body)) s.hiddenBodies.erase(body);
    else s.hiddenBodies.insert(body);
  });
  std::vector<Pick> kept;
  for (auto& p : selection_)
    if (p.body != body) kept.push_back(p);
  setSelection(std::move(kept));
}

void ModelStore::toggleHiddenItem(const Uuid& id) {
  updateSettings([&](ProjectSettings& s) {
    if (s.hiddenItems.count(id)) s.hiddenItems.erase(id);
    else s.hiddenItems.insert(id);
  });
}

void ModelStore::renameBody(int body, const std::string& name) {
  std::string t = trimmed(name);
  updateSettings([&](ProjectSettings& s) {
    if (t.empty()) s.bodyNames.erase(body);
    else s.bodyNames[body] = t;
  });
}

void ModelStore::renameItem(const Uuid& id, const std::string& name) {
  std::string t = trimmed(name);
  if (t.empty()) return;
  auto list = features_;
  for (auto& f : list) {
    if (f.id != id) continue;
    if (auto o = f.op.as<op::SketchItem>()) o->name = t;
    else if (auto o = f.op.as<op::Plane>()) o->name = t;
  }
  commit(std::move(list));
}

void ModelStore::setAppearance(const Appearance& appearance, const std::vector<int>& bodies) {
  updateSettings([&](ProjectSettings& s) {
    for (int b : bodies) s.appearance[b] = appearance;
  });
}

std::vector<int> ModelStore::appearanceTargets() const {
  auto picked = touchedBodies();
  return picked.empty() ? bodyIds() : picked;
}

std::vector<std::pair<Appearance, std::vector<int>>> ModelStore::usedMaterials() const {
  std::vector<std::pair<Appearance, std::vector<int>>> groups;
  for (int id : bodyIds()) {
    auto it = settings_.appearance.find(id);
    Appearance a = it != settings_.appearance.end() ? it->second : Appearance();
    auto g = std::find_if(groups.begin(), groups.end(), [&](auto& x) { return x.first == a; });
    if (g != groups.end()) g->second.push_back(id);
    else groups.push_back({a, {id}});
  }
  return groups;
}

// MARK: Sketch items

void ModelStore::editItem(const Uuid& id) {
  auto it = std::find_if(features_.begin(), features_.end(), [&](auto& f) { return f.id == id; });
  if (it == features_.end()) return;
  if (it->op.is<op::SketchItem>()) editSketch(id);
  else edit(id);
}

void ModelStore::useSketchItem(const Uuid& id, bool revolve) {
  auto item = std::find_if(referenceItems_.begin(), referenceItems_.end(), [&](auto& i) { return i.id == id; });
  if (item == referenceItems_.end() || !item->sketch()) return;
  Sketch sk = *item->sketch();
  if (sk.profiles().empty()) return setToast(item->name + " has no closed profile");
  updateSettings([&](ProjectSettings& s) { s.hiddenItems.insert(id); });
  itemSelection_.clear();
  if (revolve) begin(op::Revolve{sk, 360, true, CombineMode::newBody, std::nullopt});
  else begin(op::Extrude{sk, 20, CombineMode::newBody, std::nullopt});
}

std::vector<Uuid> ModelStore::selectedSketchItems() const {
  std::vector<Uuid> out;
  for (auto& id : itemSelection_)
    if (std::any_of(referenceItems_.begin(), referenceItems_.end(), [&](auto& i) { return i.id == id && i.isSketch(); }))
      out.push_back(id);
  return out;
}

void ModelStore::sweepSelection() {
  auto sketches = selectedSketchItems();
  if (sketches.size() != 2) return setToast(std::string("Select two sketches in Items: the profile, then the path"));
  begin(op::Sweep{sketches[0], sketches[1], CombineMode::newBody, std::nullopt});
}

void ModelStore::loftSelection() {
  auto sketches = selectedSketchItems();
  if (sketches.size() < 2) return setToast(std::string("Select two or more sketches in Items, in loft order"));
  begin(op::Loft{sketches, CombineMode::newBody, std::nullopt});
}

// MARK: Construct

SketchPlane ModelStore::planeBase() const {
  auto faces = selectedFaces();
  if (!faces.empty())
    if (auto info = faceInfo(faces[0].first, faces[0].second); info && info->planar)
      return {fromO3d(info->centroid), fromO3d(info->normal), fromO3d(info->xDir)};
  auto items = selectedItems();
  if (!items.empty()) return items.front().plane();
  return SketchPlane::xy();
}

void ModelStore::addOffsetPlane() {
  begin(op::Plane{planeBase(), 10, 0, "Plane " + std::to_string(count(isPlaneOp) + 1)});
}

void ModelStore::addAngledPlane() {
  begin(op::Plane{planeBase(), 0, 45, "Plane " + std::to_string(count(isPlaneOp) + 1)});
}

void ModelStore::addMidplane() {
  std::vector<o3d::FaceInfo> faces;
  for (auto& f : selectedFaces())
    if (auto info = faceInfo(f.first, f.second); info && info->planar) faces.push_back(*info);
  if (faces.size() != 2) return setToast(std::string("Select two flat faces"));
  SketchPlane base{(fromO3d(faces[0].centroid) + fromO3d(faces[1].centroid)) / 2.0, fromO3d(faces[0].normal),
                   fromO3d(faces[0].xDir)};
  begin(op::Plane{base, 0, 0, "Plane " + std::to_string(count(isPlaneOp) + 1)});
}

void ModelStore::startSketchOnSelectedPlane() {
  for (auto& item : selectedItems())
    if (item.isPlane()) return startSketch(item.plane());
  setToast(std::string("Select a plane in Items"));
}

// MARK: Transform

void ModelStore::scaleSelection() {
  auto b = selectedBody();
  if (!b) return;
  begin(op::ScaleAxes{*b, {2, 2, 2}, centerOf(*b), true});
}

void ModelStore::scatterSelection() {
  auto bodies = uniqueBodies(selection_);
  auto m = bodies.size() == 2 ? meshes_.find(bodies[0]) : meshes_.end();
  if (m == meshes_.end()) return setToast(std::string("Select the body to scatter, then the body to scatter it on"));
  const BodyMesh& mesh = *m->second;
  double footprint = double(std::max(mesh.boundsMax.x - mesh.boundsMin.x, mesh.boundsMax.y - mesh.boundsMin.y));
  ScatterSettings p;
  p.spacing = std::round(footprint * 0.9);
  begin(op::Scatter{bodies[0], bodies[1], {}, p});
}

void ModelStore::linearPatternSelection() {
  auto b = selectedBody();
  if (!b || !meshes_.count(*b)) return;
  const BodyMesh& m = *meshes_.at(*b);
  double size = double(m.boundsMax.x - m.boundsMin.x);
  begin(op::LinearPattern{*b, {1, 0, 0}, 3, std::ceil(size * 1.5), false});
}

void ModelStore::circularPatternSelection() {
  auto b = selectedBody();
  if (!b) return;
  begin(op::CircularPattern{*b, Vec3d(), {0, 0, 1}, 6, 360, false});
}

void ModelStore::mirrorSelection() {
  auto b = selectedBody();
  if (!b || !meshes_.count(*b)) return;
  begin(op::Mirror{*b, axisPlane(0, toD(meshes_.at(*b)->boundsMax)), true});
}

// MARK: Tools

void ModelStore::splitSelection() {
  auto b = selectedBody();
  if (!b) return;
  begin(op::Split{*b, axisPlane(2, centerOf(*b)), 0});
}

static std::vector<int> facesOf(const std::vector<std::pair<int, int>>& faces, int body) {
  std::vector<int> out;
  for (auto& f : faces)
    if (f.first == body) out.push_back(f.second);
  return out;
}

void ModelStore::deleteFacesSelection() {
  auto faces = selectedFaces();
  if (faces.empty()) return setToast(std::string("Select the faces to delete"));
  begin(op::DeleteFaces{faces[0].first, facesOf(faces, faces[0].first)});
}

void ModelStore::draftSelection() {
  auto faces = selectedFaces();
  if (faces.empty()) return setToast(std::string("Select the side faces to draft"));
  begin(op::Draft{faces[0].first, facesOf(faces, faces[0].first), {0, 0, 1}, 5});
}

void ModelStore::offsetFacesSelection() {
  auto faces = selectedFaces();
  if (faces.empty()) return setToast(std::string("Select the faces to offset"));
  begin(op::OffsetFaces{faces[0].first, facesOf(faces, faces[0].first), 2});
}

// MARK: Measure

std::optional<o3d::Measurement> ModelStore::measureSelection() const {
  auto ref = [](const Pick& p) {
    switch (p.kind) {
      case Pick::Kind::Body: return o3d::Ref{p.body, o3d::RefKind::Body, -1};
      case Pick::Kind::Face: return o3d::Ref{p.body, o3d::RefKind::Face, p.index};
      case Pick::Kind::Edge: return o3d::Ref{p.body, o3d::RefKind::Edge, p.index};
    }
    return o3d::Ref{};
  };
  if (selection_.empty()) return std::nullopt;
  o3d::Ref a = ref(selection_[0]);
  o3d::Ref b = selection_.size() > 1 ? ref(selection_[1]) : o3d::Ref{-1, o3d::RefKind::Body, -1};
  o3d::Measurement m;
  if (!kernel_->measure(a, b, m)) return std::nullopt;
  return m;
}

std::vector<MeasureRow> ModelStore::measureRows() {
  std::vector<MeasureRow> rows;
  auto m = measureSelection();
  if (!m) return rows;
  LengthUnit u = settings_.units;
  double s = scale(u);
  std::string sym = symbol(u);
  if (m->hasDistance && selection_.size() == 2) rows.push_back({"Distance", m->distance / s, sym});
  if (m->hasAngle) rows.push_back({"Angle", m->angleDeg, "°"});
  if (m->hasLength) rows.push_back({"Length", m->length / s, sym});
  if (m->hasRadius) rows.push_back({"Radius", m->radius / s, sym});
  if (m->hasArea) rows.push_back({"Area", m->area / (s * s), sym + "²"});
  if (selection_.size() == 1 && selection_[0].kind == Pick::Kind::Body)
    if (auto p = properties(selection_[0].body)) rows.push_back({"Volume", p->volume / (s * s * s), sym + "³"});
  return rows;
}

std::optional<std::pair<Vec3d, Vec3d>> ModelStore::selectedAxis() const {
  for (auto& item : selectedItems())
    if (auto a = item.axis()) return std::make_pair(a->origin, a->direction);
  return std::nullopt;
}

void ModelStore::addAxis() {
  Vec3d origin, direction(0, 0, 1);
  auto faces = selectedFaces();
  auto edges = selectedEdges();
  std::optional<o3d::FaceInfo> fi;
  if (!faces.empty()) fi = faceInfo(faces[0].first, faces[0].second);
  if (fi && fi->radius > 0) {
    origin = fromO3d(fi->axisOrigin);
    direction = fromO3d(fi->axisDir);
  } else if (!edges.empty()) {
    if (auto info = edgeInfo(edges[0].first, edges[0].second)) {
      if (info->linear) {
        origin = fromO3d(info->start);
        direction = fromO3d(info->end) - fromO3d(info->start);
      } else if (info->circular) {
        origin = fromO3d(info->center);
        direction = fromO3d(info->axis);
      }
    }
  }
  commitImmediately(op::Axis{origin, direction, "Axis " + std::to_string(count(isAxisOp) + 1)});
}

void ModelStore::translateSelection() {
  auto b = selectedBody();
  if (!b) return;
  begin(op::Translate{*b, {1, 0, 0}, 10});
}

void ModelStore::rotateSelection() {
  auto b = selectedBody();
  if (!b) return;
  Vec3d origin = meshes_.count(*b) ? centerOf(*b) : Vec3d(), dir(0, 0, 1);
  if (auto axis = selectedAxis()) {
    origin = axis->first;
    dir = axis->second;
  }
  begin(op::Rotate{*b, origin, dir, 90});
}

void ModelStore::alignSelection() {
  auto bodies = uniqueBodies(selection_);
  if (bodies.size() != 2) return setToast(std::string("Select the body to move, then the one to align to"));
  begin(op::Align{bodies[0], bodies[1], {2, 2, 1}});
}

void ModelStore::replaceFaceSelection() {
  auto faces = selectedFaces();
  if (faces.size() != 2) return setToast(std::string("Select the face to move, then the target face"));
  commitImmediately(op::ReplaceFace{faces[0].first, faces[0].second, faces[1].first, faces[1].second});
}

void ModelStore::fillSelection() {
  auto edges = selectedEdges();
  if (edges.empty()) return setToast(std::string("Select a closed loop of edges"));
  commitImmediately(op::Fill{edges[0].first, facesOf(edges, edges[0].first)});
}

void ModelStore::projectSelection() {
  auto b = selectedBody();
  if (!b) return setToast(std::string("Select a body to project"));
  SketchPlane plane = SketchPlane::xy();
  bool found = false;
  for (auto& item : selectedItems())
    if (item.isPlane()) {
      plane = item.plane();
      found = true;
      break;
    }
  if (!found)
    for (auto& f : selectedFaces())
      if (f.first != *b) {
        if (auto info = faceInfo(f.first, f.second); info && info->planar)
          plane = {fromO3d(info->centroid), fromO3d(info->normal), fromO3d(info->xDir)};
        break;
      }
  commitImmediately(op::Projection{*b, plane, "Projection " + std::to_string(count(isProjectionOp) + 1)});
}

void ModelStore::projectIntoSketch() {
  if (!sketch_) return;
  SketchSession s = *sketch_;
  std::vector<int> bodies;
  if (selection_.empty()) {
    for (auto& [id, _] : visibleMeshes()) bodies.push_back(id);
  } else {
    bodies = touchedBodies();
  }
  for (int b : bodies) {
    for (auto& line : kernel_->projectEdges(b, s.plane().bridged())) {
      std::vector<int> ids;
      for (auto& p : line) ids.push_back(s.sketch.addPoint(fromArr(p)));
      for (size_t i = 0; i + 1 < ids.size(); ++i) s.sketch.addLine(ids[i], ids[i + 1]);
    }
  }
  s.resolve();
  setSketch(std::move(s));
}

// MARK: Insert

void ModelStore::insertImage(const std::vector<uint8_t>& imageBytes, const std::string& name) {
  auto image = decodeImage(imageBytes);
  std::vector<uint8_t> png;
  if (image) png = encodePNG(*image);
  if (!image || png.empty()) return setToast(std::string("Couldn't read that image"));
  SketchPlane plane = SketchPlane::xy();
  for (auto& item : selectedItems())
    if (item.isPlane()) {
      plane = item.plane();
      break;
    }
  auto faces = selectedFaces();
  if (!faces.empty())
    if (auto info = faceInfo(faces[0].first, faces[0].second); info && info->planar)
      plane = {fromO3d(info->centroid) + fromO3d(info->normal) * 0.01, fromO3d(info->normal), fromO3d(info->xDir)};
  double width = 100.0;
  ReferenceImage ref{name, png, plane, width, width * double(image->height) / double(std::max(image->width, 1))};
  selection_.clear();
  begin(op::Image{ref});
}

void ModelStore::insertProject(const std::string& documentBytes, const std::string& name) {
  Sculpt3DFile file;
  try {
    file = Sculpt3DFile::decode(documentBytes);
  } catch (...) {
    return setToast(std::string("Couldn't open that project"));
  }
  ModelStore other(file.features, file.settings);
  auto list = features_;
  size_t before = list.size();
  for (int id : other.bodyIds()) {
    std::string brep = other.kernel().toBrep(id);
    if (!brep.empty()) list.push_back(Feature(op::Imported{name, brep}));
  }
  if (list.size() == before) return setToast(name + " has no bodies");
  commit(std::move(list));
}

// MARK: Variables

void ModelStore::addVariable() {
  int n = int(settings_.variables.size()) + 1;
  auto taken = [&](int k) {
    return std::any_of(settings_.variables.begin(), settings_.variables.end(),
                       [&](auto& v) { return lower(v.name) == "v" + std::to_string(k); });
  };
  while (taken(n)) ++n;
  updateSettings([&](ProjectSettings& s) {
    Variable v;
    v.name = "v" + std::to_string(n);
    v.expression = "10";
    s.variables.push_back(v);
  });
}

std::map<std::string, double> ModelStore::variableValues() const {
  return ExpressionEvaluator(settings_.variables).variables;
}

void ModelStore::bindDraft(int parameter, std::optional<std::string> expression) {
  if (!draft_) return;
  Draft d = *draft_;
  std::map<int, std::string> bindings = d.feature.bindings.value_or(std::map<int, std::string>());
  if (expression) bindings[parameter] = *expression;
  else bindings.erase(parameter);
  if (bindings.empty()) d.feature.bindings.reset();
  else d.feature.bindings = bindings;
  if (expression)
    if (auto v = ExpressionEvaluator(settings_.variables).evaluate(*expression))
      d.feature.op = d.feature.op.setting(parameter, *v);
  draft_ = std::move(d);
  regenerate();
}

// MARK: Terrain.swift

void ModelStore::insertRollingHills() {
  static std::mt19937 rng{std::random_device{}()};
  TerrainSettings t;
  t.name = "Rolling Hills";
  TerrainNoise n;
  n.seed = std::uniform_int_distribution<int>(1, 9999)(rng);
  t.noise = n;
  t.regenerate();
  selection_.clear();
  begin(op::Terrain{t});
}

void ModelStore::insertTerrain(const std::vector<uint8_t>& imageBytes, const std::string& name) {
  auto image = decodeImage(imageBytes);
  if (!image) return setToast(std::string("Couldn't read that image"));
  TerrainSettings t;
  t.name = name;
  double aspect = double(image->height) / double(std::max(image->width, 1));
  t.depth = std::round(t.width * aspect);
  t.nx = 64;
  t.ny = std::max(8, std::min(128, int(std::round(64 * aspect))));
  auto field = Terrain::field(image->gray(), image->width, image->height, t.nx, t.ny);
  t.heights.clear();
  for (double v : field) t.heights.push_back(v * 40);
  selection_.clear();
  begin(op::Terrain{t});
}

}  // namespace sculpt
