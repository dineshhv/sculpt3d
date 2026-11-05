// Internal: a small HTTP/1.1 server on 127.0.0.1 over BSD sockets / Winsock,
// for the MCP Streamable HTTP transport (port of MCPHTTPTransport.swift's
// connection handling). One thread accepts; each connection gets a thread.
// Requests need a Content-Length body (no chunked uploads); connections are
// kept alive unless the client asks to close.
#pragma once

#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sculpt::mcp {

class HttpServer {
 public:
  struct Request {
    std::string method;  // upper case
    std::string path;    // without the query
    std::map<std::string, std::string> headers;  // names lower case
    std::string body;
  };
  struct Response {
    int status = 200;
    std::string body;
    bool json = false;  // Content-Type: application/json
    std::vector<std::pair<std::string, std::string>> headers;
    /// Close the connection without answering (the server is stopping).
    bool drop = false;
  };
  /// Runs on a connection thread.
  using Handler = std::function<Response(const Request&)>;

  /// Largest request accepted (a model's JSON never comes near this).
  static constexpr size_t kMaxBody = size_t(8) << 20;

  HttpServer() = default;
  ~HttpServer();
  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  /// Binds 127.0.0.1:port (0 picks a free port). Returns "" or an error.
  std::string start(int port, Handler handler);
  void stop();
  bool running() const { return running_; }
  int port() const { return port_; }
  /// Set while stopping, so handlers waiting on work can give up.
  bool stopping() const { return stopping_; }

 private:
  struct Connection;
  void acceptLoop();
  void serve(std::shared_ptr<Connection> c);
  void reap(bool all);

  Handler handler_;
  intptr_t listener_ = -1;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopping_{false};
  int port_ = 0;
  std::thread acceptThread_;
  std::mutex connectionsMutex_;
  std::list<std::shared_ptr<Connection>> connections_;
};

}  // namespace sculpt::mcp
