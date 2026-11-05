// Port of ModelStore.swift and its extensions (+Construct, +Files,
// +SketchTools, +Tools, Terrain, Exporters, WebViewer, DrawingView): owns the
// geometry kernel, the feature history and everything the UI shows.
//
// SwiftUI's @Published is replaced by `onChanged` (called after any change the
// UI shows) and a monotonically increasing `revision()`. Properties the Swift
// UI assigned directly have setters; `setSketch` and `setSettings` keep the
// Swift `didSet` behaviour (sketch undo steps, regeneration on variable changes).
//
// Not thread-safe: call from one thread (the C API serialises calls).
#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "o3d/Kernel.hpp"
#include "sculpt/model/BodyMesh.hpp"
#include "sculpt/model/Drawing.hpp"
#include "sculpt/model/Exporters.hpp"
#include "sculpt/model/Feature.hpp"
#include "sculpt/model/PointPick.hpp"
#include "sculpt/model/ProjectSettings.hpp"
#include "sculpt/model/SketchSession.hpp"

namespace sculpt {

/// Sketch ▸ Scale / Pattern / Rotate (ModelStore+SketchTools.swift).
enum class SketchTransformKind { scale, linearPattern, circularPattern, rotate };
std::string rawValue(SketchTransformKind k);
std::string title(SketchTransformKind k);

struct SketchTransformParams {
  double factor = 2.0;
  double angle = 90.0;
  int count = 3;
  double spacing = 20.0;
  double direction = 0.0;  // degrees from the sketch's X axis
  double totalAngle = 360.0;
  bool keepOriginal = false;
};

/// One element of a glyph outline for Sketch ▸ Text. The host lays out the
/// text with its font engine (CoreText, DirectWrite, FreeType…) and passes
/// the glyph paths already positioned, in millimetres.
struct PathElement {
  enum class Kind { moveTo, lineTo, quadTo, cubicTo, close } kind = Kind::moveTo;
  Vec2d p[3];  // moveTo/lineTo: p[0]; quadTo: control p[0], end p[1]; cubicTo: p[0], p[1], end p[2]
};

/// A row of the Measure panel (Panels.swift MeasurePanel).
struct MeasureRow {
  std::string label;
  double value;      // in display units
  std::string unit;  // "mm", "°", "mm²", …
  std::string text() const;
};

class ModelStore {
 public:
  explicit ModelStore(std::vector<Feature> features = {}, ProjectSettings settings = ProjectSettings());
  ModelStore(const ModelStore&) = delete;
  ModelStore& operator=(const ModelStore&) = delete;

  // MARK: Change notification
  /// Called after anything the UI shows changes (any thread that made the change).
  std::function<void()> onChanged;
  /// Increases with every change notification.
  uint64_t revision() const { return revision_; }
  /// Called with the new history after commit/undo/redo (document dirty tracking).
  std::function<void(const std::vector<Feature>&)> onFeaturesChanged;
  std::function<void(const ProjectSettings&)> onSettingsChanged;
  /// Swift rebuilt a slider-driven draft once per run-loop turn. When set, the
  /// store hands the rebuild to this scheduler (the host runs it on its UI
  /// thread later); when unset, updateDraft rebuilds immediately.
  std::function<void(std::function<void()>)> scheduleRebuild;

  // MARK: State (ModelStore.swift)
  const std::vector<Feature>& features() const { return features_; }
  const std::map<int, std::shared_ptr<const BodyMesh>>& meshes() const { return meshes_; }
  const std::map<Uuid, std::string>& errors() const { return errors_; }
  int geometryVersion() const { return geometryVersion_; }

  const std::optional<Draft>& draft() const { return draft_; }
  void setDraft(std::optional<Draft> d);
  const std::vector<Pick>& selection() const { return selection_; }
  void setSelection(std::vector<Pick> s);
  /// The face or edge under the pointer. Not a change notification (only the viewport draws it).
  const std::optional<Pick>& hover() const { return hover_; }
  void setHover(std::optional<Pick> p) { hover_ = p; }

