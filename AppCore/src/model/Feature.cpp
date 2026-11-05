// Port of Feature.swift.
#include "sculpt/model/Feature.hpp"

#include <algorithm>
#include <cmath>

#include "sculpt/model/Expression.hpp"

namespace sculpt {

// MARK: Enums

const std::vector<PrimitiveKind>& allPrimitiveKinds() {
  static const std::vector<PrimitiveKind> all = {PrimitiveKind::box, PrimitiveKind::cylinder, PrimitiveKind::sphere,
                                                 PrimitiveKind::cone, PrimitiveKind::torus};
  return all;
}

std::string rawValue(PrimitiveKind k) {
  switch (k) {
    case PrimitiveKind::box: return "box";
    case PrimitiveKind::cylinder: return "cylinder";
    case PrimitiveKind::sphere: return "sphere";
    case PrimitiveKind::cone: return "cone";
    case PrimitiveKind::torus: return "torus";
  }
  return "box";
}

std::optional<PrimitiveKind> primitiveKind(const std::string& raw) {
  for (auto k : allPrimitiveKinds())
    if (rawValue(k) == raw) return k;
  return std::nullopt;
}

static std::string capitalized(std::string s) {
  if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = char(s[0] - 'a' + 'A');
  return s;
}

std::string title(PrimitiveKind k) { return capitalized(rawValue(k)); }

std::string symbol(PrimitiveKind k) {
  switch (k) {
    case PrimitiveKind::box: return "cube";
    case PrimitiveKind::cylinder: return "cylinder";
    case PrimitiveKind::sphere: return "globe";
    case PrimitiveKind::cone: return "cone";
    case PrimitiveKind::torus: return "smallcircle.circle";
  }
  return "";
}

Vec3d defaultDims(PrimitiveKind k) {
  switch (k) {
    case PrimitiveKind::box: return {40, 40, 40};
    case PrimitiveKind::cylinder: return {20, 40, 0};
    case PrimitiveKind::sphere: return {20, 0, 0};
    case PrimitiveKind::cone: return {20, 5, 40};
    case PrimitiveKind::torus: return {25, 8, 0};
  }
  return {};
}

std::vector<std::string> dimLabels(PrimitiveKind k) {
  switch (k) {
    case PrimitiveKind::box: return {"Width", "Depth", "Height"};
    case PrimitiveKind::cylinder: return {"Radius", "Height"};
    case PrimitiveKind::sphere: return {"Radius"};
    case PrimitiveKind::cone: return {"Bottom radius", "Top radius", "Height"};
    case PrimitiveKind::torus: return {"Major radius", "Minor radius"};
  }
  return {};
}

o3d::Primitive bridged(PrimitiveKind k) {
  switch (k) {
    case PrimitiveKind::box: return o3d::Primitive::Box;
    case PrimitiveKind::cylinder: return o3d::Primitive::Cylinder;
    case PrimitiveKind::sphere: return o3d::Primitive::Sphere;
    case PrimitiveKind::cone: return o3d::Primitive::Cone;
    case PrimitiveKind::torus: return o3d::Primitive::Torus;
  }
  return o3d::Primitive::Box;
}

const std::vector<CombineMode>& allCombineModes() {
  static const std::vector<CombineMode> all = {CombineMode::newBody, CombineMode::join, CombineMode::cut};
  return all;
}

std::string rawValue(CombineMode m) {
  switch (m) {
    case CombineMode::newBody: return "newBody";
    case CombineMode::join: return "join";
    case CombineMode::cut: return "cut";
  }
  return "newBody";
}

std::optional<CombineMode> combineMode(const std::string& raw) {
  for (auto m : allCombineModes())
    if (rawValue(m) == raw) return m;
  return std::nullopt;
}

std::string title(CombineMode m) {
  switch (m) {
    case CombineMode::newBody: return "New body";
    case CombineMode::join: return "Join";
    case CombineMode::cut: return "Cut";
  }
  return "";
}

o3d::CombineMode bridged(CombineMode m) {
  switch (m) {
    case CombineMode::newBody: return o3d::CombineMode::NewBody;
    case CombineMode::join: return o3d::CombineMode::Join;
    case CombineMode::cut: return o3d::CombineMode::Cut;
  }
  return o3d::CombineMode::NewBody;
}

const std::vector<BooleanKind>& allBooleanKinds() {
  static const std::vector<BooleanKind> all = {BooleanKind::union_, BooleanKind::subtract, BooleanKind::intersect};
  return all;
}

std::string rawValue(BooleanKind k) {
  switch (k) {
    case BooleanKind::union_: return "union";
    case BooleanKind::subtract: return "subtract";
    case BooleanKind::intersect: return "intersect";
  }
  return "union";
}

std::optional<BooleanKind> booleanKind(const std::string& raw) {
  for (auto k : allBooleanKinds())
    if (rawValue(k) == raw) return k;
  return std::nullopt;
}

std::string title(BooleanKind k) { return capitalized(rawValue(k)); }

std::string symbol(BooleanKind k) {
  switch (k) {
    case BooleanKind::union_: return "square.on.square.squareshape.controlhandles";
    case BooleanKind::subtract: return "square.on.square.dashed";
    case BooleanKind::intersect: return "square.on.square.intersection.dashed";
  }
  return "";
}

o3d::BoolOp bridged(BooleanKind k) {
  switch (k) {
    case BooleanKind::union_: return o3d::BoolOp::Union;
    case BooleanKind::subtract: return o3d::BoolOp::Subtract;
    case BooleanKind::intersect: return o3d::BoolOp::Intersect;
  }
  return o3d::BoolOp::Union;
}

// MARK: Axes

const std::vector<Vec3d>& worldAxes() {
  static const std::vector<Vec3d> axes = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  return axes;
}

int axisIndex(const Vec3d& v) {
  Vec3d a = vabs(v);
  return a.x >= a.y && a.x >= a.z ? 0 : (a.y >= a.z ? 1 : 2);
}

SketchPlane axisPlane(int axis, const Vec3d& origin) {
  switch (axis) {
    case 0: return SketchPlane{origin, {1, 0, 0}, {0, 1, 0}};
    case 1: return SketchPlane{origin, {0, -1, 0}, {1, 0, 0}};
    default: return SketchPlane{origin, {0, 0, 1}, {1, 0, 0}};
  }
}

// MARK: Operation

std::string Operation::caseName() const {
  static const char* names[] = {
      "primitive", "extrude", "revolve", "pushPull", "boolean", "fillet", "chamfer", "shell", "move",
      "mirror", "duplicate", "delete", "imported", "sketch", "plane", "sweep", "loft", "scale",
      "linearPattern", "circularPattern", "split", "deleteFaces", "draft", "offsetFaces", "axis", "translate",
      "rotate", "align", "image", "replaceFace", "fill", "projection", "emboss", "scaleAxes", "scatter", "terrain"};
  static_assert(sizeof(names) / sizeof(names[0]) == std::variant_size<Variant>::value, "case names");
  return names[v.index()];
}

using P = Operation::Parameter;
using C = Operation::Choice;

std::vector<P> Operation::parameters() const {
  if (auto o = as<op::Primitive>()) {
    std::vector<P> params;
    auto labels = dimLabels(o->kind);
    for (size_t i = 0; i < labels.size(); ++i) params.push_back({int(i), labels[i], o->dims[int(i)]});
    const char* xyz[] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) params.push_back({10 + i, xyz[i], o->position[i]});
    return params;
  }
  if (auto o = as<op::Extrude>()) return {{0, "Distance", o->distance}};
  if (auto o = as<op::Revolve>()) return {{0, "Angle", o->angle, "°"}};
  if (auto o = as<op::PushPull>()) return {{0, "Distance", o->distance}};
  if (auto o = as<op::Fillet>()) return {{0, "Radius", o->radius}};
  if (auto o = as<op::Chamfer>()) return {{0, "Distance", o->distance}};
  if (auto o = as<op::Shell>()) return {{0, "Thickness", o->thickness}};
  if (auto o = as<op::Move>())
    return {{0, "X", o->translation.x}, {1, "Y", o->translation.y}, {2, "Z", o->translation.z},
            {3, "Rotate Z", o->angle, "°"}};
  if (auto o = as<op::Plane>()) return {{0, "Offset", o->offset}, {1, "Angle", o->angle, "°"}};
  if (auto o = as<op::Scale>()) return {{0, "Factor", o->factor, "×"}};
  if (auto o = as<op::LinearPattern>()) return {{0, "Count", double(o->count), ""}, {1, "Spacing", o->spacing}};
  if (auto o = as<op::CircularPattern>()) return {{0, "Count", double(o->count), ""}, {1, "Angle", o->angle, "°"}};
  if (auto o = as<op::Split>()) return {{0, "Offset", o->offset}};
  if (auto o = as<op::Draft>()) return {{0, "Angle", o->angle, "°"}};
  if (auto o = as<op::OffsetFaces>()) return {{0, "Distance", o->distance}};
  if (auto o = as<op::Translate>()) return {{0, "Distance", o->distance}};
  if (auto o = as<op::Rotate>()) return {{0, "Angle", o->angle, "°"}};
  if (auto o = as<op::Emboss>()) return {{0, "Height", o->height}};
  if (auto o = as<op::ScaleAxes>()) {
    if (o->uniform) return {{0, "Factor", o->factors.x, "×"}};
    return {{0, "X", o->factors.x, "×"}, {1, "Y", o->factors.y, "×"}, {2, "Z", o->factors.z, "×"}};
  }
  if (auto o = as<op::Scatter>()) {
    const auto& p = o->settings;
    return {{0, "Count", double(p.count), ""},       {1, "Seed", double(p.seed), ""},
            {2, "Min size", p.minScale * 100, "%"},  {3, "Max size", p.maxScale * 100, "%"},
            {4, "Spacing", p.spacing},               {5, "Max slope", p.maxSlope, "°"},
            {6, "Sink", p.sink * 100, "%"}};
  }
  if (auto o = as<op::Terrain>()) {
    const auto& t = o->settings;
    std::vector<P> params = {{0, "Width", t.width}, {1, "Depth", t.depth}, {2, "Height", t.heightScale * 100, "%"},
                             {3, "Base thickness", t.floor}};
    if (t.noise) {
      params.push_back({4, "Seed", double(t.noise->seed), ""});
      params.push_back({5, "Hill size", t.noise->scale * 100, "%"});
      params.push_back({6, "Roughness", t.noise->roughness * 100, "%"});
    }
    const char* xyz[] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) params.push_back({10 + i, xyz[i], t.center[i]});
    return params;
  }
  if (auto o = as<op::Image>())
    return {{0, "Width", o->image.width}, {1, "Opacity", o->image.opacity * 100, "%"}};
  return {};
}

