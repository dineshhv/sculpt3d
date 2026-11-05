# Sculpt3D shared model (C++)

Portable C++17 port of `App/Sources/Model/*.swift` plus the non-view logic of
`App/Sources/UI` and the model side of `App/Sources/Viewport/ViewportController+*`.
Namespace `sculpt`, headers in `AppCore/include/sculpt/model/`, sources in
`AppCore/src/model/` (built into `sculpt_core`). It uses `o3d::Kernel` and
`o3d::sketch::solve` from `Core/` directly.

Build and test on a Mac:

```
cmake -S AppCore -B build/appcore-mac -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/appcore-mac && ./build/appcore-mac/model_tests
```

`AppCore/tests/fixtures/golden.sculpt3d` and `empty.sculpt3d` were written by the
Apple app's own Swift code (`AppCore/tests/golden/make_golden.sh` compiles
`App/Sources/Model` headlessly, like `scripts/test_sketch_flow.sh`, and runs
`tests/golden/main.swift`). The test decodes and re-encodes them byte for byte.

## Files

| Header | Swift source | Contents |
| --- | --- | --- |
| `Math.hpp` | simd | `Vec2d`, `Vec3d`, `Vec3f`, `dot`, `cross`, `length`, `normalize`, `distance`, `vmin/vmax`, `toD/toF`, `toO3d/fromO3d` |
| `Uuid.hpp` | `UUID` | `Uuid::random()` (v4), `parse`, `string()` (upper case) |
| `Equatable.hpp` | synthesized `Equatable` | `SCULPT_FIELDS(...)` gives `==`/`!=` |
| `Sketch.hpp` | Sketch, SketchOps, SketchTransforms | `SketchPlane`, `SketchLine`, `SketchCircle`, `SketchSpline`, `SketchEllipse`, `SketchConstraint` (+ `rawValue/arguments/isDimension/title/symbol/glyph`), `SketchRef`, `SketchSnap`, `SketchStatus`, `SketchTransform`, `Sketch` |
| `SketchSession.hpp` | ModelStore (SketchTool, SketchSession, Draft), SketchEditing, SketchBar | `SketchTool` (+ `title/symbol/drags/key`), `SketchSession`, `Draft` |
| `Feature.hpp` | Feature | `PrimitiveKind`, `CombineMode`, `BooleanKind`, `ScatterSettings`, `op::*` (one struct per `Operation` case), `Operation`, `Feature`, `worldAxes/axisIndex/axisPlane` |
| `Expression.hpp` | Expression | `ExpressionEvaluator` |
| `ProjectSettings.hpp` | ProjectSettings | `LengthUnit` (+ `symbol/title/scale/isImperial`), `Variable`, `SnapSettings`, `ProjectSettings`, `ReferenceItem` |
| `Visualization.hpp` | Visualization (+ `Appearance`) | `Appearance`, `MaterialPreset` (library, `label`, `search`), `EnvironmentSettings` (+ `backgrounds()`), `CameraSettings`, `ReferenceImage` |
| `Terrain.hpp` | Terrain | `TerrainNoise`, `TerrainSettings`, `Terrain::noiseField/field/add/terrace/carve/distance/point/smoothstep`, `Hill`, `Channel` |
| `BodyMesh.hpp` | BodyMesh | `BodyMesh` (shared, immutable), `Pick` |
| `DisplayFormat.hpp` | DisplayFormat, `formatted()` | `FootInchFormat`, `DegreeFormat`, `DisplayFormat`, `formatted`, `formatLength`, `architectural`, `formatAngle`, `formatArea`, `formatVolume` |
| `Codec.hpp` | Codable | `JValue` (Swift-exact JSON writer), `swiftNumber`, `swiftString`, `base64Encode/Decode`, `encode(...)`/`decode(json, ...)` for every Codable type, `CodingError` |
| `Sculpt3DFile.hpp` | Sculpt3DFile | `Sculpt3DFile::decode(bytes)`, `encoded()` |
| `Exporters.hpp` | Exporters, Sculpt3DFile (`ExchangeFormat`) | `ExchangeFormat`, `ExportKind` (+ `title/detail/fileExtension/mimeType/isAvailable/...`), `ZipWriter`, `SketchVector::curves/dxf/svg`, `xmlEscape` |
| `Drawing.hpp` | UI/DrawingView (data) | `DrawingProjection`, `PaperSize`, `DrawingSettings`, `DrawingSheet` (`layout`, `dimension`, `niceScale`, `scaleText`, `svg`, `pdf`) |
| `Image.hpp` | ImageIO / `PNGFile.encode` | `decodeImage` (stb_image), `encodePNG` (stb_image_write), `RGBAImage::gray` |
| `PointPick.hpp` | Viewport/ViewportController+Points (model side) | `PickedPoint`, `PointPickPurpose`, `PointPickSession` (`make`, `rejection`), `PointMeasurement` |
| `SketchInput.hpp` | Viewport/ViewportController+Sketch (model side) | `gridStep()`, `polygonCorner()`, `SketchAnnotation`, `SketchInput` (snap, hit, hover, tap, drag) |
| `ToolCatalog.hpp` | UI/Commands (ToolCatalog), Viewport/+Keys | `ToolCategory`, `ToolCommand`, `ToolCatalog::all/commands/command/search/run/shortcut/keyShortcuts/handleKey` |
| `ModelStore.hpp` | ModelStore + all extensions | `ModelStore` (below), `SketchTransformKind/Params`, `PathElement`, `MeasureRow` |

