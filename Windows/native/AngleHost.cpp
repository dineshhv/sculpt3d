// ANGLE (OpenGL ES 3 on Direct3D 11) host for WinUI 3 SwapChainPanels.
// See sculpt_win.h for the design. Compiled into sculpt.dll on Windows only.
//
// All functions must be called on the UI thread (the thread that owns the
// SwapChainPanels); no locking is done here.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#if defined(__has_include)
#if __has_include(<EGL/eglext_angle.h>)
#include <EGL/eglext_angle.h>
#endif
#endif
#include <GLES3/gl3.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "sculpt_win.h"

using Microsoft::WRL::ComPtr;

// ---- EGL / ANGLE constants, in case the headers in use lack them ----------
#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif
#ifndef EGL_PLATFORM_ANGLE_ANGLE
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE 0x3208
#endif
#ifndef EGL_DEVICE_EXT
#define EGL_DEVICE_EXT 0x322C
#endif
#ifndef EGL_D3D11_DEVICE_ANGLE
#define EGL_D3D11_DEVICE_ANGLE 0x33A1
#endif
#ifndef EGL_D3D_TEXTURE_ANGLE
#define EGL_D3D_TEXTURE_ANGLE 0x33A3
#endif
#ifndef EGL_FLEXIBLE_SURFACE_COMPATIBILITY_SUPPORTED_ANGLE
#define EGL_FLEXIBLE_SURFACE_COMPATIBILITY_SUPPORTED_ANGLE 0x33A6
#endif
#ifndef EGL_CONTEXT_LOST
#define EGL_CONTEXT_LOST 0x300E
#endif

// Function pointer types for the EXT entry points (looked up at run time).
typedef EGLDisplay(EGLAPIENTRY* GetPlatformDisplayEXTFn)(EGLenum platform, void* nativeDisplay, const EGLint* attribs);
typedef EGLBoolean(EGLAPIENTRY* QueryDisplayAttribEXTFn)(EGLDisplay display, EGLint attribute, EGLAttrib* value);
typedef EGLBoolean(EGLAPIENTRY* QueryDeviceAttribEXTFn)(void* device, EGLint attribute, EGLAttrib* value);

// ISwapChainPanelNative as declared by the Windows App SDK
// (microsoft.ui.xaml.media.dxinterop.h). WinUI 3 uses a different IID from the
// UWP interface (windows.ui.xaml.media.dxinterop.h), so it is declared here
// rather than taken from the Windows SDK.
MIDL_INTERFACE("63aad0b8-7c24-40ff-85a8-640d944cc325")
ISwapChainPanelNativeWinUI : public IUnknown {
 public:
  virtual HRESULT STDMETHODCALLTYPE SetSwapChain(IDXGISwapChain* swapChain) = 0;
};

namespace {

constexpr int kWantedSamples = 4;

struct Device {
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLConfig config = nullptr;
  EGLContext context = EGL_NO_CONTEXT;
  EGLSurface dummy = EGL_NO_SURFACE;  // 1x1 pbuffer to make the context current without a panel
  ComPtr<ID3D11Device> d3d;
  ComPtr<IDXGIFactory2> factory;
  bool flexibleSurfaces = false;  // EGL_ANGLE_flexible_surface_compatibility (BGRA texture, RGBA config)
  uint64_t generation = 0;
};

Device g_device;
std::string g_error;
std::vector<SculptWinHost*> g_hosts;
int g_flipY = -1;  // -1: not read from the environment yet

bool flipY() {
  if (g_flipY < 0) {
    g_flipY = 1;
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, "SCULPT3D_FLIP_Y") == 0 && value) {
      if (value[0] == '0') g_flipY = 0;
      std::free(value);
    }
  }
  return g_flipY != 0;
}

char* dup(const std::string& s) {
  char* out = static_cast<char*>(std::malloc(s.size() + 1));
  if (!out) return nullptr;
  std::memcpy(out, s.data(), s.size());
  out[s.size()] = 0;
  return out;
}

std::string hex(long v) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(v));
  return buf;
}

bool fail(const std::string& message) {
  g_error = message;
  return false;
}

bool failEgl(const std::string& what) { return fail(what + " failed (EGL error " + hex(long(eglGetError())) + ")"); }

