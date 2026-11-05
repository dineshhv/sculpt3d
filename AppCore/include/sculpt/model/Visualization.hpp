// Port of Visualization.swift (data and presets) plus Appearance from
// ProjectSettings.swift, which the material library needs.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sculpt/model/Equatable.hpp"
#include "sculpt/model/Math.hpp"
#include "sculpt/model/Sketch.hpp"

namespace sculpt {

/// How a body looks: base colour plus physically based material values.
struct Appearance {
  Vec3f color{0.72f, 0.75f, 0.8f};
  float metallic = 0;
  float roughness = 0.45f;
  float opacity = 1;
  /// The library preset this came from, for the material list.
  std::optional<std::string> preset;
  SCULPT_FIELDS(color, metallic, roughness, opacity, preset)

  static const std::vector<Vec3f>& palette();
};

/// A material from the Visualize library.
struct MaterialPreset {
  std::string id;
  std::string family;  // Plastic, Metal, Glass, Rubber, Wood, Ceramic
  std::string finish;  // Glossy, Matte, Polished, Brushed, …
  std::string name;    // ABS, Iron, Aluminium, …
  Appearance appearance;
  SCULPT_FIELDS(id, family, finish, name, appearance)

  std::string title() const { return finish + " " + name; }

  /// Families in display order.
  static const std::vector<std::string>& families();
  /// The Visualize material library. Ids are stable: documents store them in `Appearance::preset`.
  static const std::vector<MaterialPreset>& library();
  static const MaterialPreset* preset(const std::string& id);
  /// Library presets of one family, in library order.
  static std::vector<MaterialPreset> presets(const std::string& family);
  /// The material panel's label for an appearance (VisualizationPanel.swift `label(for:)`).
  static std::pair<std::string, std::string> label(const Appearance& a);
  /// Library presets whose "family finish name" contains `query` (case-insensitive).
  static std::vector<MaterialPreset> search(const std::string& query);
};

/// Scene background and lighting for Visualize mode and screenshots.
struct EnvironmentSettings {
  Vec3f background{0.90f, 0.91f, 0.93f};
  /// Blend the background towards a lighter top for a studio look.
  bool gradient = true;
  bool showGrid = true;
  float lightIntensity = 1;
  /// Key light direction around the vertical axis, in degrees.
  float lightAzimuth = 35;
  bool groundShadow = true;
  /// Key light height above the ground, in degrees.
  float lightElevation = 50;
  /// Bodies cast real shadows on each other and on the floor (Visualized).
  bool castShadows = true;
  /// 0 crisp to 1 very soft shadow edges.
  float shadowSoftness = 0.45f;
  /// Darkens creases, contacts and crevices (Visualized).
  bool ambientOcclusion = true;
  float occlusionStrength = 1;
  /// Blends normals across faces that meet at shallow angles (Visualized).
  bool smoothShading = true;
  SCULPT_FIELDS(background, gradient, showGrid, lightIntensity, lightAzimuth, groundShadow, lightElevation, castShadows,
                shadowSoftness, ambientOcclusion, occlusionStrength, smoothShading)

  /// Background swatches of the environment panel (VisualizationPanel.swift).
  static const std::vector<Vec3f>& backgrounds();
};

struct CameraSettings {
  bool orthographic = false;
  /// Vertical field of view in degrees (perspective only).
  float fieldOfView = 40;
  SCULPT_FIELDS(orthographic, fieldOfView)
};

/// A reference image placed on a plane in the scene (Insert ▸ Image).
struct ReferenceImage {
  std::string name;
  /// PNG bytes (Swift `Data`, base64 in documents).
  std::vector<uint8_t> png;
  SketchPlane plane;
  double width = 0;
  double height = 0;
  double opacity = 0.8;
  /// A decal: an image stuck on a body face, drawn opaque.
  bool decal = false;
  SCULPT_FIELDS(name, png, plane, width, height, opacity, decal)
};

}  // namespace sculpt
