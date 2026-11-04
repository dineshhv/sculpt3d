// Open3D geometry core: a thin, platform-neutral facade over OpenCascade.
// Bodies are B-rep solids addressed by integer ids. Faces and edges are
// addressed by their index in TopExp::MapShapes order, which is stable for
// a given shape, so features can be replayed deterministically.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace o3d {

struct Vec3 {
  double x = 0, y = 0, z = 0;
};

// A sketch plane in world space. `normal` and `xDir` must be orthogonal.
struct Plane {
  Vec3 origin{0, 0, 0};
  Vec3 normal{0, 0, 1};
  Vec3 xDir{1, 0, 0};
};

// One edge of a closed wire profile or of a sweep path, in plane (u, v) coordinates.
struct Segment {
  enum class Kind { Line, Arc, Spline, Ellipse } kind = Kind::Line;
  std::array<double, 2> a{0, 0}, b{0, 0};  // start and end (Spline: first and last point)
  std::array<double, 2> center{0, 0};      // Arc and Ellipse
  bool ccw = true;                          // Arc only: direction from a to b around center
  // Spline only: an interpolating curve through every point, in order. When
  // `closed` it is periodic and forms a closed profile on its own.
  std::vector<std::array<double, 2>> points;
  bool closed = false;
  // Ellipse only: always a full closed ellipse (a profile on its own). `rotation`
  // is the angle in radians of the major axis from the plane's u axis.
  double majorRadius = 0, minorRadius = 0, rotation = 0;
};

// A closed profile in sketch-plane (u, v) coordinates.
struct Profile {
  enum class Kind { Polygon, Circle, Wire } kind = Kind::Polygon;
  std::vector<std::array<double, 2>> points;  // Polygon: implicitly closed
  double cu = 0, cv = 0, radius = 0;          // Circle
  std::vector<Segment> segments;              // Wire: closed loop, in order, each b == next a
};

enum class BoolOp { Union, Subtract, Intersect };

// How a sketch-based feature combines with an existing body.
enum class CombineMode { NewBody, Join, Cut };

enum class Primitive { Box, Cylinder, Sphere, Cone, Torus };

struct Mesh {
  std::vector<float> positions;     // xyz per vertex
  std::vector<float> normals;       // xyz per vertex
  std::vector<uint32_t> vertexFace; // face index per vertex
  std::vector<uint32_t> indices;    // 3 per triangle
  std::vector<float> edgePositions; // xyz per line-segment endpoint (pairs)
  std::vector<uint32_t> edgeIds;    // edge index per line-segment endpoint
  uint32_t faceCount = 0;
  uint32_t edgeCount = 0;
};

struct FaceInfo {
  bool planar = false;
  Vec3 centroid, normal, xDir;
  double area = 0;
  // `cylindrical` is set for cylinders. The axis and radius are also filled for
  // cones (radius at the axis origin) and spheres (axisOrigin = centre).
  bool cylindrical = false;
  Vec3 axisOrigin, axisDir;
  double radius = 0;
};

struct EdgeInfo {
  bool linear = false, circular = false;
  Vec3 start, end;            // end points
  Vec3 center, axis;          // circular: centre and axis direction
  double radius = 0, length = 0;
};

struct Properties {
  double volume = 0, area = 0;
  Vec3 bboxMin, bboxMax;
  int faceCount = 0, edgeCount = 0;
};

struct Measurement {
  bool hasDistance = false; double distance = 0; Vec3 p1, p2;   // closest points
  bool hasAngle = false; double angleDeg = 0;                    // between two planar faces or two linear edges
  bool hasLength = false; double length = 0;                     // single edge, or total of edges
  bool hasArea = false; double area = 0;                         // single face (or a body's surface)
  bool hasRadius = false; double radius = 0;                     // circular edge or cylindrical/spherical face
};

enum class RefKind { Body = 0, Face = 1, Edge = 2 };

// A body, or one of its faces/edges by MapShapes index.
struct Ref {
  int body = -1;
  RefKind kind = RefKind::Body;
  int index = -1;
};

// A saved set of bodies (shapes are shared, so saving is cheap).
struct KernelState;

class Kernel {
 public:
  Kernel();
  ~Kernel();
  Kernel(const Kernel&) = delete;
  Kernel& operator=(const Kernel&) = delete;

  // Removes all bodies and resets id allocation so replays are deterministic.
  void reset();
  // Checkpoints for incremental rebuilds: the bodies and id counter as they are now.
  std::shared_ptr<const KernelState> saveState() const;
  void restoreState(const std::shared_ptr<const KernelState>& state);
  // Changes whenever a body's geometry is replaced (same shape → same key).
  uint64_t shapeKey(int body) const;

