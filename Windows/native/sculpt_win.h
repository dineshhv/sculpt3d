/*
 * Windows-only additions to sculpt.dll: an OpenGL ES (ANGLE on Direct3D 11)
 * host for a WinUI 3 SwapChainPanel. AppCore stays platform-neutral; this file
 * and AngleHost.cpp are compiled into sculpt.dll by Windows/CMakeLists.txt.
 *
 * How it works
 * - One process-wide EGL display (ANGLE, D3D11 backend) and one GLES 3 context
 *   shared by every viewport. Its ID3D11Device is queried through
 *   EGL_EXT_device_query and used to create a DXGI composition swap chain per
 *   SwapChainPanel (BGRA8, flip-sequential, 2 buffers).
 * - The swap chain's back buffer is wrapped as an EGL pbuffer
 *   (eglCreatePbufferFromClientBuffer with EGL_D3D_TEXTURE_ANGLE). Each frame
 *   the core draws into a 4x MSAA framebuffer, which is resolved and blitted
 *   (flipped: GL rows go bottom-up, D3D rows top-down) into the pbuffer, then
 *   the swap chain is presented.
 * - Everything here runs on the UI thread (the thread that owns the panels).
 *   The UI thread is therefore also the core's GL thread, so MCP screenshots
 *   render synchronously (see AppCore/API.md "Threading").
 *
 * Status codes: SCULPT_WIN_OK, SCULPT_WIN_ERROR (see sculpt_win_last_error),
 * SCULPT_WIN_DEVICE_LOST (call sculpt_view_gl_release for every document,
 * then sculpt_win_reset_device, then sculpt_view_gl_init again once a host
 * renders with the new device generation).
 */
#ifndef SCULPT_WIN_H
#define SCULPT_WIN_H

#include "sculpt/sculpt_c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SculptWinHost SculptWinHost;

enum { SCULPT_WIN_OK = 0, SCULPT_WIN_ERROR = 1, SCULPT_WIN_DEVICE_LOST = 2 };

/* `swap_chain_panel` is any COM interface pointer of a
 * Microsoft.UI.Xaml.Controls.SwapChainPanel (e.g. its IInspectable); the host
 * queries ISwapChainPanelNative from it and keeps a reference. Returns NULL
 * with *error set (free it with sculpt_free) on failure. */
SCULPT_API SculptWinHost* sculpt_win_host_create(void* swap_chain_panel, char** error);
SCULPT_API void sculpt_win_host_destroy(SculptWinHost* host);

/* Size of the panel in physical pixels and its CompositionScaleX/Y. */
SCULPT_API int sculpt_win_host_resize(SculptWinHost* host, int width_px, int height_px, float scale_x, float scale_y);

/* Makes the shared context current with this host's surface (for
 * sculpt_view_gl_init / snapshots). */
SCULPT_API int sculpt_win_host_make_current(SculptWinHost* host);

/* Makes the context current, calls sculpt_view_render(doc) into the MSAA
 * framebuffer, resolves into the swap chain and presents. */
SCULPT_API int sculpt_win_host_render(SculptWinHost* host, SculptDoc* doc);

/* Makes the shared context current on a 1x1 pbuffer (e.g. before
 * sculpt_mcp_drain, so an MCP screenshot can render). Creates the device on
 * first use. */
SCULPT_API int sculpt_win_make_current(void);

/* Increases whenever the device/context is (re)created: documents must call
 * sculpt_view_gl_init again when it changes. 0 before the first device. */
SCULPT_API uint64_t sculpt_win_device_generation(void);

/* Drops the context, display and every host's swap chain after a device loss;
 * the next resize/render recreates them. */
SCULPT_API void sculpt_win_reset_device(void);

/* The last error message (free with sculpt_free), "" when none. */
SCULPT_API char* sculpt_win_last_error(void);

/* 1 (default): flip rows when copying into the swap chain. Set 0 if the
 * viewport shows upside down on some ANGLE build. Also read from the
 * SCULPT3D_FLIP_Y environment variable ("0" or "1") at first use. */
SCULPT_API void sculpt_win_set_flip_y(int flip);

#ifdef __cplusplus
}
#endif

#endif /* SCULPT_WIN_H */