bool hasExtension(const char* list, const char* name) {
  if (!list) return false;
  size_t n = std::strlen(name);
  for (const char* p = list; (p = std::strstr(p, name)) != nullptr; p += n) {
    bool startOk = p == list || p[-1] == ' ';
    bool endOk = p[n] == 0 || p[n] == ' ';
    if (startOk && endOk) return true;
  }
  return false;
}

bool deviceLost(HRESULT hr) { return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG; }

bool ensureDevice() {
  Device& d = g_device;
  if (d.context != EGL_NO_CONTEXT) return true;

  auto getPlatformDisplay = reinterpret_cast<GetPlatformDisplayEXTFn>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (!getPlatformDisplay) return fail("ANGLE: eglGetPlatformDisplayEXT is not available (is libEGL.dll from ANGLE?)");
  const EGLint displayAttribs[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE, EGL_NONE};
  EGLDisplay display = getPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, nullptr, displayAttribs);
  if (display == EGL_NO_DISPLAY) return failEgl("eglGetPlatformDisplayEXT(D3D11)");
  EGLint major = 0, minor = 0;
  if (!eglInitialize(display, &major, &minor)) return failEgl("eglInitialize (Direct3D 11)");

  // The D3D11 device ANGLE renders with: the swap chains must be made on it.
  auto queryDisplayAttrib = reinterpret_cast<QueryDisplayAttribEXTFn>(eglGetProcAddress("eglQueryDisplayAttribEXT"));
  auto queryDeviceAttrib = reinterpret_cast<QueryDeviceAttribEXTFn>(eglGetProcAddress("eglQueryDeviceAttribEXT"));
  if (!queryDisplayAttrib || !queryDeviceAttrib) {
    eglTerminate(display);
    return fail("ANGLE: EGL_EXT_device_query is not available");
  }
  EGLAttrib eglDevice = 0, d3dDevice = 0;
  if (!queryDisplayAttrib(display, EGL_DEVICE_EXT, &eglDevice) || !eglDevice) {
    eglTerminate(display);
    return failEgl("eglQueryDisplayAttribEXT(EGL_DEVICE_EXT)");
  }
  if (!queryDeviceAttrib(reinterpret_cast<void*>(eglDevice), EGL_D3D11_DEVICE_ANGLE, &d3dDevice) || !d3dDevice) {
    eglTerminate(display);
    return failEgl("eglQueryDeviceAttribEXT(EGL_D3D11_DEVICE_ANGLE)");
  }
  ComPtr<ID3D11Device> d3d = reinterpret_cast<ID3D11Device*>(d3dDevice);  // AddRef: ANGLE keeps its own reference

  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIFactory2> factory;
  HRESULT hr = d3d.As(&dxgiDevice);
  if (SUCCEEDED(hr)) hr = dxgiDevice->GetAdapter(&adapter);
  if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    eglTerminate(display);
    return fail("Couldn't get the DXGI factory of ANGLE's device (" + hex(hr) + ")");
  }

  const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
  if (!hasExtension(extensions, "EGL_ANGLE_d3d_texture_client_buffer")) {
    eglTerminate(display);
    return fail("ANGLE: EGL_ANGLE_d3d_texture_client_buffer is not supported");
  }

  const EGLint configAttribs[] = {EGL_RED_SIZE,     8,
                                  EGL_GREEN_SIZE,   8,
                                  EGL_BLUE_SIZE,    8,
                                  EGL_ALPHA_SIZE,   8,
                                  EGL_DEPTH_SIZE,   24,
                                  EGL_STENCIL_SIZE, 8,
                                  EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                  EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  if (!eglChooseConfig(display, configAttribs, &config, 1, &count) || count < 1) {
    eglTerminate(display);
    return failEgl("eglChooseConfig (RGBA8, depth 24, stencil 8, ES 3)");
  }
  const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
  if (context == EGL_NO_CONTEXT) {
    eglTerminate(display);
    return failEgl("eglCreateContext (OpenGL ES 3.0)");
  }
  const EGLint dummyAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  EGLSurface dummy = eglCreatePbufferSurface(display, config, dummyAttribs);
  if (dummy == EGL_NO_SURFACE) {
    eglDestroyContext(display, context);
    eglTerminate(display);
    return failEgl("eglCreatePbufferSurface (1x1)");
  }

  d.display = display;
  d.config = config;
  d.context = context;
  d.dummy = dummy;
  d.d3d = d3d;
  d.factory = factory;
  d.flexibleSurfaces = hasExtension(extensions, "EGL_ANGLE_flexible_surface_compatibility");
  ++d.generation;
  g_error.clear();
  return true;
}

}  // namespace

