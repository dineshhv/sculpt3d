#include "o3d/Kernel.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Defeaturing.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <gp_GTrsf.hxx>
#include <GeomAPI_PointsToBSplineSurface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <random>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <HLRAlgo_Projector.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepOffset_MakeOffset.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <Geom_BSplineCurve.hxx>
#include <IGESControl_Controller.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESControl_Writer.hxx>
#include <Interface_Static.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Reader.hxx>
#include <StlAPI_Writer.hxx>
#include <TopExp.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>

namespace o3d {

namespace {

gp_Pnt toPnt(const Vec3& v) { return gp_Pnt(v.x, v.y, v.z); }
gp_Dir toDir(const Vec3& v) { return gp_Dir(v.x, v.y, v.z); }
Vec3 toVec(const gp_XYZ& p) { return {p.X(), p.Y(), p.Z()}; }

std::string lowerExtension(const std::string& path) {
  auto dot = path.find_last_of('.');
  std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

// Merges coplanar faces and collinear edges left behind by Booleans, which
// keeps face/edge counts small and the model clean for later operations.
TopoDS_Shape unify(const TopoDS_Shape& shape) {
  ShapeUpgrade_UnifySameDomain unifier(shape, true, true, true);
  unifier.Build();
  return unifier.Shape();
}

struct PlaneFrame {
  gp_Pnt origin;
  gp_Dir n, x, y;
  explicit PlaneFrame(const Plane& p) : origin(toPnt(p.origin)), n(toDir(p.normal)), x(toDir(p.xDir)) {
    y = n.Crossed(x);
  }
  gp_Pnt at(double u, double v) const {
    return origin.Translated(gp_Vec(x) * u + gp_Vec(y) * v);
  }
};

// An interpolating B-spline through the segment's points (periodic when closed).
TopoDS_Edge splineEdge(const PlaneFrame& f, const Segment& sg) {
  std::vector<gp_Pnt> pts;
  for (const auto& p : sg.points) {
    gp_Pnt q = f.at(p[0], p[1]);
    if (pts.empty() || pts.back().Distance(q) > 1e-7) pts.push_back(q);
  }
  if (sg.closed && pts.size() > 1 && pts.front().Distance(pts.back()) < 1e-7) pts.pop_back();
  if (pts.size() < (sg.closed ? 3u : 2u)) throw Standard_Failure("Spline needs more distinct points");
  Handle(TColgp_HArray1OfPnt) arr = new TColgp_HArray1OfPnt(1, static_cast<int>(pts.size()));
  for (size_t i = 0; i < pts.size(); ++i) arr->SetValue(static_cast<int>(i) + 1, pts[i]);
  GeomAPI_Interpolate interp(arr, sg.closed, 1e-7);
  interp.Perform();
  if (!interp.IsDone()) throw Standard_Failure("Spline interpolation failed");
  BRepBuilderAPI_MakeEdge mk(interp.Curve());
  if (!mk.IsDone()) throw Standard_Failure("Invalid spline segment");
  return mk.Edge();
}

// A full ellipse; the minor radius is swapped onto the other axis if larger.
TopoDS_Edge ellipseEdge(const PlaneFrame& f, const Segment& sg) {
  double major = sg.majorRadius, minor = sg.minorRadius, rot = sg.rotation;
  if (minor > major) {
    std::swap(major, minor);
    rot += M_PI / 2;
  }
  if (minor < 1e-9) throw Standard_Failure("Ellipse radii must be positive");
  gp_Dir xr(gp_Vec(f.x) * std::cos(rot) + gp_Vec(f.y) * std::sin(rot));
  gp_Elips elips(gp_Ax2(f.at(sg.center[0], sg.center[1]), f.n, xr), major, minor);
  BRepBuilderAPI_MakeEdge mk(elips);
  if (!mk.IsDone()) throw Standard_Failure("Invalid ellipse segment");
  return mk.Edge();
}

// Builds a wire from line/arc/spline/ellipse segments on a plane (closed or open, as drawn).
TopoDS_Wire segmentWire(const PlaneFrame& f, const std::vector<Segment>& segments) {
  if (segments.empty()) throw Standard_Failure("Wire has no segments");
  BRepBuilderAPI_MakeWire wire;
  for (const auto& sg : segments) {
    gp_Pnt a = f.at(sg.a[0], sg.a[1]), b = f.at(sg.b[0], sg.b[1]);
    TopoDS_Edge edge;
    if (sg.kind == Segment::Kind::Spline) {
      edge = splineEdge(f, sg);
    } else if (sg.kind == Segment::Kind::Ellipse) {
      edge = ellipseEdge(f, sg);
    } else if (sg.kind == Segment::Kind::Arc) {
      gp_Pnt c = f.at(sg.center[0], sg.center[1]);
      double r = c.Distance(a);
      if (r < 1e-9) throw Standard_Failure("Arc has zero radius");
      gp_Circ circ(gp_Ax2(c, sg.ccw ? f.n : f.n.Reversed(), f.x), r);
      BRepBuilderAPI_MakeEdge mk = a.Distance(b) < 1e-9 ? BRepBuilderAPI_MakeEdge(circ)
                                                        : BRepBuilderAPI_MakeEdge(circ, a, b);
      if (!mk.IsDone()) throw Standard_Failure("Invalid arc segment");
      edge = mk.Edge();
    } else {
      if (a.Distance(b) < 1e-9) continue;
      edge = BRepBuilderAPI_MakeEdge(a, b).Edge();
    }
    wire.Add(edge);
    if (!wire.IsDone()) throw Standard_Failure("Segments are not connected end to end");
  }
  if (!wire.IsDone()) throw Standard_Failure("Wire has no valid segments");
  return wire.Wire();
}

TopoDS_Face profileFace(const PlaneFrame& f, const Profile& p) {
  if (p.kind == Profile::Kind::Wire) {
    TopoDS_Wire wire = segmentWire(f, p.segments);
    TopoDS_Vertex first, last;
    TopExp::Vertices(wire, first, last);
    if (first.IsNull() || !first.IsSame(last)) throw Standard_Failure("Profile wire is not closed");
    BRepBuilderAPI_MakeFace face(wire, true);
    if (!face.IsDone()) throw Standard_Failure("Profile is not planar");
    return face.Face();
  }
  if (p.kind == Profile::Kind::Circle) {
    gp_Circ circ(gp_Ax2(f.at(p.cu, p.cv), f.n, f.x), p.radius);
    TopoDS_Wire wire = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(circ).Edge()).Wire();
    return BRepBuilderAPI_MakeFace(wire, true).Face();
  }
  BRepBuilderAPI_MakePolygon poly;
  for (const auto& pt : p.points) poly.Add(f.at(pt[0], pt[1]));
  poly.Close();
  if (!poly.IsDone()) throw Standard_Failure("Profile is not a valid polygon");
  BRepBuilderAPI_MakeFace face(poly.Wire(), true);
  if (!face.IsDone()) throw Standard_Failure("Profile is not planar");
  return face.Face();
}

double faceArea(const TopoDS_Shape& face) {
  GProp_GProps props;
  BRepGProp::SurfaceProperties(face, props);
  return props.Mass();
}

double edgeLength(const TopoDS_Shape& e) {
  GProp_GProps props;
  BRepGProp::LinearProperties(e, props);
  return props.Mass();
}

// True if `inner` lies inside `outer` (both coplanar faces).
bool faceInside(const TopoDS_Face& inner, const TopoDS_Face& outer) {
  TopExp_Explorer ex(inner, TopAbs_VERTEX);
  if (!ex.More()) return false;
  gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(ex.Current()));
  BRepClass_FaceClassifier cls(outer, p, 1e-6);
  return cls.State() == TopAbs_IN;
}

// Sweeps every profile and combines them: profiles nested inside a larger
// profile become holes, the rest are unioned.
template <typename SweepFn>
TopoDS_Shape sweepProfiles(const Plane& plane, const std::vector<Profile>& profiles, SweepFn sweep) {
  if (profiles.empty()) throw Standard_Failure("Sketch has no closed profiles");
  PlaneFrame frame(plane);
  std::vector<std::pair<double, TopoDS_Face>> faces;
  for (const auto& p : profiles) {
    TopoDS_Face face = profileFace(frame, p);
    faces.emplace_back(faceArea(face), face);
  }
  std::sort(faces.begin(), faces.end(), [](auto& a, auto& b) { return a.first > b.first; });

  TopoDS_Shape result;
  std::vector<TopoDS_Face> outers;
  for (const auto& [area, face] : faces) {
    bool isHole = false;
    for (const auto& outer : outers) isHole = isHole || faceInside(face, outer);
    TopoDS_Shape solid = sweep(face);
    if (result.IsNull()) {
      result = solid;
    } else if (isHole) {
      result = BRepAlgoAPI_Cut(result, solid).Shape();
    } else {
      result = BRepAlgoAPI_Fuse(result, solid).Shape();
    }
    if (!isHole) outers.push_back(face);
  }
  return result;
}


// The extent of a shape's bounding-box diagonal (1 for empty shapes).
double shapeSize(const TopoDS_Shape& s) {
  Bnd_Box box;
  BRepBndLib::Add(s, box);
  return box.IsVoid() ? 1.0 : std::sqrt(box.SquareExtent());
}

gp_Vec unitVec(const Vec3& v, const char* what) {
  gp_Vec u(v.x, v.y, v.z);
  if (u.Magnitude() < 1e-12) throw Standard_Failure(what);
  return u.Normalized();
}

// Unions `copies` into `base` in a single Boolean.
TopoDS_Shape fuseAll(const TopoDS_Shape& base, const std::vector<TopoDS_Shape>& copies) {
  if (copies.empty()) return base;
  TopTools_ListOfShape args, tools;
  args.Append(base);
  for (const auto& c : copies) tools.Append(c);
  BRepAlgoAPI_Fuse op;
  op.SetArguments(args);
  op.SetTools(tools);
  op.SetRunParallel(false);
  op.Build();
  if (!op.IsDone() || op.Shape().IsNull()) throw Standard_Failure("Fusing the pattern failed");
  return unify(op.Shape());
}

// Moves `faces` of a solid along their outward normals by `distance`. Zero global
// offset with per-face values: only the chosen faces move, their neighbours are
// extended or trimmed to meet them.
TopoDS_Shape offsetShapeFaces(const TopoDS_Shape& s, const std::vector<TopoDS_Face>& faces, double distance) {
  BRepOffset_MakeOffset mk;
  mk.Initialize(s, 0.0, 1e-6, BRepOffset_Skin, false, false, GeomAbs_Intersection);
  for (const auto& f : faces) mk.SetOffsetOnFace(f, distance);
  mk.MakeOffsetShape();
  if (!mk.IsDone() || mk.Shape().IsNull()) throw Standard_Failure("Offset failed: faces may collide with the body");
  TopoDS_Shape r = mk.Shape();
  // Skin offsets of a solid come back as a shell or a compound holding one solid.
  if (r.ShapeType() != TopAbs_SOLID) {
    TopExp_Explorer ex(r, TopAbs_SOLID);
    if (ex.More()) {
      r = ex.Current();
    } else {
      BRep_Builder b;
      TopoDS_Solid solid;
      b.MakeSolid(solid);
      for (TopExp_Explorer sh(r, TopAbs_SHELL); sh.More(); sh.Next()) b.Add(solid, sh.Current());
      r = solid;
    }
  }
  return unify(r);
}

}  // namespace

