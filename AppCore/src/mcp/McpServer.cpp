// Port of App/Sources/MCP/MCPServer.swift (JSON-RPC core) and the request
// handling of MCPHTTPTransport.swift, plus the hand-off of each request to the
// host's UI thread (Swift ran the server on the MainActor).
#include <chrono>
#include <condition_variable>
#include <deque>
#include <algorithm>
#include <cctype>
#include <mutex>

#include "mcp/McpHost.hpp"
#include "mcp/McpHttp.hpp"
#include "mcp/McpTools.hpp"
#include "sculpt/model/ModelStore.hpp"

namespace sculpt::mcp {

namespace {

const std::vector<std::string>& supportedVersions() {
  static const std::vector<std::string> v = {"2025-06-18", "2025-03-26", "2024-11-05"};
  return v;
}

const char* kInstructions =
    "Sculpt3D is a parametric B-rep CAD app (OpenCascade kernel). Tools act on the active open document and show up "
    "live in its window. Units are millimetres and degrees; Z is up. Every modeling tool appends a feature to the "
    "parametric history, so undo, redo, update_feature and delete_feature work on it.\n"
    "Body ids come from get_model and can change when earlier history changes, so read them again after edits. Face "
    "and edge indices come from get_body_topology. Use screenshot to check the result visually.";

json result(const json& id, const json& r) {
  json o = json::object();
  o["jsonrpc"] = "2.0";
  o["id"] = id;
  o["result"] = r;
  return o;
}

json error(const json& id, int code, const std::string& message) {
  json e = json::object();
  e["code"] = code;
  e["message"] = message;
  json o = json::object();
  o["jsonrpc"] = "2.0";
  o["id"] = id;
  o["error"] = e;
  return o;
}

json errorBody(const std::string& message) { return error(nullptr, -32000, message); }

bool isLocalOrigin(const std::string& origin) {
  // URL(string:)?.host: scheme://[userinfo@]host[:port][/…]
  size_t scheme = origin.find("://");
  if (scheme == std::string::npos) return false;
  std::string rest = origin.substr(scheme + 3);
  rest = rest.substr(0, rest.find_first_of("/?#"));
  if (auto at = rest.rfind('@'); at != std::string::npos) rest = rest.substr(at + 1);
  std::string host;
  if (!rest.empty() && rest[0] == '[') {
    size_t close = rest.find(']');
    if (close == std::string::npos) return false;
    host = rest.substr(1, close - 1);
  } else {
    host = rest.substr(0, rest.find(':'));
  }
  for (auto& c : host) c = char(std::tolower(static_cast<unsigned char>(c)));
  return host == "localhost" || host == "127.0.0.1" || host == "::1" || host == "[::1]";
}

}  // namespace

struct Server::Impl {
  struct Job {
    json message;
    std::optional<json> response;
    bool started = false, done = false, cancelled = false;
  };

  DocumentRegistry registry;
  Toolbox tools{registry};
  HttpServer http;
  std::string version = "0.1.0";

  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::shared_ptr<Job>> queue;
  std::function<void()> post;
  bool stopping = false;

  /// How long a request waits for the UI thread to pick it up.
  static constexpr auto kPickupTimeout = std::chrono::seconds(60);

  // MARK: JSON-RPC (MCPServer.swift)

  std::optional<json> handle(const json& message) {
    if (message.is_array()) {
      json out = json::array();
      for (auto& m : message)
        if (auto r = handleOne(m)) out.push_back(*r);
      if (out.empty()) return std::nullopt;
      return out;
    }
    return handleOne(message);
  }

  std::optional<json> handleOne(const json& msg) {
    if (!msg.is_object() || !msg.contains("jsonrpc") || msg["jsonrpc"] != "2.0")
      return error(nullptr, -32600, "Invalid request");
    // Notifications and responses from the client need no reply.
    if (!msg.contains("method") || !msg["method"].is_string() || !msg.contains("id")) return std::nullopt;
    std::string method = msg["method"].get<std::string>();
    const json& id = msg["id"];
    json params = msg.contains("params") && msg["params"].is_object() ? msg["params"] : json::object();

    if (method == "initialize") {
      std::string requested = params.contains("protocolVersion") && params["protocolVersion"].is_string()
                                  ? params["protocolVersion"].get<std::string>()
                                  : "";
      const auto& versions = supportedVersions();
      bool known = std::find(versions.begin(), versions.end(), requested) != versions.end();
      json tools = json::object();
      tools["listChanged"] = false;
      json caps = json::object();
      caps["tools"] = tools;
      json info = json::object();
      info["name"] = "sculpt3d";
      info["title"] = "Sculpt3D";
      info["version"] = version;
      json r = json::object();
      r["protocolVersion"] = known ? requested : versions[0];
      r["capabilities"] = caps;
      r["serverInfo"] = info;
      r["instructions"] = kInstructions;
      return result(id, r);
    }
    if (method == "ping") return result(id, json::object());
    if (method == "tools/list") {
      json r = json::object();
      r["tools"] = tools.definitions();
      return result(id, r);
    }
    if (method == "tools/call") {
      if (!params.contains("name") || !params["name"].is_string()) return error(id, -32602, "Missing tool name");
      std::string name = params["name"].get<std::string>();
      if (!tools.has(name)) return error(id, -32602, "Unknown tool: " + name);
      json args = params.contains("arguments") && params["arguments"].is_object() ? params["arguments"] : json::object();
      json r = json::object();
      try {
        r["content"] = tools.call(name, args);
      } catch (const std::exception& e) {
        // Tool failures go back to the model as results it can act on.
        json block = json::object();
        block["type"] = "text";
        block["text"] = e.what();
        r = json::object();
        r["content"] = json::array({block});
        r["isError"] = true;
      } catch (...) {
        json block = json::object();
        block["type"] = "text";
        block["text"] = "The operation failed";
        r = json::object();
        r["content"] = json::array({block});
        r["isError"] = true;
      }
      return result(id, r);
    }
    return error(id, -32601, "Method not found: " + method);
  }

