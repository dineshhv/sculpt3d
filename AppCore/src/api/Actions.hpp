// Argument-free user actions of the Swift UI (menu items, selection bar,
// sketch bar, item menu…) with the enabled rule each view uses. The state
// JSON lists them under "actions" (id → enabled) and each id is also a
// command name for sculpt_doc_command.
#pragma once

#include <functional>
#include <string>
#include <vector>

struct SculptDoc;

namespace sculpt {
class ModelStore;
}

namespace sculpt::api {

/// Tools ▸ Sketch, and the Sketch menu's tools without an open sketch: on the
/// selected face, else the selected plane, else Top.
void startSketchHere(ModelStore& store);

struct Action {
  std::string id;
  std::string title;
  std::string symbol;
  std::function<bool(SculptDoc&)> enabled;
  /// Performs the action (only called when enabled). May return a UI action
  /// for the front end ("ui.showVariables"…), else "".
  std::function<std::string(SculptDoc&)> run;
};

const std::vector<Action>& actions();
const Action* action(const std::string& id);

}  // namespace sculpt::api
