#include "Overlays.hpp"

#include <algorithm>
#include <cmath>

namespace sculpt::viewport {

double gridStep(const Camera& camera, std::optional<double> fixedStep) {
  if (fixedStep) return *fixedStep;
  double span = double(camera.distance);
  return std::pow(10.0, std::floor(std::log10(std::max(span / 15, 0.01))));
}

namespace {
void addAxes(LineBatch& batch, float e) {
  batch.line({-e, 0, 0}, {e, 0, 0}, {0.85f, 0.25f, 0.25f, 0.6f});
  batch.line({0, -e, 0}, {0, e, 0}, {0.25f, 0.7f, 0.3f, 0.6f});
  batch.line({0, 0, 0}, {0, 0, e / 2}, {0.25f, 0.4f, 0.9f, 0.6f});
}
}  // namespace

LineBatch grid(const Camera& camera, const Plane& plane, double gridStepValue, bool sketching, bool showWorldAxes) {
  LineBatch batch;
  double step = sketching ? gridStepValue : std::max(gridStepValue, 1.0) * 10;
  // Enough lines to reach past the edges of the view at any zoom.
  double reach = double(camera.distance) * 1.5 / step;
  int count = std::isfinite(reach) ? std::min(std::max(20, int(std::ceil(reach))), 4000) : 20;
  double extent = double(count) * step;
  auto world = [&](double x, double y) { return plane.world({float(x), float(y)}); };
  for (int i = -count; i <= count; ++i) {
    double v = double(i) * step;
    bool major = i % 10 == 0;
    float alpha = sketching ? (major ? 0.35f : 0.16f) : (i == 0 ? 0.0f : 0.14f);
    Vec4f color{0.3f, 0.33f, 0.4f, alpha};
    batch.line(world(v, -extent), world(v, extent), color);
    batch.line(world(-extent, v), world(extent, v), color);
  }
  if (!sketching && showWorldAxes) addAxes(batch, float(extent));
  return batch;
}

LineBatch axesOnly(const Camera& camera, double gridStepValue) {
  LineBatch batch;
  addAxes(batch, std::max(float(gridStepValue) * 200, camera.distance * 1.5f));
  return batch;
}

std::vector<Vec3f> thickOffsets(const Camera& camera, Size2 size, float points) {
  float s = camera.worldPerPoint(size) * points;
  Vec3f r = camera.right() * s, u = camera.up() * s;
  return {{0, 0, 0}, r, -r, u, -u};
}

void highlightEdges(LineBatch& batch, const MeshMap& meshes, const std::vector<Pick>& selection,
                    const std::optional<Pick>& hover, const Camera& camera, Size2 size) {
  std::vector<std::pair<Pick, Vec4f>> picks;
  for (const Pick& p : selection) {
    if (p.kind == Pick::Kind::Edge) picks.emplace_back(p, kHighlightColor);
  }
  if (hover && hover->kind == Pick::Kind::Edge &&
      std::find(selection.begin(), selection.end(), *hover) == selection.end()) {
    picks.emplace_back(*hover, kHoverColor);
  }
  if (picks.empty()) return;
  std::vector<Vec3f> offsets = thickOffsets(camera, size, 0.75f);
  for (const auto& [p, color] : picks) {
    auto it = meshes.find(p.body);
    if (it == meshes.end() || !it->second) continue;
    const BodyMesh& mesh = *it->second;
    for (size_t i = 0; i + 1 < mesh.edgePositions.size() && i < mesh.edgeIds.size(); i += 2) {
      if (mesh.edgeIds[i] != uint32_t(p.index)) continue;
      for (const Vec3f& o : offsets) batch.line(mesh.edgePositions[i] + o, mesh.edgePositions[i + 1] + o, color);
    }
  }
}

void marker(LineBatch& batch, Vec3f c, Vec4f color, const Camera& camera, Size2 size, float r) {
  float s = camera.worldPerPoint(size);
  Vec3f rr = camera.right() * (s * r), uu = camera.up() * (s * r);
  batch.polyline({c - rr - uu, c + rr - uu, c + rr + uu, c - rr + uu, c - rr - uu}, color);
}

}  // namespace sculpt::viewport
