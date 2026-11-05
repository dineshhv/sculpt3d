# Sculpt3D core C API

The Android (Kotlin/JNI) and Windows (C#/P/Invoke) apps are thin UIs over
`AppCore/include/sculpt/sculpt_c.h`. The core owns the document, the
parametric history, sketching, selection, the viewport (camera, picking,
gestures, keys, GLES rendering), file exchange and the MCP server. A front end
draws native chrome (rail, panels, sheets, text labels) from one JSON
**state** snapshot and sends **commands**; the 3D view is a GL surface the
core draws into, fed with raw pointer events.

The Apple app is the reference: `App/Sources/UI/*.swift` shows what each
panel looks like; this document says which state field it reads and which
command each control sends (see the **UI checklist** at the end).

Implementation: `AppCore/src/api/` — `CApi.cpp` (C functions, `SculptDoc`),
`State.cpp`, `Commands.cpp`, `Actions.cpp` (argument-free actions with their
enabled rules), `ViewportController*.{hpp,cpp}` + `ViewportOverlay.cpp`
(port of `ViewportController.swift` + extensions and `ViewportView.swift`),
`CApiMcp.cpp`. Tests: `AppCore/tests/capi_tests.cpp`.

## Conventions

- Strings are UTF-8. Every returned `char*`/`uint8_t*` is freed with `sculpt_free`.
- Lengths are **millimetres**, angles **degrees**, Z up. The state also gives
  display strings in the project's units (`…Text`, `display…`), formatted as
  the Swift `formatted()` / `formatLength` do. Use them as-is.
- View coordinates are **points** (dp on Android, DIPs on Windows: pixels /
  scale), origin top-left.
- Icons are **SF Symbol names** (`"square.stack.3d.up"`), exactly as the
  Apple app uses them; each front end maps them to its own icon set. A few
  custom glyphs: `sculpt3d.project` (brand mark), `sculpt3d.visualize` (colour
  wheel), `sculpt3d.magnet` (snapping).
- Ids: bodies are small integers (stable for a given history); features,
  items, variables and point measurements are upper-case UUID strings.

## Threading

- Every `SculptDoc` has a re-entrant lock; every C call takes it. The UI
  thread may send commands while the GL thread renders.
- `sculpt_view_gl_init/release/resize/render/snapshot*` must be called on the
  thread with the current EGL/ANGLE context. The thread that last called
  `render` is remembered as the GL thread.
- `sculpt_doc_set_listener`: the listener is called after a call that
  changed something returns (lock released), on that call's thread, whenever
  `sculpt_doc_revision` **or** `sculpt_view_revision` increased. Post to the
  UI thread; request a GL frame; re-read `sculpt_doc_state` only when
  `sculpt_doc_revision` changed (camera moves only move the view revision).
  It may also be called while the MCP server waits for a screenshot, so it
  must never block.
- MCP: `sculpt_mcp_drain()` (UI thread) locks every registered document while
  tool jobs run. A `screenshot` tool asks the GL thread for a frame
  (`sculpt_view_needs_redraw` turns true and the listener fires), releases
  the lock and waits up to 10 s. If the UI thread *is* the GL thread (e.g. an
  ANGLE swap chain driven from the UI thread), it renders directly.
- Draft rebuilds are synchronous (Swift deferred slider rebuilds to the next
  run-loop turn; hosts may throttle slider commands to ~30/s).

## Functions

| Function | Notes |
| --- | --- |
| `sculpt_version()` | static string |
| `sculpt_doc_create(name)` / `destroy` | destroy also unregisters from MCP |
| `sculpt_doc_load(doc, bytes, len)` | `.sculpt3d` / legacy `.o3d` JSON; resets view state (re-framed once); clean afterwards. Returns NULL or an error. |
| `sculpt_doc_save(doc)` | `.sculpt3d` JSON, byte-compatible with the Apple app |
| `sculpt_doc_is_dirty` / `sculpt_doc_mark_saved` | dirty = history or settings changed since load/mark |
| `sculpt_doc_set_name` | shown in titles, MCP listings, default export names |
| `sculpt_doc_revision` | store revision + view-state revision; cheap |
| `sculpt_doc_state` | **State** JSON below |
| `sculpt_doc_command` | **Commands** below |
| `sculpt_doc_export(doc, format, options, &len, &err)` | `format`: any export kind id or extension: `step`/`stp`, `iges`/`igs`, `stl`, `brep`, `obj`, `glb`/`gltf`, `3mf`/`threeMF`, `sculpt3d`, `svg`, `pdf`, `dxf`/`dxfSketch`, `dxfDrawing`. Exports the selection's bodies, or all visible bodies. `options`: `{"drawing": DrawingSettings}` for `pdf`, `svg`, `dxfDrawing`. Unavailable kinds (`usdz`, `parasolid`, `jt`, `dwg*`) return an error. |
| `sculpt_doc_import_path(doc, path)` | `.step .stp .iges .igs .stl .brep .brp` (as `imported` bodies) or `.sculpt3d .o3d` (Insert ▸ Project). Frames the view. |
| `sculpt_doc_import_image(doc, bytes, len, name, mode)` | `mode` 0 = Insert ▸ Image (selected plane/flat face, else Top), 1 = File ▸ Import Image as Decal (selected flat face), 2 = Insert ▸ Terrain ▸ From Height Map |
| `sculpt_view_gl_init` / `gl_release` | init again after context loss |
| `sculpt_view_resize(doc, wpx, hpx, scale)` | scale = pixels per point; the view size in points is wpx/scale × hpx/scale. Call before the first render (headless too: picking needs the size). |
| `sculpt_view_render` | draws the frame into the bound framebuffer, then serves queued MCP screenshots |
| `sculpt_view_needs_redraw` | true after any change or while a screenshot is queued; cleared by render |
| `sculpt_view_snapshot(doc, w, h)` | RGBA8 top-row-first (GL thread); NULL without GL |
| `sculpt_view_snapshot_png(doc, w, h)` | **added**: PNG bytes (GL thread). Screenshot button: 2× the view size; Visualize ▸ Capture: 4×. |
| `sculpt_view_revision` | **added**: moves with anything drawn or annotated, camera included |
| `sculpt_view_annotations` | **added**: label positions, see **Annotations** |
| `sculpt_view_pointer`, `orbit`, `pan`, `zoom`, `key` | see **Viewport input** |
| `sculpt_mcp_*` | see **MCP** |

Additions to the original header (all additive; JNI and Kotlin updated):
`sculpt_view_snapshot_png`, `sculpt_view_revision`, `sculpt_view_annotations`,
and the meaning of `sculpt_doc_import_image`'s last argument (0/1/2; Kotlin
`docImportImageAs`).

