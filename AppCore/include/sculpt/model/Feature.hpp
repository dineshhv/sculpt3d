// Port of Feature.swift: the parametric history's operations and features,
// and the inspector's parameters and choices for each operation.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "o3d/Kernel.hpp"
#include "sculpt/model/Equatable.hpp"
#include "sculpt/model/Math.hpp"
#include "sculpt/model/Sketch.hpp"
#include "sculpt/model/Terrain.hpp"
#include "sculpt/model/Uuid.hpp"
#include "sculpt/model/Visualization.hpp"

namespace sculpt {

class ExpressionEvaluator;

enum class PrimitiveKind { box, cylinder, sphere, cone, torus };
const std::vector<PrimitiveKind>& allPrimitiveKinds();
std::string rawValue(PrimitiveKind k);
std::optional<PrimitiveKind> primitiveKind(const std::string& raw);
std::string title(PrimitiveKind k);
std::string symbol(PrimitiveKind k);
Vec3d defaultDims(PrimitiveKind k);
std::vector<std::string> dimLabels(PrimitiveKind k);
o3d::Primitive bridged(PrimitiveKind k);

enum class CombineMode { newBody, join, cut };
const std::vector<CombineMode>& allCombineModes();
std::string rawValue(CombineMode m);
std::optional<CombineMode> combineMode(const std::string& raw);
std::string title(CombineMode m);
o3d::CombineMode bridged(CombineMode m);

enum class BooleanKind { union_, subtract, intersect };
const std::vector<BooleanKind>& allBooleanKinds();
std::string rawValue(BooleanKind k);  // "union", "subtract", "intersect"
std::optional<BooleanKind> booleanKind(const std::string& raw);
std::string title(BooleanKind k);
std::string symbol(BooleanKind k);
o3d::BoolOp bridged(BooleanKind k);

/// Options of Transform ▸ Scatter.
struct ScatterSettings {
  int count = 12;
  int seed = 1;
  double minScale = 0.8;
  double maxScale = 1.2;
  /// Minimum distance between copies on the ground plan, mm.
  double spacing = 0.0;
  /// Steeper ground than this (degrees from flat) takes no copies.
  double maxSlope = 40.0;
  /// How far each copy is pushed into the ground, as a fraction of its height.
  double sink = 0.02;
  bool randomRotation = true;
  /// Tilt copies to the surface normal instead of keeping them upright.
  bool alignToSurface = false;
  bool keepOriginal = false;
  SCULPT_FIELDS(count, seed, minScale, maxScale, spacing, maxSlope, sink, randomRotation, alignToSurface, keepOriginal)
};

/// The cases of Swift's `enum Operation`, one struct each, with the Swift
/// associated-value labels as field names (they are the JSON keys).
namespace op {
using sculpt::operator==;
using sculpt::operator!=;

struct Primitive { PrimitiveKind kind = PrimitiveKind::box; Vec3d dims, position; SCULPT_FIELDS(kind, dims, position) };
struct Extrude { Sketch sketch; double distance = 0; CombineMode mode = CombineMode::newBody; std::optional<int> target;
  SCULPT_FIELDS(sketch, distance, mode, target) };
struct Revolve { Sketch sketch; double angle = 0; bool aroundV = true; CombineMode mode = CombineMode::newBody;
  std::optional<int> target; SCULPT_FIELDS(sketch, angle, aroundV, mode, target) };
struct PushPull { int body = 0, face = 0; double distance = 0; SCULPT_FIELDS(body, face, distance) };
struct Boolean { BooleanKind kind = BooleanKind::union_; int target = 0, tool = 0; SCULPT_FIELDS(kind, target, tool) };
struct Fillet { int body = 0; std::vector<int> edges; double radius = 0; SCULPT_FIELDS(body, edges, radius) };
struct Chamfer { int body = 0; std::vector<int> edges; double distance = 0; SCULPT_FIELDS(body, edges, distance) };
struct Shell { int body = 0; std::vector<int> faces; double thickness = 0; SCULPT_FIELDS(body, faces, thickness) };
struct Move { int body = 0; Vec3d translation, axis; double angle = 0; SCULPT_FIELDS(body, translation, axis, angle) };
struct Mirror { int body = 0; SketchPlane plane; bool keepOriginal = true; SCULPT_FIELDS(body, plane, keepOriginal) };
struct Duplicate { int body = 0; SCULPT_FIELDS(body) };
struct Delete { int body = 0; SCULPT_FIELDS(body) };
struct Imported { std::string name, brep; SCULPT_FIELDS(name, brep) };
/// A standalone sketch, kept for sweeps, lofts and later extrusions (Swift `.sketch`).
struct SketchItem { Sketch sketch; std::string name; SCULPT_FIELDS(sketch, name) };
/// A construction plane: `base` turned by `angle` about its X axis, then moved `offset` along its normal.
struct Plane { SketchPlane base; double offset = 0, angle = 0; std::string name; SCULPT_FIELDS(base, offset, angle, name) };
struct Sweep { Uuid profile, path; CombineMode mode = CombineMode::newBody; std::optional<int> target;
  SCULPT_FIELDS(profile, path, mode, target) };
struct Loft { std::vector<Uuid> sections; CombineMode mode = CombineMode::newBody; std::optional<int> target;
  SCULPT_FIELDS(sections, mode, target) };
struct Scale { int body = 0; double factor = 1; Vec3d center; SCULPT_FIELDS(body, factor, center) };
struct LinearPattern { int body = 0; Vec3d direction; int count = 2; double spacing = 0; bool fuse = false;
  SCULPT_FIELDS(body, direction, count, spacing, fuse) };
struct CircularPattern { int body = 0; Vec3d origin, axis; int count = 2; double angle = 360; bool fuse = false;
  SCULPT_FIELDS(body, origin, axis, count, angle, fuse) };
struct Split { int body = 0; SketchPlane plane; double offset = 0; SCULPT_FIELDS(body, plane, offset) };
struct DeleteFaces { int body = 0; std::vector<int> faces; SCULPT_FIELDS(body, faces) };
struct Draft { int body = 0; std::vector<int> faces; Vec3d pull; double angle = 0; SCULPT_FIELDS(body, faces, pull, angle) };
struct OffsetFaces { int body = 0; std::vector<int> faces; double distance = 0; SCULPT_FIELDS(body, faces, distance) };
/// A construction axis.
struct Axis { Vec3d origin, direction; std::string name; SCULPT_FIELDS(origin, direction, name) };
struct Translate { int body = 0; Vec3d direction; double distance = 0; SCULPT_FIELDS(body, direction, distance) };
struct Rotate { int body = 0; Vec3d origin, axis; double angle = 0; SCULPT_FIELDS(body, origin, axis, angle) };
/// Moves `body` so its bounding box lines up with `target`'s. Per axis: 0 leave, 1 minimum, 2 centre, 3 maximum.
struct Align { int body = 0, target = 0; std::vector<int> modes; SCULPT_FIELDS(body, target, modes) };
struct Image { ReferenceImage image; SCULPT_FIELDS(image) };
struct ReplaceFace { int body = 0, face = 0, targetBody = 0, targetFace = 0; SCULPT_FIELDS(body, face, targetBody, targetFace) };
struct Fill { int body = 0; std::vector<int> edges; SCULPT_FIELDS(body, edges) };
/// Edges of a body projected onto a plane, kept as a sketch.
struct Projection { int body = 0; SketchPlane plane; std::string name; SCULPT_FIELDS(body, plane, name) };
/// Tools ▸ Wrap & Emboss: a sketch projected onto a body and raised or sunk.
struct Emboss { Uuid sketch; int body = 0; double height = 0; bool deboss = false; SCULPT_FIELDS(sketch, body, height, deboss) };
/// Scales along each world axis about `center`; `uniform` keeps the three factors equal.
struct ScaleAxes { int body = 0; Vec3d factors, center; bool uniform = true; SCULPT_FIELDS(body, factors, center, uniform) };
/// Randomised copies of `source` placed on the top surface of `target`.
struct Scatter { int source = 0, target = 0; std::vector<int> avoid; ScatterSettings settings;
  SCULPT_FIELDS(source, target, avoid, settings) };
/// Insert ▸ Terrain: a smooth landscape from a height grid.
struct Terrain { TerrainSettings settings; SCULPT_FIELDS(settings) };
}  // namespace op

/// One step of the parametric history. Body ids refer to the ids the kernel
/// assigns during replay, which are deterministic for a given history.
/// The variant order matches `OpKind` and `opCaseName`.
struct Operation {
  using Variant =
      std::variant<op::Primitive, op::Extrude, op::Revolve, op::PushPull, op::Boolean, op::Fillet, op::Chamfer, op::Shell,
                   op::Move, op::Mirror, op::Duplicate, op::Delete, op::Imported, op::SketchItem, op::Plane, op::Sweep,
                   op::Loft, op::Scale, op::LinearPattern, op::CircularPattern, op::Split, op::DeleteFaces, op::Draft,
                   op::OffsetFaces, op::Axis, op::Translate, op::Rotate, op::Align, op::Image, op::ReplaceFace, op::Fill,
                   op::Projection, op::Emboss, op::ScaleAxes, op::Scatter, op::Terrain>;
  Variant v;

