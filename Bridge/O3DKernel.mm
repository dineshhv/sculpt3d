#import "O3DKernel.h"

#include "o3d/Kernel.hpp"

namespace {

o3d::Vec3 vec(simd_double3 v) { return {v.x, v.y, v.z}; }
simd_double3 toSimd(const o3d::Vec3& v) { return simd_make_double3(v.x, v.y, v.z); }

template <typename T>
NSData* data(const std::vector<T>& v) {
  return [NSData dataWithBytes:v.data() length:v.size() * sizeof(T)];
}

std::vector<int> ints(NSArray<NSNumber*>* values) {
  std::vector<int> out;
  for (NSNumber* n in values) out.push_back(n.intValue);
  return out;
}

NSArray<NSNumber*>* numbers(const std::vector<int>& values) {
  NSMutableArray* out = [NSMutableArray arrayWithCapacity:values.size()];
  for (int v : values) [out addObject:@(v)];
  return out;
}

}  // namespace

@interface O3DPlane ()
- (o3d::Plane)plane;
@end

@interface O3DMesh ()
- (instancetype)initWithMesh:(const o3d::Mesh&)m;
@end

@interface O3DFaceInfo ()
- (instancetype)initWithInfo:(const o3d::FaceInfo&)f;
@end

@interface O3DEdgeInfo ()
- (instancetype)initWithInfo:(const o3d::EdgeInfo&)e;
@end

@interface O3DProperties ()
- (instancetype)initWithProperties:(const o3d::Properties&)p;
@end

@interface O3DMeasurement ()
- (instancetype)initWithMeasurement:(const o3d::Measurement&)m;
@end

@interface O3DKernelState () {
 @public
  std::shared_ptr<const o3d::KernelState> _state;
}
@end

@implementation O3DKernelState
@end

@implementation O3DPlane
- (instancetype)initWithOrigin:(simd_double3)origin normal:(simd_double3)normal xDir:(simd_double3)xDir {
  if ((self = [super init])) {
    _origin = origin;
    _normal = normal;
    _xDir = xDir;
  }
  return self;
}
- (o3d::Plane)plane {
  return {vec(_origin), vec(_normal), vec(_xDir)};
}
@end

@implementation O3DSegment {
 @public
  o3d::Segment _segment;
}
+ (instancetype)lineFrom:(simd_double2)a to:(simd_double2)b {
  O3DSegment* s = [O3DSegment new];
  s->_segment.kind = o3d::Segment::Kind::Line;
  s->_segment.a = {a.x, a.y};
  s->_segment.b = {b.x, b.y};
  return s;
}
+ (instancetype)arcFrom:(simd_double2)a to:(simd_double2)b center:(simd_double2)c ccw:(BOOL)ccw {
  O3DSegment* s = [O3DSegment lineFrom:a to:b];
  s->_segment.kind = o3d::Segment::Kind::Arc;
  s->_segment.center = {c.x, c.y};
  s->_segment.ccw = ccw;
  return s;
}
+ (instancetype)splineThrough:(const simd_double2*)points count:(NSInteger)count closed:(BOOL)closed {
  O3DSegment* s = [O3DSegment new];
  s->_segment.kind = o3d::Segment::Kind::Spline;
  for (NSInteger i = 0; i < count; ++i) s->_segment.points.push_back({points[i].x, points[i].y});
  if (count > 0) {
    s->_segment.a = s->_segment.points.front();
    s->_segment.b = s->_segment.points.back();
  }
  s->_segment.closed = closed;
  return s;
}
+ (instancetype)ellipseAt:(simd_double2)center majorRadius:(double)a minorRadius:(double)b rotation:(double)rotation {
  O3DSegment* s = [O3DSegment new];
  s->_segment.kind = o3d::Segment::Kind::Ellipse;
  s->_segment.center = {center.x, center.y};
  s->_segment.majorRadius = a;
  s->_segment.minorRadius = b;
  s->_segment.rotation = rotation;
  return s;
}
@end

