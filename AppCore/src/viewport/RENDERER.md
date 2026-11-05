# Viewport renderer (OpenGL ES 3.0)

Portable C++17 port of the Apple app's Metal viewport
(`App/Sources/Viewport/Renderer.swift`, `Shaders.metal`, `ShaderTypes.h`,
`Camera.swift`, `ReferenceImages.swift` and the picking / frame-geometry helpers
of `ViewportController.swift`). Namespace `sculpt::viewport`. Android (EGL /
GLSurfaceView) and Windows (ANGLE) draw with it; the shaders are GLSL ES 3.00
and use only core ES 3.0 features (no float render targets, no extensions).

| File | What |
| --- | --- |
| `ViewMath.hpp` | `Vec2f/Vec3f/Vec4f`, column-major `Mat4f` (`fromRows`, `*`, `inverse`). Header only. |
| `Scene.hpp` | Renderer input: `Scene`, `BodyMesh`, `LineBatch`, `Pick`, `Appearance`, `EnvironmentSettings`, `CameraSettings`, `Plane`, `ReferenceImage`, `DisplayMode`. |
| `Camera.hpp/.cpp` | Z-up orbit camera (no GL). |
| `Picking.hpp/.cpp` | `rayTriangle`, `rayHitsBox`, `surfaceHit`, `edgeHit`, `pick` (no GL). |
| `Overlays.hpp/.cpp` | `gridStep`, `grid`, `axesOnly`, `highlightEdges`, `marker`, `thickOffsets` (no GL). |
| `Lighting.hpp/.cpp` | `backgroundColors`, `keyLight`, `lightMatrix`, `smoothNormals`, `tileTransform`/`tileMatrix` (no GL). |
| `ImageDecode.hpp` + `StbImpl.cpp` | PNG/JPEG decode, downscale, premultiply, fingerprint (no GL). |
| `Shaders.hpp/.cpp` | GLSL ES 3.00 sources as raw string literals. |
| `GL.hpp` | `#include <GLES3/gl3.h>`. |
| `Renderer.hpp/.cpp` | The GL renderer. |

Only `Renderer.cpp` and `GL.hpp` touch GL; everything else builds on any host
(`AppCore/tests/viewport_math_tests.cpp` includes those `.cpp` files directly).

**stb_image:** `third_party/stb/stb_image.h` (v2.30), included as
`"stb/stb_image.h"` (the `third_party/` include dir comes from `sculpt_core`).
`STB_IMAGE_IMPLEMENTATION` is defined only in `viewport/StbImpl.cpp`, together
with `STB_IMAGE_STATIC`, `STBI_ONLY_PNG`, `STBI_ONLY_JPEG`, `STBI_NO_STDIO`. The
static flag keeps all `stbi_*` symbols private to that file, so another module
may define its own `STB_IMAGE_IMPLEMENTATION` without a duplicate-symbol clash.
Other code decodes through `decodeImage()` / `prepareImage()` in
`ImageDecode.hpp`.

## Scene

`Scene` is everything one frame needs (Swift `SceneState` + the meshes Swift
passes through `Renderer.sync`). The camera is passed separately to `render()`.

