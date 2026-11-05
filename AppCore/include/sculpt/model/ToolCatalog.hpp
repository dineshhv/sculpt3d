// Port of Commands.swift's ToolCategory / ToolCommand / ToolCatalog: every
// runnable tool of the rail, menus and search palette, with its enabled rule.
//
// Swift's `run` took the store, viewport and UI state. Here `run` performs the
// model part on the store and returns the UI action the front end still has
// to do (empty when none): "ui.showVariables", "ui.importImage",
// "ui.importFile", "ui.importProject", "view.fit", "view.iso", "view.top",
// "view.front", "view.right", "view.toggleSection", "view.toggleMeasure",
// "view.screenshot", "view.mode.<shaded|wireframe|xray|visualized|zebra|curvature>",
// "export.<step|iges|stl|brep>" (save the bytes of ModelStore::exportExchange).
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sculpt {

class ModelStore;

enum class ToolCategory { sketch, insert, construct, transform, tools, visualize, view };
const std::vector<ToolCategory>& allToolCategories();
/// Groups shown on the rail; View lives on the right-hand controls.
const std::vector<ToolCategory>& railCategories();
std::string rawValue(ToolCategory c);
std::string title(ToolCategory c);
std::string symbol(ToolCategory c);

struct ToolCommand {
  std::string id;
  std::string title;
  std::string symbol;
  ToolCategory category;
  std::string hint;
  std::function<bool(const ModelStore&)> isEnabled;
  std::function<std::string(ModelStore&)> run;
};

namespace ToolCatalog {
bool idle(const ModelStore& s);
bool hasBody(const ModelStore& s);
const std::vector<ToolCommand>& all();
std::vector<ToolCommand> commands(ToolCategory category);
const ToolCommand* command(const std::string& id);
/// Viewport single-key shortcuts (outside sketches): key → command id.
const std::map<char, std::string>& keyShortcuts();
/// The key shown next to a command, if it has one ("E", "⌘U", …).
std::optional<std::string> shortcut(const std::string& id);
/// The search palette (Panels.swift): matches on "title category hint"; an
/// empty query lists the enabled commands.
std::vector<const ToolCommand*> search(const ModelStore& store, const std::string& query);
/// Runs a command if it is enabled; returns its UI action, or nothing when disabled/unknown.
std::optional<std::string> run(ModelStore& store, const std::string& id);
/// The model side of ViewportController+Keys.swift `handleKey`: in a sketch,
/// tool keys and Delete/Backspace; otherwise the tool shortcuts. `key` is a
/// single character ("l", "\x7f", "\b") or "Escape"/"Delete"/"Backspace".
/// Returns the UI action (possibly empty) when handled, nothing when not.
/// (Escape while picking points is the viewport's.)
std::optional<std::string> handleKey(ModelStore& store, const std::string& key);
}  // namespace ToolCatalog

}  // namespace sculpt