static std::vector<o3d::Segment> segments(NSArray<O3DSegment*>* list) {
  std::vector<o3d::Segment> out;
  for (O3DSegment* s in list) out.push_back(s->_segment);
  return out;
}

@implementation O3DProfile {
 @public
  o3d::Profile _profile;
}
+ (instancetype)wireWithSegments:(NSArray<O3DSegment*>*)list {
  O3DProfile* p = [O3DProfile new];
  p->_profile.kind = o3d::Profile::Kind::Wire;
  p->_profile.segments = segments(list);
  return p;
}
+ (instancetype)polygonWithPoints:(const simd_double2*)points count:(NSInteger)count {
  O3DProfile* p = [O3DProfile new];
  p->_profile.kind = o3d::Profile::Kind::Polygon;
  for (NSInteger i = 0; i < count; ++i) p->_profile.points.push_back({points[i].x, points[i].y});
  return p;
}
+ (instancetype)circleAt:(simd_double2)center radius:(double)radius {
  O3DProfile* p = [O3DProfile new];
  p->_profile.kind = o3d::Profile::Kind::Circle;
  p->_profile.cu = center.x;
  p->_profile.cv = center.y;
  p->_profile.radius = radius;
  return p;
}
@end

@implementation O3DMesh
- (instancetype)initWithMesh:(const o3d::Mesh&)m {
  if ((self = [super init])) {
    _positions = data(m.positions);
    _normals = data(m.normals);
    _vertexFace = data(m.vertexFace);
    _indices = data(m.indices);
    _edgePositions = data(m.edgePositions);
    _edgeIds = data(m.edgeIds);
    _faceCount = m.faceCount;
    _edgeCount = m.edgeCount;
  }
  return self;
}
@end

@implementation O3DFaceInfo
- (instancetype)initWithInfo:(const o3d::FaceInfo&)f {
  if ((self = [super init])) {
    _planar = f.planar;
    _centroid = toSimd(f.centroid);
    _normal = toSimd(f.normal);
    _xDir = toSimd(f.xDir);
    _area = f.area;
    _cylindrical = f.cylindrical;
    _axisOrigin = toSimd(f.axisOrigin);
    _axisDir = toSimd(f.axisDir);
    _radius = f.radius;
  }
  return self;
}
@end

@implementation O3DEdgeInfo
- (instancetype)initWithInfo:(const o3d::EdgeInfo&)e {
  if ((self = [super init])) {
    _linear = e.linear;
    _circular = e.circular;
    _start = toSimd(e.start);
    _end = toSimd(e.end);
    _center = toSimd(e.center);
    _axis = toSimd(e.axis);
    _radius = e.radius;
    _length = e.length;
  }
  return self;
}
@end

@implementation O3DProperties
- (instancetype)initWithProperties:(const o3d::Properties&)p {
  if ((self = [super init])) {
    _volume = p.volume;
    _area = p.area;
    _bboxMin = toSimd(p.bboxMin);
    _bboxMax = toSimd(p.bboxMax);
    _faceCount = p.faceCount;
    _edgeCount = p.edgeCount;
  }
  return self;
}
@end

@implementation O3DMeasurement
- (instancetype)initWithMeasurement:(const o3d::Measurement&)m {
  if ((self = [super init])) {
    _hasDistance = m.hasDistance;
    _distance = m.distance;
    _p1 = toSimd(m.p1);
    _p2 = toSimd(m.p2);
    _hasAngle = m.hasAngle;
    _angle = m.angleDeg;
    _hasLength = m.hasLength;
    _length = m.length;
    _hasArea = m.hasArea;
    _area = m.area;
    _hasRadius = m.hasRadius;
    _radius = m.radius;
  }
  return self;
}
@end

@implementation O3DKernel {
  o3d::Kernel _kernel;
}

static std::vector<o3d::Profile> profiles(NSArray<O3DProfile*>* list) {
  std::vector<o3d::Profile> out;
  for (O3DProfile* p in list) out.push_back(p->_profile);
  return out;
}

