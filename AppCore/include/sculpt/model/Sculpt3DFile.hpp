// Port of Sculpt3DFile.swift: the native document, the feature history and
// settings as `{version: 2, features, settings}` JSON, written exactly as
// Swift's JSONEncoder (.prettyPrinted, .sortedKeys) writes it.
#pragma once

#include <string>
#include <vector>

#include "sculpt/model/Feature.hpp"
#include "sculpt/model/ProjectSettings.hpp"

namespace sculpt {

struct Sculpt3DFile {
  std::vector<Feature> features;
  ProjectSettings settings;

  /// Parses a .sculpt3d (or legacy .o3d) document; throws CodingError (or
  /// nlohmann::json::parse_error) on data the Apple app couldn't open either.
  static Sculpt3DFile decode(const std::string& data);
  /// The document bytes (UTF-8 JSON, no trailing newline).
  std::string encoded() const;
};

}  // namespace sculpt
