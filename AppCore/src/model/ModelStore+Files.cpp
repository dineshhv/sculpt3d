// Port of ModelStore+Files.swift: File ▸ Import Material and Import Image as Decal.
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

#include "sculpt/model/Image.hpp"
#include "sculpt/model/ModelStore.hpp"

namespace sculpt {

void ModelStore::importMaterial(const std::string& jsonText, const std::string& fileName) {
  nlohmann::json json;
  try {
    json = nlohmann::json::parse(jsonText);
  } catch (...) {
    json = nullptr;
  }
  if (!json.is_object()) return setToast(std::string("That isn't a Sculpt3D material file (JSON)"));
  Appearance look;
  auto color = json.find("color");
  if (color != json.end() && color->is_string() && ModelStore::color(color->get<std::string>())) {
    look.color = *ModelStore::color(color->get<std::string>());
  } else if (color != json.end() && color->is_array() && color->size() >= 3 &&
             std::all_of(color->begin(), color->end(), [](auto& v) { return v.is_number(); })) {
    double rgb[3] = {(*color)[0].get<double>(), (*color)[1].get<double>(), (*color)[2].get<double>()};
    double scale = (rgb[0] > 1 || rgb[1] > 1 || rgb[2] > 1) ? 255.0 : 1;
    look.color = {float(rgb[0] / scale), float(rgb[1] / scale), float(rgb[2] / scale)};
  }
  auto number = [&](std::initializer_list<const char*> keys) -> std::optional<float> {
    for (auto k : keys) {
      auto it = json.find(k);
      if (it != json.end() && it->is_number()) return float(it->get<double>());
    }
    return std::nullopt;
  };
  look.metallic = number({"metallic", "metalness"}).value_or(look.metallic);
  look.roughness = number({"roughness"}).value_or(look.roughness);
  look.opacity = number({"opacity"}).value_or(look.opacity);
  auto bodies = selection_.empty() ? bodyIds() : touchedBodies();
  if (bodies.empty()) return setToast(std::string("Add a body to apply the material to"));
  setAppearance(look, bodies);
  auto nameIt = json.find("name");
  std::string name = nameIt != json.end() && nameIt->is_string() ? nameIt->get<std::string>() : fileName;
  setToast("Applied " + name + " to " + (bodies.size() == 1 ? std::string("the body") : std::to_string(bodies.size()) + " bodies"));
}

std::optional<Vec3f> ModelStore::color(const std::string& hex) {
  std::string s = hex;
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  if (!s.empty() && s[0] == '#') s.erase(s.begin());
  if (s.size() != 6) return std::nullopt;
  int v = 0;
  for (char c : s) {
    int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    if (d < 0) return std::nullopt;
    v = v * 16 + d;
  }
  return Vec3f(float((v >> 16) & 255) / 255, float((v >> 8) & 255) / 255, float(v & 255) / 255);
}

void ModelStore::insertDecal(const std::vector<uint8_t>& imageBytes, const std::string& name) {
  auto faces = selectedFaces();
  std::optional<o3d::FaceInfo> info;
  if (!faces.empty()) info = faceInfo(faces[0].first, faces[0].second);
  if (!info || !info->planar) return setToast(std::string("Select a flat face for the decal"));
  auto image = decodeImage(imageBytes);
  if (!image) return setToast(std::string("Couldn't read that image"));
  auto png = encodePNG(*image);
  if (png.empty()) return setToast(std::string("Couldn't read that image"));
  // Sized to most of the face, never larger than it.
  double side = std::max(std::sqrt(info->area) * 0.6, 1.0);
  double aspect = double(image->height) / double(std::max(image->width, 1));
  Vec3d normal = fromO3d(info->normal);
  SketchPlane plane{fromO3d(info->centroid) + normal * 0.02, normal, fromO3d(info->xDir)};
  ReferenceImage ref{name, png, plane, side, side * aspect, 1, true};
  selection_.clear();
  begin(op::Image{ref});
}

}  // namespace sculpt
