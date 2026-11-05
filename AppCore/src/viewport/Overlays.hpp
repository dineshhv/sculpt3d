// Frame geometry helpers ported from ViewportController.swift that only
// need a camera and meshes: grid, world axes, thick highlighted edges and
// screen-sized markers. The controller decides when to use them and adds
// the store-dependent overlays (sketch, reference items, measurements).
#pragma once

#include <optional>
#include <vector>

#include "Camera.hpp"
#include "Scene.hpp"

namespace sculpt::viewport {

/// Grid spacing: `fixedStep` when set, else a power of ten following the zoom.
double gridStep(const Camera& camera, std::optional<double> fixedStep = std::nullopt);

/// The ground grid (or the sketch grid while sketching). `plane` is the sketch
/// plane, or XY (moved to the model bottom if the controller wants that).
/// With `showWorldAxes` (and not sketching) the X, Y and Z axes are added.
LineBatch grid(const Camera& camera, const Plane& plane, double gridStep, bool sketching, bool showWorldAxes);

/// World axes on their own, when the grid is hidden.
LineBatch axesOnly(const Camera& camera, double gridStep);

/// Selection orange and hover blue used for highlights.
constexpr Vec4f kHighlightColor{1.0f, 0.55f, 0.12f, 1};
constexpr Vec4f kHoverColor{0.25f, 0.55f, 1.0f, 1};

/// GL lines are one pixel wide, so selected and hovered edges are drawn
/// again as a bundle of offset lines that reads as a thick stroke.
void highlightEdges(LineBatch& batch, const MeshMap& meshes, const std::vector<Pick>& selection,
                    const std::optional<Pick>& hover, const Camera& camera, Size2 size);

/// A screen-aligned square marker of `radiusPoints` around a world point.
void marker(LineBatch& batch, Vec3f p, Vec4f color, const Camera& camera, Size2 size, float radiusPoints = 5);

/// Offsets (in world units) for drawing a line `thick`: centre plus ±right/±up by one point.
std::vector<Vec3f> thickOffsets(const Camera& camera, Size2 size, float points = 1);

}  // namespace sculpt::viewport
