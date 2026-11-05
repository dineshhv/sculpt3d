// sculpt_mcp_* (include/sculpt/sculpt_c.h): thin wrappers over
// sculpt::mcp::Server (src/mcp/McpHost.hpp), plus the Document adapter that
// lets the server reach a C API document's store, camera and screenshots.
//
// Locking: tool jobs run inside sculpt_mcp_drain() on the host UI thread.
// While draining, every registered document is locked (in address order, so
// it can't deadlock against a GL thread that only ever takes one), so the GL
// thread can't render a store mid-change. A screenshot releases its
// document's lock while it waits for the GL thread to render.
#include <algorithm>
#include <mutex>
#include <vector>

#include "api/Document.hpp"
#include "api/McpDocument.hpp"

using namespace sculpt;
using namespace sculpt::api;

namespace {

std::mutex g_mutex;
std::vector<SculptDoc*> g_registered;
SculptPost g_post = nullptr;
void* g_postContext = nullptr;
SculptNewDocument g_newDocument = nullptr;
void* g_newDocumentContext = nullptr;
/// Documents locked by the drain in progress (UI thread only).
std::vector<SculptDoc*>* g_draining = nullptr;

}  // namespace

namespace sculpt::api {

std::string McpDocument::name() const { return doc->store->documentName(); }

ModelStore& McpDocument::store() { return *doc->store; }

void McpDocument::setView(const sculpt::mcp::ViewRequest& r) {
  ViewportController& v = *doc->view;
  doc->sync();
  if (r.preset) {
    const std::string& p = *r.preset;
    if (p == "iso") v.look(vp::Camera::Preset::Iso);
    else if (p == "top") v.look(vp::Camera::Preset::Top);
    else if (p == "front") v.look(vp::Camera::Preset::Front);
    else if (p == "right") v.look(vp::Camera::Preset::Right);
    else if (p == "back") v.look(vp::Camera::Preset::Back);
    else if (p == "left") v.look(vp::Camera::Preset::Left);
    else if (p == "bottom") v.look(vp::Camera::Preset::Bottom);
  }
  if (r.fit) v.fitAll();
  if (r.yawRadians || r.pitchRadians)
    v.setOrientation(r.yawRadians ? float(*r.yawRadians) : v.camera.yaw,
                     r.pitchRadians ? float(*r.pitchRadians) : v.camera.pitch);
  if (r.zoom && *r.zoom > 0) v.zoom(float(*r.zoom));
  if (r.displayMode)
    if (auto m = displayMode(*r.displayMode)) v.setDisplayMode(*m);
  v.redraw();
}

void McpDocument::redraw() { doc->view->redraw(); }

std::vector<uint8_t> McpDocument::screenshotPng(double scale, std::string& error) {
  doc->sync();
  return doc->screenshotPng(scale, error);
}

}  // namespace sculpt::api

extern "C" {

SCULPT_API char* sculpt_mcp_start(int port, SculptPost post, void* context) {
  {
    std::lock_guard<std::mutex> l(g_mutex);
    g_post = post;
    g_postContext = context;
  }
  std::string error = sculpt::mcp::Server::shared().start(port > 0 ? port : sculpt::mcp::Server::kDefaultPort, [] {
    SculptPost p;
    void* c;
    {
      std::lock_guard<std::mutex> l(g_mutex);
      p = g_post;
      c = g_postContext;
    }
    if (p) p(c);
  });
  return error.empty() ? nullptr : dupString(error);
}

SCULPT_API void sculpt_mcp_stop(void) { sculpt::mcp::Server::shared().stop(); }

SCULPT_API int sculpt_mcp_running(void) { return sculpt::mcp::Server::shared().running() ? 1 : 0; }

SCULPT_API void sculpt_mcp_drain(void) {
  std::vector<SculptDoc*> docs;
  {
    std::lock_guard<std::mutex> l(g_mutex);
    docs = g_registered;
  }
  std::sort(docs.begin(), docs.end());
  for (SculptDoc* d : docs) d->lock.lock();
  for (SculptDoc* d : docs) d->sync();
  std::vector<SculptDoc*>* previous = g_draining;
  g_draining = &docs;
  try {
    sculpt::mcp::Server::shared().drain();
  } catch (...) {
  }
  g_draining = previous;
  std::vector<std::pair<SculptListener, void*>> listeners;
  for (SculptDoc* d : docs) {
    d->sync();
    if (d->lock.depth() == 1 && d->takeNotification() && d->listener) listeners.push_back({d->listener, d->listenerContext});
  }
  for (auto it = docs.rbegin(); it != docs.rend(); ++it) (*it)->lock.unlock();
  for (auto& [f, c] : listeners) f(c);
}

SCULPT_API void sculpt_mcp_register(SculptDoc* doc) {
  if (!doc) return;
  {
    std::lock_guard<std::mutex> l(g_mutex);
    if (std::find(g_registered.begin(), g_registered.end(), doc) != g_registered.end()) return;
    g_registered.push_back(doc);
  }
  sculpt::mcp::Server::shared().registerDocument(doc->mcp.get());
}

SCULPT_API void sculpt_mcp_unregister(SculptDoc* doc) {
  if (!doc) return;
  {
    std::lock_guard<std::mutex> l(g_mutex);
    auto it = std::find(g_registered.begin(), g_registered.end(), doc);
    if (it == g_registered.end()) return;
    g_registered.erase(it);
  }
  sculpt::mcp::Server::shared().unregisterDocument(doc->mcp.get());
}

SCULPT_API void sculpt_mcp_activate(SculptDoc* doc) {
  if (!doc) return;
  sculpt::mcp::Server::shared().activate(doc->mcp.get());
}

SCULPT_API void sculpt_mcp_set_new_document_handler(SculptNewDocument handler, void* context) {
  {
    std::lock_guard<std::mutex> l(g_mutex);
    g_newDocument = handler;
    g_newDocumentContext = context;
  }
  if (!handler) {
    sculpt::mcp::Server::shared().setNewDocumentHandler(nullptr);
    return;
  }
  sculpt::mcp::Server::shared().setNewDocumentHandler([](const std::string& name) -> sculpt::mcp::Document* {
    SculptNewDocument h;
    void* c;
    {
      std::lock_guard<std::mutex> l(g_mutex);
      h = g_newDocument;
      c = g_newDocumentContext;
    }
    if (!h) return nullptr;
    SculptDoc* d = h(c, name.c_str());
    if (!d) return nullptr;
    sculpt_mcp_register(d);
    // Lock it like the documents of the drain in progress, so its GL thread
    // can't render while tools change it.
    if (g_draining && std::find(g_draining->begin(), g_draining->end(), d) == g_draining->end()) {
      d->lock.lock();
      d->sync();
      g_draining->push_back(d);
    }
    return d->mcp.get();
  });
}

}  // extern "C"
