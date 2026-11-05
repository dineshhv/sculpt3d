// Internal: the MCP tools (port of App/Sources/MCP/MCPTools.swift), the
// document registry (DocumentRegistry in MCPServer.swift) and JSON output
// matching Darwin's JSONSerialization.
#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mcp/McpHost.hpp"
#include "sculpt/model/Uuid.hpp"

namespace sculpt::mcp {

using json = nlohmann::json;

/// A failure reported back to the model as a tool result (MCPToolError).
struct ToolError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

/// JSON text as Darwin's JSONSerialization writes it with
/// [.sortedKeys, .withoutEscapingSlashes]: keys in byte order, doubles as
/// "%.17g", no escaped slashes, UTF-8 kept as is.
std::string serialize(const json& value);

/// The documents open in the app, most recently active first.
class DocumentRegistry {
 public:
  struct Entry {
    Uuid id;
    Document* doc;
  };

  std::vector<Entry> open() const;
  std::optional<Entry> active() const;
  void add(Document* doc);
  void remove(Document* doc);
  void activate(Document* doc);
  bool activate(const Uuid& id);

  void setNewDocumentHandler(std::function<Document*(const std::string&)> h);
  std::function<Document*(const std::string&)> newDocumentHandler() const;

 private:
  mutable std::recursive_mutex mutex_;
  std::vector<Entry> entries_;
  std::function<Document*(const std::string&)> newDocument_;
};

/// The tools Sculpt3D offers over MCP. Call only on the host UI thread.
class Toolbox {
 public:
  explicit Toolbox(DocumentRegistry& registry);
  ~Toolbox();

  /// The tools/list entries, in the Swift order.
  json definitions() const;
  bool has(const std::string& name) const;
  /// Runs a tool; returns its content blocks. Throws ToolError (or other exceptions).
  json call(const std::string& name, const json& arguments);

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace sculpt::mcp
