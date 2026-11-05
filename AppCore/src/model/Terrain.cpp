// Port of Terrain.swift.
#include "sculpt/model/Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sculpt {

std::vector<double> TerrainSettings::topHeights() const {
  std::vector<double> out;
  out.reserve(heights.size());
  for (double h : heights) out.push_back(center.z + floor + std::max(h, 0.0) * heightScale);
  return out;
}

void TerrainSettings::regenerate() {
  if (!noise) return;
  auto f = Terrain::noiseField(nx, ny, depth / std::max(width, 1e-6), *noise);
  heights.clear();
  for (double v : f) heights.push_back(v * noiseHeight);
}

namespace Terrain {

static double hash(int64_t x, int64_t y, int64_t seed) {
  // Swift's wrapping Int (64-bit) arithmetic.
  uint64_t v = uint64_t(x) * 374761393ULL + uint64_t(y) * 668265263ULL + uint64_t(seed) * 2147483647ULL;
  uint64_t h = v;
  h = (h ^ (h >> 13)) * 1274126177ULL;
  h ^= h >> 16;
  return double(h & 0xFFFFFF) / double(0xFFFFFF);
}

static double valueNoise(double x, double y, int64_t seed) {
  int64_t xi = int64_t(std::floor(x)), yi = int64_t(std::floor(y));
  double fx = x - std::floor(x), fy = y - std::floor(y);
  double sx = fx * fx * fx * (fx * (fx * 6 - 15) + 10), sy = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
  double a = hash(xi, yi, seed), b = hash(xi + 1, yi, seed), c = hash(xi, yi + 1, seed), d = hash(xi + 1, yi + 1, seed);
  return (a + (b - a) * sx) * (1 - sy) + (c + (d - c) * sx) * sy;
}

std::vector<double> noiseField(int nx, int ny, double aspect, const TerrainNoise& n) {
  double freq = 1 / std::max(n.scale, 0.02);
  std::vector<double> out(size_t(std::max(nx * ny, 0)), 0.0);
  for (int j = 0; j < ny; ++j) {
    for (int i = 0; i < nx; ++i) {
      double u = double(i) / double(std::max(nx - 1, 1)), v = double(j) / double(std::max(ny - 1, 1)) * aspect;
      double amp = 1.0, f = freq, sum = 0.0, norm = 0.0;
      for (int o = 0; o < std::max(n.octaves, 1); ++o) {
        sum += valueNoise(u * f + 17.3 * double(o), v * f - 9.1 * double(o), int64_t(n.seed) + int64_t(o) * 101) * amp;
        norm += amp;
        amp *= n.roughness;
        f *= 2.03;
      }
      out[size_t(j * nx + i)] = sum / norm;
    }
  }
  if (out.empty()) return out;
  double lo = *std::min_element(out.begin(), out.end()), hi = *std::max_element(out.begin(), out.end());
  for (double& v : out) v = (v - lo) / std::max(hi - lo, 1e-9);
  return out;
}

std::vector<double> field(const std::vector<uint8_t>& gray, int width, int height, int nx, int ny) {
  std::vector<double> out(size_t(std::max(nx * ny, 0)), 0.0);
  if (width <= 0 || height <= 0 || gray.size() < size_t(width) * size_t(height)) return out;
  // Box-filter each target cell over the source pixels it covers (CoreGraphics'
  // high-quality interpolation when shrinking), bilinear when enlarging.
  for (int j = 0; j < ny; ++j) {
    int row = ny - 1 - j;  // bitmap rows start at the top; terrain rows at -Y
    for (int i = 0; i < nx; ++i) {
      double x0 = double(i) * width / nx, x1 = double(i + 1) * width / nx;
      double y0 = double(row) * height / ny, y1 = double(row + 1) * height / ny;
      double sum = 0, weight = 0;
      if (x1 - x0 >= 1 && y1 - y0 >= 1) {
        for (int y = int(y0); y < int(std::ceil(y1)) && y < height; ++y)
          for (int x = int(x0); x < int(std::ceil(x1)) && x < width; ++x) {
            sum += gray[size_t(y) * width + x];
            weight += 1;
          }
      } else {
        double cx = std::min(std::max((x0 + x1) / 2 - 0.5, 0.0), double(width - 1));
        double cy = std::min(std::max((y0 + y1) / 2 - 0.5, 0.0), double(height - 1));
        int ix = int(cx), iy = int(cy);
        int jx = std::min(ix + 1, width - 1), jy = std::min(iy + 1, height - 1);
        double fx = cx - ix, fy = cy - iy;
        auto px = [&](int x, int y) { return double(gray[size_t(y) * width + x]); };
        sum = (px(ix, iy) * (1 - fx) + px(jx, iy) * fx) * (1 - fy) + (px(ix, jy) * (1 - fx) + px(jx, jy) * fx) * fy;
        weight = 1;
      }
      out[size_t(j * nx + i)] = weight > 0 ? std::round(sum / weight) / 255 : 0;
    }
  }
  return out;
}

double distance(const Vec2d& p, const std::vector<Vec2d>& line) {
  if (line.size() <= 1)
    return line.empty() ? std::numeric_limits<double>::infinity() : sculpt::distance(line[0], p);
  double best = std::numeric_limits<double>::infinity();
  for (size_t k = 0; k + 1 < line.size(); ++k) {
    Vec2d a = line[k], ab = line[k + 1] - a;
    double t = std::min(std::max(dot(p - a, ab) / std::max(lengthSquared(ab), 1e-12), 0.0), 1.0);
    best = std::min(best, sculpt::distance(p, a + ab * t));
  }
  return best;
}

Vec2d point(int i, int j, const TerrainSettings& t) {
  return {t.center.x - t.width / 2 + t.width * double(i) / double(t.nx - 1),
          t.center.y - t.depth / 2 + t.depth * double(j) / double(t.ny - 1)};
}

double smoothstep(double e0, double e1, double x) {
  double t = std::min(std::max((x - e0) / (e1 - e0), 0.0), 1.0);
  return t * t * (3 - 2 * t);
}

void add(const std::vector<Hill>& hills, TerrainSettings& t) {
  for (int j = 0; j < t.ny; ++j)
    for (int i = 0; i < t.nx; ++i) {
      Vec2d p = point(i, j, t);
      for (auto& h : hills) {
        if (!(h.radius > 0)) continue;
        double r = sculpt::distance(p, Vec2d(h.x, h.y)) / h.radius;
        if (r < 1) t.heights[size_t(j * t.nx + i)] += h.height * 0.5 * (1 + std::cos(kPi * r));
      }
    }
}

void terrace(TerrainSettings& t, int count, double softness) {
  if (count <= 1 || t.heights.empty()) return;
  double hi = *std::max_element(t.heights.begin(), t.heights.end());
  if (!(hi > 0)) return;
  double step = hi / double(count);
  double s = std::min(std::max(softness, 0.02), 1.0) * 0.5;
  for (double& h : t.heights) {
    double k = std::floor(h / step), f = h / step - k;
    h = (k + smoothstep(0.5 - s, 0.5 + s, f)) * step;
  }
}

void carve(const std::vector<Channel>& channels, TerrainSettings& t) {
  for (int j = 0; j < t.ny; ++j)
    for (int i = 0; i < t.nx; ++i) {
      Vec2d p = point(i, j, t);
      double h = t.heights[size_t(j * t.nx + i)];
      for (auto& c : channels) {
        double d = distance(p, c.points);
        double k = 1 - smoothstep(c.width / 2, c.width / 2 + std::max(c.bank, 1e-3), d);
        if (k > 0) h = h + (std::min(h, c.bed) - h) * k;
      }
      t.heights[size_t(j * t.nx + i)] = h;
    }
}

}  // namespace Terrain
}  // namespace sculpt