## Viewport input

### Pointer events — `sculpt_view_pointer(doc, id, type, button, phase, x, y, mods)`

Send every raw event; the core recognises taps, double taps and drags the
way the Apple app's recognisers do (`ViewportView.swift`).

| Event | Behaviour |
| --- | --- |
| `DOWN` | starts a gesture (one at a time). While a touch gesture is active, another touch `DOWN` (different `id`) cancels it: a drawing drag ends (`drawEnded` at the last point), and no tap fires. Two-finger pan and pinch are the host's gestures (`sculpt_view_pan` / `sculpt_view_zoom`). |
| `MOVE` | before the drag threshold (touch 10 pt, mouse/pen 3 pt) nothing happens. Past it, a **primary-button / one-finger / pen** drag *draws* when `dragDraws` says so — in a sketch with a drag tool (rectangle, centre rectangle, circle, ellipse, polygon), or with Select on sketch geometry (moves it) — and otherwise **orbits**. Mouse/pen with ALT or SHIFT held **pans** (Mac ⌥/⇧-drag). Secondary button orbits, middle button pans. `MOVE` with no gesture (mouse/pen) is treated as `HOVER`. |
| `UP` | ends a drawing drag (`drawEnded`); without a drag on the primary button it's a **tap**. A second tap within 0.5 s and 6 pt (mouse/pen) or 0.35 s and 30 pt (touch) is a **double tap** (the first tap already ran; a third is a tap again). `additive` = SHIFT or CONTROL. |
| `CANCEL` | like the iOS recogniser's cancelled state: a drawing drag ends at the last point; nothing else. |
| `HOVER` / `EXIT` | hover highlight (faces, edges, sketch snapping cursor, point-pick preview) / clear it |

Tap semantics (`ViewportController.tap/doubleTap`):
- In a sketch: the active tool's tap (line chain, arc 3 points, spline points
  — tap the first point to close, the last again to finish —, point, trim;
  other tools select geometry, additive with SHIFT/CONTROL). Double taps are
  two taps.
- While a draft (tool panel) is open: ignored.
- During a point pick: picks the snapped point (corner > edge > face); wrong
  kinds toast "Pick a point on a face/edge"; the last point completes it.
- Measure mode: toggles the pick, keeping the last two.
- Otherwise: selects the face or edge under the pointer (edges within 14 pt
  on touch, 6 pt with mouse/pen), additive toggles. Double tap selects the
  whole body (keeping at most one previously selected body so two can be
  combined; double-tapping a selected body deselects it).

Pick tolerances follow the pointer type of the last event: touch uses the iOS
values (edge 14 pt, sketch hit 14 pt, point pick 18 pt), mouse and pen the
Mac values (6, 7, 9 pt).

### Gestures from the host

| Call | Use |
| --- | --- |
| `sculpt_view_orbit(dx, dy)` | trackpad two-finger scroll: `orbit(-scrollDX, -scrollDY)` (Mac) |
| `sculpt_view_pan(dx, dy)` | two-finger touch pan: the translation delta in points; trackpad scroll with SHIFT: `pan(-scrollDX, -scrollDY)` |
| `sculpt_view_zoom(factor, x, y)` | factor > 1 zooms **in**, towards the surface under (x, y) (pass x < 0 for no point). Pinch: `factor = scale change` (`>1` spreading). Mouse wheel: `factor = 1.1^(notchesUp)` (Swift `zoom(pow(1.1, -deltaY/3))` inverted). Trackpad magnify: `1 + magnification`. |

### Keys — `sculpt_view_key(doc, key, mods)` → 1 when handled

`key` is one character (`"l"`, `"1"`) or a name: `Escape`, `Delete`,
`Backspace`, `Enter`/`Return`. `SCULPT_MOD_CONTROL` stands for the Mac's ⌘.
Send keys only while the 3D view has focus (never while typing in a field).

| Key | Action |
| --- | --- |
| Escape | cancel point pick → sketch Escape (end line / cancel arc / end spline / deselect / leave sketch) → cancel draft → clear selection |
| Enter | apply draft (if no error); in a sketch finish it (Extrude, or Done when editing) unless a dimension is selected |
| Delete / Backspace | sketch: delete selection; else delete selected bodies |
| plain letters (no sketch) | `ToolCatalog.keyShortcuts`: E extrude, V revolve, W sweep, F fillet, H shell, M move/rotate, N translate, K sketch on Top (only when the tool is enabled) |
| plain letters (sketch) | tool keys (`state.sketch.tools[].key`: L line, A arc, R rectangle, C circle, P polygon, S select, T trim…), O offset edge |
| SHIFT+letter (sketch) | constraint column: ⇧A parallel, ⇧P perpendicular, ⇧T tangent, ⇧N coincident, ⇧M midpoint, ⇧C concentric, ⇧V horizontal/vertical, ⇧E equal, ⇧S symmetry, ⇧L lock |
| Ctrl+Z / Ctrl+Shift+Z (Ctrl+Y) | undo / redo (sketch steps while sketching) |
| Ctrl+D, Ctrl+U, Ctrl+B, Ctrl+I | duplicate, union, subtract, intersect |
| Ctrl+A / Ctrl+Shift+A | select all visible bodies / deselect all |
| Ctrl+1…7, Ctrl+8 | default (iso), front, back, top, bottom, right, left; nearest ortho |
| Ctrl+Shift+F | zoom to fit |
| Ctrl+Shift+H | show/hide selection |
| Ctrl+Alt+M / Ctrl+Alt+S | measure / section view toggles |
| Ctrl+Delete | delete (sketch selection or bodies) |
| Alt+S/W/X/V/Z/C | display mode shaded / wireframe / x-ray / visualized / zebra / curvature |
| Alt+H / Alt+Shift+H | show hidden bodies / invert body visibility |