struct Kernel::Impl {
  std::map<int, TopoDS_Shape> bodies;
  int nextId = 1;
  std::string error;

  int add(const TopoDS_Shape& s) {
    if (s.IsNull()) throw Standard_Failure("Operation produced no geometry");
    int id = nextId++;
    bodies[id] = s;
    return id;
  }

  TopoDS_Shape& get(int id) {
    auto it = bodies.find(id);
    if (it == bodies.end()) throw Standard_Failure("Body does not exist");
    return it->second;
  }

  // Applies a CombineMode for sketch features: returns the affected body id.
  int combine(const TopoDS_Shape& shape, CombineMode mode, int target) {
    if (mode == CombineMode::NewBody) return add(shape);
    TopoDS_Shape& base = get(target);
    if (mode == CombineMode::Join) {
      BRepAlgoAPI_Fuse op(base, shape);
      if (!op.IsDone()) throw Standard_Failure("Join failed");
      base = unify(op.Shape());
    } else {
      BRepAlgoAPI_Cut op(base, shape);
      if (!op.IsDone()) throw Standard_Failure("Cut failed");
      base = unify(op.Shape());
    }
    return target;
  }

  static TopoDS_Face face(const TopoDS_Shape& s, int index) {
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(s, TopAbs_FACE, map);
    if (index < 0 || index >= map.Extent()) throw Standard_Failure("Face does not exist");
    return TopoDS::Face(map(index + 1));
  }

  static TopoDS_Edge edge(const TopoDS_Shape& s, int index) {
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(s, TopAbs_EDGE, map);
    if (index < 0 || index >= map.Extent()) throw Standard_Failure("Edge does not exist");
    return TopoDS::Edge(map(index + 1));
  }

  // Outward normal and centroid of a planar face.
  static bool planarFrame(const TopoDS_Face& f, gp_Pnt& centroid, gp_Dir& normal, gp_Dir& xDir) {
    BRepAdaptor_Surface surf(f);
    if (surf.GetType() != GeomAbs_Plane) return false;
    gp_Pln pln = surf.Plane();
    normal = pln.Axis().Direction();
    xDir = pln.XAxis().Direction();
    if (f.Orientation() == TopAbs_REVERSED) normal.Reverse();
    GProp_GProps props;
    BRepGProp::SurfaceProperties(f, props);
    centroid = props.CentreOfMass();
    return true;
  }

  template <typename Fn>
  auto guard(Fn fn, decltype(fn()) failValue) -> decltype(fn()) {
    error.clear();
    try {
      return fn();
    } catch (const Standard_Failure& e) {
      error = e.GetMessageString() && *e.GetMessageString() ? e.GetMessageString() : e.DynamicType()->Name();
    } catch (const std::exception& e) {
      error = e.what();
    } catch (...) {
      error = "Unknown geometry error";
    }
    return failValue;
  }
};

Kernel::Kernel() : d(std::make_unique<Impl>()) {}
Kernel::~Kernel() = default;

struct KernelState {
  std::map<int, TopoDS_Shape> bodies;
  int nextId = 1;
};

std::shared_ptr<const KernelState> Kernel::saveState() const {
  auto st = std::make_shared<KernelState>();
  st->bodies = d->bodies;
  st->nextId = d->nextId;
  return st;
}

void Kernel::restoreState(const std::shared_ptr<const KernelState>& st) {
  if (!st) return reset();
  d->bodies = st->bodies;
  d->nextId = st->nextId;
  d->error.clear();
}

uint64_t Kernel::shapeKey(int body) const {
  auto it = d->bodies.find(body);
  if (it == d->bodies.end() || it->second.IsNull()) return 0;
  const TopoDS_Shape& s = it->second;
  uint64_t h = 1469598103934665603ull ^ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(s.TShape().get()));
  h = (h ^ static_cast<uint64_t>(s.Orientation())) * 1099511628211ull;
  const gp_Trsf t = s.Location().Transformation();
  for (int r = 1; r <= 3; ++r)
    for (int c = 1; c <= 4; ++c) {
      double v = t.Value(r, c);
      uint64_t b;
      std::memcpy(&b, &v, sizeof b);
      h = (h ^ b) * 1099511628211ull;
    }
  return h;
}

void Kernel::reset() {
  d->bodies.clear();
  d->nextId = 1;
  d->error.clear();
}

const std::string& Kernel::lastError() const { return d->error; }

std::vector<int> Kernel::bodies() const {
  std::vector<int> ids;
  for (const auto& [id, _] : d->bodies) ids.push_back(id);
  return ids;
}

