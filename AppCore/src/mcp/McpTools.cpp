// Port of App/Sources/MCP/MCPTools.swift: the tools Sculpt3D offers over MCP.
// Each modeling tool appends one feature to the active document's history.
// Names, schemas, descriptions and result JSON match the Swift exactly.
#include "mcp/McpTools.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <utility>

#include "sculpt/model/Codec.hpp"
#include "sculpt/model/Expression.hpp"
#include "sculpt/model/Image.hpp"
#include "sculpt/model/ModelStore.hpp"
#include "sculpt/model/Terrain.hpp"

namespace fs = std::filesystem;

namespace sculpt::mcp {

// MARK: - JSON output

static void write(const json& v, std::string& out) {
  switch (v.type()) {
    case json::value_t::null: out += "null"; break;
    case json::value_t::boolean: out += v.get<bool>() ? "true" : "false"; break;
    case json::value_t::number_integer: out += std::to_string(v.get<int64_t>()); break;
    case json::value_t::number_unsigned: out += std::to_string(v.get<uint64_t>()); break;
    case json::value_t::number_float: {
      double d = v.get<double>();
      if (!std::isfinite(d)) { out += "null"; break; }
      char buf[40];
      std::snprintf(buf, sizeof buf, "%.17g", d);
      for (char* c = buf; *c; ++c)
        if (*c == ',') *c = '.';  // decimal comma locales
      out += buf;
      break;
    }
    case json::value_t::string:
      out += json(v.get_ref<const std::string&>()).dump(-1, ' ', false, json::error_handler_t::replace);
      break;
    case json::value_t::array: {
      out += '[';
      bool first = true;
      for (auto& e : v) {
        if (!first) out += ',';
        first = false;
        write(e, out);
      }
      out += ']';
      break;
    }
    case json::value_t::object: {
      out += '{';
      bool first = true;
      for (auto it = v.begin(); it != v.end(); ++it) {  // std::map: keys in byte order
        if (!first) out += ',';
        first = false;
        out += json(it.key()).dump(-1, ' ', false, json::error_handler_t::replace);
        out += ':';
        write(it.value(), out);
      }
      out += '}';
      break;
    }
    default: out += "null"; break;
  }
}

std::string serialize(const json& value) {
  std::string out;
  write(value, out);
  return out;
}

// MARK: - Registry

std::vector<DocumentRegistry::Entry> DocumentRegistry::open() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return entries_;
}

std::optional<DocumentRegistry::Entry> DocumentRegistry::active() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (entries_.empty()) return std::nullopt;
  return entries_.front();
}

void DocumentRegistry::add(Document* doc) {
  if (!doc) return;
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (auto& e : entries_)
    if (e.doc == doc) return activate(doc);
  entries_.insert(entries_.begin(), Entry{Uuid::random(), doc});
}

void DocumentRegistry::remove(Document* doc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.doc == doc; }),
                 entries_.end());
}

void DocumentRegistry::activate(Document* doc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.doc == doc; });
  if (it == entries_.end()) return;
  Entry e = *it;
  entries_.erase(it);
  entries_.insert(entries_.begin(), e);
}

bool DocumentRegistry::activate(const Uuid& id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.id == id; });
  if (it == entries_.end()) return false;
  Entry e = *it;
  entries_.erase(it);
  entries_.insert(entries_.begin(), e);
  return true;
}

void DocumentRegistry::setNewDocumentHandler(std::function<Document*(const std::string&)> h) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  newDocument_ = std::move(h);
}

std::function<Document*(const std::string&)> DocumentRegistry::newDocumentHandler() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return newDocument_;
}

// MARK: - Helpers