Not handled (return 0, host menus do them): Ctrl+F search, Ctrl+Shift+E
export, Ctrl+Shift+I import, Ctrl+Shift+P import project, Ctrl+Alt+P
properties, Ctrl+Alt+S… is section (handled), Alt+L collapse panels,
Ctrl+Shift+V variables.

## Annotations — `sculpt_view_annotations(doc)`

Text that the Apple app draws as SwiftUI overlays is returned with **screen
positions in points** so front ends draw native text. Positions depend on the
camera: re-read whenever `sculpt_view_revision` changes (e.g. after each
rendered frame, or on the listener). The document revision does not move with
the camera.

```json
{
  "viewRevision": 812,
  "size": [800, 600],
  "camera": {"yaw": -0.785, "pitch": 0.55, "distance": 220, "target": [0,0,10],
             "right": [x,y,z], "up": [x,y,z], "forward": [x,y,z],
             "orthographic": false, "fieldOfView": 40, "worldPerPoint": 0.27},
  "gridStep": 10, "gridLabel": "10\nmm",
  "annotations": [
    {"id": "3", "kind": "dimension", "text": "60 mm", "x": 412.5, "y": 188, "selected": false,
     "constraint": 3, "constraintKind": "length", "value": 60, "editValue": "60", "unit": "mm"},
    {"id": "0", "kind": "constraint", "text": "H", "x": 380, "y": 230, "selected": false,
     "constraint": 0, "constraintKind": "horizontal"},
    {"id": "hint", "kind": "hint", "text": "Midpoint", "x": 434, "y": 284, "selected": true, "constraint": -1}
  ],
  "measurements": [{"id": "UUID", "text": "42.5 mm", "x": 300, "y": 220}]
}
```

- `annotations` (only while sketching and `view.showConstraints`): the label
  centre point. Dimensions: semibold monospaced caption, constraint glyphs:
  bold caption2; `selected` → white on orange, else on a material
  background. Overlapping labels are already spread (Swift `16/18 pt` rule).
  The snapping hint (`kind: "hint"`) is not tappable.
- **Tap** on a dimension or glyph: `{"cmd":"sketch.toggleConstraintSelection","index":constraint}`
  (selects only it, or deselects). A selected dimension appears in
  `state.sketch.selectedDimension`, where the sketch bar shows its value
  field. To edit in place (e.g. double tap on the label), show a text field
  prefilled with `editValue` (+ `unit`) and send
  `{"cmd":"sketch.setDimension","index":…,"text":"<typed>"}`.
- `measurements`: point-measurement labels (shown when
  `view.showPinnedMeasurements`), centre at (x, y) — already 16 pt above the
  anchor — with an × button: `{"cmd":"measure.removePoint","id":…}`.
- `camera` drives a view cube (`Panels.swift ViewCube`): project with
  `right`/`up`; a face tap sends `{"cmd":"view.setOrientation","face":"top"}`.

## State — `sculpt_doc_state(doc)`

One object; arrays keep model order. Fields marked † are display strings.

```text
revision            number   sculpt_doc_revision
viewRevision        number   sculpt_view_revision at the time
document            {name, path, dirty}
mode                "modeling" | "sketching" | "measuring" | "visualize"
modeTitle †         "Modeling" | "Sketching" | "Measuring" | "Visualize"  (rail Project pill subtitle)
idle                bool     no draft and no sketch
toast               string | null   show as a capsule with ⚠︎, dismiss after 4 s → "toast.dismiss"
uiRequest           {serial, action}  a key shortcut asked for front-end UI (serial increments)
gl                  {ready, error}
units               {id: mm|cm|m|inch|ft, symbol, title, scale (mm per unit), imperial}

features[]          History panel rows:
  {id, index, number (1-based), title, symbol, case (Swift case name), suppressed,
   error: string|null, hasParameters, hasSketch, editable, editing (open in the draft),
   isReference}
history             {canUndo, canRedo, sketchUndo (undo acts on the sketch), emptyTitle †, emptySubtitle †}

selection
  picks[]           {kind: body|face|edge, body, index?}
  bodies[]          selected whole-body ids
  bodyCount, faceCount, edgeCount
  summary †         "1 body, 2 faces"
  items[]           selected item UUIDs, in pick order
  touchedBodies[]   bodies of everything selected
  selectedBody      id | null
  hover             pick | null
  hintTouch †, hintPointer †   SelectionBar text when nothing is selected
selectionBar[]      {id (an action command), title, symbol, destructive}
                    the SelectionBar buttons in order; the last is "selection.clear" (×)

bodies[]            Items ▸ Bodies, in id order:
  {id, name, hidden, selected, touched,
   appearance: Appearance,
   properties: null | Properties}      (filled for selected/touched bodies)
items[]             Items ▸ Sketches & Planes:
  {id, name, kind: sketch|plane|axis|image, symbol, hidden, selected,
   order: n|null (badge when several are selected), editTitle † ("Edit Plane"/"Edit Sketch")}
project             {bodyCount, hiddenBodyCount, featureCount, summary † ("2 bodies · 5 history steps"), isolating}

draft               null | Draft           (OperationPanel)
sketch              null | Sketch          (sketch mode)
settings            Settings
view                View
measure             Measure
pointPick           null | {purpose, title, prompt, picked, needed, text † ("Pick the second point  ·  2 of 2")}
tools[]             ToolCategory
actions             {actionId: enabled}    every argument-free action (see Commands ▸ Actions)
visualize           Visualize
export              {kinds[]: ExportKind, scope † ("All 3 visible bodies"), bodyCount}
displayFormat       {footInch, footInchOptions[{id,title}], footInchEnabled, degrees, degreeOptions[{id,title}]}
```

