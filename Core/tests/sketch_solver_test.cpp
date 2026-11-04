// Checks of the 2D sketch constraint solver. No OpenCascade needed.
#include <cmath>
#include <cstdio>

#include "o3d/SketchSolver.hpp"

using namespace o3d::sketch;
using T = ConstraintType;

static int failures = 0;

static void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

static bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

static double len(const Sketch& s, int l) {
  auto a = s.points[s.lines[l].a], b = s.points[s.lines[l].b];
  return std::hypot(b.x - a.x, b.y - a.y);
}

// Origin (point 0) plus a rough quadrilateral 1-2-3-4 with lines 0..3.
static Sketch roughRectangle() {
  Sketch s;
  s.points = {{0, 0}, {1, 2}, {31, -1}, {29, 18}, {-2, 21}};
  s.lines = {{1, 2}, {2, 3}, {3, 4}, {4, 1}};
  s.constraints = {{T::Horizontal, 0}, {T::Vertical, 1}, {T::Horizontal, 2}, {T::Vertical, 3}};
  return s;
}

int main() {
  {
    Sketch s = roughRectangle();
    auto r = solve(s, {0});
    check(r.status == SolveStatus::Solved, "rectangle solves");
    check(near(s.points[1].y, s.points[2].y) && near(s.points[2].x, s.points[3].x), "rectangle is axis-aligned");
    // 4 free points = 8 params, 4 equations -> 4 dof (x, y, width, height).
    check(r.degreesOfFreedom == 4, "rectangle has 4 degrees of freedom");
    check(!r.pointFixed[1], "unconstrained corner is free");
    check(r.pointFixed[0], "pinned origin is fixed");
  }
  {
    Sketch s = roughRectangle();
    s.constraints.push_back({T::Coincident, 0, 1});
    s.constraints.push_back({T::Length, 0, -1, 40});
    s.constraints.push_back({T::Length, 1, -1, 25});
    auto r = solve(s, {0});
    check(r.status == SolveStatus::Solved, "dimensioned rectangle solves");
    check(near(len(s, 0), 40) && near(len(s, 1), 25), "dimensions are met");
    check(r.degreesOfFreedom == 0, "fully constrained");
    check(r.pointFixed[3], "far corner is determined");
  }
  {
    Sketch s = roughRectangle();
    s.constraints.push_back({T::Length, 0, -1, 40});
    s.constraints.push_back({T::Length, 2, -1, 30});
    s.constraints.push_back({T::EqualLength, 0, 2});
    Sketch before = s;
    auto r = solve(s, {0});
    check(r.status == SolveStatus::Conflicting, "conflicting lengths are reported");
    check(near(s.points[2].x, before.points[2].x), "conflict leaves geometry untouched");
  }
  {
    Sketch s;
    s.points = {{0, 0}, {0, 0}, {10, 1}, {3, 3}, {9, 12}};
    s.lines = {{1, 2}, {3, 4}};
    s.constraints = {{T::Coincident, 0, 1}, {T::Horizontal, 0}, {T::Perpendicular, 0, 1}, {T::Midpoint, 3, 0}};
    auto r = solve(s, {0});
    check(r.status == SolveStatus::Solved, "perpendicular + midpoint solve");
    check(near(s.points[3].x, s.points[2].x / 2) && near(s.points[4].x, s.points[3].x), "second line is a vertical bisector");
  }
  {
    Sketch s;
    s.points = {{0, 0}, {-30, 12}, {30, 9}, {2, 1}};
    s.lines = {{1, 2}};
    s.circles = {{3, 4}};
    s.constraints = {{T::Horizontal, 0}, {T::Coincident, 0, 3}, {T::Tangent, 0, 0}, {T::Radius, 0, -1, 7.5}};
    auto r = solve(s, {0});
    check(r.status == SolveStatus::Solved, "tangent circle solves");
    check(near(std::abs(s.points[1].y), 7.5) && near(s.circles[0].radius, 7.5), "line is tangent at radius distance");
  }
  {
    Sketch s;
    s.points = {{0, 0}, {0, 0}, {20, 3}, {0, 0}, {5, 20}};
    s.lines = {{1, 2}, {3, 4}};
    s.constraints = {{T::Coincident, 0, 1}, {T::Coincident, 0, 3}, {T::Horizontal, 0}, {T::Angle, 0, 1, 60}};
    auto r = solve(s, {0});
    double angle = std::atan2(s.points[4].y, s.points[4].x) * 180 / 3.14159265358979;
    check(r.status == SolveStatus::Solved && near(angle, 60, 1e-4), "angle of 60 degrees");
  }
  {
    // Dragging a corner of a constrained rectangle keeps it rectangular and
    // lands the dragged corner on the target.
    Sketch s = roughRectangle();
    solve(s, {0});
    auto r = solve(s, {0}, {{3, 50, 40}});
    check(r.status == SolveStatus::Solved, "drag solves");
    check(near(s.points[3].x, 50, 1e-4) && near(s.points[3].y, 40, 1e-4), "dragged corner follows the pointer");
    check(near(s.points[1].y, s.points[2].y) && near(s.points[2].x, s.points[3].x), "drag keeps constraints");
  }
  {
    // A fully fixed point can't be dragged away.
    Sketch s;
    s.points = {{0, 0}, {10, 10}};
    s.constraints = {{T::Fixed, 1, -1, 10, 10}};
    auto r = solve(s, {0}, {{1, 30, 30}});
    check(r.status == SolveStatus::Solved && near(s.points[1].x, 10), "fixed point stays put");
  }
  {
    // Symmetric about the vertical line x = 0.
    Sketch s;
    s.points = {{0, 0}, {0, -10}, {0, 10}, {7, 3}, {-2, 5}};
    s.lines = {{1, 2}};
    s.constraints = {{T::Vertical, 0}, {T::Coincident, 0, 1}, {T::Length, 0, -1, 20}};
    Constraint sym{T::Symmetric, 3, 4};
    sym.c = 0;
    s.constraints.push_back(sym);
    auto r = solve(s, {0});
    check(r.status == SolveStatus::Solved && near(s.points[3].x, -s.points[4].x) && near(s.points[3].y, s.points[4].y),
          "symmetric points mirror about the line");
  }
  {
    // Horizontal on a vertical line could only be met by collapsing it.
    Sketch s;
    s.points = {{0, 0}, {10, 0}, {10, 20}};
    s.lines = {{1, 2}};
    s.constraints = {{T::Vertical, 0}, {T::Horizontal, 0}};
    auto r = solve(s, {0});
    check(r.status == SolveStatus::Conflicting && near(s.points[2].y, 20), "collapsing a line is a conflict");
  }
  {
    Sketch s;
    s.points = {{0, 0}};
    s.lines = {{0, 7}};
    check(solve(s, {0}).status == SolveStatus::Invalid, "bad index is rejected");
  }
  std::printf(failures ? "\n%d check(s) failed\n" : "\nall solver checks passed\n", failures);
  return failures ? 1 : 0;
}