## File format

`Sculpt3DFile::encoded()` reproduces Swift `JSONEncoder` with
`[.prettyPrinted, .sortedKeys]` exactly: 2-space indent, `"key" : value`, keys
sorted by UTF-8 bytes, empty containers as `[\n\n  ]`, `/` escaped as `\/`,
doubles and floats in Swift's shortest form (`40`, `0.72`, `1e-05`, `1e+16`,
decimal up to 2^53 / 2^24), no trailing newline. Enum cases with associated
values are `{"case": {labels...}}`, nil optionals are omitted, `Data` is base64,
`Set` is an array, `[Int: T]` an object with string keys, UUIDs upper case.

Every custom `init(from:)` is honoured: Sketch legacy `shapes`
(`rectangle {_0,_1}`, `circle {center,radius}`, `polygon {_0}`) and missing
arrays; SketchSpline optional flags; Appearance missing keys and legacy
`material`; ProjectSettings missing keys; EnvironmentSettings partial decode;
ReferenceImage optional `opacity`/`decal`. Synthesized types require their keys,
as Swift does (a missing key throws `CodingError`). The top level requires
`version` and `features`; `settings` is optional.

The only intended byte difference from Swift: `Set`s (`hiddenBodies`,
`hiddenItems`) are written sorted, where Swift writes them in hash order (which
varies run to run). Decoding is unaffected.

## ModelStore API

Change notification: `onChanged` (called after every change the UI shows) and
`revision()` (monotonic). `onFeaturesChanged(features)` fires after
commit/undo/redo (dirty tracking); `onSettingsChanged(settings)` after a settings
change. Set `scheduleRebuild` to defer slider-driven draft rebuilds to the host's
UI loop (Swift used `DispatchQueue.main.async`); without it `updateDraft`
rebuilds immediately. `flushPendingRebuild()` runs a deferred rebuild now. The
store is single-threaded (the C API should hold its document mutex).

### State (ModelStore.swift)