  // MARK: UI-thread hand-off

  /// Runs `message` on the UI thread (via post/drain) and waits for the answer.
  /// Returns false when the request was dropped (stopping) and `timedOut` when
  /// the UI thread never picked it up.
  bool runOnUiThread(const json& message, std::optional<json>& response, bool& timedOut) {
    auto job = std::make_shared<Job>();
    job->message = message;
    std::function<void()> poster;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (stopping) return false;
      queue.push_back(job);
      poster = post;
    }
    if (poster) poster();
    std::unique_lock<std::mutex> lock(mutex);
    bool picked = cv.wait_for(lock, kPickupTimeout, [&] { return job->started || job->done || stopping; });
    if (!picked) {
      job->cancelled = true;
      queue.erase(std::remove(queue.begin(), queue.end(), job), queue.end());
      timedOut = true;
      return true;
    }
    // Once started, a tool runs to completion (long kernel operations included).
    cv.wait(lock, [&] { return job->done || (stopping && !job->started); });
    if (!job->done) return false;
    response = std::move(job->response);
    return true;
  }

  void drain() {
    for (;;) {
      std::shared_ptr<Job> job;
      {
        std::lock_guard<std::mutex> lock(mutex);
        while (!queue.empty() && queue.front()->cancelled) queue.pop_front();
        if (queue.empty()) return;
        job = queue.front();
        queue.pop_front();
        job->started = true;
      }
      cv.notify_all();
      std::optional<json> response;
      try {
        response = handle(job->message);
      } catch (const std::exception& e) {
        response = error(nullptr, -32603, std::string("Internal error: ") + e.what());
      } catch (...) {
        response = error(nullptr, -32603, "Internal error");
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        job->response = std::move(response);
        job->done = true;
      }
      cv.notify_all();
    }
  }

  // MARK: HTTP (MCPHTTPTransport.swift `respond`)

  HttpServer::Response respond(const HttpServer::Request& req) {
    HttpServer::Response res;
    auto origin = req.headers.find("origin");
    if (origin != req.headers.end() && !isLocalOrigin(origin->second)) {
      res.status = 403;
      res.json = true;
      res.body = serialize(errorBody("Origin not allowed"));
      return res;
    }
    if (req.path != "/mcp" && req.path != "/") {
      res.status = 404;
      return res;
    }
    if (req.method == "POST") {
      json message;
      try {
        message = json::parse(req.body);
      } catch (...) {
        res.status = 400;
        res.json = true;
        res.body = serialize(error(nullptr, -32700, "Parse error"));
        return res;
      }
      std::optional<json> response;
      bool timedOut = false;
      if (!runOnUiThread(message, response, timedOut)) {
        res.drop = true;
        return res;
      }
      if (timedOut) {
        json id = message.is_object() && message.contains("id") ? message["id"] : json(nullptr);
        res.status = 200;
        res.json = true;
        res.body = serialize(error(id, -32001, "Sculpt3D did not respond in time; the app may be busy or in the background"));
        return res;
      }
      // Notifications and client responses are acknowledged without a body.
      if (response) {
        res.status = 200;
        res.json = true;
        res.body = serialize(*response);
      } else {
        res.status = 202;
      }
      return res;
    }
    if (req.method == "DELETE") {
      res.status = 200;
      return res;
    }
    res.status = 405;
    res.headers.push_back({"Allow", "POST, DELETE"});
    return res;
  }
};

Server& Server::shared() {
  static Server server;
  return server;
}

Server::Server() : impl_(std::make_unique<Impl>()) {}

Server::~Server() { stop(); }

std::string Server::start(int port, std::function<void()> post) {
  if (impl_->http.running()) return "";
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->post = std::move(post);
    impl_->stopping = false;
  }
  Impl* impl = impl_.get();
  return impl_->http.start(port, [impl](const HttpServer::Request& r) { return impl->respond(r); });
}

void Server::stop() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stopping = true;
    for (auto& j : impl_->queue) j->cancelled = true;
    impl_->queue.clear();
  }
  impl_->cv.notify_all();
  impl_->http.stop();
}

bool Server::running() const { return impl_->http.running(); }
int Server::port() const { return impl_->http.port(); }
std::string Server::endpoint() const { return "http://127.0.0.1:" + std::to_string(port()) + "/mcp"; }
void Server::drain() { impl_->drain(); }

void Server::registerDocument(Document* doc) { impl_->registry.add(doc); }
void Server::unregisterDocument(Document* doc) { impl_->registry.remove(doc); }
void Server::activate(Document* doc) { impl_->registry.activate(doc); }
void Server::setNewDocumentHandler(std::function<Document*(const std::string&)> handler) {
  impl_->registry.setNewDocumentHandler(std::move(handler));
}
void Server::setVersion(const std::string& version) { impl_->version = version; }

std::string Server::handleMessage(const std::string& text) {
  json message;
  try {
    message = json::parse(text);
  } catch (...) {
    return serialize(error(nullptr, -32700, "Parse error"));
  }
  auto r = impl_->handle(message);
  return r ? serialize(*r) : "";
}

}  // namespace sculpt::mcp