  Operation() = default;
  template <class T, class = std::enable_if_t<!std::is_same<std::decay_t<T>, Operation>::value>>
  Operation(T&& value) : v(std::forward<T>(value)) {}

  friend bool operator==(const Operation& a, const Operation& b) { return a.v == b.v; }
  friend bool operator!=(const Operation& a, const Operation& b) { return !(a.v == b.v); }

  template <class T> bool is() const { return std::holds_alternative<T>(v); }
  template <class T> const T* as() const { return std::get_if<T>(&v); }
  template <class T> T* as() { return std::get_if<T>(&v); }

  /// The Swift case name ("primitive", "extrude", …), the JSON key.
  std::string caseName() const;

  // MARK: Inspector (Feature.swift `extension Operation`)
  struct Parameter {
    int id;
    std::string label;
    double value;
    std::string unit = "mm";
  };
  /// A pick-one option, for the inspector.
  struct Choice {
    int id;
    std::string label;
    std::vector<std::string> options;
    int selected;
  };
  std::vector<Parameter> parameters() const;
  std::vector<Choice> choices() const;
  Operation settingChoice(int id, int index) const;
  Operation setting(int id, double value) const;
  /// Combine mode for sketch features, when a target body is available.
  std::optional<CombineMode> combineMode() const;
  Operation setting(CombineMode mode, std::optional<int> target) const;
  const Sketch* sketch() const;
  Operation setting(const Sketch& sketch) const;
  std::optional<int> sketchTarget() const;
  /// Revolve's axis picker (OperationPanel.swift): around the sketch's V (vertical) or U axis.
  Operation settingAroundV(bool aroundV) const;
};

struct Feature {
  Uuid id = Uuid::random();
  Operation op;
  bool suppressed = false;
  /// Parameter ids bound to expressions over project variables.
  std::optional<std::map<int, std::string>> bindings;
  SCULPT_FIELDS(id, op, suppressed, bindings)

  Feature() = default;
  explicit Feature(Operation o) : op(std::move(o)) {}
  Feature(Uuid i, Operation o, bool s = false) : id(i), op(std::move(o)), suppressed(s) {}

  /// The operation with every bound parameter set from its expression.
  Operation resolved(const ExpressionEvaluator& evaluator) const;
  std::string title() const;
  /// Features that only add reference geometry, not bodies.
  bool isReference() const;
  std::string symbol() const;
};

/// World axes offered for directions and plane normals.
const std::vector<Vec3d>& worldAxes();
int axisIndex(const Vec3d& v);
/// A plane with the given normal axis through `origin`.
SketchPlane axisPlane(int axis, const Vec3d& origin);

}  // namespace sculpt