  const std::optional<SketchSession>& sketch() const { return sketch_; }
  /// Swift's `sketch` didSet: every geometry change is one undo step, except
  /// the steps of a drag; ending the sketch clears its undo stacks.
  void setSketch(std::optional<SketchSession> s);
  /// Applies `change` to the open sketch session (no-op without one).
  void updateSketch(const std::function<void(SketchSession&)>& change);
  const std::vector<Sketch>& sketchUndo() const { return sketchUndo_; }
  const std::vector<Sketch>& sketchRedo() const { return sketchRedo_; }

  const std::optional<std::string>& toast() const { return toast_; }
  void setToast(std::optional<std::string> t);
  const std::string& documentName() const { return documentName_; }
  void setDocumentName(std::string n);
  const std::string& documentPath() const { return documentPath_; }
  void setDocumentPath(std::string p);
  /// Hidden bodies from before Item ▸ Isolate, restored by Exit Isolation.
  const std::optional<std::set<int>>& isolationRestore() const { return isolationRestore_; }
  void setIsolationRestore(std::optional<std::set<int>> r);
  /// Sketches, planes, axes and images in the history, for display and picking.
  const std::vector<ReferenceItem>& referenceItems() const { return referenceItems_; }
  /// Selected sketch and plane items, in the order they were picked.
  const std::vector<Uuid>& itemSelection() const { return itemSelection_; }
  void setItemSelection(std::vector<Uuid> s);
  const ProjectSettings& settings() const { return settings_; }
  /// Swift's `settings` didSet: variables changing rebuilds, anything else bumps geometryVersion.
  void setSettings(ProjectSettings s);
  void updateSettings(const std::function<void(ProjectSettings&)>& change);
  /// Scatter copies from the last rebuild: copy id → (source, first copy).
  const std::map<int, std::pair<int, int>>& scatterOrigins() const { return scatterOrigins_; }

  o3d::Kernel& kernel() { return *kernel_; }
  const o3d::Kernel& kernel() const { return *kernel_; }

  /// Replaces the whole model (no undo step), e.g. when a document finishes loading.
  void reload(std::vector<Feature> features, ProjectSettings settings);

  bool canUndo() const { return !undoStack_.empty(); }
  bool canRedo() const { return !redoStack_.empty(); }
  std::vector<int> bodyIds() const;
  std::optional<std::string> draftError() const;

  // MARK: History
  void commit(std::vector<Feature> newFeatures);
  void undo();
  void redo();
  void toggleSuppressed(const Uuid& id);
  void deleteFeature(const Uuid& id);
  /// Replays the history (plus the draft) from the longest unchanged checkpoint.
  void regenerate();

  // MARK: Drafts
  void begin(Operation op, std::optional<SketchSession> sketch = std::nullopt, std::optional<Uuid> replacing = std::nullopt);
  void edit(const Uuid& id);
  void updateDraft(Operation op);
  /// Runs a rebuild deferred by updateDraft now (applyDraft does this itself).
  void flushPendingRebuild();
  void applyDraft();
  void cancelDraft();
  /// Body appearance, with scatter copies shown like their source while they are a preview.
  std::map<int, Appearance> displayAppearance() const;
  /// Trial-runs `op` and commits it only if it succeeds (else toasts the error).
  void commitImmediately(Operation op);

  // MARK: Selection
  std::vector<int> selectedBodies() const;
  std::vector<std::pair<int, int>> selectedFaces() const;
  std::vector<std::pair<int, int>> selectedEdges() const;
  void select(std::optional<Pick> pick, bool additive);
  std::optional<o3d::FaceInfo> faceInfo(int body, int face) const;
  std::optional<o3d::EdgeInfo> edgeInfo(int body, int edge) const;
  /// Volume, area and bounds, cached by shape key.
  std::optional<o3d::Properties> properties(int body);

