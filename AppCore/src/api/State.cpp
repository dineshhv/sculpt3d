// sculpt_doc_state: one JSON snapshot of everything the SwiftUI views read.
// The schema is documented in AppCore/API.md ("State").
#include <cctype>
#include <algorithm>
#include <cmath>
#include <set>

#include "api/Actions.hpp"
#include "api/Document.hpp"
#include "sculpt/model/DisplayFormat.hpp"
#include "sculpt/model/ToolCatalog.hpp"

namespace sculpt::api {

using json = nlohmann::json;

json vec(const Vec3d& v) { return json::array({v.x, v.y, v.z}); }
json vec(const Vec3f& v) { return json::array({v.x, v.y, v.z}); }
json vec(const Vec2d& v) { return json::array({v.x, v.y}); }

json pickJson(const Pick& p) {
  const char* kind = p.kind == Pick::Kind::Body ? "body" : (p.kind == Pick::Kind::Face ? "face" : "edge");
  json j = {{"kind", kind}, {"body", p.body}};
  if (p.kind != Pick::Kind::Body) j["index"] = p.index;
  return j;
}

std::string refKind(SketchRef::Kind k) {
  switch (k) {
    case SketchRef::Kind::Point: return "point";
    case SketchRef::Kind::Line: return "line";
    case SketchRef::Kind::Circle: return "circle";
    case SketchRef::Kind::Constraint: return "constraint";
    case SketchRef::Kind::Spline: return "spline";
    case SketchRef::Kind::Ellipse: return "ellipse";
  }
  return "point";
}

json refJson(const SketchRef& r) { return {{"kind", refKind(r.kind)}, {"index", r.index}}; }

json appearanceJson(const Appearance& a) {
  auto label = MaterialPreset::label(a);
  json j = {{"color", vec(a.color)},
            {"metallic", a.metallic},
            {"roughness", a.roughness},
            {"opacity", a.opacity},
            {"preset", a.preset ? json(*a.preset) : json(nullptr)},
            {"finish", label.first},
            {"name", label.second}};
  return j;
}

std::string pointPickPurposeRaw(PointPickPurpose p) {
  switch (p) {
    case PointPickPurpose::planeThrough3Points: return "planeThrough3Points";
    case PointPickPurpose::planeOnCurveAtPoint: return "planeOnCurveAtPoint";
    case PointPickPurpose::planePerpendicularToEdgeAtPoint: return "planePerpendicularToEdgeAtPoint";
    case PointPickPurpose::planeParallelToFaceAtPoint: return "planeParallelToFaceAtPoint";
    case PointPickPurpose::axisThrough2Points: return "axisThrough2Points";
    case PointPickPurpose::axisPerpendicularToFaceAtPoint: return "axisPerpendicularToFaceAtPoint";
    case PointPickPurpose::measurePointToPoint: return "measurePointToPoint";
    case PointPickPurpose::measureThreePointAngle: return "measureThreePointAngle";
  }
  return "";
}

static std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

/// ParameterField's ± step, in display units.
double parameterStep(const std::string& unit, double shown) {
  if (unit == "°") return 15;
  if (unit == "×") return 0.5;
  if (unit.empty()) return 1;
  return std::abs(shown) >= 20 ? 5 : 1;
}

json parameterJson(const Operation::Parameter& p, LengthUnit u, const std::optional<std::map<int, std::string>>& bindings) {
  bool isLength = p.unit == "mm";
  double shown = isLength ? p.value / scale(u) : p.value;
  json j = {{"id", p.id},
            {"label", p.label},
            {"value", p.value},
            {"unit", p.unit},
            {"isLength", isLength},
            {"displayValue", shown},
            {"displayUnit", isLength ? symbol(u) : p.unit},
            {"text", formatted(shown)},
            {"step", parameterStep(p.unit, shown)}};
  if (bindings) {
    auto it = bindings->find(p.id);
    j["binding"] = it != bindings->end() ? json(it->second) : json(nullptr);
  } else {
    j["binding"] = nullptr;
  }
  return j;
}

static std::string planeName(const SketchPlane& p) {
  Vec3d zero;
  if (p.origin == zero) {
    if (p.normal == SketchPlane::xy().normal) return "Top plane";
    if (p.normal == SketchPlane::xz().normal) return "Front plane";
    if (p.normal == SketchPlane::yz().normal) return "Right plane";
  }
  return "Face plane";
}

static std::optional<std::string> toolSubtitle(SketchTool t, const SketchSession& s) {
  switch (t) {
    case SketchTool::rectangle: return std::string("Diagonal");
    case SketchTool::centerRectangle: return std::string("Center");
    case SketchTool::spline: return std::string("Fit");
    case SketchTool::polygon: return std::to_string(s.polygonSides) + " sides";
    case SketchTool::arc: return std::string("3-point");
    default: return std::nullopt;
  }
}

static json sketchJson(SculptDoc& doc) {
  ModelStore& store = *doc.store;
  ViewportController& view = *doc.view;
  const SketchSession& s = *store.sketch();
  LengthUnit u = store.settings().units;
  json j;
  j["tool"] = rawValue(s.tool);
  json tools = json::array();
  for (auto t : allSketchTools()) {
    char k = key(t);
    auto sub = toolSubtitle(t, s);
    tools.push_back({{"id", rawValue(t)},
                     {"title", title(t)},
                     {"symbol", symbol(t)},
                     {"key", k ? json(std::string(1, char(std::toupper((unsigned char)k)))) : json(nullptr)},
                     {"subtitle", sub ? json(*sub) : json(nullptr)},
                     {"drags", drags(t)},
                     {"active", s.tool == t}});
  }
  j["tools"] = tools;
  const SketchPlane& plane = s.plane();
  j["plane"] = {{"name", planeName(plane)}, {"origin", vec(plane.origin)}, {"normal", vec(plane.normal)}, {"xDir", vec(plane.xDir)}};
  j["replacing"] = s.replacing ? json(s.replacing->string()) : json(nullptr);
  j["target"] = s.target ? json(*s.target) : json(nullptr);
  j["constructionMode"] = s.constructionMode;
  j["polygonSides"] = s.polygonSides;
  j["polygonCircumscribed"] = s.polygonCircumscribed;
  j["offsetDistance"] = s.offsetDistance;
  j["offsetDistanceText"] = formatted(s.offsetDistance / scale(u));
  j["filletRadius"] = s.filletRadius;
  j["filletRadiusText"] = formatted(s.filletRadius / scale(u));
  json sel = json::array();
  for (auto& r : s.selection) sel.push_back(refJson(r));
  j["selection"] = sel;
  j["hover"] = s.hover ? refJson(*s.hover) : json(nullptr);
  auto points = s.selectedPoints(), lines = s.selectedLines(), circles = s.selectedCircles();
  auto splines = s.selectedSplines(), ellipses = s.selectedEllipses(), constraints = s.selectedConstraints();
  j["selectedCounts"] = {{"points", points.size()},       {"lines", lines.size()},
                         {"circles", circles.size()},     {"splines", splines.size()},
                         {"ellipses", ellipses.size()},   {"constraints", constraints.size()}};
  bool empty = s.sketch.isEmpty();
  int profiles = s.sketch.profileCount();
  j["isEmpty"] = empty;
  j["profiles"] = profiles;
  j["status"] = {{"degreesOfFreedom", s.status.degreesOfFreedom}, {"fullyConstrained", s.status.fullyConstrained()}};
  // SketchBar's status column.
  std::string statusText;
  if (empty) statusText = "Grid " + formatted(view.gridStep()) + " mm";
  else if (s.status.fullyConstrained()) statusText = "Fully constrained";
  else statusText = plural(s.status.degreesOfFreedom, "degree of freedom", "degrees of freedom");
  j["statusText"] = statusText;
  j["profilesText"] = plural(profiles, "profile", "profiles");
  j["escapeTitle"] = s.escapeTitle();
  j["canUndo"] = store.canUndoSketch();
  j["canRedo"] = store.canRedoSketch();
  j["canTransform"] = store.canTransformSketch();
  auto dim = s.selectedDimension();
  j["finishOnReturn"] = !dim.has_value();
  j["showConstraints"] = view.showConstraints;
  j["chainActive"] = s.chainEnd.has_value();
  j["pendingArcPoints"] = s.pending.size();
  j["splineDraftPoints"] = s.splineDraft.size();
  j["snapHint"] = s.snapHint ? json(*s.snapHint) : json(nullptr);

  // ConstraintBar: the selected dimension's value, or the constraints that fit.
  if (dim) {
    const auto& c = s.sketch.constraints[size_t(*dim)];
    bool isLength = c.unit() == "mm";
    double shown = isLength ? c.value / scale(u) : c.value;
    j["selectedDimension"] = {{"index", *dim},
                              {"kind", rawValue(c.kind)},
                              {"label", title(c.kind)},
                              {"value", c.value},
                              {"unit", c.unit()},
                              {"displayValue", shown},
                              {"displayUnit", isLength ? symbol(u) : c.unit()},
                              {"text", formatted(shown)},
                              {"step", parameterStep(c.unit(), shown)}};
  } else {
    j["selectedDimension"] = nullptr;
  }
  auto kinds = s.availableConstraints();
  json available = json::array();
  for (auto k : kinds) {
    std::string t = k == SketchConstraint::Kind::Coincident && circles.size() == 2 ? "Concentric" : title(k);
    available.push_back({{"kind", rawValue(k)}, {"title", t}, {"symbol", symbol(k)}});
  }
  j["availableConstraints"] = available;
  bool curves = !lines.empty() || !circles.empty() || !splines.empty() || !ellipses.empty();
  j["constraintBar"] = {
      {"visible", !s.selection.empty()},
      {"noneFits", kinds.empty() && constraints.empty() && splines.empty() && ellipses.empty()},
      {"showOffset", curves},
      {"showFillet", points.size() == 1 || (lines.size() == 2 && points.empty())},
      {"showDisconnect", points.size() == 1 && s.selection.size() == 1},
      {"showMakeConstruction", curves}};
  auto has = [&](SketchConstraint::Kind k) { return std::find(kinds.begin(), kinds.end(), k) != kinds.end(); };
  using K = SketchConstraint::Kind;
  auto item = [](const char* id, const std::string& t, const char* sym, const char* keys, bool enabled) {
    return json{{"id", id}, {"title", t}, {"symbol", sym}, {"keys", keys ? json(keys) : json(nullptr)}, {"enabled", enabled}};
  };
  // ConstraintColumn, top to bottom; run with {"cmd": "sketch.constraintColumn", "id": …}.
  j["constraintColumn"] = json::array(
      {item("settings", view.showConstraints ? "Hide Constraints" : "Show Constraints", "gearshape", nullptr, true),
       item("parallel", "Parallel", "equal", "⇧A", has(K::Parallel)),
       item("perpendicular", "Perpendicular", "angle", "⇧P", has(K::Perpendicular)),
       item("tangent", "Tangent", "circle.and.line.horizontal", "⇧T", has(K::Tangent)),
       item("coincident", "Coincident", "smallcircle.filled.circle", "⇧N",
            has(K::Coincident) || has(K::PointOnLine) || has(K::PointOnCircle)),
       item("midpoint", "Midpoint", "line.diagonal", "⇧M", has(K::Midpoint)),
       item("concentric", "Concentric", "circle.circle", "⇧C", circles.size() == 2 && points.empty() && lines.empty()),
       item("hv", "Horizontal/Vertical", "arrow.left.and.right", "⇧V", !lines.empty() && points.empty() && circles.empty()),
       item("equal", "Equal", "equal.circle", "⇧E", has(K::EqualLength) || has(K::EqualRadius)),
       item("symmetry", "Symmetry", "arrow.left.and.line.vertical.and.arrow.right", "⇧S", has(K::Symmetric)),
       item("disconnect", "Disconnect", "link.badge.plus", nullptr, points.size() == 1),
       item("lock", "Lock", "lock", "⇧L", has(K::Fixed)),
       item("construction", "Make Construction", "square.dashed", nullptr, !s.selection.empty())});
  json list = json::array();
  for (size_t i = 0; i < s.sketch.constraints.size(); ++i) {
    const auto& c = s.sketch.constraints[i];
    bool selected = std::find(s.selection.begin(), s.selection.end(), SketchRef::constraint(int(i))) != s.selection.end();
    list.push_back({{"index", i},
                    {"kind", rawValue(c.kind)},
                    {"title", title(c.kind)},
                    {"symbol", symbol(c.kind)},
                    {"glyph", glyph(c.kind)},
                    {"isDimension", isDimension(c.kind)},
                    {"value", c.value},
                    {"unit", c.unit()},
                    {"label", isDimension(c.kind) ? s.label(c) : glyph(c.kind)},
                    {"selected", selected}});
  }
  j["constraints"] = list;
  j["counts"] = {{"points", s.sketch.points.size()},   {"lines", s.sketch.lines.size()},
                 {"circles", s.sketch.circles.size()}, {"splines", s.sketch.splines.size()},
                 {"ellipses", s.sketch.ellipses.size()}, {"constraints", s.sketch.constraints.size()}};
  return j;
}

static json draftJson(SculptDoc& doc) {
  ModelStore& store = *doc.store;
  const Draft& d = *store.draft();
  const Operation& op = d.feature.op;
  LengthUnit u = store.settings().units;
  json j;
  std::string t = d.feature.title();
  j["title"] = d.replacing ? "Edit " + t : t;
  j["featureTitle"] = t;
  j["symbol"] = d.feature.symbol();
  j["case"] = op.caseName();
  j["replacing"] = d.replacing ? json(d.replacing->string()) : json(nullptr);
  json params = json::array();
  for (auto& p : op.parameters()) params.push_back(parameterJson(p, u, d.feature.bindings));
  j["parameters"] = params;
  json choices = json::array();
  for (auto& c : op.choices()) choices.push_back({{"id", c.id}, {"label", c.label}, {"options", c.options}, {"selected", c.selected}});
  j["choices"] = choices;
  auto mode = op.combineMode();
  if (mode && op.sketchTarget()) {
    json options = json::array();
    for (auto m : allCombineModes()) options.push_back({{"id", rawValue(m)}, {"title", title(m)}});
    j["combineMode"] = {{"selected", rawValue(*mode)}, {"options", options}};
  } else {
    j["combineMode"] = nullptr;
  }
  if (auto r = op.as<op::Revolve>()) {
    j["revolveAxis"] = {{"aroundV", r->aroundV}, {"options", json::array({{{"aroundV", true}, {"title", "Vertical"}},
                                                                            {{"aroundV", false}, {"title", "Horizontal"}}})}};
  } else {
    j["revolveAxis"] = nullptr;
  }
  j["canEditSketch"] = d.replacing.has_value() && op.sketch() != nullptr;
  auto error = store.draftError();
  j["error"] = error ? json(*error) : json(nullptr);
  j["canApply"] = !error.has_value();
  return j;
}

static json propertiesJson(ModelStore& store, int body) {
  auto p = store.properties(body);
  if (!p) return nullptr;
  LengthUnit u = store.settings().units;
  Vec3d size = fromO3d(p->bboxMax) - fromO3d(p->bboxMin);
  double s = scale(u);
  return {{"size", vec(size)},
          {"volume", p->volume},
          {"area", p->area},
          {"faceCount", p->faceCount},
          {"edgeCount", p->edgeCount},
          {"bboxMin", vec(fromO3d(p->bboxMin))},
          {"bboxMax", vec(fromO3d(p->bboxMax))},
          // PropertiesPanel rows.
          {"sizeText", formatLength(size.x, u) + " × " + formatLength(size.y, u) + " × " + formatLength(size.z, u)},
          {"volumeText", formatVolume(p->volume, u)},
          {"areaText", formatArea(p->area, u)},
          {"facesEdgesText", std::to_string(p->faceCount) + " / " + std::to_string(p->edgeCount)},
          // ItemsPanel's caption under a selected body.
          {"captionLines",
           json::array({formatted(size.x / s) + " × " + formatted(size.y / s) + " × " + formatted(size.z / s) + " " + symbol(u),
                        "Volume " + formatted(p->volume / std::pow(s, 3)) + " " + symbol(u) + "³",
                        "Area " + formatted(p->area / std::pow(s, 2)) + " " + symbol(u) + "² · " +
                            std::to_string(p->faceCount) + " faces · " + std::to_string(p->edgeCount) + " edges"})}};
}

json bodyProperties(ModelStore& store, int body) { return propertiesJson(store, body); }

/// MeasureSidePanel.currentRows.
static json measureRows(SculptDoc& doc) {
  ModelStore& store = *doc.store;
  ViewportController& view = *doc.view;
  json rows = json::array();
  if (store.selection().empty()) return rows;
  const auto& m = view.measurement();
  if (!m) return rows;
  LengthUnit u = store.settings().units;
  auto len = [&](double v) { return formatLength(v, u); };
  auto row = [&](const std::string& label, const std::string& text) { rows.push_back({{"label", label}, {"text", text}}); };
  if (m->hasDistance && store.selection().size() == 2) {
    row("Distance", len(m->distance));
    if (view.measureDeltas) {
      Vec3d d = fromO3d(m->p2) - fromO3d(m->p1);
      row("ΔX", len(std::abs(d.x)));
      row("ΔY", len(std::abs(d.y)));
      row("ΔZ", len(std::abs(d.z)));
    }
  }
  if (m->hasAngle) row("Angle", formatAngle(m->angleDeg));
  if (m->hasLength) row("Length", len(m->length));
  if (m->hasRadius) {
    row("Radius", len(m->radius));
    row("Diameter", len(m->radius * 2));
  }
  if (m->hasArea) row("Area", formatArea(m->area, u));
  if (store.selection().size() == 1 && store.selection()[0].kind == Pick::Kind::Body) {
    if (auto p = store.properties(store.selection()[0].body)) {
      row("Volume", formatVolume(p->volume, u));
      if (view.measureDeltas) {
        Vec3d size = fromO3d(p->bboxMax) - fromO3d(p->bboxMin);
        row("Size X", len(size.x));
        row("Size Y", len(size.y));
        row("Size Z", len(size.z));
      }
    }
  }
  return rows;
}

json documentState(SculptDoc& doc) {
  ModelStore& store = *doc.store;
  ViewportController& view = *doc.view;
  const ProjectSettings& settings = store.settings();
  LengthUnit u = settings.units;
  bool sketching = store.sketch().has_value();
  bool idle = !store.draft() && !sketching;
  json st;
  st["revision"] = doc.revision();
  st["viewRevision"] = view.viewRevision();
  st["document"] = {{"name", store.documentName()}, {"path", store.documentPath()}, {"dirty", doc.dirty}};
  st["mode"] = view.visualizing ? "visualize" : (sketching ? "sketching" : (view.measuring ? "measuring" : "modeling"));
  st["modeTitle"] = view.visualizing ? "Visualize" : (sketching ? "Sketching" : (view.measuring ? "Measuring" : "Modeling"));
  st["idle"] = idle;
  st["toast"] = store.toast() ? json(*store.toast()) : json(nullptr);
  st["uiRequest"] = {{"serial", view.uiRequestSerial}, {"action", view.uiRequest}};
  st["gl"] = {{"ready", doc.glReady}, {"error", doc.glError}};
  st["units"] = {{"id", rawValue(u)}, {"symbol", symbol(u)}, {"title", title(u)}, {"scale", scale(u)}, {"imperial", isImperial(u)}};

  // History panel.
  const auto& features = store.features();
  json fs = json::array();
  for (size_t i = 0; i < features.size(); ++i) {
    const Feature& f = features[i];
    auto err = store.errors().find(f.id);
    bool hasParams = !f.op.parameters().empty();
    bool hasSketch = f.op.sketch() != nullptr;
    fs.push_back({{"id", f.id.string()},
                  {"index", i},
                  {"number", i + 1},
                  {"title", f.title()},
                  {"symbol", f.symbol()},
                  {"case", f.op.caseName()},
                  {"suppressed", f.suppressed},
                  {"error", err != store.errors().end() ? json(err->second) : json(nullptr)},
                  {"hasParameters", hasParams},
                  {"hasSketch", hasSketch},
                  {"editable", hasParams || hasSketch},
                  {"editing", store.draft() && store.draft()->replacing == f.id},
                  {"isReference", f.isReference()}});
  }
  st["features"] = fs;
  st["history"] = {{"canUndo", action("edit.undo")->enabled(doc)},
                   {"canRedo", action("edit.redo")->enabled(doc)},
                   {"sketchUndo", sketching},
                   {"emptyTitle", "Workspace is empty"},
                   {"emptySubtitle", "Your modeling steps will show up here"}};

  // Selection.
  auto selBodies = store.selectedBodies();
  auto selFaces = store.selectedFaces();
  auto selEdges = store.selectedEdges();
  std::set<int> touched;
  for (int b : store.touchedBodies()) touched.insert(b);
  json picks = json::array();
  for (auto& p : store.selection()) picks.push_back(pickJson(p));
  std::vector<std::string> parts;
  int nb = int(selBodies.size()), nf = int(selFaces.size()), ne = int(selEdges.size());
  if (nb > 0) parts.push_back(plural(nb, "body", "bodies"));
  if (nf > 0) parts.push_back(plural(nf, "face", "faces"));
  if (ne > 0) parts.push_back(plural(ne, "edge", "edges"));
  std::string summary;
  for (size_t i = 0; i < parts.size(); ++i) summary += (i ? ", " : "") + parts[i];
  json items = json::array();
  for (auto& id : store.itemSelection()) items.push_back(id.string());
  auto selectedBody = store.selectedBody();
  st["selection"] = {{"picks", picks},
                     {"bodies", selBodies},
                     {"faceCount", nf},
                     {"edgeCount", ne},
                     {"bodyCount", nb},
                     {"summary", summary},
                     {"items", items},
                     {"touchedBodies", std::vector<int>(touched.begin(), touched.end())},
                     {"selectedBody", selectedBody ? json(*selectedBody) : json(nullptr)},
                     {"hover", store.hover() ? pickJson(*store.hover()) : json(nullptr)},
                     {"hintTouch", "Tap a face or edge · double-tap a body · drag to orbit, two fingers to pan and zoom"},
                     {"hintPointer",
                      "Click a face or edge · double-click a body · drag to orbit, ⌥-drag to pan, scroll to orbit, pinch to zoom"}};
  // SelectionBar buttons, in order.
  json bar = json::array();
  auto barItem = [&](const std::string& id, const std::string& t, const std::string& sym, bool destructive = false) {
    bar.push_back({{"id", id}, {"title", t}, {"symbol", sym}, {"destructive", destructive}});
  };
  if (!store.selection().empty()) {
    if (nf > 0) {
      if (nf == 1) {
        barItem("model.pushPull", "Push/Pull", "arrow.up.and.down.square");
        barItem("model.sketchOnFace", "Sketch", "pencil.and.outline");
      }
      barItem("model.shell", "Shell", "square.dashed");
    }
    if (ne > 0) {
      barItem("model.fillet", "Fillet", "circle.bottomhalf.filled");
      barItem("model.chamfer", "Chamfer", "triangle.bottomhalf.filled");
    }
    if (nb == 2)
      for (auto k : allBooleanKinds()) barItem("model." + rawValue(k), title(k), symbol(k));
    if (nb == 1) {
      barItem("model.move", "Move", "move.3d");
      barItem("model.shell", "Shell", "square.dashed");
      barItem("model.mirror", "Mirror", "arrow.left.and.right.righttriangle.left.righttriangle.right");
      barItem("model.duplicate", "Duplicate", "plus.square.on.square");
    }
    if (nb > 0) barItem("model.delete", "Delete body", "trash", true);
    barItem("selection.clear", "Clear selection", "xmark");
  }
  st["selectionBar"] = bar;

  // Items panel: bodies, then sketches and planes.
  json bodies = json::array();
  for (int id : store.bodyIds()) {
    Appearance a;
    auto it = settings.appearance.find(id);
    if (it != settings.appearance.end()) a = it->second;
    bool selected = std::find(store.selection().begin(), store.selection().end(), Pick::bodyPick(id)) != store.selection().end();
    json b = {{"id", id},
              {"name", settings.name(id)},
              {"hidden", settings.hiddenBodies.count(id) > 0},
              {"selected", selected},
              {"touched", touched.count(id) > 0},
              {"appearance", appearanceJson(a)}};
    // Properties are measured for the bodies the Items and Properties panels show them for.
    b["properties"] = touched.count(id) || selected ? propertiesJson(store, id) : json(nullptr);
    bodies.push_back(b);
  }
  st["bodies"] = bodies;
  json refs = json::array();
  const auto& itemSel = store.itemSelection();
  for (auto& item : store.referenceItems()) {
    auto pos = std::find(itemSel.begin(), itemSel.end(), item.id);
    bool selected = pos != itemSel.end();
    std::string kind = item.isSketch() ? "sketch" : item.isPlane() ? "plane" : item.isAxis() ? "axis" : "image";
    refs.push_back({{"id", item.id.string()},
                    {"name", item.name},
                    {"kind", kind},
                    {"symbol", item.isPlane() ? "square.on.square.dashed" : "pencil.and.outline"},
                    {"hidden", settings.hiddenItems.count(item.id) > 0},
                    {"selected", selected},
                    {"order", selected && itemSel.size() > 1 ? json(int(pos - itemSel.begin()) + 1) : json(nullptr)},
                    {"editTitle", item.isPlane() ? "Edit Plane" : "Edit Sketch"}});
  }
  st["items"] = refs;
  st["project"] = {{"bodyCount", store.bodyIds().size()},
                   {"hiddenBodyCount", settings.hiddenBodies.size()},
                   {"featureCount", features.size()},
                   {"summary", plural(int(store.bodyIds().size()), "body", "bodies") + " · " +
                                   plural(int(features.size()), "history step", "history steps")},
                   {"isolating", store.isIsolating()}};

  st["draft"] = store.draft() ? draftJson(doc) : json(nullptr);
  st["sketch"] = sketching ? sketchJson(doc) : json(nullptr);

  // Settings.
  json vars = json::array();
  auto values = store.variableValues();
  for (auto& v : settings.variables) {
    std::string key = v.name;
    for (auto& c : key) c = char(std::tolower((unsigned char)c));
    auto it = values.find(key);
    vars.push_back({{"id", v.id.string()},
                    {"name", v.name},
                    {"expression", v.expression},
                    {"value", it != values.end() ? json(it->second) : json(nullptr)},
                    {"valueText", it != values.end() ? formatted(it->second) : "—"}});
  }
  const SnapSettings& snap = settings.snap;
  const EnvironmentSettings& env = settings.environment;
  json unitOptions = json::array();
  for (auto unit : allLengthUnits())
    unitOptions.push_back({{"id", rawValue(unit)}, {"symbol", symbol(unit)}, {"title", title(unit)}});
  st["settings"] = {
      {"units", rawValue(u)},
      {"unitOptions", unitOptions},
      {"snapping", settings.snapping},
      {"snap",
       {{"grid", snap.grid}, {"guideLines", snap.guideLines}, {"guidePoints", snap.guidePoints}, {"points3D", snap.points3D},
        {"distantEdges", snap.distantEdges}, {"showGuidePoints", snap.showGuidePoints}, {"showHints", snap.showHints}}},
      {"environment",
       {{"background", vec(env.background)}, {"gradient", env.gradient}, {"showGrid", env.showGrid},
        {"lightIntensity", env.lightIntensity}, {"lightAzimuth", env.lightAzimuth}, {"groundShadow", env.groundShadow},
        {"lightElevation", env.lightElevation}, {"castShadows", env.castShadows}, {"shadowSoftness", env.shadowSoftness},
        {"ambientOcclusion", env.ambientOcclusion}, {"occlusionStrength", env.occlusionStrength},
        {"smoothShading", env.smoothShading}}},
      {"camera", {{"orthographic", settings.camera.orthographic}, {"fieldOfView", settings.camera.fieldOfView}}},
      {"variables", vars}};

  // View state (ViewportController's @Published vars).
  json modes = json::array();
  for (auto& m : displayModes())
    modes.push_back({{"id", m.raw}, {"title", m.title}, {"symbol", m.symbol}, {"key", std::string(1, m.key)}});
  auto range = view.sectionRange();
  json gridOptions = json::array();
  for (double g : {0.1, 0.5, 1.0, 5.0, 10.0, 50.0, 100.0})
    gridOptions.push_back({{"value", g * scale(u)}, {"title", formatted(g) + " " + symbol(u)}});
  const auto& dm = info(view.displayMode);
  st["view"] = {{"displayMode", dm.raw},
                {"displayModeTitle", dm.title},
                {"displayModeSymbol", dm.symbol},
                {"displayModes", modes},
                {"showEdges", view.showEdges},
                {"showHiddenEdges", view.showHiddenEdges},
                {"showSketches", view.showSketches},
                {"showDecals", view.showDecals},
                {"showPinnedMeasurements", view.showPinnedMeasurements},
                {"showWorldAxes", view.showWorldAxes},
                {"showConstraints", view.showConstraints},
                {"gridAtModelBottom", view.gridAtModelBottom},
                {"fixedGrid", view.fixedGrid ? json(*view.fixedGrid) : json(nullptr)},
                {"gridStep", view.gridStep()},
                {"gridLabel", view.gridLabel()},
                {"gridOptions", gridOptions},
                {"lockGridTitle", "Lock Grid Size (" + formatLength(view.fixedGrid ? *view.fixedGrid : view.gridStep(), u) + ")"},
                {"section",
                 {{"enabled", view.sectionEnabled},
                  {"axis", view.sectionAxis},
                  {"offset", view.sectionOffset},
                  {"offsetText", formatted(std::round(view.sectionOffset))},
                  {"flipped", view.sectionFlipped},
                  {"range", json::array({range.first, range.second})}}},
                {"measuring", view.measuring},
                {"measureDeltas", view.measureDeltas},
                {"visualizing", view.visualizing}};

  // Measure panels.
  json pointMs = json::array();
  for (auto& m : view.pointMeasurements) pointMs.push_back({{"id", m.id.string()}, {"label", m.label}});
  json rows = view.measuring || !store.selection().empty() ? measureRows(doc) : json::array();
  std::string message;
  if (store.selection().empty()) message = "Select any item to measure — or two to measure between them.";
  else if (rows.empty()) message = "Can't measure this selection";
  // MeasurePanel (compact) rows, from the controller's cached measurement
  // (ModelStore::measureRows would measure again on every state read).
  json compact = json::array();
  if (view.measuring && !store.selection().empty()) {
    if (const auto& m = view.measurement()) {
      double sc = scale(u);
      std::string sym = symbol(u);
      auto add = [&](const char* label, double v, const std::string& unit) {
        MeasureRow r{label, v, unit};
        compact.push_back({{"label", r.label}, {"value", r.value}, {"unit", r.unit}, {"text", r.text()}});
      };
      if (m->hasDistance && store.selection().size() == 2) add("Distance", m->distance / sc, sym);
      if (m->hasAngle) add("Angle", m->angleDeg, "°");
      if (m->hasLength) add("Length", m->length / sc, sym);
      if (m->hasRadius) add("Radius", m->radius / sc, sym);
      if (m->hasArea) add("Area", m->area / (sc * sc), sym + "²");
      if (store.selection().size() == 1 && store.selection()[0].kind == Pick::Kind::Body)
        if (auto p = store.properties(store.selection()[0].body)) add("Volume", p->volume / (sc * sc * sc), sym + "³");
    }
  }
  st["measure"] = {{"rows", rows},
                   {"compactRows", compact},
                   {"message", message},
                   {"kernelError", store.kernel().lastError()},
                   {"pinned", view.pinnedMeasurements},
                   {"recent", view.recentMeasurements},
                   {"pointMeasurements", pointMs}};
  if (view.pointPick) {
    const auto& p = *view.pointPick;
    st["pointPick"] = {{"purpose", pointPickPurposeRaw(p.purpose)},
                       {"title", p.title},
                       {"prompt", p.prompt()},
                       {"picked", p.points.size()},
                       {"needed", p.needed()},
                       {"text", p.prompt() + "  ·  " + std::to_string(p.points.size() + 1) + " of " + std::to_string(p.needed())}};
  } else {
    st["pointPick"] = nullptr;
  }

  // Tool catalog: rail groups with their flyout tools.
  json categories = json::array();
  auto rail = railCategories();
  for (auto c : allToolCategories()) {
    json tools = json::array();
    for (auto& t : ToolCatalog::commands(c)) {
      auto sc = ToolCatalog::shortcut(t.id);
      tools.push_back({{"id", t.id},
                       {"title", t.title},
                       {"symbol", t.symbol},
                       {"hint", t.hint},
                       {"enabled", t.isEnabled(store)},
                       {"shortcut", sc ? json(*sc) : json(nullptr)}});
    }
    bool onRail = std::find(rail.begin(), rail.end(), c) != rail.end();
    categories.push_back({{"id", rawValue(c)},
                          {"title", title(c)},
                          {"symbol", c == ToolCategory::visualize ? "sculpt3d.visualize" : symbol(c)},
                          {"onRail", onRail},
                          {"dot", c == ToolCategory::tools},
                          {"tools", tools}});
  }
  st["tools"] = categories;
  json acts = json::object();
  for (auto& a : actions()) acts[a.id] = a.enabled(doc);
  st["actions"] = acts;

  // Visualize panel.
  auto targets = store.appearanceTargets();
  Appearance current;
  if (!targets.empty()) {
    auto it = settings.appearance.find(targets.front());
    if (it != settings.appearance.end()) current = it->second;
  }
  int finishIndex = current.metallic > 0.5f ? 2 : (current.roughness > 0.7f ? 0 : 1);
  json used = json::array();
  for (auto& [a, ids] : store.usedMaterials()) used.push_back({{"appearance", appearanceJson(a)}, {"bodies", ids}, {"count", ids.size()}});
  json palette = json::array(), backgrounds = json::array();
  for (auto& c : Appearance::palette()) palette.push_back(vec(c));
  for (auto& c : EnvironmentSettings::backgrounds()) backgrounds.push_back(vec(c));
  std::string targetText = store.selection().empty()
                               ? "Applies to all bodies"
                               : "Applies to " + std::to_string(targets.size()) + " selected bod" +
                                     (targets.size() == 1 ? "y" : "ies");
  st["visualize"] = {{"targets", targets},
                     {"targetText", targetText},
                     {"appearancePanelText", store.selection().empty() ? "All bodies"
                                                                        : std::to_string(targets.size()) + " selected bod" +
                                                                              (targets.size() == 1 ? "y" : "ies")},
                     {"current", appearanceJson(current)},
                     {"finishIndex", finishIndex},
                     {"finishOptions", json::array({"Matte", "Plastic", "Metal"})},
                     {"usedMaterials", used},
                     {"palette", palette},
                     {"backgrounds", backgrounds},
                     {"families", MaterialPreset::families()}};

  // Export to menu and sheet.
  json kinds = json::array();
  for (auto k : exportMenu())
    kinds.push_back({{"id", rawValue(k)},
                     {"title", title(k)},
                     {"detail", detail(k)},
                     {"extension", fileExtension(k)},
                     {"mimeType", mimeType(k)},
                     {"available", isAvailable(k)},
                     {"enabled", isAvailable(k) && store.canExport(k)},
                     {"forSlicer", forSlicer(k)},
                     {"fromDrawing", fromDrawing(k)}});
  size_t exportCount = store.exportBodies().size();
  st["export"] = {{"kinds", kinds},
                  {"scope", touched.empty() ? "All " + std::to_string(exportCount) + " visible bodies"
                                            : std::to_string(exportCount) + " selected " + (exportCount == 1 ? "body" : "bodies")},
                  {"bodyCount", exportCount}};
  json footInch = json::array(), degrees = json::array();
  for (auto f : {FootInchFormat::architectural, FootInchFormat::decimal})
    footInch.push_back({{"id", rawValue(f)}, {"title", title(f)}});
  for (auto f : {DegreeFormat::decimal, DegreeFormat::degreesMinutesSeconds})
    degrees.push_back({{"id", rawValue(f)}, {"title", title(f)}});
  st["displayFormat"] = {{"footInch", rawValue(DisplayFormat::footInch())},
                         {"footInchOptions", footInch},
                         {"footInchEnabled", isImperial(u)},
                         {"degrees", rawValue(DisplayFormat::degrees())},
                         {"degreeOptions", degrees}};
  return st;
}

}  // namespace sculpt::api