std::vector<C> Operation::choices() const {
  const std::vector<std::string> axisNames = {"X", "Y", "Z"};
  if (auto o = as<op::Mirror>())
    return {{0, "Plane normal", axisNames, axisIndex(o->plane.normal)},
            {1, "Original", {"Keep", "Remove"}, o->keepOriginal ? 0 : 1}};
  if (auto o = as<op::LinearPattern>())
    return {{0, "Direction", axisNames, axisIndex(o->direction)}, {1, "Result", {"Separate", "Join"}, o->fuse ? 1 : 0}};
  if (auto o = as<op::CircularPattern>())
    return {{0, "Axis", axisNames, axisIndex(o->axis)}, {1, "Result", {"Separate", "Join"}, o->fuse ? 1 : 0}};
  if (auto o = as<op::Split>()) return {{0, "Plane normal", axisNames, axisIndex(o->plane.normal)}};
  if (auto o = as<op::Draft>()) return {{0, "Pull", axisNames, axisIndex(o->pull)}};
  if (auto o = as<op::Translate>()) return {{0, "Direction", axisNames, axisIndex(o->direction)}};
  if (auto o = as<op::Rotate>()) return {{0, "Axis", axisNames, axisIndex(o->axis)}};
  if (auto o = as<op::Emboss>()) return {{0, "Type", {"Emboss", "Deboss"}, o->deboss ? 1 : 0}};
  if (auto o = as<op::ScaleAxes>()) return {{0, "Mode", {"Uniform", "Per axis"}, o->uniform ? 0 : 1}};
  if (auto o = as<op::Scatter>()) {
    const auto& p = o->settings;
    return {{0, "Rotation", {"Random", "None"}, p.randomRotation ? 0 : 1},
            {1, "Orientation", {"Upright", "Follow surface"}, p.alignToSurface ? 1 : 0},
            {2, "Original", {"Keep", "Remove"}, p.keepOriginal ? 0 : 1}};
  }
  if (auto o = as<op::Align>()) {
    std::vector<C> out;
    for (int i = 0; i < 3; ++i)
      out.push_back({i, "Align " + axisNames[i], {"–", "Min", "Center", "Max"},
                     i < int(o->modes.size()) ? o->modes[i] : 0});
    return out;
  }
  return {};
}

