#import "O3DSketchSolver.h"

#include "o3d/SketchSolver.hpp"

namespace sk = o3d::sketch;

@implementation O3DSketchSolver {
  sk::Sketch _sketch;
  std::vector<int> _pinned;
  std::vector<sk::DragTarget> _drag;
  sk::SolveResult _result;
}

- (void)addPoint:(simd_double2)point {
  _sketch.points.push_back({point.x, point.y});
}

- (void)addLineFrom:(NSInteger)a to:(NSInteger)b {
  _sketch.lines.push_back({int(a), int(b)});
}

- (void)addCircleAt:(NSInteger)center radius:(double)radius {
  _sketch.circles.push_back({int(center), radius});
}

- (void)addConstraint:(O3DConstraintType)type a:(NSInteger)a b:(NSInteger)b value:(double)value value2:(double)value2 {
  _sketch.constraints.push_back({sk::ConstraintType(type), int(a), int(b), value, value2});
}

- (void)addConstraint:(O3DConstraintType)type a:(NSInteger)a b:(NSInteger)b c:(NSInteger)c value:(double)value {
  sk::Constraint k{sk::ConstraintType(type), int(a), int(b), value, 0};
  k.c = int(c);
  _sketch.constraints.push_back(k);
}

- (void)pinPoint:(NSInteger)point {
  _pinned.push_back(int(point));
}

- (void)dragPoint:(NSInteger)point to:(simd_double2)target {
  _drag.push_back({int(point), target.x, target.y});
}

- (O3DSolveStatus)solve {
  _result = sk::solve(_sketch, _pinned, _drag);
  switch (_result.status) {
    case sk::SolveStatus::Solved: return O3DSolveStatusSolved;
    case sk::SolveStatus::Conflicting: return O3DSolveStatusConflicting;
    case sk::SolveStatus::Invalid: return O3DSolveStatusInvalid;
  }
}

- (NSInteger)degreesOfFreedom {
  return _result.degreesOfFreedom;
}

- (simd_double2)point:(NSInteger)index {
  if (index < 0 || index >= NSInteger(_sketch.points.size())) return simd_make_double2(0, 0);
  auto& p = _sketch.points[index];
  return simd_make_double2(p.x, p.y);
}

- (double)radius:(NSInteger)index {
  if (index < 0 || index >= NSInteger(_sketch.circles.size())) return 0;
  return _sketch.circles[index].radius;
}

- (BOOL)isPointFixed:(NSInteger)index {
  return index >= 0 && index < NSInteger(_result.pointFixed.size()) && _result.pointFixed[index];
}

- (BOOL)isCircleFixed:(NSInteger)index {
  return index >= 0 && index < NSInteger(_result.circleFixed.size()) && _result.circleFixed[index];
}

@end
