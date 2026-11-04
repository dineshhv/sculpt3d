// Objective-C facade over the C++ sketch constraint solver.
#import <Foundation/Foundation.h>
#import <simd/simd.h>

NS_ASSUME_NONNULL_BEGIN

/// Mirrors o3d::sketch::ConstraintType; argument kinds are documented there.
typedef NS_ENUM(NSInteger, O3DConstraintType) {
  O3DConstraintTypeCoincident,
  O3DConstraintTypeHorizontal,
  O3DConstraintTypeVertical,
  O3DConstraintTypeParallel,
  O3DConstraintTypePerpendicular,
  O3DConstraintTypeEqualLength,
  O3DConstraintTypeEqualRadius,
  O3DConstraintTypePointOnLine,
  O3DConstraintTypePointOnCircle,
  O3DConstraintTypeTangent,
  O3DConstraintTypeMidpoint,
  O3DConstraintTypeFixed,
  O3DConstraintTypeLength,
  O3DConstraintTypeDistance,
  O3DConstraintTypeHorizontalDistance,
  O3DConstraintTypeVerticalDistance,
  O3DConstraintTypeRadius,
  O3DConstraintTypeAngle,
  O3DConstraintTypeSymmetric,
};

typedef NS_ENUM(NSInteger, O3DSolveStatus) {
  O3DSolveStatusSolved,
  O3DSolveStatusConflicting,
  O3DSolveStatusInvalid,
};

/// Build the sketch, call -solve, then read the results back. Indices are
/// in the order entities were added.
@interface O3DSketchSolver : NSObject
- (void)addPoint:(simd_double2)point;
- (void)addLineFrom:(NSInteger)a to:(NSInteger)b;
- (void)addCircleAt:(NSInteger)center radius:(double)radius;
- (void)addConstraint:(O3DConstraintType)type a:(NSInteger)a b:(NSInteger)b value:(double)value value2:(double)value2;
/// For constraints with a third entity (Symmetric's line).
- (void)addConstraint:(O3DConstraintType)type a:(NSInteger)a b:(NSInteger)b c:(NSInteger)c value:(double)value;
- (void)pinPoint:(NSInteger)point;
- (void)dragPoint:(NSInteger)point to:(simd_double2)target;

- (O3DSolveStatus)solve;

@property(nonatomic, readonly) NSInteger degreesOfFreedom;
- (simd_double2)point:(NSInteger)index;
- (double)radius:(NSInteger)index;
- (BOOL)isPointFixed:(NSInteger)index;
- (BOOL)isCircleFixed:(NSInteger)index;
@end

NS_ASSUME_NONNULL_END
