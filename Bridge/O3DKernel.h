// Objective-C facade over the C++ geometry core, consumed from Swift.
#import <Foundation/Foundation.h>
#import <simd/simd.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, O3DPrimitive) {
  O3DPrimitiveBox,
  O3DPrimitiveCylinder,
  O3DPrimitiveSphere,
  O3DPrimitiveCone,
  O3DPrimitiveTorus,
};

typedef NS_ENUM(NSInteger, O3DBoolOp) {
  O3DBoolOpUnion,
  O3DBoolOpSubtract,
  O3DBoolOpIntersect,
};

typedef NS_ENUM(NSInteger, O3DCombineMode) {
  O3DCombineModeNewBody,
  O3DCombineModeJoin,
  O3DCombineModeCut,
};

/// An opaque saved kernel state (see -saveState).
@interface O3DKernelState : NSObject
@end

@interface O3DPlane : NSObject
@property(nonatomic) simd_double3 origin;
@property(nonatomic) simd_double3 normal;
@property(nonatomic) simd_double3 xDir;
- (instancetype)initWithOrigin:(simd_double3)origin normal:(simd_double3)normal xDir:(simd_double3)xDir;
@end

/// One line or arc of a wire profile or sweep path, in plane (u, v) coordinates.
@interface O3DSegment : NSObject
+ (instancetype)lineFrom:(simd_double2)a to:(simd_double2)b;
/// Arc from a to b around center; ccw is measured looking down the plane normal.
+ (instancetype)arcFrom:(simd_double2)a to:(simd_double2)b center:(simd_double2)c ccw:(BOOL)ccw;
/// Interpolating spline through every point in order. A closed spline is periodic
/// and forms a closed profile on its own.
+ (instancetype)splineThrough:(const simd_double2 *)points count:(NSInteger)count closed:(BOOL)closed;
/// A full ellipse (a closed profile on its own); rotation (radians) is the major
/// axis' angle from the plane's u axis.
+ (instancetype)ellipseAt:(simd_double2)center majorRadius:(double)a minorRadius:(double)b rotation:(double)rotation;
@end

/// A closed sketch profile in plane (u, v) coordinates.
@interface O3DProfile : NSObject
+ (instancetype)polygonWithPoints:(const simd_double2 *)points count:(NSInteger)count;
+ (instancetype)circleAt:(simd_double2)center radius:(double)radius;
/// A closed loop of segments in order, each segment's end being the next one's start.
+ (instancetype)wireWithSegments:(NSArray<O3DSegment *> *)segments;
@end

@interface O3DMesh : NSObject
@property(nonatomic, readonly) NSData *positions;   // float3 packed (3 floats)
@property(nonatomic, readonly) NSData *normals;     // float3 packed
@property(nonatomic, readonly) NSData *vertexFace;  // uint32 per vertex
@property(nonatomic, readonly) NSData *indices;     // uint32, 3 per triangle
@property(nonatomic, readonly) NSData *edgePositions;
@property(nonatomic, readonly) NSData *edgeIds;     // uint32 per edge vertex
@property(nonatomic, readonly) NSInteger faceCount;
@property(nonatomic, readonly) NSInteger edgeCount;
@end

@interface O3DFaceInfo : NSObject
@property(nonatomic, readonly) BOOL planar;
@property(nonatomic, readonly) simd_double3 centroid;
@property(nonatomic, readonly) simd_double3 normal;
@property(nonatomic, readonly) simd_double3 xDir;
@property(nonatomic, readonly) double area;
/// Set for cylinders; axisOrigin/axisDir/radius are also filled for cones and spheres.
@property(nonatomic, readonly) BOOL cylindrical;
@property(nonatomic, readonly) simd_double3 axisOrigin;
@property(nonatomic, readonly) simd_double3 axisDir;
@property(nonatomic, readonly) double radius;
@end

@interface O3DEdgeInfo : NSObject
@property(nonatomic, readonly) BOOL linear;
@property(nonatomic, readonly) BOOL circular;
@property(nonatomic, readonly) simd_double3 start;
@property(nonatomic, readonly) simd_double3 end;
@property(nonatomic, readonly) simd_double3 center;  // circular only
@property(nonatomic, readonly) simd_double3 axis;    // circular only
@property(nonatomic, readonly) double radius;
@property(nonatomic, readonly) double length;
@end

