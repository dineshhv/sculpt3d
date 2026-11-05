// Tests of the shared document model (AppCore/src/model): Swift-compatible
// JSON, sketches and profiles, replay, drafts, undo, expressions, exports.
// Plain checks, no framework: prints each result, exits non-zero on failure.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "sculpt/model/Codec.hpp"
#include "sculpt/model/DisplayFormat.hpp"
#include "sculpt/model/Expression.hpp"
#include "sculpt/model/ModelStore.hpp"
#include "sculpt/model/Sculpt3DFile.hpp"
#include "sculpt/model/SketchInput.hpp"
#include "sculpt/model/ToolCatalog.hpp"

using namespace sculpt;

static int failures = 0, passes = 0;
static void check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (ok) ++passes;
  else ++failures;
}
static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
static std::string num(double v) {
  char b[64];
  std::snprintf(b, sizeof b, "%.4f", v);
  return b;
}

static std::string fixture(const std::string& name) {
  std::ifstream in(std::string(SCULPT_FIXTURES) + "/" + name, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static double volume(ModelStore& store) {
  double v = 0;
  for (int b : store.bodyIds())
    if (auto p = store.properties(b)) v += p->volume;
  return v;
}

// MARK: JSON

static void jsonTests() {
  std::printf("\n# JSON (Swift Codable compatibility)\n");
  check(swiftNumber(40.0) == "40" && swiftNumber(0.1) == "0.1" && swiftNumber(-3.25) == "-3.25", "plain doubles");
  check(swiftNumber(1e16) == "1e+16" && swiftNumber(1e15) == "1000000000000000" &&
            swiftNumber(9007199254740992.0) == "9007199254740992" && swiftNumber(9007199254740994.0) == "9.007199254740994e+15",
        "large doubles switch to exponent above 2^53");
  check(swiftNumber(0.0001) == "0.0001" && swiftNumber(1e-5) == "1e-05" && swiftNumber(5e-324) == "5e-324" &&
            swiftNumber(1.0 / 3) == "0.3333333333333333" && swiftNumber(1.2345678901234568e+17) == "1.2345678901234568e+17",
        "small and long doubles");
  check(swiftNumber(0.72f) == "0.72" && swiftNumber(16777216.0f) == "16777216" && swiftNumber(16777218.0f) == "1.6777218e+07" &&
            swiftNumber(0.0001f) == "0.0001" && swiftNumber(-0.0f) == "-0",
        "floats print their own shortest digits");
  check(swiftString("a/b \"q\" \\ \n\t \x01") == "\"a\\/b \\\"q\\\" \\\\ \\n\\t \\u0001\"", "string escaping like Swift");
  std::vector<uint8_t> raw = {1, 2, 3, 250}, back;
  check(base64Encode(raw) == "AQID+g==" && base64Decode("AQID+g==", back) && back == raw, "Data as base64");

  // The Apple app's own output, decoded and re-encoded byte for byte.
  std::string golden = fixture("golden.sculpt3d");
  check(!golden.empty(), "golden.sculpt3d fixture present (written by Swift's JSONEncoder)");
  Sculpt3DFile file;
  bool decoded = true;
  try {
    file = Sculpt3DFile::decode(golden);
  } catch (const std::exception& e) {
    decoded = false;
    std::printf("     %s\n", e.what());
  }
  check(decoded && file.features.size() == 39, "golden decodes: " + std::to_string(file.features.size()) + " features");
  std::set<std::string> cases;
  for (auto& f : file.features) cases.insert(f.op.caseName());
  check(cases.size() == 36, "golden covers all 36 Operation cases (" + std::to_string(cases.size()) + ")");
  std::string again = file.encoded();
  check(again == golden, "re-encoding the golden file is byte-identical to Swift's output");
  if (again != golden) {
    size_t i = 0;
    while (i < again.size() && i < golden.size() && again[i] == golden[i]) ++i;
    std::printf("     first difference at byte %zu:\n     swift: %s\n     c++:   %s\n", i,
                golden.substr(i > 40 ? i - 40 : 0, 100).c_str(), again.substr(i > 40 ? i - 40 : 0, 100).c_str());
  }
  Sculpt3DFile round = Sculpt3DFile::decode(again);
  check(round.features == file.features && round.settings == file.settings, "decode → encode → decode is equal");
  check(file.features[1].suppressed && file.features[1].bindings && file.features[1].bindings->at(11) == "h",
        "suppressed flag and [Int: String] bindings");
  check(file.settings.units == LengthUnit::inch && file.settings.bodyNames.at(10) == "Ten" &&
            file.settings.appearance.at(1).preset == std::string("metal.polished.iron") &&
            file.settings.appearance.at(1).color.x == 0.85f && file.settings.environment.lightAzimuth == 12.5f &&
            file.settings.camera.orthographic && !file.settings.snap.guideLines && file.settings.variables.size() == 2,
        "project settings decode");
  auto img = file.features[30].op.as<op::Image>();
  check(img && img->image.png.size() == 8 && img->image.png[7] == 250 && img->image.opacity == 0.8,
        "reference image PNG bytes and default opacity");
  auto terr = file.features[38].op.as<op::Terrain>();
  check(terr && terr->settings.noise && terr->settings.noise->seed == 7 && terr->settings.heights[3] == 3,
        "terrain settings with noise");
  std::string empty = fixture("empty.sculpt3d");
  check(!empty.empty() && Sculpt3DFile::decode(empty).encoded() == empty, "empty document round-trips byte for byte");

  // Custom init(from:) fallbacks.
  auto legacy = nlohmann::json::parse(
      R"({"plane":{"origin":[0,0,0],"normal":[0,0,1],"xDir":[1,0,0]},"shapes":[{"rectangle":{"_0":[0,0],"_1":[10,5]}},{"circle":{"center":[20,0],"radius":3}},{"polygon":{"_0":[[0,20],[5,20],[0,25]]}}]})");
  Sketch old;
  decode(legacy, old);
  check(old.lines.size() == 7 && old.circles.size() == 1 && old.profileCount() == 3,
        "legacy `shapes` load as constrained geometry");
  Appearance metal;
  decode(nlohmann::json::parse(R"({"material":"metal"})"), metal);
  check(metal.metallic == 1 && metal.roughness == 0.25f && metal.color == Appearance().color, "legacy Appearance material");
  EnvironmentSettings env;
  decode(nlohmann::json::parse(R"({"showGrid":false})"), env);
  check(!env.showGrid && env.castShadows && env.lightElevation == 50, "EnvironmentSettings partial decode");
  ProjectSettings ps;
  decode(nlohmann::json::parse(R"({"units":"cm"})"), ps);
  check(ps.units == LengthUnit::cm && ps.snap.grid && ps.variables.empty(), "ProjectSettings missing keys default");
  SketchConstraint dummy;
  bool threw = false;
  try {
    Operation o;
    decode(nlohmann::json::parse(R"({"fillet":{"body":1,"edges":[1]}})"), o);
  } catch (const CodingError&) {
    threw = true;
  }
  check(threw, "missing required key is rejected like Swift");
  // Any formatting decodes.
  auto compact = nlohmann::json::parse(golden).dump();
  check(Sculpt3DFile::decode(compact).encoded() == golden, "compact JSON decodes to the same document");
}

// MARK: Sketches

static void sketchTests() {
  std::printf("\n# Sketches and profiles\n");
  Sketch r(SketchPlane::xy());
  r.addRectangle({3, 4}, {41, 27}, std::nullopt, std::nullopt);
  auto status = r.solve();
  check(r.lines.size() == 4 && r.constraints.size() == 4, "rectangle has 4 lines and H/V constraints");
  check(status && status->degreesOfFreedom == 4, "rectangle has 4 dof");
  auto profiles = r.profiles();
  check(profiles.size() == 1 && profiles[0].kind == o3d::Profile::Kind::Polygon && profiles[0].points.size() == 4,
        "rectangle is one polygon profile");

  Sketch c;
  c.addCircle(c.addPoint({25, 15}), 6);
  auto cp = c.profiles();
  check(cp.size() == 1 && cp[0].kind == o3d::Profile::Kind::Circle && cp[0].radius == 6 && cp[0].cu == 25,
        "circle is a circle profile");

  // A 20 x 10 slot: two lines and two semicircle arcs.
  Sketch slot;
  int a = slot.addPoint({0, 0}), b = slot.addPoint({20, 0}), cc = slot.addPoint({20, 10}), d = slot.addPoint({0, 10});
  slot.addLine(a, b);
  slot.addLine(cc, d);
  slot.addArc(slot.addPoint({20, 5}), b, cc);
  slot.addArc(slot.addPoint({0, 5}), d, a);
  auto sp = slot.profiles();
  int arcs = 0;
  if (!sp.empty())
    for (auto& s : sp[0].segments) arcs += s.kind == o3d::Segment::Kind::Arc;
  check(sp.size() == 1 && sp[0].kind == o3d::Profile::Kind::Wire && sp[0].segments.size() == 4 && arcs == 2,
        "slot of lines and arcs is one wire profile");
  check(slot.solve().has_value(), "slot solves");

  Sketch splines;
  std::vector<int> ids;
  for (Vec2d p : {Vec2d(0, 0), Vec2d(20, -5), Vec2d(30, 10), Vec2d(10, 20)}) ids.push_back(splines.addPoint(p));
  SketchSpline closed;
  closed.points = ids;
  closed.closed = true;
  splines.splines.push_back(closed);
  SketchEllipse e;
  e.center = splines.addPoint({60, 0});
  e.majorRadius = 10;
  e.minorRadius = 5;
  splines.ellipses.push_back(e);
  check(splines.profileCount() == 2, "closed spline and ellipse are profiles");

  // Open path for sweeps.
  Sketch path;
  path.addLine(0, path.addPoint({0, 50}));
  check(path.path().size() == 1 && path.profileCount() == 0, "an open line is a sweep path, not a profile");

  // SketchOps (from App/Tests/SketchFlow).
  Sketch t;
  int h0 = t.addPoint({-10, 0}), h1 = t.addPoint({10, 0}), v0 = t.addPoint({0, -10}), v1 = t.addPoint({0, 10});
  t.addLine(h0, h1);
  t.addLine(v0, v1);
  check(t.trim(SketchRef::line(0), {6, 0}), "trim cuts the line");
  double right = -99;
  for (auto& l : t.lines) right = std::max(right, std::max(t.points[l.a].x, t.points[l.b].x));
  check(t.lines.size() == 2 && near(right, 0, 1e-9), "trimmed line ends at the crossing");
  Sketch ct;
  ct.addCircle(ct.addPoint({0, 0}), 10);
  ct.addLine(ct.addPoint({-20, 0}), ct.addPoint({20, 0}));
  check(ct.trim(SketchRef::circle(0), {0, -10}) && ct.circles[0].isArc() && near(ct.arcAngles(0)->second, kPi, 1e-6),
        "trimming a circle leaves the upper arc");
  Sketch o;
  o.addRectangle({0, 0}, {20, 10}, std::nullopt, std::nullopt);
  auto made = o.offset({SketchRef::line(0), SketchRef::line(1), SketchRef::line(2), SketchRef::line(3)}, -2);
  double lo = 1e9, hi = -1e9;
  for (auto& r : made) {
    auto& l = o.lines[size_t(r.index)];
    for (int p : {l.a, l.b}) { lo = std::min(lo, o.points[p].x); hi = std::max(hi, o.points[p].x); }
  }
  check(made.size() == 4 && near(hi - lo, 24, 1e-6) && o.profileCount() == 2, "offset rectangle is 24 wide; two profiles");
  Sketch f;
  f.addRectangle({0, 0}, {20, 10}, std::nullopt, std::nullopt);
  check(!f.fillet(f.lines[0].b, 3) && f.circles.size() == 1 && f.solve() && f.profileCount() == 1 &&
            near(f.circles[0].radius, 3, 1e-6),
        "corner fillet adds a radius-3 arc and keeps one profile");
  Sketch dc;
  dc.addRectangle({0, 0}, {10, 10}, std::nullopt, std::nullopt);
  dc.disconnect(dc.lines[0].b);
  check(dc.profileCount() == 0, "disconnecting a corner opens the loop");
  Sketch del = r;
  size_t nc = del.constraints.size();
  del.remove({SketchRef::line(0)});
  check(del.lines.size() == 3 && del.constraints.size() < nc && del.profileCount() == 0, "deleting a line drops its constraints");
  Sketch m;
  m.addLine(m.addPoint({1, 1}), m.addPoint({5, 1}));
  SketchTransform mt;
  mt.mirrorAxis = Vec2d(0, 1);
  auto copies = m.addCopy({SketchRef::line(0)}, mt);
  check(copies.size() == 1 && near(m.points[m.lines[1].b].x, -5, 1e-9) && m.constraints.size() == 2,
        "mirror copy keeps the horizontal constraint");
}

// MARK: Sketch session

static void sessionTests() {
  std::printf("\n# Sketch sessions\n");
  ModelStore store;
  store.startSketch(SketchPlane::xy());
  SketchSession s = *store.sketch();
  s.sketch.addRectangle({3, 4}, {41, 27}, std::nullopt, std::nullopt);
  s.resolve();
  s.selection = {SketchRef::point(1), SketchRef::point(0)};
  auto avail = s.availableConstraints();
  check(std::find(avail.begin(), avail.end(), SketchConstraint::Kind::Coincident) != avail.end(), "two points offer coincident");
  check(!s.constrain(SketchConstraint::Kind::Coincident) && length(s.sketch.points[1]) < 1e-6, "coincident moves corner to origin");
  s.selection = {SketchRef::line(0)};
  check(!s.constrain(SketchConstraint::Kind::Length) && !s.setDimension(*s.selectedDimension(), 50), "width 50");
  s.selection = {SketchRef::line(1)};
  check(!s.constrain(SketchConstraint::Kind::Length) && !s.setDimension(*s.selectedDimension(), 30), "height 30");
  check(s.status.fullyConstrained(), "rectangle fully constrained");
  s.selection = {SketchRef::line(2)};
  check(s.constrain(SketchConstraint::Kind::Length).has_value(), "redundant length is refused");
  s.selection = {SketchRef::line(0)};
  check(s.constrain(SketchConstraint::Kind::Vertical).has_value(), "conflicting vertical is refused");
  int c = s.sketch.addPoint({25, 15});
  s.sketch.addCircle(c, 6);
  s.resolve();
  s.selection = {SketchRef::circle(0)};
  check(!s.constrain(SketchConstraint::Kind::Radius) && !s.setDimension(*s.selectedDimension(), 8), "radius 8");
  check(s.label(s.sketch.constraints.back()) == "R8", "radius label");
  store.setSketch(s);
  store.finishSketch(false);
  check(store.draft().has_value() && !store.sketch(), "finishing opens an extrude draft");
  store.updateDraft(store.draft()->feature.op.setting(0, 10));
  store.applyDraft();
  double expected = 50.0 * 30 * 10 - kPi * 64 * 10;
  check(near(volume(store), expected, 1), "plate volume " + num(volume(store)) + " ≈ " + num(expected));

  // Edit the sketch from history: 60 wide.
  Uuid fid = store.features()[0].id;
  store.editSketch(fid);
  check(store.meshes().empty(), "editing the first feature's sketch hides its result");
  SketchSession ed = *store.sketch();
  int li = -1;
  for (size_t i = 0; i < ed.sketch.constraints.size(); ++i)
    if (ed.sketch.constraints[i].kind == SketchConstraint::Kind::Length && ed.sketch.constraints[i].value == 50) li = int(i);
  check(li >= 0 && !ed.setDimension(li, 60), "width edited to 60");
  store.setSketch(ed);
  store.finishSketch(false);
  store.applyDraft();
  check(store.features().size() == 1 && near(volume(store), 60.0 * 30 * 10 - kPi * 64 * 10, 1),
        "history updated in place, volume " + num(volume(store)));

  // Undo inside a sketch: each edit is one step; a drag is one step.
  ModelStore u;
  u.startSketch(SketchPlane::xy());
  u.updateSketch([](SketchSession& e) { e.sketch.addRectangle({0, 0}, {10, 10}, std::nullopt, std::nullopt); });
  u.updateSketch([](SketchSession& e) { e.sketch.addCircle(e.sketch.addPoint({30, 0}), 4); });
  u.beginSketchDrag();
  for (double x : {31.0, 32.0, 33.0})
    u.updateSketch([&](SketchSession& e) { e.sketch.points[size_t(e.sketch.circles[0].center)].x = x; });
  u.endSketchDrag();
  u.undoSketch();
  check(u.sketch()->sketch.points[size_t(u.sketch()->sketch.circles[0].center)].x == 30, "sketch undo reverts a whole drag");
  u.undoSketch();
  check(u.sketch()->sketch.circles.empty() && u.sketch()->sketch.lines.size() == 4, "sketch undo removes the circle");
  u.redoSketch();
  check(u.sketch()->sketch.circles.size() == 1, "sketch redo restores it");

  // Pointer input in plane coordinates: a rectangle drag and a line chain.
  ModelStore p;
  p.startSketch(SketchPlane::xy());
  SketchInput input(p);
  input.pickRadius = 0.5;
  input.gridStep = 1;
  p.setSketchTool(SketchTool::rectangle);
  input.drawBegan(Vec2d(0.1, -0.1));
  input.drawChanged(Vec2d(5, 2));
  input.drawEnded(Vec2d(10.2, 4.9));
  check(p.sketch()->sketch.lines.size() == 4 && p.sketch()->sketch.profileCount() == 1 && p.sketchUndo().size() >= 1,
        "rectangle drawn by dragging, snapped to the grid");
  p.setSketchTool(SketchTool::line);
  input.tap({20, 0}, false);
  input.tap({30, 0}, false);
  input.tap({30, 10}, false);
  check(p.sketch()->sketch.lines.size() == 6 && p.sketch()->chainEnd.has_value(), "line tool chains taps");
  p.sketchEscape();
  check(!p.sketch()->chainEnd && p.sketch()->escapeTitle() == "Cancel", "escape ends the chain");
  auto hit = input.hit({5, 0.1});
  check(hit && hit->kind == SketchRef::Kind::Line, "hit test finds a line");
  auto annotations = input.annotations(1);
  check(!annotations.empty(), "constraint glyphs are placed (" + std::to_string(annotations.size()) + ")");
  check(ToolCatalog::handleKey(p, "c").has_value() && p.sketch()->tool == SketchTool::circle, "the C key picks the circle tool");
}

// MARK: Replay

static void replayTests() {
  std::printf("\n# Replay, drafts, undo\n");
  ModelStore store;
  store.addPrimitive(PrimitiveKind::box);
  check(store.draft().has_value() && store.bodyIds().size() == 1, "box draft previews a body");
  store.applyDraft();
  check(store.features().size() == 1 && near(volume(store), 64000, 1), "box 40³ = " + num(volume(store)));

  // Extrude a cylinder through the box and subtract it.
  Sketch hole(SketchPlane::xy());
  hole.addCircle(hole.addPoint({0, 0}), 5);
  store.commitImmediately(op::Extrude{hole, 60, CombineMode::newBody, std::nullopt});
  check(store.bodyIds().size() == 2, "extruded cylinder is a second body");
  store.setSelection({Pick::bodyPick(store.bodyIds()[0]), Pick::bodyPick(store.bodyIds()[1])});
  store.boolean(BooleanKind::subtract);
  // The box is centred on the origin? Volume tells how much of the cylinder was inside.
  double v = volume(store);
  check(store.bodyIds().size() == 1 && v < 64000 - 1 && v > 64000 - kPi * 25 * 60 - 1, "subtract removes the hole: " + num(v));
  check(store.errors().empty(), "no feature errors");

  // Fillet edges of the result.
  int body = store.bodyIds()[0];
  store.setSelection({Pick::edge(body, 0), Pick::edge(body, 1)});
  store.filletSelection(false);
  check(store.draft() && store.draftError() == std::nullopt, "fillet draft previews");
  double before = v;
  store.applyDraft();
  double after = volume(store);
  check(store.features().size() == 4 && after < before && after > before - 200, "fillet trims a little: " + num(after));

  // Undo / redo of the history.
  store.undo();
  check(store.features().size() == 3 && near(volume(store), before, 1e-6), "undo removes the fillet");
  store.redo();
  check(store.features().size() == 4 && near(volume(store), after, 1e-6), "redo restores it");

  // A failing operation never enters the history.
  store.commitImmediately(op::Fillet{body, {0}, 500});
  check(store.features().size() == 4 && store.toast().has_value(), "impossible fillet is refused: " + store.toast().value_or(""));

  // Draft apply / cancel.
  store.addPrimitive(PrimitiveKind::sphere);
  check(store.bodyIds().size() == 2, "sphere draft shows");
  store.cancelDraft();
  check(!store.draft() && store.bodyIds().size() == 1 && store.features().size() == 4, "cancel discards the draft");
  Uuid boxId = store.features()[0].id;
  store.edit(boxId);
  store.updateDraft(store.draft()->feature.op.setting(2, 50));
  store.applyDraft();
  check(store.features().size() == 4 && store.features()[0].op.as<op::Primitive>()->dims.z == 50,
        "editing a feature replaces it in place");
  check(store.errors().empty(), "later features still replay after the edit");

  // Suppress and delete.
  store.toggleSuppressed(store.features()[3].id);
  check(near(volume(store), (40.0 * 40 * 50) - kPi * 25 * 50, 5), "suppressing the fillet: " + num(volume(store)));
  store.deleteFeature(store.features()[3].id);
  check(store.features().size() == 3, "delete feature");

  // Revolve, pattern, sweep, loft.
  ModelStore sweep;
  sweep.startSketch(SketchPlane::xz());
  sweep.updateSketch([](SketchSession& s) { s.sketch.addCircle(0, 3); });
  sweep.finishSketchAsItem();
  sweep.startSketch(SketchPlane::xy());
  sweep.updateSketch([](SketchSession& s) { s.sketch.addLine(0, s.sketch.addPoint({0, 50})); });
  sweep.finishSketchAsItem();
  std::vector<Uuid> items;
  for (auto& i : sweep.referenceItems()) items.push_back(i.id);
  sweep.setItemSelection(items);
  sweep.sweepSelection();
  sweep.applyDraft();
  check(sweep.errors().empty() && near(volume(sweep), kPi * 9 * 50, 5), "sweep volume " + num(volume(sweep)));

  ModelStore loft;
  loft.startSketch(SketchPlane::xy());
  loft.updateSketch([](SketchSession& s) { s.sketch.addRectangle({-10, -10}, {10, 10}, std::nullopt, std::nullopt); });
  loft.finishSketchAsItem();
  loft.addOffsetPlane();
  loft.updateDraft(loft.draft()->feature.op.setting(0, 30));
  loft.applyDraft();
  SketchPlane plane;
  for (auto& i : loft.referenceItems())
    if (i.isPlane()) plane = i.plane();
  loft.startSketch(plane);
  loft.updateSketch([](SketchSession& s) { s.sketch.addRectangle({-5, -5}, {5, 5}, std::nullopt, std::nullopt); });
  loft.finishSketchAsItem();
  std::vector<Uuid> sections;
  for (auto& i : loft.referenceItems())
    if (!i.isPlane()) sections.push_back(i.id);
  loft.setItemSelection(sections);
  loft.loftSelection();
  loft.applyDraft();
  check(loft.errors().empty() && near(volume(loft), 7000, 5), "loft volume " + num(volume(loft)));

  ModelStore pattern({Feature(op::Primitive{PrimitiveKind::box, {10, 10, 10}, {}})});
  pattern.setSelection({Pick::bodyPick(pattern.bodyIds()[0])});
  pattern.linearPatternSelection();
  pattern.applyDraft();
  check(pattern.bodyIds().size() == 3 && near(volume(pattern), 3000, 1), "linear pattern makes 3 bodies");
  pattern.setSelection({Pick::bodyPick(pattern.bodyIds()[0]), Pick::bodyPick(pattern.bodyIds()[1])});
  auto m = pattern.measureSelection();
  check(m && m->hasDistance && m->distance > 0, "measure distance between copies: " + num(m ? m->distance : -1));
  check(!pattern.measureRows().empty(), "measure panel rows");
  pattern.setSelection({Pick::bodyPick(pattern.bodyIds()[0])});
  pattern.splitSelection();
  pattern.applyDraft();
  check(pattern.bodyIds().size() == 4, "split adds a body");

  // Revolve a rectangle into a ring.
  Sketch ring(SketchPlane::xz());
  ring.addRectangle({10, 0}, {12, 5}, std::nullopt, std::nullopt);
  ModelStore rev({Feature(op::Revolve{ring, 360, true, CombineMode::newBody, std::nullopt})});
  double ringVolume = kPi * (144 - 100) * 5;
  check(rev.errors().empty() && near(volume(rev), ringVolume, 2), "revolved ring " + num(volume(rev)) + " ≈ " + num(ringVolume));

  // Checkpointed replay: changing the last feature restarts from its checkpoint.
  ModelStore cp({Feature(op::Primitive{PrimitiveKind::box, {10, 10, 10}, {}}),
                 Feature(op::Primitive{PrimitiveKind::sphere, {5, 0, 0}, {30, 0, 0}})});
  uint64_t key = cp.kernel().shapeKey(cp.bodyIds()[0]);
  auto firstMesh = cp.meshes().begin()->second;
  auto list = cp.features();
  list[1].op = list[1].op.setting(0, 6);
  cp.commit(list);
  check(cp.kernel().shapeKey(cp.bodyIds()[0]) == key && cp.meshes().begin()->second == firstMesh,
        "unchanged prefix is restored from a checkpoint (same shape, same mesh)");

  // Visibility and isolation.
  cp.setSelection({Pick::bodyPick(cp.bodyIds()[0])});
  cp.isolateSelection();
  check(cp.isIsolating() && cp.settings().hiddenBodies.size() == 1, "isolate hides the other body");
  cp.exitIsolation();
  check(!cp.isIsolating() && cp.settings().hiddenBodies.empty(), "exit isolation restores it");

  // A document decoded from the Apple app replays without crashing.
  Sculpt3DFile golden = Sculpt3DFile::decode(fixture("golden.sculpt3d"));
  ModelStore g(golden.features, golden.settings);
  check(g.features().size() == 39 && g.referenceItems().size() >= 4,
        "golden document replays (" + std::to_string(g.bodyIds().size()) + " bodies, " + std::to_string(g.errors().size()) +
            " feature errors)");
}

// MARK: Expressions

static void expressionTests() {
  std::printf("\n# Expressions and variables\n");
  Variable w, h;
  w.name = "w";
  w.expression = "20";
  h.name = "h";
  h.expression = "w / 2 + 5";
  ExpressionEvaluator ev(std::vector<Variable>{w, h});
  check(ev.evaluate("w * 2 + h") == 55.0, "variables resolve in order");
  check(ev.evaluate("max(w, 3) - sqrt(16)") == 16.0 && !ev.evaluate("2 +"), "functions; bad input rejected");
  check(near(*ev.evaluate("sin(30) + 2^3^2 + -W"), 0.5 + 512 - 20, 1e-12), "trig in degrees, right-assoc power, case-insensitive");
  check(!ev.evaluate("1/0") && !ev.evaluate("nope") && ExpressionEvaluator::isNumber(" 2,5 ") && !ExpressionEvaluator::isNumber("w"),
        "non-finite and unknown names; isNumber");

  ModelStore store;
  ProjectSettings s = store.settings();
  Variable width;
  width.name = "width";
  width.expression = "30";
  s.variables = {width};
  store.setSettings(s);
  store.addPrimitive(PrimitiveKind::box);
  store.bindDraft(0, std::string("width * 2"));
  store.applyDraft();
  auto size = [&] {
    auto p = store.properties(store.bodyIds()[0]);
    return p->bboxMax.x - p->bboxMin.x;
  };
  check(near(size(), 60, 0.5), "bound width is 60 (" + num(size()) + ")");
  store.updateSettings([](ProjectSettings& p) { p.variables[0].expression = "25"; });
  check(near(size(), 50, 0.5), "changing the variable rebuilds to 50 (" + num(size()) + ")");
  check(store.variableValues().at("width") == 25, "variable values");
  store.addVariable();
  check(store.settings().variables.back().name == "v2", "new variables are named v<n>");
}

// MARK: Export

static void exportTests() {
  std::printf("\n# Export\n");
  ModelStore store({Feature(op::Primitive{PrimitiveKind::box, {40, 40, 40}, {}}),
                    Feature(op::Primitive{PrimitiveKind::cylinder, {10, 30, 0}, {60, 0, 0}})});
  store.setDocumentName("Parts");
  auto step = store.exportData(ExportKind::step);
  std::string stepText = step ? std::string(step->begin(), step->end()) : "";
  check(step && stepText.find("ISO-10303-21") != std::string::npos, "STEP export (" + std::to_string(stepText.size()) + " bytes)");
  auto stl = store.exportData(ExportKind::stl);
  check(stl && stl->size() > 84, "STL export (" + std::to_string(stl ? stl->size() : 0) + " bytes)");
  auto brep = store.exportData(ExportKind::brep);
  check(brep && !brep->empty(), "BREP export");
  auto threeMF = store.exportData(ExportKind::threeMF);
  check(threeMF && (*threeMF)[0] == 'P' && (*threeMF)[1] == 'K', "3MF is a zip");
  auto obj = store.exportData(ExportKind::obj);
  check(obj && std::string(obj->begin(), obj->end()).find("o Body_1") != std::string::npos, "OBJ export");
  auto glb = store.exportData(ExportKind::glb);
  check(glb && glb->size() > 20 && std::string(glb->begin(), glb->begin() + 4) == "glTF", "GLB export");
  auto sculpt = store.exportData(ExportKind::sculpt3d);
  check(sculpt && Sculpt3DFile::decode(std::string(sculpt->begin(), sculpt->end())).features == store.features(),
        "Sculpt3D export round-trips");
  auto pdf = store.exportData(ExportKind::pdf);
  check(pdf && std::string(pdf->begin(), pdf->begin() + 5) == "%PDF-", "drawing PDF");
  auto svg = store.exportData(ExportKind::svg);
  check(svg && std::string(svg->begin(), svg->end()).find("FRONT") != std::string::npos, "drawing SVG with views");
  auto dxfDrawing = store.exportData(ExportKind::dxfDrawing);
  check(dxfDrawing && std::string(dxfDrawing->begin(), dxfDrawing->end()).find("HIDDEN") != std::string::npos, "drawing DXF");
  check(!store.exportData(ExportKind::usdz) && store.toast().has_value(), "USDZ reports it is unavailable");
  auto page = store.webPage("Parts");
  check(page && page->find("const parts = [{\"name\":\"Body 1\"") != std::string::npos, "web viewer page");

  // Re-import the STEP file.
  std::string path = (std::filesystem::temp_directory_path() / "sculpt_model_tests_export.step").string();
  std::ofstream(path, std::ios::binary).write(stepText.data(), std::streamsize(stepText.size()));
  ModelStore imported;
  imported.importFile(path);
  check(imported.bodyIds().size() == 2 && near(volume(imported), volume(store), 1),
        "STEP re-imports as 2 bodies, volume " + num(volume(imported)));
  std::remove(path.c_str());

  // Sketch DXF / SVG.
  ModelStore sk;
  sk.startSketch(SketchPlane::xy());
  sk.updateSketch([](SketchSession& s) {
    s.sketch.addRectangle({0, 0}, {20, 10}, std::nullopt, std::nullopt);
    s.sketch.addCircle(s.sketch.addPoint({30, 5}), 4);
  });
  auto dxf = sk.exportData(ExportKind::dxfSketch);
  std::string dxfText = dxf ? std::string(dxf->begin(), dxf->end()) : "";
  check(dxfText.find("LINE") != std::string::npos && dxfText.find("CIRCLE") != std::string::npos && dxfText.find("EOF") != std::string::npos,
        "sketch DXF");
  auto ssvg = sk.exportData(ExportKind::svg);
  check(ssvg && std::string(ssvg->begin(), ssvg->end()).find("<circle") != std::string::npos, "sketch SVG");

  check(formatted(1234.5678) == "1,234.568" && formatted(20) == "20" && formatLength(25.4, LengthUnit::inch) == "1 in",
        "number formatting");
  DisplayFormat::setFootInch(FootInchFormat::architectural);
  check(formatLength(368.3, LengthUnit::ft) == "1' 2 1/2\"", "architectural feet and inches");
  DisplayFormat::setFootInch(FootInchFormat::decimal);
  check(ToolCatalog::all().size() > 60 && ToolCatalog::search(store, "fillet").size() >= 1, "tool catalog and search");
}

int main() {
  jsonTests();
  sketchTests();
  sessionTests();
  replayTests();
  expressionTests();
  exportTests();
  std::printf("\n%d passed, %d failed\n", passes, failures);
  return failures == 0 ? 0 : 1;
}
