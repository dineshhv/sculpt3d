// Tests of the MCP server (AppCore/src/mcp): starts it on a free port with a
// fake document, drives it over HTTP from a client thread while the main
// thread pumps drain() like a host UI loop.
// Plain checks, no framework: prints each result, exits non-zero on failure.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "mcp/McpHost.hpp"
#include "mcp/McpTools.hpp"
#include "sculpt/model/ModelStore.hpp"

using namespace sculpt;
using nlohmann::json;
namespace fs = std::filesystem;

static std::mutex printMutex;
static int failures = 0, passes = 0;
static void check(bool ok, const std::string& what) {
  std::lock_guard<std::mutex> lock(printMutex);
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (ok) ++passes;
  else ++failures;
}

// MARK: - Fake host document

class FakeDocument : public mcp::Document {
 public:
  explicit FakeDocument(std::string n) : name_(std::move(n)) {}
  std::string name() const override { return name_; }
  ModelStore& store() override { return store_; }
  void setView(const mcp::ViewRequest& r) override {
    ++views;
    lastView = r;
  }
  void redraw() override { ++redraws; }
  std::vector<uint8_t> screenshotPng(double, std::string& error) override {
    error = "No viewport in the test host";
    return {};
  }
  int views = 0, redraws = 0;
  mcp::ViewRequest lastView;

 private:
  std::string name_;
  ModelStore store_;
};

// MARK: - HTTP client

struct HttpResult {
  int status = 0;
  std::string headers, body;
};

static int connectTo(int port) {
  int s = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(uint16_t(port));
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
    ::close(s);
    return -1;
  }
  return s;
}

/// Sends one request on `s` and reads one response (Content-Length framed).
static HttpResult exchange(int s, const std::string& method, const std::string& body,
                           const std::string& extraHeaders = "") {
  std::string req = method + " /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
                    "Accept: application/json, text/event-stream\r\n" + extraHeaders +
                    "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  ::send(s, req.data(), req.size(), 0);
  std::string buf;
  char chunk[65536];
  HttpResult r;
  for (;;) {
    size_t end = buf.find("\r\n\r\n");
    if (end != std::string::npos) {
      r.headers = buf.substr(0, end);
      size_t cl = 0;
      auto p = r.headers.find("Content-Length: ");
      if (p != std::string::npos) cl = std::stoul(r.headers.substr(p + 16));
      if (buf.size() >= end + 4 + cl) {
        r.body = buf.substr(end + 4, cl);
        r.status = std::stoi(r.headers.substr(9, 3));
        return r;
      }
    }
    auto n = ::recv(s, chunk, sizeof chunk, 0);
    if (n <= 0) return r;
    buf.append(chunk, size_t(n));
  }
}

static HttpResult post(int port, const std::string& body, const std::string& extraHeaders = "",
                       const std::string& method = "POST") {
  int s = connectTo(port);
  if (s < 0) return {};
  HttpResult r = exchange(s, method, body, extraHeaders + "Connection: close\r\n");
  ::close(s);
  return r;
}

static int nextId = 1;
static json rpc(int port, const std::string& method, const json& params) {
  json m = {{"jsonrpc", "2.0"}, {"id", nextId++}, {"method", method}, {"params", params}};
  auto r = post(port, m.dump());
  try {
    return json::parse(r.body);
  } catch (...) {
    return json();
  }
}

/// tools/call; returns the result object.
static json call(int port, const std::string& tool, const json& args = json::object()) {
  json r = rpc(port, "tools/call", {{"name", tool}, {"arguments", args}});
  return r.contains("result") ? r["result"] : json();
}

static json textJson(const json& result) {
  try {
    return json::parse(result["content"][0]["text"].get<std::string>());
  } catch (...) {
    return json();
  }
}

static bool isError(const json& result) { return result.value("isError", false); }

// MARK: - Client script

