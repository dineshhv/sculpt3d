// C++ port of App/Sources/Viewport/ViewportController.swift and its
// extensions (+Sketch, +Points, +Reference, +Keys), plus the gesture
// semantics of ViewportView.swift (touch / Pencil on iOS, mouse / trackpad on
// the Mac) recognised from raw pointer events.
//
// The controller owns the camera and the window-level view state Swift kept
// outside the store (display mode, section view, measuring, point picks…),
// builds the renderer's Scene from the store, and turns pointer input into
// picks, selection and sketch edits. It never touches GL: rendering is the
// C API's job, so the whole controller works headless (tests, SCULPT_WITH_GL=0).
//
// Not thread-safe; the C API serialises every call with the document lock.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "sculpt/model/ModelStore.hpp"
#include "sculpt/model/PointPick.hpp"
#include "sculpt/model/SketchInput.hpp"
#include "viewport/Camera.hpp"
#include "viewport/Picking.hpp"
#include "viewport/Scene.hpp"

namespace sculpt::api {

namespace vp = sculpt::viewport;

enum class PointerKind { Touch = 0, Mouse = 1, Pen = 2 };

/// Display modes in View-menu order, with the Swift raw values, titles, symbols and ⌥ keys.
struct DisplayModeInfo {
  vp::DisplayMode mode;
  const char* raw;
  const char* title;
  const char* symbol;
  char key;
};
const std::vector<DisplayModeInfo>& displayModes();  // shaded, wireframe, xray, visualized, zebra, curvature
const DisplayModeInfo& info(vp::DisplayMode m);
std::optional<vp::DisplayMode> displayMode(const std::string& raw);

class ViewportController {
 public:
  explicit ViewportController(ModelStore& store);

  ModelStore& store;
  vp::Camera camera;

  // MARK: View state (Swift @Published vars). Change them through the setters
  // below so the revisions move; plain reads are fine.
  std::optional<double> fixedGrid;
  bool showConstraints = true;
  bool measureDeltas = false;
  std::vector<std::string> pinnedMeasurements;
  vp::DisplayMode displayMode = vp::DisplayMode::Shaded;
  bool showEdges = true;
  bool showHiddenEdges = false;
  bool showSketches = true;
  bool showDecals = true;
  bool showPinnedMeasurements = true;
  bool showWorldAxes = true;
  bool gridAtModelBottom = false;
  std::optional<PointPickSession> pointPick;
  std::vector<PointMeasurement> pointMeasurements;
  std::vector<std::string> recentMeasurements;
  bool sectionEnabled = false;
  int sectionAxis = 2;
  double sectionOffset = 0;
  bool sectionFlipped = false;
  bool measuring = false;
  /// Visualize mode (Swift UIState.visualizing): it changes what the scene draws.
  bool visualizing = false;
  /// A UI action a key shortcut or tool produced that only the front end can do
  /// ("ui.importFile", "view.screenshot", "export.step"…), with a serial number.
  std::string uiRequest;
  uint64_t uiRequestSerial = 0;

  // MARK: Revisions
  /// Bumps when view state that sculpt_doc_state shows changes.
  uint64_t stateRevision() const { return stateRevision_; }
  /// Bumps when anything drawn or annotated changes, camera included.
  uint64_t viewRevision() const { return viewRevision_; }
  bool needsRedraw() const { return needsRedraw_; }
  void clearNeedsRedraw() { needsRedraw_ = false; }
  /// View state changed (shown in the state JSON and drawn).
  void stateChanged();
  /// Something drawn changed (camera, hover…), not the state JSON.
  void redraw();

  // MARK: Size
  void setSize(float widthPoints, float heightPoints);
  vp::Size2 size() const { return size_; }

  /// Call after the store changed (the Swift `storeChanged` sink): camera
  /// settings, first fit when a document opens, facing a newly opened sketch.
  void storeChanged();

  // MARK: View-state setters with the Swift didSet behaviour
  void setSectionEnabled(bool on);
  void setSectionAxis(int axis);
  void setSectionOffset(double offset);
  void setSectionFlipped(bool on);
  /// Extent of the visible bodies along the section axis.
  std::pair<double, double> sectionRange() const;
  std::optional<vp::Vec4f> sectionPlane() const;
  void setMeasuring(bool on);
  void setVisualizing(bool on);
  void setDisplayMode(vp::DisplayMode m);
  void requestUi(const std::string& action);
  /// Runs a ToolCatalog UI action the core can do itself ("view.fit",
  /// "view.top", "view.toggleSection", "view.mode.zebra"…). False for the
  /// front end's actions ("ui.*", "export.*", "view.screenshot").
  bool performAction(const std::string& action);

  // MARK: Camera
  void orbit(float dx, float dy);
  void pan(float dx, float dy);
  /// Swift `zoom(_:at:)`: factor < 1 moves closer, towards the surface under `at`.
  void zoom(float factor, std::optional<vp::Vec2f> at = std::nullopt);
  void look(vp::Camera::Preset preset);
  void fitAll();
  void faceSketchPlane();
  void zoomToSelection();
  void snapToNearestOrtho();
  void rotateView(float yawSteps, float pitchSteps);
  void lookAtSection();
  /// ViewCube face click.
  void setOrientation(float yaw, float pitch);

