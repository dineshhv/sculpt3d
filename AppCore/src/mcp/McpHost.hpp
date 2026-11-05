// Host interface of the Sculpt3D MCP server (port of App/Sources/MCP).
//
// The server listens on http://127.0.0.1:<port>/mcp (Streamable HTTP, plain
// JSON responses). Tools run on the host's UI thread: the HTTP thread queues
// a job, calls the `post` callback given to start(), and waits (up to 60 s)
// for the host to call drain() on its UI thread. A front end implements
// Document for each open window and registers it.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sculpt {
class ModelStore;
}

namespace sculpt::mcp {

/// What set_view / screenshot ask of the viewport. Unset fields stay as they are.
struct ViewRequest {
  /// Camera preset: "iso", "top", "front" or "right" (the tool's enum).
  std::optional<std::string> preset;
  /// Zoom to fit all bodies. Applied after the preset (default true when a preset is given).
  bool fit = false;
  /// Orbit yaw in radians around Z (0 looks from +X); applied after fit.
  std::optional<double> yawRadians;
  /// Orbit pitch in radians above the ground plane, already clamped to ±89°.
  std::optional<double> pitchRadians;
  /// Multiplies the camera distance (> 0).
  std::optional<double> zoom;
  /// "shaded", "wireframe", "xray", "visualized", "zebra" or "curvature".
  std::optional<std::string> displayMode;
  // Projection (perspective/orthographic) is written by the server to
  // store().settings().camera.orthographic, as the Swift does.
};

/// One open document window. All calls happen on the host UI thread (inside drain()).
class Document {
 public:
  virtual ~Document() = default;
  /// Display name (the window title / file name).
  virtual std::string name() const = 0;
  virtual ModelStore& store() = 0;
  /// Applies a view request to the window's camera and redraws.
  virtual void setView(const ViewRequest& request) = 0;
  /// Requests a redraw after appearance/environment changes.
  virtual void redraw() = 0;
  /// Renders the viewport to PNG bytes at `scale` × its point size (1–3).
  /// On failure returns empty and sets `error`. May block for the GL thread
  /// (it is called from drain() on the UI thread).
  virtual std::vector<uint8_t> screenshotPng(double scale, std::string& error) = 0;
};

/// The MCP server: JSON-RPC core, tools and the localhost HTTP transport.
class Server {
 public:
  static constexpr int kDefaultPort = 7317;
  static Server& shared();

  /// Starts listening on 127.0.0.1:port. Returns "" on success or an error
  /// message. `post` may be called from any thread; it must arrange for
  /// drain() to run soon on the host UI thread.
  std::string start(int port, std::function<void()> post);
  void stop();
  bool running() const;
  int port() const;
  /// "http://127.0.0.1:<port>/mcp".
  std::string endpoint() const;
  /// Runs queued tool jobs. Call on the host UI thread.
  void drain();

  /// Documents, most recently active first. Pointers are not owned; the host
  /// unregisters a document before destroying it.
  void registerDocument(Document* doc);
  void unregisterDocument(Document* doc);
  void activate(Document* doc);
  /// Called (on the UI thread) by new_document. Returns the new, registered
  /// document, or nullptr if no window could be opened.
  void setNewDocumentHandler(std::function<Document*(const std::string& name)> handler);

  /// Handles one JSON-RPC message (or batch) as text, on the calling thread
  /// (which must be the UI thread). Returns "" when no response is due.
  /// Used by tests and hosts that bring their own transport.
  std::string handleMessage(const std::string& json);

  /// Server version reported by initialize (default "0.1.0", the Mac MARKETING_VERSION).
  void setVersion(const std::string& version);

  struct Impl;

 private:
  Server();
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  std::unique_ptr<Impl> impl_;
};

}  // namespace sculpt::mcp
