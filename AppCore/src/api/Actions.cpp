#include "api/Actions.hpp"

#include <algorithm>
#include <set>

#include "api/Document.hpp"
#include "sculpt/model/ToolCatalog.hpp"

namespace sculpt::api {

namespace {

ModelStore& S(SculptDoc& d) { return *d.store; }
bool idle(SculptDoc& d) { return !d.store->draft() && !d.store->sketch(); }
bool hasBody(SculptDoc& d) { return idle(d) && d.store->selectedBody().has_value(); }
int distinctBodies(SculptDoc& d) {
  std::set<int> b;
  for (auto& p : d.store->selection()) b.insert(p.body);
  return int(b.size());
}
size_t faces(SculptDoc& d) { return d.store->selectedFaces().size(); }
size_t edges(SculptDoc& d) { return d.store->selectedEdges().size(); }
bool planeItemSelected(SculptDoc& d) {
  for (auto& i : d.store->selectedItems())
    if (i.isPlane()) return true;
  return false;
}
size_t planeItems(SculptDoc& d) {
  size_t n = 0;
  for (auto& i : d.store->selectedItems())
    if (i.isPlane()) ++n;
  return n;
}
bool inSketch(SculptDoc& d) { return d.store->sketch().has_value(); }
bool sketchSelection(SculptDoc& d) { return d.store->sketch() && !d.store->sketch()->selection.empty(); }
bool toolEnabled(SculptDoc& d, const char* id) {
  auto c = ToolCatalog::command(id);
  return c && c->isEnabled(*d.store);
}

using Run = std::function<std::string(SculptDoc&)>;
Run store(void (ModelStore::*f)()) {
  return [f](SculptDoc& d) {
    (S(d).*f)();
    return std::string();
  };
}
Run pickRun(PointPickPurpose p) {
  return [p](SculptDoc& d) {
    d.view->beginPointPick(p);
    return std::string();
  };
}

}  // namespace

void startSketchHere(ModelStore& s) {
  bool plane = false;
  for (auto& i : s.selectedItems())
    if (i.isPlane()) plane = true;
  if (!s.selectedFaces().empty()) {
    s.startSketchOnSelectedFace();
  } else if (plane) {
    s.startSketchOnSelectedPlane();
  } else {
    s.startSketch(SketchPlane::xy());
  }
}

const std::vector<Action>& actions() {
  static const std::vector<Action> list = [] {
    std::vector<Action> a;
    auto add = [&](std::string id, std::string title, std::string symbol, std::function<bool(SculptDoc&)> enabled, Run run) {
      a.push_back({std::move(id), std::move(title), std::move(symbol), std::move(enabled), std::move(run)});
    };
    auto always = [](SculptDoc&) { return true; };

    // Edit menu and the History panel's buttons (undo steps through the sketch while sketching).
    add("edit.undo", "Undo", "arrow.uturn.backward",
        [](SculptDoc& d) { return inSketch(d) ? S(d).canUndoSketch() : (S(d).canUndo() && !S(d).draft()); },
        [](SculptDoc& d) {
          inSketch(d) ? S(d).undoSketch() : S(d).undo();
          return std::string();
        });
    add("edit.redo", "Redo", "arrow.uturn.forward",
        [](SculptDoc& d) { return inSketch(d) ? S(d).canRedoSketch() : (S(d).canRedo() && !S(d).draft()); },
        [](SculptDoc& d) {
          inSketch(d) ? S(d).redoSketch() : S(d).redo();
          return std::string();
        });
    add("edit.delete", "Delete", "trash",
        [](SculptDoc& d) { return inSketch(d) ? sketchSelection(d) : (S(d).selectedBody().has_value() && !S(d).draft()); },
        [](SculptDoc& d) {
          inSketch(d) ? S(d).deleteSketchSelection() : S(d).deleteSelection();
          return std::string();
        });
    add("edit.duplicate", "Duplicate", "plus.square.on.square", [](SculptDoc& d) { return toolEnabled(d, "transform.duplicate"); },
        store(&ModelStore::duplicateSelection));
    add("edit.selectAll", "Select All Bodies", "checkmark.circle", idle, [](SculptDoc& d) {
      std::vector<Pick> sel;
      for (int id : S(d).bodyIds())
        if (!S(d).settings().hiddenBodies.count(id)) sel.push_back(Pick::bodyPick(id));
      S(d).setSelection(sel);
      return std::string();
    });
    add("edit.deselectAll", "Deselect All", "circle", always, [](SculptDoc& d) {
      S(d).setSelection({});
      S(d).setItemSelection({});
      return std::string();
    });

    // Item menu.
    add("item.toggleVisibilityOfSelection", "Show/Hide", "eye",
        [](SculptDoc& d) { return !S(d).selection().empty() || !S(d).itemSelection().empty(); },
        store(&ModelStore::toggleVisibilityOfSelection));
    add("item.isolate", "Isolate", "scope", [](SculptDoc& d) { return !S(d).touchedBodies().empty(); },
        store(&ModelStore::isolateSelection));
    add("item.exitIsolation", "Exit Isolation", "arrow.uturn.backward", [](SculptDoc& d) { return S(d).isIsolating(); },
        store(&ModelStore::exitIsolation));
    add("item.showAll", "Show Hidden Bodies", "eye", [](SculptDoc& d) { return !S(d).settings().hiddenBodies.empty(); },
        store(&ModelStore::showAllBodies));
    add("item.invertVisibility", "Invert Body Visibility", "eye.slash", [](SculptDoc& d) { return !S(d).bodyIds().empty(); },
        store(&ModelStore::invertBodyVisibility));
    add("view.zoomToSelection", "Zoom to", "arrow.up.left.and.arrow.down.right",
        [](SculptDoc& d) { return !S(d).touchedBodies().empty(); },
        [](SculptDoc& d) {
          d.view->zoomToSelection();
          return std::string();
        });

    // Selection bar and the Tools / Transform / Construct menus.
    add("model.pushPull", "Push/Pull", "arrow.up.and.down.square", [](SculptDoc& d) { return idle(d) && faces(d) == 1; },
        store(&ModelStore::pushPullSelection));
    add("model.sketchOnFace", "Sketch", "pencil.and.outline", [](SculptDoc& d) { return idle(d) && faces(d) > 0; },
        store(&ModelStore::startSketchOnSelectedFace));
    add("model.sketchOnPlane", "Sketch on Plane", "square.on.square.dashed",
        [](SculptDoc& d) { return idle(d) && planeItemSelected(d); }, store(&ModelStore::startSketchOnSelectedPlane));
    add("model.sketch", "Sketch", "pencil.and.outline", idle, [](SculptDoc& d) {
      startSketchHere(S(d));
      return std::string();
    });
    add("model.shell", "Shell", "square.dashed", hasBody, store(&ModelStore::shellSelection));
    add("model.fillet", "Fillet", "circle.bottomhalf.filled", [](SculptDoc& d) { return idle(d) && edges(d) > 0; },
        [](SculptDoc& d) {
          S(d).filletSelection(false);
          return std::string();
        });
    add("model.chamfer", "Chamfer", "triangle.bottomhalf.filled", [](SculptDoc& d) { return idle(d) && edges(d) > 0; },
        [](SculptDoc& d) {
          S(d).filletSelection(true);
          return std::string();
        });
    for (auto kind : allBooleanKinds()) {
      std::string raw = rawValue(kind);
      add("model." + raw, title(kind), symbol(kind), [](SculptDoc& d) { return idle(d) && distinctBodies(d) == 2; },
          [kind](SculptDoc& d) {
            S(d).boolean(kind);
            return std::string();
          });
    }
    add("model.move", "Move", "move.3d", hasBody, store(&ModelStore::moveSelection));
    add("model.mirror", "Mirror", "arrow.left.and.right.righttriangle.left.righttriangle.right", hasBody,
        store(&ModelStore::mirrorSelection));
    add("model.duplicate", "Duplicate", "plus.square.on.square", hasBody, store(&ModelStore::duplicateSelection));
    add("model.delete", "Delete Body", "trash", [](SculptDoc& d) { return S(d).selectedBody().has_value() && !S(d).draft(); },
        store(&ModelStore::deleteSelection));
    add("model.scale", "Scale", "arrow.up.left.and.arrow.down.right", hasBody, store(&ModelStore::scaleSelection));
    add("model.translate", "Translate", "arrow.right.square", hasBody, store(&ModelStore::translateSelection));
    add("model.linearPattern", "Pattern - Linear", "square.grid.3x1.below.line.grid.1x2", hasBody,
        store(&ModelStore::linearPatternSelection));
    add("model.circularPattern", "Pattern - Circular", "circle.grid.cross", hasBody,
        store(&ModelStore::circularPatternSelection));
    add("model.rotate", "Rotate Around Axis", "arrow.clockwise.circle", hasBody, store(&ModelStore::rotateSelection));
    add("model.split", "Split Body", "square.split.diagonal", hasBody, store(&ModelStore::splitSelection));
    add("model.project", "Project as Edge", "rectangle.portrait.on.rectangle.portrait.angled", hasBody,
        store(&ModelStore::projectSelection));
    add("model.scatter", "Scatter", "circle.dotted", [](SculptDoc& d) { return idle(d) && distinctBodies(d) == 2; },
        store(&ModelStore::scatterSelection));
    add("model.align", "Align", "align.horizontal.center", [](SculptDoc& d) { return idle(d) && distinctBodies(d) == 2; },
        store(&ModelStore::alignSelection));
    add("model.offsetFaces", "Offset Face", "arrow.up.square", [](SculptDoc& d) { return idle(d) && faces(d) > 0; },
        store(&ModelStore::offsetFacesSelection));
    add("model.draft", "Draft", "trapezoid.and.line.vertical", [](SculptDoc& d) { return idle(d) && faces(d) > 0; },
        store(&ModelStore::draftSelection));
    add("model.deleteFaces", "Delete Faces", "eraser", [](SculptDoc& d) { return idle(d) && faces(d) > 0; },
        store(&ModelStore::deleteFacesSelection));
    add("model.replaceFace", "Replace Face", "square.2.layers.3d.top.filled",
        [](SculptDoc& d) { return idle(d) && faces(d) == 2; }, store(&ModelStore::replaceFaceSelection));
    add("model.fill", "Fill", "square.fill.on.square", [](SculptDoc& d) { return idle(d) && edges(d) > 0; },
        store(&ModelStore::fillSelection));
    add("model.sweep", "Sweep", "point.bottomleft.forward.to.point.topright.scurvepath", idle,
        store(&ModelStore::sweepSelection));
    add("model.loft", "Loft", "cone", idle, store(&ModelStore::loftSelection));
    add("model.emboss", "Wrap & Emboss", "textformat", [](SculptDoc& d) { return idle(d) && !S(d).selectedSketchItems().empty(); },
        store(&ModelStore::embossSelection));
    add("model.offsetPlane", "Offset Plane", "square.stack", idle, store(&ModelStore::addOffsetPlane));
    add("model.angledPlane", "Angled Plane", "square.on.square.squareshape.controlhandles", idle,
        store(&ModelStore::addAngledPlane));
    add("model.midplane", "Midplane", "rectangle.split.2x1", [](SculptDoc& d) { return idle(d) && faces(d) == 2; },
        store(&ModelStore::addMidplane));
    add("model.planeThroughEdge", "Plane Through Edge at Angle", "square.on.square.dashed",
        [](SculptDoc& d) { return idle(d) && edges(d) > 0; }, store(&ModelStore::addPlaneThroughEdge));
    add("model.axis", "Axis", "line.diagonal", idle, store(&ModelStore::addAxis));
    add("model.axisThroughTwoPlanes", "Axis Through 2 Planes", "line.diagonal",
        [](SculptDoc& d) { return idle(d) && faces(d) + planeItems(d) >= 2; }, store(&ModelStore::addAxisThroughTwoPlanes));
    add("model.axisOfRoundFace", "Axis of Cylinder/Cone", "line.diagonal",
        [](SculptDoc& d) { return idle(d) && (faces(d) > 0 || edges(d) > 0); }, store(&ModelStore::addAxisOfRoundFace));
    add("model.axisAlongEdge", "Axis Along Edge", "line.diagonal", [](SculptDoc& d) { return idle(d) && edges(d) > 0; },
        store(&ModelStore::addAxisAlongEdge));
    add("model.rollingHills", "Rolling Hills", "mountain.2", idle, store(&ModelStore::insertRollingHills));
    add("model.addVariable", "Add Variable", "function", always, store(&ModelStore::addVariable));

    // Construct ▸ Plane/Axis point picks and Tools ▸ Measure.
    add("pick.planeThrough3Points", "Plane Through 3 Points", "scope", idle, pickRun(PointPickPurpose::planeThrough3Points));
    add("pick.planeOnCurveAtPoint", "Plane on Curve at Point", "scope", idle, pickRun(PointPickPurpose::planeOnCurveAtPoint));
    add("pick.planePerpendicularToEdgeAtPoint", "Plane Perpendicular to Edge at Point", "scope", idle,
        pickRun(PointPickPurpose::planePerpendicularToEdgeAtPoint));
    add("pick.planeParallelToFaceAtPoint", "Plane Parallel to Face at Point", "scope", idle,
        pickRun(PointPickPurpose::planeParallelToFaceAtPoint));
    add("pick.axisThrough2Points", "Axis Through 2 Points", "scope", idle, pickRun(PointPickPurpose::axisThrough2Points));
    add("pick.axisPerpendicularToFaceAtPoint", "Axis Perpendicular to Face at Point", "scope", idle,
        pickRun(PointPickPurpose::axisPerpendicularToFaceAtPoint));
    add("pick.measurePointToPoint", "Point-to-Point Distance", "ruler", idle, pickRun(PointPickPurpose::measurePointToPoint));
    add("pick.measureThreePointAngle", "3-Point Angle", "angle", idle, pickRun(PointPickPurpose::measureThreePointAngle));
    add("pick.cancel", "Cancel", "xmark", [](SculptDoc& d) { return d.view->pointPick.has_value(); }, [](SculptDoc& d) {
      d.view->cancelPointPick();
      return std::string();
    });
    add("measure.objects", "Object Measurements", "ruler", idle, [](SculptDoc& d) {
      d.view->setMeasuring(true);
      return std::string();
    });
    add("measure.clear", "Clear Measurements", "trash",
        [](SculptDoc& d) { return !d.view->pointMeasurements.empty() || !d.view->recentMeasurements.empty(); },
        [](SculptDoc& d) {
          d.view->pointMeasurements.clear();
          d.view->recentMeasurements.clear();
          d.view->stateChanged();
          return std::string();
        });

    // Sketch menu, sketch rail and sketch bar.
    add("sketch.setTool", "Sketch tool", "pencil", [](SculptDoc& d) { return !S(d).draft(); }, nullptr);
    add("sketch.project", "Project as Sketch", "rectangle.portrait.on.rectangle.portrait.angled", inSketch,
        store(&ModelStore::projectIntoSketch));
    add("sketch.offset", "Offset Edge", "square.on.square", sketchSelection, store(&ModelStore::offsetSketchSelection));
    add("sketch.fillet", "Fillet Sketch", "arrow.turn.up.right", sketchSelection, store(&ModelStore::filletSketchSelection));
    add("sketch.disconnect", "Disconnect", "link.badge.plus",
        [](SculptDoc& d) { return inSketch(d) && S(d).sketch()->selectedPoints().size() == 1; },
        store(&ModelStore::disconnectSketchSelection));
    add("sketch.toggleConstruction", "Make Construction", "square.dashed", sketchSelection,
        store(&ModelStore::toggleConstruction));
    add("sketch.horizontalOrVertical", "Horizontal/Vertical", "arrow.left.and.right",
        [](SculptDoc& d) {
          if (!inSketch(d)) return false;
          auto& s = *S(d).sketch();
          return !s.selectedLines().empty() && s.selectedPoints().empty() && s.selectedCircles().empty();
        },
        store(&ModelStore::constrainHorizontalOrVertical));
    add("sketch.deleteSelection", "Delete", "trash", sketchSelection, store(&ModelStore::deleteSketchSelection));
    add("sketch.text", "Text…", "textformat", inSketch, [](SculptDoc&) { return std::string("ui.sketchText"); });
    add("sketch.transform", "Scale / Pattern / Rotate", "arrow.up.left.and.arrow.down.right",
        [](SculptDoc& d) { return S(d).canTransformSketch(); }, nullptr);
    add("sketch.mirror", "Mirror Sketch", "arrow.left.and.right.righttriangle.left.righttriangle.right",
        [](SculptDoc& d) { return S(d).canTransformSketch(); }, store(&ModelStore::mirrorSketchSelection));
    add("sketch.finish", "Extrude", "square.stack.3d.up",
        [](SculptDoc& d) { return inSketch(d) && (S(d).sketch()->replacing || !S(d).sketch()->sketch.isEmpty()); },
        [](SculptDoc& d) {
          S(d).finishSketch(false);
          return std::string();
        });
    add("sketch.revolve", "Revolve", "arrow.triangle.2.circlepath",
        [](SculptDoc& d) { return inSketch(d) && !S(d).sketch()->replacing && !S(d).sketch()->sketch.isEmpty(); },
        [](SculptDoc& d) {
          S(d).finishSketch(true);
          return std::string();
        });
    add("sketch.finishAsItem", "Finish Sketch", "checkmark", inSketch, store(&ModelStore::finishSketchAsItem));
    add("sketch.cancel", "Cancel Sketch", "xmark", inSketch, store(&ModelStore::cancelSketch));
    add("sketch.escape", "Escape", "escape", inSketch, store(&ModelStore::sketchEscape));
    add("sketch.undo", "Undo", "arrow.uturn.backward", [](SculptDoc& d) { return S(d).canUndoSketch(); },
        store(&ModelStore::undoSketch));
    add("sketch.redo", "Redo", "arrow.uturn.forward", [](SculptDoc& d) { return S(d).canRedoSketch(); },
        store(&ModelStore::redoSketch));
    add("sketch.lookAt", "Look at Sketch Plane", "viewfinder", inSketch, [](SculptDoc& d) {
      d.view->faceSketchPlane();
      return std::string();
    });
    add("sketch.toggleConstructionMode", "Construction", "square.dashed", inSketch, [](SculptDoc& d) {
      S(d).setConstructionMode(!S(d).sketch()->constructionMode);
      return std::string();
    });

    // Draft (operation panel).
    add("draft.apply", "Apply", "checkmark", [](SculptDoc& d) { return S(d).draft() && !S(d).draftError(); },
        store(&ModelStore::applyDraft));
    add("draft.cancel", "Cancel", "xmark", [](SculptDoc& d) { return S(d).draft().has_value(); },
        store(&ModelStore::cancelDraft));
    add("draft.editSketch", "Edit Sketch", "pencil.and.outline",
        [](SculptDoc& d) { return S(d).draft() && S(d).draft()->replacing && S(d).draft()->feature.op.sketch(); },
        [](SculptDoc& d) {
          S(d).editSketch(*S(d).draft()->replacing);
          return std::string();
        });

    // View, Visualize and File.
    add("visualize.enter", "Visualize", "paintpalette", [](SculptDoc& d) { return !inSketch(d); }, [](SculptDoc& d) {
      d.view->setVisualizing(true);
      return std::string();
    });
    add("visualize.exit", "Exit Visualization", "xmark", [](SculptDoc& d) { return d.view->visualizing; },
        [](SculptDoc& d) {
          d.view->setVisualizing(false);
          return std::string();
        });
    add("view.goToModeling", "Go to Modeling", "cube",
        [](SculptDoc& d) { return d.view->visualizing || d.view->displayMode == vp::DisplayMode::Visualized; },
        [](SculptDoc& d) {
          d.view->setVisualizing(false);
          if (d.view->displayMode == vp::DisplayMode::Visualized) d.view->setDisplayMode(vp::DisplayMode::Shaded);
          return std::string();
        });
    add("view.lookAtSection", "2D Section View", "square.split.bottomrightquarter", always, [](SculptDoc& d) {
      d.view->lookAtSection();
      return std::string();
    });
    add("file.importFile", "Import File…", "doc.badge.plus", idle, [](SculptDoc&) { return std::string("ui.importFile"); });
    add("file.importProject", "Import Project…", "shippingbox", idle,
        [](SculptDoc&) { return std::string("ui.importProject"); });
    add("file.importMaterial", "Import Material…", "paintpalette",
        [](SculptDoc& d) { return idle(d) && !S(d).bodyIds().empty(); },
        [](SculptDoc&) { return std::string("ui.importMaterial"); });
    add("file.importDecal", "Import Image as Decal…", "photo", [](SculptDoc& d) { return idle(d) && faces(d) > 0; },
        [](SculptDoc&) { return std::string("ui.importDecal"); });
    add("file.importHeightMap", "From Height Map…", "photo", idle, [](SculptDoc&) { return std::string("ui.importHeightMap"); });
    return a;
  }();
  return list;
}

const Action* action(const std::string& id) {
  for (auto& a : actions())
    if (a.id == id) return &a;
  return nullptr;
}

}  // namespace sculpt::api