Operation Operation::settingChoice(int id, int index) const {
  if (auto o = as<op::ScaleAxes>()) {
    auto r = *o;
    bool uniform = index == 0;
    if (uniform) r.factors = {o->factors.x, o->factors.x, o->factors.x};
    r.uniform = uniform;
    return r;
  }
  if (auto o = as<op::Scatter>()) {
    auto r = *o;
    switch (id) {
      case 0: r.settings.randomRotation = index == 0; break;
      case 1: r.settings.alignToSurface = index == 1; break;
      default: r.settings.keepOriginal = index == 0; break;
    }
    return r;
  }
  if (auto o = as<op::Emboss>()) {
    auto r = *o;
    r.deboss = index == 1;
    return r;
  }
  if (auto o = as<op::Mirror>()) {
    auto r = *o;
    if (id == 0) r.plane = axisPlane(index, o->plane.origin);
    else r.keepOriginal = index == 0;
    return r;
  }
  auto axis = [&](int i) { return worldAxes()[size_t(clamp(i, 0, 2))]; };
  if (auto o = as<op::LinearPattern>()) {
    auto r = *o;
    if (id == 0) r.direction = axis(index);
    if (id == 1) r.fuse = index == 1;
    return r;
  }
  if (auto o = as<op::CircularPattern>()) {
    auto r = *o;
    if (id == 0) r.axis = axis(index);
    if (id == 1) r.fuse = index == 1;
    return r;
  }
  if (auto o = as<op::Split>()) {
    auto r = *o;
    r.plane = axisPlane(index, o->plane.origin);
    return r;
  }
  if (auto o = as<op::Draft>()) {
    auto r = *o;
    r.pull = axis(index);
    return r;
  }
  if (auto o = as<op::Translate>()) {
    auto r = *o;
    r.direction = axis(index);
    return r;
  }
  if (auto o = as<op::Rotate>()) {
    auto r = *o;
    r.axis = axis(index);
    return r;
  }
  if (auto o = as<op::Align>()) {
    auto r = *o;
    if (id >= 0 && id < int(r.modes.size())) r.modes[size_t(id)] = index;
    return r;
  }
  return *this;
}

