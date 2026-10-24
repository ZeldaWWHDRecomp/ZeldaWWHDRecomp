// The world editor's "Use the game's materials (kit)" option of the model import (Outliner > Models, issue #280): a
// model whose texture-free materials are named after a game room's materials draws with that room's own textures
// and light (docs/mod-editor.md, "Using the game's own materials"). The option picks the room (Link's by default),
// lists the room's materials (list_kit_materials) so the Blender materials can be named after them, maps the
// model's other names onto them, and adds "kit" and "materials" to check_model and import_model. It only builds
// commands and draws; the panel (editor_panel.cpp) runs them.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../mods/mod_json.h"

namespace editor_kit_import {
using Value = mods::json::Value;

// A game room the option offers: the warp list's islands and rooms (list_locations), with a readable label
// ("Windfall Island (sea 11)", "Dragon Roost Cavern (Magtail) room 3").
struct Room {
    std::string stage;
    int room = 0;
    std::string label;
};
std::vector<Room> rooms(const Value& locations);
// the label of stage/room among `all`; "<stage> room <n>" for one the list doesn't name
std::string room_label(const std::vector<Room>& all, const std::string& stage, int room);

struct State {
    bool on = false;          // the option
    std::string stage;        // the kit room
    int room = -1;
    bool picked = false;      // chosen by the user (else it follows Link's room)
    Value listed;             // list_kit_materials' result for listed_for
    std::string listed_for;   // "stage:room" the list is for ("" none yet)
    std::string list_error;   // why the room's materials could not be listed
    bool listing = false;     // a list_kit_materials is running
    std::map<std::string, std::string> mapping;  // the model's material -> a game material (the import's "materials")
    char filter[64] = {};     // the room picker's filter
};

// Runs a command; `then` gets its result ({ok, result, error}) later, on the drawing thread.
using Run = std::function<void(const std::string&, const Value&, std::function<void(const Value&)>)>;

// The room Link stands in (read_state's "link"), until the user picks another.
void follow_link(State& s, const Value& link);
// {"stage", "room"}: the import's "kit"
Value kit(const State& s);
// `params` (a check_model or import_model request with "file") with "kit" and "materials" added when the option is on
// and a room is chosen; unchanged otherwise (the import as before).
Value with_kit(Value params, const State& s);
// What the check depends on: changes when the option, the room or the mapping changes ("" while off).
std::string key(const State& s);
// The room's material names, one per line, for "Copy names".
std::string names_text(const Value& listed);
// Whether an import with this check result (check_model's "result") may go ahead: no material is left that has no
// texture and isn't one of the room's.
bool ready(const State& s, const Value& checked);

// The option's part of the import dialog: the checkbox, the room picker, the room's materials and, when `checked`
// (check_model's "result" for the file, with "kit_report") is given, each of the model's materials with what it
// becomes and a picker that maps it onto a game material. True when the kit choice changed (check the file again).
bool draw(State& s, const Value& locations, const Value& link, const Value& checked, const Run& run);
// A kit report (check_model's or import_model's "kit_report"): game materials, own textures and the others.
void draw_report(const Value& report);

}  // namespace editor_kit_import