namespace {

double r(double x) { return std::round(x * 10000) / 10000; }
json v(const Vec3d& p) { return json::array({r(p.x), r(p.y), r(p.z)}); }
json v(const o3d::Vec3& p) { return v(fromO3d(p)); }

/// One text content block holding `value` as JSON (or a plain string).
json text(const json& value) {
  json block = json::object();
  block["type"] = "text";
  block["text"] = value.is_string() ? value.get<std::string>() : serialize(value);
  return json::array({block});
}

std::string joined(const std::vector<int>& xs) {
  std::string s;
  for (size_t i = 0; i < xs.size(); ++i) s += (i ? ", " : "") + std::to_string(xs[i]);
  return s;
}

std::string joined(const std::vector<std::string>& xs) {
  std::string s;
  for (size_t i = 0; i < xs.size(); ++i) s += (i ? ", " : "") + xs[i];
  return s;
}

std::string lower(std::string s) {
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string upper(std::string s) {
  for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

bool contains(const std::vector<int>& xs, int x) { return std::find(xs.begin(), xs.end(), x) != xs.end(); }

/// `(value as? NSNumber)?.doubleValue`: numbers and booleans.
std::optional<double> numberValue(const json& v) {
  if (v.is_boolean()) return v.get<bool>() ? 1.0 : 0.0;
  if (v.is_number()) return v.get<double>();
  return std::nullopt;
}

/// `(value as? NSNumber)?.intValue`.
std::optional<int> intValue(const json& v) {
  if (v.is_boolean()) return v.get<bool>() ? 1 : 0;
  if (v.is_number_integer() || v.is_number_unsigned()) {
    if (v.is_number_unsigned()) {
      uint64_t u = v.get<uint64_t>();
      return int(std::min<uint64_t>(u, uint64_t(std::numeric_limits<int>::max())));
    }
    int64_t i = v.get<int64_t>();
    return int(std::clamp<int64_t>(i, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
  }
  if (v.is_number_float()) {
    double d = v.get<double>();
    if (!std::isfinite(d)) return 0;
    return int(std::clamp(std::trunc(d), double(std::numeric_limits<int>::min()), double(std::numeric_limits<int>::max())));
  }
  return std::nullopt;
}

std::optional<bool> boolValue(const json& v) {
  if (auto d = numberValue(v)) return *d != 0;
  return std::nullopt;
}

/// Typed reads of a tool's JSON arguments.
class Args {
 public:
  explicit Args(const json& raw) : raw_(raw.is_object() ? raw : json::object()) {}

  const json& raw() const { return raw_; }
  const json* get(const std::string& k) const {
    auto it = raw_.find(k);
    return it == raw_.end() ? nullptr : &*it;
  }
  bool has(const std::string& k) const { return get(k) != nullptr; }
  /// `raw[k] as? String`.
  std::optional<std::string> str(const std::string& k) const {
    auto p = get(k);
    if (p && p->is_string()) return p->get<std::string>();
    return std::nullopt;
  }

  std::optional<double> dbl(const std::string& k) const {
    auto p = get(k);
    return p ? numberValue(*p) : std::nullopt;
  }
  std::optional<int> integer(const std::string& k) const {
    auto p = get(k);
    return p ? intValue(*p) : std::nullopt;
  }
  std::optional<bool> boolean(const std::string& k) const {
    auto p = get(k);
    return p ? boolValue(*p) : std::nullopt;
  }
  std::optional<std::vector<int>> ints(const std::string& k) const {
    auto p = get(k);
    if (!p || !p->is_array()) return std::nullopt;
    std::vector<int> out;
    for (auto& e : *p)
      if (auto i = intValue(e)) out.push_back(*i);
    return out;
  }
  std::optional<Args> object(const std::string& k) const {
    auto p = get(k);
    if (!p || !p->is_object()) return std::nullopt;
    return Args(*p);
  }

  double requireDouble(const std::string& k) const {
    if (auto d = dbl(k)) return *d;
    throw ToolError(k + " is required (a number)");
  }
  int requireInt(const std::string& k) const {
    if (auto i = integer(k)) return *i;
    throw ToolError(k + " is required (an integer)");
  }
  std::string string(const std::string& k) const {
    if (auto s = str(k)) return *s;
    throw ToolError(k + " is required (a string)");
  }
  std::optional<Vec3d> vec3(const std::string& k) const {
    auto p = get(k);
    if (!p) return std::nullopt;
    if (!p->is_array() || p->size() != 3) throw ToolError(k + " must be [x, y, z]");
    double n[3];
    for (int i = 0; i < 3; ++i) {
      auto d = numberValue((*p)[size_t(i)]);
      if (!d) throw ToolError(k + " must be [x, y, z]");
      n[i] = *d;
    }
    return Vec3d{n[0], n[1], n[2]};
  }

 private:
  json raw_;
};

// MARK: JSON Schema builders

using Props = std::vector<std::pair<std::string, json>>;

namespace S {
json described(json o, const char* d) {
  if (d) o["description"] = d;
  return o;
}
json typed(const char* t, const char* d) {
  json o = json::object();
  o["type"] = t;
  return described(o, d);
}
json number(const char* d = nullptr) { return typed("number", d); }
json integer(const char* d = nullptr) { return typed("integer", d); }
json boolean(const char* d = nullptr) { return typed("boolean", d); }
json string(const char* d = nullptr, const std::vector<std::string>& options = {}) {
  json o = typed("string", d);
  if (!options.empty()) o["enum"] = options;
  return o;
}
json vec3(const char* d) {
  json o = typed("array", d);
  o["items"] = typed("number", nullptr);
  o["minItems"] = 3;
  o["maxItems"] = 3;
  return o;
}
json intArray(const char* d) {
  json o = typed("array", d);
  o["items"] = typed("integer", nullptr);
  return o;
}
json props(const Props& p) {
  json o = json::object();
  for (auto& [k, val] : p) o[k] = val;
  return o;
}
json objectJ(const json& properties, const std::vector<std::string>& required = {}) {
  json o = json::object();
  o["type"] = "object";
  o["properties"] = properties;
  if (!required.empty()) o["required"] = required;
  return o;
}
json object(const Props& p, const std::vector<std::string>& required = {}) { return objectJ(props(p), required); }
json merged(json base, const Props& extra) {
  for (auto& [k, val] : extra) base[k] = val;
  return base;
}
json edges(const Props& extra, const std::vector<std::string>& required) {
  json base = props({{"body", integer("Body id")},
                     {"edges", intArray("Edge indices")},
                     {"all", boolean("Use every edge of the body")}});
  return objectJ(merged(base, extra), required);
}
/// A free-form property: {"description": d} plus extra keys.
json raw(const Props& p) { return props(p); }
}  // namespace S

std::vector<std::string> primitiveNames() {
  std::vector<std::string> out;
  for (auto k : allPrimitiveKinds()) out.push_back(rawValue(k));
  return out;
}

std::vector<std::string> booleanNames() {
  std::vector<std::string> out;
  for (auto k : allBooleanKinds()) out.push_back(rawValue(k));
  return out;
}

const std::vector<std::string>& displayModes() {
  static const std::vector<std::string> modes = {"shaded", "wireframe", "xray", "visualized", "zebra", "curvature"};
  return modes;
}

const std::vector<std::string>& exportFormats() {
  static const std::vector<std::string> formats = [] {
    std::vector<std::string> f;
    for (auto e : allExchangeFormats()) f.push_back(rawValue(e));
    for (const char* s : {"3mf", "glb", "obj", "usdz", "dxf", "dxf_drawing", "svg", "pdf", "sculpt3d"}) f.push_back(s);
    return f;
  }();
  return formats;
}

json planeSchema() {
  json o = json::object();
  o["description"] = "\"xy\", \"xz\", \"yz\", or {origin, normal, x_dir}";
  json named = S::typed("string", nullptr);
  named["enum"] = json::array({"xy", "xz", "yz"});
  o["oneOf"] = json::array({named, S::object({{"origin", S::vec3("Plane origin")},
                                              {"normal", S::vec3("Plane normal")},
                                              {"x_dir", S::vec3("Sketch u direction")}},
                                             {"normal"})});
  return o;
}

json cameraProperties() {
  return S::props({
      {"view", S::string("Camera view", {"iso", "top", "front", "right"})},
      {"fit", S::boolean("Zoom to fit all bodies (default true when view is given)")},
      {"yaw", S::number("Degrees around Z; 0 looks from +X")},
      {"pitch", S::number("Degrees above the ground plane")},
      {"zoom", S::number("Distance multiplier; 0.8 is 20% closer")},
      {"display_mode",
       S::string("Viewport display mode; visualized renders with materials, cast shadows and ambient occlusion and hides CAD edges",
                 displayModes())},
      {"projection", S::string("Camera projection", {"perspective", "orthographic"})},
  });
}

json entitySchema() {
  return S::object({{"body", S::integer("Body id")}, {"face", S::integer("Face index")}, {"edge", S::integer("Edge index")}},
                   {"body"});
}

json sketchProperties() {
  json pointItems = S::typed("array", nullptr);
  pointItems["items"] = S::typed("number", nullptr);
  pointItems["minItems"] = 2;
  pointItems["maxItems"] = 2;
  json points = S::typed("array", nullptr);
  points["items"] = pointItems;

  json items = json::object();
  items["type"] = "object";
  items["properties"] = S::props({
      {"type", S::string("Shape", {"rectangle", "rounded_rectangle", "circle", "polygon", "polyline"})},
      {"x", S::number()}, {"y", S::number()}, {"width", S::number()}, {"height", S::number()},
      {"centered", S::boolean()}, {"cx", S::number()}, {"cy", S::number()}, {"radius", S::number()},
      {"sides", S::integer()}, {"points", points},
  });
  items["required"] = json::array({"type"});

  json shapes = S::typed(
      "array",
      "Closed shapes in plane coordinates (mm): {type: rectangle, x, y, width, height, centered?} (x, y is the "
      "lower-left corner, or the centre when centered is true); {type: rounded_rectangle, same as rectangle plus "
      "radius} for rounded corners; {type: circle, cx, cy, radius}; "
      "{type: polygon, cx, cy, radius, sides}; {type: polyline, points: [[u, v], ...]} (closed automatically).");
  shapes["items"] = items;

  return S::props({
      {"plane", planeSchema()},
      {"face", S::object({{"body", S::integer("Body id")}, {"face", S::integer("Planar face index")}}, {"body", "face"})},
      {"offset", S::number("Moves the sketch plane along its normal, mm")},
      {"shapes", shapes},
  });
}

std::optional<Vec3f> color(const std::string& hex) {
  size_t b = hex.find_first_not_of("# "), e = hex.find_last_not_of("# ");
  std::string h = b == std::string::npos ? "" : hex.substr(b, e - b + 1);
  if (h.size() != 6) return std::nullopt;
  for (char c : h)
    if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
  unsigned long v = std::strtoul(h.c_str(), nullptr, 16);
  return Vec3f{float((v >> 16) & 0xFF) / 255, float((v >> 8) & 0xFF) / 255, float(v & 0xFF) / 255};
}

std::string homeDirectory() {
#ifdef _WIN32
  if (const char* p = std::getenv("USERPROFILE")) return p;
#endif
  if (const char* p = std::getenv("HOME")) return p;
  return "";
}

/// NSString.expandingTildeInPath for "~" and "~/…".
std::string expandingTilde(const std::string& path) {
  if (path.empty() || path[0] != '~') return path;
  if (path.size() > 1 && path[1] != '/' && path[1] != '\\') return path;
  std::string home = homeDirectory();
  if (home.empty()) return path;
  return home + path.substr(1);
}

/// ~/Downloads, or the temporary folder where there is none (Android).
fs::path downloadsDirectory() {
  std::error_code ec;
  std::string home = homeDirectory();
  if (!home.empty()) {
    fs::path p = fs::path(home) / "Downloads";
    if (fs::is_directory(p, ec)) return p;
  }
  fs::path t = fs::temp_directory_path(ec);
  return ec ? fs::path(".") : t;
}

std::string pathString(const fs::path& p) { return p.u8string(); }

/// NSString.deletingPathExtension.
std::string deletingPathExtension(const std::string& name) {
  auto dot = name.find_last_of('.');
  if (dot == std::string::npos || dot == 0) return name;
  return name.substr(0, dot);
}

std::string trimmingSpaces(const std::string& s) {
  size_t b = s.find_first_not_of(" \t"), e = s.find_last_not_of(" \t");
  return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

}  // namespace

// MARK: - Toolbox

struct Toolbox::Impl {
  struct Tool {
    std::string name;
    std::string description;
    json schema;
    bool readOnly = false;
    std::function<json(const Args&)> run;
  };

  DocumentRegistry& registry;
  std::vector<Tool> tools;

  explicit Impl(DocumentRegistry& r) : registry(r) { tools = makeTools(); }

  // MARK: Documents

  Document& document(bool editing = true) {
    auto e = registry.active();
    if (!e || !e->doc) throw ToolError("No Sculpt3D document is open. Call new_document first.");
    ModelStore& store = e->doc->store();
    if (editing && (store.sketch().has_value() || store.draft().has_value()))
      throw ToolError("A sketch or tool is open in the app. Finish or cancel it there first.");
    return *e->doc;
  }

  std::string activeName() const {
    auto e = registry.active();
    return e && e->doc ? e->doc->name() : "";
  }

  json newDocument() {
    auto handler = registry.newDocumentHandler();
    if (!handler) throw ToolError("Creating documents is not available in this app");
    Document* doc = handler("");
    if (!doc) throw ToolError("The new document window did not open");
    registry.add(doc);  // no-op (activates) when the host registered it already
    registry.activate(doc);
    auto e = registry.active();
    if (!e) throw ToolError("The new document window did not open");
    json out = json::object();
    out["id"] = e->id.string();
    out["name"] = e->doc->name();
    return text(out);
  }

  // MARK: Applying operations

  /// Trial-runs the operations as drafts, so a failing one never enters the
  /// history, then commits them. Returns the new bodies and the model.
  json apply(ModelStore& store, const std::vector<Operation>& ops) {
    auto beforeList = store.bodyIds();
    std::set<int> before(beforeList.begin(), beforeList.end());
    std::vector<Feature> added;
    for (auto& op : ops) {
      Feature f(op);
      store.setDraft(sculpt::Draft{f, std::nullopt, std::nullopt});
      store.regenerate();
      std::optional<std::string> error;
      auto it = store.errors().find(f.id);
      if (it != store.errors().end()) error = it->second;
      store.setDraft(std::nullopt);
      if (error) {
        for (size_t i = 0; i < added.size(); ++i) store.undo();
        store.regenerate();
        throw ToolError(*error);
      }
      added.push_back(f);
      auto list = store.features();
      list.push_back(f);
      store.commit(std::move(list));
    }
    store.setSelection({});
    json features = json::array(), newBodies = json::array(), bodies = json::array();
    for (auto& f : added) {
      json d = json::object();
      d["id"] = f.id.string();
      d["title"] = f.title();
      features.push_back(d);
    }
    for (int b : store.bodyIds()) {
      if (!before.count(b)) newBodies.push_back(b);
      bodies.push_back(bodySummary(store, b));
    }
    json out = json::object();
    out["features"] = features;
    out["new_bodies"] = newBodies;
    out["bodies"] = bodies;
    return text(out);
  }

  json apply(ModelStore& store, Operation op) { return apply(store, std::vector<Operation>{std::move(op)}); }

  json updateFeature(const Args& a, ModelStore& store) {
    Feature f = feature(a, store);
    const json* values = a.get("values");
    if (!values || !values->is_object() || values->empty()) throw ToolError("values is empty");
    auto params = f.op.parameters();
    for (auto it = values->begin(); it != values->end(); ++it) {
      const std::string& key = it.key();
      auto p = std::find_if(params.begin(), params.end(), [&](const Operation::Parameter& p) {
        return std::to_string(p.id) == key || lower(p.label) == lower(key);
      });
      if (p == params.end()) {
        std::vector<std::string> names;
        for (auto& q : params) names.push_back(std::to_string(q.id) + " " + q.label);
        throw ToolError(f.title() + " has no parameter \"" + key + "\". Its parameters: " + joined(names));
      }
      const json& value = it.value();
      if (value.is_string()) {
        std::string expr = value.get<std::string>();
        auto v = ExpressionEvaluator(store.settings().variables).evaluate(expr);
        if (!v) throw ToolError("Could not evaluate \"" + expr + "\"");
        f.op = f.op.setting(p->id, *v);
        if (!f.bindings) f.bindings = std::map<int, std::string>();
        (*f.bindings)[p->id] = expr;
      } else if (auto v = numberValue(value)) {
        f.op = f.op.setting(p->id, *v);
        if (f.bindings) f.bindings->erase(p->id);
      } else {
        throw ToolError("Value for " + key + " must be a number or an expression string");
      }
    }
    store.setDraft(sculpt::Draft{f, f.id, std::nullopt});
    store.regenerate();
    std::optional<std::string> error;
    auto it = store.errors().find(f.id);
    if (it != store.errors().end()) error = it->second;
    store.setDraft(std::nullopt);
    if (error) {
      store.regenerate();
      throw ToolError(*error);
    }
    auto list = store.features();
    for (auto& g : list)
      if (g.id == f.id) g = f;
    store.commit(std::move(list));
    return text(modelSummary(store));
  }

  // MARK: Arguments

  int body(const Args& a, const std::string& key, ModelStore& store) {
    int b = a.requireInt(key);
    auto ids = store.bodyIds();
    if (!contains(ids, b)) throw ToolError("No body " + std::to_string(b) + ". Bodies: " + joined(ids));
    return b;
  }

  Feature feature(const Args& a, ModelStore& store) {
    std::string id = a.string("feature_id");
    for (auto& f : store.features())
      if (f.id.string() == upper(id)) return f;
    throw ToolError("No feature with id " + id);
  }

  std::vector<int> edges(const Args& a, int body, ModelStore& store) {
    if (a.boolean("all") == std::optional<bool>(true)) {
      if (auto p = store.properties(body)) {
        std::vector<int> all(size_t(std::max(p->edgeCount, 0)));
        for (int i = 0; i < p->edgeCount; ++i) all[size_t(i)] = i;
        return all;
      }
    }
    auto e = a.ints("edges");
    if (!e || e->empty()) throw ToolError("Give edge indices or all: true");
    return *e;
  }

  std::pair<CombineMode, std::optional<int>> combine(const Args& a, ModelStore& store) {
    std::string raw = a.str("mode").value_or("new_body");
    CombineMode mode;
    if (raw == "join") mode = CombineMode::join;
    else if (raw == "cut") mode = CombineMode::cut;
    else if (raw == "new_body") return {CombineMode::newBody, std::nullopt};
    else throw ToolError("mode must be new_body, join or cut");
    if (a.has("target_body")) return {mode, body(a, "target_body", store)};
    auto ids = store.bodyIds();
    if (ids.size() != 1) throw ToolError(std::string(mode == CombineMode::cut ? "cut" : "join") + " needs target_body");
    return {mode, ids[0]};
  }

  SketchPlane plane(const Args& a, ModelStore& store) {
    SketchPlane p;
    if (auto face = a.object("face")) {
      int b = body(*face, "body", store), i = face->requireInt("face");
      auto info = store.faceInfo(b, i);
      if (!info) throw ToolError("Body " + std::to_string(b) + " has no face " + std::to_string(i));
      if (!info->planar)
        throw ToolError("Face " + std::to_string(i) + " of body " + std::to_string(b) + " is not planar");
      p = SketchPlane{fromO3d(info->centroid), fromO3d(info->normal), fromO3d(info->xDir)};
    } else if (auto name = a.str("plane")) {
      std::string n = lower(*name);
      if (n == "xy") p = SketchPlane::xy();
      else if (n == "xz") p = SketchPlane::xz();
      else if (n == "yz") p = SketchPlane::yz();
      else throw ToolError("plane must be xy, xz, yz or {origin, normal, x_dir}");
    } else if (auto o = a.object("plane")) {
      auto n = o->vec3("normal");
      if (!n || !(length(*n) > 1e-9)) throw ToolError("plane.normal must be non-zero");
      Vec3d normal = normalize(*n);
      Vec3d x = o->vec3("x_dir").value_or(std::abs(normal.z) > 0.9 ? Vec3d{1, 0, 0} : Vec3d{0, 0, 1});
      x -= normal * dot(x, normal);
      if (!(length(x) > 1e-9)) throw ToolError("plane.x_dir must not be parallel to the normal");
      p = SketchPlane{o->vec3("origin").value_or(Vec3d{}), normal, normalize(x)};
    } else {
      p = SketchPlane::xy();
    }
    if (auto offset = a.dbl("offset")) p.origin += p.normal * *offset;
    return p;
  }

  Sketch sketch(const Args& a, ModelStore& store) {
    Sketch s(plane(a, store));
    const json* shapes = a.get("shapes");
    bool ok = shapes && shapes->is_array() && !shapes->empty() &&
              std::all_of(shapes->begin(), shapes->end(), [](const json& e) { return e.is_object(); });
    if (!ok) throw ToolError("shapes is empty");
    for (size_t i = 0; i < shapes->size(); ++i) {
      const json& raw = (*shapes)[i];
      Args sh(raw);
      std::string type = sh.string("type");
      std::string tag = "shape " + std::to_string(i) + ": ";
      if (type == "rectangle") {
        double w = sh.requireDouble("width"), h = sh.requireDouble("height");
        Vec2d p0{sh.dbl("x").value_or(0), sh.dbl("y").value_or(0)};
        if (sh.boolean("centered").value_or(false)) p0 -= Vec2d{w, h} / 2.0;
        s.addRectangle(p0, p0 + Vec2d{w, h}, std::nullopt, std::nullopt);
      } else if (type == "rounded_rectangle") {
        double w = sh.requireDouble("width"), h = sh.requireDouble("height");
        Vec2d p0{sh.dbl("x").value_or(0), sh.dbl("y").value_or(0)};
        if (sh.boolean("centered").value_or(false)) p0 -= Vec2d{w, h} / 2.0;
        double rad = std::min(std::max(sh.dbl("radius").value_or(0), 0.0), std::min(w, h) / 2 - 1e-6);
        if (!(rad > 1e-6)) {
          s.addRectangle(p0, p0 + Vec2d{w, h}, std::nullopt, std::nullopt);
          continue;
        }
        double x0 = p0.x, y0 = p0.y, x1 = p0.x + w, y1 = p0.y + h;
        // Straight runs between the corner arcs; arcs run counter-clockwise.
        int b0 = s.addPoint({x0 + rad, y0}), b1 = s.addPoint({x1 - rad, y0});
        int r0 = s.addPoint({x1, y0 + rad}), r1 = s.addPoint({x1, y1 - rad});
        int t0 = s.addPoint({x1 - rad, y1}), t1 = s.addPoint({x0 + rad, y1});
        int l0 = s.addPoint({x0, y1 - rad}), l1 = s.addPoint({x0, y0 + rad});
        for (auto [p, q] : {std::pair<int, int>{b0, b1}, {r0, r1}, {t0, t1}, {l0, l1}})
          if (p != q) s.addLine(p, q);
        s.addArc(s.addPoint({x1 - rad, y0 + rad}), b1, r0);
        s.addArc(s.addPoint({x1 - rad, y1 - rad}), r1, t0);
        s.addArc(s.addPoint({x0 + rad, y1 - rad}), t1, l0);
        s.addArc(s.addPoint({x0 + rad, y0 + rad}), l1, b0);
      } else if (type == "circle") {
        int c = s.addPoint({sh.dbl("cx").value_or(0), sh.dbl("cy").value_or(0)});
        s.addCircle(c, sh.requireDouble("radius"));
      } else if (type == "polygon") {
        Vec2d c{sh.dbl("cx").value_or(0), sh.dbl("cy").value_or(0)};
        s.addPolygon(c, c + Vec2d{sh.requireDouble("radius"), 0}, sh.integer("sides").value_or(6));
      } else if (type == "polyline") {
        const json* pts = sh.get("points");
        bool arrays = pts && pts->is_array() &&
                      std::all_of(pts->begin(), pts->end(), [](const json& e) { return e.is_array(); });
        if (!arrays || pts->size() < 3) throw ToolError(tag + "polyline needs 3+ points");
        std::vector<int> ids;
        for (auto& p : *pts) {
          std::optional<double> x, y;
          if (p.size() == 2) x = numberValue(p[0]), y = numberValue(p[1]);
          if (!x || !y) throw ToolError(tag + "points are [u, v] pairs");
          ids.push_back(s.addPoint({*x, *y}));
        }
        for (size_t k = 0; k < ids.size(); ++k) s.addLine(ids[k], ids[(k + 1) % ids.size()]);
      } else {
        throw ToolError(tag + "type must be rectangle, rounded_rectangle, circle, polygon or polyline");
      }
    }
    if (s.profileCount() <= 0) throw ToolError("The shapes make no closed profile");
    return s;
  }

  void setView(const Args& a, Document& doc) {
    ViewRequest req;
    auto view = a.str("view");
    if (view && (*view == "iso" || *view == "top" || *view == "front" || *view == "right")) req.preset = *view;
    req.fit = a.boolean("fit").value_or(req.preset.has_value());
    if (auto yaw = a.dbl("yaw")) req.yawRadians = double(float(*yaw * kPi / 180));
    if (auto pitch = a.dbl("pitch")) req.pitchRadians = double(float(std::min(std::max(*pitch, -89.0), 89.0) * kPi / 180));
    if (auto zoom = a.dbl("zoom"); zoom && *zoom > 0) req.zoom = *zoom;
    if (auto mode = a.str("display_mode"))
      if (std::find(displayModes().begin(), displayModes().end(), *mode) != displayModes().end()) req.displayMode = *mode;
    if (auto projection = a.str("projection")) {
      bool ortho = *projection == "orthographic";
      doc.store().updateSettings([&](ProjectSettings& s) { s.camera.orthographic = ortho; });
    }
    doc.setView(req);
  }

  // MARK: Reports

  json modelSummary(ModelStore& store) {
    json features = json::array();
    const auto& list = store.features();
    for (size_t i = 0; i < list.size(); ++i) {
      const Feature& f = list[i];
      json d = json::object();
      d["index"] = int(i);
      d["id"] = f.id.string();
      d["title"] = f.title();
      if (f.suppressed) d["suppressed"] = true;
      auto e = store.errors().find(f.id);
      if (e != store.errors().end()) d["error"] = e->second;
      auto params = f.op.parameters();
      if (!params.empty()) {
        json ps = json::array();
        for (auto& p : params) {
          json pd = json::object();
          pd["id"] = p.id;
          pd["label"] = p.label;
          pd["value"] = r(p.value);
          pd["unit"] = p.unit;
          if (f.bindings) {
            auto b = f.bindings->find(p.id);
            if (b != f.bindings->end()) pd["expression"] = b->second;
          }
          ps.push_back(pd);
        }
        d["parameters"] = ps;
      }
      features.push_back(d);
    }
    json bodies = json::array();
    for (int b : store.bodyIds()) bodies.push_back(bodySummary(store, b));
    json variables = json::array();
    for (auto& var : store.settings().variables) {
      json vd = json::object();
      vd["name"] = var.name;
      vd["expression"] = var.expression;
      variables.push_back(vd);
    }
    json out = json::object();
    out["document"] = activeName();
    out["features"] = features;
    out["bodies"] = bodies;
    out["variables"] = variables;
    out["units"] = "mm";
    return out;
  }

  json bodySummary(ModelStore& store, int id) {
    json out = json::object();
    out["id"] = id;
    auto p = store.properties(id);
    if (!p) return out;
    out["volume"] = r(p->volume);
    out["area"] = r(p->area);
    out["bbox_min"] = v(p->bboxMin);
    out["bbox_max"] = v(p->bboxMax);
    out["size"] = v(fromO3d(p->bboxMax) - fromO3d(p->bboxMin));
    out["faces"] = p->faceCount;
    out["edges"] = p->edgeCount;
    return out;
  }

  json topology(ModelStore& store, int body) {
    auto p = store.properties(body);
    if (!p) throw ToolError("No body " + std::to_string(body));
    const int limit = 400;
    json faces = json::array();
    for (int i = 0; i < std::min(p->faceCount, limit); ++i) {
      auto f = store.faceInfo(body, i);
      if (!f) continue;
      json d = json::object();
      d["index"] = i;
      d["area"] = r(f->area);
      if (f->planar) {
        d["type"] = "plane";
        d["centroid"] = v(f->centroid);
        d["normal"] = v(f->normal);
      } else if (f->cylindrical) {
        d["type"] = "cylinder";
        d["radius"] = r(f->radius);
        d["axis_origin"] = v(f->axisOrigin);
        d["axis"] = v(f->axisDir);
      } else {
        d["type"] = f->radius > 0 ? "revolved" : "curved";
        if (f->radius > 0) {
          d["radius"] = r(f->radius);
          d["axis_origin"] = v(f->axisOrigin);
          d["axis"] = v(f->axisDir);
        }
      }
      faces.push_back(d);
    }
    json edgeList = json::array();
    for (int i = 0; i < std::min(p->edgeCount, limit); ++i) {
      o3d::EdgeInfo e;
      if (!store.kernel().edgeInfo(body, i, e)) continue;
      json d = json::object();
      d["index"] = i;
      d["length"] = r(e.length);
      d["start"] = v(e.start);
      d["end"] = v(e.end);
      d["type"] = e.linear ? "line" : e.circular ? "circle" : "curve";
      if (e.circular) {
        d["center"] = v(e.center);
        d["radius"] = r(e.radius);
      }
      edgeList.push_back(d);
    }
    json out = json::object();
    out["body"] = body;
    out["faces"] = faces;
    out["edges"] = edgeList;
    if (p->faceCount > limit || p->edgeCount > limit)
      out["truncated"] = "Only the first " + std::to_string(limit) + " faces and edges are listed";
    return out;
  }

  json measure(const Args& a, ModelStore& store) {
    struct Entity {
      int body, kind, index;
    };
    auto entity = [&](const std::string& key) -> std::optional<Entity> {
      auto e = a.object(key);
      if (!e) return std::nullopt;
      int b = body(*e, "body", store);
      if (auto f = e->integer("face")) return Entity{b, 1, *f};
      if (auto ed = e->integer("edge")) return Entity{b, 2, *ed};
      return Entity{b, 0, 0};
    };
    auto ea = entity("a");
    if (!ea) throw ToolError("a is required");
    auto eb = entity("b");
    o3d::Ref ra{ea->body, static_cast<o3d::RefKind>(ea->kind), ea->index};
    o3d::Ref rb{eb ? eb->body : -1, static_cast<o3d::RefKind>(eb ? eb->kind : 0), eb ? eb->index : 0};
    o3d::Measurement m;
    if (!store.kernel().measure(ra, rb, m)) {
      const std::string& err = store.kernel().lastError();
      throw ToolError(err.empty() ? "Could not measure that" : err);
    }
    json out = json::object();
    if (m.hasDistance) {
      out["distance"] = r(m.distance);
      out["closest_points"] = json::array({v(m.p1), v(m.p2)});
    }
    if (m.hasAngle) out["angle_degrees"] = r(m.angleDeg);
    if (m.hasLength) out["length"] = r(m.length);
    if (m.hasArea) out["area"] = r(m.area);
    if (m.hasRadius) out["radius"] = r(m.radius);
    if (!eb && ea->kind == 0) {
      if (auto p = store.properties(ea->body)) {
        out["volume"] = r(p->volume);
        out["surface_area"] = r(p->area);
      }
    }
    return out;
  }

  static std::optional<std::string> writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
    if (f) f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    if (f) return std::nullopt;
    fs::path p = fs::u8path(path);
    // Foundation's NSFileWriteUnknownError text.
    return "The file “" + p.filename().u8string() + "” couldn’t be saved in the folder “" +
           p.parent_path().filename().u8string() + "”.";
  }

  json exportModel(const Args& a, ModelStore& store) {
    std::string requested = lower(a.string("format"));
    auto format = exchangeFormat(requested);
    if (!format || rawValue(*format) != requested) return exportOther(requested, a, store);
    auto ids = store.bodyIds();
    std::vector<int> bodies = a.ints("bodies").value_or(ids);
    for (int b : bodies)
      if (!contains(ids, b)) throw ToolError("No body " + std::to_string(b));
    if (bodies.empty()) throw ToolError("The model has no bodies to export");
    std::string path;
    if (auto given = a.str("path")) {
      path = expandingTilde(*given);
    } else {
      auto e = registry.active();
      std::string name = e && e->doc ? e->doc->name() : "Sculpt3D";
      path = pathString(downloadsDirectory() / fs::u8path(deletingPathExtension(name) + "." + rawValue(*format)));
    }
    if (!store.kernel().exportFile(path, bodies)) {
      const std::string& err = store.kernel().lastError();
      throw ToolError(err.empty() ? "Export failed" : err);
    }
    std::error_code ec;
    auto size = fs::file_size(fs::u8path(path), ec);
    json out = json::object();
    out["path"] = path;
    out["bytes"] = ec ? 0 : int64_t(size);
    out["bodies"] = bodies;
    return text(out);
  }

  /// Formats written by the app's own exporters rather than the kernel.
  json exportOther(const std::string& format, const Args& a, ModelStore& store) {
    static const std::vector<std::pair<std::string, ExportKind>> kinds = {
        {"3mf", ExportKind::threeMF}, {"glb", ExportKind::glb},       {"obj", ExportKind::obj},
        {"usdz", ExportKind::usdz},   {"dxf", ExportKind::dxfSketch}, {"dxf_drawing", ExportKind::dxfDrawing},
        {"svg", ExportKind::svg},     {"pdf", ExportKind::pdf},       {"sculpt3d", ExportKind::sculpt3d}};
    auto k = std::find_if(kinds.begin(), kinds.end(), [&](auto& e) { return e.first == format; });
    if (k == kinds.end()) throw ToolError("format must be one of " + joined(exportFormats()));
    ExportKind kind = k->second;

    struct RestoreSelection {
      ModelStore& store;
      std::vector<Pick> saved;
      ~RestoreSelection() { store.setSelection(saved); }
    } restore{store, store.selection()};

    if (auto bodies = a.ints("bodies")) {
      auto ids = store.bodyIds();
      std::vector<Pick> picks;
      for (int b : *bodies) {
        if (!contains(ids, b)) throw ToolError("No body " + std::to_string(b));
        picks.push_back(Pick::bodyPick(b));
      }
      store.setSelection(picks);
    } else {
      store.setSelection({});
    }
    store.setToast(std::nullopt);
    auto data = store.exportData(kind);
    if (!data) throw ToolError(store.toast().value_or("Export failed"));
    std::string path;
    if (auto given = a.str("path")) {
      path = expandingTilde(*given);
    } else {
      path = pathString(downloadsDirectory() / fs::u8path(store.documentName() + "." + fileExtension(kind)));
    }
    if (auto err = writeFile(path, *data)) throw ToolError(*err);
    json out = json::object();
    out["path"] = path;
    out["bytes"] = int64_t(data->size());
    out["format"] = title(kind);
    return text(out);
  }

  // MARK: Terrain

  json createTerrain(const Args& a) {
    ModelStore& store = document().store();
    TerrainSettings t;
    t.name = a.str("name").value_or("Terrain");
    if (auto c = a.vec3("center")) t.center = *c;
    t.width = a.dbl("width").value_or(200);
    t.depth = a.dbl("depth").value_or(200);
    if (!(t.width > 0) || !(t.depth > 0)) throw ToolError("width and depth must be positive");
    int n = std::min(std::max(a.integer("resolution").value_or(48), 8), 129);
    if (t.width >= t.depth) {
      t.nx = n;
      t.ny = std::max(8, int(std::round(double(n) * t.depth / t.width)));
    } else {
      t.ny = n;
      t.nx = std::max(8, int(std::round(double(n) * t.width / t.depth)));
    }
    if (auto f = a.dbl("base_thickness")) t.floor = std::max(*f, 0.5);
    t.heights.assign(size_t(t.nx * t.ny), 0.0);
    bool shaped = false;

    // `a.raw["heights"] as? [[Any]]`: every row must be an array.
    if (const json* rows = a.get("heights");
        rows && rows->is_array() && !rows->empty() &&
        std::all_of(rows->begin(), rows->end(), [](const json& e) { return e.is_array(); })) {
      std::vector<std::vector<double>> grid;
      for (auto& row : *rows) {
        std::vector<double> vals;
        for (auto& e : row)
          if (auto d = numberValue(e)) vals.push_back(*d);
        grid.push_back(std::move(vals));
      }
      size_t w = grid.front().size();
      bool equal = std::all_of(grid.begin(), grid.end(), [&](auto& r) { return r.size() == w; });
      if (!(w > 1) || !equal || !(grid.size() > 1)) throw ToolError("heights must be at least 2 × 2 rows of equal length");
      int gw = int(w), gh = int(grid.size());
      for (int j = 0; j < t.ny; ++j) {
        for (int i = 0; i < t.nx; ++i) {
          // Bilinear resample.
          double x = double(i) / double(t.nx - 1) * double(gw - 1), y = double(j) / double(t.ny - 1) * double(gh - 1);
          int x0 = std::min(int(x), gw - 2), y0 = std::min(int(y), gh - 2);
          double fx = x - x0, fy = y - y0;
          double top = grid[size_t(y0)][size_t(x0)] * (1 - fx) + grid[size_t(y0)][size_t(x0 + 1)] * fx;
          double bottom = grid[size_t(y0 + 1)][size_t(x0)] * (1 - fx) + grid[size_t(y0 + 1)][size_t(x0 + 1)] * fx;
          t.heights[size_t(j * t.nx + i)] += top * (1 - fy) + bottom * fy;
        }
      }
      shaped = true;
    }
    if (auto path = a.str("image_path")) {
      std::vector<uint8_t> bytes;
      {
        std::ifstream in(fs::u8path(*path), std::ios::binary);
        if (in) bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      }
      auto image = bytes.empty() ? std::nullopt : decodeImage(bytes);
      if (!image || image->width <= 0 || image->height <= 0) throw ToolError("Couldn't read the image at " + *path);
      auto field = Terrain::field(image->gray(), image->width, image->height, t.nx, t.ny);
      if (field.size() != t.heights.size()) throw ToolError("Couldn't read the image at " + *path);
      double relief = a.dbl("image_height").value_or(40);
      for (size_t k = 0; k < t.heights.size(); ++k) t.heights[k] += field[k] * relief;
      shaped = true;
    }
    if (auto nz = a.object("noise")) {
      TerrainNoise noise;
      if (auto v = nz->integer("seed")) noise.seed = *v;
      if (auto v = nz->dbl("scale")) noise.scale = *v;
      if (auto v = nz->integer("octaves")) noise.octaves = std::min(std::max(*v, 1), 8);
      if (auto v = nz->dbl("roughness")) noise.roughness = *v;
      double height = nz->dbl("height").value_or(40);
      auto field = Terrain::noiseField(t.nx, t.ny, t.depth / t.width, noise);
      for (size_t k = 0; k < t.heights.size() && k < field.size(); ++k) t.heights[k] += field[k] * height;
      if (!shaped) {
        t.noise = noise;
        t.noiseHeight = height;
      }
    }
    auto objects = [&](const char* key) -> const json* {
      const json* p = a.get(key);
      if (p && p->is_array() && !p->empty() &&
          std::all_of(p->begin(), p->end(), [](const json& e) { return e.is_object(); }))
        return p;
      return nullptr;
    };
    if (const json* hills = objects("hills")) {
      std::vector<Terrain::Hill> list;
      for (auto& h : *hills) {
        Args ha(h);
        auto x = ha.dbl("x"), y = ha.dbl("y"), rad = ha.dbl("radius"), z = ha.dbl("height");
        if (x && y && rad && z) list.push_back(Terrain::Hill{*x, *y, *rad, *z});
      }
      Terrain::add(list, t);
      t.noise.reset();
    }
    if (auto tr = a.object("terraces")) {
      if (auto count = tr->integer("count")) {
        Terrain::terrace(t, *count, tr->dbl("softness").value_or(0.3));
        t.noise.reset();
      }
    }
    if (const json* chans = objects("channels")) {
      std::vector<Terrain::Channel> list;
      for (auto& c : *chans) {
        Args ca(c);
        std::vector<Vec2d> pts;
        const json* raw = ca.get("points");
        if (raw && raw->is_array() && std::all_of(raw->begin(), raw->end(), [](const json& e) { return e.is_array(); })) {
          for (auto& p : *raw) {
            std::vector<double> vals;
            for (auto& e : p)
              if (auto d = numberValue(e)) vals.push_back(*d);
            if (vals.size() >= 2) pts.push_back({vals[0], vals[1]});
          }
        }
        if (pts.size() < 2) throw ToolError("each channel needs at least two points");
        list.push_back(Terrain::Channel{pts, ca.dbl("width").value_or(20), ca.dbl("bed").value_or(0),
                                        ca.dbl("bank").value_or(10)});
      }
      Terrain::carve(list, t);
      t.noise.reset();
    }
    return apply(store, op::Terrain{t});
  }

  // MARK: Tool list

  std::vector<Tool> makeTools();
};

std::vector<Toolbox::Impl::Tool> Toolbox::Impl::makeTools() {
  using namespace S;
  std::vector<Tool> t;
  auto add = [&](const char* name, std::string description, json schema, bool readOnly,
                 std::function<json(const Args&)> run) {
    t.push_back(Tool{name, std::move(description), std::move(schema), readOnly, std::move(run)});
  };
  const json noArgs = objectJ(json::object());

  add("list_documents", "Lists the open Sculpt3D documents. The first one is active; tools act on it.", noArgs, true,
      [this](const Args&) {
        json list = json::array();
        auto open = registry.open();
        for (size_t i = 0; i < open.size(); ++i) {
          json d = json::object();
          d["id"] = open[i].id.string();
          d["name"] = open[i].doc->name();
          d["active"] = i == 0;
          d["features"] = int(open[i].doc->store().features().size());
          list.push_back(d);
        }
        return text(list);
      });

  add("select_document", "Makes the document with this id (from list_documents) the active one.",
      object({{"id", string("Document id")}}, {"id"}), false, [this](const Args& a) {
        auto id = Uuid::parse(a.string("id"));
        if (!id || !registry.activate(*id)) throw ToolError("No open document has that id");
        return text("Active document: " + activeName());
      });

  add("new_document", "Opens a new, empty Sculpt3D document window and makes it active.", noArgs, false,
      [this](const Args&) { return newDocument(); });

  add("get_model",
      "The active document's parametric history (features with their editable parameters), its bodies with volume, "
      "area and bounding box, and the project variables.",
      noArgs, true, [this](const Args&) { return text(modelSummary(document(false).store())); });

  add("get_body_topology",
      "Faces and edges of one body, with the indices that fillet_edges, chamfer_edges, shell_body, measure and sketch "
      "planes on faces take.",
      object({{"body", integer("Body id")}}, {"body"}), true, [this](const Args& a) {
        ModelStore& store = document(false).store();
        return text(topology(store, body(a, "body", store)));
      });

  add("add_primitive",
      "Adds a solid primitive as a new body. Its base sits on `position` (default origin), centred in X and Y, rising "
      "along +Z. Sizes in mm: box width (X), depth (Y), height (Z); cylinder radius, height; sphere radius; cone "
      "radius (bottom), top_radius, height; torus major_radius, minor_radius.",
      object({{"kind", string("Primitive", primitiveNames())},
              {"width", number()},
              {"depth", number()},
              {"height", number()},
              {"radius", number()},
              {"top_radius", number()},
              {"major_radius", number()},
              {"minor_radius", number()},
              {"position", vec3("Base centre [x, y, z]")}},
             {"kind"}),
      false, [this](const Args& a) {
        ModelStore& store = document().store();
        auto kind = primitiveKind(a.string("kind"));
        if (!kind) throw ToolError("Unknown primitive kind");
        Vec3d d = defaultDims(*kind);
        switch (*kind) {
          case PrimitiveKind::box:
            d = {a.dbl("width").value_or(d.x), a.dbl("depth").value_or(d.y), a.dbl("height").value_or(d.z)};
            break;
          case PrimitiveKind::cylinder: d = {a.dbl("radius").value_or(d.x), a.dbl("height").value_or(d.y), 0}; break;
          case PrimitiveKind::sphere: d = {a.dbl("radius").value_or(d.x), 0, 0}; break;
          case PrimitiveKind::cone:
            d = {a.dbl("radius").value_or(d.x), a.dbl("top_radius").value_or(d.y), a.dbl("height").value_or(d.z)};
            break;
          case PrimitiveKind::torus:
            d = {a.dbl("major_radius").value_or(d.x), a.dbl("minor_radius").value_or(d.y), 0};
            break;
        }
        return apply(store, op::Primitive{*kind, d, a.vec3("position").value_or(Vec3d{})});
      });

  add("sketch_extrude",
      "Draws closed shapes on a sketch plane and extrudes them along the plane normal. Shape coordinates are 2D in the "
      "plane (u along its X direction, v along its Y). Planes: xy (normal +Z), xz (normal -Y), yz (normal +X); a "
      "custom {origin, normal, x_dir}; or a planar face of a body via `face`, whose sketch origin is the face "
      "centroid. For new_body and join, distance runs along the normal (negative goes the other way); for cut it is "
      "the depth into the material, against the normal, so a cut sketched on a top face with distance 5 removes 5 mm "
      "downward. Shapes inside other shapes become holes. mode: new_body, join or cut (join/cut need target_body "
      "unless there is only one body).",
      objectJ(merged(sketchProperties(),
                    {{"distance", number("Extrusion distance in mm (for cut: depth into the material)")},
                     {"mode", string("new_body, join or cut", {"new_body", "join", "cut"})},
                     {"target_body", integer("Body to join to or cut from")}}),
             {"shapes", "distance"}),
      false, [this](const Args& a) {
        ModelStore& store = document().store();
        Sketch s = sketch(a, store);
        auto [mode, target] = combine(a, store);
        return apply(store, op::Extrude{s, a.requireDouble("distance"), mode, target});
      });

  add("sketch_revolve",
      "Draws closed shapes on a sketch plane (as in sketch_extrude) and revolves them about the plane's u or v axis "
      "through its origin. The profile must not cross the axis.",
      objectJ(merged(sketchProperties(), {{"angle", number("Degrees, default 360")},
                                         {"axis", string("Sketch axis to revolve around", {"u", "v"})},
                                         {"mode", string("new_body, join or cut", {"new_body", "join", "cut"})},
                                         {"target_body", integer("Body to join to or cut from")}}),
             {"shapes", "axis"}),
      false, [this](const Args& a) {
        ModelStore& store = document().store();
        Sketch s = sketch(a, store);
        auto [mode, target] = combine(a, store);
        return apply(store, op::Revolve{s, a.dbl("angle").value_or(360), a.string("axis") == "v", mode, target});
      });

  add("boolean", "Union, subtract or intersect two bodies. The result keeps target_body's id; tool_body is consumed.",
      object({{"operation", string("Boolean operation", booleanNames())},
              {"target_body", integer("Body that is kept")},
              {"tool_body", integer("Body that is added, removed or intersected")}},
             {"operation", "target_body", "tool_body"}),
      false, [this](const Args& a) {
        ModelStore& store = document().store();
        auto kind = booleanKind(a.string("operation"));
        if (!kind) throw ToolError("Unknown boolean operation");
        int target = body(a, "target_body", store);
        int tool = body(a, "tool_body", store);
        return apply(store, op::Boolean{*kind, target, tool});
      });

  add("fillet_edges", "Rounds edges of a body. Pass edge indices from get_body_topology, or all: true.",
      S::edges({{"radius", number("Radius in mm")}}, {"body", "radius"}), false, [this](const Args& a) {
        ModelStore& store = document().store();
        int b = body(a, "body", store);
        auto e = this->edges(a, b, store);
        return apply(store, op::Fillet{b, e, a.requireDouble("radius")});
      });

  add("chamfer_edges", "Bevels edges of a body. Pass edge indices from get_body_topology, or all: true.",
      S::edges({{"distance", number("Chamfer distance in mm")}}, {"body", "distance"}), false, [this](const Args& a) {
        ModelStore& store = document().store();
        int b = body(a, "body", store);
        auto e = this->edges(a, b, store);
        return apply(store, op::Chamfer{b, e, a.requireDouble("distance")});
      });

  add("shell_body", "Hollows a body to the given wall thickness, removing the listed faces to leave openings.",
      object({{"body", integer("Body id")},
              {"faces", intArray("Face indices to open")},
              {"thickness", number("Wall thickness in mm")}},
             {"body", "thickness"}),
      false, [this](const Args& a) {
        ModelStore& store = document().store();
        int b = body(a, "body", store);
        return apply(store, op::Shell{b, a.ints("faces").value_or(std::vector<int>{}), a.requireDouble("thickness")});
      });

  add("push_pull_face", "Moves a planar face along its normal, adding material (positive) or removing it (negative).",
      object({{"body", integer("Body id")}, {"face", integer("Face index")}, {"distance", number("mm")}},
             {"body", "face", "distance"}),
      false, [this](const Args& a) {
        ModelStore& store = document().store();
        int b = body(a, "body", store);
        int face = a.requireInt("face");
        return apply(store, op::PushPull{b, face, a.requireDouble("distance")});
      });

  add("transform_body",
      "Moves and/or rotates a body. rotate turns `angle` degrees about `axis` through `origin` (default the origin) "
      "and is applied before translate.",
      object({{"body", integer("Body id")},
              {"translate", vec3("Offset [x, y, z] in mm")},
              {"rotate", object({{"axis", vec3("Axis direction")},
                                 {"angle", number("Degrees")},
                                 {"origin", vec3("Point on the axis")}},
                                {"axis", "angle"})}},
             {"body"}),
      false, [this](const Args& a) {
        ModelStore& store = document().store();
        int b = body(a, "body", store);
        std::vector<Operation> ops;
        if (auto rot = a.object("rotate")) {
          auto axis = rot->vec3("axis");
          if (!axis || !(length(*axis) > 1e-9)) throw ToolError("rotate.axis must be a non-zero vector");
          Vec3d origin = rot->vec3("origin").value_or(Vec3d{});
          ops.push_back(op::Rotate{b, origin, *axis, rot->requireDouble("angle")});
        }
        if (auto tr = a.vec3("translate"); tr && length(*tr) > 1e-9)
          ops.push_back(op::Translate{b, normalize(*tr), length(*tr)});
        if (ops.empty()) throw ToolError("Give translate and/or rotate");
        return apply(store, ops);
      });

  add("mirror_body", "Mirrors a body across a plane (xy, xz, yz or custom, optionally offset along its normal).",
      object({{"body", integer("Body id")},
              {"plane", planeSchema()},
              {"offset", number("Plane offset in mm")},
              {"keep_original", boolean("Keep the original body (default true)")}},
             {"body", "plane"}),
      false, [this](const Args& a) {
        ModelStore& store = document().store();
        int b = body(a, "body", store);
        SketchPlane p = plane(a, store);
        return apply(store, op::Mirror{b, p, a.boolean("keep_original").value_or(true)});
      });

  add("pattern_body",
      "Repeats a body. linear: count copies `spacing` mm apart along `direction`. circular: count copies spread over "
      "`angle` degrees about `axis` through `origin`. fuse joins the copies into one body.",
      object({{"body", integer("Body id")},
              {"kind", string("Pattern kind", {"linear", "circular"})},
              {"count", integer("Number of instances, including the original")},
              {"direction", vec3("Linear direction")},
              {"spacing", number("Linear spacing in mm")},
              {"axis", vec3("Circular axis direction (default Z)")},
              {"origin", vec3("Circular axis point")},
              {"angle", number("Circular total angle, default 360")},
              {"fuse", boolean("Join copies (default false)")}},
             {"body", "kind", "count"}),
      false, [this](const Args& a) {
        ModelStore& store = document().store();
        int b = body(a, "body", store);
        int count = a.requireInt("count");
        bool fuse = a.boolean("fuse").value_or(false);
        if (a.string("kind") == "linear") {
          auto dir = a.vec3("direction");
          if (!dir || !(length(*dir) > 1e-9)) throw ToolError("linear needs a direction");
          return apply(store, op::LinearPattern{b, normalize(*dir), count, a.requireDouble("spacing"), fuse});
        }
        Vec3d origin = a.vec3("origin").value_or(Vec3d{});
        Vec3d axis = a.vec3("axis").value_or(Vec3d{0, 0, 1});
        return apply(store, op::CircularPattern{b, origin, axis, count, a.dbl("angle").value_or(360), fuse});
      });

  add("scale_body",
      "Scales a body about `center` (default its bounding-box centre; \"bottom\" uses the centre of its base). Pass "
      "`factor` for uniform scaling or `factors` [x, y, z] to stretch it differently along each world axis, e.g. a "
      "sphere into a flattened pebble.",
      object({{"body", integer("Body id")},
              {"factor", number("Uniform factor")},
              {"factors", vec3("Per-axis factors [x, y, z]")},
              {"center", raw({{"description", "Point [x, y, z], or \"bottom\" for the base centre"}})}},
             {"body"}),
      false, [this](const Args& a) {
        ModelStore& store = document().store();
        int b = body(a, "body", store);
        auto m = store.meshes().find(b);
        if (m == store.meshes().end() || !m->second) throw ToolError("Body " + std::to_string(b) + " has no geometry");
        Vec3d center = toD((m->second->boundsMin + m->second->boundsMax) / 2.0f);
        if (a.str("center") == std::optional<std::string>("bottom")) {
          center.z = double(m->second->boundsMin.z);
        } else {
          try {
            if (auto c = a.vec3("center")) center = *c;
          } catch (const ToolError&) {
          }
        }
        if (auto f = a.vec3("factors")) {
          if (!(f->x > 0 && f->y > 0 && f->z > 0)) throw ToolError("factors must be positive");
          return apply(store, op::ScaleAxes{b, *f, center, f->x == f->y && f->y == f->z});
        }
        auto f = a.dbl("factor");
        if (!f || !(*f > 0)) throw ToolError("Pass factor or factors");
        return apply(store, op::ScaleAxes{b, Vec3d{*f, *f, *f}, center, true});
      });

  {
    json avoid = S::typed("array", "Bodies whose footprint takes no copies");
    avoid["items"] = S::typed("integer", nullptr);
    add("scatter_body",
        "Scatters randomised copies of `body` over the upward-facing surface of `target` (trees on terrain, pebbles on "
        "a bank). Copies land where a straight-down ray first hits target, not under any `avoid` body (e.g. water), on "
        "slopes below max_slope, at least `spacing` mm apart. Each copy gets a random size between min_scale and "
        "max_scale and a random turn; the seed makes it repeatable. Copies take the source's appearance. The source "
        "is removed unless keep_original.",
        object({{"body", integer("Body to copy")},
                {"target", integer("Body to scatter onto")},
                {"count", integer("Number of copies (fewer if there isn't room)")},
                {"seed", integer("Random seed, default 1")},
                {"min_scale", number("Smallest size factor, default 0.8")},
                {"max_scale", number("Largest size factor, default 1.2")},
                {"spacing", number("Minimum distance between copies in mm, default 0")},
                {"max_slope", number("Steepest ground in degrees, default 40")},
                {"sink", number("Fraction of height pushed into the ground, default 0.02")},
                {"random_rotation", boolean("Turn each copy about its up axis (default true)")},
                {"align_to_surface", boolean("Tilt copies to the surface normal (default false)")},
                {"keep_original", boolean("Keep the source body (default false)")},
                {"avoid", avoid}},
               {"body", "target", "count"}),
        false, [this](const Args& a) {
          ModelStore& store = document().store();
          int source = body(a, "body", store);
          int target = body(a, "target", store);
          ScatterSettings p;
          p.count = a.requireInt("count");
          if (auto x = a.integer("seed")) p.seed = *x;
          if (auto x = a.dbl("min_scale")) p.minScale = *x;
          if (auto x = a.dbl("max_scale")) p.maxScale = *x;
          if (auto x = a.dbl("spacing")) p.spacing = *x;
          if (auto x = a.dbl("max_slope")) p.maxSlope = *x;
          if (auto x = a.dbl("sink")) p.sink = *x;
          if (auto x = a.boolean("random_rotation")) p.randomRotation = *x;
          if (auto x = a.boolean("align_to_surface")) p.alignToSurface = *x;
          if (auto x = a.boolean("keep_original")) p.keepOriginal = *x;
          std::vector<int> avoidList = a.ints("avoid").value_or(std::vector<int>{});
          auto ids = store.bodyIds();
          for (int b : avoidList)
            if (!contains(ids, b)) throw ToolError("avoid body " + std::to_string(b) + " does not exist");
          return apply(store, op::Scatter{source, target, avoidList, p});
        });
  }

  {
    json rowItems = S::typed("array", nullptr);
    rowItems["items"] = S::typed("number", nullptr);
    json heights = S::typed("array", "Rows of heights in mm; resampled to the grid");
    heights["items"] = rowItems;
    json objectItems = S::typed("object", nullptr);
    json hills = S::typed("array", "[{x, y, radius, height}] smooth domes, mm");
    hills["items"] = objectItems;
    json channels = S::typed("array", "[{points: [[x, y], …], width, bed, bank}] carved beds, mm");
    channels["items"] = objectItems;
    add("create_terrain",
        "Adds a smooth landscape solid: a surface fitted through a grid of heights, with vertical sides and a flat "
        "bottom at center z. Relief comes from any mix of: `heights` (rows of mm above the base, first row at -Y), "
        "`image_path` (greyscale height map, white high, scaled to image_height mm), `noise` (rolling hills), then "
        "`hills` (domes added on top), `terraces` (flat steps), and finally `channels` (river or path beds carved "
        "along polylines to `bed` mm above the base, easing back up over `bank` mm). Coordinates are world XY in mm. "
        "Returns the new body.",
        object({
            {"name", string("Name in the history")},
            {"center", vec3("Centre of the bottom face, default [0, 0, 0]")},
            {"width", number("Size along X, mm (default 200)")},
            {"depth", number("Size along Y, mm (default 200)")},
            {"resolution", integer("Grid samples along the longer side, 8–129 (default 48)")},
            {"base_thickness", number("Solid under the lowest relief, mm (default 10)")},
            {"heights", heights},
            {"image_path", string("Absolute path to a greyscale height map image")},
            {"image_height", number("Relief of white in the image, mm (default 40)")},
            {"noise", S::typed("object",
                               "{seed, height (mm, default 40), scale (hill size as a fraction of the terrain, default "
                               "0.45), octaves (4), roughness (0.5)}")},
            {"hills", hills},
            {"terraces", S::typed("object", "{count, softness 0–1}: flat steps")},
            {"channels", channels},
        }),
        false, [this](const Args& a) { return createTerrain(a); });
  }

  add("duplicate_body", "Copies a body in place as a new body.", object({{"body", integer("Body id")}}, {"body"}), false,
      [this](const Args& a) {
        ModelStore& store = document().store();
        return apply(store, op::Duplicate{body(a, "body", store)});
      });

  add("delete_body", "Removes a body (as a history step, so it can be undone).",
      object({{"body", integer("Body id")}}, {"body"}), false, [this](const Args& a) {
        ModelStore& store = document().store();
        return apply(store, op::Delete{body(a, "body", store)});
      });

  add("measure",
      "Measures one entity (volume/area/length/radius) or the distance and angle between two. An entity is {body} or "
      "{body, face} or {body, edge}.",
      object({{"a", entitySchema()}, {"b", entitySchema()}}, {"a"}), true,
      [this](const Args& a) { return text(measure(a, document(false).store())); });

  {
    json values = S::typed("object", "Parameter id or label → number or expression");
    json addl = json::object();
    addl["type"] = json::array({"number", "string"});
    values["additionalProperties"] = addl;
    add("update_feature",
        "Changes parameters of a feature in the history and rebuilds everything after it. values maps a parameter id "
        "or label (from get_model) to a number, or to an expression string over project variables (e.g. \"width * "
        "2\").",
        object({{"feature_id", string("Feature id")}, {"values", values}}, {"feature_id", "values"}), false,
        [this](const Args& a) { return updateFeature(a, document().store()); });
  }

  add("delete_feature", "Removes a feature from the history and rebuilds.",
      object({{"feature_id", string("Feature id")}}, {"feature_id"}), false, [this](const Args& a) {
        ModelStore& store = document().store();
        Feature f = feature(a, store);
        store.deleteFeature(f.id);
        json out = json::object();
        out["deleted"] = f.title();
        out["model"] = modelSummary(store);
        return text(out);
      });

  add("suppress_feature", "Turns a feature off (or back on) without deleting it.",
      object({{"feature_id", string("Feature id")}, {"suppressed", boolean("Default true")}}, {"feature_id"}), false,
      [this](const Args& a) {
        ModelStore& store = document().store();
        Feature f = feature(a, store);
        if (f.suppressed != a.boolean("suppressed").value_or(true)) store.toggleSuppressed(f.id);
        return text(modelSummary(store));
      });

  add("set_variable",
      "Creates, changes or (with an empty expression) removes a project variable. Feature parameters bound to "
      "expressions update with it.",
      object({{"name", string("Variable name")},
              {"expression", string("Number or expression, e.g. \"40\" or \"width / 2\"")}},
             {"name", "expression"}),
      false, [this](const Args& a) {
        ModelStore& store = document().store();
        std::string name = a.string("name");
        std::string expr = trimmingSpaces(a.string("expression"));
        ProjectSettings settings = store.settings();
        auto it = std::find_if(settings.variables.begin(), settings.variables.end(),
                               [&](const Variable& v) { return v.name == name; });
        if (it != settings.variables.end()) {
          if (expr.empty()) settings.variables.erase(it);
          else it->expression = expr;
        } else if (!expr.empty()) {
          Variable var;
          var.name = name;
          var.expression = expr;
          settings.variables.push_back(var);
        }
        store.setSettings(settings);
        return text(modelSummary(store));
      });

  add("undo", "Undoes the last history change.", noArgs, false, [this](const Args&) {
    ModelStore& store = document().store();
    if (!store.canUndo()) throw ToolError("Nothing to undo");
    store.undo();
    return text(modelSummary(store));
  });

  add("redo", "Redoes the last undone change.", noArgs, false, [this](const Args&) {
    ModelStore& store = document().store();
    if (!store.canRedo()) throw ToolError("Nothing to redo");
    store.redo();
    return text(modelSummary(store));
  });

  add("import_file", "Imports a STEP, IGES or BREP file from an absolute path as new bodies.",
      object({{"path", string("Absolute file path")}}, {"path"}), false, [this](const Args& a) {
        ModelStore& store = document().store();
        std::string path = expandingTilde(a.string("path"));
        std::error_code ec;
        if (!fs::exists(fs::u8path(path), ec)) throw ToolError("No file at " + path);
        auto beforeList = store.bodyIds();
        std::set<int> before(beforeList.begin(), beforeList.end());
        store.importFile(path);
        json added = json::array();
        for (int b : store.bodyIds())
          if (!before.count(b)) added.push_back(bodySummary(store, b));
        if (added.empty()) throw ToolError(store.toast().value_or("Nothing was imported"));
        json out = json::object();
        out["new_bodies"] = added;
        return text(out);
      });

  add("export_model",
      "Writes bodies to a file: STEP, IGES, STL, BREP (precise), 3MF, GLB, OBJ, USDZ (meshes with colours), DXF or "
      "SVG (the open, selected or visible sketches; \"dxf_drawing\" and \"pdf\" give a drawing sheet of the model), "
      "or SCULPT3D (the whole document). Defaults: all bodies, saved to ~/Downloads/<document name>.<extension>.",
      object({{"format", string("File format", exportFormats())},
              {"path", string("Absolute output path")},
              {"bodies", intArray("Body ids (default all)")}},
             {"format"}),
      true, [this](const Args& a) { return exportModel(a, document(false).store()); });

  add("set_appearance",
      "Paints bodies: a colour (\"#RRGGBB\"), metallic 0–1, roughness 0–1 and opacity 0–1. Shown in the viewport and "
      "in Visualize renders, and saved with the document.",
      object({{"bodies", intArray("Body ids")},
              {"color", string("Hex colour, e.g. #8F9A78")},
              {"metallic", number("0 matte to 1 metal")},
              {"roughness", number("0 mirror to 1 matte")},
              {"opacity", number("1 solid, lower is see-through")}},
             {"bodies"}),
      false, [this](const Args& a) {
        Document& doc = document(false);
        ModelStore& store = doc.store();
        auto ids = store.bodyIds();
        std::vector<int> bodies = a.ints("bodies").value_or(std::vector<int>{});
        for (int b : bodies)
          if (!contains(ids, b)) throw ToolError("No body " + std::to_string(b));
        if (bodies.empty()) throw ToolError("bodies is empty");
        Appearance look;
        if (auto hex = a.str("color")) {
          auto c = color(*hex);
          if (!c) throw ToolError("color must be #RRGGBB");
          look.color = *c;
        }
        look.metallic = float(std::min(std::max(a.dbl("metallic").value_or(0), 0.0), 1.0));
        look.roughness = float(std::min(std::max(a.dbl("roughness").value_or(0.45), 0.0), 1.0));
        look.opacity = float(std::min(std::max(a.dbl("opacity").value_or(1), 0.05), 1.0));
        ProjectSettings settings = store.settings();
        for (int b : bodies) settings.appearance[b] = look;
        store.setSettings(settings);
        doc.redraw();
        json out = json::object();
        out["painted"] = bodies;
        return text(out);
      });

  add("set_environment",
      "Sets the background, lighting and Visualized render options of the active document. Only the given values "
      "change. Rendering effects (cast shadows, ambient occlusion, smooth shading) show in the visualized display "
      "mode (set_view display_mode) and in Visualize.",
      object({
          {"background", string("Background colour #RRGGBB")},
          {"gradient", boolean("Lighter towards the top")},
          {"show_grid", boolean("Show the ground grid")},
          {"light_intensity", number("Key light multiplier 0–2")},
          {"light_azimuth", number("Key light direction, degrees right of the viewer (-180–180)")},
          {"light_elevation", number("Key light height above the ground, degrees 10–85")},
          {"ground_shadow", boolean("Soft contact shadow under each body")},
          {"cast_shadows", boolean("Bodies cast shadows on each other and on the floor")},
          {"shadow_softness", number("0 crisp to 1 very soft")},
          {"ambient_occlusion", boolean("Darken creases and contacts")},
          {"occlusion_strength", number("0–1")},
          {"smooth_shading", boolean("Blend normals across shallow face joins")},
      }),
      false, [this](const Args& a) {
        Document& doc = document(false);
        ModelStore& store = doc.store();
        EnvironmentSettings env = store.settings().environment;
        if (auto hex = a.str("background")) {
          auto c = color(*hex);
          if (!c) throw ToolError("background must be #RRGGBB");
          env.background = *c;
        }
        auto f = [&](const char* k, double lo, double hi) -> std::optional<float> {
          if (auto d = a.dbl(k)) return float(std::min(std::max(*d, lo), hi));
          return std::nullopt;
        };
        if (auto x = a.boolean("gradient")) env.gradient = *x;
        if (auto x = a.boolean("show_grid")) env.showGrid = *x;
        if (auto x = f("light_intensity", 0, 2)) env.lightIntensity = *x;
        if (auto x = f("light_azimuth", -180, 180)) env.lightAzimuth = *x;
        if (auto x = f("light_elevation", 10, 85)) env.lightElevation = *x;
        if (auto x = a.boolean("ground_shadow")) env.groundShadow = *x;
        if (auto x = a.boolean("cast_shadows")) env.castShadows = *x;
        if (auto x = f("shadow_softness", 0, 1)) env.shadowSoftness = *x;
        if (auto x = a.boolean("ambient_occlusion")) env.ambientOcclusion = *x;
        if (auto x = f("occlusion_strength", 0, 1)) env.occlusionStrength = *x;
        if (auto x = a.boolean("smooth_shading")) env.smoothShading = *x;
        store.updateSettings([&](ProjectSettings& s) { s.environment = env; });
        doc.redraw();
        return text("Environment updated");
      });

  add("set_view",
      "Points the document's camera at a standard view, zooms to fit, and/or sets an exact orbit angle. yaw is "
      "degrees around Z (0 looks from +X), pitch is degrees above the ground plane, zoom scales the distance (below 1 "
      "is closer).",
      objectJ(cameraProperties()), false, [this](const Args& a) {
        setView(a, document(false));
        return text("View updated");
      });

  add("screenshot",
      "Renders the active document's viewport to a PNG image. Optionally sets a view (iso, top, front, right) and "
      "fits first.",
      objectJ(merged(cameraProperties(), {{"scale", number("Pixel scale 1–3, default 1.5")}})), true,
      [this](const Args& a) {
        Document& doc = document(false);
        setView(a, doc);
        double scale = std::min(std::max(a.dbl("scale").value_or(1.5), 1.0), 3.0);
        std::string error;
        std::vector<uint8_t> png = doc.screenshotPng(scale, error);
        if (png.empty())
          throw ToolError(error.empty() ? "The document window needs to be open and visible to render a screenshot"
                                        : error);
        json block = json::object();
        block["type"] = "image";
        block["data"] = base64Encode(png);
        block["mimeType"] = "image/png";
        return json::array({block});
      });

  return t;
}

Toolbox::Toolbox(DocumentRegistry& registry) : impl_(std::make_unique<Impl>(registry)) {}
Toolbox::~Toolbox() = default;

json Toolbox::definitions() const {
  json out = json::array();
  for (auto& t : impl_->tools) {
    json d = json::object();
    d["name"] = t.name;
    d["description"] = t.description;
    d["inputSchema"] = t.schema;
    json annotations = json::object();
    annotations["readOnlyHint"] = t.readOnly;
    d["annotations"] = annotations;
    out.push_back(d);
  }
  return out;
}

bool Toolbox::has(const std::string& name) const {
  for (auto& t : impl_->tools)
    if (t.name == name) return true;
  return false;
}

json Toolbox::call(const std::string& name, const json& arguments) {
  for (auto& t : impl_->tools)
    if (t.name == name) return t.run(Args(arguments));
  throw ToolError("Unknown tool: " + name);
}

}  // namespace sculpt::mcp