| Swift | C++ |
| --- | --- |
| `features` | `features()` |
| `meshes` | `meshes()` → `std::map<int, std::shared_ptr<const BodyMesh>>` (reused across rebuilds while `shapeKey` is unchanged) |
| `errors` | `errors()` → `std::map<Uuid, std::string>` |
| `geometryVersion` | `geometryVersion()` |
| `draft` | `draft()`, `setDraft(...)` |
| `selection` | `selection()`, `setSelection(...)` |
| `hover` | `hover()`, `setHover(...)` (no notification) |
| `sketch` (+ didSet undo) | `sketch()`, `setSketch(...)`, `updateSketch(fn)` |
| `sketchUndo/Redo` | `sketchUndo()`, `sketchRedo()` |
| `toast` | `toast()`, `setToast(...)` |
| `documentName`, `documentURL` | `documentName()/setDocumentName`, `documentPath()/setDocumentPath` |
| `isolationRestore` | `isolationRestore()`, `setIsolationRestore` |
| `referenceItems` | `referenceItems()` |
| `itemSelection` | `itemSelection()`, `setItemSelection` |
| `settings` (+ didSet) | `settings()`, `setSettings(...)`, `updateSettings(fn)` |
| `scatterOrigins` | `scatterOrigins()` (copy id → (source, first copy)) |
| `kernel` | `kernel()` (`o3d::Kernel&`) |
| `reload(features:settings:)` | `reload(features, settings)` |
| `canUndo/canRedo/bodyIds/draftError` | same names |

### History, drafts, selection (ModelStore.swift)

`commit(features)`, `undo()`, `redo()`, `toggleSuppressed(id)`,
`deleteFeature(id)`, `regenerate()` (checkpointed replay: resumes from the
longest unchanged prefix with `Kernel::saveState/restoreState`, resolves
variable bindings, rebuilds reference items, reuses meshes by shape key, prunes
per-body settings when no draft/sketch is open, filters the selection).

`begin(op, sketch?, replacing?)`, `edit(id)`, `updateDraft(op)`,
`flushPendingRebuild()`, `applyDraft()`, `cancelDraft()`,
`displayAppearance()`, `commitImmediately(op)`.

`selectedBodies()`, `selectedFaces()`, `selectedEdges()` (pairs of body, index),
`select(pick?, additive)`, `faceInfo(body, face)`, `edgeInfo(body, edge)`,
`properties(body)` (cached by shape key).

### Commands (ModelStore.swift)

`addPrimitive(kind)`, `startSketch(plane, target?)`,
`startSketchOnSelectedFace()`, `editSketch(id)`, `finishSketch(revolve)`,
`finishSketchAsItem()`, `cancelSketch()`, `setSketchTool(tool)`,
`sketchEscape()`, `beginSketchDrag()`, `endSketchDrag()`, `canUndoSketch()`,
`canRedoSketch()`, `undoSketch()`, `redoSketch()`.

Sketch bar setters (Swift assigned `store.sketch?.x`): `setPolygonSides`,
`setPolygonCircumscribed`, `setConstructionMode`, `setOffsetDistance`,
`setFilletRadius`, `setSketchSelection`, `toggleSketchConstraintSelection(i)`.

Sketch constraints: `constrainSelection(kind)`, `setDimension(index, value)`,
`deleteSketchSelection()`, `constrainHorizontalOrVertical()`,
`offsetSketchSelection()`, `filletSketchSelection()`,
`disconnectSketchSelection()`, `toggleConstruction()`.

Body tools: `pushPullSelection()`, `filletSelection(chamfer)`,
`shellSelection()`, `moveSelection()`, `duplicateSelection()`,
`deleteSelection()`, `boolean(kind)`.