- (NSString*)lastError {
  return [NSString stringWithUTF8String:_kernel.lastError().c_str()];
}

- (NSArray<NSNumber*>*)bodies {
  return numbers(_kernel.bodies());
}

- (O3DKernelState*)saveState {
  O3DKernelState* st = [O3DKernelState new];
  st->_state = _kernel.saveState();
  return st;
}

- (void)restoreState:(O3DKernelState*)state {
  _kernel.restoreState(state ? state->_state : nullptr);
}

- (uint64_t)shapeKey:(NSInteger)body {
  return _kernel.shapeKey((int)body);
}

- (void)reset {
  _kernel.reset();
}

- (NSInteger)primitive:(O3DPrimitive)type dims:(simd_double3)dims position:(simd_double3)position {
  return _kernel.primitive(static_cast<o3d::Primitive>(type), {dims.x, dims.y, dims.z}, vec(position));
}

- (NSInteger)extrude:(O3DPlane*)plane profiles:(NSArray<O3DProfile*>*)list distance:(double)distance
                mode:(O3DCombineMode)mode target:(NSInteger)target {
  return _kernel.extrude([plane plane], profiles(list), distance, static_cast<o3d::CombineMode>(mode), (int)target);
}

- (NSInteger)revolve:(O3DPlane*)plane profiles:(NSArray<O3DProfile*>*)list aroundV:(BOOL)aroundV
               angle:(double)angle mode:(O3DCombineMode)mode target:(NSInteger)target {
  return _kernel.revolve([plane plane], profiles(list), aroundV, angle, static_cast<o3d::CombineMode>(mode), (int)target);
}

- (BOOL)pushPull:(NSInteger)body face:(NSInteger)face distance:(double)distance {
  return _kernel.pushPull((int)body, (int)face, distance);
}

- (BOOL)boolean:(NSInteger)target tool:(NSInteger)tool op:(O3DBoolOp)op {
  return _kernel.boolean((int)target, (int)tool, static_cast<o3d::BoolOp>(op));
}

- (BOOL)fillet:(NSInteger)body edges:(NSArray<NSNumber*>*)edges radius:(double)radius {
  return _kernel.fillet((int)body, ints(edges), radius);
}

- (BOOL)chamfer:(NSInteger)body edges:(NSArray<NSNumber*>*)edges distance:(double)distance {
  return _kernel.chamfer((int)body, ints(edges), distance);
}

- (BOOL)shell:(NSInteger)body removeFaces:(NSArray<NSNumber*>*)faces thickness:(double)thickness {
  return _kernel.shell((int)body, ints(faces), thickness);
}

- (BOOL)transform:(NSInteger)body translation:(simd_double3)t axis:(simd_double3)axis angle:(double)angle {
  return _kernel.transform((int)body, vec(t), vec(axis), angle);
}

- (BOOL)rotate:(NSInteger)body axisOrigin:(simd_double3)o axisDir:(simd_double3)d angle:(double)angle {
  return _kernel.rotate((int)body, vec(o), vec(d), angle);
}

- (NSInteger)sweep:(O3DPlane*)plane profiles:(NSArray<O3DProfile*>*)list pathPlane:(O3DPlane*)pathPlane
              path:(NSArray<O3DSegment*>*)path mode:(O3DCombineMode)mode target:(NSInteger)target {
  return _kernel.sweep([plane plane], profiles(list), [pathPlane plane], segments(path),
                       static_cast<o3d::CombineMode>(mode), (int)target);
}

- (NSInteger)loft:(NSArray<O3DPlane*>*)planes sections:(NSArray<O3DProfile*>*)sections
             mode:(O3DCombineMode)mode target:(NSInteger)target {
  std::vector<o3d::Plane> ps;
  for (O3DPlane* p in planes) ps.push_back([p plane]);
  return _kernel.loft(ps, profiles(sections), static_cast<o3d::CombineMode>(mode), (int)target);
}

