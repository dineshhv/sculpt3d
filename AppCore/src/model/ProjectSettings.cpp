// Port of ProjectSettings.swift.
#include "sculpt/model/ProjectSettings.hpp"

#include <cmath>

namespace sculpt {

const std::vector<LengthUnit>& allLengthUnits() {
  static const std::vector<LengthUnit> all = {LengthUnit::mm, LengthUnit::cm, LengthUnit::m, LengthUnit::inch,
                                              LengthUnit::ft};
  return all;
}

std::string rawValue(LengthUnit u) {
  switch (u) {
    case LengthUnit::mm: return "mm";
    case LengthUnit::cm: return "cm";
    case LengthUnit::m: return "m";
    case LengthUnit::inch: return "inch";
    case LengthUnit::ft: return "ft";
  }
  return "mm";
}

std::optional<LengthUnit> lengthUnit(const std::string& raw) {
  for (auto u : allLengthUnits())
    if (rawValue(u) == raw) return u;
  return std::nullopt;
}

std::string symbol(LengthUnit u) { return u == LengthUnit::inch ? "in" : rawValue(u); }

std::string title(LengthUnit u) {
  switch (u) {
    case LengthUnit::mm: return "Millimeters";
    case LengthUnit::cm: return "Centimeters";
    case LengthUnit::m: return "Meters";
    case LengthUnit::inch: return "Inches";
    case LengthUnit::ft: return "Feet";
  }
  return "";
}

bool isImperial(LengthUnit u) { return u == LengthUnit::inch || u == LengthUnit::ft; }

double scale(LengthUnit u) {
  switch (u) {
    case LengthUnit::mm: return 1;
    case LengthUnit::cm: return 10;
    case LengthUnit::m: return 1000;
    case LengthUnit::inch: return 25.4;
    case LengthUnit::ft: return 304.8;
  }
  return 1;
}

std::string ProjectSettings::name(int body) const {
  auto it = bodyNames.find(body);
  return it != bodyNames.end() ? it->second : "Body " + std::to_string(body);
}

SketchPlane ReferenceItem::plane() const {
  if (auto s = std::get_if<Sketch>(&content)) return s->plane;
  if (auto p = std::get_if<SketchPlane>(&content)) return *p;
  if (auto i = std::get_if<ReferenceImage>(&content)) return i->plane;
  const auto& a = std::get<Axis>(content);
  // A plane the axis is normal to, for sketching around it.
  Vec3d d = normalize(a.direction);
  Vec3d x = normalize(cross(std::abs(d.z) < 0.9 ? Vec3d(0, 0, 1) : Vec3d(1, 0, 0), d));
  return SketchPlane{a.origin, d, x};
}

}  // namespace sculpt