static int roundedInt(double v) { return int(std::round(v)); }

Operation Operation::setting(int id, double v) const {
  if (auto o = as<op::Terrain>()) {
    auto r = *o;
    auto& t = r.settings;
    switch (id) {
      case 0: t.width = std::max(v, 1.0); break;
      case 1: t.depth = std::max(v, 1.0); break;
      case 2: t.heightScale = std::max(v / 100, 0.0); break;
      case 3: t.floor = std::max(v, 0.5); break;
      case 4: if (t.noise) t.noise->seed = std::max(roundedInt(v), 0); break;
      case 5: if (t.noise) t.noise->scale = std::min(std::max(v / 100, 0.02), 4.0); break;
      case 6: if (t.noise) t.noise->roughness = std::min(std::max(v / 100, 0.0), 0.95); break;
      case 10: case 11: case 12: t.center[id - 10] = v; break;
      default: break;
    }
    if ((id >= 0 && id <= 1) || (id >= 4 && id <= 6)) t.regenerate();
    return r;
  }
  if (auto o = as<op::ScaleAxes>()) {
    auto r = *o;
    double x = std::max(v, 0.001);
    if (o->uniform) r.factors = {x, x, x};
    else r.factors[clamp(id, 0, 2)] = x;
    return r;
  }
  if (auto o = as<op::Scatter>()) {
    auto r = *o;
    auto& p = r.settings;
    switch (id) {
      case 0: p.count = std::min(std::max(roundedInt(v), 1), 2000); break;
      case 1: p.seed = std::max(roundedInt(v), 0); break;
      case 2: p.minScale = std::max(v / 100, 0.01); p.maxScale = std::max(p.maxScale, p.minScale); break;
      case 3: p.maxScale = std::max(v / 100, 0.01); p.minScale = std::min(p.minScale, p.maxScale); break;
      case 4: p.spacing = std::max(v, 0.0); break;
      case 5: p.maxSlope = std::min(std::max(v, 0.0), 90.0); break;
      default: p.sink = std::min(std::max(v / 100, -1.0), 1.0); break;
    }
    return r;
  }
  if (auto o = as<op::Emboss>()) { auto r = *o; r.height = v; return r; }
  if (auto o = as<op::Primitive>()) {
    auto r = *o;
    if (id < 10) { if (id >= 0 && id < 3) r.dims[id] = v; }
    else if (id - 10 < 3) r.position[id - 10] = v;
    return r;
  }
  if (auto o = as<op::Extrude>()) { auto r = *o; r.distance = v; return r; }
  if (auto o = as<op::Revolve>()) { auto r = *o; r.angle = v; return r; }
  if (auto o = as<op::PushPull>()) { auto r = *o; r.distance = v; return r; }
  if (auto o = as<op::Fillet>()) { auto r = *o; r.radius = v; return r; }
  if (auto o = as<op::Chamfer>()) { auto r = *o; r.distance = v; return r; }
  if (auto o = as<op::Shell>()) { auto r = *o; r.thickness = v; return r; }
  if (auto o = as<op::Move>()) {
    auto r = *o;
    if (id < 3) { if (id >= 0) r.translation[id] = v; }
    else r.angle = v;
    return r;
  }
  if (auto o = as<op::Plane>()) {
    auto r = *o;
    if (id == 0) r.offset = v;
    if (id == 1) r.angle = v;
    return r;
  }
  if (auto o = as<op::Scale>()) { auto r = *o; r.factor = std::max(v, 0.001); return r; }
  if (auto o = as<op::LinearPattern>()) {
    auto r = *o;
    if (id == 0) r.count = std::max(roundedInt(v), 2);
    if (id == 1) r.spacing = v;
    return r;
  }
  if (auto o = as<op::CircularPattern>()) {
    auto r = *o;
    if (id == 0) r.count = std::max(roundedInt(v), 2);
    if (id == 1) r.angle = v;
    return r;
  }
  if (auto o = as<op::Split>()) { auto r = *o; r.offset = v; return r; }
  if (auto o = as<op::Draft>()) { auto r = *o; r.angle = v; return r; }
  if (auto o = as<op::OffsetFaces>()) { auto r = *o; r.distance = v; return r; }
  if (auto o = as<op::Translate>()) { auto r = *o; r.distance = v; return r; }
  if (auto o = as<op::Rotate>()) { auto r = *o; r.angle = v; return r; }
  if (auto o = as<op::Image>()) {
    auto r = *o;
    if (id == 0 && v > 0) {
      r.image.height *= v / r.image.width;
      r.image.width = v;
    } else if (id == 1) {
      r.image.opacity = std::min(std::max(v / 100, 0.05), 1.0);
    }
    return r;
  }
  return *this;
}

