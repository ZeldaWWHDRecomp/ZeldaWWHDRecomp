// The world editor's actions on an imported model (Outliner > Models, issue #287): Replace from file... (the model's
// new version under the same id: its placements keep their places and show it), Watch file (replaced by itself when
// the file changes on disk) and Remove (with its placements, after a confirmation that lists them). It only builds
// the commands (replace_model, watch_model, remove_model) and draws; the panel (editor_panel.cpp) runs them.
#pragma once

#include <functional>
#include <string>

#include "../mods/mod_json.h"

namespace editor_model_actions {
using Value = mods::json::Value;

struct State {
    std::string removing;  // the model whose Remove confirmation is open ("" none)
};

// Runs a command; `then` gets its result later.
using Run = std::function<void(const std::string&, const Value&, std::function<void(const Value&)>)>;
// Opens a file chooser for a .glb; calls `picked` with the chosen path (maybe on another thread), never when cancelled.
using Choose = std::function<void(std::function<void(std::string)>)>;

Value replace_params(const std::string& model, const std::string& file);
Value watch_params(const std::string& model, bool on, const std::string& file);
Value remove_params(const std::string& model, bool with_placements);

// One model's row of actions. `model`: its manifest entry; `files`: its list_model_files entry (file, watching,
// placements, items; null while not known yet).
void draw(State& s, const Value& model, const Value& files, const Run& run, const Choose& choose);

}  // namespace editor_model_actions
