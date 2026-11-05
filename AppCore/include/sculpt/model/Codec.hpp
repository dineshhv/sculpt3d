// Swift Codable compatibility: a small JSON value that remembers whether a
// number was a Double, a Float or an Int, a writer that reproduces Swift's
// JSONEncoder (.prettyPrinted + .sortedKeys) byte for byte, and the
// encode/decode functions for every model type, following each type's
// synthesized or custom Codable conformance.
//
// Decoding takes any JSON (nlohmann::json) and throws CodingError on data
// Swift's JSONDecoder would reject.
#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "sculpt/model/Feature.hpp"
#include "sculpt/model/ProjectSettings.hpp"

namespace sculpt {

struct CodingError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

/// A JSON value as Swift's encoder sees it.
class JValue {
 public:
  enum class Type { Null, Bool, Int, Double, Float, String, Array, Object };

  JValue() = default;
  JValue(std::nullptr_t) {}
  JValue(bool b) : type_(Type::Bool), b_(b) {}
  JValue(int i) : type_(Type::Int), i_(i) {}
  JValue(long i) : type_(Type::Int), i_(int64_t(i)) {}
  JValue(long long i) : type_(Type::Int), i_(int64_t(i)) {}
  JValue(unsigned i) : type_(Type::Int), i_(int64_t(i)) {}
  JValue(unsigned long i) : type_(Type::Int), i_(int64_t(i)) {}
  JValue(unsigned long long i) : type_(Type::Int), i_(int64_t(i)) {}
  JValue(double d) : type_(Type::Double), d_(d) {}
  JValue(float f) : type_(Type::Float), d_(f) {}
  JValue(const char* s) : type_(Type::String), s_(s) {}
  JValue(std::string s) : type_(Type::String), s_(std::move(s)) {}

  static JValue array() { JValue v; v.type_ = Type::Array; return v; }
  static JValue object() { JValue v; v.type_ = Type::Object; return v; }

  Type type() const { return type_; }
  /// Arrays.
  void push(JValue v) { arr_.push_back(std::move(v)); }
  const std::vector<JValue>& items() const { return arr_; }
  /// Objects (keys are sorted when written, like `.sortedKeys`).
  JValue& operator[](const std::string& key) { return obj_[key]; }
  void set(const std::string& key, JValue v) { obj_[key] = std::move(v); }
  const std::map<std::string, JValue>& members() const { return obj_; }

  bool boolValue() const { return b_; }
  int64_t intValue() const { return i_; }
  double doubleValue() const { return type_ == Type::Int ? double(i_) : d_; }
  const std::string& stringValue() const { return s_; }

  /// Swift JSONEncoder output: `pretty` gives `.prettyPrinted` (2 spaces,
  /// `"key" : value`); keys are always sorted by UTF-8 bytes.
  std::string dump(bool pretty = true) const;
  nlohmann::json toJson() const;

 private:
  void write(std::string& out, bool pretty, int indent) const;
  Type type_ = Type::Null;
  bool b_ = false;
  int64_t i_ = 0;
  double d_ = 0;
  std::string s_;
  std::vector<JValue> arr_;
  std::map<std::string, JValue> obj_;
};

/// Swift's description of a Double / Float as JSONEncoder writes it
/// (shortest round-trip digits, "40" not "40.0", "1e-05", "1.2e+17").
std::string swiftNumber(double v);
std::string swiftNumber(float v);
/// JSON string literal with Swift's escaping ("/" as "\/").
std::string swiftString(const std::string& s);

std::string base64Encode(const std::vector<uint8_t>& data);
bool base64Decode(const std::string& text, std::vector<uint8_t>& out);

// MARK: Model types

JValue encode(const SketchPlane& v);
JValue encode(const Sketch& v);
JValue encode(const ReferenceImage& v);
JValue encode(const TerrainSettings& v);
JValue encode(const ScatterSettings& v);
JValue encode(const Operation& v);
JValue encode(const Feature& v);
JValue encode(const Appearance& v);
JValue encode(const EnvironmentSettings& v);
JValue encode(const CameraSettings& v);
JValue encode(const SnapSettings& v);
JValue encode(const Variable& v);
JValue encode(const ProjectSettings& v);

void decode(const nlohmann::json& j, SketchPlane& v);
void decode(const nlohmann::json& j, Sketch& v);
void decode(const nlohmann::json& j, ReferenceImage& v);
void decode(const nlohmann::json& j, TerrainSettings& v);
void decode(const nlohmann::json& j, ScatterSettings& v);
void decode(const nlohmann::json& j, Operation& v);
void decode(const nlohmann::json& j, Feature& v);
void decode(const nlohmann::json& j, Appearance& v);
void decode(const nlohmann::json& j, EnvironmentSettings& v);
void decode(const nlohmann::json& j, CameraSettings& v);
void decode(const nlohmann::json& j, SnapSettings& v);
void decode(const nlohmann::json& j, Variable& v);
void decode(const nlohmann::json& j, ProjectSettings& v);

}  // namespace sculpt