  // MARK: Commands
  void addPrimitive(PrimitiveKind kind);
  void startSketch(const SketchPlane& plane, std::optional<int> target = std::nullopt);
  void startSketchOnSelectedFace();
  /// Reopens the sketch of an extrude, revolve or sketch feature for editing.
  void editSketch(const Uuid& id);
  void finishSketch(bool revolve);
  /// Keeps the sketch as an item of its own, for sweeps, lofts and later use.
  void finishSketchAsItem();
  void cancelSketch();
  void setSketchTool(SketchTool tool);
  /// Escape ends the line being drawn, then clears the selection, then leaves the sketch.
  void sketchEscape();
  void beginSketchDrag();
  void endSketchDrag();
  bool canUndoSketch() const { return sketch_.has_value() && !sketchUndo_.empty(); }
  bool canRedoSketch() const { return sketch_.has_value() && !sketchRedo_.empty(); }
  void undoSketch();
  void redoSketch();

  // Sketch bar settings (Swift wrote `store.sketch?.x = …`).
  void setPolygonSides(int n);
  void setPolygonCircumscribed(bool on);
  void setConstructionMode(bool on);
  void setOffsetDistance(double d);
  void setFilletRadius(double r);
  void setSketchSelection(std::vector<SketchRef> refs);
  /// Selects only this constraint, or deselects it (SketchBar's annotation buttons).
  void toggleSketchConstraintSelection(int index);

  // Sketch constraints
  void constrainSelection(SketchConstraint::Kind kind);
  void setDimension(int index, double value);
  void deleteSketchSelection();
  /// Horizontal or vertical for each selected line, whichever it is closer to.
  void constrainHorizontalOrVertical();
  void offsetSketchSelection();
  void filletSketchSelection();
  void disconnectSketchSelection();
  void toggleConstruction();

  void pushPullSelection();
  void filletSelection(bool chamfer);
  void shellSelection();
  void moveSelection();
  void duplicateSelection();
  void deleteSelection();
  void boolean(BooleanKind kind);

  // Exchange
  /// Imports a STEP, IGES, STL or BREP file as `imported` features.
  void importFile(const std::string& path);
  /// The selection's bodies (or all) as one exchange file; nothing (with a toast) on failure.
  std::optional<std::vector<uint8_t>> exportExchange(ExchangeFormat format);

  // MARK: ModelStore+Construct.swift
  void addPlaneThrough(const std::vector<PickedPoint>& points);
  /// A plane through a point on an edge, normal to the edge there.
  void addPlaneNormalToEdge(const PickedPoint& p);
  /// A plane parallel to the picked face, through the picked point.
  void addPlaneParallelToFace(const PickedPoint& p);
  /// A plane containing the selected straight edge, turned about it (angle editable).
  void addPlaneThroughEdge();
  void addAxisThrough(const std::vector<PickedPoint>& points);
  /// The line where two selected planes or flat faces meet.
  void addAxisThroughTwoPlanes();
  void addAxisOfRoundFace();
  void addAxisAlongEdge();
  void addAxisNormalToFace(const PickedPoint& p);
  /// Projects the selected sketch onto the selected (or nearest) body and raises it.
  void embossSelection();
  /// Bodies of everything selected: whole bodies, and the bodies of faces and edges.
  std::vector<int> touchedBodies() const;
  void toggleVisibilityOfSelection();
  void isolateSelection();
  bool isIsolating() const { return isolationRestore_.has_value(); }
  void exitIsolation();
  void showAllBodies();
  void invertBodyVisibility();
  /// Runs what a finished point pick was for. Measurements come back for the
  /// viewport to pin (with `recentText` for its Recent list); constructions return nothing.
  struct PointPickResult {
    PointMeasurement measurement;
    std::string recentText;
  };
  std::optional<PointPickResult> completePointPick(PointPickPurpose purpose, const std::vector<PickedPoint>& points,
                                                   bool measureDeltas = false);
  /// Starting a point pick clears the selection (ViewportController.beginPointPick).
  PointPickSession beginPointPick(PointPickPurpose purpose);

