# Sculpt3D for Windows

A native Windows front end for Sculpt3D at feature parity with the Mac app:
WinUI 3 (Windows App SDK 1.6, C# / .NET 8) chrome around the shared C++ core
(`AppCore/`, OpenCascade), which renders the 3D view with OpenGL ES 3 through
ANGLE (Direct3D 11) into a `SwapChainPanel`.

| Path | What it is |
| --- | --- |
| `Windows/CMakeLists.txt` | Builds `sculpt.dll`: AppCore (model, viewport, MCP server) + `native/AngleHost.cpp`. |
| `Windows/native/AngleHost.cpp`, `sculpt_win.h` | The ANGLE ↔ SwapChainPanel host (EGL on D3D11, DXGI composition swap chain, 4× MSAA, device-lost recovery). Exported from `sculpt.dll` as `sculpt_win_*`. |
| `Windows/Sculpt3D.sln`, `Windows/Sculpt3D/Sculpt3D.csproj` | The WinUI 3 app (unpackaged, self-contained: no runtime installs needed). |
| `Windows/Sculpt3D/Native/SculptNative.cs` | `[LibraryImport("sculpt")]` declarations for every C function (`AppCore/include/sculpt/sculpt_c.h` + `sculpt_win.h`). |
| `Windows/Sculpt3D/Core/` | `SculptDocument` (state JSON, commands, listener → UI thread, files), `McpService`, `AppSettings`, `FileAssociation`. |
| `Windows/Sculpt3D/UI/` | Everything you see: `MainWindow` (menu bar, tabs, save prompts), `DocumentView` (the layout of `ContentView.swift`), `ViewportControl` (SwapChainPanel + pointer/touch/pen/wheel input), rail, panels, bars, dialogs, drawing window, `Icons.cs` (SF Symbol → Segoe Fluent Icons map), `TextOutlines.cs` (DirectWrite glyph outlines via Win2D). |
| `scripts/build_windows_deps.ps1` | One-time: OpenCascade 7.9.3 (static, MSVC), ANGLE (vcpkg), header libraries. |
| `scripts/build_windows.ps1` | Builds `sculpt.dll` and `Sculpt3D.exe` and puts the DLLs next to the exe. |

The whole UI is built in C# code (no `.xaml` files), so there is no XAML
compiler step, and the C# type-checks anywhere the .NET 8 SDK runs (see
"Checking the C# without Windows" below). The core's behaviour and the meaning
of every state field and command are in `AppCore/API.md`.

## Prerequisites

- Windows 10 1903 or later (Windows 11 recommended), x64.
- **Visual Studio 2022** (Community is fine) with the workloads
  - *Desktop development with C++* (MSVC v143, Windows 11 SDK 10.0.22621 or later,
    *C++ CMake tools for Windows* — brings CMake and Ninja), and
  - *.NET desktop development* (optional, for building/debugging in the IDE).
- **.NET 8 SDK** (`winget install Microsoft.DotNet.SDK.8`).
- **Git** (`winget install Git.Git`); internet access for the first dependency build
  (OpenCascade sources, vcpkg/ANGLE, NuGet packages).
- About 15 GB free disk space and ~1 hour for the first dependency build.

You do **not** need to install the Windows App SDK runtime: the app is
self-contained (`WindowsAppSDKSelfContained`, `SelfContained`).

## Build