struct SculptWinHost {
  ComPtr<ISwapChainPanelNativeWinUI> panel;
  ComPtr<IDXGISwapChain1> swapChain;
  EGLSurface surface = EGL_NO_SURFACE;
  int width = 0, height = 0;        // requested size in pixels
  int chainWidth = 0, chainHeight = 0;  // size of the swap chain's buffers
  float scaleX = 1, scaleY = 1;
  uint64_t generation = 0;  // device generation of swapChain, surface and the framebuffers

  // Framebuffers (objects of the shared context).
  GLuint drawFbo = 0, drawColor = 0, drawDepth = 0;  // multisampled when samples > 0
  GLuint resolveFbo = 0, resolveColor = 0;
  int fboWidth = 0, fboHeight = 0, samples = -1;
};

namespace {

void makeDummyCurrent() {
  Device& d = g_device;
  if (d.context != EGL_NO_CONTEXT) eglMakeCurrent(d.display, d.dummy, d.dummy, d.context);
}

void destroySurface(SculptWinHost* h) {
  Device& d = g_device;
  if (h->surface == EGL_NO_SURFACE) return;
  // A current surface is only released when it stops being current.
  if (eglGetCurrentSurface(EGL_DRAW) == h->surface) makeDummyCurrent();
  eglDestroySurface(d.display, h->surface);
  h->surface = EGL_NO_SURFACE;
}

void deleteFramebuffers(SculptWinHost* h) {
  // Only valid while the context the names belong to is current.
  if (h->generation == g_device.generation && g_device.context != EGL_NO_CONTEXT) {
    makeDummyCurrent();
    if (h->drawFbo) glDeleteFramebuffers(1, &h->drawFbo);
    if (h->resolveFbo) glDeleteFramebuffers(1, &h->resolveFbo);
    GLuint rbs[3] = {h->drawColor, h->drawDepth, h->resolveColor};
    glDeleteRenderbuffers(3, rbs);
  }
  h->drawFbo = h->drawColor = h->drawDepth = h->resolveFbo = h->resolveColor = 0;
  h->fboWidth = h->fboHeight = 0;
  h->samples = -1;
}

/// Forgets everything made with an old device (its objects died with it).
void dropDeviceObjects(SculptWinHost* h) {
  h->surface = EGL_NO_SURFACE;
  h->swapChain.Reset();
  h->drawFbo = h->drawColor = h->drawDepth = h->resolveFbo = h->resolveColor = 0;
  h->fboWidth = h->fboHeight = 0;
  h->samples = -1;
  h->chainWidth = h->chainHeight = 0;
}

void applyScaleTransform(SculptWinHost* h) {
  // The panel stretches the swap chain to its size in DIPs; scale it back so
  // one buffer pixel is one screen pixel (CompositionScale = DPI scale * zoom).
  ComPtr<IDXGISwapChain2> chain2;
  if (h->swapChain && SUCCEEDED(h->swapChain.As(&chain2))) {
    DXGI_MATRIX_3X2_F m = {};
    m._11 = 1.0f / std::max(h->scaleX, 0.01f);
    m._22 = 1.0f / std::max(h->scaleY, 0.01f);
    chain2->SetMatrixTransform(&m);
  }
}

int createSwapChain(SculptWinHost* h) {
  Device& d = g_device;
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = UINT(h->width);
  desc.Height = UINT(h->height);
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.Stereo = FALSE;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
  desc.BufferCount = 2;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  desc.Flags = 0;
  ComPtr<IDXGISwapChain1> chain;
  HRESULT hr = d.factory->CreateSwapChainForComposition(d.d3d.Get(), &desc, nullptr, &chain);
  if (FAILED(hr)) {
    fail("CreateSwapChainForComposition failed (" + hex(hr) + ")");
    return deviceLost(hr) ? SCULPT_WIN_DEVICE_LOST : SCULPT_WIN_ERROR;
  }
  hr = h->panel->SetSwapChain(chain.Get());
  if (FAILED(hr)) {
    fail("ISwapChainPanelNative::SetSwapChain failed (" + hex(hr) + ")");
    return SCULPT_WIN_ERROR;
  }
  h->swapChain = chain;
  h->chainWidth = h->width;
  h->chainHeight = h->height;
  applyScaleTransform(h);
  return SCULPT_WIN_OK;
}

int createSurface(SculptWinHost* h) {
  Device& d = g_device;
  ComPtr<ID3D11Texture2D> buffer;
  // Flip-model swap chains in D3D11: buffer 0 always names the current back buffer.
  HRESULT hr = h->swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer));
  if (FAILED(hr)) {
    fail("IDXGISwapChain::GetBuffer failed (" + hex(hr) + ")");
    return deviceLost(hr) ? SCULPT_WIN_DEVICE_LOST : SCULPT_WIN_ERROR;
  }
  std::vector<EGLint> attribs = {EGL_WIDTH, h->chainWidth, EGL_HEIGHT, h->chainHeight};
  if (d.flexibleSurfaces) {
    attribs.push_back(EGL_FLEXIBLE_SURFACE_COMPATIBILITY_SUPPORTED_ANGLE);
    attribs.push_back(EGL_TRUE);
  }
  attribs.push_back(EGL_NONE);
  EGLSurface surface = eglCreatePbufferFromClientBuffer(d.display, EGL_D3D_TEXTURE_ANGLE,
                                                        reinterpret_cast<EGLClientBuffer>(buffer.Get()), d.config, attribs.data());
  if (surface == EGL_NO_SURFACE) {
    EGLint e = eglGetError();
    fail("eglCreatePbufferFromClientBuffer(EGL_D3D_TEXTURE_ANGLE) failed (EGL error " + hex(e) + ")");
    return e == EGL_CONTEXT_LOST ? SCULPT_WIN_DEVICE_LOST : SCULPT_WIN_ERROR;
  }
  h->surface = surface;
  return SCULPT_WIN_OK;
}

