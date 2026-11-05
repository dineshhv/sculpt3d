// Drives the C API (include/sculpt/sculpt_c.h) headlessly: documents,
// commands, sketching through pointer events on a fixed-size viewport,
// extrude, fillet, undo/redo, state JSON, keys, annotations, save/load and
// STEP export. Built against sculpt_headless (SCULPT_WITH_GL=0), so the
// viewport does camera maths, picking and input but no rendering.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "sculpt/sculpt_c.h"

using json = nlohmann::json;

static int failures = 0, passes = 0;
static void check(bool ok, const std::string& what) {
  if (ok) {
    ++passes;
    std::printf("ok   %s\n", what.c_str());
  } else {
    ++failures;
    std::printf("FAIL %s\n", what.c_str());
  }
}

static json take(char* s) {
  if (!s) return nullptr;
  json j = json::parse(s);
  sculpt_free(s);
  return j;
}

static json state(SculptDoc* d) { return take(sculpt_doc_state(d)); }

static json cmd(SculptDoc* d, json c) { return take(sculpt_doc_command(d, c.dump().c_str())); }

static bool okCmd(SculptDoc* d, json c) {
  json r = cmd(d, c);
  if (!r.value("ok", false)) std::printf("     %s -> %s\n", c.dump().c_str(), r.dump().c_str());
  return r.value("ok", false);
}

/// A mouse drag with the primary button, in view points.
static void drag(SculptDoc* d, float x0, float y0, float x1, float y1, int mods = 0) {
  sculpt_view_pointer(d, 1, SCULPT_POINTER_MOUSE, SCULPT_BUTTON_PRIMARY, SCULPT_PHASE_DOWN, x0, y0, mods);
  for (int i = 1; i <= 10; ++i) {
    float t = float(i) / 10;
    sculpt_view_pointer(d, 1, SCULPT_POINTER_MOUSE, SCULPT_BUTTON_PRIMARY, SCULPT_PHASE_MOVE, x0 + (x1 - x0) * t,
                        y0 + (y1 - y0) * t, mods);
  }
  sculpt_view_pointer(d, 1, SCULPT_POINTER_MOUSE, SCULPT_BUTTON_PRIMARY, SCULPT_PHASE_UP, x1, y1, mods);
}

static void click(SculptDoc* d, float x, float y, int type = SCULPT_POINTER_MOUSE, int mods = 0) {
  sculpt_view_pointer(d, 1, type, SCULPT_BUTTON_PRIMARY, SCULPT_PHASE_DOWN, x, y, mods);
  sculpt_view_pointer(d, 1, type, SCULPT_BUTTON_PRIMARY, SCULPT_PHASE_UP, x, y, mods);
}

static std::atomic<int> listenerCalls{0};
static void listener(void*) { ++listenerCalls; }

