// Exercises every kernel operation once. Build with scripts/test_core.sh.
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <string>

#include "o3d/Kernel.hpp"

// Used only to build an open (shell) test body that the kernel API cannot make directly.
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <sstream>

using namespace o3d;

static int failures = 0;

static void check(bool ok, const char* what, const Kernel& k) {
  std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, ok ? "" : ": ", ok ? "" : k.lastError().c_str());
  if (!ok) ++failures;
}

static double volume(const Kernel& k, int body) {
  Properties p;
  k.properties(body, p);
  return p.volume;
}

static int faceCount(const Kernel& k, int body) {
  Properties p;
  k.properties(body, p);
  return p.faceCount;
}

static bool near(double value, double expected, double relTol) {
  return std::abs(value - expected) <= relTol * std::abs(expected);
}

// Index of the first face of `body` whose planar normal matches n (or -1).
static int planarFace(const Kernel& k, int body, Vec3 n) {
  for (int f = 0; f < faceCount(k, body); ++f) {
    FaceInfo info;
    if (k.faceInfo(body, f, info) && info.planar &&
        info.normal.x * n.x + info.normal.y * n.y + info.normal.z * n.z > 0.999)
      return f;
  }
  return -1;
}

static int nonPlanarFace(const Kernel& k, int body) {
  for (int f = 0; f < faceCount(k, body); ++f) {
    FaceInfo info;
    if (k.faceInfo(body, f, info) && !info.planar) return f;
  }
  return -1;
}

static Segment line(double au, double av, double bu, double bv) {
  Segment s;
  s.a = {au, av};
  s.b = {bu, bv};
  return s;
}

static Segment arc(double au, double av, double bu, double bv, double cu, double cv, bool ccw) {
  Segment s = line(au, av, bu, bv);
  s.kind = Segment::Kind::Arc;
  s.center = {cu, cv};
  s.ccw = ccw;
  return s;
}

