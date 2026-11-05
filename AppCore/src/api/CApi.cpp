// The C API (include/sculpt/sculpt_c.h): documents, files, viewport and
// input. State and commands are in State.cpp / Commands.cpp, MCP in CApiMcp.cpp.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "api/Document.hpp"
#include "api/McpDocument.hpp"
#include "sculpt/model/Image.hpp"
#include "sculpt/model/Sculpt3DFile.hpp"

using namespace sculpt;
using namespace sculpt::api;
using json = nlohmann::json;

namespace sculpt::api {

char* dupString(const std::string& s) {
  char* out = static_cast<char*>(std::malloc(s.size() + 1));
  if (!out) return nullptr;
  std::memcpy(out, s.data(), s.size());
  out[s.size()] = 0;
  return out;
}

static uint8_t* dupBytes(const std::vector<uint8_t>& v, size_t* outLength) {
  uint8_t* out = static_cast<uint8_t*>(std::malloc(std::max<size_t>(v.size(), 1)));
  if (!out) return nullptr;
  if (!v.empty()) std::memcpy(out, v.data(), v.size());
  if (outLength) *outLength = v.size();
  return out;
}

Call::~Call() {
  if (!d_) return;
  bool notify = false;
  if (d_->lock.depth() == 1) {
    d_->sync();
    notify = d_->takeNotification();
  }
  SculptListener listener = d_->listener;
  void* context = d_->listenerContext;
  d_->lock.unlock();
  if (notify && listener) listener(context);
}

}  // namespace sculpt::api

// MARK: SculptDoc

SculptDoc::SculptDoc(const std::string& name) {
  store = std::make_unique<ModelStore>();
  store->setDocumentName(name.empty() ? "Untitled" : name);
  store->onFeaturesChanged = [this](const std::vector<Feature>&) { dirty = true; };
  store->onSettingsChanged = [this](const ProjectSettings&) { dirty = true; };
  view = std::make_unique<ViewportController>(*store);
  mcp = std::make_unique<McpDocument>(this);
  sync();
  notifiedRevision = revision();
  notifiedViewRevision = view->viewRevision();
}

SculptDoc::~SculptDoc() {
  // Wake any screenshot waiter.
  std::lock_guard<std::mutex> l(snapshotMutex);
  for (auto& r : snapshotRequests) {
    std::lock_guard<std::mutex> rl(r->m);
    r->done = true;
    r->error = "The document was closed";
    r->cv.notify_all();
  }
}

void SculptDoc::sync() {
  // storeChanged may change the store itself (facing a new sketch turns its
  // plane), so settle in a few rounds.
  for (int i = 0; i < 3 && syncedStoreRevision != store->revision(); ++i) {
    syncedStoreRevision = store->revision();
    view->storeChanged();
  }
}

bool SculptDoc::takeNotification() {
  uint64_t r = revision(), v = view->viewRevision();
  bool due = r != notifiedRevision || v != notifiedViewRevision;
  notifiedRevision = r;
  notifiedViewRevision = v;
  return due;
}

void SculptDoc::notifyListener() {
  if (listener) listener(listenerContext);
}

std::vector<uint8_t> SculptDoc::renderSnapshot(int width, int height, std::string& error) {
#if SCULPT_WITH_GL
  if (!renderer || !glReady) {
    error = "The viewport has no GL context";
    return {};
  }
  sync();
  auto rgba = renderer->snapshot(view->scene(), view->camera, width, height);
  if (rgba.empty()) error = renderer->lastError().empty() ? "Rendering failed" : renderer->lastError();
  return rgba;
#else
  (void)width;
  (void)height;
  error = "Rendering is not available in this build (no GL)";
  return {};
#endif
}

void SculptDoc::serveSnapshots() {
  std::vector<std::shared_ptr<SnapshotRequest>> requests;
  {
    std::lock_guard<std::mutex> l(snapshotMutex);
    requests.swap(snapshotRequests);
  }
  for (auto& r : requests) {
    std::string error;
    auto rgba = renderSnapshot(r->width, r->height, error);
    std::lock_guard<std::mutex> rl(r->m);
    r->rgba = std::move(rgba);
    r->error = error;
    r->done = true;
    r->cv.notify_all();
  }
}

