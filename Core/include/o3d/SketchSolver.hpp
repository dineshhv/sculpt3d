// 2D geometric constraint solver for sketches. Geometry is points, lines
// (pairs of point indices) and circles (center point + radius). Constraints
// are residual equations solved with damped minimum-norm Gauss-Newton, so an
// under-constrained sketch moves as little as possible from where it was.
#pragma once

#include <vector>

namespace o3d::sketch {

struct Point {
  double x = 0, y = 0;
};

struct Line {
  int a = 0, b = 0;  // point indices
};

struct Circle {
  int center = 0;
  double radius = 1;
};

// Argument kinds per type: P = point, L = line, C = circle.
enum class ConstraintType {
  Coincident,          // P, P
  Horizontal,          // L
  Vertical,            // L
  Parallel,            // L, L
  Perpendicular,       // L, L
  EqualLength,         // L, L
  EqualRadius,         // C, C
  PointOnLine,         // P, L
  PointOnCircle,       // P, C
  Tangent,             // L, C
  Midpoint,            // P, L
  Fixed,               // P            value, value2 = x, y
  Length,              // L            value
  Distance,            // P, P         value
  HorizontalDistance,  // P, P         value = b.x - a.x
  VerticalDistance,    // P, P         value = b.y - a.y
  Radius,              // C            value
  Angle,               // L, L         value in degrees, from a to b
  Symmetric,           // P, P, L (c)  a and b mirror each other about line c
};

struct Constraint {
  ConstraintType type = ConstraintType::Coincident;
  int a = -1, b = -1;
  double value = 0, value2 = 0;
  int c = -1;  // third entity, for Symmetric
};

struct Sketch {
  std::vector<Point> points;
  std::vector<Line> lines;
  std::vector<Circle> circles;
  std::vector<Constraint> constraints;
};

// A point the user is dragging: pulled towards the target, but real
// constraints always win.
struct DragTarget {
  int point = -1;
  double x = 0, y = 0;
};

enum class SolveStatus { Solved, Conflicting, Invalid };

struct SolveResult {
  SolveStatus status = SolveStatus::Solved;
  double residual = 0;
  int iterations = 0;
  int degreesOfFreedom = 0;
  std::vector<bool> pointFixed;    // fully determined by the constraints
  std::vector<bool> circleFixed;   // center and radius determined
};

// Number of scalar equations a constraint contributes.
int equationCount(ConstraintType type);

// Solves in place. Points listed in `pinned` are held fixed (e.g. the sketch
// origin). On failure the sketch is left unchanged.
SolveResult solve(Sketch& sketch, const std::vector<int>& pinned = {},
                  const std::vector<DragTarget>& drag = {});

}  // namespace o3d::sketch