- (BOOL)scale:(NSInteger)body factor:(double)factor center:(simd_double3)center {
  return _kernel.scale((int)body, factor, vec(center));
}

- (BOOL)scaleAxes:(NSInteger)body factors:(simd_double3)factors center:(simd_double3)center {
  return _kernel.scaleAxes((int)body, vec(factors), vec(center));
}

- (NSInteger)terrain:(simd_double3)center width:(double)width depth:(double)depth nx:(NSInteger)nx ny:(NSInteger)ny
             heights:(NSData*)heights {
  const double* p = static_cast<const double*>(heights.bytes);
  std::vector<double> h(p, p + heights.length / sizeof(double));
  return _kernel.terrain(vec(center), width, depth, (int)nx, (int)ny, h);
}

- (NSArray<NSNumber*>*)scatter:(NSInteger)source onto:(NSInteger)target avoid:(NSArray<NSNumber*>*)avoid
                         count:(NSInteger)count seed:(NSInteger)seed minScale:(double)minScale maxScale:(double)maxScale
                       spacing:(double)spacing maxSlope:(double)maxSlope sink:(double)sink
                randomRotation:(BOOL)rotate alignToSurface:(BOOL)align keepOriginal:(BOOL)keep {
  std::vector<int> av;
  for (NSNumber* n in avoid) av.push_back(n.intValue);
  o3d::Kernel::ScatterOptions o;
  o.count = (int)count;
  o.seed = (unsigned)seed;
  o.minScale = minScale;
  o.maxScale = maxScale;
  o.spacing = spacing;
  o.maxSlopeDeg = maxSlope;
  o.sink = sink;
  o.randomRotation = rotate;
  o.alignToSurface = align;
  o.keepOriginal = keep;
  return numbers(_kernel.scatter((int)source, (int)target, av, o));
}

- (NSArray<NSNumber*>*)linearPattern:(NSInteger)body direction:(simd_double3)dir count:(NSInteger)count
                             spacing:(double)spacing fuse:(BOOL)fuse {
  return numbers(_kernel.linearPattern((int)body, vec(dir), (int)count, spacing, fuse));
}

- (NSArray<NSNumber*>*)circularPattern:(NSInteger)body axisOrigin:(simd_double3)o axisDir:(simd_double3)d
                                 count:(NSInteger)count angle:(double)angle fuse:(BOOL)fuse {
  return numbers(_kernel.circularPattern((int)body, vec(o), vec(d), (int)count, angle, fuse));
}

- (NSArray<NSNumber*>*)split:(NSInteger)body plane:(O3DPlane*)plane {
  return numbers(_kernel.split((int)body, [plane plane]));
}

- (BOOL)deleteFaces:(NSInteger)body faces:(NSArray<NSNumber*>*)faces {
  return _kernel.deleteFaces((int)body, ints(faces));
}

- (BOOL)draft:(NSInteger)body faces:(NSArray<NSNumber*>*)faces pull:(simd_double3)pull angle:(double)angle {
  return _kernel.draft((int)body, ints(faces), vec(pull), angle);
}

- (BOOL)offsetFaces:(NSInteger)body faces:(NSArray<NSNumber*>*)faces distance:(double)distance {
  return _kernel.offsetFaces((int)body, ints(faces), distance);
}

- (BOOL)replaceFace:(NSInteger)body face:(NSInteger)face withBody:(NSInteger)targetBody face:(NSInteger)targetFace {
  return _kernel.replaceFace((int)body, (int)face, (int)targetBody, (int)targetFace);
}

- (NSInteger)fill:(NSInteger)body edges:(NSArray<NSNumber*>*)edges {
  return _kernel.fill((int)body, ints(edges));
}

- (NSInteger)mirror:(NSInteger)body plane:(O3DPlane*)plane keepOriginal:(BOOL)keep {
  return _kernel.mirror((int)body, [plane plane], keep);
}

