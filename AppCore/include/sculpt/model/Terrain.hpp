// Port of Terrain.swift (the ModelStore insert commands live in ModelStore).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sculpt/model/Equatable.hpp"
#include "sculpt/model/Math.hpp"

namespace sculpt {

/// Procedural rolling hills: fractal value noise.
struct TerrainNoise {
  int seed = 1;
  /// Size of the largest hills, as a fraction of the terrain's longer side.
  double scale = 0.45;
  /// Layers of finer detail on top of the big shapes.
  int octaves = 4;
  /// How much each finer layer adds (0.3 smooth – 0.7 rugged).
  double roughness = 0.5;
  SCULPT_FIELDS(seed, scale, octaves, roughness)
};

/// Insert ▸ Terrain: a smooth landscape solid built from a grid of heights.
struct TerrainSettings {
  std::string name = "Terrain";
  /// Centre of the bottom face.
  Vec3d center;
  double width = 200.0;
  double depth = 200.0;
  int nx = 48;
  int ny = 48;
  /// Top surface height above the bottom face, mm, row-major with rows along +Y.
  std::vector<double> heights;
  /// Multiplies the relief above `floor` (the inspector's Height %).
  double heightScale = 1.0;
  /// Solid thickness under the lowest point of the relief, mm.
  double floor = 10.0;
  /// When set, the heights are this noise and Seed/Roughness can be edited.
  std::optional<TerrainNoise> noise;
  /// Relief height of generated noise before heightScale, mm.
  double noiseHeight = 40.0;
  SCULPT_FIELDS(name, center, width, depth, nx, ny, heights, heightScale, floor, noise, noiseHeight)

  /// Absolute top heights handed to the kernel.
  std::vector<double> topHeights() const;
  /// Regenerates `heights` from `noise`.
  void regenerate();
};

namespace Terrain {

/// Fractal noise over the grid, normalised to 0…1.
std::vector<double> noiseField(int nx, int ny, double aspect, const TerrainNoise& n);

/// Brightness of an 8-bit grey image (`gray`, width × height, top row first)
/// resampled to nx × ny, 0 (black) … 1 (white); the image's top row is the
/// terrain's far (+Y) edge. Swift drew a CGImage into a grey bitmap context.
std::vector<double> field(const std::vector<uint8_t>& gray, int width, int height, int nx, int ny);

struct Hill {
  double x = 0, y = 0, radius = 0, height = 0;
};
/// A river or path bed: the surface within `width` of the polyline is lowered
/// to `bed` (height above the floor), with banks of `bank` mm easing back up.
struct Channel {
  std::vector<Vec2d> points;
  double width = 0, bed = 0, bank = 0;
};

/// Distance from p to a polyline.
double distance(const Vec2d& p, const std::vector<Vec2d>& line);
/// World XY of grid sample (i, j).
Vec2d point(int i, int j, const TerrainSettings& t);
double smoothstep(double e0, double e1, double x);
/// Adds smooth dome-shaped hills (cosine falloff) to the heights.
void add(const std::vector<Hill>& hills, TerrainSettings& t);
/// Rounds the relief into `count` flat terraces joined by soft steps.
void terrace(TerrainSettings& t, int count, double softness);
void carve(const std::vector<Channel>& channels, TerrainSettings& t);

}  // namespace Terrain

}  // namespace sculpt
