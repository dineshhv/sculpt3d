// struct SculptDoc: one open document behind the C API. It owns the model
// store, the viewport controller and (with SCULPT_WITH_GL) the GLES renderer,
// and serialises every C call with a re-entrant lock.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "api/ViewportController.hpp"
#include "nlohmann/json.hpp"
#include "sculpt/model/ModelStore.hpp"
#include "sculpt/sculpt_c.h"

#ifndef SCULPT_WITH_GL
#define SCULPT_WITH_GL 0
#endif
#if SCULPT_WITH_GL
#include "viewport/Renderer.hpp"
#endif

namespace sculpt::api {

/// A recursive lock that can be released completely while waiting (a
/// screenshot requested from the UI thread waits for the GL thread, which
/// needs the same document).
class ReentrantLock {
 public:
  void lock() {
    auto me = std::this_thread::get_id();
    std::unique_lock<std::mutex> l(m_);
    if (owner_ == me) {
      ++depth_;
      return;
    }
    cv_.wait(l, [&] { return depth_ == 0; });
    owner_ = me;
    depth_ = 1;
  }
  void unlock() {
    std::unique_lock<std::mutex> l(m_);
    if (--depth_ == 0) {
      owner_ = std::thread::id();
      l.unlock();
      cv_.notify_all();
    }
  }
  int depth() {
    std::lock_guard<std::mutex> l(m_);
    return owner_ == std::this_thread::get_id() ? depth_ : 0;
  }
  /// Releases every level held by this thread; returns how many.
  int releaseAll() {
    std::unique_lock<std::mutex> l(m_);
    if (owner_ != std::this_thread::get_id()) return 0;
    int d = depth_;
    depth_ = 0;
    owner_ = std::thread::id();
    l.unlock();
    cv_.notify_all();
    return d;
  }
  void reacquire(int levels) {
    if (levels <= 0) return;
    lock();
    std::lock_guard<std::mutex> l(m_);
    depth_ = levels;
  }

 private:
  std::mutex m_;
  std::condition_variable cv_;
  std::thread::id owner_;
  int depth_ = 0;
};

/// A screenshot the UI thread asked for, fulfilled by the next render on the GL thread.
struct SnapshotRequest {
  int width = 0, height = 0;
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  std::vector<uint8_t> rgba;
  std::string error;
};

class McpDocument;

}  // namespace sculpt::api

struct SculptDoc {
  SculptDoc(const std::string& name);
  ~SculptDoc();

  sculpt::api::ReentrantLock lock;
  std::unique_ptr<sculpt::ModelStore> store;
  std::unique_ptr<sculpt::api::ViewportController> view;
  std::unique_ptr<sculpt::api::McpDocument> mcp;
#if SCULPT_WITH_GL
  std::unique_ptr<sculpt::viewport::Renderer> renderer;
#endif
  bool glReady = false;
  std::string glError;
  std::thread::id glThread;
  int pixelWidth = 0, pixelHeight = 0;
  float pixelScale = 1;

  SculptListener listener = nullptr;
  void* listenerContext = nullptr;
  bool dirty = false;

  std::mutex snapshotMutex;
  std::vector<std::shared_ptr<sculpt::api::SnapshotRequest>> snapshotRequests;

  /// The store revision the controller last saw (storeChanged sync).
  uint64_t syncedStoreRevision = ~0ull;
  /// Revisions at the last listener call.
  uint64_t notifiedRevision = 0, notifiedViewRevision = 0;

  /// Store revision + controller state revision: what sculpt_doc_revision reports.
  uint64_t revision() const { return store->revision() + view->stateRevision(); }
  /// Brings the controller up to date with the store (call with the lock held).
  void sync();
  /// Whether the listener is due (and records the revisions as notified).
  bool takeNotification();
  /// Renders the current frame RGBA (GL thread, lock held). Empty on failure.
  std::vector<uint8_t> renderSnapshot(int width, int height, std::string& error);
  /// Fulfils queued snapshot requests (GL thread, lock held).
  void serveSnapshots();
  /// PNG bytes of the view at `scale` × its point size, from any thread with
  /// the lock held: renders directly on the GL thread, else asks the GL thread
  /// and waits (up to 10 s) with the lock released.
  std::vector<uint8_t> screenshotPng(double scale, std::string& error);
  void notifyListener();
};

namespace sculpt::api {

/// RAII guard for one C API call: locks the document, and on the outermost
/// exit syncs the controller and calls the listener (after unlocking) when a
/// revision moved.
class Call {
 public:
  explicit Call(SculptDoc* d) : d_(d) {
    if (d_) d_->lock.lock();
  }
  ~Call();
  Call(const Call&) = delete;
  Call& operator=(const Call&) = delete;

 private:
  SculptDoc* d_;
};

/// Builds sculpt_doc_state's JSON (State.cpp).
nlohmann::json documentState(SculptDoc& doc);
/// Runs one sculpt_doc_command (Commands.cpp). Throws nothing; errors come back as {"ok": false}.
nlohmann::json runCommand(SculptDoc& doc, const nlohmann::json& command);

/// Drawing settings from JSON ({paper, views, showHidden, showDimensions, scale, title}); nothing + error when invalid.
std::optional<DrawingSettings> parseDrawingSettings(const nlohmann::json& j, std::string& error,
                                                   const std::string& defaultTitle);
/// Imports a CAD file or Sculpt3D project by path. Returns "" or an error message.
std::string importPath(SculptDoc& doc, const std::string& path);
/// Inserts image bytes: 0 reference image, 1 decal on the selected face, 2 terrain from a height map.
std::string importImage(SculptDoc& doc, const std::vector<uint8_t>& bytes, const std::string& name, int as);

/// Heap copy of a string for the C API (free with sculpt_free).
char* dupString(const std::string& s);

}  // namespace sculpt::api