std::vector<uint8_t> SculptDoc::screenshotPng(double scale, std::string& error) {
  if (!std::isfinite(scale) || scale <= 0) scale = 2;
  vp::Size2 size = view->size();
  int w = std::max(1, int(std::lround(size.width * scale)));
  int h = std::max(1, int(std::lround(size.height * scale)));
  std::vector<uint8_t> rgba;
#if SCULPT_WITH_GL
  if (!glReady) {
    error = "The viewport isn't showing yet (no GL context)";
    return {};
  }
  if (std::this_thread::get_id() == glThread) {
    rgba = renderSnapshot(w, h, error);
  } else {
    auto request = std::make_shared<SnapshotRequest>();
    request->width = w;
    request->height = h;
    {
      std::lock_guard<std::mutex> l(snapshotMutex);
      snapshotRequests.push_back(request);
    }
    view->redraw();
    notifyListener();  // hosts render on demand: this asks for a frame
    int levels = lock.releaseAll();
    {
      std::unique_lock<std::mutex> l(request->m);
      request->cv.wait_for(l, std::chrono::seconds(10), [&] { return request->done; });
    }
    lock.reacquire(levels);
    std::lock_guard<std::mutex> l(request->m);
    if (!request->done) {
      std::lock_guard<std::mutex> sl(snapshotMutex);
      snapshotRequests.erase(std::remove(snapshotRequests.begin(), snapshotRequests.end(), request),
                             snapshotRequests.end());
      error = "Timed out waiting for the viewport to render";
      return {};
    }
    rgba = std::move(request->rgba);
    error = request->error;
  }
#else
  (void)w;
  (void)h;
  error = "Rendering is not available in this build (no GL)";
  return {};
#endif
  if (rgba.empty()) {
    if (error.empty()) error = "Rendering failed";
    return {};
  }
  RGBAImage image;
  image.width = w;
  image.height = h;
  image.pixels = std::move(rgba);
  return encodePNG(image);
}

// MARK: C API