  // MARK: ModelStore+Files.swift
  /// Applies a material file (JSON: name, color "#rrggbb" or [r, g, b], metallic, roughness, opacity)
  /// to the selected bodies or all of them. `fileName` is used when the JSON has no name.
  void importMaterial(const std::string& jsonText, const std::string& fileName);
  static std::optional<Vec3f> color(const std::string& hex);
  /// Sticks an image (PNG/JPEG bytes) on the selected flat face, as an opaque decal.
  void insertDecal(const std::vector<uint8_t>& imageBytes, const std::string& name);

  // MARK: ModelStore+SketchTools.swift
  bool canTransformSketch() const;
  void transformSketchSelection(SketchTransformKind kind, const SketchTransformParams& p);
  /// Mirrors the selection across the last selected line, or the sketch's vertical axis.
  void mirrorSketchSelection();
  /// Adds glyph outlines as closed lines and splines (Sketch ▸ Text), selecting them.
  void addSketchOutlines(const std::vector<PathElement>& path);

  // MARK: ModelStore+Tools.swift
  std::map<int, std::shared_ptr<const BodyMesh>> visibleMeshes() const;
  std::vector<ReferenceItem> selectedItems() const;
  /// The body the selection is about (a body, or the body of a face or edge).
  std::optional<int> selectedBody() const;
  void toggleItemSelection(const Uuid& id);
  void toggleHiddenBody(int body);
  void toggleHiddenItem(const Uuid& id);
  void renameBody(int body, const std::string& name);
  void renameItem(const Uuid& id, const std::string& name);
  void setAppearance(const Appearance& appearance, const std::vector<int>& bodies);
  /// Bodies that Visualize acts on: the selection, or everything.
  std::vector<int> appearanceTargets() const;
  /// Distinct appearances on the model's bodies with the bodies using each (VisualizationPanel).
  std::vector<std::pair<Appearance, std::vector<int>>> usedMaterials() const;
  void editItem(const Uuid& id);
  /// Extrudes or revolves a copy of a standalone sketch, then hides the sketch.
  void useSketchItem(const Uuid& id, bool revolve);
  std::vector<Uuid> selectedSketchItems() const;
  void sweepSelection();
  void loftSelection();
  void addOffsetPlane();
  void addAngledPlane();
  void addMidplane();
  void startSketchOnSelectedPlane();
  void scaleSelection();
  void scatterSelection();
  void linearPatternSelection();
  void circularPatternSelection();
  void mirrorSelection();
  void splitSelection();
  void deleteFacesSelection();
  void draftSelection();
  void offsetFacesSelection();
  /// One body, face or edge, or the relation between two.
  std::optional<o3d::Measurement> measureSelection() const;
  /// The Measure panel's rows for the selection, in the project's units.
  std::vector<MeasureRow> measureRows();
  std::optional<std::pair<Vec3d, Vec3d>> selectedAxis() const;
  /// An axis through a round face, along a straight edge, or the world Z axis.
  void addAxis();
  void translateSelection();
  void rotateSelection();
  void alignSelection();
  void replaceFaceSelection();
  void fillSelection();
  void projectSelection();
  /// In a sketch: brings the visible (or selected) bodies' edges onto the sketch plane.
  void projectIntoSketch();
  /// Places an image (PNG/JPEG bytes) on the selected plane or flat face (or Top).
  void insertImage(const std::vector<uint8_t>& imageBytes, const std::string& name);
  /// Adds the bodies of another Sculpt3D project (its file bytes) as imported bodies.
  void insertProject(const std::string& documentBytes, const std::string& name);
  void addVariable();
  std::map<std::string, double> variableValues() const;
  /// Binds a parameter of the draft to an expression, or clears the binding.
  void bindDraft(int parameter, std::optional<std::string> expression);

  // MARK: Terrain.swift
  void insertRollingHills();
  /// Insert ▸ Terrain ▸ From Height Map: white is high, black is low.
  void insertTerrain(const std::vector<uint8_t>& imageBytes, const std::string& name);