@interface O3DProperties : NSObject
@property(nonatomic, readonly) double volume;
@property(nonatomic, readonly) double area;
@property(nonatomic, readonly) simd_double3 bboxMin;
@property(nonatomic, readonly) simd_double3 bboxMax;
@property(nonatomic, readonly) NSInteger faceCount;
@property(nonatomic, readonly) NSInteger edgeCount;
@end

@interface O3DMeasurement : NSObject
@property(nonatomic, readonly) BOOL hasDistance;
@property(nonatomic, readonly) double distance;
@property(nonatomic, readonly) simd_double3 p1;  // closest points
@property(nonatomic, readonly) simd_double3 p2;
@property(nonatomic, readonly) BOOL hasAngle;
@property(nonatomic, readonly) double angle;     // degrees
@property(nonatomic, readonly) BOOL hasLength;
@property(nonatomic, readonly) double length;
@property(nonatomic, readonly) BOOL hasArea;
@property(nonatomic, readonly) double area;
@property(nonatomic, readonly) BOOL hasRadius;
@property(nonatomic, readonly) double radius;
@end

/// Body ids are positive integers; creating calls return -1 on failure and
/// modifying calls return NO. `lastError` describes the most recent failure.
@interface O3DKernel : NSObject
@property(nonatomic, readonly, copy) NSString *lastError;
@property(nonatomic, readonly) NSArray<NSNumber *> *bodies;

- (void)reset;
/// The bodies as they are now, to restore later without replaying history.
- (O3DKernelState *)saveState;
- (void)restoreState:(O3DKernelState *)state;
/// Changes whenever the body's geometry is replaced.
- (uint64_t)shapeKey:(NSInteger)body;
- (NSInteger)primitive:(O3DPrimitive)type dims:(simd_double3)dims position:(simd_double3)position;
- (NSInteger)extrude:(O3DPlane *)plane profiles:(NSArray<O3DProfile *> *)profiles distance:(double)distance
                mode:(O3DCombineMode)mode target:(NSInteger)target;
- (NSInteger)revolve:(O3DPlane *)plane profiles:(NSArray<O3DProfile *> *)profiles aroundV:(BOOL)aroundV
               angle:(double)angle mode:(O3DCombineMode)mode target:(NSInteger)target;
- (BOOL)pushPull:(NSInteger)body face:(NSInteger)face distance:(double)distance;
- (BOOL)boolean:(NSInteger)target tool:(NSInteger)tool op:(O3DBoolOp)op;
- (BOOL)fillet:(NSInteger)body edges:(NSArray<NSNumber *> *)edges radius:(double)radius;
- (BOOL)chamfer:(NSInteger)body edges:(NSArray<NSNumber *> *)edges distance:(double)distance;
- (BOOL)shell:(NSInteger)body removeFaces:(NSArray<NSNumber *> *)faces thickness:(double)thickness;
- (BOOL)transform:(NSInteger)body translation:(simd_double3)t axis:(simd_double3)axis angle:(double)angle;
/// Rotates a body by `angle` degrees about an arbitrary axis.
- (BOOL)rotate:(NSInteger)body axisOrigin:(simd_double3)o axisDir:(simd_double3)d angle:(double)angle;
- (NSInteger)sweep:(O3DPlane *)plane profiles:(NSArray<O3DProfile *> *)profiles pathPlane:(O3DPlane *)pathPlane
              path:(NSArray<O3DSegment *> *)path mode:(O3DCombineMode)mode target:(NSInteger)target;
- (NSInteger)loft:(NSArray<O3DPlane *> *)planes sections:(NSArray<O3DProfile *> *)sections
             mode:(O3DCombineMode)mode target:(NSInteger)target;
- (BOOL)scale:(NSInteger)body factor:(double)factor center:(simd_double3)center;
- (BOOL)scaleAxes:(NSInteger)body factors:(simd_double3)factors center:(simd_double3)center;
/// A terrain solid through a grid of absolute top heights (row-major, rows along +Y), flat bottom at center.z.
- (NSInteger)terrain:(simd_double3)center width:(double)width depth:(double)depth nx:(NSInteger)nx ny:(NSInteger)ny
             heights:(NSData *)heights;