Open **"x64 Native Tools Command Prompt for VS 2022"** (Start menu), start
PowerShell in it, and from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_windows_deps.ps1   # once, ~1 h
powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -Run    # builds and starts the app
```

`build_windows.ps1` options: `-Configuration Debug`, `-Run`, `-SkipNative`
(reuse `build\windows-native\appcore\sculpt.dll`), `-Publish` (a self-contained
folder in `dist\Sculpt3D-win-x64` — zip it to give the app to someone).

What the scripts do, in case you want to run the steps by hand:

```powershell
# sculpt.dll (always Release, like the OpenCascade build)
cmake -S Windows -B build\windows-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build\windows-native
# the app
dotnet build Windows\Sculpt3D\Sculpt3D.csproj -c Release -p:Platform=x64
# then copy build\windows-native\appcore\sculpt.dll and third_party\vcpkg\installed\x64-windows\bin\*.dll
# next to Windows\Sculpt3D\bin\x64\Release\net8.0-windows10.0.22621.0\win-x64\Sculpt3D.exe
```

In Visual Studio: open `Windows\Sculpt3D.sln`, choose `Release | x64` (or
Debug), and press F5. Build `sculpt.dll` with the script first; the project
copies it (and the ANGLE DLLs) into the output folder when they exist.

## Run

`Windows\Sculpt3D\bin\x64\Release\net8.0-windows10.0.22621.0\win-x64\Sculpt3D.exe`.
It opens on the welcome page (New / Open / recent files). Documents open in
tabs; `.sculpt3d` files are byte-compatible with the Mac and iPad apps.

On first launch the app registers `.sculpt3d` (and legacy `.o3d`) files for the
current user (`HKCU\Software\Classes`), so double-clicking a project in Explorer
opens it — in the running window if Sculpt3D is already open. Settings, recent
files and a log live in `%LOCALAPPDATA%\Sculpt3D\` (`settings.json`, `log.txt`).

Controls (Help ▸ Controls in the app):

- **Mouse:** click a face or edge (Shift/Ctrl-click to add), double-click a body;
  drag or right-drag to orbit, middle-drag or Shift/Alt-drag to pan, wheel zooms
  towards the pointer (Shift+wheel pans).
- **Precision touchpad:** two-finger scroll orbits (as on the Mac trackpad),
  Shift+scroll pans, pinch zooms.
- **Touch and pen:** tap / double-tap; one finger orbits (draws while sketching),
  two fingers pan and pinch; pen hover highlights.
- **Keys:** tool letters (E, V, W, F, H, M, N, K; while sketching L, A, R, C, P, S,
  T, O and Shift+letter constraints), Esc, Enter, Del go to the 3D view when it
  has focus (click it). Ctrl shortcuts are menu accelerators (Ctrl = the Mac's ⌘):
  Ctrl+Z/Ctrl+Shift+Z (or Ctrl+Y), Ctrl+D, Ctrl+U/B/I, Ctrl+1…8 views,
  Ctrl+Shift+F zoom to fit, Ctrl+F search tools, Ctrl+Alt+S section,
  Ctrl+Alt+M measure, Alt+W/X/S/V/Z/C display modes, Ctrl+Alt+H/I/P sidebars.

## MCP server

Like the Mac app, Sculpt3D runs a Model Context Protocol server on
`http://127.0.0.1:7317/mcp` (localhost only) over every open document, so AI
assistants can model live in the window. Turn it on or off, see its status, or
copy the setup command from the **MCP** menu. For Claude Code:

```powershell
claude mcp add --transport http sculpt3d http://127.0.0.1:7317/mcp
```

`new_document` opens a new tab; `screenshot` renders through the same ANGLE
context (only documents whose tab has been shown can render).

## How it fits together

- `SculptDocument` owns a `SculptDoc*`. The core's listener (any thread) posts to
  the UI `DispatcherQueue`, which re-reads `sculpt_doc_state` when the document
  revision moved and asks the viewport for a frame. Panels are `StatePanel`s:
  each rebuilds only when the part of the state it shows changed, and updates
  text fields and sliders in place otherwise (so typing and dragging aren't
  interrupted).
- `ViewportControl` renders on demand on the UI thread from
  `CompositionTarget.Rendering` (which unhooks itself when idle). The UI thread
  is therefore also the GL thread, so MCP screenshots render synchronously.
- `AngleHost.cpp`: one process-wide EGL display (ANGLE, D3D11 backend) and GLES 3
  context; its `ID3D11Device` (via `EGL_EXT_device_query`) makes a composition
  swap chain per panel (`CreateSwapChainForComposition`, BGRA8, flip-sequential,
  2 buffers, inverse `CompositionScale` matrix), attached with
  `ISwapChainPanelNative::SetSwapChain`. The back buffer is wrapped with
  `eglCreatePbufferFromClientBuffer(EGL_D3D_TEXTURE_ANGLE)`; each frame the core
  draws into a 4× MSAA framebuffer that is resolved and blitted (rows flipped)
  into it, then `Present(1, 0)`. Device loss releases every document's GL
  objects, resets the device and re-initialises on the next frame.

## Checking the C# without Windows

The C# compiles on macOS/Linux with the .NET 8 SDK (it can't run there):

```bash
cd Windows/Sculpt3D
dotnet build -c Release -p:Platform=x64 -p:EnableWindowsTargeting=true -p:WindowsAppSDKSelfContained=false
```