int Kernel::primitive(Primitive type, const std::array<double, 3>& dims, const Vec3& position) {
  return d->guard([&] {
    // Primitives sit on the ground plane (z = 0), centred on `position`.
    gp_Pnt c = toPnt(position);
    TopoDS_Shape s;
    switch (type) {
      case Primitive::Box:
        s = BRepPrimAPI_MakeBox(gp_Pnt(c.X() - dims[0] / 2, c.Y() - dims[1] / 2, c.Z()), dims[0], dims[1], dims[2]).Shape();
        break;
      case Primitive::Cylinder:
        s = BRepPrimAPI_MakeCylinder(gp_Ax2(c, gp::DZ()), dims[0], dims[1]).Shape();
        break;
      case Primitive::Sphere:
        s = BRepPrimAPI_MakeSphere(c.Translated(gp_Vec(0, 0, dims[0])), dims[0]).Shape();
        break;
      case Primitive::Cone:
        s = BRepPrimAPI_MakeCone(gp_Ax2(c, gp::DZ()), dims[0], dims[1], dims[2]).Shape();
        break;
      case Primitive::Torus:
        s = BRepPrimAPI_MakeTorus(gp_Ax2(c.Translated(gp_Vec(0, 0, dims[1])), gp::DZ()), dims[0], dims[1]).Shape();
        break;
    }
    return d->add(s);
  }, -1);
}

int Kernel::extrude(const Plane& plane, const std::vector<Profile>& profiles, double distance,
                    CombineMode mode, int target) {
  return d->guard([&] {
    if (std::abs(distance) < 1e-9) throw Standard_Failure("Distance must not be zero");
    gp_Vec dir = gp_Vec(toDir(plane.normal)) * distance;
    TopoDS_Shape s = sweepProfiles(plane, profiles, [&](const TopoDS_Face& f) {
      return BRepPrimAPI_MakePrism(f, dir).Shape();
    });
    return d->combine(s, mode, target);
  }, -1);
}

int Kernel::revolve(const Plane& plane, const std::vector<Profile>& profiles, bool aroundV,
                    double angleDeg, CombineMode mode, int target) {
  return d->guard([&] {
    PlaneFrame frame(plane);
    gp_Ax1 axis(frame.origin, aroundV ? frame.y : frame.x);
    double angle = std::clamp(angleDeg, -360.0, 360.0) * M_PI / 180.0;
    TopoDS_Shape s = sweepProfiles(plane, profiles, [&](const TopoDS_Face& f) {
      BRepPrimAPI_MakeRevol revol(f, axis, angle);
      if (!revol.IsDone()) throw Standard_Failure("Revolve failed: profile may cross the axis");
      return revol.Shape();
    });
    return d->combine(s, mode, target);
  }, -1);
}

bool Kernel::pushPull(int body, int face, double distance) {
  return d->guard([&] {
    TopoDS_Shape& s = d->get(body);
    TopoDS_Face f = Impl::face(s, face);
    gp_Pnt c;
    gp_Dir n, x;
    if (!Impl::planarFrame(f, c, n, x)) throw Standard_Failure("Push/pull needs a planar face");
    TopoDS_Shape prism = BRepPrimAPI_MakePrism(f, gp_Vec(n) * distance).Shape();
    TopoDS_Shape result = distance > 0 ? BRepAlgoAPI_Fuse(s, prism).Shape() : BRepAlgoAPI_Cut(s, prism).Shape();
    if (result.IsNull()) throw Standard_Failure("Push/pull failed");
    s = unify(result);
    return true;
  }, false);
}

bool Kernel::boolean(int target, int tool, BoolOp op) {
  return d->guard([&] {
    if (target == tool) throw Standard_Failure("Pick two different bodies");
    TopoDS_Shape& a = d->get(target);
    const TopoDS_Shape& b = d->get(tool);
    TopoDS_Shape r;
    switch (op) {
      case BoolOp::Union: r = BRepAlgoAPI_Fuse(a, b).Shape(); break;
      case BoolOp::Subtract: r = BRepAlgoAPI_Cut(a, b).Shape(); break;
      case BoolOp::Intersect: r = BRepAlgoAPI_Common(a, b).Shape(); break;
    }
    if (r.IsNull()) throw Standard_Failure("Boolean operation failed");
    a = unify(r);
    d->bodies.erase(tool);
    return true;
  }, false);
}

bool Kernel::fillet(int body, const std::vector<int>& edges, double radius) {
  return d->guard([&] {
    TopoDS_Shape& s = d->get(body);
    BRepFilletAPI_MakeFillet mk(s);
    for (int e : edges) mk.Add(radius, Impl::edge(s, e));
    mk.Build();
    if (!mk.IsDone()) throw Standard_Failure("Fillet failed: radius may be too large");
    s = mk.Shape();
    return true;
  }, false);
}

bool Kernel::chamfer(int body, const std::vector<int>& edges, double distance) {
  return d->guard([&] {
    TopoDS_Shape& s = d->get(body);
    BRepFilletAPI_MakeChamfer mk(s);
    for (int e : edges) mk.Add(distance, Impl::edge(s, e));
    mk.Build();
    if (!mk.IsDone()) throw Standard_Failure("Chamfer failed: distance may be too large");
    s = mk.Shape();
    return true;
  }, false);
}

bool Kernel::shell(int body, const std::vector<int>& removeFaces, double thickness) {
  return d->guard([&] {
    TopoDS_Shape& s = d->get(body);
    TopTools_ListOfShape faces;
    for (int f : removeFaces) faces.Append(Impl::face(s, f));
    BRepOffsetAPI_MakeThickSolid mk;
    mk.MakeThickSolidByJoin(s, faces, -std::abs(thickness), 1e-3);
    mk.Build();
    if (!mk.IsDone()) throw Standard_Failure("Shell failed: wall may be too thick");
    s = mk.Shape();
    return true;
  }, false);
}

bool Kernel::transform(int body, const Vec3& t, const Vec3& axis, double angleDeg) {
  return d->guard([&] {
    TopoDS_Shape& s = d->get(body);
    gp_Trsf trsf;
    if (std::abs(angleDeg) > 1e-9) {
      Bnd_Box box;
      BRepBndLib::Add(s, box);
      gp_Pnt c = box.IsVoid() ? gp::Origin() : gp_Pnt((box.CornerMin().XYZ() + box.CornerMax().XYZ()) / 2);
      trsf.SetRotation(gp_Ax1(c, toDir(axis)), angleDeg * M_PI / 180.0);
    }
    gp_Trsf move;
    move.SetTranslation(gp_Vec(t.x, t.y, t.z));
    s = BRepBuilderAPI_Transform(s, move * trsf, true).Shape();
    return true;
  }, false);
}

bool Kernel::rotate(int body, const Vec3& axisOrigin, const Vec3& axisDir, double angleDeg) {
  return d->guard([&] {
    TopoDS_Shape& s = d->get(body);
    gp_Trsf trsf;
    trsf.SetRotation(gp_Ax1(toPnt(axisOrigin), gp_Dir(unitVec(axisDir, "Rotation axis is zero"))),
                     angleDeg * M_PI / 180.0);
    s = BRepBuilderAPI_Transform(s, trsf, true).Shape();
    return true;
  }, false);
}

int Kernel::sweep(const Plane& plane, const std::vector<Profile>& profiles, const Plane& pathPlane,
                  const std::vector<Segment>& path, CombineMode mode, int target) {
  return d->guard([&] {
    TopoDS_Wire spine = segmentWire(PlaneFrame(pathPlane), path);
    gp_Dir binormal = toDir(pathPlane.normal);
    TopoDS_Shape s = sweepProfiles(plane, profiles, [&](const TopoDS_Face& f) {
      BRepOffsetAPI_MakePipeShell pipe(spine);
      // The path is planar, so keeping the binormal fixed avoids any twist.
      pipe.SetMode(binormal);
      pipe.SetTransitionMode(BRepBuilderAPI_RightCorner);
      pipe.Add(BRepTools::OuterWire(f));
      pipe.Build();
      if (!pipe.IsDone()) throw Standard_Failure("Sweep failed: path may be too tight for the profile");
      if (!pipe.MakeSolid()) throw Standard_Failure("Sweep did not produce a closed solid");
      return pipe.Shape();
    });
    return d->combine(s, mode, target);
  }, -1);
}