/// Randomised copies of `source` dropped onto `target`; returns the copy ids (empty on failure).
- (NSArray<NSNumber *> *)scatter:(NSInteger)source onto:(NSInteger)target avoid:(NSArray<NSNumber *> *)avoid
                           count:(NSInteger)count seed:(NSInteger)seed minScale:(double)minScale maxScale:(double)maxScale
                         spacing:(double)spacing maxSlope:(double)maxSlope sink:(double)sink
                  randomRotation:(BOOL)rotate alignToSurface:(BOOL)align keepOriginal:(BOOL)keep;
/// Patterns return the original id first, then the copies ({body} when fused).
- (NSArray<NSNumber *> *)linearPattern:(NSInteger)body direction:(simd_double3)dir count:(NSInteger)count
                               spacing:(double)spacing fuse:(BOOL)fuse;
- (NSArray<NSNumber *> *)circularPattern:(NSInteger)body axisOrigin:(simd_double3)o axisDir:(simd_double3)d
                                   count:(NSInteger)count angle:(double)angle fuse:(BOOL)fuse;
/// The original id (first piece) followed by the new pieces; empty on failure.
- (NSArray<NSNumber *> *)split:(NSInteger)body plane:(O3DPlane *)plane;
- (BOOL)deleteFaces:(NSInteger)body faces:(NSArray<NSNumber *> *)faces;
- (BOOL)draft:(NSInteger)body faces:(NSArray<NSNumber *> *)faces pull:(simd_double3)pull angle:(double)angle;
- (BOOL)offsetFaces:(NSInteger)body faces:(NSArray<NSNumber *> *)faces distance:(double)distance;
/// Moves `face` onto the plane of a parallel planar face (extending or trimming the body).
- (BOOL)replaceFace:(NSInteger)body face:(NSInteger)face withBody:(NSInteger)targetBody face:(NSInteger)targetFace;
/// A new surface body (a face, not a solid) filling a closed loop of edges.
- (NSInteger)fill:(NSInteger)body edges:(NSArray<NSNumber *> *)edges;
- (NSInteger)mirror:(NSInteger)body plane:(O3DPlane *)plane keepOriginal:(BOOL)keep;
/// Projects closed profiles onto the side of `body` facing the plane and raises
/// them by `height` (sinks them in when `deboss`), following the surface.
- (BOOL)emboss:(NSInteger)body plane:(O3DPlane *)plane profiles:(NSArray<O3DProfile *> *)profiles
        height:(double)height deboss:(BOOL)deboss;
/// Hidden-line view of bodies for drawings. Polylines are packed simd_double2 in
/// the view's (right, up) coordinates; `hidden` receives the hidden edges.
- (NSArray<NSData *> *)drawingView:(NSArray<NSNumber *> *)bodies direction:(simd_double3)direction up:(simd_double3)up
                            hidden:(NSArray<NSData *> *_Nullable *_Nullable)hidden;
- (NSInteger)duplicate:(NSInteger)body;
- (BOOL)remove:(NSInteger)body;

- (nullable NSString *)brepForBody:(NSInteger)body;
- (NSInteger)bodyFromBrep:(NSString *)brep;
- (NSArray<NSNumber *> *)importFile:(NSString *)path;
- (BOOL)exportFile:(NSString *)path bodies:(NSArray<NSNumber *> *)bodies;

- (nullable O3DMesh *)tessellate:(NSInteger)body quality:(double)quality;
- (nullable O3DFaceInfo *)faceInfo:(NSInteger)body face:(NSInteger)face;
- (nullable O3DEdgeInfo *)edgeInfo:(NSInteger)body edge:(NSInteger)edge;
- (nullable O3DProperties *)properties:(NSInteger)body;
/// The body's edges projected onto the plane; each NSData is packed simd_double2
/// (2 doubles per point) in plane (u, v) coordinates.
- (NSArray<NSData *> *)projectEdges:(NSInteger)body plane:(O3DPlane *)plane;
/// Kinds: 0 body, 1 face, 2 edge. Pass bodyB = -1 to measure a single entity.
- (nullable O3DMeasurement *)measureBody:(NSInteger)bodyA kind:(NSInteger)kindA index:(NSInteger)indexA
                             againstBody:(NSInteger)bodyB kind:(NSInteger)kindB index:(NSInteger)indexB;
@end

NS_ASSUME_NONNULL_END
