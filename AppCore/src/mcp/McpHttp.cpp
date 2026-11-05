// Small portable HTTP/1.1 server on 127.0.0.1 (see McpHttp.hpp).
#include "mcp/McpHttp.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static const socket_t kInvalid = INVALID_SOCKET;
static void closeSocket(socket_t s) { closesocket(s); }
static int lastSocketError() { return WSAGetLastError(); }
static std::string socketErrorText(int e) {
  char buf[256] = {0};
  if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, DWORD(e), 0, buf, sizeof buf,
                     nullptr) == 0)
    return "Winsock error " + std::to_string(e);
  std::string s(buf);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == '.' || s.back() == ' ')) s.pop_back();
  return s;
}
#define SHUT_BOTH SD_BOTH
#define SHUT_SEND SD_SEND
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using socket_t = int;
static const socket_t kInvalid = -1;
static void closeSocket(socket_t s) { ::close(s); }
static int lastSocketError() { return errno; }
static std::string socketErrorText(int e) { return std::strerror(e); }
#define SHUT_BOTH SHUT_RDWR
#define SHUT_SEND SHUT_WR
#endif

#if defined(MSG_NOSIGNAL)
static const int kSendFlags = MSG_NOSIGNAL;
#else
static const int kSendFlags = 0;
#endif

namespace sculpt::mcp {

namespace {

socket_t sock(intptr_t s) { return socket_t(s); }

/// Waits until the socket is readable. Returns 1 readable, 0 timeout, -1 error.
int waitReadable(socket_t s, int milliseconds) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(s, &set);
  timeval tv;
  tv.tv_sec = milliseconds / 1000;
  tv.tv_usec = (milliseconds % 1000) * 1000;
#ifdef _WIN32
  int n = ::select(0, &set, nullptr, nullptr, &tv);
#else
  int n = ::select(s + 1, &set, nullptr, nullptr, &tv);
#endif
  if (n < 0) {
#ifndef _WIN32
    if (errno == EINTR) return 0;
#endif
    return -1;
  }
  return n > 0 ? 1 : 0;
}

bool sendAll(socket_t s, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    int chunk = int(std::min<size_t>(data.size() - sent, 1 << 20));
    auto n = ::send(s, data.data() + sent, chunk, kSendFlags);
    if (n <= 0) {
#ifndef _WIN32
      if (n < 0 && errno == EINTR) continue;
#endif
      return false;
    }
    sent += size_t(n);
  }
  return true;
}

std::string lower(std::string s) {
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t"), e = s.find_last_not_of(" \t");
  return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

const char* reason(int status) {
  switch (status) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    default: return "Error";
  }
}

#ifdef _WIN32
bool startWinsock() {
  static bool ok = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return ok;
}
#endif

}  // namespace

struct HttpServer::Connection {
  intptr_t socket = -1;
  std::thread thread;
  std::atomic<bool> done{false};
};

HttpServer::~HttpServer() { stop(); }

std::string HttpServer::start(int port, Handler handler) {
  if (running_) return "";
#ifdef _WIN32
  if (!startWinsock()) return "Could not start Winsock";
#endif
  socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == kInvalid) return "Could not create a socket: " + socketErrorText(lastSocketError());
  int one = 1;
#ifdef _WIN32
  ::setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&one), sizeof one);
#else
  ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(uint16_t(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1 only
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(s, 16) != 0) {
    std::string err = socketErrorText(lastSocketError());
    closeSocket(s);
    return "Could not listen on 127.0.0.1:" + std::to_string(port) + ": " + err;
  }
  socklen_t len = sizeof addr;
  if (::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) == 0) port_ = ntohs(addr.sin_port);
  else port_ = port;

  handler_ = std::move(handler);
  listener_ = intptr_t(s);
  stopping_ = false;
  running_ = true;
  acceptThread_ = std::thread([this] { acceptLoop(); });
  return "";
}

void HttpServer::stop() {
  if (!running_) return;
  stopping_ = true;
  if (acceptThread_.joinable()) acceptThread_.join();
  closeSocket(sock(listener_));
  listener_ = -1;
  {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    for (auto& c : connections_) ::shutdown(sock(c->socket), SHUT_BOTH);
  }
  reap(true);
  running_ = false;
  stopping_ = false;
}