std::optional<CombineMode> Operation::combineMode() const {
  if (auto o = as<op::Extrude>()) return o->mode;
  if (auto o = as<op::Revolve>()) return o->mode;
  if (auto o = as<op::Sweep>()) return o->mode;
  if (auto o = as<op::Loft>()) return o->mode;
  return std::nullopt;
}

Operation Operation::setting(CombineMode mode, std::optional<int> target) const {
  CombineMode m = target ? mode : CombineMode::newBody;
  if (auto o = as<op::Extrude>()) { auto r = *o; r.mode = m; r.target = target; return r; }
  if (auto o = as<op::Revolve>()) { auto r = *o; r.mode = m; r.target = target; return r; }
  if (auto o = as<op::Sweep>()) { auto r = *o; r.mode = m; r.target = target; return r; }
  if (auto o = as<op::Loft>()) { auto r = *o; r.mode = m; r.target = target; return r; }
  return *this;
}

const Sketch* Operation::sketch() const {
  if (auto o = as<op::Extrude>()) return &o->sketch;
  if (auto o = as<op::Revolve>()) return &o->sketch;
  if (auto o = as<op::SketchItem>()) return &o->sketch;
  return nullptr;
}

Operation Operation::setting(const Sketch& sketch) const {
  if (auto o = as<op::Extrude>()) { auto r = *o; r.sketch = sketch; return r; }
  if (auto o = as<op::Revolve>()) { auto r = *o; r.sketch = sketch; return r; }
  if (auto o = as<op::SketchItem>()) { auto r = *o; r.sketch = sketch; return r; }
  return *this;
}

