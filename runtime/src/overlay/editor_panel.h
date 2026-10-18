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
void mcp_area();                        // Claude / MCP: the switch, the address, the token and the command
void mcp_prompt();                      // a tool client's Allow / Deny question, when one waits

// ---- the quest editor (editor_quests.cpp) ----
void quest_editor(const Value& m);      // the Quests window: the graph (or the list) and its inspector
void quest_live(const Value& m);        // while playing: the graph with the game's step highlighted
bool quest_graph_while_playing();       // the user wants the graph over the game while playing
bool quest_connecting();                // two steps are being connected (the drag, or the popup that completes it)
void cancel_quest_connect();            // Esc: drop that connection
void flags_area();                      // the open mod's flags and their values (test mode: editable)
void search_area();                     // search the open mod
std::string quest_status();             // "Lighthouse quest: step 3 of 6" for the status bar ("" none)
void show_quest(const std::string& quest, const std::string& field);  // a problem was clicked: open that quest and step
std::vector<std::string>& multi_selection();  // the selected actors (Ctrl or Shift adds); selected() is the main one

// ---- the scene editor (editor_scenes.cpp) ----
void scene_editor(const Value& m);      // the Scenes window: steps, timeline, inspector, trigger, native version, previews
void show_scene(const std::string& scene, int step);  // open the Scenes window at that scene and step (0: none)
bool scenes_focused();                  // the Scenes window has the keyboard (Ctrl+C / Ctrl+V are its own)
// What the picture shows of the scene editor: the shown scene, its selected step and walk point (1-based,
// 0 none) and the timeline's cursor (frames, < 0 none). False without a scene.
bool scene_picture(std::string& scene, int& step, int& point, double& scrub);
}  // namespace editor_ui