  // MARK: Exporters.swift
  /// Bodies an export covers: the selection's, or every visible body.
  std::vector<int> exportBodies() const;
  /// The sketches a 2D export uses: the open one, the selected ones, or every visible sketch.
  std::vector<Sketch> exportSketches() const;
  /// Whether File ▸ Export to can make this format now (ModelCommands.swift `exportable`).
  bool canExport(ExportKind kind) const;
  /// The file for one format, or nothing (with a toast saying why).
  std::optional<std::vector<uint8_t>> exportData(ExportKind kind, const std::optional<DrawingSettings>& drawing = std::nullopt);
  std::optional<std::vector<uint8_t>> threeMF();
  std::optional<std::vector<uint8_t>> obj();
  std::optional<std::vector<uint8_t>> glb();
  /// Writes the file for `kind` into `folder`, named after the document; returns its path.
  std::optional<std::string> write(ExportKind kind, const std::string& folder, const std::string& name = "");
  /// File ▸ Quick Export to Favorite Formats: writes each format; toasts the result.
  std::vector<std::string> quickExport(const std::vector<ExportKind>& formats, const std::string& folder);

  // MARK: WebViewer.swift
  /// A self-contained HTML page showing the visible bodies in 3D (three.js).
  std::optional<std::string> webPage(const std::string& title);

  // MARK: DrawingView.swift
  /// Hidden-line projections of the visible bodies.
  std::vector<DrawingProjection> drawingProjections() const;
  std::optional<DrawingSheet> drawingSheet(const std::optional<DrawingSettings>& settings);

 private:
  struct Checkpoint {
    Feature feature;
    std::shared_ptr<const o3d::KernelState> state;
    std::map<Uuid, Sketch> sketches;
    std::vector<ReferenceItem> items;
    std::map<Uuid, std::string> errors;
    std::map<int, std::pair<int, int>> origins;
  };

  void changed();
  std::optional<std::string> apply(const Operation& op);
  void endChain(SketchSession& s);
  void restore(SketchSession& s, const Sketch& geometry);
  std::pair<std::vector<SketchRef>, std::optional<Vec2d>> sketchTargets(const SketchSession& s) const;
  int addOutline(const std::vector<PathElement>& path, Sketch& sketch) const;
  SketchPlane planeBase() const;
  int count(bool (*kind)(const Operation&)) const;
  Vec3d centerOf(int body) const;
  std::pair<std::vector<Vec3f>, std::vector<std::array<uint32_t, 3>>> welded(const BodyMesh& m) const;
  std::optional<std::vector<std::shared_ptr<const BodyMesh>>> meshesFor(const std::vector<int>& bodies);

  std::unique_ptr<o3d::Kernel> kernel_;
  std::vector<Feature> features_;
  std::map<int, std::shared_ptr<const BodyMesh>> meshes_;
  std::map<Uuid, std::string> errors_;
  int geometryVersion_ = 0;
  std::optional<Draft> draft_;
  std::vector<Pick> selection_;
  std::optional<Pick> hover_;
  std::optional<SketchSession> sketch_;
  std::vector<Sketch> sketchUndo_, sketchRedo_;
  bool restoringSketch_ = false;
  std::optional<std::string> toast_;
  std::string documentName_ = "Untitled";
  std::string documentPath_;
  std::optional<std::set<int>> isolationRestore_;
  std::vector<ReferenceItem> referenceItems_;
  std::vector<Uuid> itemSelection_;
  ProjectSettings settings_;
  std::map<Uuid, Sketch> replaySketches_;
  std::vector<Checkpoint> checkpoints_;
  std::map<int, uint64_t> meshKeys_;
  std::shared_ptr<const o3d::KernelState> meshState_;
  std::map<int, std::pair<int, int>> scatterOrigins_;
  std::map<int, std::pair<uint64_t, o3d::Properties>> propertiesCache_;
  std::vector<std::vector<Feature>> undoStack_, redoStack_;
  bool draftRebuildPending_ = false;
  uint64_t revision_ = 0;
  /// Lets deferred rebuilds notice the store is gone.
  std::shared_ptr<char> alive_ = std::make_shared<char>(0);
};

}  // namespace sculpt