  // All creating operations return the new body id, or -1 on failure.
  // Modifying operations return false on failure; see lastError().
  int primitive(Primitive type, const std::array<double, 3>& dims, const Vec3& position);
  int extrude(const Plane& plane, const std::vector<Profile>& profiles, double distance,
              CombineMode mode, int target);
  int revolve(const Plane& plane, const std::vector<Profile>& profiles, bool aroundV,
              double angleDeg, CombineMode mode, int target);
  bool pushPull(int body, int face, double distance);
  bool boolean(int target, int tool, BoolOp op);
  bool fillet(int body, const std::vector<int>& edges, double radius);
  bool chamfer(int body, const std::vector<int>& edges, double distance);
  bool shell(int body, const std::vector<int>& removeFaces, double thickness);
  bool transform(int body, const Vec3& translation, const Vec3& axis, double angleDeg);
  // Rotates a body about an arbitrary axis.
  bool rotate(int body, const Vec3& axisOrigin, const Vec3& axisDir, double angleDeg);
  // Sweeps closed profiles along an open or closed path drawn on pathPlane.
  int sweep(const Plane& plane, const std::vector<Profile>& profiles, const Plane& pathPlane,
            const std::vector<Segment>& path, CombineMode mode, int target);
  // Lofts through one profile per section, in order (each a single closed Profile on its own plane).
  int loft(const std::vector<Plane>& planes, const std::vector<Profile>& sections, CombineMode mode,
           int target);
  bool scale(int body, double factor, const Vec3& center);
  // Stretches a body by a different factor along each world axis about `center`.
  bool scaleAxes(int body, const Vec3& factors, const Vec3& center);
  // Places randomised copies of `source` on the upward-facing surface of `target`:
  // rays are cast straight down at seeded random points; a copy stands where a
  // ray hits `target` first (no `avoid` body above it), the slope is under
  // maxSlopeDeg and no other copy is within `spacing`. Each copy is scaled
  // between minScale and maxScale, optionally turned about its up axis and
  // tilted to the surface normal, and sunk by `sink` of its height. Returns the
  // copy ids; the source is removed unless keepOriginal.
  struct ScatterOptions {
    int count = 10;
    unsigned seed = 1;
    double minScale = 0.8, maxScale = 1.2;
    double spacing = 0;
    double maxSlopeDeg = 40;
    double sink = 0.02;
    bool randomRotation = true;
    bool alignToSurface = false;
    bool keepOriginal = true;
  };
  // A terrain solid: a smooth surface interpolated through an nx × ny grid of
  // top heights (absolute z, row-major, rows along +Y) spanning width × depth
  // centred on (center.x, center.y), with vertical sides and a flat bottom at center.z.
  int terrain(const Vec3& center, double width, double depth, int nx, int ny, const std::vector<double>& heights);
  std::vector<int> scatter(int source, int target, const std::vector<int>& avoid, const ScatterOptions& options);
  // Patterns return the original id first, then the copies; when fuse is true the copies
  // are unioned into body and the result is {body}. `count` includes the original.
  std::vector<int> linearPattern(int body, const Vec3& direction, int count, double spacing, bool fuse);
  // Spreads `count` instances over totalAngleDeg (a full turn does not repeat the original).
  std::vector<int> circularPattern(int body, const Vec3& axisOrigin, const Vec3& axisDir, int count,
                                   double totalAngleDeg, bool fuse);
  // Cuts a body with a plane; the original id is kept for the first piece and new ids are
  // returned for the others (result includes the original id first).
  std::vector<int> split(int body, const Plane& plane);
  bool deleteFaces(int body, const std::vector<int>& faces);
  // Tapers faces relative to the pull direction; the neutral plane passes through each
  // face's lowest point along `pullDirection`.
  bool draft(int body, const std::vector<int>& faces, const Vec3& pullDirection, double angleDeg);
  // Moves faces along their outward normals (planar or curved); neighbours are extended.
  bool offsetFaces(int body, const std::vector<int>& faces, double distance);
  // Replaces a face with the surface of another (planar) face: the body is
  // extended or trimmed so that `face` lies on targetFace's plane. Fails if the
  // target isn't planar or the faces aren't (nearly) parallel.
  bool replaceFace(int body, int face, int targetBody, int targetFace);
  // Creates a surface patch (a new body that is a face, not a solid) filling a
  // closed loop of edges of `body`.
  int fill(int body, const std::vector<int>& edges);
  int mirror(int body, const Plane& plane, bool keepOriginal);
  // Projects closed profiles along the plane normal onto the side of the body
  // facing the plane and raises them by `height` (or sinks them in when deboss),
  // following curved surfaces.
  bool emboss(int body, const Plane& plane, const std::vector<Profile>& profiles, double height, bool deboss);
  // Hidden-line projection of bodies for 2D drawings: visible and hidden edge
  // polylines in the view's (u, v) coordinates, u along `right`, v along `up`.
  struct DrawingView {
    std::vector<std::vector<std::array<double, 2>>> visible, hidden;
  };
  DrawingView drawingView(const std::vector<int>& bodies, const Vec3& viewDirection, const Vec3& up) const;
  int duplicate(int body);
  bool remove(int body);

  // Serialization of a single body as OpenCascade BREP text (used to keep
  // imported geometry inside our own document format).
  std::string toBrep(int body) const;
  int fromBrep(const std::string& brep);

  // File exchange. Import returns the ids of the created bodies.
  std::vector<int> importFile(const std::string& path);
  bool exportFile(const std::string& path, const std::vector<int>& bodies) const;

  std::vector<int> bodies() const;
  bool tessellate(int body, Mesh& out, double quality = 1.0) const;
  bool faceInfo(int body, int face, FaceInfo& out) const;
  bool edgeInfo(int body, int edge, EdgeInfo& out) const;
  // Projects a body's edges onto a plane, as polylines in plane (u, v) coordinates
  // (curved edges sampled for display, straight edges as 2 points). Edges that
  // collapse to a point (perpendicular to the plane) are skipped.
  std::vector<std::vector<std::array<double, 2>>> projectEdges(int body, const Plane& plane) const;
  bool properties(int body, Properties& out) const;
  // Measures one entity (b.body < 0) or the relation between two.
  bool measure(const Ref& a, const Ref& b, Measurement& out) const;

  const std::string& lastError() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> d;
};

}  // namespace o3d
