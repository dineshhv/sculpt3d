// Swift Codable compatibility for the model types (see Codec.hpp).
#include "sculpt/model/Codec.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace sculpt {

using json = nlohmann::json;

// MARK: Numbers

namespace {

// Shortest digits that round-trip, as (digits, decimal exponent of the first digit).
template <class F>
void shortest(F v, std::string& digits, int& exponent) {
  const int maxDigits = std::numeric_limits<F>::max_digits10;
  char buf[64];
  for (int prec = 1; prec <= maxDigits; ++prec) {
    std::snprintf(buf, sizeof buf, "%.*e", prec - 1, double(v));
    F back = std::is_same<F, float>::value ? F(std::strtof(buf, nullptr)) : F(std::strtod(buf, nullptr));
    if (back == v || prec == maxDigits) break;
  }
  // buf is "d.ddde±XX" (no sign: callers pass |v|).
  const char* e = std::strchr(buf, 'e');
  digits.clear();
  for (const char* p = buf; p < e; ++p)
    if (*p != '.') digits += *p;
  while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
  exponent = std::atoi(e + 1);
}

template <class F>
std::string format(F v, double decimalLimit) {
  if (std::isnan(v)) return "0";  // Swift refuses to encode NaN; never written by the model.
  if (std::isinf(v)) v = v > 0 ? std::numeric_limits<F>::max() : -std::numeric_limits<F>::max();
  if (v == 0) return std::signbit(v) ? "-0" : "0";
  std::string sign = v < 0 ? "-" : "";
  F a = v < 0 ? -v : v;
  std::string digits;
  int exp;
  shortest(a, digits, exp);
  std::string out = sign;
  if (exp >= -4 && double(a) <= decimalLimit) {
    if (exp >= 0) {
      if (int(digits.size()) <= exp + 1) {
        out += digits + std::string(size_t(exp + 1 - int(digits.size())), '0');
      } else {
        out += digits.substr(0, size_t(exp + 1)) + "." + digits.substr(size_t(exp + 1));
      }
    } else {
      out += "0." + std::string(size_t(-exp - 1), '0') + digits;
    }
    return out;
  }
  out += digits.substr(0, 1);
  if (digits.size() > 1) out += "." + digits.substr(1);
  char e[16];
  std::snprintf(e, sizeof e, "e%c%02d", exp < 0 ? '-' : '+', exp < 0 ? -exp : exp);
  return out + e;
}

}  // namespace

std::string swiftNumber(double v) { return format<double>(v, 9007199254740992.0); }
std::string swiftNumber(float v) { return format<float>(v, 16777216.0); }

std::string swiftString(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '/': out += "\\/"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += char(c);
        }
    }
  }
  return out + "\"";
}

// MARK: JValue

void JValue::write(std::string& out, bool pretty, int indent) const {
  switch (type_) {
    case Type::Null: out += "null"; return;
    case Type::Bool: out += b_ ? "true" : "false"; return;
    case Type::Int: out += std::to_string(i_); return;
    case Type::Double: out += swiftNumber(d_); return;
    case Type::Float: out += swiftNumber(float(d_)); return;
    case Type::String: out += swiftString(s_); return;
    case Type::Array: {
      out += '[';
      if (pretty) {
        out += '\n';
        if (arr_.empty()) out += '\n';
        for (size_t i = 0; i < arr_.size(); ++i) {
          out.append(size_t(indent + 2), ' ');
          arr_[i].write(out, pretty, indent + 2);
          out += i + 1 < arr_.size() ? ",\n" : "\n";
        }
        out.append(size_t(indent), ' ');
      } else {
        for (size_t i = 0; i < arr_.size(); ++i) {
          if (i) out += ',';
          arr_[i].write(out, pretty, indent);
        }
      }
      out += ']';
      return;
    }
    case Type::Object: {
      out += '{';
      if (pretty) {
        out += '\n';
        if (obj_.empty()) out += '\n';
        size_t i = 0;
        for (auto& [k, v] : obj_) {
          out.append(size_t(indent + 2), ' ');
          out += swiftString(k);
          out += " : ";
          v.write(out, pretty, indent + 2);
          out += ++i < obj_.size() ? ",\n" : "\n";
        }
        out.append(size_t(indent), ' ');
      } else {
        bool first = true;
        for (auto& [k, v] : obj_) {
          if (!first) out += ',';
          first = false;
          out += swiftString(k);
          out += ':';
          v.write(out, pretty, indent);
        }
      }
      out += '}';
      return;
    }
  }
}

std::string JValue::dump(bool pretty) const {
  std::string out;
  write(out, pretty, 0);
  return out;
}

json JValue::toJson() const {
  switch (type_) {
    case Type::Null: return nullptr;
    case Type::Bool: return b_;
    case Type::Int: return i_;
    case Type::Double: return d_;
    case Type::Float: return double(float(d_));
    case Type::String: return s_;
    case Type::Array: {
      json a = json::array();
      for (auto& v : arr_) a.push_back(v.toJson());
      return a;
    }
    case Type::Object: {
      json o = json::object();
      for (auto& [k, v] : obj_) o[k] = v.toJson();
      return o;
    }
  }
  return nullptr;
}

// MARK: Base64