Exchange: `importFile(path)` (STEP/IGES/STL/BREP → `imported` features),
`exportExchange(format)` → bytes (selection's bodies or all).

### ModelStore+Construct.swift

`addPlaneThrough(points)`, `addPlaneNormalToEdge(p)`,
`addPlaneParallelToFace(p)`, `addPlaneThroughEdge()`, `addAxisThrough(points)`,
`addAxisThroughTwoPlanes()`, `addAxisOfRoundFace()`, `addAxisAlongEdge()`,
`addAxisNormalToFace(p)`, `embossSelection()`, `touchedBodies()`,
`toggleVisibilityOfSelection()`, `isolateSelection()`, `isIsolating()`,
`exitIsolation()`, `showAllBodies()`, `invertBodyVisibility()`.

Point picks (Swift passed closures to `beginPointPick`): `beginPointPick(purpose)`
clears the selection and returns a `PointPickSession` (title, prompts, accepted
kinds) for the viewport to fill; `session.rejection(point)` is the toast for a
wrong kind; when `points.size() == needed()` call
`completePointPick(purpose, points, measureDeltas)`. Constructions run on the
store; measurements come back as `PointPickResult{measurement, recentText}` for
the viewport's pinned list / Recent menu.

### ModelStore+Files.swift

`importMaterial(jsonText, fileName)`, `ModelStore::color(hex)`,
`insertDecal(imageBytes, name)` (PNG/JPEG/... bytes instead of a URL).

### ModelStore+SketchTools.swift

`canTransformSketch()`, `transformSketchSelection(kind, params)`,
`mirrorSketchSelection()`, `addSketchOutlines(pathElements)` (Sketch ▸ Text: the
host lays out the text with its font engine and passes positioned glyph paths in
mm; the store does Swift's `addOutline`: lines, splines through quad/cubic
samples, closing, selection).

### ModelStore+Tools.swift

`visibleMeshes()`, `selectedItems()`, `selectedBody()`,
`toggleItemSelection(id)`, `toggleHiddenBody(body)` (Swift `toggleHidden(body:)`),
`toggleHiddenItem(id)` (`toggleHidden(item:)`), `renameBody(body, name)`
(`rename(body:to:)`), `renameItem(id, name)`, `setAppearance(appearance, bodies)`,
`appearanceTargets()`, `usedMaterials()` (VisualizationPanel), `editItem(id)`,
`useSketchItem(id, revolve)`, `selectedSketchItems()`, `sweepSelection()`,
`loftSelection()`, `addOffsetPlane()`, `addAngledPlane()`, `addMidplane()`,
`startSketchOnSelectedPlane()`, `scaleSelection()`, `scatterSelection()`,
`linearPatternSelection()`, `circularPatternSelection()`, `mirrorSelection()`,
`splitSelection()`, `deleteFacesSelection()`, `draftSelection()`,
`offsetFacesSelection()`, `measureSelection()` (→ `o3d::Measurement`),
`measureRows()` (MeasurePanel rows in project units), `selectedAxis()`,
`addAxis()`, `translateSelection()`, `rotateSelection()`, `alignSelection()`,
`replaceFaceSelection()`, `fillSelection()`, `projectSelection()`,
`projectIntoSketch()`, `insertImage(imageBytes, name)`,
`insertProject(documentBytes, name)`, `addVariable()`, `variableValues()`,
`bindDraft(parameter, expression?)`.

### Terrain.swift

`insertRollingHills()`, `insertTerrain(imageBytes, name)`.

### Exporters.swift, ExportSheet.swift, WebViewer.swift, DrawingView.swift

`exportBodies()`, `exportSketches()`, `canExport(kind)` (ModelCommands'
`exportable`), `exportData(kind, drawingSettings?)` → bytes, `threeMF()`,
`obj()`, `glb()`, `write(kind, folder, name?)` → path, `quickExport(formats,
folder)` (favourite formats/folder were UserDefaults; the host passes them),
`webPage(title)` → HTML, `drawingProjections()`, `drawingSheet(settings?)`.

### Operation inspector (Feature.swift, OperationPanel.swift)

`Operation::parameters()` (`{id, label, value, unit}`; unit `"mm"` values are
lengths to show in project units), `choices()` (`{id, label, options,
selected}`), `setting(id, value)`, `settingChoice(id, index)`,
`combineMode()`, `setting(mode, target)` (the Result picker, shown when
`sketchTarget()` is set), `settingAroundV(bool)` (revolve axis picker),
`sketch()`, `setting(sketch)`. The panel calls `store.updateDraft(op.setting…)`,
`store.applyDraft()`, `store.cancelDraft()`, shows `store.draftError()`, and
"Edit Sketch" calls `store.editSketch(draft.replacing)`. `Feature::title()`,
`symbol()` (SF Symbol names, for hosts that map them), `isReference()`.

### Tool catalog and keys (Commands.swift, ViewportController+Keys.swift)

`ToolCatalog::all()` lists every rail/menu/search tool with `isEnabled(store)`
and `run(store)`; `run` returns a UI action string for the host when the tool
needs UI (`"ui.importFile"`, `"view.fit"`, `"view.mode.zebra"`,
`"export.step"`… see ToolCatalog.hpp). `ToolCatalog::run(store, id)`,
`search(store, query)`, `shortcut(id)`, `handleKey(store, key)`.

### Sketch pointer input (ViewportController+Sketch.swift)

`SketchInput input(store)`; per event set `input.pickRadius` (7 points on
desktop, 14 on touch, in plane units) and `input.gridStep`
(`gridStep(camera.distance, fixedGrid)`), convert the pointer to a plane point
(ray ∩ sketch plane, `plane.local(hit)`), then call `hover(p?)`, `tap(p,
additive)`, `dragDraws(p)`, `drawBegan(p?)`, `drawChanged(p?)`,
`drawEnded(p?)`. `snap(p)`, `hit(p)`, `projectedBodyPoints()` are public.
`annotations(offset)` gives constraint glyphs/dimension labels at plane
positions; project them and call `SketchInput::spreadAnnotations(screen)`.

## Viewport functions not ported (left to the viewport agent)

From `ViewportController+Sketch.swift`: `planePoint(at:)` (camera ray ∩ plane),
`pickRadius` (camera `worldPerPoint`), `sketchOverlay` (line batch drawing of the
sketch, previews, guides, cursor), the screen projection inside
`sketchAnnotations` (data part ported as `SketchInput::annotations` +
`spreadAnnotations`).
From `+Points.swift`: `pickPoint(at:)` (screen-space vertex/edge/face snapping;
`BodyMesh::nearestFaceNormal` ports its curved-face normal fallback),
`pointPickTap/Hover` (use `PointPickSession::rejection` and
`ModelStore::completePointPick`), `pointOverlay`, `remember` (Recent list),
`zoomToSelection`, `snapToNearestOrtho`, `rotateView`, `lookAtSection`
(camera; it also sets `settings.camera.orthographic = true` via `updateSettings`).
From `+Reference.swift`: `referenceOverlay`, `measureOverlay` (drawing only).
From `+Keys.swift`: Escape cancelling a point pick (viewport state); the rest is
`ToolCatalog::handleKey`.
`ViewportController` state that Swift kept outside the store (display mode,
section view, show edges/sketches/decals, pinned measurements, fixed grid,
grid position) stays with the viewport.

## Swift behaviour not ported, and differences

- **USDZ export**: written with SceneKit; `ExportKind::usdz` is reported
  unavailable (`isAvailable` false, toast "AR Quick Look, Reality Composer").
- **Text outlines** (`addSketchText`): CoreText glyph layout is the host's job;
  the store takes the glyph paths (`addSketchOutlines`).
- **PDF drawings**: CoreGraphics/CoreText replaced by a small PDF writer using
  the standard Helvetica fonts (text centring uses Helvetica AFM widths, bold
  approximated); strokes and layout are identical.
- **Images**: CGImageSource/CGImageDestination replaced by stb_image /
  stb_image_write (`third_party/stb`). Height-map greyscale conversion
  (CoreGraphics device-gray draw with high-quality interpolation) is
  approximated by Rec.601 luma with box filtering, so terrain heights from an
  image can differ slightly from the Mac's.
- **Security-scoped URLs / file pickers / UserDefaults**: inputs are paths or
  bytes; `DisplayFormat` and Quick Export preferences are set by the host.
- **Slicer detection** (`Slicer.installed`, Applications folder scan) and
  opening apps/URLs, share sheets, pasteboard: host-specific, not ported.
- **Locale**: `formatted()` mimics en_US `.number.precision(.fractionLength(0...3))`
  (grouping commas); `abbreviatedDate()` is en_US ("Nov 5, 2026").
  Variable names are lower-cased ASCII-only (Swift lower-cases Unicode).
- **Sets** are written sorted (see File format).
- **NaN/∞** can't be encoded by Swift (it throws); the writer writes `0` / ±max
  instead. The model never stores them.
- `updateDraft` coalescing: see `scheduleRebuild`.
- `Pick` equality ignores `index` for body picks (as Swift's enum cases did).
- No Core bug fixes were needed; `App/`, `Bridge/` and `Core/` are untouched.