int Kernel::loft(const std::vector<Plane>& planes, const std::vector<Profile>& sections, CombineMode mode,
                 int target) {
  return d->guard([&] {
    if (sections.size() < 2) throw Standard_Failure("Loft needs at least two sections");
    if (planes.size() != sections.size()) throw Standard_Failure("Each loft section needs a plane");
    BRepOffsetAPI_ThruSections loft(true, false);
    for (size_t i = 0; i < sections.size(); ++i) {
      TopoDS_Face face = profileFace(PlaneFrame(planes[i]), sections[i]);
      loft.AddWire(BRepTools::OuterWire(face));
    }
    loft.Build();
    if (!loft.IsDone() || loft.Shape().IsNull()) throw Standard_Failure("Loft failed: sections may be incompatible");
    return d->combine(loft.Shape(), mode, target);
  }, -1);
}

bool Kernel::scale(int body, double factor, const Vec3& center) {
  return d->guard([&] {
    if (factor <= 1e-9) throw Standard_Failure("Scale factor must be positive");
    TopoDS_Shape& s = d->get(body);
    gp_Trsf trsf;
    trsf.SetScale(toPnt(center), factor);
    s = BRepBuilderAPI_Transform(s, trsf, true).Shape();
    return true;
  }, false);
}

bool Kernel::scaleAxes(int body, const Vec3& f, const Vec3& center) {
  return d->guard([&] {
    if (f.x <= 1e-9 || f.y <= 1e-9 || f.z <= 1e-9) throw Standard_Failure("Scale factors must be positive");
    TopoDS_Shape& s = d->get(body);
    if (std::abs(f.x - f.y) < 1e-12 && std::abs(f.y - f.z) < 1e-12) {
      gp_Trsf trsf;
      trsf.SetScale(toPnt(center), f.x);
      s = BRepBuilderAPI_Transform(s, trsf, true).Shape();
      return true;
    }
    gp_GTrsf g;
    g.SetVectorialPart(gp_Mat(f.x, 0, 0, 0, f.y, 0, 0, 0, f.z));
    g.SetTranslationPart(gp_XYZ(center.x * (1 - f.x), center.y * (1 - f.y), center.z * (1 - f.z)));
    BRepBuilderAPI_GTransform mk(s, g, true);
    if (!mk.IsDone()) throw Standard_Failure("Scale failed");
    s = mk.Shape();
    return true;
  }, false);
}

int Kernel::terrain(const Vec3& c, double width, double depth, int nx, int ny, const std::vector<double>& h) {
  return d->guard([&] {
    if (nx < 2 || ny < 2 || (int)h.size() != nx * ny) throw Standard_Failure("Terrain needs an nx × ny grid of heights");
    if (width <= 1e-6 || depth <= 1e-6) throw Standard_Failure("Terrain width and depth must be positive");
    double zmin = h[0], zmax = h[0];
    for (double z : h) { zmin = std::min(zmin, z); zmax = std::max(zmax, z); }
    if (zmin <= c.z) throw Standard_Failure("Terrain heights must stay above its base");
    TColgp_Array2OfPnt pts(1, nx, 1, ny);
    for (int j = 0; j < ny; ++j)
      for (int i = 0; i < nx; ++i)
        pts.SetValue(i + 1, j + 1, gp_Pnt(c.x - width / 2 + width * i / (nx - 1), c.y - depth / 2 + depth * j / (ny - 1),
                                          h[j * nx + i]));
    GeomAPI_PointsToBSplineSurface fit;
    fit.Interpolate(pts);
    if (!fit.IsDone()) throw Standard_Failure("Couldn't fit a surface through the heights");
    TopoDS_Face top = BRepBuilderAPI_MakeFace(fit.Surface(), Precision::Confusion());
    // Extrude well below the base, then trim flat at the base with a box a
    // little wider than the terrain so only its bottom face cuts.
    double drop = (zmax - c.z) + (zmax - zmin) + 10;
    TopoDS_Shape prism = BRepPrimAPI_MakePrism(top, gp_Vec(0, 0, -drop)).Shape();
    double m = std::max(width, depth) * 0.01 + 1;
    TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(c.x - width / 2 - m, c.y - depth / 2 - m, c.z),
                                           gp_Pnt(c.x + width / 2 + m, c.y + depth / 2 + m, zmax + (zmax - zmin) + 10)).Shape();
    BRepAlgoAPI_Common common(prism, box);
    if (!common.IsDone() || common.Shape().IsNull()) throw Standard_Failure("Couldn't close the terrain into a solid");
    return d->add(unify(common.Shape()));
  }, -1);
}

std::vector<int> Kernel::scatter(int source, int target, const std::vector<int>& avoid, const ScatterOptions& o) {
  return d->guard([&] {
    if (source == target) throw Standard_Failure("Scatter needs a different body to scatter onto");
    if (o.count < 1) throw Standard_Failure("Scatter needs at least one copy");
    if (o.minScale <= 1e-9 || o.maxScale < o.minScale) throw Standard_Failure("Scatter scales must be positive, min ≤ max");
    TopoDS_Shape src = d->get(source);
    const TopoDS_Shape& tgt = d->get(target);

    Bnd_Box sb, tb;
    BRepBndLib::AddOptimal(src, sb, false, false);
    BRepBndLib::AddOptimal(tgt, tb, false, false);
    double sx0, sy0, sz0, sx1, sy1, sz1, tx0, ty0, tz0, tx1, ty1, tz1;
    sb.Get(sx0, sy0, sz0, sx1, sy1, sz1);
    tb.Get(tx0, ty0, tz0, tx1, ty1, tz1);
    gp_Pnt anchor((sx0 + sx1) / 2, (sy0 + sy1) / 2, sz0);
    double height = sz1 - sz0;
    double top = tz1 + 1.0;
    for (int a : avoid) {
      Bnd_Box ab;
      BRepBndLib::Add(d->get(a), ab);
      double x0, y0, z0, x1, y1, z1;
      ab.Get(x0, y0, z0, x1, y1, z1);
      top = std::max(top, z1 + 1.0);
    }

    double tol = std::max(1e-6, (tx1 - tx0 + ty1 - ty0) * 1e-7);
    IntCurvesFace_ShapeIntersector onTarget;
    onTarget.Load(tgt, tol);
    std::vector<std::unique_ptr<IntCurvesFace_ShapeIntersector>> blockers;
    for (int a : avoid) {
      if (a == target || a == source) continue;
      blockers.push_back(std::make_unique<IntCurvesFace_ShapeIntersector>());
      blockers.back()->Load(d->get(a), tol);
    }

    std::mt19937 rng(o.seed);
    std::uniform_real_distribution<double> ux(tx0, tx1), uy(ty0, ty1), unit(0, 1);
    double cosSlope = std::cos(std::clamp(o.maxSlopeDeg, 0.0, 90.0) * M_PI / 180.0);
    std::vector<gp_Pnt> placed;
    std::vector<TopoDS_Shape> copies;
    const gp_Dir down(0, 0, -1);
    int attempts = o.count * 40 + 200;
    for (int i = 0; i < attempts && (int)copies.size() < o.count; ++i) {
      // Draw every random number up front so a rejected point doesn't shift the rest.
      double x = ux(rng), y = uy(rng), r1 = unit(rng), r2 = unit(rng);
      gp_Lin ray(gp_Pnt(x, y, top), down);
      onTarget.Perform(ray, 0, RealLast());
      if (!onTarget.IsDone() || onTarget.NbPnt() < 1) continue;
      int best = 1;
      for (int k = 2; k <= onTarget.NbPnt(); ++k)
        if (onTarget.WParameter(k) < onTarget.WParameter(best)) best = k;
      double w = onTarget.WParameter(best);
      bool blocked = false;
      for (auto& b : blockers) {
        b->Perform(ray, 0, w - tol);
        if (b->IsDone() && b->NbPnt() > 0) { blocked = true; break; }
      }
      if (blocked) continue;
      gp_Pnt hit = onTarget.Pnt(best);
      if (o.spacing > 0) {
        bool close = false;
        for (const auto& p : placed)
          if (std::hypot(p.X() - hit.X(), p.Y() - hit.Y()) < o.spacing) { close = true; break; }
        if (close) continue;
      }
      BRepAdaptor_Surface surf(onTarget.Face(best));
      gp_Pnt sp;
      gp_Vec du, dv;
      surf.D1(onTarget.UParameter(best), onTarget.VParameter(best), sp, du, dv);
      gp_Vec nv = du.Crossed(dv);
      gp_Dir n(0, 0, 1);
      if (nv.Magnitude() > 1e-12) {
        n = gp_Dir(nv);
        if (n.Z() < 0) n.Reverse();
      }
      if (n.Z() < cosSlope - 1e-9) continue;

      double sc = o.minScale + (o.maxScale - o.minScale) * r1;
      gp_Trsf toOrigin, scaling, spin, tilt, place;
      toOrigin.SetTranslation(gp_Vec(anchor, gp_Pnt(0, 0, 0)));
      scaling.SetScale(gp_Pnt(0, 0, 0), sc);
      if (o.randomRotation) spin.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), r2 * 2 * M_PI);
      if (o.alignToSurface) {
        gp_Vec axis = gp_Vec(0, 0, 1).Crossed(gp_Vec(n));
        double ang = std::acos(std::clamp(n.Z(), -1.0, 1.0));
        if (axis.Magnitude() > 1e-9 && ang > 1e-6) tilt.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(axis)), ang);
      }
      gp_Vec sinkBy = gp_Vec(n) * (-o.sink * height * sc);
      place.SetTranslation(gp_Vec(hit.XYZ()) + sinkBy);
      gp_Trsf t = place;
      t.Multiply(tilt);
      t.Multiply(spin);
      t.Multiply(scaling);
      t.Multiply(toOrigin);
      copies.push_back(BRepBuilderAPI_Transform(src, t, true).Shape());
      placed.push_back(hit);
    }
    if (copies.empty()) throw Standard_Failure("No spot on the target took a copy: check the slope limit, spacing and avoid bodies");
    std::vector<int> ids;
    for (const auto& c : copies) ids.push_back(d->add(c));
    if (!o.keepOriginal) d->bodies.erase(source);
    return ids;
  }, std::vector<int>{});
}

