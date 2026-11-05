/*
 * Sculpt3D app core: the flat C API that the Android (JNI) and Windows (C#
 * P/Invoke) front ends call. Everything a window shows comes from here: the
 * document model and its replay, sketching, selection, the viewport (camera,
 * picking, input handling, OpenGL ES 3 rendering), file import/export and the
 * MCP server. Front ends only draw native chrome around it.
 *
 * Conventions
 * - Strings are UTF-8. Every `char*` returned by a function here is allocated by
 *   the core and must be released with sculpt_free(). Never NULL on success.
 * - Rich data crosses as JSON: sculpt_doc_state() returns a snapshot of
 *   everything the UI shows, and sculpt_doc_command() performs one action.
 *   The schemas are documented in AppCore/API.md.
 * - Thread safety: each document serialises calls with an internal mutex, so
 *   the UI thread may send commands while the GL thread renders.
 * - Coordinates passed to sculpt_view_* input functions are in view points
 *   (device-independent pixels, i.e. pixels / scale), origin top-left.
 * - Units are millimetres and degrees, Z up, as in the Apple app.
 */
#ifndef SCULPT_C_H
#define SCULPT_C_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(SCULPT_BUILD_DLL)
#    define SCULPT_API __declspec(dllexport)
#  else
#    define SCULPT_API __declspec(dllimport)
#  endif
#else
#  define SCULPT_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SculptDoc SculptDoc;

/* ---- Memory ------------------------------------------------------------ */

SCULPT_API void sculpt_free(void* p);

/* Core version string, e.g. "0.1.0". Static; do not free. */
SCULPT_API const char* sculpt_version(void);

/* ---- Documents --------------------------------------------------------- */

/* A new, empty document. `name` is shown in titles and MCP listings. */
SCULPT_API SculptDoc* sculpt_doc_create(const char* name);
SCULPT_API void sculpt_doc_destroy(SculptDoc* doc);

/* Loads a .sculpt3d (or legacy .o3d) file's bytes, replacing the history.
 * Returns NULL on success, or an error message (free it). */
SCULPT_API char* sculpt_doc_load(SculptDoc* doc, const char* data, size_t length);

/* The document as .sculpt3d JSON (byte-compatible with the Apple app). */
SCULPT_API char* sculpt_doc_save(SculptDoc* doc);

/* True when the history or settings changed since the last load/save/mark. */
SCULPT_API int sculpt_doc_is_dirty(SculptDoc* doc);
SCULPT_API void sculpt_doc_mark_saved(SculptDoc* doc);

SCULPT_API void sculpt_doc_set_name(SculptDoc* doc, const char* name);

/* ---- State and commands ------------------------------------------------ */

/* Increases whenever anything the UI shows changes. Cheap; poll per frame. */
SCULPT_API uint64_t sculpt_doc_revision(SculptDoc* doc);

/* Called (on whatever thread made the change, after the call that made it
 * returns its lock) when sculpt_doc_revision or sculpt_view_revision
 * increases. The host should hop to its UI thread, request a frame, and
 * re-read the state if sculpt_doc_revision moved. It may also be called while
 * the MCP server waits for a screenshot (to ask for a frame), so it must only
 * post, never block. */
typedef void (*SculptListener)(void* context);
SCULPT_API void sculpt_doc_set_listener(SculptDoc* doc, SculptListener listener, void* context);

/* JSON snapshot of the UI state (see API.md "State"). */
SCULPT_API char* sculpt_doc_state(SculptDoc* doc);

/* Performs one action. `json` is {"cmd": "<name>", ...arguments}. Returns
 * {"ok": true, ...result} or {"ok": false, "error": "..."} (see API.md "Commands"). */
SCULPT_API char* sculpt_doc_command(SculptDoc* doc, const char* json);

/* ---- Binary file exchange ---------------------------------------------- */

/* Exports bodies (all when the selection is empty) as "step", "iges", "stl",
 * "brep", "obj", "gltf", "glb", "usdz", "3mf", "svg" or "dxf" (drawing views)...
 * see API.md. Returns a buffer to free with sculpt_free, or NULL with
 * *error set (free it). */
SCULPT_API uint8_t* sculpt_doc_export(SculptDoc* doc, const char* format, const char* options_json,
                                      size_t* out_length, char** error);

/* Imports a CAD file from a filesystem path (STEP, IGES, STL, BREP, or a
 * .sculpt3d project). Returns NULL on success or an error message. */
SCULPT_API char* sculpt_doc_import_path(SculptDoc* doc, const char* path);

/* Imports an image (PNG/JPEG bytes). `as_decal`: 0 = reference image on the
 * selected plane or flat face (Insert > Image), 1 = decal on the selected flat
 * face (File > Import Image as Decal), 2 = terrain from a height map
 * (Insert > Terrain > From Height Map). Returns NULL or an error message. */
SCULPT_API char* sculpt_doc_import_image(SculptDoc* doc, const uint8_t* data, size_t length,
                                         const char* name, int as_decal);

/* ---- Viewport (OpenGL ES 3.0) ------------------------------------------ */

/* Call on the GL thread with a current ES 3.0 context (EGL on Android, ANGLE
 * on Windows). gl_init may be called again after context loss. */
SCULPT_API void sculpt_view_gl_init(SculptDoc* doc);
SCULPT_API void sculpt_view_gl_release(SculptDoc* doc);