/// Brings the swap chain and its pbuffer to the requested size.
int prepare(SculptWinHost* h) {
  if (!ensureDevice()) return SCULPT_WIN_ERROR;
  Device& d = g_device;
  if (h->generation != d.generation) {
    dropDeviceObjects(h);
    h->generation = d.generation;
  }
  if (h->width <= 0 || h->height <= 0) return SCULPT_WIN_OK;
  if (!h->swapChain) {
    int st = createSwapChain(h);
    if (st != SCULPT_WIN_OK) return st;
  } else if (h->chainWidth != h->width || h->chainHeight != h->height) {
    destroySurface(h);
    HRESULT hr = h->swapChain->ResizeBuffers(2, UINT(h->width), UINT(h->height), DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    if (deviceLost(hr)) {
      fail("ResizeBuffers: the graphics device was lost (" + hex(hr) + ")");
      return SCULPT_WIN_DEVICE_LOST;
    }
    if (SUCCEEDED(hr)) {
      h->chainWidth = h->width;
      h->chainHeight = h->height;
      applyScaleTransform(h);
    } else {
      // ANGLE may still reference the old buffer; a new swap chain always works.
      h->swapChain.Reset();
      int st = createSwapChain(h);
      if (st != SCULPT_WIN_OK) return st;
    }
  }
  if (h->surface == EGL_NO_SURFACE) return createSurface(h);
  return SCULPT_WIN_OK;
}

int makeCurrent(SculptWinHost* h) {
  Device& d = g_device;
  EGLSurface s = h->surface != EGL_NO_SURFACE ? h->surface : d.dummy;
  if (!eglMakeCurrent(d.display, s, s, d.context)) {
    EGLint e = eglGetError();
    fail("eglMakeCurrent failed (EGL error " + hex(e) + ")");
    return e == EGL_CONTEXT_LOST ? SCULPT_WIN_DEVICE_LOST : SCULPT_WIN_ERROR;
  }
  return SCULPT_WIN_OK;
}

bool makeRenderbuffer(GLuint& rb, int samples, GLenum format, int w, int h) {
  glGenRenderbuffers(1, &rb);
  glBindRenderbuffer(GL_RENDERBUFFER, rb);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, format, w, h);
  return glGetError() == GL_NO_ERROR;
}

