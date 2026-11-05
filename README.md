# Sculpt3D

A native, touch-first solid modeler for iPhone, iPad and Mac, built on the
OpenCascade B-rep kernel.

## Layout

| Path | What it is |
| --- | --- |
| `Core/` | Platform-neutral C++17 geometry core (`o3d::Kernel`) over OpenCascade, and the 2D sketch constraint solver (`o3d::sketch::solve`). No Apple code, so a Windows app can reuse it. |
| `Bridge/` | Objective-C++ facades (`O3DKernel`, `O3DSketchSolver`) that Swift calls. |
| `App/Sources/Model` | Feature history (the document), replay/regeneration, selection, import/export. |
| `App/Sources/Viewport` | Metal renderer, orbit camera, CPU picking, sketch input, touch/Pencil and mouse/trackpad handling. |
| `App/Sources/UI` | SwiftUI: model browser, toolbars, contextual action bar, live operation panel, menu commands. |
| `App/Sources/MCP` | MCP server (Mac): JSON-RPC core, the modeling tools, and the localhost HTTP transport. |
| `AppCore/` | Shared C++ app core behind a C API (`AppCore/API.md`): document, viewport (OpenGL ES 3), MCP server. Used by Windows and Android. |
| `Windows/` | Windows app: WinUI 3 (C# / .NET 8) front end over `sculpt.dll` (AppCore + ANGLE). See `Windows/README.md`. |
| `scripts/` | `build_occt.sh` (OpenCascade → `third_party/OCCT.xcframework`), `test_core.sh` (kernel and solver tests), `test_sketch_flow.sh` (headless sketch → extrude → edit test). |

## Build

```bash
scripts/build_occt.sh          # once, ~1h: macOS, iOS and iOS Simulator slices
xcodegen generate
open Sculpt3D.xcodeproj         # schemes Sculpt3D-macOS and Sculpt3D-iOS
```

Run the kernel and solver tests with `scripts/test_core.sh`, and the headless
end-to-end sketch test with `scripts/test_sketch_flow.sh`. With the Mac app
running, `scripts/test_mcp.py` models a part through the MCP server and checks it.

### Windows

On Windows 10/11 x64 with Visual Studio 2022 (Desktop C++), the .NET 8 SDK and
Git, from an "x64 Native Tools Command Prompt for VS 2022":

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_windows_deps.ps1   # once, ~1h: OpenCascade + ANGLE
powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -Run
```

The app is unpackaged and self-contained (no runtime installs). Details,
controls and first-build troubleshooting are in `Windows/README.md`.

## MCP server

The Mac and Windows apps run a [Model Context Protocol](https://modelcontextprotocol.io)
server, so AI assistants can model in the open documents; changes show up live
in the window. It listens only on `http://127.0.0.1:7317/mcp` (Streamable HTTP,
plain JSON responses) and refuses browser requests from other origins. Turn it
off, or copy the setup command, from **Sculpt3D ▸ MCP Server** in the menu bar
(on Windows: the **MCP** menu).

Add it to Claude Code:

```bash
claude mcp add --transport http sculpt3d http://127.0.0.1:7317/mcp
```

Other clients that support HTTP servers take the same URL. Tools (units mm and
degrees, Z up):

| Group | Tools |
| --- | --- |
| Documents | `list_documents`, `select_document`, `new_document` |
| Inspect | `get_model` (history, parameters, bodies, variables), `get_body_topology` (face and edge indices), `measure`, `screenshot`, `set_view` |
| Create | `add_primitive`, `sketch_extrude`, `sketch_revolve` (rectangles, circles, polygons, polylines on xy/xz/yz, a custom plane or a body face), `import_file` |
| Modify | `boolean`, `fillet_edges`, `chamfer_edges`, `shell_body`, `push_pull_face`, `transform_body`, `mirror_body`, `pattern_body`, `duplicate_body`, `delete_body` |
| History | `update_feature` (numbers or expressions), `delete_feature`, `suppress_feature`, `set_variable`, `undo`, `redo` |
| Output | `export_model` (STEP, IGES, STL, BREP) |

Every modeling tool appends a feature to the parametric history and is
trial-run first, so a failing operation reports its error without changing
the model. Editing tools return an error while a sketch or tool panel is open
in the app; finish or cancel it there first.

## Android

`Android/` is the Android app (Kotlin, Jetpack Compose, Material 3) over the
shared C++ core in `AppCore/` (C API `AppCore/include/sculpt/sculpt_c.h`,
documented in `AppCore/API.md`), which owns the model, sketching, the GLES 3
viewport and the MCP server. Phones and tablets, light and dark.

```bash
scripts/build_occt.sh android-arm64     # once: OpenCascade for Android
cd Android && ./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

MCP on Android: turn it on from the app menu (MCP Server…), then

```bash
adb forward tcp:7317 tcp:7317
claude mcp add --transport http sculpt3d http://127.0.0.1:7317/mcp
```

See `Android/README.md` for details and `Android/docs/screenshots/` for the
emulator verification run.

## Modeling features

The left rail holds the tool groups (icons with label pills and shortcut key
caps); ⌘F searches every tool, and the Mac menu bar mirrors the groups. The
rail changes with the mode: modeling, sketching, measuring or Visualize.

- **Project / Items:** units, snapping; bodies, sketches, planes, axes and
  images with select, rename, show/hide and properties.
- **Sketch:** line, 3-point arc, spline, rectangle (diagonal or centre), circle,
  ellipse, polygon, point; project bodies into the sketch, offset edge, corner
  fillet, trim, delete; construction mode; undo inside the sketch. Constraints
  down the right: parallel, perpendicular, tangent, coincident, midpoint,
  concentric, horizontal/vertical, equal, symmetry, disconnect, lock, make
  construction; dimensions for length, distance, radius and angle.
- **Snapping:** grid, guide lines (alignment with existing points), guide
  points (midpoints, quadrants), 3D points (body corners) with hints; fixed or
  automatic grid size.
- **Insert:** variables (usable as expressions in any field), reference image,
  file (STEP, IGES, STL, BREP), another Sculpt3D project, primitives.
- **Construct:** offset, angled and mid planes; axes.
- **Transform:** move/rotate, scale, translate, linear and circular pattern,
  rotate around axis, align, mirror, duplicate.
- **Tools:** offset face, fillet, chamfer, extrude, shell, loft, union,
  subtract, intersect, split body, revolve, sweep, fill, replace face, project
  as edge, push/pull, draft, delete faces.
- **Visualize:** material library (plastics, metals, glass, rubber, ceramic,
  wood) with physically based shading, environment (background, light, ground
  shadow), camera (perspective/orthographic, field of view), 4× capture.
- **View:** view cube, display modes, section view, measure mode (deltas,
  pinned measurements), screenshot, export.
- **History:** every step is replayed from the document, so any past step can
  be edited, suppressed or deleted and the model regenerates.

Not yet: generative (AI) rendering, decals, wrap & emboss.

## Controls

- **Mac:** click a face or edge (⇧/⌘-click to add), double-click a body;
  drag or two-finger scroll to orbit, ⌥-drag / ⇧-scroll to pan, pinch or
  mouse wheel to zoom. Model and Camera menus have shortcuts.
- **iPhone/iPad:** tap a face or edge, double-tap a body; one finger orbits
  (draws while sketching), two fingers pan and pinch to zoom. Pointer hover on iPad.
- **Sketching:** tap/click points, lines and circles to select them (except
  with the Line tool); the bar above the tools offers the constraints that fit.
  Tap a dimension label to edit its value. Esc ends a line, then deselects,
  then leaves the sketch; ⌘⌫ deletes the selection.
