// The world editor's panels (docs/mod-editor.md): the Editor tab of the settings overlay and the parts the
// launch mode's own overlays (editor_shell.cpp) show, all on the render thread. Everything they change
// goes through the command layer (editor/commands.h), run in order on a worker thread; what they show is
// a snapshot of the session taken once per frame, so a command that waits for the game never stops a frame.
// Both exist only in the editor launch mode (--editor).
#pragma once
#include <array>
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
    Value villagers;  // list_villagers: the game's villagers the NPC catalogue knows, loaded now (twice a second)
    Value stages;     // list_stages: the open mod's stages, built or not (once a second, when it has any)
    Value model_files;  // list_model_files: the open mod's models, the file each came from, which are watched (twice a second)
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
bool stage_item(std::string& stage, std::string& field);  // the stage item whose handle shows in the picture (stage id, its field)
void stages_area(const Value& m);      // the Stages tab (editor_stages.cpp): new stages, rooms, rebuild, warp, check, place here
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

// ---- letters on their own, treasure charts and items (editor_story.cpp) ----
void story_area(const Value& m);        // the Story tab: list, add, edit and remove the mod's letters, charts and items
void show_story(const std::string& kind, const std::string& id);  // a problem or search result: select it there

// ---- the scene editor (editor_scenes.cpp) ----
void scene_editor(const Value& m);      // the Scenes window: steps, timeline, inspector, trigger, native version, previews
void show_scene(const std::string& scene, int step);  // open the Scenes window at that scene and step (0: none)
bool scenes_focused();                  // the Scenes window has the keyboard (Ctrl+C / Ctrl+V are its own)
// What the picture shows of the scene editor: the shown scene, its selected step and walk point (1-based,
// 0 none) and the timeline's cursor (frames, < 0 none). False without a scene.
bool scene_picture(std::string& scene, int& step, int& point, double& scrub);
// A demo step's part of it: the demo frame at the cursor (< 0 outside the step) and the selected key
// (lane and frame; frame < 0 none).
bool demo_picture(double& frame, std::string& lane, int& key);

// ---- an actor's route (editor_route.cpp) ----
void route_section(const Value& m, const Value& actor);  // the Inspector's Route section
// What the picture shows of the selected actor's route: its id, the selected point (1-based, 0 none)
// and the segments the ground check marked (from, to; a resident's 0 is its position). False: none.
bool route_picture(std::string& actor, int& point, std::vector<std::pair<int, int>>& bad);
void route_select_point(int point);  // a click on a point in the picture

// ---- the mod's room paths (editor_paths.cpp) ----
void path_section(const Value& m, const Value& actor);  // the Inspector's Path section (an actor that uses a room path)
// The path the picture shows with its points and handle (the selected actor's) and its selected point
// (1-based, 0 none). False: none.
bool path_picture(std::string& path, int& point);
void path_select_point(int point);  // a click on a point in the picture
// ---- the water tool (editor_water.cpp): ponds, rivers and waterfalls drawn in the picture
struct WaterDraft {
    bool open = false;        // the Water window
    std::string kind = "pond";  // pond, river or waterfall
    std::vector<std::array<float, 3>> points;  // world places (as picked; a pond's outline, a river's centre line, a waterfall's edge)
    int selected = -1;        // the selected point (its move handle in the picture)
    bool drawing = false;     // clicks into the picture add points
    bool dragging = false;    // a point's or the level's handle is held (no check meanwhile)
    float level = 0;          // pond: the water's height (level_set: chosen, else the lowest bank the check found)
    bool level_set = false;
    int depth = 200, rim = 0, width = 400, top = 0, bottom = 0, side = 0;  // side 0: the lower side
    bool top_set = false, bottom_set = false, foam = false;
    bool lava = false;  // pond: a lava pool instead of water
    float floor[3] = {0.48f, 0.55f, 0.36f};
    std::vector<bool> shallow;  // river: per section
    std::string editing;        // the body of water being changed ("" a new one)
    Value check;                // check_water's last answer
    std::string checked;
    bool checking = false;
};
WaterDraft& water_draft();
void water_window(const Value& m);                      // the Water window
void water_section(const Value& m, const Value& actor); // the Inspector's Water section (a body made with the tool)
bool water_actor(const Value& m, const Value& actor);   // made with the water tool (its model is under models/water/)
void water_start(const std::string& kind);              // a new pond, river or waterfall: the window open, drawing
void water_edit(const std::string& id);                 // its points and settings into the window
void water_add_point(float x, float y, float z);        // a click on the ground while drawing
void water_test_tick();                                 // test aid WWHD_TEST_WATER (editor_water.cpp)
}  // namespace editor_ui
