// The world editor's panels (docs/mod-editor.md): the Editor tab of the settings overlay and the parts the
// launch mode's own overlays (editor_shell.cpp) show, all on the render thread. Everything they change
// goes through the command layer (editor/commands.h), run in order on a worker thread; what they show is
// a snapshot of the session taken once per frame, so a command that waits for the game never stops a frame.
// Both exist only in the editor launch mode (--editor).
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "../editor/commands.h"

namespace editor_ui {
using editor::Value;

void register_tab();  // main thread, at start-up (extensions.h): the tab and the editor's overlays
void draw();          // the tab's page (render thread)

// ---- shared with the editor's overlays ----
struct Snapshot {
    bool valid = false;
    std::vector<std::string> mods;
    std::string open, undo, redo;
    bool dirty = false;
    Value manifest;   // the open mod's
    Value state;      // read_state
    Value locations;  // list_locations (refreshed every second)
    std::vector<editor::LogEntry> log;
};
void refresh();               // once per frame, before drawing: the snapshot and finished commands
const Snapshot& snapshot();
// Runs a command (in order, on the worker); `then` runs on the render thread with its result.
void command(const std::string& name, const Value& params = Value(), std::function<void(const Value&)> then = nullptr);
bool busy();                  // commands are still running
const std::string& message(bool& ok);  // the last command's outcome
std::string& selected();      // the selected actor's id
// the problem list of the last check (check_package), and the field a clicked problem names
Value& problems();
std::string& focus_field();

void actor_inspector(const Value& m);  // the selected actor's properties, actions and dialogue
void actor_list(const Value& m);       // the open mod's actors (select)
void models_area(const Value& m);
void scenes_area(const Value& m);
void quests_area(const Value& m);
void warp_area();
void camera_test_area();
void log_area();
void problems_area();                   // the last check's problems: a click selects the actor and its field
void import_area();                     // a .glb into the open mod (check, import)
void export_area();                     // check and write the package into a folder
void mod_bar();                         // the open mod, undo / redo, check, save, revert
}  // namespace editor_ui