static void basics() {
  check(std::strlen(sculpt_version()) > 0, "version string");
  SculptDoc* d = sculpt_doc_create("Part");
  check(d != nullptr, "document created");
  sculpt_view_resize(d, 1600, 1200, 2);  // 800 × 600 points
  json s = state(d);
  check(s["document"]["name"] == "Part", "state: document name");
  check(s["mode"] == "modeling" && s["features"].empty() && s["bodies"].empty(), "state: empty modeling document");
  check(s["tools"].size() == 7 && s["actions"].contains("model.pushPull"), "state: tool catalog and actions");
  check(s["sketch"].is_null() && s["draft"].is_null(), "state: no sketch, no draft");

  json bad = cmd(d, {{"cmd", "no.such.command"}});
  check(bad["ok"] == false && bad["error"].get<std::string>().find("Unknown command") != std::string::npos,
        "unknown command is an error");
  check(cmd(d, {{"cmd", "insert.primitive"}, {"kind", "hexagon"}})["ok"] == false, "bad argument is an error");
  check(cmd(d, {{"cmd", "insert.primitive"}})["ok"] == false, "missing argument is an error");
  check(take(sculpt_doc_command(d, "{not json"))["ok"] == false, "invalid JSON is an error");
  check(cmd(d, {{"cmd", "model.pushPull"}})["ok"] == false, "disabled action is an error");

  sculpt_doc_set_listener(d, listener, nullptr);
  uint64_t r0 = sculpt_doc_revision(d);
  check(okCmd(d, {{"cmd", "insert.primitive"}, {"kind", "box"}}), "insert box");
  check(state(d)["draft"]["title"] == "Box", "a primitive opens its tool panel");
  check(okCmd(d, {{"cmd", "draft.apply"}}), "apply the box");
  check(sculpt_doc_revision(d) > r0 && listenerCalls > 0, "revision moved and the listener ran");
  s = state(d);
  check(s["features"].size() == 1 && s["bodies"].size() == 1, "one feature, one body");
  check(s["features"][0]["title"].is_string() && s["features"][0]["symbol"].is_string(), "feature has title and symbol");
  check(sculpt_doc_is_dirty(d) == 1, "document is dirty after an edit");

  // Camera moves change the view revision, not the document's.
  uint64_t docRev = sculpt_doc_revision(d), viewRev = sculpt_view_revision(d);
  sculpt_view_orbit(d, 20, 5);
  check(sculpt_doc_revision(d) == docRev && sculpt_view_revision(d) > viewRev, "orbit moves only the view revision");
  check(sculpt_view_needs_redraw(d) == 1, "orbit needs a redraw");
  sculpt_view_render(d);  // headless: no GL, still clears the flag
  check(sculpt_view_needs_redraw(d) == 0, "render clears needs-redraw");
  size_t n = 0;
  check(sculpt_view_snapshot(d, 64, 64, &n) == nullptr, "snapshot is unavailable without GL");

  // Picking: a click at the centre of the framed box selects a face; a quick
  // second click there is a double click, which selects the body.
  okCmd(d, {{"cmd", "view.look"}, {"preset", "front"}});
  okCmd(d, {{"cmd", "view.fit"}});
  click(d, 400, 300);
  s = state(d);
  check(s["selection"]["faceCount"] == 1, "click selects a face");
  check(s["selectionBar"].size() > 0 && s["selectionBar"][0]["id"] == "model.pushPull", "selection bar offers Push/Pull");
  click(d, 400, 300);
  s = state(d);
  check(s["selection"]["bodyCount"] == 1 && s["selection"]["faceCount"] == 0, "double click selects the body");
  check(!s["bodies"][0]["properties"].is_null(), "selected body has properties");
  check(sculpt_view_key(d, "Escape", 0) == 1 && state(d)["selection"]["picks"].empty(), "Escape clears the selection");

  // Hover with the mouse highlights without changing the state revision.
  docRev = sculpt_doc_revision(d);
  sculpt_view_pointer(d, 0, SCULPT_POINTER_MOUSE, SCULPT_BUTTON_PRIMARY, SCULPT_PHASE_HOVER, 400, 300, 0);
  check(state(d)["selection"]["hover"].is_object(), "hover picks the face under the mouse");
  sculpt_view_pointer(d, 0, SCULPT_POINTER_MOUSE, SCULPT_BUTTON_PRIMARY, SCULPT_PHASE_EXIT, 0, 0, 0);
  check(state(d)["selection"]["hover"].is_null(), "exit clears the hover");

  // Keys: ⌘Z / ⇧⌘Z (Control on Windows/Android).
  check(sculpt_view_key(d, "z", SCULPT_MOD_CONTROL) == 1 && state(d)["features"].empty(), "Ctrl+Z undoes");
  check(sculpt_view_key(d, "z", SCULPT_MOD_CONTROL | SCULPT_MOD_SHIFT) == 1 && state(d)["features"].size() == 1,
        "Ctrl+Shift+Z redoes");
  check(sculpt_view_key(d, "q", 0) == 0, "unbound key is not handled");
  check(sculpt_view_key(d, "z", SCULPT_MOD_ALT) == 1 && state(d)["view"]["displayMode"] == "zebra", "Alt+Z is Zebra");
  okCmd(d, {{"cmd", "view.setDisplayMode"}, {"mode", "shaded"}});

  sculpt_doc_set_listener(d, nullptr, nullptr);
  sculpt_doc_destroy(d);
}