void HttpServer::reap(bool all) {
  std::list<std::shared_ptr<Connection>> finished;
  {
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    for (auto it = connections_.begin(); it != connections_.end();) {
      if (all || (*it)->done) {
        finished.push_back(*it);
        it = connections_.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (auto& c : finished) {
    if (c->thread.joinable()) c->thread.join();
    closeSocket(sock(c->socket));
  }
}

void HttpServer::acceptLoop() {
  socket_t listener = sock(listener_);
  while (!stopping_) {
    reap(false);
    int ready = waitReadable(listener, 200);
    if (ready <= 0) continue;
    socket_t c = ::accept(listener, nullptr, nullptr);
    if (c == kInvalid) continue;
#if defined(SO_NOSIGPIPE)
    int one = 1;
    ::setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    int nodelay = 1;
    ::setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof nodelay);
    auto conn = std::make_shared<Connection>();
    conn->socket = intptr_t(c);
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    connections_.push_back(conn);
    conn->thread = std::thread([this, conn] { serve(conn); });
  }
}

void HttpServer::serve(std::shared_ptr<Connection> conn) {
  socket_t s = sock(conn->socket);
  std::string buffer;
  auto lastActivity = std::chrono::steady_clock::now();
  const auto idleLimit = std::chrono::seconds(120);
  char chunk[64 * 1024];

  // Reads more bytes into the buffer; false when the connection should end.
  auto readMore = [&]() -> bool {
    while (!stopping_) {
      int ready = waitReadable(s, 250);
      if (ready < 0) return false;
      if (ready == 0) {
        if (std::chrono::steady_clock::now() - lastActivity > idleLimit) return false;
        continue;
      }
      auto n = ::recv(s, chunk, int(sizeof chunk), 0);
      if (n <= 0) return false;
      buffer.append(chunk, size_t(n));
      lastActivity = std::chrono::steady_clock::now();
      return true;
    }
    return false;
  };

  while (!stopping_) {
    size_t end = buffer.find("\r\n\r\n");
    if (end == std::string::npos) {
      if (buffer.size() > kMaxBody || !readMore()) break;
      continue;
    }
    // Request line and headers.
    Request req;
    std::string version;
    {
      std::string head = buffer.substr(0, end);
      size_t lineEnd = head.find("\r\n");
      std::string first = head.substr(0, lineEnd);
      std::vector<std::string> parts;
      size_t pos = 0;
      while (pos < first.size()) {
        size_t sp = first.find(' ', pos);
        if (sp == std::string::npos) sp = first.size();
        if (sp > pos) parts.push_back(first.substr(pos, sp - pos));
        pos = sp + 1;
      }
      if (parts.size() < 2) break;
      for (auto& ch : parts[0]) ch = char(std::toupper(static_cast<unsigned char>(ch)));
      req.method = parts[0];
      req.path = parts[1].substr(0, parts[1].find('?'));
      version = parts.size() > 2 ? parts[2] : "HTTP/1.0";
      size_t lpos = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
      while (lpos < head.size()) {
        size_t next = head.find("\r\n", lpos);
        if (next == std::string::npos) next = head.size();
        std::string line = head.substr(lpos, next - lpos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) req.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
        lpos = next + 2;
      }
    }
    size_t contentLength = 0;
    if (auto it = req.headers.find("content-length"); it != req.headers.end()) {
      try {
        long long n = std::stoll(it->second);
        contentLength = n > 0 ? size_t(n) : 0;
      } catch (...) {
        contentLength = 0;
      }
    }
    if (contentLength > kMaxBody) break;
    if (buffer.size() < end + 4 + contentLength) {
      if (!readMore()) break;
      continue;
    }
    req.body = buffer.substr(end + 4, contentLength);
    buffer.erase(0, end + 4 + contentLength);

    std::string connection = lower(req.headers.count("connection") ? req.headers["connection"] : "");
    bool keepAlive = version == "HTTP/1.0" ? connection.find("keep-alive") != std::string::npos
                                           : connection.find("close") == std::string::npos;

    Response res = handler_(req);
    if (res.drop) break;
    std::string out = "HTTP/1.1 " + std::to_string(res.status) + " " + reason(res.status) + "\r\n";
    if (res.json) out += "Content-Type: application/json\r\n";
    out += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
    out += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    for (auto& [k, v] : res.headers) out += k + ": " + v + "\r\n";
    out += "\r\n";
    out += res.body;
    if (!sendAll(s, out) || !keepAlive) break;
    lastActivity = std::chrono::steady_clock::now();
  }
  ::shutdown(s, SHUT_SEND);
  conn->done = true;
}

}  // namespace sculpt::mcp