extern "C" {

SCULPT_API void sculpt_free(void* p) { std::free(p); }

SCULPT_API const char* sculpt_version(void) { return "0.1.0"; }

SCULPT_API SculptDoc* sculpt_doc_create(const char* name) {
  try {
    return new SculptDoc(name ? name : "Untitled");
  } catch (...) {
    return nullptr;
  }
}

SCULPT_API void sculpt_doc_destroy(SculptDoc* doc) {
  if (!doc) return;
  sculpt_mcp_unregister(doc);
  delete doc;
}

SCULPT_API char* sculpt_doc_load(SculptDoc* doc, const char* data, size_t length) {
  if (!doc) return dupString("No document");
  if (!data) return dupString("No data");
  Call call(doc);
  try {
    Sculpt3DFile file = Sculpt3DFile::decode(std::string(data, length));
    if (doc->store->sketch()) doc->store->cancelSketch();
    if (doc->store->draft()) doc->store->cancelDraft();
    doc->store->setSelection({});
    doc->store->setItemSelection({});
    doc->store->reload(std::move(file.features), std::move(file.settings));
    // A freshly opened document: new view state, framed once on its first change.
    vp::Size2 size = doc->view->size();
    bool sized = doc->pixelWidth > 0;
    doc->view = std::make_unique<ViewportController>(*doc->store);
    if (sized) doc->view->setSize(size.width, size.height);
    doc->syncedStoreRevision = ~0ull;
    doc->sync();
    doc->view->stateChanged();
    doc->dirty = false;
    return nullptr;
  } catch (const std::exception& e) {
    return dupString(std::string("The file couldn't be opened: ") + e.what());
  } catch (...) {
    return dupString("The file couldn't be opened");
  }
}

SCULPT_API char* sculpt_doc_save(SculptDoc* doc) {
  if (!doc) return dupString("");
  Call call(doc);
  Sculpt3DFile file{doc->store->features(), doc->store->settings()};
  return dupString(file.encoded());
}

SCULPT_API int sculpt_doc_is_dirty(SculptDoc* doc) {
  if (!doc) return 0;
  Call call(doc);
  return doc->dirty ? 1 : 0;
}

SCULPT_API void sculpt_doc_mark_saved(SculptDoc* doc) {
  if (!doc) return;
  Call call(doc);
  doc->dirty = false;
  doc->view->stateChanged();
}

SCULPT_API void sculpt_doc_set_name(SculptDoc* doc, const char* name) {
  if (!doc) return;
  Call call(doc);
  doc->store->setDocumentName(name && *name ? name : "Untitled");
}

SCULPT_API uint64_t sculpt_doc_revision(SculptDoc* doc) {
  if (!doc) return 0;
  Call call(doc);
  return doc->revision();
}

SCULPT_API void sculpt_doc_set_listener(SculptDoc* doc, SculptListener listener, void* context) {
  if (!doc) return;
  Call call(doc);
  doc->listener = listener;
  doc->listenerContext = context;
}

SCULPT_API char* sculpt_doc_state(SculptDoc* doc) {
  if (!doc) return dupString("{}");
  Call call(doc);
  try {
    doc->sync();
    return dupString(documentState(*doc).dump());
  } catch (const std::exception& e) {
    return dupString(json{{"error", e.what()}}.dump());
  }
}

SCULPT_API char* sculpt_doc_command(SculptDoc* doc, const char* text) {
  if (!doc) return dupString(R"({"ok":false,"error":"No document"})");
  Call call(doc);
  json result;
  try {
    json cmd = json::parse(text ? text : "");
    result = runCommand(*doc, cmd);
  } catch (const json::parse_error& e) {
    result = {{"ok", false}, {"error", std::string("Invalid JSON: ") + e.what()}};
  } catch (const std::exception& e) {
    result = {{"ok", false}, {"error", e.what()}};
  }
  return dupString(result.dump());
}

// MARK: Files

SCULPT_API uint8_t* sculpt_doc_export(SculptDoc* doc, const char* format, const char* options_json, size_t* out_length,
                                      char** error) {
  auto fail = [&](const std::string& message) -> uint8_t* {
    if (error) *error = dupString(message);
    if (out_length) *out_length = 0;
    return nullptr;
  };
  if (error) *error = nullptr;
  if (!doc) return fail("No document");
  Call call(doc);
  try {
    json options = options_json && *options_json ? json::parse(options_json) : json::object();
    std::string raw = format ? format : "";
    auto kind = exportKind(raw);
    if (!kind) return fail("Unknown export format \"" + raw + "\"");
    if (!isAvailable(*kind)) return fail(title(*kind) + " export isn't available: " + detail(*kind));
    std::optional<DrawingSettings> drawing;
    if (options.contains("drawing")) {
      std::string err;
      drawing = parseDrawingSettings(options["drawing"], err, doc->store->documentName());
      if (!drawing) return fail(err);
    }
    auto previousToast = doc->store->toast();
    auto data = doc->store->exportData(*kind, drawing);
    if (!data) {
      auto toast = doc->store->toast();
      return fail(toast && toast != previousToast ? *toast : "Nothing to export");
    }
    return dupBytes(*data, out_length);
  } catch (const std::exception& e) {
    return fail(e.what());
  }
}

static bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

static std::string baseName(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = name.find_last_of('.');
  return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

static std::string extension(const std::string& path) {
  size_t dot = path.find_last_of('.');
  size_t slash = path.find_last_of("/\\");
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
  std::string e = path.substr(dot + 1);
  for (auto& c : e) c = char(std::tolower((unsigned char)c));
  return e;
}

}  // extern "C"

namespace sculpt::api {
/// Imports one file by path (shared with the "import.file" command). Returns "" or an error.
std::string importPath(SculptDoc& doc, const std::string& path) {
  ModelStore& store = *doc.store;
  if (store.draft() || store.sketch()) return "Finish the open sketch or tool first";
  std::string ext = extension(path);
  size_t before = store.features().size();
  auto previousToast = store.toast();
  if (ext == "sculpt3d" || ext == "o3d") {
    std::string bytes;
    if (!readFile(path, bytes)) return "Couldn't read " + path;
    store.insertProject(bytes, baseName(path));
  } else if (ext == "step" || ext == "stp" || ext == "iges" || ext == "igs" || ext == "stl" || ext == "brep" ||
             ext == "brp") {
    store.importFile(path);
  } else {
    return "Unsupported file type \"." + ext + "\" (STEP, IGES, STL, BREP or a Sculpt3D project)";
  }
  if (store.features().size() == before) {
    auto toast = store.toast();
    return toast && toast != previousToast ? *toast : "Nothing was imported from " + path;
  }
  // ContentView: importing frames the model.
  doc.view->fitAll();
  return "";
}

/// Inserts an image: 0 reference image, 1 decal on the selected face, 2 terrain height map.
std::string importImage(SculptDoc& doc, const std::vector<uint8_t>& bytes, const std::string& name, int as) {
  ModelStore& store = *doc.store;
  if (store.draft() || store.sketch()) return "Finish the open sketch or tool first";
  if (bytes.empty()) return "No image data";
  size_t before = store.features().size();
  auto previousToast = store.toast();
  std::string n = name.empty() ? "Image" : name;
  if (as == 1) {
    store.insertDecal(bytes, n);
  } else if (as == 2) {
    store.insertTerrain(bytes, n);
  } else {
    store.insertImage(bytes, n);
  }
  if (store.features().size() == before && !store.draft()) {
    auto toast = store.toast();
    return toast && toast != previousToast ? *toast : "The image couldn't be inserted";
  }
  return "";
}
}  // namespace sculpt::api

extern "C" {

SCULPT_API char* sculpt_doc_import_path(SculptDoc* doc, const char* path) {
  if (!doc) return dupString("No document");
  if (!path || !*path) return dupString("No path");
  Call call(doc);
  try {
    std::string err = importPath(*doc, path);
    return err.empty() ? nullptr : dupString(err);
  } catch (const std::exception& e) {
    return dupString(e.what());
  }
}

SCULPT_API char* sculpt_doc_import_image(SculptDoc* doc, const uint8_t* data, size_t length, const char* name,
                                         int as_decal) {
  if (!doc) return dupString("No document");
  Call call(doc);
  try {
    std::vector<uint8_t> bytes(data, data + (data ? length : 0));
    std::string err = importImage(*doc, bytes, name ? name : "", as_decal);
    return err.empty() ? nullptr : dupString(err);
  } catch (const std::exception& e) {
    return dupString(e.what());
  }
}

// MARK: Viewport

SCULPT_API void sculpt_view_gl_init(SculptDoc* doc) {
  if (!doc) return;
  Call call(doc);
  doc->glThread = std::this_thread::get_id();
#if SCULPT_WITH_GL
  if (!doc->renderer) doc->renderer = std::make_unique<vp::Renderer>();
  doc->glReady = doc->renderer->init();
  doc->glError = doc->glReady ? "" : doc->renderer->lastError();
  if (doc->glReady && doc->pixelWidth > 0) doc->renderer->resize(doc->pixelWidth, doc->pixelHeight, doc->pixelScale);
#else
  doc->glReady = false;
  doc->glError = "Built without GL";
#endif
  doc->view->redraw();
}

SCULPT_API void sculpt_view_gl_release(SculptDoc* doc) {
  if (!doc) return;
  Call call(doc);
#if SCULPT_WITH_GL
  if (doc->renderer) doc->renderer->release();
#endif
  doc->glReady = false;
}

SCULPT_API void sculpt_view_resize(SculptDoc* doc, int width_px, int height_px, float scale) {
  if (!doc) return;
  Call call(doc);
  if (!(scale > 0) || !std::isfinite(scale)) scale = 1;
  doc->pixelWidth = std::max(width_px, 1);
  doc->pixelHeight = std::max(height_px, 1);
  doc->pixelScale = scale;
#if SCULPT_WITH_GL
  if (doc->renderer && doc->glReady) doc->renderer->resize(doc->pixelWidth, doc->pixelHeight, scale);
#endif
  doc->view->setSize(float(doc->pixelWidth) / scale, float(doc->pixelHeight) / scale);
}

SCULPT_API void sculpt_view_render(SculptDoc* doc) {
  if (!doc) return;
  Call call(doc);
  doc->glThread = std::this_thread::get_id();
  doc->sync();
#if SCULPT_WITH_GL
  if (doc->renderer && doc->glReady) doc->renderer->render(doc->view->scene(), doc->view->camera);
#endif
  doc->view->clearNeedsRedraw();
  doc->serveSnapshots();
}

SCULPT_API int sculpt_view_needs_redraw(SculptDoc* doc) {
  if (!doc) return 0;
  Call call(doc);
  std::lock_guard<std::mutex> l(doc->snapshotMutex);
  return doc->view->needsRedraw() || !doc->snapshotRequests.empty() ? 1 : 0;
}

SCULPT_API uint8_t* sculpt_view_snapshot(SculptDoc* doc, int width_px, int height_px, size_t* out_length) {
  if (out_length) *out_length = 0;
  if (!doc || width_px <= 0 || height_px <= 0) return nullptr;
  Call call(doc);
  std::string error;
  auto rgba = doc->renderSnapshot(width_px, height_px, error);
  if (rgba.empty()) return nullptr;
  return dupBytes(rgba, out_length);
}

SCULPT_API uint8_t* sculpt_view_snapshot_png(SculptDoc* doc, int width_px, int height_px, size_t* out_length) {
  if (out_length) *out_length = 0;
  if (!doc || width_px <= 0 || height_px <= 0) return nullptr;
  Call call(doc);
  std::string error;
  auto rgba = doc->renderSnapshot(width_px, height_px, error);
  if (rgba.empty()) return nullptr;
  RGBAImage image;
  image.width = width_px;
  image.height = height_px;
  image.pixels = std::move(rgba);
  return dupBytes(encodePNG(image), out_length);
}

SCULPT_API uint64_t sculpt_view_revision(SculptDoc* doc) {
  if (!doc) return 0;
  Call call(doc);
  return doc->view->viewRevision();
}

SCULPT_API char* sculpt_view_annotations(SculptDoc* doc) {
  if (!doc) return dupString("{}");
  Call call(doc);
  doc->sync();
  return dupString(doc->view->annotations().dump());
}

static bool validPoint(float x, float y) { return std::isfinite(x) && std::isfinite(y); }

SCULPT_API void sculpt_view_pointer(SculptDoc* doc, int id, int pointer_type, int button, int phase, float x, float y,
                                    int modifiers) {
  if (!doc || !validPoint(x, y)) return;
  if (pointer_type < 0 || pointer_type > 2 || phase < 0 || phase > 5) return;
  Call call(doc);
  doc->view->pointer(id, PointerKind(pointer_type), button, phase, {x, y}, modifiers);
}

SCULPT_API void sculpt_view_orbit(SculptDoc* doc, float dx, float dy) {
  if (!doc || !validPoint(dx, dy)) return;
  Call call(doc);
  doc->view->orbit(dx, dy);
}

SCULPT_API void sculpt_view_pan(SculptDoc* doc, float dx, float dy) {
  if (!doc || !validPoint(dx, dy)) return;
  Call call(doc);
  doc->view->pan(dx, dy);
}

SCULPT_API void sculpt_view_zoom(SculptDoc* doc, float factor, float x, float y) {
  if (!doc || !(factor > 0) || !std::isfinite(factor)) return;
  Call call(doc);
  std::optional<vp::Vec2f> at;
  if (validPoint(x, y) && x >= 0 && y >= 0) at = vp::Vec2f{x, y};
  // The C API's factor > 1 zooms in; the camera's shrinks the distance by it.
  doc->view->zoom(1.0f / factor, at);
}

SCULPT_API int sculpt_view_key(SculptDoc* doc, const char* key, int modifiers) {
  if (!doc || !key) return 0;
  Call call(doc);
  try {
    return doc->view->key(key, modifiers) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

}  // extern "C"
