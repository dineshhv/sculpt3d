# Sculpt3D for Android

The Android front end of Sculpt3D: Kotlin + Jetpack Compose (Material 3) over
the shared C++ core (`AppCore/`, OpenCascade). All modeling, sketching,
selection, the GLES 3 viewport and the MCP server live in the core; this app
draws the chrome from the core's state JSON and sends it commands
(`AppCore/API.md`). The SwiftUI app (`App/Sources/UI`) is the reference for
layout and behaviour.

## Requirements

- Android SDK with NDK `28.2.13676358` and CMake `3.22.1` (`local.properties` → `sdk.dir`)
- JDK 17+ (builds with JDK 23; `jvmTarget` is 17)
- OpenCascade for Android, built once: `scripts/build_occt.sh android-arm64`
  → `third_party/occt-build/android-arm64-install`
- arm64-v8a devices or emulators (an `x86_64` slice needs `scripts/build_occt.sh android-x64`)

## Build and run

```bash
cd Android
./gradlew :app:assembleDebug          # native code is always built optimised
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.higglerslab.sculpt3d/.MainActivity
```

Emulator used for verification: `Pixel_3a_API_33_arm64-v8a`
(`~/Library/Android/sdk/emulator/emulator -avd Pixel_3a_API_33_arm64-v8a`).
Screenshots from that run are in `docs/screenshots/`.

## Layout of the code

| Path | What it is |
| --- | --- |
| `app/src/main/cpp/` | `CMakeLists.txt` (builds AppCore + OCCT) and `sculpt_jni.cpp` (one JNI function per C API call) |
| `core/SculptNative.kt` | Kotlin binding of `sculpt_c.h` |
| `model/State.kt` | Immutable data classes for the state, annotations and command replies (kotlinx.serialization) |
| `session/DocumentSession.kt` | One open document: native handle, listener → main thread → state/annotations, commands, UI requests |
| `session/Documents.kt` | Open documents, active document, recents, autosave, display formats, MCP server |
| `viewport/SculptGLView.kt` | `GLSurfaceView` (ES 3.0, 4× MSAA config with fallback, render-on-demand), raw pointer events, pinch/pan/twist, mouse wheel, key names |
| `ui/Workspace.kt` | The document window (Swift `ContentView`): viewport, rail, panels, bars, toasts |
| `ui/Rail.kt` | Tool rail per mode, flyout, sketch rail, constraint column, view controls, view cube, snap/grid/project popovers |
| `ui/Bars.kt` | Selection bar, operation panel, sketch bar + constraint bar, section, measure, point-pick banner, pinned card |
| `ui/Panels.kt` | Items, History, Properties, Measure side panel, Search palette, Variables |
| `ui/Visualize.kt` | Visualize panel (material library, custom material, environment, camera, capture) |
| `ui/Annotations.kt` | Dimension/constraint labels, inline dimension editor, measurement labels |
| `ui/Sheets.kt` | Export, Field of View, sketch transforms, Sketch ▸ Text (glyph outlines), MCP, documents, Drawing |
| `ui/MainMenu.kt` | The app menu (File, Edit, Item, Sketch, Insert, Construct, Transform, Tools, View) |
| `ui/SymbolIcons.kt` | The one SF Symbol → Material icon mapping |
| `MainActivity.kt` | Host: Storage Access Framework pickers/savers, sharing, hardware keyboard, Back, autosave |

Phones (< 700 dp wide) get icon-only rails and bottom sheets for Items,
History and Visualize; tablets get label pills and side panels like the iPad.

## Using it

- Tap a face or edge, double-tap a body; one finger orbits (draws while
  sketching), two fingers pan, pinch zooms, twist turns the model.
  Mouse: wheel zooms, right-drag orbits, middle-drag pans, Shift/Alt-drag pans.
- Hardware keyboard: the core's shortcuts (`AppCore/API.md` "Keys") plus
  Ctrl+F search, Ctrl+S / Ctrl+Shift+S save, Ctrl+O open, Ctrl+N new,
  Ctrl+W close, Ctrl+Shift+E export, Ctrl+Shift+I/P import, Ctrl+Shift+V variables.
- Back steps out: closes popovers, cancels a point pick or operation, escapes
  the sketch, leaves Measure/Visualize, clears the selection, then shows the
  welcome page.
- Documents: the name pill at the top opens the document switcher (New, Open,
  Save, Save As, Home). Documents with a location are saved back on pause;
  untitled ones are autosaved to app storage and listed under Recent.

## MCP server

Menu ▸ **MCP Server…** turns the server on (it remembers the setting). It
listens on `127.0.0.1:7317` on the device; forward it to your computer and add
it to Claude Code:

```bash
adb forward tcp:7317 tcp:7317
claude mcp add --transport http sculpt3d http://127.0.0.1:7317/mcp
```

Tools run on the UI thread (`sculpt_mcp_drain`); `screenshot` renders on the
viewport's GL thread; `new_document` opens a new document in the app.