  // MARK: Picking
  const vp::MeshMap& visibleMeshes();
  std::optional<vp::SurfaceHit> surfaceHit(vp::Vec2f p);
  std::optional<Pick> pick(vp::Vec2f p);
  /// Unsnapped point on the sketch plane under the pointer.
  std::optional<Vec2d> planePoint(vp::Vec2f p) const;
  /// The snapped point on the model under the pointer: corners, edges, faces.
  std::optional<PickedPoint> pickPoint(vp::Vec2f p);
  double gridStep() const;

  // MARK: Input (Swift ViewportController entry points)
  void tap(vp::Vec2f p, bool additive);
  void doubleTap(vp::Vec2f p, bool additive);
  void hover(std::optional<vp::Vec2f> p);
  bool dragDraws(vp::Vec2f p);
  void drawBegan(vp::Vec2f p);
  void drawChanged(vp::Vec2f p);
  void drawEnded(vp::Vec2f p);

  /// One raw pointer event (sculpt_view_pointer); recognises taps, double
  /// taps and drags as ViewportView.swift's recognisers do.
  void pointer(int id, PointerKind kind, int button, int phase, vp::Vec2f p, int modifiers);
  /// Clock for tap timing (seconds); tests may replace it.
  std::function<double()> clock;

  /// sculpt_view_key: viewport shortcuts. Returns true when handled.
  bool key(const std::string& key, int modifiers);

  // MARK: Point picks and measurements
  void beginPointPick(PointPickPurpose purpose);
  void cancelPointPick();
  void remember(const std::string& text);
  /// The measurement of the selection, cached until it or the geometry changes.
  const std::optional<o3d::Measurement>& measurement();

  // MARK: Frame
  /// The renderer's input for this frame (cached while nothing changed).
  const vp::Scene& scene();
  /// Screen annotations (sculpt_view_annotations): constraint glyphs, dimension
  /// labels, snapping hint, point-measurement labels, all in view points.
  nlohmann::json annotations();
  /// Grid size label of the right-hand controls ("10\nmm").
  std::string gridLabel() const;

 private:
  struct Gesture {
    int id = 0;
    PointerKind kind = PointerKind::Mouse;
    int button = 0;
    vp::Vec2f down, last;
    double downTime = 0;
    bool dragging = false;
    bool drawing = false;
    bool cancelled = false;
  };

  float pickTolerance() const;   // edges, in points
  double pickRadiusPoints() const;  // sketch hits, in points
  float pointTolerance() const;  // point picks, in points
  void prepareSketchInput();
  void sketchTap(vp::Vec2f p, bool additive);
  bool pointPickTap(vp::Vec2f p);
  bool pointPickHover(std::optional<vp::Vec2f> p);
  void cameraChanged();
  void endGesture(bool completed);

  // Overlays (ViewportOverlay.cpp)
  vp::LineBatch gridBatch();
  vp::LineBatch overlay();
  void referenceOverlay(vp::LineBatch& batch);
  void measureOverlay(vp::LineBatch& batch);
  void pointOverlay(vp::LineBatch& batch);
  void sketchOverlay(vp::LineBatch& batch);

  vp::Size2 size_{1, 1};
  uint64_t stateRevision_ = 1, viewRevision_ = 1;
  bool needsRedraw_ = true;
  bool hasFitted_ = false;
  bool sized_ = false;
  bool fittedUnsized_ = false;
  bool wasSketching_ = false;
  std::vector<Pick> bodiesBeforeTap_;
  std::unique_ptr<SketchInput> sketchInput_;
  PointerKind lastKind_ = PointerKind::Mouse;

  // Raw pointer recognition.
  std::optional<Gesture> gesture_;
  std::map<int, vp::Vec2f> extraTouches_;
  double lastTapTime_ = -1;
  vp::Vec2f lastTapPoint_;
  bool lastTapWasDouble_ = false;

  // Caches.
  uint64_t meshesRevision_ = ~0ull;
  vp::MeshMap visible_;
  std::map<int, std::pair<std::shared_ptr<const BodyMesh>, std::shared_ptr<const vp::BodyMesh>>> converted_;
  int64_t sceneGeometryVersion_ = 0;
  struct ImageCache {
    size_t size = 0;
    std::shared_ptr<const std::vector<uint8_t>> bytes;
    uint64_t key = 0;
  };
  std::map<Uuid, ImageCache> images_;
  uint64_t sceneKey_[3] = {~0ull, ~0ull, ~0ull};
  vp::Scene scene_;
  struct MeasureCache {
    std::vector<Pick> selection;
    int version = -1;
    std::optional<o3d::Measurement> result;
  };
  std::optional<MeasureCache> measureCache_;
};

}  // namespace sculpt::api