std::vector<int> Kernel::linearPattern(int body, const Vec3& direction, int count, double spacing, bool fuse) {
  return d->guard([&] {
    if (count < 2) throw Standard_Failure("Pattern needs at least two instances");
    gp_Vec dir = unitVec(direction, "Pattern direction is zero");
    TopoDS_Shape& s = d->get(body);
    std::vector<TopoDS_Shape> copies;
    for (int i = 1; i < count; ++i) {
      gp_Trsf trsf;
      trsf.SetTranslation(dir * (spacing * i));
      copies.push_back(BRepBuilderAPI_Transform(s, trsf, true).Shape());
    }
    std::vector<int> ids{body};
    if (fuse) {
      s = fuseAll(s, copies);
    } else {
      for (const auto& c : copies) ids.push_back(d->add(c));
    }
    return ids;
  }, std::vector<int>{});
}

std::vector<int> Kernel::circularPattern(int body, const Vec3& axisOrigin, const Vec3& axisDir, int count,
                                         double totalAngleDeg, bool fuse) {
  return d->guard([&] {
    if (count < 2) throw Standard_Failure("Pattern needs at least two instances");
    gp_Ax1 axis(toPnt(axisOrigin), gp_Dir(unitVec(axisDir, "Pattern axis is zero")));
    bool fullTurn = std::abs(totalAngleDeg) >= 360.0 - 1e-9;
    double step = (fullTurn ? std::copysign(360.0, totalAngleDeg) / count : totalAngleDeg / (count - 1)) * M_PI / 180.0;
    TopoDS_Shape& s = d->get(body);
    std::vector<TopoDS_Shape> copies;
    for (int i = 1; i < count; ++i) {
      gp_Trsf trsf;
      trsf.SetRotation(axis, step * i);
      copies.push_back(BRepBuilderAPI_Transform(s, trsf, true).Shape());
    }
    std::vector<int> ids{body};
    if (fuse) {
      s = fuseAll(s, copies);
    } else {
      for (const auto& c : copies) ids.push_back(d->add(c));
    }
    return ids;
  }, std::vector<int>{});
}

std::vector<int> Kernel::split(int body, const Plane& plane) {
  return d->guard([&] {
    TopoDS_Shape& s = d->get(body);
    // A planar face large enough to cross the whole body.
    Bnd_Box box;
    BRepBndLib::Add(s, box);
    gp_Pnt mid = box.IsVoid() ? gp::Origin() : gp_Pnt((box.CornerMin().XYZ() + box.CornerMax().XYZ()) / 2);
    double half = mid.Distance(toPnt(plane.origin)) + shapeSize(s) + 1.0;
    gp_Pln pln(gp_Ax3(toPnt(plane.origin), toDir(plane.normal), toDir(plane.xDir)));
    TopoDS_Face cutter = BRepBuilderAPI_MakeFace(pln, -half, half, -half, half).Face();

    TopTools_ListOfShape args, tools;
    args.Append(s);
    tools.Append(cutter);
    BRepAlgoAPI_Splitter splitter;
    splitter.SetArguments(args);
    splitter.SetTools(tools);
    splitter.SetRunParallel(false);
    splitter.Build();
    if (!splitter.IsDone()) throw Standard_Failure("Split failed");

    std::vector<TopoDS_Shape> pieces;
    for (TopExp_Explorer ex(splitter.Shape(), TopAbs_SOLID); ex.More(); ex.Next()) pieces.push_back(ex.Current());
    if (pieces.size() < 2) throw Standard_Failure("The plane does not cut the body");
    s = unify(pieces[0]);
    std::vector<int> ids{body};
    for (size_t i = 1; i < pieces.size(); ++i) ids.push_back(d->add(unify(pieces[i])));
    return ids;
  }, std::vector<int>{});
}

bool Kernel::deleteFaces(int body, const std::vector<int>& faces) {
  return d->guard([&] {
    if (faces.empty()) throw Standard_Failure("Select faces to delete");
    TopoDS_Shape& s = d->get(body);
    TopTools_ListOfShape list;
    for (int f : faces) list.Append(Impl::face(s, f));
    BRepAlgoAPI_Defeaturing df;
    df.SetShape(s);
    df.AddFacesToRemove(list);
    df.SetRunParallel(false);
    df.SetToFillHistory(false);
    df.Build();
    if (!df.IsDone() || df.Shape().IsNull()) throw Standard_Failure("Delete faces failed: the gap cannot be healed");
    s = unify(df.Shape());
    return true;
  }, false);
}

bool Kernel::draft(int body, const std::vector<int>& faces, const Vec3& pullDirection, double angleDeg) {
  return d->guard([&] {
    if (faces.empty()) throw Standard_Failure("Select faces to draft");
    TopoDS_Shape& s = d->get(body);
    gp_Dir pull(unitVec(pullDirection, "Pull direction is zero"));
    double angle = angleDeg * M_PI / 180.0;
    BRepOffsetAPI_DraftAngle mk(s);
    for (int fi : faces) {
      TopoDS_Face f = Impl::face(s, fi);
      // Neutral plane: perpendicular to the pull, through the face's lowest vertex.
      gp_Pnt lowest;
      double best = INFINITY;
      for (TopExp_Explorer ex(f, TopAbs_VERTEX); ex.More(); ex.Next()) {
        gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(ex.Current()));
        double h = gp_Vec(p.XYZ()).Dot(gp_Vec(pull));
        if (h < best) best = h, lowest = p;
      }
      if (!std::isfinite(best)) throw Standard_Failure("Face has no vertices");
      mk.Add(f, pull, angle, gp_Pln(lowest, pull));
      if (!mk.AddDone()) throw Standard_Failure("Draft failed: a face cannot be tapered along the pull direction");
    }
    mk.Build();
    if (!mk.IsDone() || mk.Shape().IsNull()) throw Standard_Failure("Draft failed");
    s = unify(mk.Shape());
    return true;
  }, false);
}

bool Kernel::offsetFaces(int body, const std::vector<int>& faces, double distance) {
  return d->guard([&] {
    if (faces.empty()) throw Standard_Failure("Select faces to offset");
    if (std::abs(distance) < 1e-9) throw Standard_Failure("Distance must not be zero");
    TopoDS_Shape& s = d->get(body);
    std::vector<TopoDS_Face> list;
    for (int f : faces) list.push_back(Impl::face(s, f));
    s = offsetShapeFaces(s, list, distance);
    return true;
  }, false);
}