/// The MSAA framebuffer the core draws into and the single-sample one it is
/// resolved into, at the swap chain's size. Falls back to no MSAA.
bool ensureFramebuffers(SculptWinHost* h) {
  int w = h->chainWidth, ht = h->chainHeight;
  if (h->drawFbo && h->fboWidth == w && h->fboHeight == ht) return true;
  if (h->drawFbo) {
    glDeleteFramebuffers(1, &h->drawFbo);
    glDeleteFramebuffers(1, &h->resolveFbo);
    GLuint rbs[3] = {h->drawColor, h->drawDepth, h->resolveColor};
    glDeleteRenderbuffers(3, rbs);
    h->drawFbo = h->drawColor = h->drawDepth = h->resolveFbo = h->resolveColor = 0;
  }
  while (glGetError() != GL_NO_ERROR) {
  }
  GLint maxSamples = 0;
  glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
  int wanted = h->samples >= 0 ? h->samples : std::min(kWantedSamples, int(maxSamples));
  for (int samples : {wanted, 0}) {
    bool ok = makeRenderbuffer(h->drawColor, samples, GL_RGBA8, w, ht) &&
              makeRenderbuffer(h->drawDepth, samples, GL_DEPTH24_STENCIL8, w, ht) &&
              makeRenderbuffer(h->resolveColor, 0, GL_RGBA8, w, ht);
    glGenFramebuffers(1, &h->drawFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, h->drawFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, h->drawColor);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, h->drawDepth);
    ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glGenFramebuffers(1, &h->resolveFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, h->resolveFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, h->resolveColor);
    ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    if (ok) {
      h->samples = samples;
      h->fboWidth = w;
      h->fboHeight = ht;
      return true;
    }
    glDeleteFramebuffers(1, &h->drawFbo);
    glDeleteFramebuffers(1, &h->resolveFbo);
    GLuint rbs[3] = {h->drawColor, h->drawDepth, h->resolveColor};
    glDeleteRenderbuffers(3, rbs);
    h->drawFbo = h->drawColor = h->drawDepth = h->resolveFbo = h->resolveColor = 0;
    while (glGetError() != GL_NO_ERROR) {
    }
    if (samples == 0) break;
  }
  return fail("Couldn't create the viewport framebuffers");
}

}  // namespace