**Appearance** `{color: [r,g,b] 0…1 sRGB, metallic, roughness, opacity, preset: id|null, finish †, name †}` (finish/name: the material panel's label).

**Properties** `{size: [x,y,z] mm, volume mm³, area mm², faceCount, edgeCount,
bboxMin, bboxMax, sizeText †, volumeText †, areaText †, facesEdgesText †,
captionLines † [3 lines for the Items row]}`.

**Draft**
```text
title † ("Extrude" / "Edit Extrude"), featureTitle, symbol, case, replacing: UUID|null
parameters[]  {id, label, value (mm or °/×/count), unit ("mm", "°", "×", ""), isLength,
               displayValue, displayUnit, text † (field text), step (± button step in display units),
               binding: expression|null}
choices[]     {id, label, options[], selected}         segmented pickers
combineMode   null | {selected: newBody|join|cut, options[{id,title}]}   "Result" picker
revolveAxis   null | {aroundV, options[{aroundV,title}]}               "Axis" picker
canEditSketch bool   (header "Edit Sketch" button)
error         string|null    (orange ⚠︎ line; Apply disabled)
canApply      bool
```

**Sketch**
```text
tool          select|line|arc|spline|controlSpline|rectangle|centerRectangle|circle|ellipse|polygon|point|trim
tools[]       {id, title, symbol, key|null (upper case), subtitle|null ("Diagonal", "6 sides"…), drags, active}
plane         {name † ("Top plane"/"Front plane"/"Right plane"/"Face plane"), origin, normal, xDir}
replacing     UUID|null (editing a feature's sketch: show only "Done")
target        body id|null
constructionMode, polygonSides (3…24), polygonCircumscribed
offsetDistance, offsetDistanceText †, filletRadius, filletRadiusText †   (ConstraintBar number fields)
selection[]   {kind: point|line|circle|constraint|spline|ellipse, index}
hover         ref|null
selectedCounts {points, lines, circles, splines, ellipses, constraints}
counts        {points, lines, circles, splines, ellipses, constraints}
isEmpty, profiles, profilesText † ("1 profile")
status        {degreesOfFreedom, fullyConstrained}
statusText †  "Grid 10 mm" (empty) | "Fully constrained" (green seal) | "3 degrees of freedom"
escapeTitle † "End line" | "Cancel arc" | "End spline" | "Deselect" | "Cancel"
canUndo, canRedo, canTransform
finishOnReturn  Return finishes the sketch (false while a dimension is selected)
showConstraints, chainActive, pendingArcPoints, splineDraftPoints, snapHint
selectedDimension  null | {index, kind, label †, value, unit, displayValue, displayUnit, text †, step}
availableConstraints[] {kind, title († "Concentric" for two circles), symbol}   ConstraintBar buttons
constraintBar  {visible (selection non-empty), noneFits ("No constraint fits this selection"),
                showOffset, showFillet, showDisconnect, showMakeConstruction}
constraintColumn[] {id, title, symbol, keys|null, enabled}   right-hand ConstraintColumn
constraints[]  {index, kind, title, symbol, glyph, isDimension, value, unit, label †, selected}
```

**Settings**
```text
units, unitOptions[{id, symbol, title}], snapping
snap         {grid, guideLines, guidePoints, points3D, distantEdges, showGuidePoints, showHints}
environment  {background [r,g,b], gradient, showGrid, lightIntensity (0…2), lightAzimuth (-180…180),
              groundShadow, lightElevation (10…85), castShadows, shadowSoftness (0…1),
              ambientOcclusion, occlusionStrength (0…1), smoothShading}
camera       {orthographic, fieldOfView (°)}
variables[]  {id, name, expression, value|null, valueText † ("—" when invalid → red)}
```

**View**
```text
displayMode (shaded|wireframe|xray|visualized|zebra|curvature), displayModeTitle †, displayModeSymbol
displayModes[] {id, title, symbol, key (Alt+key)}
showEdges, showHiddenEdges, showSketches, showDecals, showPinnedMeasurements, showWorldAxes,
showConstraints, gridAtModelBottom
fixedGrid    mm|null (null = automatic)
gridStep     mm, gridLabel † ("10\nmm", the grid button), gridOptions[{value mm, title †}] (Grid popover),
lockGridTitle † ("Lock Grid Size (10 mm)")
section      {enabled, axis 0|1|2, offset mm, offsetText †, flipped, range [lo, hi] (slider bounds)}
measuring, measureDeltas, visualizing
```

**Measure**
```text
rows[]         {label, text †}   MeasureSidePanel rows (Distance, ΔX/ΔY/ΔZ with deltas, Angle, Length,
                                 Radius, Diameter, Area, Volume, Size X/Y/Z with deltas)
compactRows[]  {label, value, unit, text †}   MeasurePanel (compact/phone) rows
message †      when rows are empty: "Select any item…" / "Can't measure this selection"
kernelError    the kernel's last error (compact panel shows it when not empty)
pinned[]       pinned texts ("Distance: 12 mm"), unpin by index
recent[]       Tools ▸ Measure ▸ Recent (copy to clipboard in the host)
pointMeasurements[] {id, label}
```

**ToolCategory** (`tools[]`, order sketch, insert, construct, transform, tools, visualize, view)
```text
{id, title, symbol, onRail (all but view), dot (Tools shows an accent dot),
 tools[] {id, title, symbol, hint, enabled, shortcut|null ("E", "⌘U", "⇧⌘F")}}
```
`visualize` has no tools: its rail button enters Visualize (`visualize.enter`).
The `view` category is the right-hand controls / View menu.

**Visualize**
```text
targets[]          bodies the material applies to (selection, or all)
targetText †       "Applies to all bodies" | "Applies to 2 selected bodies"
appearancePanelText † "All bodies" | "1 selected body"
current            Appearance of the first target
finishIndex        0 Matte, 1 Plastic, 2 Metal (AppearancePanel picker); finishOptions[]
usedMaterials[]    {appearance, bodies[], count}   "Used materials" rows (tap → selection.selectBodies)
palette[]          AppearancePanel swatches [r,g,b]
backgrounds[]      Environment background swatches [r,g,b]
families[]         material library families in order (library: "materials.library")
```

**ExportKind** `{id, title, detail †, extension, mimeType, available, enabled, forSlicer, fromDrawing}`
(`ExportKind.menu` order: sculpt3d, threeMF, parasolid, step, dxfSketch, dxfDrawing, iges, stl, usdz,
jt, glb, obj, dwgSketch, dwgDrawing, svg, pdf).

## Commands — `sculpt_doc_command(doc, json)`

Request `{"cmd": "<name>", …args}`. Reply `{"ok": true, …}` or
`{"ok": false, "error": "…"}` (unknown command, missing/ill-typed argument,
precondition such as "Finish or cancel the sketch first", or a disabled
action). Model failures surface the store's toast: `{"ok": true, "toast": "…"}`
(the toast is also in the state). A reply with `"ui": "<action>"` asks the
front end to do UI work (open a file picker, …) — see **UI actions**.

### Actions (argument-free; `state.actions[id]` is the enabled flag)

Running a disabled action returns `ok:false`. `{"cmd":"action.run","id":…}` is
an alias.

| Id | Swift | Enabled when |
| --- | --- | --- |
| `edit.undo` / `edit.redo` | History buttons, Edit menu (sketch-aware) | sketch: sketch undo/redo; else history undo/redo and no draft |
| `edit.delete` | Edit ▸ Delete (⌫) | sketch: selection; else a body selected and no draft |
| `edit.duplicate` | Edit ▸ Duplicate | tool `transform.duplicate` enabled |
| `edit.selectAll` / `edit.deselectAll` | Edit menu | idle / always |
| `item.toggleVisibilityOfSelection` | Item ▸ Show/Hide | selection or item selection |
| `item.isolate`, `item.exitIsolation` | Item menu | touched bodies / isolating |
| `item.showAll`, `item.invertVisibility` | Item menu | hidden bodies / any body |
| `view.zoomToSelection` | Item ▸ Zoom to | touched bodies |
| `model.pushPull`, `model.sketchOnFace` | SelectionBar | one face, idle / faces, idle |
| `model.sketchOnPlane` | Sketch on Plane | a plane item selected |
| `model.sketch` | Tools ▸ Sketch (face, else plane, else Top) | idle |
| `model.shell` | Shell | idle and a body (or its faces/edges) selected |
| `model.fillet`, `model.chamfer` | SelectionBar, Tools | edges selected |
| `model.union`, `model.subtract`, `model.intersect` | booleans | two distinct bodies |
| `model.move`, `model.mirror`, `model.duplicate`, `model.scale`, `model.translate`, `model.linearPattern`, `model.circularPattern`, `model.rotate`, `model.split`, `model.project` | Transform / Tools | idle and a body |
| `model.delete` | SelectionBar trash | a body, no draft |
| `model.scatter`, `model.align` | Transform | two distinct bodies |
| `model.offsetFaces`, `model.draft`, `model.deleteFaces` | Tools | faces |
| `model.replaceFace`, `model.midplane` | | exactly two faces |
| `model.fill`, `model.planeThroughEdge`, `model.axisAlongEdge` | | edges |
| `model.sweep`, `model.loft`, `model.offsetPlane`, `model.angledPlane`, `model.axis`, `model.rollingHills` | | idle |
| `model.emboss` | Tools ▸ Wrap & Emboss | idle, a sketch item selected |
| `model.axisThroughTwoPlanes` | | faces + plane items ≥ 2 |
| `model.axisOfRoundFace` | | faces or edges |
| `model.addVariable` | Variables ▸ Add | always |
| `pick.planeThrough3Points`, `pick.planeOnCurveAtPoint`, `pick.planePerpendicularToEdgeAtPoint`, `pick.planeParallelToFaceAtPoint`, `pick.axisThrough2Points`, `pick.axisPerpendicularToFaceAtPoint`, `pick.measurePointToPoint`, `pick.measureThreePointAngle` | Construct / Measure point picks (clear the selection, leave measure mode, show `state.pointPick`) | idle |
| `pick.cancel` | PointPickBanner Cancel | a point pick |
| `measure.objects` | Tools ▸ Measure ▸ Object Measurements | idle |
| `measure.clear` | Measure ▸ Recent ▸ Clear Measurements | any point measurement or recent |
| `sketch.project`, `sketch.offset`, `sketch.fillet`, `sketch.disconnect`, `sketch.toggleConstruction`, `sketch.horizontalOrVertical`, `sketch.deleteSelection`, `sketch.mirror` | Sketch menu / rail / bar | as in Swift (selection, one point, transformable…) |
| `sketch.finish` | Extrude (or Done when editing) | sketch not empty, or replacing |
| `sketch.revolve` | Revolve | new sketch, not empty |
| `sketch.finishAsItem` | Done (keep as item) / Sketch ▸ Finish | in a sketch (bar disables it when empty) |
| `sketch.cancel`, `sketch.escape` | Exit Sketching / Escape button | in a sketch |
| `sketch.undo`, `sketch.redo`, `sketch.lookAt`, `sketch.toggleConstructionMode` | | in a sketch |
| `sketch.text` | Sketch ▸ Text… | in a sketch → reply `ui: "ui.sketchText"` |
| `sketch.setTool`, `sketch.transform` | (enabled flags only; need arguments) | no draft / selection transformable |
| `draft.apply`, `draft.cancel`, `draft.editSketch` | OperationPanel | draft without error / draft / editing a sketch feature |
| `visualize.enter`, `visualize.exit` | rail Visualize / Exit Visualization | not sketching / visualizing |
| `view.goToModeling` | View ▸ Go to Modeling | visualizing or Visualized mode |
| `view.lookAtSection` | Tools ▸ 2D Section View | always (turns the section on, orthographic) |
| `file.importFile`, `file.importProject`, `file.importMaterial`, `file.importDecal`, `file.importHeightMap` | File / Insert menu items | as Swift; reply `ui: "ui.importFile"` etc. |

### Commands with arguments

**General**
| Command | Args | Result |
| --- | --- | --- |
| `state` | — | `{state}` |
| `toast.dismiss` | — | |
| `document.setName` / `document.setPath` | `name` / `path` | |
| `tool.run` | `id` (ToolCatalog id, e.g. `insert.box`, `tools.extrude`, `view.top`, `export.step`) | runs if enabled; view actions run in the core; else `{ui}` |
| `tool.search` | `query` (empty lists enabled tools) | `{results[{id,title,symbol,hint,category,categoryTitle,enabled,shortcut}]}` |
| `ui.clearRequest` | — | clears `state.uiRequest.action` |

**History**: `feature.open {id}` (row tap: edit parameters or sketch),
`feature.edit {id}`, `feature.editSketch {id}`, `feature.toggleSuppressed {id}`,
`feature.delete {id}`.

**Draft** (OperationPanel)
| Command | Args |
| --- | --- |
| `draft.setParameter` | `id` and one of `value` (mm/°), `text` (as typed: number in display units — comma ok — or an expression over variables, which binds the field), `expression` (binds). A plain value clears an existing binding. Reply `{error}` = draft error after the rebuild. |
| `draft.stepParameter` | `id`, `direction` (+1/−1): the ± buttons (step = `parameters[].step`) |
| `draft.setChoice` | `id`, `index` |
| `draft.setCombineMode` | `mode`: `newBody` \| `join` \| `cut` |
| `draft.setRevolveAxis` | `aroundV`: bool |

**Insert / import**: `insert.primitive {kind: box|cylinder|sphere|cone|torus}`
(opens the primitive's draft: Apply/Cancel), `import.file {path}`,
`import.image {path, as: image|decal|terrain, name?}`,
`import.material {json, fileName} | {path}`.

**Sketch**
| Command | Args |
| --- | --- |
| `sketch.start` | `plane`: `"xy"`/`"top"`, `"xz"`/`"front"`, `"yz"`/`"right"`, or `{origin, normal, xDir}` |
| `sketch.startOnItem` | `id` of a plane item (Items ▸ Sketch on Plane) |
| `sketch.setTool` | `tool`, optional `sides` (3…24), `circumscribed`. Starts a sketch (face → plane → Top) when none is open, like the Sketch menu. |
| `sketch.setPolygonSides` | `sides` |
| `sketch.setPolygonCircumscribed`, `sketch.setConstructionMode` | `on` |
| `sketch.setOffsetDistance`, `sketch.setFilletRadius` | `value` (mm) or `text` (display units), > 0 |
| `sketch.select` | `refs[{kind, index}]`; `sketch.clearSelection` |
| `sketch.toggleConstraintSelection` | `index` (annotation tap) |
| `sketch.constrain` | `kind` (raw: `horizontal`, `vertical`, `length`, `distance`, `horizontalDistance`, `verticalDistance`, `radius`, `angle`, `parallel`, `perpendicular`, `equalLength`, `equalRadius`, `coincident`, `pointOnLine`, `pointOnCircle`, `tangent`, `midpoint`, `fixed`, `symmetric`); must be in `availableConstraints` |
| `sketch.constraintColumn` | `id` of `sketch.constraintColumn[]` (`settings` toggles constraint display; `coincident`, `concentric`, `equal`, `hv` pick the right kind) |
| `sketch.setDimension` | `index` + `value` (mm/°) or `text` (display units or expression) or `expression`. Bad values (≤ 0 lengths, unsolvable) → `ok:false` with the reason. |
| `sketch.transform` | `kind`: `scale` (`factor`, `keepOriginal`), `rotate` (`angle`, `keepOriginal`), `linearPattern` (`count` 2…200, `spacing` mm, `direction` °), `circularPattern` (`count` 2…360, `totalAngle`) |
| `sketch.addOutlines` | `path[{op: move|line|quad|cubic|close, points: [[x,y]…]}]` in mm at the sketch origin — Sketch ▸ Text: lay out the text with the platform font (DirectWrite / Android `Paint.getTextPath`), font size = height mm, then send the glyph outlines |
| `sketch.setShowConstraints` | `on` |

**Selection, bodies, items**: `selection.set {picks[{kind, body, index?}]}`,
`selection.clear`, `selection.toggleBody {id}` (Items row tap),
`selection.selectBodies {ids}`, `item.toggleSelection {id}`,
`body.rename {id, name}`, `body.toggleHidden {id}`, `body.setVisible {id, visible}`,
`body.properties {id}` → `{properties}`, `item.rename {id, name}`,
`item.toggleHidden {id}`, `item.edit {id}`, `item.extrude {id}`,
`item.revolve {id}`, `item.delete {id}`.

**Appearance**: `appearance.set` with optional `bodies[]` (default: the
Visualize targets) and one of `preset` (library id), `reset: true`, or any of
`color` ([r,g,b] or "#rrggbb"), `metallic`, `roughness`, `opacity` (0.05…1),
`finish` (`matte`|`plastic`|`metal`). Custom edits start from the first
target's appearance and clear its preset. `materials.library {query?}` →
`{families[{family, presets[{id, finish, name, title, appearance}]}]}`.

**Settings**: `settings.setUnits {units}`, `settings.setSnap {grid?, guideLines?,
guidePoints?, points3D?, distantEdges?, showGuidePoints?, showHints?, snapping?}`,
`settings.setEnvironment {any environment field…, reset?}` (background as
[r,g,b]/"#rrggbb"; values clamped to the slider ranges),
`settings.setCamera {orthographic?, fieldOfView?, reset?}`,
`variable.add` → `{id}`, `variable.update {id, name?, expression?}`,
`variable.remove {id}`, `format.set {footInch?: architectural|decimal,
degrees?: decimal|degreesMinutesSeconds}` (app-wide; persist and restore at launch).

**View**
| Command | Args |
| --- | --- |
| `view.look` | `preset`: iso/default, front, back, top, bottom, right, left |
| `view.fit`, `view.snapToNearestOrtho`, `view.cycleDisplayMode`, `view.toggleSection`, `view.toggleMeasuring` | — |
| `view.rotate` | `yaw?`, `pitch?` in quarter turns (Rotate Left = yaw −1, Up = pitch +1) |
| `view.setOrientation` | `face`: top/bottom/front/back/right/left (view cube) or `yaw`, `pitch` radians |
| `view.orbit` | `dx`, `dy` |
| `view.setDisplayMode` | `mode` |
| `view.set` | any of `showEdges`, `showHiddenEdges`, `showSketches`, `showDecals`, `showPinnedMeasurements`, `showWorldAxes`, `gridAtModelBottom`, `showConstraints`, `measureDeltas` |
| `view.setFixedGrid` | `value` mm or null (Automatic) |
| `view.setGridLock` | `on` (locks the current automatic step) |
| `view.section` | any of `enabled`, `axis` (0/1/2), `offset` mm, `flipped` |
| `view.setMeasuring`, `view.setVisualizing` | `on` |

**Measure / point picks**: `measure.pin {text} | {label, value}`,
`measure.unpin {index}`, `measure.removePoint {id}`, `pointPick.begin {purpose}`
(same purposes as the `pick.*` actions).

**Export / drawing**
| Command | Args | Result |
| --- | --- | --- |
| `export.write` | `kind`, `folder`, `name?` | `{path}` (also Export to Slicer: write 3MF/STL/OBJ to a temp folder, open it with the slicer) |
| `export.quick` | `formats[]`, `folder` | `{paths}`, toast "Exported 3mf, step to …" |
| `export.webPage` | `title?` | `{html, defaultName}` — File ▸ Publish / View in Browser |
| `drawing.sheet` | `drawing` (DrawingSettings) | `{sheet{size [w,h] mm, scale, scaleText, strokes[{points[[x,y]], dashed, width}], labels[{text, at, size, bold}]}, papers[], views[], scales[], title}` — paper mm, y down |

DrawingSettings: `{paper: a4|a3|letter|tabloid, views: [front, top, right,
isometric], showHidden, showDimensions, scale: number|null (paper:model, null
fits), title}`. Drawing files: `sculpt_doc_export(doc, "pdf"|"svg"|"dxfDrawing",
'{"drawing":{…}}')`.

### UI actions (`"ui"` in a reply, or `state.uiRequest.action`)

`ui.showVariables` (open the Variables sheet), `ui.importImage`,
`ui.importFile`, `ui.importProject`, `ui.importMaterial`, `ui.importDecal`,
`ui.importHeightMap` (file pickers; then call `sculpt_doc_import_image` /
`import_path` / `import.material`), `ui.sketchText` (Text sheet → glyph
outlines → `sketch.addOutlines`), `view.screenshot` (GL thread:
`sculpt_view_snapshot_png` at 2× the view size, then a save dialog),
`export.step|iges|stl|brep` (`sculpt_doc_export`, then a save dialog named
`Model.<ext>`). View actions (`view.fit`, `view.iso`, `view.top`,
`view.front`, `view.right`, `view.toggleSection`, `view.toggleMeasure`,
`view.mode.*`) run in the core and never reach the front end.

## MCP

`sculpt_mcp_start(port, post, ctx)` starts `sculpt::mcp::Server`
(`src/mcp/`) on 127.0.0.1:port (7317 in the Apple app); `post` is called
from the HTTP thread whenever a tool job is queued — post a call to
`sculpt_mcp_drain()` onto the UI thread. Register every open document
(`sculpt_mcp_register`, in window order), `sculpt_mcp_activate` the
front-most one on focus changes, unregister before destroy (destroy does it
too). `sculpt_mcp_set_new_document_handler` gives `new_document` a way to
open a window: create the document, show it, return it (the core registers
it). The `screenshot` tool renders on the GL thread (see Threading); with no
GL context it returns an error. Tools changing a document fire its listener.

## UI checklist (SwiftUI view → state → commands)

| Swift view | Reads | Sends |
| --- | --- | --- |
| `ContentView` (layout) | `mode`, `sketch`, `draft`, `view.measuring`, `view.visualizing`, `view.section.enabled`, `pointPick`, `toast`, `measure.pinned` | bottom bar: SectionPanel if section on; OperationPanel if draft; SketchBar if sketch; MeasurePanel (compact) if measuring; else SelectionBar (not in Visualize) |
| `ToolRail` | `modeTitle`, `tools[]` (rail = `onRail`), `actions` | Project (popover), Items (panel), Search (palette), category → flyout; Visualize → `visualize.enter`; bottom: Section `view.toggleSection` (subtitle On/Off), Measure `view.toggleMeasuring`; measure mode: Exit Measure `view.setMeasuring{on:false}`; visualize: Exit `visualize.exit` |
| `ToolFlyout` | `tools[cat].tools[]` (title, symbol, `shortcut` key caps, enabled) | `tool.run {id}` (handle `ui`) |
| `SketchRail` | `sketch.tools[]` (all but select/trim in the drawing group), `sketch.plane.name`, `sketch.selection`, `sketch.constructionMode` | Exit `sketch.cancel`; plane → `sketch.lookAt`; tool → `sketch.setTool`; Project `sketch.project`; Offset Edge `sketch.offset`; Fillet `sketch.fillet`; Select/Trim `sketch.setTool`; Delete `sketch.deleteSelection`; Construction `sketch.toggleConstructionMode` |
| `ConstraintColumn` | `sketch.constraintColumn[]` | `sketch.constraintColumn {id}` |
| `ViewControlsColumn` | `annotations.camera` (ViewCube), `settings.snap.grid` (magnet active), `view.gridLabel`, `settings.environment.showGrid`, `view.displayModeSymbol/Title`, `history` | cube face → `view.setOrientation{face}`; magnet → SnapSettings popover; grid label → Grid popover; grid toggle → `settings.setEnvironment{showGrid}`; Display Modes → `view.cycleDisplayMode`; Screenshot → `sculpt_view_snapshot_png`; Export STEP → `sculpt_doc_export("step")`; History → host panel toggle |
| `SnapSettingsPanel` | `settings.snap` | `settings.setSnap{…}` |
| `GridSettingsPanel` | `view.fixedGrid`, `view.gridOptions` | `view.setFixedGrid{value|null}` |
| `ProjectPanel` | `settings.units`, `settings.unitOptions`, `settings.snap.grid`, `project.summary` | `settings.setUnits`, `settings.setSnap{grid}` |
| `ItemsPanel` | `bodies[]` (name, hidden, selected, appearance.color, properties.captionLines), `items[]` | row tap `selection.toggleBody` / `item.toggleSelection`; eye `body.toggleHidden` / `item.toggleHidden`; rename `body.rename` / `item.rename`; item menu: `item.edit`, `item.extrude`, `item.revolve`, `sketch.startOnItem`, `item.delete` |
| `HistoryPanel` / `FeatureRow` | `features[]`, `history` | undo/redo `edit.undo`/`edit.redo`; row tap `feature.open` (if editable); menu `feature.edit`, `feature.editSketch`, `feature.toggleSuppressed`, `feature.delete` |
| `SearchPalette` | `tool.search` results | `tool.run` (Return runs the first enabled) |
| `SelectionBar` | `selection.summary`, `selection.hintTouch/hintPointer`, `selectionBar[]` | each button's `id` as a command (`selection.clear` for ×) |
| `OperationPanel` / `ParameterField` | `draft` | `draft.setParameter{text}` on submit/blur, ± → `draft.stepParameter`, pickers `draft.setChoice` / `draft.setCombineMode` / `draft.setRevolveAxis`, `draft.editSketch`, `draft.cancel`, `draft.apply` (disabled when `!canApply`) |
| `SketchBar` | `sketch.statusText`, `profilesText`, `escapeTitle`, `replacing`, `isEmpty`, `finishOnReturn` | Escape `sketch.escape`; replacing: Done `sketch.finish`; else Done `sketch.finishAsItem`, Revolve `sketch.revolve`, Extrude `sketch.finish` (disabled when empty) |
| `ConstraintBar` | `sketch.selectedDimension`, `availableConstraints`, `constraintBar`, `offsetDistanceText`, `filletRadiusText` | dimension field `sketch.setDimension{index,text}`; constraint buttons `sketch.constrain{kind}`; Offset `sketch.offset` + field `sketch.setOffsetDistance{text}`; Fillet `sketch.fillet` + `sketch.setFilletRadius{text}`; Disconnect `sketch.disconnect`; Make Construction `sketch.toggleConstruction`; trash `sketch.deleteSelection` |
| `SketchAnnotationsView` | `sculpt_view_annotations().annotations` | tap `sketch.toggleConstraintSelection{index}` |
| `MeasurementLabels` | `sculpt_view_annotations().measurements` | × `measure.removePoint{id}` |
| `PinnedMeasurementsCard` | `measure.pinned` (outside measure mode, when `view.showPinnedMeasurements`) | × `measure.unpin{index}` |
| `MeasureSidePanel` | `measure.rows`, `measure.message`, `view.measureDeltas`, `measure.pinned` | close `view.setMeasuring{on:false}`; deltas `view.set{measureDeltas}`; pin `measure.pin{label,value:text}`; unpin `measure.unpin` |
| `MeasurePanel` (compact) | `measure.compactRows`, `measure.kernelError` | close `view.setMeasuring{on:false}` |
| `SectionPanel` | `view.section` | `view.section{axis|offset|flipped}`, close `view.section{enabled:false}` |
| `PointPickBanner` | `pointPick.title`, `pointPick.text` | Cancel `pick.cancel` |
| `ToastView` | `toast` | × or after 4 s `toast.dismiss` |
| `PropertiesPanel` | `selection.touchedBodies` → `bodies[].properties` (sizeText, volumeText, areaText, facesEdgesText), `appearance`, `hidden`; empty: `document.name`, `project` | colour `appearance.set{bodies:[id], color}`; opacity `appearance.set{bodies:[id], opacity}`; Visible `body.setVisible`; units `settings.setUnits` |
| `AppearancePanel` | `visualize.appearancePanelText`, `visualize.palette`, `visualize.current`, `visualize.finishIndex`, `view.displayMode` | swatch `appearance.set{color}`; finish `appearance.set{finish}`; display `view.setDisplayMode` |
| `VisualizationPanel` Material | `visualize.targetText`, `visualize.usedMaterials`, `materials.library{query}`, `visualize.current` | used row `selection.selectBodies{ids}`; tile `appearance.set{preset}`; custom colour/metallic/roughness/opacity `appearance.set{…}`; Reset `appearance.set{reset:true}` |
| `VisualizationPanel` Environment | `settings.environment`, `visualize.backgrounds` | `settings.setEnvironment{…}`; Reset `settings.setEnvironment{reset:true}` |
| `VisualizationPanel` Camera | `settings.camera` | `settings.setCamera{orthographic|fieldOfView}`; Capture → `sculpt_view_snapshot_png` at 4× |
| `FieldOfViewSheet` | `settings.camera` | `settings.setCamera{…}`, Reset `settings.setCamera{reset:true}` |
| `VariablesPanel` | `settings.variables` | Add `variable.add`; fields `variable.update{id,name|expression}`; trash `variable.remove` |
| `SketchTransformSheet` | `sketch.canTransform` | Apply `sketch.transform{kind,…}` |
| `SketchTextSheet` | — | host glyph layout → `sketch.addOutlines` |
| `ExportSheet` | `export.kinds`, `export.scope` | Export… `sculpt_doc_export(kind)` + save dialog; favourites are host preferences; Export Favorites Now `export.quick{formats, folder}`; slicer: `export.write{kind, folder: temp}` then open the file with the app |
| `DrawingWindow` / `DrawingPropertiesPanel` | `drawing.sheet{drawing}` (re-request when settings or the model change) | PDF/SVG/DXF → `sculpt_doc_export("pdf"|"svg"|"dxfDrawing", {"drawing":…})` |
| `ModelCommands` (menus) | `actions`, `tools[]`, `view`, `displayFormat`, `measure.recent` | File: import actions, `export.webPage`, export kinds; Edit: `edit.*`; Item: `item.*`, `view.zoomToSelection`; Sketch: `sketch.setTool{tool[,sides,circumscribed]}` and sketch actions, transform sheets; Insert: `tool.run insert.*`, `model.rollingHills`, `file.importHeightMap`; Construct: `model.*` and `pick.*`; Transform/Tools: `tool.run` / `model.*`, `visualize.enter/exit`, `view.toggleSection`, `view.section{enabled:true}`, `view.lookAtSection`, `measure.objects`, `pick.measure*`, `measure.clear`, `model.sketch`; View: `view.look`, `view.snapToNearestOrtho`, `view.rotate`, `view.fit`, `view.set{gridAtModelBottom|showWorldAxes|showEdges|showSketches|showHiddenEdges|showDecals|showPinnedMeasurements}`, `settings.setEnvironment{showGrid}`, `view.setDisplayMode`, `view.goToModeling`, `settings.setUnits`, `format.set`, `view.setGridLock` |
| `WelcomeView` | — | host: new (`sculpt_doc_create`) / open (`sculpt_doc_load`) |

## Not at parity (known)

- **Animated camera transitions**: none in the Swift app either.
- **Text outlines** need the host's font engine (`sketch.addOutlines`).
- **USDZ** export is unavailable (SceneKit); `isAvailable` false.
- **Slicer detection**, file pickers, share sheets, clipboard, opening URLs,
  "Copy Link": host work.
- Draft rebuilds are synchronous (Swift coalesced slider rebuilds per
  run-loop turn).
- The ⇧ constraint shortcuts and O (offset) are implemented in the core's key
  handler; the Swift app only showed them as key caps.
- Double-tap timing is the core's (0.5 s / 0.35 s) rather than the system
  double-click interval.