namespace {
// The solid of an offset result (offsets of solids can come back as a compound or shell).
TopoDS_Shape offsetSolid(const TopoDS_Shape& s, double distance) {
  BRepOffsetAPI_MakeOffsetShape mk;
  mk.PerformByJoin(s, distance, 1e-5, BRepOffset_Skin, false, false, GeomAbs_Intersection);
  if (!mk.IsDone() || mk.Shape().IsNull()) throw Standard_Failure("Can't offset this body's surface by that height");
  TopoDS_Shape r = mk.Shape();
  TopExp_Explorer ex(r, TopAbs_SOLID);
  if (ex.More()) return ex.Current();
  TopExp_Explorer shells(r, TopAbs_SHELL);
  if (shells.More()) {
    BRep_Builder b;
    TopoDS_Solid solid;
    b.MakeSolid(solid);
    b.Add(solid, shells.Current());
    BRepLib::OrientClosedSolid(solid);
    return solid;
  }
  throw Standard_Failure("Offsetting the body failed");
}
}  // namespace

bool Kernel::emboss(int body, const Plane& plane, const std::vector<Profile>& profiles, double height, bool deboss) {
  return d->guard([&] {
    if (height <= 1e-9) throw Standard_Failure("Height must be more than zero");
    TopoDS_Shape& s = d->get(body);
    gp_Dir n = toDir(plane.normal);
    gp_Pnt o = toPnt(plane.origin);
    // A prism of the profiles reaching through the whole body, both ways.
    double span = shapeSize(s) * 2 + gp_Vec(o, gp_Pnt(0, 0, 0)).Magnitude() + 1;
    gp_Trsf back;
    back.SetTranslation(gp_Vec(n) * -span);
    TopoDS_Shape prism = sweepProfiles(plane, profiles, [&](const TopoDS_Face& f) {
      TopoDS_Shape start = BRepBuilderAPI_Transform(f, back, true).Shape();
      return BRepPrimAPI_MakePrism(start, gp_Vec(n) * (2 * span)).Shape();
    });
    // The skin of the body the emboss lives in: outside it (raised) or inside it (sunk).
    TopoDS_Shape skin = deboss ? BRepAlgoAPI_Cut(s, offsetSolid(s, -height)).Shape()
                               : BRepAlgoAPI_Cut(offsetSolid(s, height), s).Shape();
    TopoDS_Shape pieces = BRepAlgoAPI_Common(skin, prism).Shape();
    // Keep only the pieces on the side facing the sketch plane, not those the
    // prism also crosses on the far side of the body.
    double nearest = 1e300, farthest = -1e300;
    std::vector<std::pair<double, TopoDS_Shape>> solids;
    for (TopExp_Explorer ex(pieces, TopAbs_SOLID); ex.More(); ex.Next()) {
      GProp_GProps props;
      BRepGProp::VolumeProperties(ex.Current(), props);
      if (props.Mass() < 1e-9) continue;
      double dist = gp_Vec(o, props.CentreOfMass()).Dot(gp_Vec(n));
      solids.emplace_back(dist, ex.Current());
    }
    if (solids.empty()) throw Standard_Failure("The sketch doesn't project onto the body");
    // Distance of the body's surfaces from the plane along the normal.
    Bnd_Box box;
    BRepBndLib::Add(s, box);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    for (double x : {xmin, xmax})
      for (double y : {ymin, ymax})
        for (double z : {zmin, zmax}) {
          double dist = gp_Vec(o, gp_Pnt(x, y, z)).Dot(gp_Vec(n));
          nearest = std::min(nearest, dist);
          farthest = std::max(farthest, dist);
        }
    // The sketch looks along -normal when the body is behind it, +normal otherwise.
    bool behind = std::abs(farthest) + std::abs(nearest) > 0 && (nearest + farthest) / 2 < 0;
    double front = behind ? farthest : nearest, mid = (nearest + farthest) / 2;
    BRep_Builder b;
    TopoDS_Compound keep;
    b.MakeCompound(keep);
    int kept = 0;
    for (auto& [dist, solid] : solids) {
      if (std::abs(dist - front) <= std::abs(dist - mid) + 1e-9) {
        b.Add(keep, solid);
        ++kept;
      }
    }
    if (kept == 0) throw Standard_Failure("The sketch doesn't project onto the body");
    TopoDS_Shape result = deboss ? BRepAlgoAPI_Cut(s, keep).Shape() : BRepAlgoAPI_Fuse(s, keep).Shape();
    if (result.IsNull()) throw Standard_Failure("Emboss failed");
    s = unify(result);
    return true;
  }, false);
}

Kernel::DrawingView Kernel::drawingView(const std::vector<int>& bodies, const Vec3& viewDirection, const Vec3& up) const {
  return d->guard([&] {
    DrawingView out;
    BRep_Builder b;
    TopoDS_Compound all;
    b.MakeCompound(all);
    for (int id : bodies) b.Add(all, d->get(id));
    gp_Dir view = toDir(viewDirection);
    gp_Dir v = toDir(up);
    gp_Dir u = v.Crossed(view.Reversed());  // right = up × toward-viewer
    // HLR looks down -Z of the projector's frame: Z points at the viewer.
    gp_Ax2 frame(gp_Pnt(0, 0, 0), view.Reversed(), u);
    Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
    algo->Add(all);
    algo->Projector(HLRAlgo_Projector(frame));
    algo->Update();
    algo->Hide();
    HLRBRep_HLRToShape shapes(algo);
    auto collect = [&](const TopoDS_Shape& edges, std::vector<std::vector<std::array<double, 2>>>& into) {
      if (edges.IsNull()) return;
      for (TopExp_Explorer ex(edges, TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Edge e = TopoDS::Edge(ex.Current());
        BRepLib::BuildCurves3d(e);
        BRepAdaptor_Curve c(e);
        std::vector<std::array<double, 2>> line;
        if (c.GetType() == GeomAbs_Line) {
          gp_Pnt p = c.Value(c.FirstParameter()), q = c.Value(c.LastParameter());
          line.push_back({p.X(), p.Y()});
          line.push_back({q.X(), q.Y()});
        } else {
          GCPnts_TangentialDeflection sampler(c, 0.05, std::max(1e-4, shapeSize(all) * 0.0005));
          for (int i = 1; i <= sampler.NbPoints(); ++i) {
            gp_Pnt p = sampler.Value(i);
            line.push_back({p.X(), p.Y()});
          }
        }
        if (line.size() >= 2) into.push_back(std::move(line));
      }
    };
    // Sharp edges, smooth (tangent) edges and silhouettes.
    collect(shapes.VCompound(), out.visible);
    collect(shapes.OutLineVCompound(), out.visible);
    collect(shapes.Rg1LineVCompound(), out.visible);
    collect(shapes.HCompound(), out.hidden);
    collect(shapes.OutLineHCompound(), out.hidden);
    return out;
  }, DrawingView{});
}

bool Kernel::replaceFace(int body, int face, int targetBody, int targetFace) {
  return d->guard([&] {
    gp_Pnt tc, sc;
    gp_Dir tn, sn, x;
    TopoDS_Face target = Impl::face(d->get(targetBody), targetFace);
    if (!Impl::planarFrame(target, tc, tn, x)) throw Standard_Failure("Replace face needs a planar target face");
    TopoDS_Shape& s = d->get(body);
    TopoDS_Face f = Impl::face(s, face);
    if (!Impl::planarFrame(f, sc, sn, x)) throw Standard_Failure("Replace face needs a planar face to move");
    if (std::abs(sn.Dot(tn)) < std::cos(0.5 * M_PI / 180.0))
      throw Standard_Failure("Replace face: the faces are not parallel");
    // Signed distance along the face's outward normal to the target plane.
    double distance = gp_Vec(sc, tc).Dot(gp_Vec(sn));
    if (std::abs(distance) < 1e-9) return true;
    s = offsetShapeFaces(s, {f}, distance);
    return true;
  }, false);
}

int Kernel::fill(int body, const std::vector<int>& edges) {
  return d->guard([&] {
    if (edges.size() < 1) throw Standard_Failure("Select a closed loop of edges to fill");
    const TopoDS_Shape& s = d->get(body);
    BRepOffsetAPI_MakeFilling mk;
    for (int e : edges) mk.Add(Impl::edge(s, e), GeomAbs_C0, true);
    mk.Build();
    if (!mk.IsDone() || mk.Shape().IsNull()) throw Standard_Failure("Fill failed: the edges may not form a closed loop");
    return d->add(mk.Shape());
  }, -1);
}

int Kernel::mirror(int body, const Plane& plane, bool keepOriginal) {
  return d->guard([&] {
    gp_Trsf trsf;
    trsf.SetMirror(gp_Ax2(toPnt(plane.origin), toDir(plane.normal)));
    TopoDS_Shape m = BRepBuilderAPI_Transform(d->get(body), trsf, true).Shape();
    if (keepOriginal) return d->add(m);
    d->get(body) = m;
    return body;
  }, -1);
}

int Kernel::duplicate(int body) {
  return d->guard([&] { return d->add(d->get(body)); }, -1);
}

bool Kernel::remove(int body) {
  return d->guard([&] {
    d->get(body);
    d->bodies.erase(body);
    return true;
  }, false);
}

std::string Kernel::toBrep(int body) const {
  auto it = d->bodies.find(body);
  if (it == d->bodies.end()) return {};
  std::ostringstream out;
  BRepTools::Write(it->second, out);
  return out.str();
}

int Kernel::fromBrep(const std::string& brep) {
  return d->guard([&] {
    std::istringstream in(brep);
    TopoDS_Shape s;
    BRep_Builder builder;
    BRepTools::Read(s, in, builder);
    return d->add(s);
  }, -1);
}

std::vector<int> Kernel::importFile(const std::string& path) {
  return d->guard([&] {
    std::string ext = lowerExtension(path);
    TopoDS_Shape shape;
    if (ext == "step" || ext == "stp") {
      STEPControl_Reader reader;
      if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) throw Standard_Failure("Could not read STEP file");
      reader.TransferRoots();
      shape = reader.OneShape();
    } else if (ext == "iges" || ext == "igs") {
      IGESControl_Controller::Init();
      IGESControl_Reader reader;
      if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) throw Standard_Failure("Could not read IGES file");
      reader.TransferRoots();
      shape = reader.OneShape();
    } else if (ext == "stl") {
      StlAPI_Reader reader;
      if (!reader.Read(shape, path.c_str())) throw Standard_Failure("Could not read STL file");
    } else if (ext == "brep") {
      BRep_Builder builder;
      if (!BRepTools::Read(shape, path.c_str(), builder)) throw Standard_Failure("Could not read BREP file");
    } else {
      throw Standard_Failure("Unsupported file type");
    }
    std::vector<int> ids;
    for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next()) ids.push_back(d->add(ex.Current()));
    if (ids.empty() && !shape.IsNull()) ids.push_back(d->add(shape));
    if (ids.empty()) throw Standard_Failure("File contains no geometry");
    return ids;
  }, std::vector<int>{});
}