static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::vector<uint8_t>& data) {
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
    out += kB64[(n >> 18) & 63];
    out += kB64[(n >> 12) & 63];
    out += kB64[(n >> 6) & 63];
    out += kB64[n & 63];
  }
  if (i < data.size()) {
    uint32_t n = uint32_t(data[i]) << 16;
    if (i + 1 < data.size()) n |= uint32_t(data[i + 1]) << 8;
    out += kB64[(n >> 18) & 63];
    out += kB64[(n >> 12) & 63];
    out += i + 1 < data.size() ? kB64[(n >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

bool base64Decode(const std::string& text, std::vector<uint8_t>& out) {
  out.clear();
  if (text.size() % 4 != 0) return false;
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  out.reserve(text.size() / 4 * 3);
  for (size_t i = 0; i < text.size(); i += 4) {
    int v[4];
    int pad = 0;
    for (int k = 0; k < 4; ++k) {
      char c = text[i + size_t(k)];
      if (c == '=' && i + 4 == text.size() && k >= 2) {
        v[k] = 0;
        ++pad;
      } else {
        if (pad) return false;
        v[k] = value(c);
        if (v[k] < 0) return false;
      }
    }
    uint32_t n = (uint32_t(v[0]) << 18) | (uint32_t(v[1]) << 12) | (uint32_t(v[2]) << 6) | uint32_t(v[3]);
    out.push_back(uint8_t(n >> 16));
    if (pad < 2) out.push_back(uint8_t(n >> 8));
    if (pad < 1) out.push_back(uint8_t(n));
  }
  return true;
}

// MARK: Encoding helpers

namespace {

JValue arr(const Vec3d& v) {
  JValue a = JValue::array();
  a.push(v.x);
  a.push(v.y);
  a.push(v.z);
  return a;
}
JValue arr(const Vec3f& v) {
  JValue a = JValue::array();
  a.push(v.x);
  a.push(v.y);
  a.push(v.z);
  return a;
}
JValue arr(const Vec2d& v) {
  JValue a = JValue::array();
  a.push(v.x);
  a.push(v.y);
  return a;
}
JValue arr(const std::vector<int>& v) {
  JValue a = JValue::array();
  for (int i : v) a.push(i);
  return a;
}
JValue arr(const std::vector<double>& v) {
  JValue a = JValue::array();
  for (double d : v) a.push(d);
  return a;
}
JValue arr(const std::vector<Uuid>& v) {
  JValue a = JValue::array();
  for (auto& u : v) a.push(u.string());
  return a;
}
template <class T>
JValue list(const std::vector<T>& v) {
  JValue a = JValue::array();
  for (auto& x : v) a.push(encode(x));
  return a;
}
void setIf(JValue& o, const char* key, const std::optional<int>& v) {
  if (v) o.set(key, *v);
}

// MARK: Decoding helpers

std::string where(const char* key) { return std::string("\"") + key + "\""; }

const json& req(const json& j, const char* key) {
  if (!j.is_object()) throw CodingError("Expected a dictionary for " + where(key));
  auto it = j.find(key);
  if (it == j.end()) throw CodingError("No value associated with key " + where(key));
  return *it;
}
// decodeIfPresent: absent or null.
const json* opt(const json& j, const char* key) {
  if (!j.is_object()) return nullptr;
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) return nullptr;
  return &*it;
}

double toDouble(const json& v) {
  if (!v.is_number()) throw CodingError("Expected a number, found " + std::string(v.type_name()));
  return v.get<double>();
}
float toFloat(const json& v) { return float(toDouble(v)); }
int toInt(const json& v) {
  if (v.is_number_integer()) return int(v.get<int64_t>());
  if (v.is_number_float()) {
    double d = v.get<double>();
    if (d == std::floor(d) && std::abs(d) < 9.3e18) return int(d);
  }
  throw CodingError("Expected an integer, found " + v.dump());
}
bool toBool(const json& v) {
  if (!v.is_boolean()) throw CodingError("Expected a Bool, found " + v.dump());
  return v.get<bool>();
}
std::string toString(const json& v) {
  if (!v.is_string()) throw CodingError("Expected a String, found " + std::string(v.type_name()));
  return v.get<std::string>();
}
const json& toArray(const json& v) {
  if (!v.is_array()) throw CodingError("Expected an array, found " + std::string(v.type_name()));
  return v;
}
Vec3d toVec3d(const json& v) {
  toArray(v);
  if (v.size() < 3) throw CodingError("Expected 3 numbers for a SIMD3");
  return {toDouble(v[0]), toDouble(v[1]), toDouble(v[2])};
}
Vec3f toVec3f(const json& v) {
  toArray(v);
  if (v.size() < 3) throw CodingError("Expected 3 numbers for a SIMD3");
  return {toFloat(v[0]), toFloat(v[1]), toFloat(v[2])};
}
Vec2d toVec2d(const json& v) {
  toArray(v);
  if (v.size() < 2) throw CodingError("Expected 2 numbers for a SIMD2");
  return {toDouble(v[0]), toDouble(v[1])};
}
std::vector<int> toInts(const json& v) {
  std::vector<int> out;
  for (auto& x : toArray(v)) out.push_back(toInt(x));
  return out;
}
std::vector<double> toDoubles(const json& v) {
  std::vector<double> out;
  for (auto& x : toArray(v)) out.push_back(toDouble(x));
  return out;
}
Uuid toUuid(const json& v) {
  auto u = Uuid::parse(toString(v));
  if (!u) throw CodingError("Attempted to decode UUID from invalid UUID string.");
  return *u;
}
std::vector<Uuid> toUuids(const json& v) {
  std::vector<Uuid> out;
  for (auto& x : toArray(v)) out.push_back(toUuid(x));
  return out;
}
template <class T>
std::vector<T> toList(const json& v) {
  std::vector<T> out;
  for (auto& x : toArray(v)) {
    T t;
    decode(x, t);
    out.push_back(std::move(t));
  }
  return out;
}
std::optional<int> optInt(const json& j, const char* key) {
  if (auto v = opt(j, key)) return toInt(*v);
  return std::nullopt;
}
int intKey(const std::string& key) {
  char* end = nullptr;
  long v = std::strtol(key.c_str(), &end, 10);
  if (key.empty() || end != key.c_str() + key.size()) throw CodingError("Expected Int key but found " + key);
  return int(v);
}

}  // namespace

// MARK: Sketch

JValue encode(const SketchPlane& v) {
  JValue o = JValue::object();
  o.set("origin", arr(v.origin));
  o.set("normal", arr(v.normal));
  o.set("xDir", arr(v.xDir));
  return o;
}

void decode(const json& j, SketchPlane& v) {
  v.origin = toVec3d(req(j, "origin"));
  v.normal = toVec3d(req(j, "normal"));
  v.xDir = toVec3d(req(j, "xDir"));
}

static JValue encode(const SketchLine& v) {
  JValue o = JValue::object();
  o.set("a", v.a);
  o.set("b", v.b);
  o.set("construction", v.construction);
  return o;
}
static void decode(const json& j, SketchLine& v) {
  v.a = toInt(req(j, "a"));
  v.b = toInt(req(j, "b"));
  v.construction = toBool(req(j, "construction"));
}

static JValue encode(const SketchCircle& v) {
  JValue o = JValue::object();
  o.set("center", v.center);
  o.set("radius", v.radius);
  o.set("construction", v.construction);
  setIf(o, "start", v.start);
  setIf(o, "end", v.end);
  return o;
}
static void decode(const json& j, SketchCircle& v) {
  v.center = toInt(req(j, "center"));
  v.radius = toDouble(req(j, "radius"));
  v.construction = toBool(req(j, "construction"));
  v.start = optInt(j, "start");
  v.end = optInt(j, "end");
}

static JValue encode(const SketchSpline& v) {
  JValue o = JValue::object();
  o.set("points", arr(v.points));
  o.set("closed", v.closed);
  o.set("construction", v.construction);
  o.set("controlPoints", v.controlPoints);
  return o;
}
static void decode(const json& j, SketchSpline& v) {
  v.points = toInts(req(j, "points"));
  v.closed = opt(j, "closed") ? toBool(*opt(j, "closed")) : false;
  v.construction = opt(j, "construction") ? toBool(*opt(j, "construction")) : false;
  v.controlPoints = opt(j, "controlPoints") ? toBool(*opt(j, "controlPoints")) : false;
}

static JValue encode(const SketchEllipse& v) {
  JValue o = JValue::object();
  o.set("center", v.center);
  o.set("majorRadius", v.majorRadius);
  o.set("minorRadius", v.minorRadius);
  o.set("rotation", v.rotation);
  o.set("construction", v.construction);
  return o;
}
static void decode(const json& j, SketchEllipse& v) {
  v.center = toInt(req(j, "center"));
  v.majorRadius = toDouble(req(j, "majorRadius"));
  v.minorRadius = toDouble(req(j, "minorRadius"));
  v.rotation = toDouble(req(j, "rotation"));
  v.construction = toBool(req(j, "construction"));
}

static JValue encode(const SketchConstraint& v) {
  JValue o = JValue::object();
  o.set("kind", rawValue(v.kind));
  o.set("a", v.a);
  o.set("b", v.b);
  o.set("value", v.value);
  o.set("value2", v.value2);
  setIf(o, "c", v.c);
  return o;
}
static void decode(const json& j, SketchConstraint& v) {
  auto kind = constraintKind(toString(req(j, "kind")));
  if (!kind) throw CodingError("Cannot initialize Kind from invalid String value " + req(j, "kind").dump());
  v.kind = *kind;
  v.a = toInt(req(j, "a"));
  v.b = toInt(req(j, "b"));
  v.value = toDouble(req(j, "value"));
  v.value2 = toDouble(req(j, "value2"));
  v.c = optInt(j, "c");
}

JValue encode(const Sketch& v) {
  JValue o = JValue::object();
  o.set("plane", encode(v.plane));
  JValue pts = JValue::array();
  for (auto& p : v.points) pts.push(arr(p));
  o.set("points", pts);
  o.set("lines", list(v.lines));
  o.set("circles", list(v.circles));
  o.set("constraints", list(v.constraints));
  o.set("markers", arr(v.markers));
  o.set("splines", list(v.splines));
  o.set("ellipses", list(v.ellipses));
  return o;
}

void decode(const json& j, Sketch& v) {
  v = Sketch();
  decode(req(j, "plane"), v.plane);
  // The first file format stored unconstrained shapes; those become
  // constrained geometry on load.
  if (auto legacy = opt(j, "shapes")) {
    for (auto& shape : toArray(*legacy)) {
      if (!shape.is_object() || shape.size() != 1) throw CodingError("Invalid number of keys found, expected one.");
      const std::string kind = shape.begin().key();
      const json& body = shape.begin().value();
      if (kind == "rectangle") {
        v.addRectangle(toVec2d(req(body, "_0")), toVec2d(req(body, "_1")), std::nullopt, std::nullopt);
      } else if (kind == "circle") {
        int c = v.addPoint(toVec2d(req(body, "center")));
        v.addCircle(c, toDouble(req(body, "radius")));
      } else if (kind == "polygon") {
        std::vector<int> ids;
        for (auto& p : toArray(req(body, "_0"))) ids.push_back(v.addPoint(toVec2d(p)));
        for (size_t i = 0; i < ids.size(); ++i) v.addLine(ids[i], ids[(i + 1) % ids.size()]);
      } else {
        throw CodingError("Unknown legacy shape " + kind);
      }
    }
    return;
  }
  if (auto p = opt(j, "points")) {
    v.points.clear();
    for (auto& x : toArray(*p)) v.points.push_back(toVec2d(x));
  }
  if (auto p = opt(j, "lines")) v.lines = toList<SketchLine>(*p);
  if (auto p = opt(j, "circles")) v.circles = toList<SketchCircle>(*p);
  if (auto p = opt(j, "constraints")) v.constraints = toList<SketchConstraint>(*p);
  if (auto p = opt(j, "markers")) v.markers = toInts(*p);
  if (auto p = opt(j, "splines")) v.splines = toList<SketchSpline>(*p);
  if (auto p = opt(j, "ellipses")) v.ellipses = toList<SketchEllipse>(*p);
}

// MARK: Visualization

JValue encode(const Appearance& v) {
  JValue o = JValue::object();
  o.set("color", arr(v.color));
  o.set("metallic", v.metallic);
  o.set("roughness", v.roughness);
  o.set("opacity", v.opacity);
  if (v.preset) o.set("preset", *v.preset);
  return o;
}

void decode(const json& j, Appearance& v) {
  v = Appearance();
  if (auto p = opt(j, "color")) v.color = toVec3f(*p);
  if (auto p = opt(j, "metallic")) v.metallic = toFloat(*p);
  if (auto p = opt(j, "roughness")) v.roughness = toFloat(*p);
  if (auto p = opt(j, "opacity")) v.opacity = toFloat(*p);
  if (auto p = opt(j, "preset")) v.preset = toString(*p);
  // Documents from before material values stored a material name.
  if (auto p = opt(j, "material")) {
    std::string m = toString(*p);
    if (m == "matte") {
      v.roughness = 0.9f;
    } else if (m == "metal") {
      v.metallic = 1;
      v.roughness = 0.25f;
    } else if (m == "glass") {
      v.opacity = 0.35f;
      v.roughness = 0.05f;
    }
  }
}

JValue encode(const EnvironmentSettings& v) {
  JValue o = JValue::object();
  o.set("background", arr(v.background));
  o.set("gradient", v.gradient);
  o.set("showGrid", v.showGrid);
  o.set("lightIntensity", v.lightIntensity);
  o.set("lightAzimuth", v.lightAzimuth);
  o.set("groundShadow", v.groundShadow);
  o.set("lightElevation", v.lightElevation);
  o.set("castShadows", v.castShadows);
  o.set("shadowSoftness", v.shadowSoftness);
  o.set("ambientOcclusion", v.ambientOcclusion);
  o.set("occlusionStrength", v.occlusionStrength);
  o.set("smoothShading", v.smoothShading);
  return o;
}

void decode(const json& j, EnvironmentSettings& v) {
  // Older documents lack the newer keys; missing ones keep their defaults.
  v = EnvironmentSettings();
  if (!j.is_object()) throw CodingError("Expected a dictionary for EnvironmentSettings");
  if (auto p = opt(j, "background")) v.background = toVec3f(*p);
  if (auto p = opt(j, "gradient")) v.gradient = toBool(*p);
  if (auto p = opt(j, "showGrid")) v.showGrid = toBool(*p);
  if (auto p = opt(j, "lightIntensity")) v.lightIntensity = toFloat(*p);
  if (auto p = opt(j, "lightAzimuth")) v.lightAzimuth = toFloat(*p);
  if (auto p = opt(j, "groundShadow")) v.groundShadow = toBool(*p);
  if (auto p = opt(j, "lightElevation")) v.lightElevation = toFloat(*p);
  if (auto p = opt(j, "castShadows")) v.castShadows = toBool(*p);
  if (auto p = opt(j, "shadowSoftness")) v.shadowSoftness = toFloat(*p);
  if (auto p = opt(j, "ambientOcclusion")) v.ambientOcclusion = toBool(*p);
  if (auto p = opt(j, "occlusionStrength")) v.occlusionStrength = toFloat(*p);
  if (auto p = opt(j, "smoothShading")) v.smoothShading = toBool(*p);
}

JValue encode(const CameraSettings& v) {
  JValue o = JValue::object();
  o.set("orthographic", v.orthographic);
  o.set("fieldOfView", v.fieldOfView);
  return o;
}

void decode(const json& j, CameraSettings& v) {
  v.orthographic = toBool(req(j, "orthographic"));
  v.fieldOfView = toFloat(req(j, "fieldOfView"));
}

JValue encode(const ReferenceImage& v) {
  JValue o = JValue::object();
  o.set("name", v.name);
  o.set("png", base64Encode(v.png));
  o.set("plane", encode(v.plane));
  o.set("width", v.width);
  o.set("height", v.height);
  o.set("opacity", v.opacity);
  o.set("decal", v.decal);
  return o;
}

void decode(const json& j, ReferenceImage& v) {
  v.name = toString(req(j, "name"));
  if (!base64Decode(toString(req(j, "png")), v.png)) throw CodingError("Encountered Data is not valid Base64.");
  decode(req(j, "plane"), v.plane);
  v.width = toDouble(req(j, "width"));
  v.height = toDouble(req(j, "height"));
  v.opacity = opt(j, "opacity") ? toDouble(*opt(j, "opacity")) : 0.8;
  v.decal = opt(j, "decal") ? toBool(*opt(j, "decal")) : false;
}

// MARK: Terrain and scatter

static JValue encode(const TerrainNoise& v) {
  JValue o = JValue::object();
  o.set("seed", v.seed);
  o.set("scale", v.scale);
  o.set("octaves", v.octaves);
  o.set("roughness", v.roughness);
  return o;
}
static void decode(const json& j, TerrainNoise& v) {
  v.seed = toInt(req(j, "seed"));
  v.scale = toDouble(req(j, "scale"));
  v.octaves = toInt(req(j, "octaves"));
  v.roughness = toDouble(req(j, "roughness"));
}

JValue encode(const TerrainSettings& v) {
  JValue o = JValue::object();
  o.set("name", v.name);
  o.set("center", arr(v.center));
  o.set("width", v.width);
  o.set("depth", v.depth);
  o.set("nx", v.nx);
  o.set("ny", v.ny);
  o.set("heights", arr(v.heights));
  o.set("heightScale", v.heightScale);
  o.set("floor", v.floor);
  if (v.noise) o.set("noise", encode(*v.noise));
  o.set("noiseHeight", v.noiseHeight);
  return o;
}

void decode(const json& j, TerrainSettings& v) {
  v.name = toString(req(j, "name"));
  v.center = toVec3d(req(j, "center"));
  v.width = toDouble(req(j, "width"));
  v.depth = toDouble(req(j, "depth"));
  v.nx = toInt(req(j, "nx"));
  v.ny = toInt(req(j, "ny"));
  v.heights = toDoubles(req(j, "heights"));
  v.heightScale = toDouble(req(j, "heightScale"));
  v.floor = toDouble(req(j, "floor"));
  v.noise.reset();
  if (auto p = opt(j, "noise")) {
    TerrainNoise n;
    decode(*p, n);
    v.noise = n;
  }
  v.noiseHeight = toDouble(req(j, "noiseHeight"));
}

JValue encode(const ScatterSettings& v) {
  JValue o = JValue::object();
  o.set("count", v.count);
  o.set("seed", v.seed);
  o.set("minScale", v.minScale);
  o.set("maxScale", v.maxScale);
  o.set("spacing", v.spacing);
  o.set("maxSlope", v.maxSlope);
  o.set("sink", v.sink);
  o.set("randomRotation", v.randomRotation);
  o.set("alignToSurface", v.alignToSurface);
  o.set("keepOriginal", v.keepOriginal);
  return o;
}

void decode(const json& j, ScatterSettings& v) {
  v.count = toInt(req(j, "count"));
  v.seed = toInt(req(j, "seed"));
  v.minScale = toDouble(req(j, "minScale"));
  v.maxScale = toDouble(req(j, "maxScale"));
  v.spacing = toDouble(req(j, "spacing"));
  v.maxSlope = toDouble(req(j, "maxSlope"));
  v.sink = toDouble(req(j, "sink"));
  v.randomRotation = toBool(req(j, "randomRotation"));
  v.alignToSurface = toBool(req(j, "alignToSurface"));
  v.keepOriginal = toBool(req(j, "keepOriginal"));
}

// MARK: Operation

namespace {

CombineMode toMode(const json& v) {
  auto m = combineMode(toString(v));
  if (!m) throw CodingError("Cannot initialize CombineMode from invalid String value " + v.dump());
  return *m;
}
template <class T>
T dec(const json& j) {
  T t;
  decode(j, t);
  return t;
}

}  // namespace

JValue encode(const Operation& op) {
  JValue o = JValue::object();
  if (auto x = op.as<op::Primitive>()) {
    o.set("kind", rawValue(x->kind));
    o.set("dims", arr(x->dims));
    o.set("position", arr(x->position));
  } else if (auto x = op.as<op::Extrude>()) {
    o.set("sketch", encode(x->sketch));
    o.set("distance", x->distance);
    o.set("mode", rawValue(x->mode));
    setIf(o, "target", x->target);
  } else if (auto x = op.as<op::Revolve>()) {
    o.set("sketch", encode(x->sketch));
    o.set("angle", x->angle);
    o.set("aroundV", x->aroundV);
    o.set("mode", rawValue(x->mode));
    setIf(o, "target", x->target);
  } else if (auto x = op.as<op::PushPull>()) {
    o.set("body", x->body);
    o.set("face", x->face);
    o.set("distance", x->distance);
  } else if (auto x = op.as<op::Boolean>()) {
    o.set("kind", rawValue(x->kind));
    o.set("target", x->target);
    o.set("tool", x->tool);
  } else if (auto x = op.as<op::Fillet>()) {
    o.set("body", x->body);
    o.set("edges", arr(x->edges));
    o.set("radius", x->radius);
  } else if (auto x = op.as<op::Chamfer>()) {
    o.set("body", x->body);
    o.set("edges", arr(x->edges));
    o.set("distance", x->distance);
  } else if (auto x = op.as<op::Shell>()) {
    o.set("body", x->body);
    o.set("faces", arr(x->faces));
    o.set("thickness", x->thickness);
  } else if (auto x = op.as<op::Move>()) {
    o.set("body", x->body);
    o.set("translation", arr(x->translation));
    o.set("axis", arr(x->axis));
    o.set("angle", x->angle);
  } else if (auto x = op.as<op::Mirror>()) {
    o.set("body", x->body);
    o.set("plane", encode(x->plane));
    o.set("keepOriginal", x->keepOriginal);
  } else if (auto x = op.as<op::Duplicate>()) {
    o.set("body", x->body);
  } else if (auto x = op.as<op::Delete>()) {
    o.set("body", x->body);
  } else if (auto x = op.as<op::Imported>()) {
    o.set("name", x->name);
    o.set("brep", x->brep);
  } else if (auto x = op.as<op::SketchItem>()) {
    o.set("sketch", encode(x->sketch));
    o.set("name", x->name);
  } else if (auto x = op.as<op::Plane>()) {
    o.set("base", encode(x->base));
    o.set("offset", x->offset);
    o.set("angle", x->angle);
    o.set("name", x->name);
  } else if (auto x = op.as<op::Sweep>()) {
    o.set("profile", x->profile.string());
    o.set("path", x->path.string());
    o.set("mode", rawValue(x->mode));
    setIf(o, "target", x->target);
  } else if (auto x = op.as<op::Loft>()) {
    o.set("sections", arr(x->sections));
    o.set("mode", rawValue(x->mode));
    setIf(o, "target", x->target);
  } else if (auto x = op.as<op::Scale>()) {
    o.set("body", x->body);
    o.set("factor", x->factor);
    o.set("center", arr(x->center));
  } else if (auto x = op.as<op::LinearPattern>()) {
    o.set("body", x->body);
    o.set("direction", arr(x->direction));
    o.set("count", x->count);
    o.set("spacing", x->spacing);
    o.set("fuse", x->fuse);
  } else if (auto x = op.as<op::CircularPattern>()) {
    o.set("body", x->body);
    o.set("origin", arr(x->origin));
    o.set("axis", arr(x->axis));
    o.set("count", x->count);
    o.set("angle", x->angle);
    o.set("fuse", x->fuse);
  } else if (auto x = op.as<op::Split>()) {
    o.set("body", x->body);
    o.set("plane", encode(x->plane));
    o.set("offset", x->offset);
  } else if (auto x = op.as<op::DeleteFaces>()) {
    o.set("body", x->body);
    o.set("faces", arr(x->faces));
  } else if (auto x = op.as<op::Draft>()) {
    o.set("body", x->body);
    o.set("faces", arr(x->faces));
    o.set("pull", arr(x->pull));
    o.set("angle", x->angle);
  } else if (auto x = op.as<op::OffsetFaces>()) {
    o.set("body", x->body);
    o.set("faces", arr(x->faces));
    o.set("distance", x->distance);
  } else if (auto x = op.as<op::Axis>()) {
    o.set("origin", arr(x->origin));
    o.set("direction", arr(x->direction));
    o.set("name", x->name);
  } else if (auto x = op.as<op::Translate>()) {
    o.set("body", x->body);
    o.set("direction", arr(x->direction));
    o.set("distance", x->distance);
  } else if (auto x = op.as<op::Rotate>()) {
    o.set("body", x->body);
    o.set("origin", arr(x->origin));
    o.set("axis", arr(x->axis));
    o.set("angle", x->angle);
  } else if (auto x = op.as<op::Align>()) {
    o.set("body", x->body);
    o.set("target", x->target);
    o.set("modes", arr(x->modes));
  } else if (auto x = op.as<op::Image>()) {
    o.set("image", encode(x->image));
  } else if (auto x = op.as<op::ReplaceFace>()) {
    o.set("body", x->body);
    o.set("face", x->face);
    o.set("targetBody", x->targetBody);
    o.set("targetFace", x->targetFace);
  } else if (auto x = op.as<op::Fill>()) {
    o.set("body", x->body);
    o.set("edges", arr(x->edges));
  } else if (auto x = op.as<op::Projection>()) {
    o.set("body", x->body);
    o.set("plane", encode(x->plane));
    o.set("name", x->name);
  } else if (auto x = op.as<op::Emboss>()) {
    o.set("sketch", x->sketch.string());
    o.set("body", x->body);
    o.set("height", x->height);
    o.set("deboss", x->deboss);
  } else if (auto x = op.as<op::ScaleAxes>()) {
    o.set("body", x->body);
    o.set("factors", arr(x->factors));
    o.set("center", arr(x->center));
    o.set("uniform", x->uniform);
  } else if (auto x = op.as<op::Scatter>()) {
    o.set("source", x->source);
    o.set("target", x->target);
    o.set("avoid", arr(x->avoid));
    o.set("settings", encode(x->settings));
  } else if (auto x = op.as<op::Terrain>()) {
    o.set("settings", encode(x->settings));
  }
  JValue outer = JValue::object();
  outer.set(op.caseName(), std::move(o));
  return outer;
}

void decode(const json& j, Operation& v) {
  if (!j.is_object() || j.size() != 1) throw CodingError("Invalid number of keys found, expected one.");
  const std::string c = j.begin().key();
  const json& b = j.begin().value();
  if (!b.is_object()) throw CodingError("Expected a dictionary for the associated values of " + c);
  auto I = [&](const char* k) { return toInt(req(b, k)); };
  auto D = [&](const char* k) { return toDouble(req(b, k)); };
  auto B = [&](const char* k) { return toBool(req(b, k)); };
  auto S = [&](const char* k) { return toString(req(b, k)); };
  auto V = [&](const char* k) { return toVec3d(req(b, k)); };
  auto Is = [&](const char* k) { return toInts(req(b, k)); };
  auto U = [&](const char* k) { return toUuid(req(b, k)); };
  auto Pl = [&](const char* k) { return dec<SketchPlane>(req(b, k)); };
  auto Sk = [&](const char* k) { return dec<Sketch>(req(b, k)); };
  auto M = [&](const char* k) { return toMode(req(b, k)); };
  auto T = [&]() { return optInt(b, "target"); };

  if (c == "primitive") {
    auto k = primitiveKind(S("kind"));
    if (!k) throw CodingError("Cannot initialize PrimitiveKind from invalid String value");
    v = op::Primitive{*k, V("dims"), V("position")};
  } else if (c == "extrude") {
    v = op::Extrude{Sk("sketch"), D("distance"), M("mode"), T()};
  } else if (c == "revolve") {
    v = op::Revolve{Sk("sketch"), D("angle"), B("aroundV"), M("mode"), T()};
  } else if (c == "pushPull") {
    v = op::PushPull{I("body"), I("face"), D("distance")};
  } else if (c == "boolean") {
    auto k = booleanKind(S("kind"));
    if (!k) throw CodingError("Cannot initialize BooleanKind from invalid String value");
    v = op::Boolean{*k, I("target"), I("tool")};
  } else if (c == "fillet") {
    v = op::Fillet{I("body"), Is("edges"), D("radius")};
  } else if (c == "chamfer") {
    v = op::Chamfer{I("body"), Is("edges"), D("distance")};
  } else if (c == "shell") {
    v = op::Shell{I("body"), Is("faces"), D("thickness")};
  } else if (c == "move") {
    v = op::Move{I("body"), V("translation"), V("axis"), D("angle")};
  } else if (c == "mirror") {
    v = op::Mirror{I("body"), Pl("plane"), B("keepOriginal")};
  } else if (c == "duplicate") {
    v = op::Duplicate{I("body")};
  } else if (c == "delete") {
    v = op::Delete{I("body")};
  } else if (c == "imported") {
    v = op::Imported{S("name"), S("brep")};
  } else if (c == "sketch") {
    v = op::SketchItem{Sk("sketch"), S("name")};
  } else if (c == "plane") {
    v = op::Plane{Pl("base"), D("offset"), D("angle"), S("name")};
  } else if (c == "sweep") {
    v = op::Sweep{U("profile"), U("path"), M("mode"), T()};
  } else if (c == "loft") {
    v = op::Loft{toUuids(req(b, "sections")), M("mode"), T()};
  } else if (c == "scale") {
    v = op::Scale{I("body"), D("factor"), V("center")};
  } else if (c == "linearPattern") {
    v = op::LinearPattern{I("body"), V("direction"), I("count"), D("spacing"), B("fuse")};
  } else if (c == "circularPattern") {
    v = op::CircularPattern{I("body"), V("origin"), V("axis"), I("count"), D("angle"), B("fuse")};
  } else if (c == "split") {
    v = op::Split{I("body"), Pl("plane"), D("offset")};
  } else if (c == "deleteFaces") {
    v = op::DeleteFaces{I("body"), Is("faces")};
  } else if (c == "draft") {
    v = op::Draft{I("body"), Is("faces"), V("pull"), D("angle")};
  } else if (c == "offsetFaces") {
    v = op::OffsetFaces{I("body"), Is("faces"), D("distance")};
  } else if (c == "axis") {
    v = op::Axis{V("origin"), V("direction"), S("name")};
  } else if (c == "translate") {
    v = op::Translate{I("body"), V("direction"), D("distance")};
  } else if (c == "rotate") {
    v = op::Rotate{I("body"), V("origin"), V("axis"), D("angle")};
  } else if (c == "align") {
    v = op::Align{I("body"), I("target"), Is("modes")};
  } else if (c == "image") {
    v = op::Image{dec<ReferenceImage>(req(b, "image"))};
  } else if (c == "replaceFace") {
    v = op::ReplaceFace{I("body"), I("face"), I("targetBody"), I("targetFace")};
  } else if (c == "fill") {
    v = op::Fill{I("body"), Is("edges")};
  } else if (c == "projection") {
    v = op::Projection{I("body"), Pl("plane"), S("name")};
  } else if (c == "emboss") {
    v = op::Emboss{U("sketch"), I("body"), D("height"), B("deboss")};
  } else if (c == "scaleAxes") {
    v = op::ScaleAxes{I("body"), V("factors"), V("center"), B("uniform")};
  } else if (c == "scatter") {
    v = op::Scatter{I("source"), I("target"), Is("avoid"), dec<ScatterSettings>(req(b, "settings"))};
  } else if (c == "terrain") {
    v = op::Terrain{dec<TerrainSettings>(req(b, "settings"))};
  } else {
    throw CodingError("Unknown operation \"" + c + "\"");
  }
}

// MARK: Feature

JValue encode(const Feature& v) {
  JValue o = JValue::object();
  o.set("id", v.id.string());
  o.set("op", encode(v.op));
  o.set("suppressed", v.suppressed);
  if (v.bindings) {
    JValue b = JValue::object();
    for (auto& [k, e] : *v.bindings) b.set(std::to_string(k), e);
    o.set("bindings", b);
  }
  return o;
}

void decode(const json& j, Feature& v) {
  v.id = toUuid(req(j, "id"));
  decode(req(j, "op"), v.op);
  v.suppressed = toBool(req(j, "suppressed"));
  v.bindings.reset();
  if (auto p = opt(j, "bindings")) {
    if (!p->is_object()) throw CodingError("Expected a dictionary for bindings");
    std::map<int, std::string> m;
    for (auto it = p->begin(); it != p->end(); ++it) m[intKey(it.key())] = toString(it.value());
    v.bindings = m;
  }
}

// MARK: Settings

JValue encode(const SnapSettings& v) {
  JValue o = JValue::object();
  o.set("grid", v.grid);
  o.set("guideLines", v.guideLines);
  o.set("guidePoints", v.guidePoints);
  o.set("points3D", v.points3D);
  o.set("distantEdges", v.distantEdges);
  o.set("showGuidePoints", v.showGuidePoints);
  o.set("showHints", v.showHints);
  return o;
}

void decode(const json& j, SnapSettings& v) {
  v.grid = toBool(req(j, "grid"));
  v.guideLines = toBool(req(j, "guideLines"));
  v.guidePoints = toBool(req(j, "guidePoints"));
  v.points3D = toBool(req(j, "points3D"));
  v.distantEdges = toBool(req(j, "distantEdges"));
  v.showGuidePoints = toBool(req(j, "showGuidePoints"));
  v.showHints = toBool(req(j, "showHints"));
}

JValue encode(const Variable& v) {
  JValue o = JValue::object();
  o.set("id", v.id.string());
  o.set("name", v.name);
  o.set("expression", v.expression);
  return o;
}

void decode(const json& j, Variable& v) {
  v.id = toUuid(req(j, "id"));
  v.name = toString(req(j, "name"));
  v.expression = toString(req(j, "expression"));
}

JValue encode(const ProjectSettings& v) {
  JValue o = JValue::object();
  o.set("units", rawValue(v.units));
  o.set("snapping", v.snapping);
  JValue names = JValue::object();
  for (auto& [k, n] : v.bodyNames) names.set(std::to_string(k), n);
  o.set("bodyNames", names);
  JValue looks = JValue::object();
  for (auto& [k, a] : v.appearance) looks.set(std::to_string(k), encode(a));
  o.set("appearance", looks);
  // Swift writes sets in hash order; sorted is as valid and deterministic.
  JValue hidden = JValue::array();
  for (int b : v.hiddenBodies) hidden.push(b);
  o.set("hiddenBodies", hidden);
  JValue items = JValue::array();
  for (auto& u : v.hiddenItems) items.push(u.string());
  o.set("hiddenItems", items);
  o.set("environment", encode(v.environment));
  o.set("camera", encode(v.camera));
  o.set("snap", encode(v.snap));
  o.set("variables", list(v.variables));
  return o;
}

void decode(const json& j, ProjectSettings& v) {
  v = ProjectSettings();
  if (!j.is_object()) throw CodingError("Expected a dictionary for ProjectSettings");
  if (auto p = opt(j, "units")) {
    auto u = lengthUnit(toString(*p));
    if (!u) throw CodingError("Cannot initialize LengthUnit from invalid String value " + p->dump());
    v.units = *u;
  }
  if (auto p = opt(j, "snapping")) v.snapping = toBool(*p);
  if (auto p = opt(j, "bodyNames")) {
    if (!p->is_object()) throw CodingError("Expected a dictionary for bodyNames");
    for (auto it = p->begin(); it != p->end(); ++it) v.bodyNames[intKey(it.key())] = toString(it.value());
  }
  if (auto p = opt(j, "appearance")) {
    if (!p->is_object()) throw CodingError("Expected a dictionary for appearance");
    for (auto it = p->begin(); it != p->end(); ++it) decode(it.value(), v.appearance[intKey(it.key())]);
  }
  if (auto p = opt(j, "hiddenBodies"))
    for (int b : toInts(*p)) v.hiddenBodies.insert(b);
  if (auto p = opt(j, "hiddenItems"))
    for (auto& u : toUuids(*p)) v.hiddenItems.insert(u);
  if (auto p = opt(j, "environment")) decode(*p, v.environment);
  if (auto p = opt(j, "camera")) decode(*p, v.camera);
  if (auto p = opt(j, "snap")) decode(*p, v.snap);
  if (auto p = opt(j, "variables")) v.variables = toList<Variable>(*p);
}

}  // namespace sculpt
