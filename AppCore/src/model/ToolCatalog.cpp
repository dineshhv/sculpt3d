// Port of Commands.swift (ToolCatalog) and the model side of ViewportController+Keys.swift.
#include "sculpt/model/ToolCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include "sculpt/model/ModelStore.hpp"

namespace sculpt {

const std::vector<ToolCategory>& allToolCategories() {
  using C = ToolCategory;
  static const std::vector<C> all = {C::sketch, C::insert, C::construct, C::transform, C::tools, C::visualize, C::view};
  return all;
}

const std::vector<ToolCategory>& railCategories() {
  using C = ToolCategory;
  static const std::vector<C> rail = {C::sketch, C::insert, C::construct, C::transform, C::tools, C::visualize};
  return rail;
}

std::string rawValue(ToolCategory c) {
  switch (c) {
    case ToolCategory::sketch: return "sketch";
    case ToolCategory::insert: return "insert";
    case ToolCategory::construct: return "construct";
    case ToolCategory::transform: return "transform";
    case ToolCategory::tools: return "tools";
    case ToolCategory::visualize: return "visualize";
    case ToolCategory::view: return "view";
  }
  return "";
}

std::string title(ToolCategory c) {
  std::string s = rawValue(c);
  s[0] = char(s[0] - 'a' + 'A');
  return s;
}

std::string symbol(ToolCategory c) {
  switch (c) {
    case ToolCategory::sketch: return "pencil.and.outline";
    case ToolCategory::insert: return "plus.square";
    case ToolCategory::construct: return "square.on.square.dashed";
    case ToolCategory::transform: return "move.3d";
    case ToolCategory::tools: return "wrench.and.screwdriver";
    case ToolCategory::visualize: return "paintpalette";
    case ToolCategory::view: return "eye";
  }
  return "";
}

namespace ToolCatalog {

bool idle(const ModelStore& s) { return !s.draft() && !s.sketch(); }
bool hasBody(const ModelStore& s) { return idle(s) && s.selectedBody().has_value(); }

static size_t distinctBodies(const ModelStore& s) {
  std::set<int> b;
  for (auto& p : s.selection()) b.insert(p.body);
  return b.size();
}
static bool hasPlaneItem(const ModelStore& s) {
  auto items = s.selectedItems();
  return std::any_of(items.begin(), items.end(), [](auto& i) { return i.isPlane(); });
}
static bool hasSketchItem(const ModelStore& s) {
  auto items = s.selectedItems();
  return std::any_of(items.begin(), items.end(), [](auto& i) { return i.isSketch(); });
}
static std::optional<Uuid> firstSketchItem(const ModelStore& s) {
  for (auto& i : s.selectedItems())
    if (i.isSketch()) return i.id;
  return std::nullopt;
}

static const auto always = [](const ModelStore&) { return true; };

template <class F>
static std::function<std::string(ModelStore&)> act(F f) {
  return [f](ModelStore& s) {
    f(s);
    return std::string();
  };
}
static std::function<std::string(ModelStore&)> ui(const std::string& action) {
  return [action](ModelStore&) { return action; };
}

static std::vector<ToolCommand> build() {
  using C = ToolCategory;
  std::vector<ToolCommand> list = {
      // Sketch
      {"sketch.top", "Sketch on Top Plane", "square.grid.3x3", C::sketch, "XY", idle,
       act([](ModelStore& s) { s.startSketch(SketchPlane::xy()); })},
      {"sketch.front", "Sketch on Front Plane", "square.grid.3x3", C::sketch, "XZ", idle,
       act([](ModelStore& s) { s.startSketch(SketchPlane::xz()); })},
      {"sketch.right", "Sketch on Right Plane", "square.grid.3x3", C::sketch, "YZ", idle,
       act([](ModelStore& s) { s.startSketch(SketchPlane::yz()); })},
      {"sketch.face", "Sketch on Face", "square.and.pencil", C::sketch, "Select a flat face",
       [](const ModelStore& s) { return idle(s) && !s.selectedFaces().empty(); },
       act([](ModelStore& s) { s.startSketchOnSelectedFace(); })},
      {"sketch.plane", "Sketch on Plane", "square.on.square.dashed", C::sketch, "Select a plane in Items",
       [](const ModelStore& s) { return idle(s) && hasPlaneItem(s); },
       act([](ModelStore& s) { s.startSketchOnSelectedPlane(); })},
      {"sketch.edit", "Edit Sketch", "pencil", C::sketch, "Select a sketch in Items",
       [](const ModelStore& s) { return idle(s) && hasSketchItem(s); },
       act([](ModelStore& s) {
         if (auto id = firstSketchItem(s)) s.editSketch(*id);
       })},
      // Insert
      {"insert.variable", "Create Variable", "function", C::insert, "Named values for any field", always,
       [](ModelStore& s) {
         if (s.settings().variables.empty()) s.addVariable();
         return std::string("ui.showVariables");
       }},
      {"insert.image", "Image", "photo.badge.plus", C::insert, "On a plane or flat face", idle, ui("ui.importImage")},
      {"insert.import", "File", "doc.badge.plus", C::insert, "STEP, IGES, STL, BREP", idle, ui("ui.importFile")},
      {"insert.project", "Project", "shippingbox", C::insert, "Bodies from another Sculpt3D project", idle,
       ui("ui.importProject")},
  };
  for (auto kind : allPrimitiveKinds())
    list.push_back({"insert." + rawValue(kind), title(kind), symbol(kind), C::insert, "", idle,
                    act([kind](ModelStore& s) { s.addPrimitive(kind); })});
  auto twoBodies = [](const ModelStore& s) { return idle(s) && distinctBodies(s) == 2; };
  auto faces = [](const ModelStore& s) { return idle(s) && !s.selectedFaces().empty(); };
  auto edges = [](const ModelStore& s) { return idle(s) && !s.selectedEdges().empty(); };
  std::vector<ToolCommand> more = {
      // Construct
      {"construct.offset", "Plane", "square.stack", C::construct, "Offset from a face, a plane or Top", idle,
       act([](ModelStore& s) { s.addOffsetPlane(); })},
      {"construct.angled", "Angled Plane", "square.on.square.squareshape.controlhandles", C::construct,
       "Turned about the base plane's X axis", idle, act([](ModelStore& s) { s.addAngledPlane(); })},
      {"construct.mid", "Midplane", "rectangle.split.2x1", C::construct, "Select two flat faces",
       [](const ModelStore& s) { return idle(s) && s.selectedFaces().size() == 2; },
       act([](ModelStore& s) { s.addMidplane(); })},
      {"construct.axis", "Axis", "line.diagonal", C::construct, "Through a round face, along an edge, or Z", idle,
       act([](ModelStore& s) { s.addAxis(); })},
      // Transform
      {"transform.move", "Move/Rotate", "move.3d", C::transform, "Select a body", hasBody,
       act([](ModelStore& s) { s.moveSelection(); })},
      {"transform.scale", "Scale", "arrow.up.left.and.arrow.down.right", C::transform, "Select a body", hasBody,
       act([](ModelStore& s) { s.scaleSelection(); })},
      {"transform.translate", "Translate", "arrow.right.square", C::transform, "Along an axis by a distance", hasBody,
       act([](ModelStore& s) { s.translateSelection(); })},
      {"transform.linear", "Pattern", "square.grid.3x1.below.line.grid.1x2", C::transform, "Linear copies", hasBody,
       act([](ModelStore& s) { s.linearPatternSelection(); })},
      {"transform.scatter", "Scatter", "circle.dotted", C::transform, "Body to scatter, then the ground", twoBodies,
       act([](ModelStore& s) { s.scatterSelection(); })},
      {"transform.circular", "Circular Pattern", "circle.grid.cross", C::transform, "Copies around an axis", hasBody,
       act([](ModelStore& s) { s.circularPatternSelection(); })},
      {"transform.rotate", "Rotate Around Axis", "arrow.clockwise.circle", C::transform, "A selected axis, or vertical",
       hasBody, act([](ModelStore& s) { s.rotateSelection(); })},
      {"transform.align", "Align", "align.horizontal.center", C::transform, "Body to move, then target", twoBodies,
       act([](ModelStore& s) { s.alignSelection(); })},
      {"transform.mirror", "Mirror", "arrow.left.and.right.righttriangle.left.righttriangle.right", C::transform,
       "Select a body", hasBody, act([](ModelStore& s) { s.mirrorSelection(); })},
      {"transform.duplicate", "Duplicate", "plus.square.on.square", C::transform, "Select a body", hasBody,
       act([](ModelStore& s) { s.duplicateSelection(); })},
      // Tools
      {"tools.offset", "Offset Face", "arrow.up.square", C::tools, "Select faces", faces,
       act([](ModelStore& s) { s.offsetFacesSelection(); })},
      {"tools.fillet", "Fillet", "circle.bottomhalf.filled", C::tools, "Select edges", edges,
       act([](ModelStore& s) { s.filletSelection(false); })},
      {"tools.chamfer", "Chamfer", "triangle.bottomhalf.filled", C::tools, "Select edges", edges,
       act([](ModelStore& s) { s.filletSelection(true); })},
      {"tools.extrude", "Extrude", "square.stack.3d.up", C::tools, "A sketch in Items, or a flat face",
       [](const ModelStore& s) { return idle(s) && (hasSketchItem(s) || s.selectedFaces().size() == 1); },
       act([](ModelStore& s) {
         if (auto id = firstSketchItem(s)) s.useSketchItem(*id, false);
         else s.pushPullSelection();
       })},
      {"tools.shell", "Shell", "square.dashed", C::tools, "Select a body or faces to open", hasBody,
       act([](ModelStore& s) { s.shellSelection(); })},
      {"tools.loft", "Loft", "cone", C::tools, "Select 2+ sketches in order", idle,
       act([](ModelStore& s) { s.loftSelection(); })},
  };
  list.insert(list.end(), more.begin(), more.end());
  for (auto kind : allBooleanKinds())
    list.push_back({"tools." + rawValue(kind), title(kind), symbol(kind), C::tools, "Select two bodies", twoBodies,
                    act([kind](ModelStore& s) { s.boolean(kind); })});
  std::vector<ToolCommand> rest = {
      {"tools.split", "Split Body", "square.split.diagonal", C::tools, "Select a body", hasBody,
       act([](ModelStore& s) { s.splitSelection(); })},
      {"tools.revolve", "Revolve", "arrow.triangle.2.circlepath", C::tools, "Select a sketch in Items",
       [](const ModelStore& s) { return idle(s) && hasSketchItem(s); },
       act([](ModelStore& s) {
         if (auto id = firstSketchItem(s)) s.useSketchItem(*id, true);
       })},
      {"tools.sweep", "Sweep", "point.bottomleft.forward.to.point.topright.scurvepath", C::tools,
       "Select profile, then path sketch", idle, act([](ModelStore& s) { s.sweepSelection(); })},
      {"tools.fill", "Fill", "square.fill.on.square", C::tools, "Select a closed loop of edges", edges,
       act([](ModelStore& s) { s.fillSelection(); })},
      {"tools.replaceFace", "Replace Face", "square.2.layers.3d.top.filled", C::tools, "Face to move, then target face",
       [](const ModelStore& s) { return idle(s) && s.selectedFaces().size() == 2; },
       act([](ModelStore& s) { s.replaceFaceSelection(); })},
      {"tools.project", "Project as Edge", "rectangle.portrait.on.rectangle.portrait.angled", C::tools,
       "A body onto a plane or face", hasBody, act([](ModelStore& s) { s.projectSelection(); })},
      {"tools.pushpull", "Push/Pull", "arrow.up.and.down.square", C::tools, "Select a flat face",
       [](const ModelStore& s) { return idle(s) && s.selectedFaces().size() == 1; },
       act([](ModelStore& s) { s.pushPullSelection(); })},
      {"tools.draft", "Draft", "trapezoid.and.line.vertical", C::tools, "Select side faces", faces,
       act([](ModelStore& s) { s.draftSelection(); })},
      {"tools.deleteFaces", "Delete Faces", "eraser", C::tools, "Heals the body around them", faces,
       act([](ModelStore& s) { s.deleteFacesSelection(); })},
      {"tools.deleteBody", "Delete Body", "trash", C::tools, "Select bodies", hasBody,
       act([](ModelStore& s) { s.deleteSelection(); })},
      // View (viewport actions)
      {"view.fit", "Zoom to Fit", "arrow.up.left.and.arrow.down.right", C::view, "", always, ui("view.fit")},
      {"view.iso", "Isometric View", "cube", C::view, "", always, ui("view.iso")},
      {"view.top", "Top View", "square", C::view, "", always, ui("view.top")},
      {"view.front", "Front View", "square", C::view, "", always, ui("view.front")},
      {"view.right", "Right View", "square", C::view, "", always, ui("view.right")},
      {"view.section", "Section View", "square.split.bottomrightquarter", C::view, "", always, ui("view.toggleSection")},
      {"view.measure", "Measure", "ruler", C::view, "", always, ui("view.toggleMeasure")},
      {"view.screenshot", "Screenshot", "camera", C::view, "", always, ui("view.screenshot")},
  };
  list.insert(list.end(), rest.begin(), rest.end());
  struct Mode { const char* raw; const char* title; const char* symbol; };
  const Mode modes[] = {{"shaded", "Shaded", "cube.fill"},           {"wireframe", "Wireframe", "cube"},
                        {"xray", "X-Ray", "cube.transparent"},       {"visualized", "Visualized", "sparkles"},
                        {"zebra", "Zebra Analysis", "line.3.horizontal"},
                        {"curvature", "Curvature Map", "circle.dotted.and.circle"}};
  for (auto& m : modes)
    list.push_back({std::string("view.") + m.raw, m.title, m.symbol, C::view, "", always,
                    ui(std::string("view.mode.") + m.raw)});
  for (auto f : allExchangeFormats())
    list.push_back({"export." + rawValue(f), "Export " + title(f) + "…", "square.and.arrow.up", C::view, "", always,
                    ui("export." + rawValue(f))});
  return list;
}

const std::vector<ToolCommand>& all() {
  static const std::vector<ToolCommand> list = build();
  return list;
}

std::vector<ToolCommand> commands(ToolCategory category) {
  std::vector<ToolCommand> out;
  for (auto& c : all())
    if (c.category == category) out.push_back(c);
  return out;
}

const ToolCommand* command(const std::string& id) {
  for (auto& c : all())
    if (c.id == id) return &c;
  return nullptr;
}

const std::map<char, std::string>& keyShortcuts() {
  static const std::map<char, std::string> keys = {
      {'e', "tools.extrude"}, {'v', "tools.revolve"},   {'w', "tools.sweep"},         {'f', "tools.fillet"},
      {'h', "tools.shell"},   {'m', "transform.move"}, {'n', "transform.translate"}, {'k', "sketch.top"},
  };
  return keys;
}

std::optional<std::string> shortcut(const std::string& id) {
  for (auto& [k, v] : keyShortcuts())
    if (v == id) return std::string(1, char(std::toupper((unsigned char)k)));
  static const std::map<std::string, std::string> other = {
      {"tools.union", "⌘U"},          {"tools.subtract", "⌘B"}, {"tools.intersect", "⌘I"},
      {"transform.duplicate", "⌘D"}, {"view.fit", "⇧⌘F"}, {"view.measure", "⌥⌘M"},
      {"view.section", "⌥⌘S"},  {"insert.import", "⇧⌘I"}};
  auto it = other.find(id);
  if (it == other.end()) return std::nullopt;
  return it->second;
}

static std::string lower(std::string s) {
  for (auto& c : s) c = char(std::tolower((unsigned char)c));
  return s;
}

std::vector<const ToolCommand*> search(const ModelStore& store, const std::string& query) {
  std::string q = query;
  while (!q.empty() && q.front() == ' ') q.erase(q.begin());
  while (!q.empty() && q.back() == ' ') q.pop_back();
  q = lower(q);
  std::vector<const ToolCommand*> out;
  for (auto& c : all()) {
    if (q.empty() ? c.isEnabled(store) : lower(c.title + " " + title(c.category) + " " + c.hint).find(q) != std::string::npos)
      out.push_back(&c);
  }
  return out;
}

std::optional<std::string> run(ModelStore& store, const std::string& id) {
  auto c = command(id);
  if (!c || !c->isEnabled(store)) return std::nullopt;
  return c->run(store);
}

std::optional<std::string> handleKey(ModelStore& store, const std::string& key) {
  if (key.empty()) return std::nullopt;
  bool isDelete = key == "Delete" || key == "Backspace" || key == "\x7f" || key == "\b";
  if (store.sketch()) {
    if (key.size() == 1) {
      char ch = char(std::tolower((unsigned char)key[0]));
      for (auto t : allSketchTools())
        if (sculpt::key(t) == ch) {
          store.setSketchTool(t);
          return std::string();
        }
    }
    if (isDelete) {
      store.deleteSketchSelection();
      return std::string();
    }
    return std::nullopt;
  }
  if (key.size() != 1) return std::nullopt;
  auto it = keyShortcuts().find(char(std::tolower((unsigned char)key[0])));
  if (it == keyShortcuts().end()) return std::nullopt;
  return run(store, it->second);
}

}  // namespace ToolCatalog
}  // namespace sculpt