static void client(int port, FakeDocument& doc) {
  // initialize: version negotiation and server info.
  json init = rpc(port, "initialize", {{"protocolVersion", "2025-03-26"}, {"capabilities", json::object()},
                                       {"clientInfo", {{"name", "test"}, {"version", "1"}}}});
  check(init["result"]["protocolVersion"] == "2025-03-26", "initialize echoes a supported protocol version");
  check(init["result"]["serverInfo"]["name"] == "sculpt3d", "initialize reports serverInfo.name sculpt3d");
  check(init["result"]["capabilities"]["tools"]["listChanged"] == false, "initialize capabilities.tools");
  json init2 = rpc(port, "initialize", {{"protocolVersion", "1999-01-01"}});
  check(init2["result"]["protocolVersion"] == "2025-06-18", "unknown protocol version falls back to 2025-06-18");

  // Notification: 202 without a body.
  auto note = post(port, R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
  check(note.status == 202 && note.body.empty(), "notification is acknowledged with 202");

  json ping = rpc(port, "ping", json::object());
  check(ping.contains("result") && ping["result"].empty(), "ping returns {}");

  json list = rpc(port, "tools/list", json::object());
  size_t count = list["result"]["tools"].size();
  check(count == 34, "tools/list has the Swift tool count (34), got " + std::to_string(count));
  check(list["result"]["tools"][0]["name"] == "list_documents" &&
            list["result"]["tools"][33]["name"] == "screenshot",
        "tools/list keeps the Swift order");
  bool schemasOk = true;
  for (auto& t : list["result"]["tools"])
    schemasOk = schemasOk && t["inputSchema"]["type"] == "object" && t.contains("annotations");
  check(schemasOk, "every tool has an object inputSchema and annotations");

  json unknown = rpc(port, "resources/list", json::object());
  check(unknown["error"]["code"] == -32601, "unknown method gives -32601");
  json noTool = rpc(port, "tools/call", {{"name", "nope"}});
  check(noTool["error"]["code"] == -32602 && noTool["error"]["message"] == "Unknown tool: nope",
        "unknown tool gives -32602");

  auto parse = post(port, "{not json");
  check(parse.status == 400 && json::parse(parse.body)["error"]["code"] == -32700, "bad JSON gives 400 / -32700");
  auto invalid = post(port, R"({"id":1,"method":"ping"})");
  check(json::parse(invalid.body)["error"]["code"] == -32600, "missing jsonrpc gives -32600");

  // Batch: two requests and a notification.
  auto batch = post(port, R"([{"jsonrpc":"2.0","id":"a","method":"ping"},{"jsonrpc":"2.0","method":"x"},)"
                          R"({"jsonrpc":"2.0","id":"b","method":"ping"}])");
  json b = json::parse(batch.body);
  check(b.is_array() && b.size() == 2 && b[0]["id"] == "a", "batch answers each request");

  // HTTP rules.
  auto evil = post(port, R"({"jsonrpc":"2.0","id":1,"method":"ping"})", "Origin: https://evil.example\r\n");
  check(evil.status == 403 && evil.body.find("Origin not allowed") != std::string::npos,
        "foreign Origin is refused with 403");
  auto local = post(port, R"({"jsonrpc":"2.0","id":1,"method":"ping"})", "Origin: http://localhost:5173\r\n");
  check(local.status == 200, "localhost Origin is allowed");
  auto get = post(port, "", "", "GET");
  check(get.status == 405 && get.headers.find("Allow: POST, DELETE") != std::string::npos, "GET gives 405 with Allow");
  auto del = post(port, "", "", "DELETE");
  check(del.status == 200, "DELETE gives 200");

  // Keep-alive: two requests on one connection.
  {
    int s = connectTo(port);
    auto r1 = exchange(s, "POST", R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
    auto r2 = exchange(s, "POST", R"({"jsonrpc":"2.0","id":2,"method":"ping"})");
    ::close(s);
    check(r1.status == 200 && r2.status == 200 && json::parse(r2.body)["id"] == 2, "keep-alive serves two requests");
  }

  // Documents.
  json docs = textJson(call(port, "list_documents"));
  check(docs.is_array() && docs.size() == 1 && docs[0]["name"] == "Test Part" && docs[0]["active"] == true,
        "list_documents lists the registered document");

  // Modeling.
  json prim = call(port, "add_primitive", {{"kind", "box"}, {"width", 40}, {"depth", 30}, {"height", 20}});
  json primOut = textJson(prim);
  check(!isError(prim) && primOut["new_bodies"].size() == 1, "add_primitive adds a body");
  int box = primOut["new_bodies"][0].get<int>();
  json size = primOut["bodies"][0]["size"];
  check(size == json::array({40, 30, 20}), "box size is 40 x 30 x 20: " + size.dump());

  json ext = call(port, "sketch_extrude",
                  {{"plane", "xy"},
                   {"offset", 20},
                   {"shapes", json::array({{{"type", "circle"}, {"cx", 0}, {"cy", 0}, {"radius", 5}}})},
                   {"distance", 10},
                   {"mode", "join"}});
  json extOut = textJson(ext);
  check(!isError(ext) && extOut["features"].size() == 1 && extOut["new_bodies"].empty(),
        "sketch_extrude joins a boss onto the only body");

  json fil = call(port, "fillet_edges", {{"body", box}, {"edges", json::array({0, 1})}, {"radius", 2}});
  check(!isError(fil), "fillet_edges rounds two edges: " + fil["content"][0]["text"].get<std::string>().substr(0, 80));

  json bad = call(port, "fillet_edges", {{"body", box}, {"all", true}, {"radius", 500}});
  check(isError(bad), "an impossible fillet is reported as a tool error");
  json noBody = call(port, "fillet_edges", {{"body", 99}, {"all", true}, {"radius", 1}});
  check(isError(noBody) && noBody["content"][0]["text"].get<std::string>().rfind("No body 99. Bodies: ", 0) == 0,
        "missing body names the bodies");

  json model = textJson(call(port, "get_model"));
  check(model["features"].size() == 3, "get_model lists 3 features (failed fillet not committed)");
  check(model["document"] == "Test Part" && model["units"] == "mm", "get_model document and units");
  check(model["features"][0]["parameters"].size() >= 3 && model["features"][0]["parameters"][0]["unit"] == "mm",
        "primitive lists its parameters with units");

  // update_feature with an expression over a variable.
  call(port, "set_variable", {{"name", "w"}, {"expression", "50"}});
  std::string firstId = model["features"][0]["id"].get<std::string>();
  json upd = call(port, "update_feature", {{"feature_id", firstId}, {"values", {{"width", "w + 10"}}}});
  json updOut = textJson(upd);
  check(!isError(upd) && updOut["features"][0]["parameters"][0]["value"] == 60 &&
            updOut["features"][0]["parameters"][0]["expression"] == "w + 10",
        "update_feature binds an expression");

  json topo = textJson(call(port, "get_body_topology", {{"body", box}}));
  check(topo["faces"].size() > 6 && topo["edges"].size() > 12, "get_body_topology lists faces and edges");

  json meas = textJson(call(port, "measure", {{"a", {{"body", box}}}}));
  check(meas.contains("volume") && meas["volume"].get<double>() > 0, "measure gives a body volume");

  // Undo / redo.
  json undo = textJson(call(port, "undo"));
  check(undo["features"].size() == 3 && undo["features"][0]["parameters"][0]["value"] == 40, "undo reverts the update");
  json redo = textJson(call(port, "redo"));
  check(redo["features"][0]["parameters"][0]["value"] == 60, "redo reapplies it");

  // Appearance and view.
  json paint = call(port, "set_appearance", {{"bodies", json::array({box})}, {"color", "#8F9A78"}});
  check(textJson(paint)["painted"] == json::array({box}) && doc.redraws >= 1, "set_appearance paints and redraws");
  json view = call(port, "set_view", {{"view", "top"}, {"projection", "orthographic"}, {"pitch", 120}});
  check(!isError(view) && doc.lastView.preset == std::optional<std::string>("top") && doc.lastView.fit &&
            doc.lastView.pitchRadians && std::abs(*doc.lastView.pitchRadians - 89 * 3.14159265358979 / 180) < 1e-6 &&
            doc.store().settings().camera.orthographic,
        "set_view passes the preset, fit and clamped pitch, and sets orthographic");
  json shot = call(port, "screenshot", {{"view", "iso"}});
  check(isError(shot) && shot["content"][0]["text"] == "No viewport in the test host",
        "screenshot reports the host's error");

  // Export STEP.
  std::string path = (fs::temp_directory_path() / "sculpt3d-mcp-test.step").string();
  std::error_code ec;
  fs::remove(path, ec);
  json exp = call(port, "export_model", {{"format", "STEP"}, {"path", path}});
  json expOut = textJson(exp);
  check(!isError(exp) && expOut["path"] == path && expOut["bytes"].get<int64_t>() > 1000 && fs::exists(path),
        "export_model writes a STEP file (" + std::to_string(expOut.value("bytes", 0)) + " bytes)");
  json exp3 = call(port, "export_model", {{"format", "3mf"}, {"path", path + ".3mf"}});
  check(!isError(exp3) && textJson(exp3)["format"] == "3MF", "export_model writes 3MF");
  fs::remove(path, ec);
  fs::remove(path + ".3mf", ec);

  // Revolve on the xz plane about v.
  json revolve = call(port, "sketch_revolve",
                      {{"plane", "xz"},
                       {"shapes", json::array({{{"type", "rectangle"}, {"x", 30}, {"y", 0}, {"width", 5}, {"height", 5}}})},
                       {"axis", "v"}});
  check(!isError(revolve) && textJson(revolve)["new_bodies"].size() == 1, "sketch_revolve makes a ring");

  json newDoc = call(port, "new_document");
  check(!isError(newDoc) && textJson(newDoc)["name"] == "Untitled 2", "new_document opens through the host handler");
  json docs2 = textJson(call(port, "list_documents"));
  check(docs2.size() == 2 && docs2[0]["name"] == "Untitled 2", "new document is active");
  json sel = call(port, "select_document", {{"id", docs2[1]["id"]}});
  check(!isError(sel) && sel["content"][0]["text"] == "Active document: Test Part", "select_document switches back");
}

int main() {
  // JSON output matches Darwin JSONSerialization.
  check(mcp::serialize(json{{"b", 40.0}, {"a", 0.3}, {"c", "x/y"}, {"d", 3}}) ==
            R"({"a":0.29999999999999999,"b":40,"c":"x/y","d":3})",
        "serialize sorts keys and writes doubles like JSONSerialization");

  FakeDocument doc("Test Part");
  std::vector<std::unique_ptr<FakeDocument>> extra;
  auto& server = mcp::Server::shared();
  server.registerDocument(&doc);
  server.setNewDocumentHandler([&](const std::string&) -> mcp::Document* {
    extra.push_back(std::make_unique<FakeDocument>("Untitled " + std::to_string(extra.size() + 2)));
    server.registerDocument(extra.back().get());
    return extra.back().get();
  });

  std::atomic<bool> pending{false};
  std::string err = server.start(0, [&] { pending = true; });
  check(err.empty(), "server starts on a free port: " + err);
  check(server.running() && server.port() > 0, "listening on " + server.endpoint());

  // Port collision reports an error.
  {
    // A second listener on the same port must fail; use a raw socket.
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(server.port()));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0, "the port is taken while running");
    ::close(s);
  }

  std::atomic<bool> done{false};
  std::thread t([&] {
    client(server.port(), doc);
    done = true;
  });
  // Host UI loop.
  while (!done) {
    if (pending.exchange(false)) server.drain();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  t.join();

  server.stop();
  check(!server.running(), "server stops");
  check(post(server.port(), R"({"jsonrpc":"2.0","id":1,"method":"ping"})").status == 0, "nothing listens after stop");
  std::string restart = server.start(0, [&] { pending = true; });
  check(restart.empty() && server.running(), "server restarts");
  server.stop();
  for (auto& d : extra) server.unregisterDocument(d.get());
  server.unregisterDocument(&doc);

  std::printf("\n%d passed, %d failed\n", passes, failures);
  return failures ? 1 : 0;
}