std::optional<int> Operation::sketchTarget() const {
  if (auto o = as<op::Extrude>()) return o->target;
  if (auto o = as<op::Revolve>()) return o->target;
  if (auto o = as<op::Sweep>()) return o->target;
  if (auto o = as<op::Loft>()) return o->target;
  return std::nullopt;
}

Operation Operation::settingAroundV(bool aroundV) const {
  if (auto o = as<op::Revolve>()) { auto r = *o; r.aroundV = aroundV; return r; }
  return *this;
}

// MARK: Feature

Operation Feature::resolved(const ExpressionEvaluator& evaluator) const {
  if (!bindings) return op;
  Operation out = op;
  for (auto& [id, expression] : *bindings)  // std::map: sorted by parameter id
    if (auto v = evaluator.evaluate(expression)) out = out.setting(id, *v);
  return out;
}

std::string Feature::title() const {
  auto n = [](size_t c) { return " (" + std::to_string(c) + ")"; };
  if (auto o = op.as<op::Primitive>()) return sculpt::title(o->kind);
  if (auto o = op.as<op::Extrude>()) return o->mode == CombineMode::cut ? "Extrude cut" : "Extrude";
  if (op.is<op::Revolve>()) return "Revolve";
  if (op.is<op::PushPull>()) return "Push/Pull";
  if (auto o = op.as<op::Boolean>()) return sculpt::title(o->kind);
  if (auto o = op.as<op::Fillet>()) return "Fillet" + n(o->edges.size());
  if (auto o = op.as<op::Chamfer>()) return "Chamfer" + n(o->edges.size());
  if (op.is<op::Shell>()) return "Shell";
  if (op.is<op::Move>()) return "Move";
  if (op.is<op::Mirror>()) return "Mirror";
  if (op.is<op::Duplicate>()) return "Duplicate";
  if (op.is<op::Delete>()) return "Delete body";
  if (auto o = op.as<op::Imported>()) return "Import " + o->name;
  if (auto o = op.as<op::SketchItem>()) return o->name;
  if (auto o = op.as<op::Plane>()) return o->name;
  if (op.is<op::Sweep>()) return "Sweep";
  if (auto o = op.as<op::Loft>()) return "Loft" + n(o->sections.size());
  if (op.is<op::Scale>()) return "Scale";
  if (auto o = op.as<op::LinearPattern>()) return "Linear pattern" + n(size_t(o->count));
  if (auto o = op.as<op::CircularPattern>()) return "Circular pattern" + n(size_t(o->count));
  if (op.is<op::Split>()) return "Split body";
  if (auto o = op.as<op::DeleteFaces>()) return "Delete faces" + n(o->faces.size());
  if (op.is<op::Draft>()) return "Draft";
  if (op.is<op::OffsetFaces>()) return "Offset faces";
  if (auto o = op.as<op::Axis>()) return o->name;
  if (op.is<op::Translate>()) return "Translate";
  if (op.is<op::Rotate>()) return "Rotate around axis";
  if (op.is<op::Align>()) return "Align";
  if (auto o = op.as<op::Image>()) return o->image.name;
  if (op.is<op::ReplaceFace>()) return "Replace face";
  if (op.is<op::Fill>()) return "Fill";
  if (auto o = op.as<op::Projection>()) return o->name;
  if (auto o = op.as<op::Emboss>()) return o->deboss ? "Deboss" : "Emboss";
  if (auto o = op.as<op::ScaleAxes>()) return o->uniform ? "Scale" : "Scale (per axis)";
  if (auto o = op.as<op::Scatter>()) return "Scatter" + n(size_t(o->settings.count));
  if (auto o = op.as<op::Terrain>()) return o->settings.name;
  return "";
}