static void newOperations() {
  const double pi = 3.14159265358979;
  Kernel k;

  // Wire profile: 20x10 rectangle whose right side is a semicircle bulging outwards.
  Profile slot;
  slot.kind = Profile::Kind::Wire;
  slot.segments = {line(0, 0, 20, 0), arc(20, 0, 20, 10, 20, 5, true), line(20, 10, 0, 10), line(0, 10, 0, 0)};
  int w = k.extrude(Plane{}, {slot}, 10, CombineMode::NewBody, -1);
  check(w > 0 && near(volume(k, w), (200 + pi * 25 / 2) * 10, 1e-4), "extrude wire profile with arc", k);
  // Same slot drawn clockwise with the arc bulging inwards removes the half-disc instead.
  Profile notch = slot;
  notch.segments[1] = arc(20, 0, 20, 10, 20, 5, false);
  int wn = k.extrude(Plane{}, {notch}, 10, CombineMode::NewBody, -1);
  check(wn > 0 && near(volume(k, wn), (200 - pi * 25 / 2) * 10, 1e-4), "extrude wire profile with cw arc", k);
  // A wire profile nested in another becomes a hole.
  Profile outer;
  outer.points = {{-5, -5}, {40, -5}, {40, 15}, {-5, 15}};
  int wh = k.extrude(Plane{}, {outer, slot}, 10, CombineMode::NewBody, -1);
  check(wh > 0 && near(volume(k, wh), 900 * 10 - (200 + pi * 25 / 2) * 10, 1e-4), "wire profile as hole", k);

  // Sweep a r=5 circle along an L-shaped path: 50 up along z, then 40 along x.
  Profile circle;
  circle.kind = Profile::Kind::Circle;
  circle.radius = 5;
  Plane pathPlane{{0, 0, 0}, {0, -1, 0}, {1, 0, 0}};  // u = x, v = z
  std::vector<Segment> path = {line(0, 0, 0, 50), line(0, 50, 40, 50)};
  int sw = k.sweep(Plane{}, {circle}, pathPlane, path, CombineMode::NewBody, -1);
  check(sw > 0 && near(volume(k, sw), pi * 25 * 90, 0.03), "sweep circle along L path", k);
  std::printf("     sweep volume %.1f (expected %.1f)\n", volume(k, sw), pi * 25 * 90);
  // Sweep along a path with an arc (quarter bend of radius 20).
  std::vector<Segment> bend = {line(0, 0, 0, 30), arc(0, 30, 20, 50, 20, 30, false), line(20, 50, 50, 50)};
  int swa = k.sweep(Plane{}, {circle}, pathPlane, bend, CombineMode::NewBody, -1);
  double bendLen = 30 + pi * 20 / 2 + 30;
  check(swa > 0 && near(volume(k, swa), pi * 25 * bendLen, 0.03), "sweep along path with arc", k);

  // Loft from a 20x20 square at z=0 to a 10x10 square at z=30: a frustum.
  Profile big, small;
  big.points = {{-10, -10}, {10, -10}, {10, 10}, {-10, 10}};
  small.points = {{-5, -5}, {5, -5}, {5, 5}, {-5, 5}};
  Plane top{{0, 0, 30}, {0, 0, 1}, {1, 0, 0}};
  int lf = k.loft({Plane{}, top}, {big, small}, CombineMode::NewBody, -1);
  check(lf > 0 && near(volume(k, lf), 30.0 / 3 * (400 + 100 + 200), 1e-3), "loft frustum", k);
  Profile disc;
  disc.kind = Profile::Kind::Circle;
  disc.radius = 8;
  int lc = k.loft({Plane{}, top}, {big, disc}, CombineMode::NewBody, -1);
  check(lc > 0 && volume(k, lc) > 0, "loft square to circle", k);

  // Scale x2 about the origin multiplies the volume by 8.
  double v0 = volume(k, lf);
  check(k.scale(lf, 2, {0, 0, 0}) && near(volume(k, lf), v0 * 8, 1e-6), "scale x2", k);
  check(!k.scale(lf, 0, {0, 0, 0}), "zero scale rejected", k);

  // Linear patterns of a 10 mm cube spaced 20 mm apart.
  int cube = k.primitive(Primitive::Box, {10, 10, 10}, {200, 0, 0});
  size_t before = k.bodies().size();
  auto lin = k.linearPattern(cube, {2, 0, 0}, 3, 20, false);
  check(lin.size() == 3 && lin[0] == cube && k.bodies().size() == before + 2, "linear pattern, 3 bodies", k);
  check(near(volume(k, lin[2]), 1000, 1e-9), "linear pattern copy volume", k);
  Properties pc;
  k.properties(lin[2], pc);
  check(std::abs(pc.bboxMin.x - (195 + 40)) < 1e-6, "linear pattern spacing", k);
  int cube2 = k.primitive(Primitive::Box, {10, 10, 10}, {200, 100, 0});
  before = k.bodies().size();
  auto linF = k.linearPattern(cube2, {0, 1, 0}, 3, 20, true);
  check(linF.size() == 1 && linF[0] == cube2 && k.bodies().size() == before && near(volume(k, cube2), 3000, 1e-6),
        "linear pattern fused", k);

  // Circular pattern of 6 cubes on a 50 mm radius around z.
  int pin = k.primitive(Primitive::Box, {10, 10, 10}, {50, 0, 0});
  auto circ = k.circularPattern(pin, {0, 0, 0}, {0, 0, 1}, 6, 360, false);
  check(circ.size() == 6 && near(volume(k, circ[3]), 1000, 1e-9), "circular pattern, 6 bodies", k);
  Properties opp;
  k.properties(circ[3], opp);
  check(std::abs(opp.bboxMax.x + 45) < 1e-6, "circular pattern 360 does not repeat original", k);
  int pin2 = k.primitive(Primitive::Box, {10, 10, 10}, {50, 0, 0});
  auto circF = k.circularPattern(pin2, {0, 0, 0}, {0, 0, 1}, 3, 90, true);
  check(circF.size() == 1 && near(volume(k, pin2), 3000, 1e-6), "circular pattern 90 deg fused", k);
  Properties pf;
  k.properties(pin2, pf);
  check(std::abs(pf.bboxMax.y - 55) < 1e-6, "circular pattern last instance at 90 deg", k);

  // Split a 40 mm cube by its mid plane.
  int sb = k.primitive(Primitive::Box, {40, 40, 40}, {0, 300, 0});
  auto pieces = k.split(sb, Plane{{0, 300, 20}, {0, 0, 1}, {1, 0, 0}});
  check(pieces.size() == 2 && pieces[0] == sb && near(volume(k, pieces[0]), 32000, 1e-6) &&
            near(volume(k, pieces[1]), 32000, 1e-6),
        "split box at mid plane", k);
  check(faceCount(k, pieces[0]) == 6 && faceCount(k, pieces[1]) == 6, "split pieces are clean boxes", k);
  check(k.split(sb, Plane{{0, 0, 500}, {0, 0, 1}, {1, 0, 0}}).empty(), "split with missing plane rejected", k);

  // Delete a fillet face: the box comes back.
  int fb = k.primitive(Primitive::Box, {20, 20, 20}, {0, 400, 0});
  check(k.fillet(fb, {0}, 4) && faceCount(k, fb) == 7, "fillet before delete", k);
  int filletFace = nonPlanarFace(k, fb);
  check(k.deleteFaces(fb, {filletFace}) && faceCount(k, fb) == 6 && near(volume(k, fb), 8000, 1e-6),
        "delete fillet face", k);

  // Draft a side face of a cube by 10 degrees against +z.
  int db = k.primitive(Primitive::Box, {20, 20, 20}, {0, 500, 0});
  int side = planarFace(k, db, {1, 0, 0});
  check(k.draft(db, {side}, {0, 0, 1}, 10), "draft side face", k);
  double dv = volume(k, db);
  check(near(dv, 8000 - 0.5 * 20 * 20 * 20 * std::tan(10 * pi / 180), 1e-3) ||
            near(dv, 8000 + 0.5 * 20 * 20 * 20 * std::tan(10 * pi / 180), 1e-3),
        "draft changes volume by the wedge", k);
  std::printf("     draft volume %.1f\n", dv);

  // Offset the top face of a cube by 5, and the side of a cylinder by 2.
  int ob = k.primitive(Primitive::Box, {20, 20, 20}, {0, 600, 0});
  int obTop = planarFace(k, ob, {0, 0, 1});
  check(k.offsetFaces(ob, {obTop}, 5) && near(volume(k, ob), 20 * 20 * 25, 1e-6), "offset planar face", k);
  int oc = k.primitive(Primitive::Cylinder, {10, 20, 0}, {0, 700, 0});
  int ocSide = nonPlanarFace(k, oc);
  check(k.offsetFaces(oc, {ocSide}, 2) && near(volume(k, oc), pi * 144 * 20, 1e-4), "offset cylindrical face", k);
  check(faceCount(k, oc) == 3, "offset cylinder keeps three faces", k);

  // Measurements.
  int ma = k.primitive(Primitive::Box, {10, 10, 10}, {0, 900, 0});
  int mb = k.primitive(Primitive::Box, {10, 10, 10}, {30, 900, 0});
  Measurement m;
  check(k.measure({ma, RefKind::Body, -1}, {mb, RefKind::Body, -1}, m) && m.hasDistance &&
            std::abs(m.distance - 20) < 1e-6 && std::abs(m.p1.x - 5) < 1e-6 && std::abs(m.p2.x - 25) < 1e-6,
        "measure distance between boxes", k);
  int maTop = planarFace(k, ma, {0, 0, 1}), maSide = planarFace(k, ma, {1, 0, 0});
  check(k.measure({ma, RefKind::Face, maTop}, {ma, RefKind::Face, maSide}, m) && m.hasAngle &&
            std::abs(m.angleDeg - 90) < 1e-6 && m.hasDistance && m.distance < 1e-6,
        "measure angle between faces", k);
  check(k.measure({ma, RefKind::Edge, 0}, {-1, RefKind::Body, -1}, m) && m.hasLength &&
            std::abs(m.length - 10) < 1e-6 && !m.hasRadius,
        "measure edge length", k);
  check(k.measure({ma, RefKind::Face, maTop}, {}, m) && m.hasArea && std::abs(m.area - 100) < 1e-6,
        "measure face area", k);
  check(k.measure({ma, RefKind::Edge, 0}, {ma, RefKind::Edge, 1}, m) && m.hasAngle &&
            std::abs(m.angleDeg - 90) < 1e-6 && std::abs(m.length - 20) < 1e-6,
        "measure two edges", k);
  int mc = k.primitive(Primitive::Cylinder, {7.5, 10, 0}, {0, 1000, 0});
  int mcSide = nonPlanarFace(k, mc);
  check(k.measure({mc, RefKind::Face, mcSide}, {}, m) && m.hasRadius && std::abs(m.radius - 7.5) < 1e-9,
        "measure cylinder radius", k);
  bool circleEdge = false;
  for (int e = 0; e < 3; ++e)
    if (k.measure({mc, RefKind::Edge, e}, {}, m) && m.hasRadius && std::abs(m.radius - 7.5) < 1e-9 &&
        std::abs(m.length - 2 * pi * 7.5) < 1e-6)
      circleEdge = true;
  check(circleEdge, "measure circular edge radius and length", k);
  check(!k.measure({ma, RefKind::Face, 99}, {}, m) && !k.lastError().empty(), "measure bad face rejected", k);
}