static void sketchFlow() {
  SculptDoc* d = sculpt_doc_create("Bracket");
  sculpt_view_resize(d, 800, 600, 1);
  check(okCmd(d, {{"cmd", "sketch.start"}, {"plane", "xy"}}), "start a sketch on Top");
  json s = state(d);
  check(s["mode"] == "sketching" && s["sketch"]["tool"] == "rectangle", "sketching with the rectangle tool");
  check(s["sketch"]["plane"]["name"] == "Top plane", "sketch plane is Top");
  json a = take(sculpt_view_annotations(d));
  check(std::abs(a["camera"]["pitch"].get<double>()) > 1.5, "the camera faces the sketch plane");

  // A rectangle dragged with the mouse (pen and mouse drags draw in sketches).
  // The view is 800 × 600 points looking down at the plane from 220 mm with a
  // 40° field of view: 37.5 points per 10 mm grid step, origin at the centre.
  drag(d, 325, 225, 475, 375);
  s = state(d);
  check(s["sketch"]["counts"]["lines"] == 4, "drag draws a rectangle (4 lines)");
  check(s["sketch"]["profiles"] == 1, "the rectangle is one profile");
  check(s["sketch"]["canUndo"] == true, "sketch undo is available");

  // Select one side by clicking on it and give it a length.
  click(d, 400, 225);
  s = state(d);
  bool lineSelected = s["sketch"]["selection"].size() == 1 && s["sketch"]["selection"][0]["kind"] == "line";
  check(lineSelected, "click selects a rectangle side");
  if (lineSelected) {
    check(okCmd(d, {{"cmd", "sketch.constrain"}, {"kind", "length"}}), "add a length dimension");
    s = state(d);
    int dims = 0, dimIndex = -1;
    for (auto& c : s["sketch"]["constraints"])
      if (c["isDimension"] == true) ++dims, dimIndex = c["index"];
    check(dims == 1, "the sketch has one dimension");
    a = take(sculpt_view_annotations(d));
    bool labelled = false;
    for (auto& an : a["annotations"])
      if (an["kind"] == "dimension") {
        double x = an["x"], y = an["y"];
        labelled = x > 0 && x < 800 && y > 0 && y < 600 && an.contains("editValue");
      }
    check(labelled, "the dimension label is placed on screen with an editable value");
    check(okCmd(d, {{"cmd", "sketch.setDimension"}, {"index", dimIndex}, {"text", "60"}}), "set the dimension to 60");
    s = state(d);
    check(std::abs(s["sketch"]["constraints"][dimIndex]["value"].get<double>() - 60) < 1e-6, "dimension value is 60 mm");
    check(cmd(d, {{"cmd", "sketch.setDimension"}, {"index", dimIndex}, {"text", "-5"}})["ok"] == false,
          "a negative length is refused");
  }
  // Keys in a sketch: tool keys, Escape deselects first.
  check(sculpt_view_key(d, "l", 0) == 1 && state(d)["sketch"]["tool"] == "line", "L picks the line tool");
  check(sculpt_view_key(d, "r", 0) == 1 && state(d)["sketch"]["tool"] == "rectangle", "R picks the rectangle tool");
  okCmd(d, {{"cmd", "sketch.clearSelection"}});

  // Finish as an extrusion and set its distance.
  check(okCmd(d, {{"cmd", "sketch.finish"}}), "finish the sketch (extrude)");
  s = state(d);
  check(s["sketch"].is_null() && s["draft"].is_object(), "the extrude tool is open");
  check(s["draft"]["parameters"][0]["label"] == "Distance", "draft shows its distance parameter");
  check(okCmd(d, {{"cmd", "draft.setParameter"}, {"id", 0}, {"text", "25"}}), "type 25 into the distance");
  s = state(d);
  check(std::abs(s["draft"]["parameters"][0]["value"].get<double>() - 25) < 1e-6, "distance is 25 mm");
  check(okCmd(d, {{"cmd", "draft.stepParameter"}, {"id", 0}, {"direction", 1}}), "the + button steps the distance");
  check(std::abs(state(d)["draft"]["parameters"][0]["value"].get<double>() - 30) < 1e-6, "distance stepped to 30 mm");
  check(okCmd(d, {{"cmd", "draft.apply"}}), "apply the extrusion");
  s = state(d);
  check(s["draft"].is_null() && s["bodies"].size() == 1, "one extruded body");
  int body = s["bodies"][0]["id"];
  json props = cmd(d, {{"cmd", "body.properties"}, {"id", body}})["properties"];
  double vol = props.is_object() ? props["volume"].get<double>() : 0;
  check(vol > 0, "body has a volume");
  if (lineSelected) {
    auto size = props["size"];
    check(std::abs(size[0].get<double>() - 60) < 1e-3 || std::abs(size[1].get<double>() - 60) < 1e-3,
          "the dimensioned side is 60 mm");
  }
  check(std::abs(props["size"][2].get<double>() - 30) < 1e-3, "the extrusion is 30 mm tall");

  // Fillet an edge.
  check(okCmd(d, {{"cmd", "selection.set"}, {"picks", {{{"kind", "edge"}, {"body", body}, {"index", 0}}}}}), "select an edge");
  check(state(d)["actions"]["model.fillet"] == true, "fillet is enabled for an edge");
  check(okCmd(d, {{"cmd", "model.fillet"}}), "open the fillet tool");
  check(okCmd(d, {{"cmd", "draft.setParameter"}, {"id", 0}, {"value", 2}}), "fillet radius 2 mm");
  check(state(d)["draft"]["error"].is_null(), "the fillet previews without error");
  check(okCmd(d, {{"cmd", "draft.apply"}}), "apply the fillet");
  s = state(d);
  bool anyError = false;
  for (auto& f : s["features"]) anyError = anyError || !f["error"].is_null();
  check(s["features"].size() == 2 && !anyError, "history: extrude and fillet, no errors");
  double vol2 = cmd(d, {{"cmd", "body.properties"}, {"id", body}})["properties"]["volume"];
  check(vol2 < vol, "the fillet removed material");

  // Undo / redo through the actions.
  check(okCmd(d, {{"cmd", "edit.undo"}}) && state(d)["features"].size() == 1, "undo the fillet");
  check(okCmd(d, {{"cmd", "edit.redo"}}) && state(d)["features"].size() == 2, "redo the fillet");

  // Editing a feature reopens its draft.
  std::string extrudeId = state(d)["features"][0]["id"];
  check(okCmd(d, {{"cmd", "feature.open"}, {"id", extrudeId}}) && state(d)["draft"]["replacing"] == extrudeId,
        "tapping a history row edits it");
  check(okCmd(d, {{"cmd", "draft.cancel"}}), "cancel the edit");

  // Save, load into a new document, STEP export.
  char* saved = sculpt_doc_save(d);
  std::string bytes = saved;
  sculpt_free(saved);
  check(bytes.find("\"features\"") != std::string::npos, "save writes .sculpt3d JSON");
  sculpt_doc_mark_saved(d);
  check(sculpt_doc_is_dirty(d) == 0, "marked saved");
  SculptDoc* e = sculpt_doc_create("Copy");
  char* err = sculpt_doc_load(e, bytes.data(), bytes.size());
  check(err == nullptr, "load the saved bytes");
  if (err) sculpt_free(err);
  json se = state(e);
  check(se["features"].size() == 2 && se["bodies"].size() == 1, "loaded document has the same history and body");
  check(sculpt_doc_is_dirty(e) == 0, "a loaded document is clean");
  char* again = sculpt_doc_save(e);
  check(std::string(again) == bytes, "save → load → save round-trips byte for byte");
  sculpt_free(again);
  char* badErr = sculpt_doc_load(e, "{]", 2);
  check(badErr != nullptr, "loading garbage reports an error");
  if (badErr) sculpt_free(badErr);

  size_t n = 0;
  char* exportErr = nullptr;
  uint8_t* step = sculpt_doc_export(d, "step", nullptr, &n, &exportErr);
  check(step && n > 100 && std::string(reinterpret_cast<char*>(step), 12) == "ISO-10303-21", "export STEP");
  if (step) sculpt_free(step);
  uint8_t* nope = sculpt_doc_export(d, "parasolid", nullptr, &n, &exportErr);
  check(!nope && exportErr, "unavailable format reports an error");
  if (exportErr) sculpt_free(exportErr);
  exportErr = nullptr;
  uint8_t* pdf = sculpt_doc_export(d, "pdf", R"({"drawing":{"paper":"a4","views":["front","top"]}})", &n, &exportErr);
  check(pdf && n > 100 && std::memcmp(pdf, "%PDF", 4) == 0, "export a drawing PDF with options");
  if (pdf) sculpt_free(pdf);
  if (exportErr) sculpt_free(exportErr);
  json sheet = cmd(d, {{"cmd", "drawing.sheet"}, {"drawing", {{"paper", "a3"}}}});
  check(sheet["ok"] == true && sheet["sheet"]["strokes"].size() > 4, "drawing sheet strokes for native drawing");

  sculpt_doc_destroy(e);
  sculpt_doc_destroy(d);
}