bool Kernel::exportFile(const std::string& path, const std::vector<int>& ids) const {
  return d->guard([&] {
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (int id : ids) builder.Add(compound, d->get(id));

    std::string ext = lowerExtension(path);
    if (ext == "step" || ext == "stp") {
      STEPControl_Writer writer;
      Interface_Static::SetCVal("write.step.unit", "MM");
      if (writer.Transfer(compound, STEPControl_AsIs) != IFSelect_RetDone) throw Standard_Failure("STEP transfer failed");
      return writer.Write(path.c_str()) == IFSelect_RetDone;
    }
    if (ext == "iges" || ext == "igs") {
      IGESControl_Controller::Init();
      IGESControl_Writer writer("MM", 0);
      writer.AddShape(compound);
      writer.ComputeModel();
      return static_cast<bool>(writer.Write(path.c_str()));
    }
    if (ext == "stl") {
      BRepMesh_IncrementalMesh(compound, 0.05, false, 0.2, true);
      StlAPI_Writer writer;
      return static_cast<bool>(writer.Write(compound, path.c_str()));
    }
    if (ext == "brep") return static_cast<bool>(BRepTools::Write(compound, path.c_str()));
    throw Standard_Failure("Unsupported file type");
  }, false);
}

bool Kernel::tessellate(int body, Mesh& out, double quality) const {
  return d->guard([&] {
    const TopoDS_Shape& s = d->get(body);
    out = Mesh{};

    Bnd_Box box;
    BRepBndLib::Add(s, box);
    double diag = box.IsVoid() ? 1.0 : std::sqrt(box.SquareExtent());
    double linDefl = std::max(1e-4, diag * 0.0015 / quality);
    double angDefl = 0.35 / quality;
    BRepTools::Clean(s);
    BRepMesh_IncrementalMesh(s, linDefl, false, angDefl, true);

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(s, TopAbs_FACE, faces);
    out.faceCount = static_cast<uint32_t>(faces.Extent());
    for (int fi = 1; fi <= faces.Extent(); ++fi) {
      TopoDS_Face face = TopoDS::Face(faces(fi));
      TopLoc_Location loc;
      Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
      if (tri.IsNull()) continue;
      if (!tri->HasNormals()) BRepLib_ToolTriangulatedShape::ComputeNormals(face, tri);
      bool reversed = face.Orientation() == TopAbs_REVERSED;
      const gp_Trsf& trsf = loc.Transformation();
      uint32_t base = static_cast<uint32_t>(out.positions.size() / 3);
      for (int i = 1; i <= tri->NbNodes(); ++i) {
        gp_Pnt p = tri->Node(i).Transformed(trsf);
        gp_Dir n = tri->Normal(i).Transformed(trsf);
        if (reversed) n.Reverse();
        out.positions.insert(out.positions.end(), {float(p.X()), float(p.Y()), float(p.Z())});
        out.normals.insert(out.normals.end(), {float(n.X()), float(n.Y()), float(n.Z())});
        out.vertexFace.push_back(uint32_t(fi - 1));
      }
      for (int i = 1; i <= tri->NbTriangles(); ++i) {
        int a, b, c;
        tri->Triangle(i).Get(a, b, c);
        if (reversed) std::swap(b, c);
        out.indices.insert(out.indices.end(), {base + a - 1, base + b - 1, base + c - 1});
      }
    }

    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(s, TopAbs_EDGE, edges);
    out.edgeCount = static_cast<uint32_t>(edges.Extent());
    for (int ei = 1; ei <= edges.Extent(); ++ei) {
      TopoDS_Edge edge = TopoDS::Edge(edges(ei));
      if (BRep_Tool::Degenerated(edge)) continue;
      BRepAdaptor_Curve curve(edge);
      GCPnts_TangentialDeflection sampler(curve, angDefl * 0.5, linDefl);
      for (int i = 1; i < sampler.NbPoints(); ++i) {
        for (int k : {i, i + 1}) {
          gp_Pnt p = sampler.Value(k);
          out.edgePositions.insert(out.edgePositions.end(), {float(p.X()), float(p.Y()), float(p.Z())});
          out.edgeIds.push_back(uint32_t(ei - 1));
        }
      }
    }
    return true;
  }, false);
}

bool Kernel::faceInfo(int body, int face, FaceInfo& out) const {
  return d->guard([&] {
    TopoDS_Face f = Impl::face(d->get(body), face);
    gp_Pnt c;
    gp_Dir n, x;
    out = FaceInfo{};
    out.planar = Impl::planarFrame(f, c, n, x);
    if (out.planar) {
      out.centroid = toVec(c.XYZ());
      out.normal = toVec(n.XYZ());
      out.xDir = toVec(x.XYZ());
    }
    out.area = faceArea(f);
    BRepAdaptor_Surface surf(f);
    switch (surf.GetType()) {
      case GeomAbs_Cylinder: {
        gp_Cylinder c = surf.Cylinder();
        out.cylindrical = true;
        out.axisOrigin = toVec(c.Location().XYZ());
        out.axisDir = toVec(c.Axis().Direction().XYZ());
        out.radius = c.Radius();
        break;
      }
      case GeomAbs_Cone: {
        gp_Cone c = surf.Cone();
        out.axisOrigin = toVec(c.Location().XYZ());
        out.axisDir = toVec(c.Axis().Direction().XYZ());
        out.radius = c.RefRadius();
        break;
      }
      case GeomAbs_Sphere: {
        gp_Sphere sp = surf.Sphere();
        out.axisOrigin = toVec(sp.Location().XYZ());
        out.axisDir = toVec(sp.Position().Direction().XYZ());
        out.radius = sp.Radius();
        break;
      }
      default:
        break;
    }
    return true;
  }, false);
}