static Segment ellipse(double cu, double cv, double a, double b, double rotation) {
  Segment s;
  s.kind = Segment::Kind::Ellipse;
  s.center = {cu, cv};
  s.majorRadius = a;
  s.minorRadius = b;
  s.rotation = rotation;
  return s;
}

static Segment spline(std::vector<std::array<double, 2>> pts, bool closed) {
  Segment s;
  s.kind = Segment::Kind::Spline;
  s.points = pts;
  s.a = pts.front();
  s.b = pts.back();
  s.closed = closed;
  return s;
}

static Profile wireProfile(std::vector<Segment> segs) {
  Profile p;
  p.kind = Profile::Kind::Wire;
  p.segments = std::move(segs);
  return p;
}

static bool bboxNear(const Kernel& k, int body, Vec3 lo, Vec3 hi, double tol) {
  Properties p;
  if (!k.properties(body, p)) return false;
  return std::abs(p.bboxMin.x - lo.x) < tol && std::abs(p.bboxMin.y - lo.y) < tol &&
         std::abs(p.bboxMin.z - lo.z) < tol && std::abs(p.bboxMax.x - hi.x) < tol &&
         std::abs(p.bboxMax.y - hi.y) < tol && std::abs(p.bboxMax.z - hi.z) < tol;
}

