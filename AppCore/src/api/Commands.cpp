// sculpt_doc_command: every user action of the Swift UI, by name.
// The catalog is documented in AppCore/API.md ("Commands").
#include <cctype>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>

#include "api/Actions.hpp"
#include "api/Document.hpp"
#include "sculpt/model/DisplayFormat.hpp"
#include "sculpt/model/Expression.hpp"
#include "sculpt/model/ToolCatalog.hpp"

namespace sculpt::api {

using json = nlohmann::json;

json vec(const Vec3d& v);
json vec(const Vec3f& v);
json vec(const Vec2d& v);
json bodyProperties(ModelStore& store, int body);
std::string refKind(SketchRef::Kind k);
json appearanceJson(const Appearance& a);
double parameterStep(const std::string& unit, double shown);

namespace {

/// A user-facing argument or precondition error.
struct CommandError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

json ok(json extra = json::object()) {
  extra["ok"] = true;
  return extra;
}

// MARK: Argument helpers

const json& need(const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) throw CommandError(std::string("Missing argument \"") + key + "\"");
  return *it;
}
bool has(const json& j, const char* key) {
  auto it = j.find(key);
  return it != j.end() && !it->is_null();
}
std::string str(const json& j, const char* key) {
  const json& v = need(j, key);
  if (!v.is_string()) throw CommandError(std::string("\"") + key + "\" must be a string");
  return v.get<std::string>();
}
double num(const json& j, const char* key) {
  const json& v = need(j, key);
  if (!v.is_number()) throw CommandError(std::string("\"") + key + "\" must be a number");
  double d = v.get<double>();
  if (!std::isfinite(d)) throw CommandError(std::string("\"") + key + "\" must be finite");
  return d;
}
int integer(const json& j, const char* key) {
  double d = num(j, key);
  if (d != std::floor(d) || std::abs(d) > 2e9) throw CommandError(std::string("\"") + key + "\" must be an integer");
  return int(d);
}
bool boolean(const json& j, const char* key) {
  const json& v = need(j, key);
  if (!v.is_boolean()) throw CommandError(std::string("\"") + key + "\" must be true or false");
  return v.get<bool>();
}
Uuid uuid(const json& j, const char* key) {
  auto u = Uuid::parse(str(j, key));
  if (!u) throw CommandError(std::string("\"") + key + "\" must be a UUID");
  return *u;
}
Vec3d vec3(const json& v, const char* key) {
  if (!v.is_array() || v.size() != 3) throw CommandError(std::string("\"") + key + "\" must be [x, y, z]");
  Vec3d out;
  for (int i = 0; i < 3; ++i) {
    if (!v[size_t(i)].is_number()) throw CommandError(std::string("\"") + key + "\" must be [x, y, z]");
    out[i] = v[size_t(i)].get<double>();
    if (!std::isfinite(out[i])) throw CommandError(std::string("\"") + key + "\" must be finite");
  }
  return out;
}
Vec3f color(const json& v, const char* key) {
  if (v.is_string()) {
    auto c = ModelStore::color(v.get<std::string>());
    if (!c) throw CommandError(std::string("\"") + key + "\" must be \"#rrggbb\" or [r, g, b]");
    return *c;
  }
  Vec3d d = vec3(v, key);
  auto c01 = [](double x) { return float(std::min(std::max(x, 0.0), 1.0)); };
  return {c01(d.x), c01(d.y), c01(d.z)};
}

/// A number typed into a field: "12,5" works too.
std::optional<double> parseNumber(const std::string& text) {
  std::string t = text;
  std::replace(t.begin(), t.end(), ',', '.');
  while (!t.empty() && std::isspace((unsigned char)t.front())) t.erase(t.begin());
  while (!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
  if (t.empty()) return std::nullopt;
  char* end = nullptr;
  double v = std::strtod(t.c_str(), &end);
  if (!end || *end != 0 || !std::isfinite(v)) return std::nullopt;
  return v;
}

struct Ctx {
  SculptDoc& doc;
  const json& args;
  ModelStore& store;
  ViewportController& view;
  LengthUnit units() const { return store.settings().units; }
};

void requireIdle(Ctx& c) {
  if (c.store.sketch()) throw CommandError("Finish or cancel the sketch first");
  if (c.store.draft()) throw CommandError("Apply or cancel the open tool first");
}
const SketchSession& requireSketch(Ctx& c) {
  if (!c.store.sketch()) throw CommandError("No sketch is open");
  return *c.store.sketch();
}
const Draft& requireDraft(Ctx& c) {
  if (!c.store.draft()) throw CommandError("No tool is open");
  return *c.store.draft();
}
const Feature& requireFeature(Ctx& c, const Uuid& id) {
  for (auto& f : c.store.features())
    if (f.id == id) return f;
  throw CommandError("No history step with id " + id.string());
}
const ReferenceItem& requireItem(Ctx& c, const Uuid& id) {
  for (auto& i : c.store.referenceItems())
    if (i.id == id) return i;
  throw CommandError("No item with id " + id.string());
}
int requireBody(Ctx& c, const json& v, const char* key = "id") {
  if (!v.is_number_integer()) throw CommandError(std::string("\"") + key + "\" must be a body id");
  int id = v.get<int>();
  auto ids = c.store.bodyIds();
  if (std::find(ids.begin(), ids.end(), id) == ids.end()) throw CommandError("No body " + std::to_string(id));
  return id;
}

/// A toast the store raised while running a command, as the command's error.
struct ToastWatch {
  ModelStore& store;
  std::optional<std::string> before;
  uint64_t revision;
  explicit ToastWatch(ModelStore& s) : store(s), before(s.toast()), revision(s.revision()) {}
  json result(json extra = json::object()) const {
    extra["ok"] = true;
    if (store.toast() && store.toast() != before) extra["toast"] = *store.toast();
    return extra;
  }
};

/// A value for a draft parameter or dimension, from {value} (mm / degrees),
/// {text} (as typed, in display units, or an expression) or {expression}.
struct Typed {
  std::optional<double> value;          // internal units
  std::optional<std::string> expression;  // bound expression
};
Typed typedValue(Ctx& c, const json& a, bool isLength) {
  Typed t;
  double s = isLength ? scale(c.units()) : 1;
  if (has(a, "value")) {
    t.value = num(a, "value");
  } else if (has(a, "text")) {
    std::string text = str(a, "text");
    if (auto v = parseNumber(text)) {
      t.value = *v * s;
    } else {
      ExpressionEvaluator e(c.store.settings().variables);
      auto v2 = e.evaluate(text);
      if (!v2) throw CommandError("\"" + text + "\" isn't a number or a valid expression");
      // Expressions are in the document's base units (mm, degrees), like the MCP tools.
      t.expression = text;
      t.value = *v2;
    }
  } else if (has(a, "expression")) {
    std::string text = str(a, "expression");
    ExpressionEvaluator e(c.store.settings().variables);
    auto v = e.evaluate(text);
    if (!v) throw CommandError("\"" + text + "\" isn't a valid expression");
    t.expression = text;
    t.value = *v;
  } else {
    throw CommandError("Give \"value\", \"text\" or \"expression\"");
  }
  return t;
}

std::optional<SketchRef::Kind> refKindNamed(const std::string& s) {
  for (auto k : {SketchRef::Kind::Point, SketchRef::Kind::Line, SketchRef::Kind::Circle, SketchRef::Kind::Constraint,
                 SketchRef::Kind::Spline, SketchRef::Kind::Ellipse})
    if (refKind(k) == s) return k;
  return std::nullopt;
}

SketchRef parseRef(const SketchSession& s, const json& r) {
  if (!r.is_object()) throw CommandError("A sketch reference is {\"kind\", \"index\"}");
  auto kind = refKindNamed(str(r, "kind"));
  if (!kind) throw CommandError("Unknown sketch reference kind \"" + str(r, "kind") + "\"");
  int i = integer(r, "index");
  size_t n = 0;
  switch (*kind) {
    case SketchRef::Kind::Point: n = s.sketch.points.size(); break;
    case SketchRef::Kind::Line: n = s.sketch.lines.size(); break;
    case SketchRef::Kind::Circle: n = s.sketch.circles.size(); break;
    case SketchRef::Kind::Constraint: n = s.sketch.constraints.size(); break;
    case SketchRef::Kind::Spline: n = s.sketch.splines.size(); break;
    case SketchRef::Kind::Ellipse: n = s.sketch.ellipses.size(); break;
  }
  if (i < 0 || size_t(i) >= n) throw CommandError("Sketch " + refKind(*kind) + " " + std::to_string(i) + " doesn't exist");
  return {*kind, i};
}

std::optional<Pick> parsePick(Ctx& c, const json& p) {
  if (!p.is_object()) throw CommandError("A pick is {\"kind\": \"body|face|edge\", \"body\", \"index\"}");
  std::string kind = str(p, "kind");
  int body = requireBody(c, need(p, "body"), "body");
  if (kind == "body") return Pick::bodyPick(body);
  int index = integer(p, "index");
  auto mesh = c.store.meshes().find(body);
  if (kind == "face") {
    if (mesh != c.store.meshes().end() && (index < 0 || index >= mesh->second->faceCount))
      throw CommandError("Body " + std::to_string(body) + " has no face " + std::to_string(index));
    return Pick::face(body, index);
  }
  if (kind == "edge") {
    if (mesh != c.store.meshes().end() && (index < 0 || index >= mesh->second->edgeCount))
      throw CommandError("Body " + std::to_string(body) + " has no edge " + std::to_string(index));
    return Pick::edge(body, index);
  }
  throw CommandError("Pick kind must be body, face or edge");
}

SketchPlane parsePlane(const json& v) {
  if (v.is_string()) {
    std::string p = v.get<std::string>();
    if (p == "xy" || p == "top") return SketchPlane::xy();
    if (p == "xz" || p == "front") return SketchPlane::xz();
    if (p == "yz" || p == "right") return SketchPlane::yz();
    throw CommandError("plane must be xy, xz, yz or {origin, normal, xDir}");
  }
  if (!v.is_object()) throw CommandError("plane must be xy, xz, yz or {origin, normal, xDir}");
  SketchPlane plane;
  plane.origin = has(v, "origin") ? vec3(v["origin"], "origin") : Vec3d();
  Vec3d n = vec3(need(v, "normal"), "normal");
  if (length(n) < 1e-9) throw CommandError("normal must not be zero");
  plane.normal = normalize(n);
  Vec3d x = has(v, "xDir") ? vec3(v["xDir"], "xDir") : Vec3d(1, 0, 0);
  x = x - plane.normal * dot(x, plane.normal);
  if (length(x) < 1e-6) {
    Vec3d alt = std::abs(plane.normal.z) < 0.9 ? Vec3d(0, 0, 1) : Vec3d(1, 0, 0);
    x = cross(alt, plane.normal);
  }
  plane.xDir = normalize(x);
  return plane;
}

std::optional<vp::Camera::Preset> preset(const std::string& p) {
  using P = vp::Camera::Preset;
  static const std::map<std::string, P> m = {{"iso", P::Iso},     {"front", P::Front}, {"back", P::Back},
                                             {"top", P::Top},     {"bottom", P::Bottom}, {"right", P::Right},
                                             {"left", P::Left},   {"default", P::Iso}};
  auto it = m.find(p);
  if (it == m.end()) return std::nullopt;
  return it->second;
}

std::optional<PointPickPurpose> purposeNamed(const std::string& s) {
  using P = PointPickPurpose;
  static const std::map<std::string, P> m = {{"planeThrough3Points", P::planeThrough3Points},
                                             {"planeOnCurveAtPoint", P::planeOnCurveAtPoint},
                                             {"planePerpendicularToEdgeAtPoint", P::planePerpendicularToEdgeAtPoint},
                                             {"planeParallelToFaceAtPoint", P::planeParallelToFaceAtPoint},
                                             {"axisThrough2Points", P::axisThrough2Points},
                                             {"axisPerpendicularToFaceAtPoint", P::axisPerpendicularToFaceAtPoint},
                                             {"measurePointToPoint", P::measurePointToPoint},
                                             {"measureThreePointAngle", P::measureThreePointAngle}};
  auto it = m.find(s);
  if (it == m.end()) return std::nullopt;
  return it->second;
}

std::string readTextFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw CommandError("Couldn't read " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string baseName(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = name.find_last_of('.');
  return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

json toolJson(const ToolCommand& t, const ModelStore& s) {
  auto sc = ToolCatalog::shortcut(t.id);
  return {{"id", t.id},
          {"title", t.title},
          {"symbol", t.symbol},
          {"hint", t.hint},
          {"category", rawValue(t.category)},
          {"categoryTitle", title(t.category)},
          {"enabled", t.isEnabled(s)},
          {"shortcut", sc ? json(*sc) : json(nullptr)}};
}

/// Runs a ToolCatalog UI action in the core when it can, else hands it back.
json uiResult(Ctx& c, const std::string& action, const ToastWatch& w) {
  if (action.empty() || c.view.performAction(action)) return w.result();
  return w.result({{"ui", action}});
}

// MARK: Drawing

json drawingSheetJson(const DrawingSheet& sheet) {
  json strokes = json::array(), labels = json::array();
  for (auto& s : sheet.strokes) {
    json pts = json::array();
    for (auto& p : s.points) pts.push_back(vec(p));
    strokes.push_back({{"points", pts}, {"dashed", s.dashed}, {"width", s.width}});
  }
  for (auto& l : sheet.labels) labels.push_back({{"text", l.text}, {"at", vec(l.at)}, {"size", l.size}, {"bold", l.bold}});
  return {{"size", vec(sheet.size)},
          {"scale", sheet.scale},
          {"scaleText", DrawingSheet::scaleText(sheet.scale)},
          {"strokes", strokes},
          {"labels", labels}};
}

// MARK: The command table

using Handler = std::function<json(Ctx&)>;

const std::map<std::string, Handler>& handlers() {
  static const std::map<std::string, Handler> table = [] {
    std::map<std::string, Handler> h;

    h["state"] = [](Ctx& c) { return ok({{"state", documentState(c.doc)}}); };
    h["toast.dismiss"] = [](Ctx& c) {
      c.store.setToast(std::nullopt);
      return ok();
    };
    h["document.setName"] = [](Ctx& c) {
      std::string n = str(c.args, "name");
      c.store.setDocumentName(n.empty() ? "Untitled" : n);
      return ok();
    };
    h["document.setPath"] = [](Ctx& c) {
      c.store.setDocumentPath(str(c.args, "path"));
      return ok();
    };

    // MARK: Tools (rail, flyouts, search, menus)
    h["tool.run"] = [](Ctx& c) {
      std::string id = str(c.args, "id");
      auto cmd = ToolCatalog::command(id);
      if (!cmd) throw CommandError("Unknown tool \"" + id + "\"");
      if (!cmd->isEnabled(c.store)) throw CommandError(cmd->title + " isn't available now" + (cmd->hint.empty() ? "" : " (" + cmd->hint + ")"));
      ToastWatch w(c.store);
      auto action = ToolCatalog::run(c.store, id);
      return uiResult(c, action.value_or(""), w);
    };
    h["tool.search"] = [](Ctx& c) {
      std::string q = has(c.args, "query") ? str(c.args, "query") : "";
      json results = json::array();
      for (auto* t : ToolCatalog::search(c.store, q)) results.push_back(toolJson(*t, c.store));
      return ok({{"results", results}});
    };
    h["action.run"] = [](Ctx& c) {
      std::string id = str(c.args, "id");
      json sub = c.args;
      sub["cmd"] = id;
      return runCommand(c.doc, sub);
    };

    // MARK: History
    h["feature.edit"] = [](Ctx& c) {
      requireIdle(c);
      const Feature& f = requireFeature(c, uuid(c.args, "id"));
      if (f.op.parameters().empty()) throw CommandError(f.title() + " has no parameters to edit");
      c.store.edit(f.id);
      return ok();
    };
    h["feature.open"] = [](Ctx& c) {
      requireIdle(c);
      const Feature& f = requireFeature(c, uuid(c.args, "id"));
      if (f.op.parameters().empty() && !f.op.sketch()) throw CommandError(f.title() + " can't be edited");
      c.store.editItem(f.id);
      return ok();
    };
    h["feature.editSketch"] = [](Ctx& c) {
      requireIdle(c);
      const Feature& f = requireFeature(c, uuid(c.args, "id"));
      if (!f.op.sketch()) throw CommandError(f.title() + " has no sketch");
      c.store.editSketch(f.id);
      return ok();
    };
    h["feature.toggleSuppressed"] = [](Ctx& c) {
      requireIdle(c);
      ToastWatch w(c.store);
      c.store.toggleSuppressed(requireFeature(c, uuid(c.args, "id")).id);
      return w.result();
    };
    h["feature.delete"] = [](Ctx& c) {
      requireIdle(c);
      ToastWatch w(c.store);
      c.store.deleteFeature(requireFeature(c, uuid(c.args, "id")).id);
      return w.result();
    };

    // MARK: Draft (operation panel)
    h["draft.setParameter"] = [](Ctx& c) {
      const Draft& d = requireDraft(c);
      int id = integer(c.args, "id");
      const Operation& op = d.feature.op;
      auto params = op.parameters();
      auto p = std::find_if(params.begin(), params.end(), [&](auto& x) { return x.id == id; });
      if (p == params.end()) throw CommandError("The tool has no parameter " + std::to_string(id));
      Typed t = typedValue(c, c.args, p->unit == "mm");
      bool bound = d.feature.bindings && d.feature.bindings->count(id);
      if (t.expression) {
        c.store.bindDraft(id, *t.expression);
      } else {
        // A plain value replaces an expression bound to the field.
        if (bound) c.store.bindDraft(id, std::nullopt);
        c.store.updateDraft(c.store.draft()->feature.op.setting(id, *t.value));
      }
      return ok({{"error", c.store.draftError() ? json(*c.store.draftError()) : json(nullptr)}});
    };
    h["draft.stepParameter"] = [](Ctx& c) {
      const Draft& d = requireDraft(c);
      int id = integer(c.args, "id");
      double dir = num(c.args, "direction") >= 0 ? 1 : -1;
      auto params = d.feature.op.parameters();
      auto p = std::find_if(params.begin(), params.end(), [&](auto& x) { return x.id == id; });
      if (p == params.end()) throw CommandError("The tool has no parameter " + std::to_string(id));
      double s = p->unit == "mm" ? scale(c.units()) : 1;
      double shown = p->value / s;
      double v = (shown + dir * parameterStep(p->unit, shown)) * s;
      if (d.feature.bindings && d.feature.bindings->count(id)) c.store.bindDraft(id, std::nullopt);
      c.store.updateDraft(c.store.draft()->feature.op.setting(id, v));
      return ok();
    };
    h["draft.setChoice"] = [](Ctx& c) {
      const Draft& d = requireDraft(c);
      int id = integer(c.args, "id"), index = integer(c.args, "index");
      auto choices = d.feature.op.choices();
      auto ch = std::find_if(choices.begin(), choices.end(), [&](auto& x) { return x.id == id; });
      if (ch == choices.end()) throw CommandError("The tool has no choice " + std::to_string(id));
      if (index < 0 || size_t(index) >= ch->options.size()) throw CommandError("Choice index out of range");
      c.store.updateDraft(d.feature.op.settingChoice(id, index));
      return ok();
    };
    h["draft.setCombineMode"] = [](Ctx& c) {
      const Draft& d = requireDraft(c);
      auto mode = combineMode(str(c.args, "mode"));
      if (!mode) throw CommandError("mode must be newBody, join or cut");
      if (!d.feature.op.combineMode() || !d.feature.op.sketchTarget()) throw CommandError("This tool has no Result choice");
      c.store.updateDraft(d.feature.op.setting(*mode, d.feature.op.sketchTarget()));
      return ok();
    };
    h["draft.setRevolveAxis"] = [](Ctx& c) {
      const Draft& d = requireDraft(c);
      if (!d.feature.op.is<op::Revolve>()) throw CommandError("Only Revolve has an axis");
      c.store.updateDraft(d.feature.op.settingAroundV(boolean(c.args, "aroundV")));
      return ok();
    };

    // MARK: Insert and import
    h["insert.primitive"] = [](Ctx& c) {
      requireIdle(c);
      auto kind = primitiveKind(str(c.args, "kind"));
      if (!kind) throw CommandError("kind must be box, cylinder, sphere, cone or torus");
      c.store.addPrimitive(*kind);
      return ok();
    };
    h["import.file"] = [](Ctx& c) {
      std::string err = importPath(c.doc, str(c.args, "path"));
      if (!err.empty()) throw CommandError(err);
      return ok();
    };
    h["import.image"] = [](Ctx& c) {
      std::string path = str(c.args, "path");
      std::string as = has(c.args, "as") ? str(c.args, "as") : "image";
      int mode = as == "image" ? 0 : as == "decal" ? 1 : as == "terrain" ? 2 : -1;
      if (mode < 0) throw CommandError("as must be image, decal or terrain");
      std::string bytes = readTextFile(path);
      std::string name = has(c.args, "name") ? str(c.args, "name") : baseName(path);
      std::string err = importImage(c.doc, std::vector<uint8_t>(bytes.begin(), bytes.end()), name, mode);
      if (!err.empty()) throw CommandError(err);
      return ok();
    };
    h["import.material"] = [](Ctx& c) {
      requireIdle(c);
      std::string text, name;
      if (has(c.args, "path")) {
        text = readTextFile(str(c.args, "path"));
        name = baseName(str(c.args, "path"));
      } else {
        text = str(c.args, "json");
        name = has(c.args, "fileName") ? str(c.args, "fileName") : "Material";
      }
      ToastWatch w(c.store);
      c.store.importMaterial(text, name);
      return w.result();
    };

    // MARK: Sketch
    h["sketch.start"] = [](Ctx& c) {
      requireIdle(c);
      c.store.startSketch(parsePlane(need(c.args, "plane")));
      return ok();
    };
    h["sketch.startOnItem"] = [](Ctx& c) {
      requireIdle(c);
      const ReferenceItem& item = requireItem(c, uuid(c.args, "id"));
      if (!item.isPlane()) throw CommandError(item.name + " isn't a plane");
      c.store.startSketch(item.plane());
      return ok();
    };
    h["sketch.setTool"] = [](Ctx& c) {
      auto tool = sketchTool(str(c.args, "tool"));
      if (!tool) throw CommandError("Unknown sketch tool \"" + str(c.args, "tool") + "\"");
      if (c.store.draft()) throw CommandError("Apply or cancel the open tool first");
      // The Sketch menu starts a sketch when none is open.
      if (!c.store.sketch()) startSketchHere(c.store);
      if (!c.store.sketch()) throw CommandError("Couldn't start a sketch");
      c.store.setSketchTool(*tool);
      if (has(c.args, "sides")) {
        int n = integer(c.args, "sides");
        if (n < 3 || n > 24) throw CommandError("sides must be 3…24");
        c.store.setPolygonSides(n);
      }
      if (has(c.args, "circumscribed")) c.store.setPolygonCircumscribed(boolean(c.args, "circumscribed"));
      return ok();
    };
    h["sketch.setPolygonSides"] = [](Ctx& c) {
      requireSketch(c);
      int n = integer(c.args, "sides");
      if (n < 3 || n > 24) throw CommandError("sides must be 3…24");
      c.store.setPolygonSides(n);
      return ok();
    };
    h["sketch.setPolygonCircumscribed"] = [](Ctx& c) {
      requireSketch(c);
      c.store.setPolygonCircumscribed(boolean(c.args, "on"));
      return ok();
    };
    h["sketch.setConstructionMode"] = [](Ctx& c) {
      requireSketch(c);
      c.store.setConstructionMode(boolean(c.args, "on"));
      return ok();
    };
    auto positiveLength = [](Ctx& c) {
      double v;
      if (has(c.args, "text")) {
        auto n = parseNumber(str(c.args, "text"));
        if (!n) throw CommandError("Not a number");
        v = *n * scale(c.units());
      } else {
        v = num(c.args, "value");
      }
      if (!(v > 0)) throw CommandError("The value must be positive");
      return v;
    };
    h["sketch.setOffsetDistance"] = [positiveLength](Ctx& c) {
      requireSketch(c);
      c.store.setOffsetDistance(positiveLength(c));
      return ok();
    };
    h["sketch.setFilletRadius"] = [positiveLength](Ctx& c) {
      requireSketch(c);
      c.store.setFilletRadius(positiveLength(c));
      return ok();
    };
    h["sketch.select"] = [](Ctx& c) {
      const SketchSession& s = requireSketch(c);
      const json& refs = need(c.args, "refs");
      if (!refs.is_array()) throw CommandError("refs must be an array");
      std::vector<SketchRef> out;
      for (auto& r : refs) out.push_back(parseRef(s, r));
      c.store.setSketchSelection(out);
      return ok();
    };
    h["sketch.clearSelection"] = [](Ctx& c) {
      requireSketch(c);
      c.store.setSketchSelection({});
      return ok();
    };
    h["sketch.toggleConstraintSelection"] = [](Ctx& c) {
      const SketchSession& s = requireSketch(c);
      int i = integer(c.args, "index");
      if (i < 0 || size_t(i) >= s.sketch.constraints.size()) throw CommandError("No constraint " + std::to_string(i));
      c.store.toggleSketchConstraintSelection(i);
      return ok();
    };
    h["sketch.constrain"] = [](Ctx& c) {
      const SketchSession& s = requireSketch(c);
      auto kind = constraintKind(str(c.args, "kind"));
      if (!kind) throw CommandError("Unknown constraint \"" + str(c.args, "kind") + "\"");
      auto kinds = s.availableConstraints();
      if (std::find(kinds.begin(), kinds.end(), *kind) == kinds.end())
        throw CommandError(title(*kind) + " doesn't fit the selection");
      ToastWatch w(c.store);
      c.store.constrainSelection(*kind);
      return w.result();
    };
    h["sketch.constraintColumn"] = [](Ctx& c) {
      const SketchSession& s = requireSketch(c);
      std::string id = str(c.args, "id");
      using K = SketchConstraint::Kind;
      auto kinds = s.availableConstraints();
      auto hasK = [&](K k) { return std::find(kinds.begin(), kinds.end(), k) != kinds.end(); };
      auto pts = s.selectedPoints().size(), lines = s.selectedLines().size(), circles = s.selectedCircles().size();
      ToastWatch w(c.store);
      auto need = [&](bool ok, const char* what) {
        if (!ok) throw CommandError(std::string(what) + " doesn't fit the selection");
      };
      if (id == "settings") {
        c.view.showConstraints = !c.view.showConstraints;
        c.view.stateChanged();
      } else if (id == "parallel") {
        need(hasK(K::Parallel), "Parallel");
        c.store.constrainSelection(K::Parallel);
      } else if (id == "perpendicular") {
        need(hasK(K::Perpendicular), "Perpendicular");
        c.store.constrainSelection(K::Perpendicular);
      } else if (id == "tangent") {
        need(hasK(K::Tangent), "Tangent");
        c.store.constrainSelection(K::Tangent);
      } else if (id == "coincident") {
        need(hasK(K::Coincident) || hasK(K::PointOnLine) || hasK(K::PointOnCircle), "Coincident");
        c.store.constrainSelection(hasK(K::Coincident) ? K::Coincident : (hasK(K::PointOnLine) ? K::PointOnLine : K::PointOnCircle));
      } else if (id == "midpoint") {
        need(hasK(K::Midpoint), "Midpoint");
        c.store.constrainSelection(K::Midpoint);
      } else if (id == "concentric") {
        need(circles == 2 && pts == 0 && lines == 0, "Concentric");
        c.store.constrainSelection(K::Coincident);
      } else if (id == "hv") {
        need(lines > 0 && pts == 0 && circles == 0, "Horizontal/Vertical");
        c.store.constrainHorizontalOrVertical();
      } else if (id == "equal") {
        need(hasK(K::EqualLength) || hasK(K::EqualRadius), "Equal");
        c.store.constrainSelection(hasK(K::EqualLength) ? K::EqualLength : K::EqualRadius);
      } else if (id == "symmetry") {
        need(hasK(K::Symmetric), "Symmetry");
        c.store.constrainSelection(K::Symmetric);
      } else if (id == "disconnect") {
        need(pts == 1, "Disconnect");
        c.store.disconnectSketchSelection();
      } else if (id == "lock") {
        need(hasK(K::Fixed), "Lock");
        c.store.constrainSelection(K::Fixed);
      } else if (id == "construction") {
        need(!s.selection.empty(), "Make Construction");
        c.store.toggleConstruction();
      } else {
        throw CommandError("Unknown constraint column item \"" + id + "\"");
      }
      return w.result();
    };
    h["sketch.setDimension"] = [](Ctx& c) {
      const SketchSession& s = requireSketch(c);
      int i = integer(c.args, "index");
      if (i < 0 || size_t(i) >= s.sketch.constraints.size()) throw CommandError("No constraint " + std::to_string(i));
      const auto& con = s.sketch.constraints[size_t(i)];
      if (!isDimension(con.kind)) throw CommandError("Constraint " + std::to_string(i) + " isn't a dimension");
      Typed t = typedValue(c, c.args, con.unit() == "mm");
      ToastWatch w(c.store);
      c.store.setDimension(i, *t.value);
      json r = w.result();
      if (r.contains("toast")) {
        r["ok"] = false;
        r["error"] = r["toast"];
      }
      return r;
    };
    h["sketch.transform"] = [](Ctx& c) {
      requireSketch(c);
      std::string k = str(c.args, "kind");
      SketchTransformKind kind;
      if (k == "scale") kind = SketchTransformKind::scale;
      else if (k == "linearPattern") kind = SketchTransformKind::linearPattern;
      else if (k == "circularPattern") kind = SketchTransformKind::circularPattern;
      else if (k == "rotate") kind = SketchTransformKind::rotate;
      else throw CommandError("kind must be scale, linearPattern, circularPattern or rotate");
      if (!c.store.canTransformSketch()) throw CommandError("Select sketch geometry first");
      SketchTransformParams p;
      if (has(c.args, "factor")) p.factor = num(c.args, "factor");
      if (has(c.args, "angle")) p.angle = num(c.args, "angle");
      if (has(c.args, "count")) p.count = integer(c.args, "count");
      if (has(c.args, "spacing")) p.spacing = num(c.args, "spacing");
      if (has(c.args, "direction")) p.direction = num(c.args, "direction");
      if (has(c.args, "totalAngle")) p.totalAngle = num(c.args, "totalAngle");
      if (has(c.args, "keepOriginal")) p.keepOriginal = boolean(c.args, "keepOriginal");
      if (kind == SketchTransformKind::scale && !(p.factor > 0)) throw CommandError("factor must be positive");
      if (kind == SketchTransformKind::linearPattern && (p.count < 2 || p.count > 200)) throw CommandError("count must be 2…200");
      if (kind == SketchTransformKind::circularPattern && (p.count < 2 || p.count > 360)) throw CommandError("count must be 2…360");
      ToastWatch w(c.store);
      c.store.transformSketchSelection(kind, p);
      return w.result();
    };
    h["sketch.addOutlines"] = [](Ctx& c) {
      requireSketch(c);
      const json& path = need(c.args, "path");
      if (!path.is_array()) throw CommandError("path must be an array of {op, points}");
      std::vector<PathElement> els;
      for (auto& e : path) {
        PathElement pe;
        std::string op = str(e, "op");
        size_t n;
        if (op == "move") pe.kind = PathElement::Kind::moveTo, n = 1;
        else if (op == "line") pe.kind = PathElement::Kind::lineTo, n = 1;
        else if (op == "quad") pe.kind = PathElement::Kind::quadTo, n = 2;
        else if (op == "cubic") pe.kind = PathElement::Kind::cubicTo, n = 3;
        else if (op == "close") pe.kind = PathElement::Kind::close, n = 0;
        else throw CommandError("op must be move, line, quad, cubic or close");
        if (n) {
          const json& pts = need(e, "points");
          if (!pts.is_array() || pts.size() != n) throw CommandError(op + " needs " + std::to_string(n) + " points");
          for (size_t i = 0; i < n; ++i) {
            const json& p = pts[i];
            if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number())
              throw CommandError("points are [x, y] in mm");
            pe.p[i] = Vec2d(p[0].get<double>(), p[1].get<double>());
          }
        }
        els.push_back(pe);
      }
      ToastWatch w(c.store);
      c.store.addSketchOutlines(els);
      return w.result();
    };
    h["sketch.setShowConstraints"] = [](Ctx& c) {
      c.view.showConstraints = boolean(c.args, "on");
      c.view.stateChanged();
      return ok();
    };

    // MARK: Selection, bodies and items
    h["selection.set"] = [](Ctx& c) {
      requireIdle(c);
      const json& picks = need(c.args, "picks");
      if (!picks.is_array()) throw CommandError("picks must be an array");
      std::vector<Pick> sel;
      for (auto& p : picks)
        if (auto pk = parsePick(c, p)) sel.push_back(*pk);
      c.store.setSelection(sel);
      return ok();
    };
    h["selection.clear"] = [](Ctx& c) {
      c.store.setSelection({});
      return ok();
    };
    h["selection.toggleBody"] = [](Ctx& c) {
      int id = requireBody(c, need(c.args, "id"));
      bool selected = std::find(c.store.selection().begin(), c.store.selection().end(), Pick::bodyPick(id)) !=
                      c.store.selection().end();
      c.store.setSelection(selected ? std::vector<Pick>{} : std::vector<Pick>{Pick::bodyPick(id)});
      return ok();
    };
    h["selection.selectBodies"] = [](Ctx& c) {
      const json& ids = need(c.args, "ids");
      if (!ids.is_array()) throw CommandError("ids must be an array of body ids");
      std::vector<Pick> sel;
      for (auto& v : ids) sel.push_back(Pick::bodyPick(requireBody(c, v, "ids")));
      c.store.setSelection(sel);
      return ok();
    };
    h["item.toggleSelection"] = [](Ctx& c) {
      c.store.toggleItemSelection(requireItem(c, uuid(c.args, "id")).id);
      return ok();
    };
    h["body.rename"] = [](Ctx& c) {
      int id = requireBody(c, need(c.args, "id"));
      c.store.renameBody(id, str(c.args, "name"));
      return ok();
    };
    h["body.toggleHidden"] = [](Ctx& c) {
      c.store.toggleHiddenBody(requireBody(c, need(c.args, "id")));
      return ok();
    };
    h["body.setVisible"] = [](Ctx& c) {
      int id = requireBody(c, need(c.args, "id"));
      bool hidden = c.store.settings().hiddenBodies.count(id) > 0;
      if (boolean(c.args, "visible") == hidden) c.store.toggleHiddenBody(id);
      return ok();
    };
    h["body.properties"] = [](Ctx& c) {
      int id = requireBody(c, need(c.args, "id"));
      return ok({{"properties", bodyProperties(c.store, id)}});
    };
    h["item.rename"] = [](Ctx& c) {
      c.store.renameItem(requireItem(c, uuid(c.args, "id")).id, str(c.args, "name"));
      return ok();
    };
    h["item.toggleHidden"] = [](Ctx& c) {
      c.store.toggleHiddenItem(requireItem(c, uuid(c.args, "id")).id);
      return ok();
    };
    h["item.edit"] = [](Ctx& c) {
      requireIdle(c);
      c.store.editItem(requireItem(c, uuid(c.args, "id")).id);
      return ok();
    };
    h["item.extrude"] = [](Ctx& c) {
      requireIdle(c);
      const ReferenceItem& item = requireItem(c, uuid(c.args, "id"));
      if (!item.isSketch()) throw CommandError(item.name + " isn't a sketch");
      ToastWatch w(c.store);
      c.store.useSketchItem(item.id, false);
      return w.result();
    };
    h["item.revolve"] = [](Ctx& c) {
      requireIdle(c);
      const ReferenceItem& item = requireItem(c, uuid(c.args, "id"));
      if (!item.isSketch()) throw CommandError(item.name + " isn't a sketch");
      ToastWatch w(c.store);
      c.store.useSketchItem(item.id, true);
      return w.result();
    };
    h["item.delete"] = [](Ctx& c) {
      requireIdle(c);
      c.store.deleteFeature(requireItem(c, uuid(c.args, "id")).id);
      return ok();
    };

    // MARK: Appearance (Visualize, Properties, Appearance panels)
    h["appearance.set"] = [](Ctx& c) {
      std::vector<int> targets;
      if (has(c.args, "bodies")) {
        const json& ids = c.args["bodies"];
        if (!ids.is_array()) throw CommandError("bodies must be an array of body ids");
        for (auto& v : ids) targets.push_back(requireBody(c, v, "bodies"));
      } else {
        targets = c.store.appearanceTargets();
      }
      if (targets.empty()) throw CommandError("There are no bodies to change");
      Appearance a;
      auto it = c.store.settings().appearance.find(targets.front());
      if (it != c.store.settings().appearance.end()) a = it->second;
      if (has(c.args, "reset") && boolean(c.args, "reset")) {
        a = Appearance();
      } else if (has(c.args, "preset")) {
        auto p = MaterialPreset::preset(str(c.args, "preset"));
        if (!p) throw CommandError("Unknown material \"" + str(c.args, "preset") + "\"");
        a = p->appearance;
      } else {
        bool custom = false;
        if (has(c.args, "color")) a.color = color(c.args["color"], "color"), custom = true;
        if (has(c.args, "metallic")) a.metallic = float(std::min(std::max(num(c.args, "metallic"), 0.0), 1.0)), custom = true;
        if (has(c.args, "roughness")) a.roughness = float(std::min(std::max(num(c.args, "roughness"), 0.0), 1.0)), custom = true;
        if (has(c.args, "opacity")) a.opacity = float(std::min(std::max(num(c.args, "opacity"), 0.05), 1.0)), custom = true;
        if (has(c.args, "finish")) {
          std::string f = str(c.args, "finish");
          if (f == "matte") a.metallic = 0, a.roughness = 0.9f;
          else if (f == "plastic") a.metallic = 0, a.roughness = 0.45f;
          else if (f == "metal") a.metallic = 1, a.roughness = 0.25f;
          else throw CommandError("finish must be matte, plastic or metal");
          custom = true;
        }
        if (!custom) throw CommandError("Give color, metallic, roughness, opacity, finish, preset or reset");
        a.preset.reset();
      }
      c.store.setAppearance(a, targets);
      return ok();
    };
    h["materials.library"] = [](Ctx& c) {
      std::string q = has(c.args, "query") ? str(c.args, "query") : "";
      auto hits = q.empty() ? MaterialPreset::library() : MaterialPreset::search(q);
      json families = json::array();
      for (auto& fam : MaterialPreset::families()) {
        json presets = json::array();
        for (auto& p : hits)
          if (p.family == fam)
            presets.push_back({{"id", p.id}, {"finish", p.finish}, {"name", p.name}, {"title", p.title()},
                               {"appearance", appearanceJson(p.appearance)}});
        if (!presets.empty()) families.push_back({{"family", fam}, {"presets", presets}});
      }
      return ok({{"families", families}});
    };

    // MARK: Settings and variables
    h["settings.setUnits"] = [](Ctx& c) {
      auto u = lengthUnit(str(c.args, "units"));
      if (!u) throw CommandError("units must be mm, cm, m, inch or ft");
      c.store.updateSettings([&](ProjectSettings& s) { s.units = *u; });
      return ok();
    };
    h["settings.setSnap"] = [](Ctx& c) {
      SnapSettings snap = c.store.settings().snap;
      bool any = false;
      auto setB = [&](const char* k, bool& field) {
        if (has(c.args, k)) field = boolean(c.args, k), any = true;
      };
      setB("grid", snap.grid);
      setB("guideLines", snap.guideLines);
      setB("guidePoints", snap.guidePoints);
      setB("points3D", snap.points3D);
      setB("distantEdges", snap.distantEdges);
      setB("showGuidePoints", snap.showGuidePoints);
      setB("showHints", snap.showHints);
      bool snapping = c.store.settings().snapping;
      if (has(c.args, "snapping")) snapping = boolean(c.args, "snapping"), any = true;
      if (!any) throw CommandError("Give at least one snap setting");
      c.store.updateSettings([&](ProjectSettings& s) {
        s.snap = snap;
        s.snapping = snapping;
      });
      return ok();
    };
    h["settings.setEnvironment"] = [](Ctx& c) {
      EnvironmentSettings e = c.store.settings().environment;
      bool any = false;
      auto setB = [&](const char* k, bool& f) {
        if (has(c.args, k)) f = boolean(c.args, k), any = true;
      };
      auto setF = [&](const char* k, float& f, float lo, float hi) {
        if (has(c.args, k)) f = float(std::min(std::max(num(c.args, k), double(lo)), double(hi))), any = true;
      };
      if (has(c.args, "background")) e.background = color(c.args["background"], "background"), any = true;
      setB("gradient", e.gradient);
      setB("showGrid", e.showGrid);
      setF("lightIntensity", e.lightIntensity, 0, 2);
      setF("lightAzimuth", e.lightAzimuth, -180, 180);
      setB("groundShadow", e.groundShadow);
      setF("lightElevation", e.lightElevation, 10, 85);
      setB("castShadows", e.castShadows);
      setF("shadowSoftness", e.shadowSoftness, 0, 1);
      setB("ambientOcclusion", e.ambientOcclusion);
      setF("occlusionStrength", e.occlusionStrength, 0, 1);
      setB("smoothShading", e.smoothShading);
      if (has(c.args, "reset") && boolean(c.args, "reset")) e = EnvironmentSettings(), any = true;
      if (!any) throw CommandError("Give at least one environment setting");
      c.store.updateSettings([&](ProjectSettings& s) { s.environment = e; });
      return ok();
    };
    h["settings.setCamera"] = [](Ctx& c) {
      CameraSettings cam = c.store.settings().camera;
      bool any = false;
      if (has(c.args, "orthographic")) cam.orthographic = boolean(c.args, "orthographic"), any = true;
      if (has(c.args, "fieldOfView")) cam.fieldOfView = float(std::min(std::max(num(c.args, "fieldOfView"), 5.0), 120.0)), any = true;
      if (has(c.args, "reset") && boolean(c.args, "reset")) cam = CameraSettings(), any = true;
      if (!any) throw CommandError("Give orthographic, fieldOfView or reset");
      c.store.updateSettings([&](ProjectSettings& s) { s.camera = cam; });
      return ok();
    };
    h["variable.add"] = [](Ctx& c) {
      c.store.addVariable();
      return ok({{"id", c.store.settings().variables.back().id.string()}});
    };
    h["variable.update"] = [](Ctx& c) {
      Uuid id = uuid(c.args, "id");
      auto vars = c.store.settings().variables;
      auto it = std::find_if(vars.begin(), vars.end(), [&](auto& v) { return v.id == id; });
      if (it == vars.end()) throw CommandError("No variable " + id.string());
      if (has(c.args, "name")) it->name = str(c.args, "name");
      if (has(c.args, "expression")) it->expression = str(c.args, "expression");
      c.store.updateSettings([&](ProjectSettings& s) { s.variables = vars; });
      return ok();
    };
    h["variable.remove"] = [](Ctx& c) {
      Uuid id = uuid(c.args, "id");
      auto vars = c.store.settings().variables;
      auto it = std::find_if(vars.begin(), vars.end(), [&](auto& v) { return v.id == id; });
      if (it == vars.end()) throw CommandError("No variable " + id.string());
      vars.erase(it);
      c.store.updateSettings([&](ProjectSettings& s) { s.variables = vars; });
      return ok();
    };
    h["format.set"] = [](Ctx& c) {
      bool any = false;
      if (has(c.args, "footInch")) {
        std::string f = str(c.args, "footInch");
        if (f == rawValue(FootInchFormat::architectural)) DisplayFormat::setFootInch(FootInchFormat::architectural);
        else if (f == rawValue(FootInchFormat::decimal)) DisplayFormat::setFootInch(FootInchFormat::decimal);
        else throw CommandError("footInch must be architectural or decimal");
        any = true;
      }
      if (has(c.args, "degrees")) {
        std::string f = str(c.args, "degrees");
        if (f == rawValue(DegreeFormat::decimal)) DisplayFormat::setDegrees(DegreeFormat::decimal);
        else if (f == rawValue(DegreeFormat::degreesMinutesSeconds)) DisplayFormat::setDegrees(DegreeFormat::degreesMinutesSeconds);
        else throw CommandError("degrees must be decimal or degreesMinutesSeconds");
        any = true;
      }
      if (!any) throw CommandError("Give footInch or degrees");
      c.view.stateChanged();
      return ok();
    };

    // MARK: View
    h["view.look"] = [](Ctx& c) {
      auto p = preset(str(c.args, "preset"));
      if (!p) throw CommandError("preset must be iso, front, back, top, bottom, right or left");
      c.view.look(*p);
      return ok();
    };
    h["view.fit"] = [](Ctx& c) {
      c.view.fitAll();
      return ok();
    };
    h["view.snapToNearestOrtho"] = [](Ctx& c) {
      c.view.snapToNearestOrtho();
      return ok();
    };
    h["view.rotate"] = [](Ctx& c) {
      c.view.rotateView(has(c.args, "yaw") ? float(num(c.args, "yaw")) : 0, has(c.args, "pitch") ? float(num(c.args, "pitch")) : 0);
      return ok();
    };
    h["view.setOrientation"] = [](Ctx& c) {
      if (has(c.args, "face")) {
        // ViewCube faces.
        static const std::map<std::string, std::pair<float, float>> faces = {
            {"top", {-vp::kPi / 2, 1.55f}},  {"bottom", {-vp::kPi / 2, -1.55f}}, {"front", {-vp::kPi / 2, 0}},
            {"back", {vp::kPi / 2, 0}},      {"right", {0, 0}},                  {"left", {vp::kPi, 0}}};
        auto it = faces.find(str(c.args, "face"));
        if (it == faces.end()) throw CommandError("face must be top, bottom, front, back, right or left");
        c.view.setOrientation(it->second.first, it->second.second);
      } else {
        c.view.setOrientation(float(num(c.args, "yaw")), float(num(c.args, "pitch")));
      }
      return ok();
    };
    h["view.orbit"] = [](Ctx& c) {
      c.view.orbit(float(num(c.args, "dx")), float(num(c.args, "dy")));
      return ok();
    };
    h["view.setDisplayMode"] = [](Ctx& c) {
      auto m = displayMode(str(c.args, "mode"));
      if (!m) throw CommandError("mode must be shaded, wireframe, xray, visualized, zebra or curvature");
      c.view.setDisplayMode(*m);
      return ok();
    };
    h["view.cycleDisplayMode"] = [](Ctx& c) {
      const auto& all = displayModes();
      size_t i = 0;
      while (i < all.size() && all[i].mode != c.view.displayMode) ++i;
      c.view.setDisplayMode(all[(i + 1) % all.size()].mode);
      return ok();
    };
    h["view.set"] = [](Ctx& c) {
      bool any = false;
      auto setB = [&](const char* k, bool& f) {
        if (has(c.args, k)) f = boolean(c.args, k), any = true;
      };
      ViewportController& v = c.view;
      setB("showEdges", v.showEdges);
      setB("showHiddenEdges", v.showHiddenEdges);
      setB("showSketches", v.showSketches);
      setB("showDecals", v.showDecals);
      setB("showPinnedMeasurements", v.showPinnedMeasurements);
      setB("showWorldAxes", v.showWorldAxes);
      setB("gridAtModelBottom", v.gridAtModelBottom);
      setB("showConstraints", v.showConstraints);
      setB("measureDeltas", v.measureDeltas);
      if (!any) throw CommandError("Give at least one view setting");
      v.stateChanged();
      return ok();
    };
    h["view.setFixedGrid"] = [](Ctx& c) {
      if (!has(c.args, "value")) {
        c.view.fixedGrid.reset();
      } else {
        double v = num(c.args, "value");
        if (!(v > 0)) throw CommandError("value must be a positive grid size in mm, or null for automatic");
        c.view.fixedGrid = v;
      }
      c.view.stateChanged();
      return ok();
    };
    h["view.setGridLock"] = [](Ctx& c) {
      bool on = boolean(c.args, "on");
      c.view.fixedGrid = on ? std::optional<double>(c.view.gridStep()) : std::nullopt;
      c.view.stateChanged();
      return ok();
    };
    h["view.section"] = [](Ctx& c) {
      bool any = false;
      if (has(c.args, "enabled")) c.view.setSectionEnabled(boolean(c.args, "enabled")), any = true;
      if (has(c.args, "axis")) {
        int a = integer(c.args, "axis");
        if (a < 0 || a > 2) throw CommandError("axis must be 0 (X), 1 (Y) or 2 (Z)");
        c.view.setSectionAxis(a), any = true;
      }
      if (has(c.args, "offset")) c.view.setSectionOffset(num(c.args, "offset")), any = true;
      if (has(c.args, "flipped")) c.view.setSectionFlipped(boolean(c.args, "flipped")), any = true;
      if (!any) throw CommandError("Give enabled, axis, offset or flipped");
      return ok();
    };
    h["view.toggleSection"] = [](Ctx& c) {
      c.view.setSectionEnabled(!c.view.sectionEnabled);
      return ok();
    };
    h["view.setMeasuring"] = [](Ctx& c) {
      c.view.setMeasuring(boolean(c.args, "on"));
      return ok();
    };
    h["view.toggleMeasuring"] = [](Ctx& c) {
      c.view.setMeasuring(!c.view.measuring);
      return ok();
    };
    h["view.setVisualizing"] = [](Ctx& c) {
      bool on = boolean(c.args, "on");
      if (on && c.store.sketch()) throw CommandError("Finish the sketch first");
      c.view.setVisualizing(on);
      return ok();
    };

    // MARK: Measure and point picks
    h["measure.pin"] = [](Ctx& c) {
      std::string text = has(c.args, "text") ? str(c.args, "text") : str(c.args, "label") + ": " + str(c.args, "value");
      c.view.pinnedMeasurements.push_back(text);
      c.view.stateChanged();
      return ok();
    };
    h["measure.unpin"] = [](Ctx& c) {
      int i = integer(c.args, "index");
      if (i < 0 || size_t(i) >= c.view.pinnedMeasurements.size()) throw CommandError("No pinned measurement " + std::to_string(i));
      c.view.pinnedMeasurements.erase(c.view.pinnedMeasurements.begin() + i);
      c.view.stateChanged();
      return ok();
    };
    h["measure.removePoint"] = [](Ctx& c) {
      Uuid id = uuid(c.args, "id");
      auto& ms = c.view.pointMeasurements;
      auto it = std::find_if(ms.begin(), ms.end(), [&](auto& m) { return m.id == id; });
      if (it == ms.end()) throw CommandError("No measurement " + id.string());
      ms.erase(it);
      c.view.stateChanged();
      return ok();
    };
    h["pointPick.begin"] = [](Ctx& c) {
      requireIdle(c);
      auto p = purposeNamed(str(c.args, "purpose"));
      if (!p) throw CommandError("Unknown point pick purpose \"" + str(c.args, "purpose") + "\"");
      c.view.beginPointPick(*p);
      return ok();
    };

    // MARK: Export and drawings
    h["export.write"] = [](Ctx& c) {
      auto kind = exportKind(str(c.args, "kind"));
      if (!kind) throw CommandError("Unknown export format \"" + str(c.args, "kind") + "\"");
      if (!isAvailable(*kind)) throw CommandError(title(*kind) + " export isn't available: " + detail(*kind));
      ToastWatch w(c.store);
      auto path = c.store.write(*kind, str(c.args, "folder"), has(c.args, "name") ? str(c.args, "name") : "");
      if (!path) throw CommandError(c.store.toast() && c.store.toast() != w.before ? *c.store.toast() : "Nothing to export");
      return ok({{"path", *path}});
    };
    h["export.quick"] = [](Ctx& c) {
      const json& f = need(c.args, "formats");
      if (!f.is_array()) throw CommandError("formats must be an array of export kinds");
      std::vector<ExportKind> kinds;
      for (auto& v : f) {
        if (!v.is_string()) throw CommandError("formats must be an array of export kinds");
        auto k = exportKind(v.get<std::string>());
        if (!k) throw CommandError("Unknown export format \"" + v.get<std::string>() + "\"");
        kinds.push_back(*k);
      }
      ToastWatch w(c.store);
      auto paths = c.store.quickExport(kinds, str(c.args, "folder"));
      return w.result({{"paths", paths}});
    };
    h["export.webPage"] = [](Ctx& c) {
      std::string t = has(c.args, "title") ? str(c.args, "title") : c.store.documentName();
      auto html = c.store.webPage(t);
      if (!html) throw CommandError(c.store.toast().value_or("There is nothing to show"));
      return ok({{"html", *html}, {"defaultName", c.store.documentName() + " " + abbreviatedDate()}});
    };
    h["drawing.sheet"] = [](Ctx& c) {
      std::string err;
      auto settings = parseDrawingSettings(has(c.args, "drawing") ? c.args["drawing"] : json::object(), err,
                                           c.store.documentName());
      if (!settings) throw CommandError(err);
      auto sheet = c.store.drawingSheet(settings);
      if (!sheet) throw CommandError(c.store.visibleMeshes().empty() ? "Add a body to the model to draw it." : "Couldn't project the views");
      json papers = json::array(), views = json::array(), scales = json::array();
      for (auto p : allPaperSizes()) papers.push_back({{"id", rawValue(p)}, {"title", title(p)}, {"size", vec(paperSize(p))}});
      for (auto k : DrawingProjection::allKinds())
        views.push_back({{"id", DrawingProjection::rawValue(k)}, {"title", DrawingProjection::title(k)}});
      for (double s : {10.0, 5.0, 2.0, 1.0, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01})
        scales.push_back({{"value", s}, {"title", DrawingSheet::scaleText(s)}});
      return ok({{"sheet", drawingSheetJson(*sheet)}, {"papers", papers}, {"views", views}, {"scales", scales},
                 {"title", settings->title}});
    };

    // MARK: Ui request acknowledgement
    h["ui.clearRequest"] = [](Ctx& c) {
      c.view.uiRequest.clear();
      c.view.stateChanged();
      return ok();
    };
    return h;
  }();
  return table;
}

}  // namespace

std::optional<DrawingSettings> parseDrawingSettings(const json& j, std::string& error, const std::string& defaultTitle) {
  DrawingSettings s;
  s.title = defaultTitle.empty() ? "Untitled" : defaultTitle;
  try {
    if (!j.is_object()) throw CommandError("drawing must be an object");
    if (has(j, "paper")) {
      std::string p = str(j, "paper");
      bool found = false;
      for (auto ps : allPaperSizes())
        if (rawValue(ps) == p) s.paper = ps, found = true;
      if (!found) throw CommandError("paper must be a4, a3, letter or tabloid");
    }
    if (has(j, "views")) {
      const json& v = j["views"];
      if (!v.is_array()) throw CommandError("views must be an array");
      s.views.clear();
      for (auto& k : v) {
        bool found = false;
        for (auto kind : DrawingProjection::allKinds())
          if (k.is_string() && DrawingProjection::rawValue(kind) == k.get<std::string>()) s.views.insert(kind), found = true;
        if (!found) throw CommandError("views are front, top, right or isometric");
      }
    }
    if (has(j, "showHidden")) s.showHidden = boolean(j, "showHidden");
    if (has(j, "showDimensions")) s.showDimensions = boolean(j, "showDimensions");
    if (has(j, "scale")) {
      double v = num(j, "scale");
      if (!(v > 0)) throw CommandError("scale must be positive (paper:model), or null to fit");
      s.scale = v;
    }
    if (has(j, "title")) s.title = str(j, "title");
  } catch (const CommandError& e) {
    error = e.what();
    return std::nullopt;
  }
  return s;
}

json runCommand(SculptDoc& doc, const json& command) {
  if (!command.is_object()) return {{"ok", false}, {"error", "A command is a JSON object {\"cmd\": …}"}};
  auto cmdIt = command.find("cmd");
  if (cmdIt == command.end() || !cmdIt->is_string()) return {{"ok", false}, {"error", "Missing \"cmd\""}};
  std::string name = cmdIt->get<std::string>();
  Ctx ctx{doc, command, *doc.store, *doc.view};
  try {
    auto& table = handlers();
    auto it = table.find(name);
    if (it != table.end()) return it->second(ctx);
    // Argument-free actions of the menus and bars.
    if (const Action* a = action(name)) {
      if (!a->run) throw CommandError(name + " needs arguments (see API.md)");
      if (!a->enabled(doc)) throw CommandError(a->title + " isn't available now");
      ToastWatch w(doc.store ? *doc.store : ctx.store);
      std::string ui = a->run(doc);
      return uiResult(ctx, ui, w);
    }
    return {{"ok", false}, {"error", "Unknown command \"" + name + "\""}};
  } catch (const CommandError& e) {
    return {{"ok", false}, {"error", e.what()}};
  } catch (const json::exception& e) {
    return {{"ok", false}, {"error", std::string("Bad arguments: ") + e.what()}};
  } catch (const std::exception& e) {
    return {{"ok", false}, {"error", e.what()}};
  }
}

}  // namespace sculpt::api