extern "C" {

SCULPT_API SculptWinHost* sculpt_win_host_create(void* swap_chain_panel, char** error) {
  if (error) *error = nullptr;
  if (!swap_chain_panel) {
    if (error) *error = dup("No SwapChainPanel");
    return nullptr;
  }
  auto* unknown = static_cast<IUnknown*>(swap_chain_panel);
  ComPtr<ISwapChainPanelNativeWinUI> native;
  HRESULT hr = unknown->QueryInterface(__uuidof(ISwapChainPanelNativeWinUI), reinterpret_cast<void**>(native.GetAddressOf()));
  if (FAILED(hr) || !native) {
    if (error) *error = dup("The SwapChainPanel has no ISwapChainPanelNative (" + hex(hr) + ")");
    return nullptr;
  }
  if (!ensureDevice()) {
    if (error) *error = dup(g_error);
    return nullptr;
  }
  auto* host = new SculptWinHost();
  host->panel = native;
  host->generation = g_device.generation;
  g_hosts.push_back(host);
  return host;
}

SCULPT_API void sculpt_win_host_destroy(SculptWinHost* host) {
  if (!host) return;
  g_hosts.erase(std::remove(g_hosts.begin(), g_hosts.end(), host), g_hosts.end());
  if (host->generation == g_device.generation) {
    destroySurface(host);
    deleteFramebuffers(host);
  }
  if (host->panel) host->panel->SetSwapChain(nullptr);
  host->swapChain.Reset();
  host->panel.Reset();
  delete host;
}

SCULPT_API int sculpt_win_host_resize(SculptWinHost* host, int width_px, int height_px, float scale_x, float scale_y) {
  if (!host) return SCULPT_WIN_ERROR;
  host->width = std::max(width_px, 0);
  host->height = std::max(height_px, 0);
  bool scaleChanged = scale_x != host->scaleX || scale_y != host->scaleY;
  host->scaleX = scale_x > 0 ? scale_x : 1;
  host->scaleY = scale_y > 0 ? scale_y : 1;
  int st = prepare(host);
  if (st == SCULPT_WIN_OK && scaleChanged) applyScaleTransform(host);
  return st;
}

SCULPT_API int sculpt_win_host_make_current(SculptWinHost* host) {
  if (!host) return SCULPT_WIN_ERROR;
  int st = prepare(host);
  if (st != SCULPT_WIN_OK) return st;
  return makeCurrent(host);
}

SCULPT_API int sculpt_win_host_render(SculptWinHost* host, SculptDoc* doc) {
  if (!host || !doc) return SCULPT_WIN_ERROR;
  int st = prepare(host);
  if (st != SCULPT_WIN_OK) return st;
  if (host->surface == EGL_NO_SURFACE) return SCULPT_WIN_OK;  // zero size: nothing to show
  st = makeCurrent(host);
  if (st != SCULPT_WIN_OK) return st;
  if (!ensureFramebuffers(host)) return SCULPT_WIN_ERROR;

  int w = host->chainWidth, h = host->chainHeight;
  glBindFramebuffer(GL_FRAMEBUFFER, host->drawFbo);
  glViewport(0, 0, w, h);
  sculpt_view_render(doc);  // draws into the bound (MSAA) framebuffer

  // Resolve, then copy into the swap chain's buffer, flipping rows: GL's row 0
  // is the bottom, the D3D texture's row 0 is the top.
  glDisable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, host->drawFbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, host->resolveFbo);
  glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, host->resolveFbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  if (flipY()) {
    glBlitFramebuffer(0, 0, w, h, 0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  } else {
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glFlush();

  HRESULT hr = host->swapChain->Present(1, 0);
  if (deviceLost(hr)) {
    fail("Present: the graphics device was lost (" + hex(hr) + ")");
    return SCULPT_WIN_DEVICE_LOST;
  }
  if (FAILED(hr)) {
    fail("Present failed (" + hex(hr) + ")");
    return SCULPT_WIN_ERROR;
  }
  return SCULPT_WIN_OK;
}

SCULPT_API int sculpt_win_make_current(void) {
  if (!ensureDevice()) return SCULPT_WIN_ERROR;
  Device& d = g_device;
  // Keep the current panel surface when there is one: only the context matters.
  if (eglGetCurrentContext() == d.context) return SCULPT_WIN_OK;
  if (!eglMakeCurrent(d.display, d.dummy, d.dummy, d.context)) {
    EGLint e = eglGetError();
    fail("eglMakeCurrent failed (EGL error " + hex(e) + ")");
    return e == EGL_CONTEXT_LOST ? SCULPT_WIN_DEVICE_LOST : SCULPT_WIN_ERROR;
  }
  return SCULPT_WIN_OK;
}

SCULPT_API uint64_t sculpt_win_device_generation(void) { return g_device.generation; }

SCULPT_API void sculpt_win_reset_device(void) {
  Device& d = g_device;
  if (d.display == EGL_NO_DISPLAY) return;
  for (SculptWinHost* h : g_hosts) {
    if (h->surface != EGL_NO_SURFACE) eglDestroySurface(d.display, h->surface);
    dropDeviceObjects(h);
  }
  eglMakeCurrent(d.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (d.dummy != EGL_NO_SURFACE) eglDestroySurface(d.display, d.dummy);
  if (d.context != EGL_NO_CONTEXT) eglDestroyContext(d.display, d.context);
  eglTerminate(d.display);
  uint64_t generation = d.generation;
  d = Device();
  d.generation = generation;  // ensureDevice increments it for the new device
}

SCULPT_API char* sculpt_win_last_error(void) { return dup(g_error); }

SCULPT_API void sculpt_win_set_flip_y(int flip) { g_flipY = flip ? 1 : 0; }

}  // extern "C"