bool Kernel::edgeInfo(int body, int edge, EdgeInfo& out) const {
  return d->guard([&] {
    TopoDS_Edge e = Impl::edge(d->get(body), edge);
    out = EdgeInfo{};
    if (BRep_Tool::Degenerated(e)) throw Standard_Failure("Edge is degenerated");
    BRepAdaptor_Curve curve(e);
    gp_Pnt p0 = curve.Value(curve.FirstParameter()), p1 = curve.Value(curve.LastParameter());
    if (e.Orientation() == TopAbs_REVERSED) std::swap(p0, p1);
    out.start = toVec(p0.XYZ());
    out.end = toVec(p1.XYZ());
    out.length = edgeLength(e);
    if (curve.GetType() == GeomAbs_Line) {
      out.linear = true;
    } else if (curve.GetType() == GeomAbs_Circle) {
      gp_Circ c = curve.Circle();
      out.circular = true;
      out.center = toVec(c.Location().XYZ());
      out.axis = toVec(c.Axis().Direction().XYZ());
      out.radius = c.Radius();
    }
    return true;
  }, false);
}

std::vector<std::vector<std::array<double, 2>>> Kernel::projectEdges(int body, const Plane& plane) const {
  using Polyline = std::vector<std::array<double, 2>>;
  return d->guard([&] {
    const TopoDS_Shape& s = d->get(body);
    PlaneFrame f(plane);
    double size = shapeSize(s);
    double linDefl = std::max(1e-4, size * 0.001), angDefl = 0.1;
    auto project = [&](const gp_Pnt& p) -> std::array<double, 2> {
      gp_Vec v(f.origin, p);
      return {v.Dot(gp_Vec(f.x)), v.Dot(gp_Vec(f.y))};
    };
    std::vector<Polyline> out;
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(s, TopAbs_EDGE, edges);
    for (int ei = 1; ei <= edges.Extent(); ++ei) {
      TopoDS_Edge edge = TopoDS::Edge(edges(ei));
      if (BRep_Tool::Degenerated(edge)) continue;
      BRepAdaptor_Curve curve(edge);
      Polyline line;
      if (curve.GetType() == GeomAbs_Line) {
        line.push_back(project(curve.Value(curve.FirstParameter())));
        line.push_back(project(curve.Value(curve.LastParameter())));
      } else {
        GCPnts_TangentialDeflection sampler(curve, angDefl, linDefl);
        for (int i = 1; i <= sampler.NbPoints(); ++i) line.push_back(project(sampler.Value(i)));
      }
      // Skip edges seen end-on: every point lands on (nearly) the same spot.
      double extent = 0;
      for (const auto& q : line)
        extent = std::max(extent, std::hypot(q[0] - line.front()[0], q[1] - line.front()[1]));
      if (line.size() < 2 || extent < size * 1e-7 + 1e-9) continue;
      out.push_back(std::move(line));
    }
    return out;
  }, std::vector<Polyline>{});
}

bool Kernel::properties(int body, Properties& out) const {
  return d->guard([&] {
    const TopoDS_Shape& s = d->get(body);
    GProp_GProps vol, surf;
    BRepGProp::VolumeProperties(s, vol);
    BRepGProp::SurfaceProperties(s, surf);
    Bnd_Box box;
    BRepBndLib::Add(s, box);
    TopTools_IndexedMapOfShape faces, edges;
    TopExp::MapShapes(s, TopAbs_FACE, faces);
    TopExp::MapShapes(s, TopAbs_EDGE, edges);
    out.volume = std::abs(vol.Mass());
    out.area = surf.Mass();
    if (!box.IsVoid()) {
      out.bboxMin = toVec(box.CornerMin().XYZ());
      out.bboxMax = toVec(box.CornerMax().XYZ());
    }
    out.faceCount = faces.Extent();
    out.edgeCount = edges.Extent();
    return true;
  }, false);
}

namespace {

TopoDS_Shape refShape(const TopoDS_Shape& body, const Ref& r) {
  switch (r.kind) {
    case RefKind::Face: {
      TopTools_IndexedMapOfShape map;
      TopExp::MapShapes(body, TopAbs_FACE, map);
      if (r.index < 0 || r.index >= map.Extent()) throw Standard_Failure("Face does not exist");
      return map(r.index + 1);
    }
    case RefKind::Edge: {
      TopTools_IndexedMapOfShape map;
      TopExp::MapShapes(body, TopAbs_EDGE, map);
      if (r.index < 0 || r.index >= map.Extent()) throw Standard_Failure("Edge does not exist");
      return map(r.index + 1);
    }
    case RefKind::Body:
      break;
  }
  return body;
}

// Fills radius/length/area for a single entity.
void measureSingle(const TopoDS_Shape& s, RefKind kind, Measurement& out) {
  if (kind == RefKind::Edge) {
    out.hasLength = true;
    out.length = edgeLength(s);
    BRepAdaptor_Curve curve(TopoDS::Edge(s));
    if (curve.GetType() == GeomAbs_Circle) {
      out.hasRadius = true;
      out.radius = curve.Circle().Radius();
    }
  } else if (kind == RefKind::Face) {
    out.hasArea = true;
    out.area = faceArea(s);
    BRepAdaptor_Surface surf(TopoDS::Face(s));
    if (surf.GetType() == GeomAbs_Cylinder) {
      out.hasRadius = true;
      out.radius = surf.Cylinder().Radius();
    } else if (surf.GetType() == GeomAbs_Sphere) {
      out.hasRadius = true;
      out.radius = surf.Sphere().Radius();
    }
  } else {
    out.hasArea = true;
    out.area = faceArea(s);
  }
}

}  // namespace

bool Kernel::measure(const Ref& a, const Ref& b, Measurement& out) const {
  return d->guard([&] {
    out = Measurement{};
    TopoDS_Shape sa = refShape(d->get(a.body), a);
    if (b.body < 0) {
      measureSingle(sa, a.kind, out);
      return true;
    }
    TopoDS_Shape sb = refShape(d->get(b.body), b);

    BRepExtrema_DistShapeShape dist(sa, sb);
    if (dist.IsDone() && dist.NbSolution() > 0) {
      out.hasDistance = true;
      out.distance = dist.Value();
      out.p1 = toVec(dist.PointOnShape1(1).XYZ());
      out.p2 = toVec(dist.PointOnShape2(1).XYZ());
    }

    if (a.kind == RefKind::Face && b.kind == RefKind::Face) {
      // Angle between the outward normals of two planar faces, 0..180.
      gp_Pnt c;
      gp_Dir na, nb, x;
      if (Impl::planarFrame(TopoDS::Face(sa), c, na, x) && Impl::planarFrame(TopoDS::Face(sb), c, nb, x)) {
        out.hasAngle = true;
        out.angleDeg = na.Angle(nb) * 180.0 / M_PI;
      }
    } else if (a.kind == RefKind::Edge && b.kind == RefKind::Edge) {
      out.hasLength = true;
      out.length = edgeLength(sa) + edgeLength(sb);
      // Angle between two straight edges as lines, 0..90.
      BRepAdaptor_Curve ca(TopoDS::Edge(sa)), cb(TopoDS::Edge(sb));
      if (ca.GetType() == GeomAbs_Line && cb.GetType() == GeomAbs_Line) {
        double angle = ca.Line().Direction().Angle(cb.Line().Direction()) * 180.0 / M_PI;
        out.hasAngle = true;
        out.angleDeg = std::min(angle, 180.0 - angle);
      }
    }
    return true;
  }, false);
}

}  // namespace o3d