// Extent (du, dv) of a set of projected polylines.
static std::array<double, 2> polylineExtent(const std::vector<std::vector<std::array<double, 2>>>& lines) {
  double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300};
  for (const auto& l : lines)
    for (const auto& p : l)
      for (int i = 0; i < 2; ++i) lo[i] = std::min(lo[i], p[i]), hi[i] = std::max(hi[i], p[i]);
  return {hi[0] - lo[0], hi[1] - lo[1]};
}

static void curveAndEditOperations() {
  const double pi = 3.14159265358979;
  Kernel k;

  // Ellipse profiles.
  int el = k.extrude(Plane{}, {wireProfile({ellipse(0, 0, 20, 10, 0)})}, 10, CombineMode::NewBody, -1);
  check(el > 0 && near(volume(k, el), pi * 200 * 10, 1e-6), "extrude ellipse", k);
  int els = k.extrude(Plane{}, {wireProfile({ellipse(0, 100, 10, 20, 0)})}, 10, CombineMode::NewBody, -1);
  check(els > 0 && near(volume(k, els), pi * 200 * 10, 1e-6) && bboxNear(k, els, {-10, 80, 0}, {10, 120, 10}, 1e-5),
        "ellipse minor > major is swapped", k);
  int elr = k.extrude(Plane{}, {wireProfile({ellipse(0, 200, 20, 10, pi / 2)})}, 10, CombineMode::NewBody, -1);
  check(elr > 0 && bboxNear(k, elr, {-10, 180, 0}, {10, 220, 10}, 1e-5), "ellipse rotation", k);
  Profile plate;
  plate.points = {{-30, 280}, {30, 280}, {30, 320}, {-30, 320}};
  int elh = k.extrude(Plane{}, {plate, wireProfile({ellipse(0, 300, 20, 10, 0.3)})}, 10, CombineMode::NewBody, -1);
  check(elh > 0 && near(volume(k, elh), (2400 - pi * 200) * 10, 1e-6), "ellipse as hole", k);
  // Revolve an ellipse (a=5, b=3) centred 30 from the axis: Pappus gives 2*pi*30 * pi*15.
  Plane side{{0, 400, 0}, {0, -1, 0}, {1, 0, 0}};
  int elv = k.revolve(side, {wireProfile({ellipse(30, 0, 5, 3, 0)})}, true, 360, CombineMode::NewBody, -1);
  check(elv > 0 && near(volume(k, elv), 2 * pi * 30 * pi * 15, 1e-4), "revolve ellipse", k);
  // Sweep an ellipse along a straight 40 mm path.
  Plane pathPlane{{0, 500, 0}, {0, -1, 0}, {1, 0, 0}};
  Plane profPlane{{0, 500, 0}, {0, 0, 1}, {1, 0, 0}};
  int elw = k.sweep(profPlane, {wireProfile({ellipse(0, 0, 6, 4, 0)})}, pathPlane, {line(0, 0, 0, 40)},
                    CombineMode::NewBody, -1);
  check(elw > 0 && near(volume(k, elw), pi * 24 * 40, 1e-3), "sweep ellipse", k);
  // Loft from a circle to an ellipse.
  Profile disc;
  disc.kind = Profile::Kind::Circle;
  disc.radius = 10;
  Plane l0{{0, 600, 0}, {0, 0, 1}, {1, 0, 0}}, l1{{0, 600, 30}, {0, 0, 1}, {1, 0, 0}};
  int ell = k.loft({l0, l1}, {disc, wireProfile({ellipse(0, 0, 15, 5, 0)})}, CombineMode::NewBody, -1);
  check(ell > 0 && volume(k, ell) > 0, "loft circle to ellipse", k);

  // Splines.
  Plane sp{{0, 700, 0}, {0, 0, 1}, {1, 0, 0}};
  int cs = k.extrude(sp, {wireProfile({spline({{0, 0}, {20, -2}, {22, 20}, {-1, 21}}, true)})}, 10,
                     CombineMode::NewBody, -1);
  check(cs > 0 && volume(k, cs) > 10 * 200, "extrude closed spline", k);
  std::printf("     closed spline volume %.1f\n", volume(k, cs));
  int os = k.extrude(sp, {wireProfile({spline({{0, 50}, {10, 55}, {20, 50}}, false), line(20, 50, 0, 50)})}, 10,
                     CombineMode::NewBody, -1);
  check(os > 0 && volume(k, os) > 0 && volume(k, os) < 20 * 5 * 10, "extrude open spline closed by a line", k);
  std::printf("     open spline volume %.1f\n", volume(k, os));
  Profile big;
  big.points = {{-20, 80}, {60, 80}, {60, 140}, {-20, 140}};
  int sh = k.extrude(sp, {big, wireProfile({spline({{0, 100}, {20, 98}, {22, 120}, {-1, 121}}, true)})}, 10,
                     CombineMode::NewBody, -1);
  check(sh > 0 && near(volume(k, sh), 80 * 60 * 10 - volume(k, cs), 1e-4), "closed spline as hole", k);
  std::vector<Segment> splinePath = {spline({{0, 0}, {10, 20}, {0, 40}}, false)};
  Profile small;
  small.kind = Profile::Kind::Circle;
  small.radius = 2;
  int ssw = k.sweep(Plane{{0, 800, 0}, {0, 0, 1}, {1, 0, 0}}, {small}, Plane{{0, 800, 0}, {0, -1, 0}, {1, 0, 0}},
                    splinePath, CombineMode::NewBody, -1);
  // The profile is not perpendicular to the spline's start tangent, so only check it is a solid.
  check(ssw > 0 && volume(k, ssw) > 0.5 * pi * 4 * 40, "sweep along spline path", k);

  // Rotate a 10x20x30 box 90 degrees about z through its corner (5, 10).
  int rb = k.primitive(Primitive::Box, {10, 20, 30}, {0, 0, 0});
  check(k.rotate(rb, {5, 10, 0}, {0, 0, 1}, 90) && bboxNear(k, rb, {5, 0, 0}, {25, 10, 30}, 1e-5),
        "rotate about corner axis", k);
  check(!k.rotate(rb, {0, 0, 0}, {0, 0, 0}, 90), "rotate with zero axis rejected", k);

  // Project a 10x20x30 box onto XY and XZ.
  int pb = k.primitive(Primitive::Box, {10, 20, 30}, {100, 0, 0});
  auto xy = k.projectEdges(pb, Plane{});
  auto exy = polylineExtent(xy);
  bool twoPoint = true;
  for (const auto& l : xy) twoPoint = twoPoint && l.size() == 2;
  check(xy.size() == 8 && twoPoint && std::abs(exy[0] - 10) < 1e-9 && std::abs(exy[1] - 20) < 1e-9,
        "project box edges onto XY", k);
  auto xz = k.projectEdges(pb, Plane{{0, 0, 0}, {0, -1, 0}, {1, 0, 0}});
  auto exz = polylineExtent(xz);
  check(xz.size() == 8 && std::abs(exz[0] - 10) < 1e-9 && std::abs(exz[1] - 30) < 1e-9,
        "project box edges onto XZ", k);
  int pc = k.primitive(Primitive::Cylinder, {5, 10, 0}, {200, 0, 0});
  auto pcl = k.projectEdges(pc, Plane{});
  auto epc = polylineExtent(pcl);
  check(!pcl.empty() && pcl[0].size() > 2 && std::abs(epc[0] - 10) < 0.05 && std::abs(epc[1] - 10) < 0.05,
        "project cylinder circles", k);

  // Replace the top of a 20 mm cube with the top of a 35 mm tall box.
  int ra = k.primitive(Primitive::Box, {20, 20, 20}, {0, 1200, 0});
  int rt = k.primitive(Primitive::Box, {20, 20, 35}, {100, 1200, 0});
  int raTop = planarFace(k, ra, {0, 0, 1}), rtTop = planarFace(k, rt, {0, 0, 1});
  check(k.replaceFace(ra, raTop, rt, rtTop) && near(volume(k, ra), 20 * 20 * 35, 1e-6), "replace face grows body", k);
  int rs = k.primitive(Primitive::Box, {20, 20, 10}, {200, 1200, 0});
  raTop = planarFace(k, ra, {0, 0, 1});
  check(k.replaceFace(ra, raTop, rs, planarFace(k, rs, {0, 0, 1})) && near(volume(k, ra), 20 * 20 * 10, 1e-6),
        "replace face trims body", k);
  check(!k.replaceFace(ra, planarFace(k, ra, {0, 0, 1}), rt, planarFace(k, rt, {1, 0, 0})) && !k.lastError().empty(),
        "replace face with non-parallel target rejected", k);
  std::printf("     non-parallel replace: %s\n", k.lastError().c_str());
  int rcyl = k.primitive(Primitive::Cylinder, {5, 10, 0}, {300, 1200, 0});
  check(!k.replaceFace(ra, planarFace(k, ra, {0, 0, 1}), rcyl, nonPlanarFace(k, rcyl)), "replace face with curved target rejected", k);

  // Fill the opening of an open box (five faces, no top).
  TopoDS_Shape solid = BRepPrimAPI_MakeBox(gp_Pnt(0, 1400, 0), 10, 10, 10).Shape();
  BRep_Builder builder;
  TopoDS_Shell shell;
  builder.MakeShell(shell);
  for (TopExp_Explorer ex(solid, TopAbs_FACE); ex.More(); ex.Next()) {
    TopoDS_Face face = TopoDS::Face(ex.Current());
    bool isTop = true;
    for (TopExp_Explorer v(face, TopAbs_VERTEX); v.More(); v.Next())
      isTop = isTop && std::abs(BRep_Tool::Pnt(TopoDS::Vertex(v.Current())).Z() - 10) < 1e-9;
    if (!isTop) builder.Add(shell, face);
  }
  std::ostringstream brep;
  BRepTools::Write(shell, brep);
  int ob = k.fromBrep(brep.str());
  Properties obp;
  k.properties(ob, obp);
  std::vector<int> rim;
  for (int e = 0; e < obp.edgeCount; ++e) {
    EdgeInfo info;
    if (k.edgeInfo(ob, e, info) && std::abs(info.start.z - 10) < 1e-9 && std::abs(info.end.z - 10) < 1e-9)
      rim.push_back(e);
  }
  check(ob > 0 && obp.faceCount == 5 && rim.size() == 4, "open box has a 4-edge rim", k);
  int patch = k.fill(ob, rim);
  Mesh pm;
  check(patch > 0 && k.tessellate(patch, pm) && pm.indices.size() >= 6 && pm.faceCount == 1, "fill rim tessellates", k);
  Properties pp;
  check(k.properties(patch, pp) && near(pp.area, 100, 1e-3) && pp.volume < 1e-6, "fill patch area, zero volume", k);
  std::printf("     fill patch: %zu tris, area %.3f\n", pm.indices.size() / 3, pp.area);
  check(k.fill(ob, {}) < 0, "fill with no edges rejected", k);

  // Edge and face info.
  int eb = k.primitive(Primitive::Box, {10, 20, 30}, {0, 1600, 0});
  EdgeInfo ei;
  check(k.edgeInfo(eb, 0, ei) && ei.linear && !ei.circular &&
            (std::abs(ei.length - 10) < 1e-9 || std::abs(ei.length - 20) < 1e-9 || std::abs(ei.length - 30) < 1e-9),
        "edge info linear", k);
  double dx = ei.end.x - ei.start.x, dy = ei.end.y - ei.start.y, dz = ei.end.z - ei.start.z;
  check(std::abs(std::sqrt(dx * dx + dy * dy + dz * dz) - ei.length) < 1e-9, "edge info end points", k);
  int ec = k.primitive(Primitive::Cylinder, {7.5, 10, 0}, {50, 1600, 0});
  bool circ = false;
  for (int e = 0; e < 3; ++e)
    if (k.edgeInfo(ec, e, ei) && ei.circular && std::abs(ei.radius - 7.5) < 1e-9 && std::abs(ei.center.x - 50) < 1e-9 &&
        std::abs(ei.center.y - 1600) < 1e-9 && std::abs(std::abs(ei.axis.z) - 1) < 1e-9 &&
        std::abs(ei.length - 2 * pi * 7.5) < 1e-6)
      circ = true;
  check(circ, "edge info circular radius and centre", k);
  check(!k.edgeInfo(ec, 99, ei), "edge info bad index rejected", k);
  FaceInfo fi;
  check(k.faceInfo(ec, nonPlanarFace(k, ec), fi) && fi.cylindrical && !fi.planar && std::abs(fi.radius - 7.5) < 1e-9 &&
            std::abs(fi.axisOrigin.x - 50) < 1e-9 && std::abs(fi.axisOrigin.y - 1600) < 1e-9 &&
            std::abs(std::abs(fi.axisDir.z) - 1) < 1e-9,
        "face info cylinder axis", k);
  check(k.faceInfo(eb, 0, fi) && fi.planar && !fi.cylindrical, "face info planar not cylindrical", k);
  int es = k.primitive(Primitive::Sphere, {6, 0, 0}, {100, 1600, 0});
  check(k.faceInfo(es, 0, fi) && !fi.cylindrical && std::abs(fi.radius - 6) < 1e-9 && std::abs(fi.axisOrigin.z - 6) < 1e-9,
        "face info sphere centre and radius", k);
}