static void moreFlows() {
  SculptDoc* d = sculpt_doc_create("Misc");
  sculpt_view_resize(d, 1000, 700, 1);
  // Touch: a one-finger drag outside a sketch orbits, a second finger cancels.
  okCmd(d, {{"cmd", "insert.primitive"}, {"kind", "cylinder"}});
  okCmd(d, {{"cmd", "draft.apply"}});
  json a0 = take(sculpt_view_annotations(d));
  sculpt_view_pointer(d, 7, SCULPT_POINTER_TOUCH, 0, SCULPT_PHASE_DOWN, 500, 350, 0);
  sculpt_view_pointer(d, 7, SCULPT_POINTER_TOUCH, 0, SCULPT_PHASE_MOVE, 520, 350, 0);
  sculpt_view_pointer(d, 7, SCULPT_POINTER_TOUCH, 0, SCULPT_PHASE_MOVE, 560, 360, 0);
  sculpt_view_pointer(d, 7, SCULPT_POINTER_TOUCH, 0, SCULPT_PHASE_UP, 560, 360, 0);
  json a1 = take(sculpt_view_annotations(d));
  check(a1["camera"]["yaw"] != a0["camera"]["yaw"], "one-finger touch drag orbits");
  check(state(d)["selection"]["picks"].empty(), "a drag is not a tap");

  // Measure mode keeps the last two picks.
  okCmd(d, {{"cmd", "view.setMeasuring"}, {"on", true}});
  okCmd(d, {{"cmd", "view.fit"}});
  okCmd(d, {{"cmd", "view.look"}, {"preset", "front"}});
  okCmd(d, {{"cmd", "view.fit"}});
  json s = state(d);
  check(s["mode"] == "measuring", "measure mode");
  okCmd(d, {{"cmd", "selection.set"}, {"picks", {{{"kind", "body"}, {"body", s["bodies"][0]["id"]}}}}});
  s = state(d);
  bool hasVolume = false;
  for (auto& r : s["measure"]["rows"]) hasVolume = hasVolume || r["label"] == "Volume";
  check(hasVolume, "measure rows show the body's volume");
  okCmd(d, {{"cmd", "measure.pin"}, {"text", "Volume: 1"}});
  check(state(d)["measure"]["pinned"].size() == 1, "pin a measurement");
  okCmd(d, {{"cmd", "view.setMeasuring"}, {"on", false}});

  // Point pick: point-to-point distance with two clicks.
  okCmd(d, {{"cmd", "selection.clear"}});
  check(okCmd(d, {{"cmd", "pick.measurePointToPoint"}}), "start a point-to-point measurement");
  s = state(d);
  check(s["pointPick"].is_object() && s["pointPick"]["needed"] == 2, "point pick prompts for two points");
  click(d, 500, 350);
  s = state(d);
  check(s["pointPick"]["picked"] == 1, "the first click picks a point");
  click(d, 500, 250);
  s = state(d);
  check(s["pointPick"].is_null() && s["measure"]["pointMeasurements"].size() == 1, "the second click measures");
  json labels = take(sculpt_view_annotations(d))["measurements"];
  check(labels.size() == 1 && labels[0]["text"].get<std::string>().find("mm") != std::string::npos,
        "the measurement label is placed over the view");
  check(s["measure"]["recent"].size() == 1, "the measurement is in Recent");
  okCmd(d, {{"cmd", "pick.axisThrough2Points"}});
  check(sculpt_view_key(d, "Escape", 0) == 1 && state(d)["pointPick"].is_null(), "Escape cancels a point pick");

  // View state commands.
  check(okCmd(d, {{"cmd", "view.section"}, {"enabled", true}, {"axis", 0}}), "section view on X");
  s = state(d);
  check(s["view"]["section"]["enabled"] == true && s["view"]["section"]["axis"] == 0, "section state");
  check(okCmd(d, {{"cmd", "view.setVisualizing"}, {"on", true}}) && state(d)["mode"] == "visualize", "visualize mode");
  check(okCmd(d, {{"cmd", "appearance.set"}, {"finish", "metal"}}), "metal finish on all bodies");
  check(state(d)["visualize"]["finishIndex"] == 2, "finish shows Metal");
  json lib = cmd(d, {{"cmd", "materials.library"}, {"query", "steel"}});
  check(lib["ok"] == true && !lib["families"].empty(), "material library search");
  okCmd(d, {{"cmd", "view.goToModeling"}});
  check(okCmd(d, {{"cmd", "settings.setUnits"}, {"units", "inch"}}) && state(d)["units"]["symbol"] == "in", "units: inches");
  check(okCmd(d, {{"cmd", "variable.add"}}) && state(d)["settings"]["variables"].size() == 1, "add a variable");

  // Tools by id; core-handled UI actions and front-end ones.
  json r = cmd(d, {{"cmd", "tool.run"}, {"id", "view.top"}});
  check(r["ok"] == true && !r.contains("ui"), "tool view.top runs in the core");
  r = cmd(d, {{"cmd", "tool.run"}, {"id", "insert.import"}});
  check(r["ok"] == true && r["ui"] == "ui.importFile", "tool insert.import asks the front end for a file");
  json found = cmd(d, {{"cmd", "tool.search"}, {"query", "fillet"}});
  check(found["results"].size() >= 1, "tool search");

  // MCP registration is safe without a running server.
  sculpt_mcp_register(d);
  sculpt_mcp_activate(d);
  sculpt_mcp_unregister(d);
  check(true, "MCP register/activate/unregister");
  sculpt_doc_destroy(d);
}

int main() {
  basics();
  sketchFlow();
  moreFlows();
  std::printf("\n%d passed, %d failed\n", passes, failures);
  return failures ? 1 : 0;
}
