// Port of Sculpt3DFile.swift.
#include "sculpt/model/Sculpt3DFile.hpp"

#include "sculpt/model/Codec.hpp"

namespace sculpt {

Sculpt3DFile Sculpt3DFile::decode(const std::string& data) {
  auto j = nlohmann::json::parse(data);
  if (!j.is_object()) throw CodingError("Expected a dictionary at the top of the document");
  // Payload's synthesized Decodable: version and features are required.
  auto version = j.find("version");
  if (version == j.end() || !version->is_number_integer())
    throw CodingError("No value associated with key \"version\"");
  auto features = j.find("features");
  if (features == j.end() || !features->is_array()) throw CodingError("No value associated with key \"features\"");
  Sculpt3DFile file;
  for (auto& f : *features) {
    Feature feature;
    sculpt::decode(f, feature);
    file.features.push_back(std::move(feature));
  }
  auto settings = j.find("settings");
  if (settings != j.end() && !settings->is_null()) sculpt::decode(*settings, file.settings);
  return file;
}

std::string Sculpt3DFile::encoded() const {
  JValue root = JValue::object();
  root.set("version", 2);
  JValue list = JValue::array();
  for (auto& f : features) list.push(encode(f));
  root.set("features", list);
  root.set("settings", encode(settings));
  return root.dump(true);
}

}  // namespace sculpt