int main(int argc, char** argv) {
  std::string tmp = argc > 1 ? argv[1] : "/tmp";
  Kernel k;

  int box = k.primitive(Primitive::Box, {40, 40, 40}, {0, 0, 0});
  check(box > 0 && std::abs(volume(k, box) - 64000) < 1, "box volume", k);

  int cyl = k.primitive(Primitive::Cylinder, {10, 60, 0}, {0, 0, -10});
  check(k.boolean(box, cyl, BoolOp::Subtract), "subtract cylinder", k);
  check(volume(k, box) < 64000, "hole removed material", k);

  // Top face: find the planar face whose normal points +Z.
  Properties props;
  k.properties(box, props);
  int top = -1;
  for (int f = 0; f < props.faceCount; ++f) {
    FaceInfo info;
    if (k.faceInfo(box, f, info) && info.planar && info.normal.z > 0.99) top = f;
  }
  check(top >= 0, "found top face", k);
  double before = volume(k, box);
  check(k.pushPull(box, top, 10), "push/pull top face", k);
  check(volume(k, box) > before, "push/pull added volume", k);

  check(k.fillet(box, {0, 1}, 3), "fillet two edges", k);
  check(k.chamfer(box, {5}, 2), "chamfer edge", k);

  Profile rect;
  rect.points = {{-10, -10}, {10, -10}, {10, 10}, {-10, 10}};
  Profile hole;
  hole.kind = Profile::Kind::Circle;
  hole.radius = 4;
  Plane xy;
  xy.origin = {80, 0, 0};
  int ext = k.extrude(xy, {rect, hole}, 15, CombineMode::NewBody, -1);
  check(ext > 0 && std::abs(volume(k, ext) - (400 - 3.14159265 * 16) * 15) < 1, "extrude with hole", k);

  Plane side{{150, 0, 0}, {0, -1, 0}, {1, 0, 0}};
  Profile ring;
  ring.points = {{5, 0}, {15, 0}, {15, 20}, {5, 20}};
  int rev = k.revolve(side, {ring}, true, 360, CombineMode::NewBody, -1);
  check(rev > 0, "revolve", k);

  check(k.shell(ext, {}, 1.5), "shell solid", k);
  check(k.transform(rev, {0, 50, 0}, {0, 0, 1}, 45), "move and rotate", k);
  int mir = k.mirror(rev, Plane{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, true);
  check(mir > 0, "mirror copy", k);

  Mesh mesh;
  check(k.tessellate(box, mesh) && !mesh.indices.empty() && !mesh.edgeIds.empty(), "tessellate", k);
  std::printf("     box mesh: %zu tris, %u faces, %u edges\n", mesh.indices.size() / 3, mesh.faceCount, mesh.edgeCount);

  std::string brep = k.toBrep(box);
  int copy = k.fromBrep(brep);
  check(copy > 0 && std::abs(volume(k, copy) - volume(k, box)) < 1e-6, "brep round trip", k);

  for (const char* ext : {"step", "iges", "stl"}) {
    std::string path = tmp + "/o3d_smoke." + ext;
    bool wrote = k.exportFile(path, {box, rev});
    check(wrote, (std::string("export ") + ext).c_str(), k);
    Kernel other;
    auto ids = other.importFile(path);
    check(!ids.empty(), (std::string("import ") + ext).c_str(), other);
  }

  check(k.remove(mir), "remove body", k);
  check(!k.boolean(box, box, BoolOp::Union), "self boolean rejected", k);
  check(!k.lastError().empty(), "error message reported", k);

  newOperations();
  curveAndEditOperations();

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