/* Drawable size in pixels and the pixels-per-point scale. */
SCULPT_API void sculpt_view_resize(SculptDoc* doc, int width_px, int height_px, float scale);

/* Draws one frame into the currently bound framebuffer. */
SCULPT_API void sculpt_view_render(SculptDoc* doc);

/* True while animating (camera transitions) or after a change; hosts with
 * on-demand rendering redraw while this is set. */
SCULPT_API int sculpt_view_needs_redraw(SculptDoc* doc);

/* Renders an offscreen image (RGBA8, top row first) of the current view.
 * Must be called on the GL thread. Free with sculpt_free. */
SCULPT_API uint8_t* sculpt_view_snapshot(SculptDoc* doc, int width_px, int height_px, size_t* out_length);

/* Added: the same snapshot encoded as PNG bytes (GL thread). Free with sculpt_free.
 * NULL when there is no GL context. */
SCULPT_API uint8_t* sculpt_view_snapshot_png(SculptDoc* doc, int width_px, int height_px, size_t* out_length);

/* Added: increases whenever anything drawn in the viewport or placed over it
 * changes, camera moves included (sculpt_doc_revision does not move with the
 * camera). Hosts re-read sculpt_view_annotations when it changes, e.g. after
 * each rendered frame. */
SCULPT_API uint64_t sculpt_view_revision(SculptDoc* doc);

/* Added: JSON of the labels drawn over the viewport in view points (sketch
 * constraint glyphs, dimension values, the snapping hint, point-measurement
 * labels) plus the camera orientation for a view cube. See API.md
 * "Annotations". */
SCULPT_API char* sculpt_view_annotations(SculptDoc* doc);

enum {
  SCULPT_PHASE_DOWN = 0,
  SCULPT_PHASE_MOVE = 1,
  SCULPT_PHASE_UP = 2,
  SCULPT_PHASE_CANCEL = 3,
  SCULPT_PHASE_HOVER = 4, /* mouse/pen moved with no button down */
  SCULPT_PHASE_EXIT = 5   /* pointer left the view */
};

enum {
  SCULPT_POINTER_TOUCH = 0,
  SCULPT_POINTER_MOUSE = 1,
  SCULPT_POINTER_PEN = 2
};

enum {
  SCULPT_BUTTON_PRIMARY = 0,
  SCULPT_BUTTON_SECONDARY = 1,
  SCULPT_BUTTON_MIDDLE = 2
};

enum {
  SCULPT_MOD_SHIFT = 1,
  SCULPT_MOD_CONTROL = 2, /* Ctrl on Windows/Android keyboards (the Mac's Command) */
  SCULPT_MOD_ALT = 4
};

/* One pointer event. Single-pointer drags with the pen or mouse primary button
 * draw / drag in sketches and select or orbit otherwise, matching the Apple
 * app. `id` distinguishes simultaneous touches. Taps (including double taps)
 * are recognised by the core from down/up timing. */
SCULPT_API void sculpt_view_pointer(SculptDoc* doc, int id, int pointer_type, int button, int phase,
                                    float x, float y, int modifiers);

/* Gestures recognised by the host's gesture system (two-finger pinch/pan/rotate,
 * mouse wheel, trackpad). */
SCULPT_API void sculpt_view_orbit(SculptDoc* doc, float dx, float dy);
SCULPT_API void sculpt_view_pan(SculptDoc* doc, float dx, float dy);
/* factor > 1 zooms in, around (x, y) in points. */
SCULPT_API void sculpt_view_zoom(SculptDoc* doc, float factor, float x, float y);

/* A key press in the viewport: `key` is a single character ("l", "r", "1") or
 * a name ("Escape", "Delete", "Backspace", "Enter", "Tab", "Up", "Down",
 * "Left", "Right", "F"...). Returns 1 when the core handled it. */
SCULPT_API int sculpt_view_key(SculptDoc* doc, const char* key, int modifiers);

/* ---- MCP server --------------------------------------------------------- */

/* Starts the Model Context Protocol server on 127.0.0.1:port (7317 in the
 * Apple app) over every registered document. Requests run on a background
 * thread; each tool call is posted to the host through `post` so it runs on
 * the UI thread, where the host must call sculpt_mcp_drain(). Returns NULL on
 * success or an error message. */
typedef void (*SculptPost)(void* context);
SCULPT_API char* sculpt_mcp_start(int port, SculptPost post, void* context);
SCULPT_API void sculpt_mcp_stop(void);
SCULPT_API int sculpt_mcp_running(void);
SCULPT_API void sculpt_mcp_drain(void);

/* Documents the MCP server can see, in window order. The front-most document
 * should be activated so `select_document` defaults to it. */
SCULPT_API void sculpt_mcp_register(SculptDoc* doc);
SCULPT_API void sculpt_mcp_unregister(SculptDoc* doc);
SCULPT_API void sculpt_mcp_activate(SculptDoc* doc);

/* MCP asks the host to open a new window for `new_document`; the callback
 * must create a document, register it and return it. */
typedef SculptDoc* (*SculptNewDocument)(void* context, const char* name);
SCULPT_API void sculpt_mcp_set_new_document_handler(SculptNewDocument handler, void* context);

#ifdef __cplusplus
}
#endif

#endif /* SCULPT_C_H */