(`WindowsAppSDKSelfContained=false` skips the manifest merge, which needs
Windows' `mt.exe`.)

## First build troubleshooting

The app was written on a Mac. The C# compiles (`dotnet build`, above);
`native/AngleHost.cpp` and every AppCore/Core source were syntax-checked for a
Windows target with clang (llvm-mingw, mingw-w64 + ANGLE headers), but not
with MSVC, and nothing has run on Windows yet. These are the places most likely
to need a tweak on the first build:

1. **ANGLE from vcpkg.** The port must provide `include\EGL\egl.h`,
   `include\GLES3\gl3.h`, `lib\libEGL.lib`, `lib\libGLESv2.lib` and
   `bin\libEGL.dll`, `bin\libGLESv2.dll` under
   `third_party\vcpkg\installed\x64-windows` (the deps script warns when one is
   missing). If the import libraries are named differently, adjust the
   `target_link_libraries(sculpt …)` line in `AppCore/CMakeLists.txt`.
   `AngleHost.cpp` defines every ANGLE constant it needs itself, and includes
   `EGL/eglext_angle.h` only when present.
2. **OpenCascade link errors** like `LNK2019 … __imp_…Standard…`: the static
   libraries need `OCCT_STATIC_BUILD` (set for Windows in
   `AppCore/CMakeLists.txt`). Unresolved Windows symbols (`CoCreateInstance`,
   `GetProcessMemoryInfo`, WIC, …): add the system library to the
   `target_link_libraries(sculpt_core PUBLIC …)` list there. If OpenCascade was
   installed with the Windows layout (`inc\`, `win64\vc14\lib`), AppCore accepts
   that too.
3. **MSVC compile errors in AppCore** (a missing standard header such as
   `<cctype>`, `<cstdlib>`, `<iterator>`): add the include; MSVC's STL includes
   less transitively than libc++.
4. **"The SwapChainPanel has no ISwapChainPanelNative"** in the viewport: the
   WinUI 3 interface IID is declared in `AngleHost.cpp`
   (`63aad0b8-7c24-40ff-85a8-640d944cc325`, from the Windows App SDK's
   `microsoft.ui.xaml.media.dxinterop.h`); compare it with that header.
5. **The 3D view is upside down** (picking then seems mirrored too): set the
   environment variable `SCULPT3D_FLIP_Y=0` before starting the app (or call
   `sculpt_win_set_flip_y(0)`). The default flips rows when copying into the
   swap chain because ANGLE stores GL rows bottom-up in the D3D texture.
6. **`eglCreatePbufferFromClientBuffer(EGL_D3D_TEXTURE_ANGLE)` fails**
   (error banner over the view): the BGRA swap-chain buffer vs. the RGBA EGL
   config. Try `DXGI_FORMAT_R8G8B8A8_UNORM` in `createSwapChain`/`ResizeBuffers`
   (composition swap chains accept it), or check that
   `EGL_ANGLE_flexible_surface_compatibility` is listed by the ANGLE build.
7. **The app starts and quits, or "Class not registered"**: the Windows App SDK
   runtime isn't next to the exe. Keep `WindowsAppSDKSelfContained`,
   `SelfContained` and `RuntimeIdentifier` (win-x64) in the csproj, build with
   `-p:Platform=x64`, and look in Event Viewer ▸ Windows Logs ▸ Application.
   If `dotnet build` fails in a PRI/packaging task, build from the VS developer
   prompt with MSBuild instead:
   `msbuild Windows\Sculpt3D.sln /restore /p:Configuration=Release /p:Platform=x64`.
8. **"Sculpt3D can't start its core"** dialog: `sculpt.dll`, `libEGL.dll` or
   `libGLESv2.dll` isn't next to `Sculpt3D.exe` (re-run `build_windows.ps1`, which
   copies them), or an MSVC runtime DLL is missing (install the VC++ 2015-2022 x64
   redistributable on a machine without Visual Studio).
9. **Panels look flat grey**: a WinUI theme brush key wasn't found (`Ui.cs`
   falls back to grey). Check the key names in `Ui.Brush(…)` against the Windows
   App SDK version.
10. **Two-finger touchpad scrolling zooms instead of orbiting**: Windows reported
    the touchpad as a mouse wheel. `ViewportControl.OnPointerWheelChanged`
    orbits only for `PointerDeviceType.Touchpad`; Alt+wheel orbits as a fallback.
11. **MCP "Not running: Could not listen on 127.0.0.1:7317"**: another Sculpt3D
    (e.g. the Mac app via a VM bridge, or a second copy) owns the port.
12. **Non-ASCII file names** rely on the UTF-8 process code page set in
    `app.manifest` (Windows 10 1903+).

## Known differences from the Mac app

- Menus follow Windows conventions (File ▸ Exit, Ctrl for ⌘); the Mac's app-menu
  MCP item is the **MCP** menu; Camera items have their own menu.
- "Copy Link" copies the project's `file:` URI; Export to Slicer looks for
  Bambu Studio, OrcaSlicer, PrusaSlicer, Creality Print and Cura in the usual
  install folders, and also offers the default app or Windows' "Open with".
- Sketch ▸ Text uses the installed Windows fonts (DirectWrite).
- No compact (phone) layout: the Windows window always uses the desktop layout.