bool Feature::isReference() const {
  return op.is<op::SketchItem>() || op.is<op::Plane>() || op.is<op::Axis>() || op.is<op::Image>() ||
         op.is<op::Projection>();
}

std::string Feature::symbol() const {
  if (auto o = op.as<op::Primitive>()) return sculpt::symbol(o->kind);
  if (auto o = op.as<op::Boolean>()) return sculpt::symbol(o->kind);
  static const char* symbols[] = {
      "",  // primitive
      "square.stack.3d.up", "arrow.triangle.2.circlepath", "arrow.up.and.down.square",
      "",  // boolean
      "circle.bottomhalf.filled", "triangle.bottomhalf.filled", "square.dashed", "move.3d",
      "arrow.left.and.right.righttriangle.left.righttriangle.right", "plus.square.on.square", "trash",
      "square.and.arrow.down", "pencil.and.outline", "square.on.square.dashed",
      "point.bottomleft.forward.to.point.topright.scurvepath", "cone", "arrow.up.left.and.arrow.down.right",
      "square.grid.3x1.below.line.grid.1x2", "circle.grid.cross", "square.split.diagonal", "eraser",
      "trapezoid.and.line.vertical", "arrow.up.square", "line.diagonal", "arrow.right.square",
      "arrow.clockwise.circle", "align.horizontal.center", "photo", "square.2.layers.3d.top.filled",
      "square.fill.on.square", "rectangle.portrait.on.rectangle.portrait.angled", "textformat",
      "arrow.up.left.and.arrow.down.right", "circle.dotted", "mountain.2"};
  static_assert(sizeof(symbols) / sizeof(symbols[0]) == std::variant_size<Operation::Variant>::value, "symbols");
  return symbols[op.v.index()];
}

}  // namespace sculpt
