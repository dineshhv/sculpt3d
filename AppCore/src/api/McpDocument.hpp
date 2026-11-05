// The MCP server's view of one C API document (sculpt::mcp::Document).
#pragma once

#include "mcp/McpHost.hpp"

struct SculptDoc;

namespace sculpt::api {

class McpDocument : public sculpt::mcp::Document {
 public:
  explicit McpDocument(SculptDoc* d) : doc(d) {}
  std::string name() const override;
  ModelStore& store() override;
  void setView(const sculpt::mcp::ViewRequest& request) override;
  void redraw() override;
  std::vector<uint8_t> screenshotPng(double scale, std::string& error) override;

  SculptDoc* doc;
};

}  // namespace sculpt::api