| Field | Meaning |
| --- | --- |
| `meshes` | `std::map<int, shared_ptr<const BodyMesh>>`: visible bodies by id. |
| `geometryVersion` | Bump whenever `meshes` changes. GPU buffers are re-synced only when it differs from the last frame's. |
| `selection` | `std::vector<Pick>` (body / face / edge). Body picks tint the body, face picks colour faces, edge picks colour edges. |
| `hover` | `std::optional<Pick>`; face hover tints, edge hover colours the edge (and forces that body's edges on). |
| `ghostBody` | Body drawn at 35 % opacity while a preview replaces it (-1 none). |
| `grid` | `LineBatch` drawn depth-tested before bodies (grid, world axes). |
| `overlay` | `LineBatch` drawn on top of everything (sketch, reference items, highlights, markers, measurements). |
| `displayMode` | `Shaded`, `Wireframe`, `XRay`, `Visualized`, `Zebra`, `Curvature`. |
| `showEdges`, `showHiddenEdges` | View ▸ Show Edges / Show Hidden Edges. |
| `clip` | Section plane `(nx, ny, nz, d)`; geometry with `dot(p, n) > d` is cut, exposed insides shade darker. |
| `appearance` | Per-body `Appearance` (colour, metallic, roughness, opacity). Missing ids use the default grey. Bodies with opacity < 1 (and the ghost) draw after opaque bodies and images. |
| `environment` | Background (+ gradient), key light intensity / azimuth / elevation, `groundShadow`, `castShadows`, `shadowSoftness`, `ambientOcclusion`, `occlusionStrength`, `smoothShading`. `showGrid` is for the controller only. |
| `cameraSettings` | Projection and FOV; applied to the camera when it doesn't already match (like Swift). |
| `images` | `ReferenceImage`s: encoded `png` bytes (PNG/JPEG) **or** decoded straight-alpha `rgba` + size, a `Plane` (origin, normal, xDir), world `width`/`height`, `opacity`, `decal`. Textures are cached by `contentKey`, or a fingerprint of the bytes when it's 0; images missing from a frame are freed. |

`BodyMesh` mirrors Swift `BodyMesh`: `positions`, `normals`, `vertexFace` (face
index per vertex), `indices` (uint32 triangles), `edgePositions` (line list),
`edgeIds` (edge index per edge vertex), `faceCount`, `edgeCount`,
`boundsMin/Max` (`computeBounds()` fills them from triangle and edge vertices)
and `shapeKey`. **GPU caching:** on a version change each body keeps its
buffers (and lazily built smooth normals) when its `shapeKey` is non-zero and
equal to the cached one; otherwise it is re-uploaded. Bodies that vanish are
freed.

## Camera

`Camera` is a value type (`target`, `distance`, `yaw`, `pitch`, `fovY`,
`orthographic`), identical in behaviour to `Camera.swift`. Sizes and points are
in **view points** with the origin top-left (`Size2`, `Vec2f`), the same units
as the Swift `CGSize`/`CGPoint` arguments.

- Gestures: `orbit(dx, dy)`, `pan(dx, dy, size)`, `zoom(factor)`,
  `zoom(factor, towards)` (zoom to the surface point under the pointer: pass
  `surfaceHit(...)->point`, as `ViewportController.zoom(_:at:)` does).
- Views: `look(Preset)`, `snapToNearestOrtho()`, `rotate(yawSteps, pitchSteps)`,
  `face(normal, origin, xDir)` (sketch plane), `fit(lo, hi, aspect)`.
- Settings: `settings()`, `setSettings()`, `matches()` (FOV clamped 5…120°).
- Maths: `forward/right/up/eye`, `viewMatrix`, `projection(aspect)`,
  `viewProjection(size)`, `ray(point, size)`, `project(p, size)` (nullopt
  behind a perspective camera), `worldPerPoint(size)`.

There are no animated camera transitions in the Swift app, so none here.
Matrices keep the Metal clip convention (z in [0, w]) so picking and
projection match the Apple app bit for bit; the shaders remap z to OpenGL's
[-w, w] (`glClip()` in `Shaders.cpp`), which makes the stored window depth equal
to Metal's NDC z.

## Renderer lifecycle

All calls on the thread with the current EGL context.

```cpp
sculpt::viewport::Renderer r;
// GLSurfaceView.onSurfaceCreated / after ANGLE context creation:
if (!r.init()) log(r.lastError());     // also call again after context loss
// onSurfaceChanged:
r.resize(widthPx, heightPx, density);  // density = pixels per point
// onDrawFrame (into the bound framebuffer, whole resize() area):
r.render(scene, camera);
// screenshot / thumbnails / MCP `screenshot`:
std::vector<uint8_t> rgba = r.snapshot(scene, camera, w, h);  // top row first
// before destroying a live context:
r.release();
```

- `init()` compiles the 11 programs, creates the shadow map (2048², or less if
  `GL_MAX_TEXTURE_SIZE` is smaller), helper textures, one VAO and streaming line
  buffers. Called while already initialised it assumes the old context is gone
  and drops (does not delete) the old handles and caches.
- `render()` saves the bound framebuffer, runs the Visualized prepasses into
  its own FBOs, rebinds it and draws. Use `viewSize()` (points) for Camera /
  picking sizes. If `resize()` was never called it falls back to `GL_VIEWPORT`.
- `snapshot()` renders through a 4× MSAA FBO (clamped to `GL_MAX_SAMPLES`),
  resolves with `glBlitFramebuffer`, reads back with `glReadPixels` and flips
  rows. Images above 2048 px a side are rendered in tiles (as in Swift); up to
  16384 px. It restores the previous framebuffer and viewport.
- On-screen anti-aliasing comes from the surface: request `EGL_SAMPLES = 4`
  (Swift uses `sampleCount = 4`).

Draw order per frame (same as `Renderer.encode`): gradient background → ground
contact shadows (Visualize; not X-Ray / wireframe) → shadow catcher (cast
shadows) → grid → opaque bodies (surfaces with polygon offset, then edges) →
reference images → translucent bodies and ghost → hidden edges (depth GREATER) →
overlay (no depth test). Visualized prepasses: key-light shadow map, AO
G-buffer, AO, depth-aware blur.

## Metal → ES 3.0 differences

| Metal | GLES port | Effect |
| --- | --- | --- |
| AO G-buffer writes world positions to an `rgba32Float` target | Normals to RGBA8 + a sampled `DEPTH_COMPONENT32F` texture; positions reconstructed with the inverse view-projection | Same occlusion without `EXT_color_buffer_float`; 8-bit normals are ample for AO. Raw / blurred AO are R8 instead of R16F. |
| Shadow map `depth32Float`, 4096² on Mac, 2048² on iOS | `DEPTH_COMPONENT24`, 2048², `sampler2DShadow` with hardware 2×2 PCF + the same 16-tap rotated Poisson kernel | Matches iOS. |
| Selected face / edge ids in a variable-length constant buffer | Per-body R8 mask texture (1024 ids per row), cached until the selection changes | No limit on selection size. |
| `setDepthBias(1, slopeScale: 1.5)` / `(0, 1.5)` | `glPolygonOffset(1.5, 1)` / `(1.5, 0)` | Same intent (edges win over their faces); unit scale differs slightly by driver. |
| Texture origin top-left | Shadow / screen lookups use `uv = ndc * 0.5 + 0.5` | Equivalent. Pixel noise patterns are mirrored vertically (invisible). |
| `[[front_facing]]` with CCW winding | `gl_FrontFacing`, `glFrontFace(GL_CCW)` | Same. |
| `fwidth` inside per-pixel branches (zebra, curvature) | Derivatives computed before the branches | Same output, defined behaviour. |
| Reference images via CoreGraphics (any format, sRGB-converted) | stb_image PNG/JPEG, box downscale to 4096, premultiplied, mipmapped | No colour-space conversion of tagged images. |

## Swift → C++ mapping

| Swift | C++ | Status |
| --- | --- | --- |
| `SceneState` | `Scene` (+ camera argument) | Ported. |
| `LineBatch` (`line`, `polyline`) | `LineBatch` (+ `append`, `clear`) | Ported. |
| `DisplayMode`, `drawsEdgesByDefault` | `DisplayMode`, `drawsEdgesByDefault()` | Ported (titles / symbols / keys are UI, left to front ends). |
| `BodyMesh`, `Pick`, `Appearance`, `EnvironmentSettings`, `CameraSettings`, `ReferenceImage`, `SketchPlane` | `BodyMesh`, `Pick`, `Appearance`, `EnvironmentSettings`, `CameraSettings`, `ReferenceImage`, `Plane` | Plain mirrors; the controller converts from the model types. |
| `Renderer.init(view:)` | `Renderer::init()` | Ported. |
| `Renderer.sync(meshes:version:)` | inside `render()`/`snapshot()` from `Scene::meshes` / `geometryVersion` | Ported, `shapeKey` cache kept. |
| `Renderer.draw(in:scene:)` | `Renderer::render(scene, camera)` | Ported. |
| `Renderer.image(scene:size:scale:)` | `Renderer::snapshot(scene, camera, w, h)` | Ported (tiles, MSAA). Returns RGBA bytes; PNG encoding is the caller's. |
| `frameUniforms`, `encode`, `drawBody`, `drawHiddenEdges`, `drawShadows`, `drawShadowCatcher`, `drawImages`, `drawLines`, `encodePrepasses`, `aoTextures`, `solidIds`, `normals(_:scene:)` | private members of the same names (`prepasses`, `ensureAoTargets`, `normalsFor`) | Ported. |
| `Renderer.backgroundColors`, `clearColor`, `keyLight`, `lightMatrix`, `smoothNormals` | `backgroundColors`, `keyLight`, `lightMatrix`, `smoothNormals` in `Lighting.hpp` | Ported. |
| `ReferenceImageCache` | `Renderer::imageTexture` + `ImageDecode.hpp` | Ported. |
| `Camera` (all members) | `Camera` | Ported. |
| `rayTriangle`, `rayHitsBox` | `rayTriangle`, `rayHitsBox` | Ported. |
| `ViewportController.surfaceHit(at:)` | `surfaceHit(meshes, camera, point, size)` | Ported (pure). |
| `ViewportController.edgeHit(at:tolerance:maxDepth:)` | `edgeHit(...)` | Ported (pure). |
| `ViewportController.pick(at:)` | `pick(meshes, camera, point, size, tolerance)`; `defaultEdgeTolerance(touch)` gives 14 / 6 pt | Ported (pure). |
| `ViewportController.gridStep` | `gridStep(camera, fixedStep)` | Ported. |
| `ViewportController.grid()` | `grid(camera, plane, step, sketching, showWorldAxes)` | Ported; the controller picks the plane (sketch plane, or XY moved to the model bottom for "grid at model bottom"). |
| `ViewportController.axesOnly()` | `axesOnly(camera, step)` | Ported. |
| `ViewportController.highlightEdges(_:)` | `highlightEdges(batch, meshes, selection, hover, camera, size)` | Ported. |
| point markers in `pointOverlay`, thick strokes in `sketchOverlay`/`measureOverlay` | `marker(...)`, `thickOffsets(...)` | Helpers only. |
| `ViewportController.scene()` | — | **Controller.** Builds `Scene` from the store: visible meshes, selection / hover, `displayAppearance`, environment (ground shadow off outside Visualize; Visualize forces `Visualized` unless zebra / curvature), camera settings, visible images (decals follow Show Decals), grid vs `axesOnly` choice. |
| `ViewportController.overlay()`: `referenceOverlay`, `measureOverlay`, `pointOverlay`, `sketchOverlay` | — | **Controller** (need the store / sketch session); they append to `Scene::overlay` via `LineBatch`. |
| `sectionPlane`, `sectionRange` | — | **Controller**; set `Scene::clip`. |
| `fitAll`, `zoom(_:at:)`, `faceSketchPlane`, `look`, `orbit`, `pan` | `Camera::fit/zoom/face/look/orbit/pan` | Camera maths ported; store lookups (bounds of visible meshes, sketch plane) stay in the controller. |
| Input (`tap`, `doubleTap`, `hover`, sketch drawing, point picks, measurements), `screenshot()` wrapper, `storeChanged` / first fit | — | **Controller.** |