- (BOOL)emboss:(NSInteger)body plane:(O3DPlane*)plane profiles:(NSArray<O3DProfile*>*)list
        height:(double)height deboss:(BOOL)deboss {
  return _kernel.emboss((int)body, [plane plane], profiles(list), height, deboss);
}

- (NSArray<NSData*>*)drawingView:(NSArray<NSNumber*>*)bodies direction:(simd_double3)direction up:(simd_double3)up
                          hidden:(NSArray<NSData*>* _Nullable* _Nullable)hidden {
  std::vector<int> ids;
  for (NSNumber* n in bodies) ids.push_back(n.intValue);
  auto view = _kernel.drawingView(ids, {direction.x, direction.y, direction.z}, {up.x, up.y, up.z});
  NSMutableArray<NSData*>* visible = [NSMutableArray array];
  for (const auto& l : view.visible) [visible addObject:data(l)];
  if (hidden) {
    NSMutableArray<NSData*>* h = [NSMutableArray array];
    for (const auto& l : view.hidden) [h addObject:data(l)];
    *hidden = h;
  }
  return visible;
}

- (NSInteger)duplicate:(NSInteger)body {
  return _kernel.duplicate((int)body);
}

- (BOOL)remove:(NSInteger)body {
  return _kernel.remove((int)body);
}

- (NSString*)brepForBody:(NSInteger)body {
  std::string s = _kernel.toBrep((int)body);
  return s.empty() ? nil : [NSString stringWithUTF8String:s.c_str()];
}

- (NSInteger)bodyFromBrep:(NSString*)brep {
  return _kernel.fromBrep(brep.UTF8String);
}

- (NSArray<NSNumber*>*)importFile:(NSString*)path {
  return numbers(_kernel.importFile(path.fileSystemRepresentation));
}

- (BOOL)exportFile:(NSString*)path bodies:(NSArray<NSNumber*>*)bodies {
  return _kernel.exportFile(path.fileSystemRepresentation, ints(bodies));
}

- (O3DMesh*)tessellate:(NSInteger)body quality:(double)quality {
  o3d::Mesh mesh;
  if (!_kernel.tessellate((int)body, mesh, quality)) return nil;
  return [[O3DMesh alloc] initWithMesh:mesh];
}

- (O3DFaceInfo*)faceInfo:(NSInteger)body face:(NSInteger)face {
  o3d::FaceInfo info;
  if (!_kernel.faceInfo((int)body, (int)face, info)) return nil;
  return [[O3DFaceInfo alloc] initWithInfo:info];
}

- (O3DEdgeInfo*)edgeInfo:(NSInteger)body edge:(NSInteger)edge {
  o3d::EdgeInfo info;
  if (!_kernel.edgeInfo((int)body, (int)edge, info)) return nil;
  return [[O3DEdgeInfo alloc] initWithInfo:info];
}

- (NSArray<NSData*>*)projectEdges:(NSInteger)body plane:(O3DPlane*)plane {
  auto lines = _kernel.projectEdges((int)body, [plane plane]);
  NSMutableArray<NSData*>* out = [NSMutableArray arrayWithCapacity:lines.size()];
  for (const auto& l : lines) [out addObject:data(l)];
  return out;
}

- (O3DProperties*)properties:(NSInteger)body {
  o3d::Properties props;
  if (!_kernel.properties((int)body, props)) return nil;
  return [[O3DProperties alloc] initWithProperties:props];
}

- (O3DMeasurement*)measureBody:(NSInteger)bodyA kind:(NSInteger)kindA index:(NSInteger)indexA
                   againstBody:(NSInteger)bodyB kind:(NSInteger)kindB index:(NSInteger)indexB {
  o3d::Ref a{(int)bodyA, static_cast<o3d::RefKind>(kindA), (int)indexA};
  o3d::Ref b{(int)bodyB, static_cast<o3d::RefKind>(kindB), (int)indexB};
  o3d::Measurement m;
  if (!_kernel.measure(a, b, m)) return nil;
  return [[O3DMeasurement alloc] initWithMeasurement:m];
}

@end
