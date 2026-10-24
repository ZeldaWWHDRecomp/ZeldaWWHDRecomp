// The model import's "Use the game's materials (kit)" option (overlay/editor_kit_import.h, issue #280) with the real
// Dear ImGui code and no game: off, the import's commands are as before; a click turns it on, the room is Link's
// ("Windfall Island (sea 11)") and its materials are listed (list_kit_materials) with Copy names; another room can be
// picked; a model material the room doesn't have keeps Import back until it is mapped onto one of the room's, and
// check_model and import_model then carry "kit" and "materials".
#include "../src/overlay/editor_kit_import.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

namespace {
using Value = mods::json::Value;
namespace ki = editor_kit_import;

struct Call {
    std::string name;
    Value params;
    std::function<void(const Value&)> then;
};
std::vector<Call> calls;
std::string clipboard;
ki::State state;
Value locations, link_place, checked;
bool changed = false;

void frame() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Import", nullptr, ImGuiWindowFlags_NoDecoration);
    changed |= ki::draw(state, locations, link_place, checked, [](const std::string& n, const Value& p, std::function<void(const Value&)> then) {
        calls.push_back({n, p, std::move(then)});
    });
    ImGui::End();
    ImGui::Render();
}
// where the item `id` of `window` is: the pointer swept over the window until ImGui says it hovers that item
ImVec2 find(ImGuiWindow* window, ImGuiID id) {
    ImGuiIO& io = ImGui::GetIO();
    const ImRect r = window->Rect();
    for (float y = r.Min.y + 2; y < r.Max.y; y += 3)
        for (float x = r.Min.x + 6; x < std::min(r.Max.x, r.Min.x + 1100); x += 25) {
            io.AddMousePosEvent(x, y);
            frame();
            if (ImGui::GetCurrentContext()->HoveredId == id) return ImVec2(x, y);
        }
    return ImVec2(-1, -1);
}
ImGuiWindow* window() { return ImGui::FindWindowByName("Import"); }
ImGuiWindow* popup() {
    frame();  // an auto-sized popup takes its size in the frames after it opens
    frame();
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->Active && (w->Flags & ImGuiWindowFlags_Popup)) return w;
    return nullptr;
}
void click(ImVec2 at, int line) {
    if (at.x < 0) std::fprintf(stderr, "line %d: the item is not on screen\n", line);
    assert(at.x >= 0 && "the item is on screen");
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(at.x, at.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
}
Value json(const char* t) { return mods::json::parse(t); }
}  // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) { clipboard = text; };

    // the warp list's islands and rooms, in words; quest places and mod stages are not kit rooms
    locations = json(R"J([{"id": "island:11", "label": "11  Windfall Island", "group": "Islands", "stage": "sea", "room": 11},
                         {"id": "island:13", "label": "13  Dragon Roost Island", "group": "Islands", "stage": "sea", "room": 13},
                         {"id": "room:M_NewD2:3", "label": "Dragon Roost Cavern (M_NewD2)  room 3", "group": "Rooms", "stage": "M_NewD2", "room": 3},
                         {"id": "actor:m:a", "label": "a  (sea room 11)", "group": "Quest", "stage": "sea", "room": 11}])J");
    const auto rooms = ki::rooms(locations);
    assert(rooms.size() == 3 && rooms[0].label == "Windfall Island (sea 11)" && rooms[2].label == "Dragon Roost Cavern (M_NewD2) room 3");
    assert(ki::room_label(rooms, "sea", 13) == "Dragon Roost Island (sea 13)" && ki::room_label(rooms, "Xboss0", 0) == "Xboss0 room 0");
    link_place = json(R"({"valid": true, "stage": "sea", "room": 11})");

    // off: nothing is asked, the import's commands are as before
    for (int i = 0; i < 2; i++) frame();
    assert(calls.empty() && !state.on);
    const Value file = json(R"({"file": "/tmp/WindfallExtension.glb"})");
    assert(mods::json::dump(ki::with_kit(file, state)) == mods::json::dump(file) && ki::key(state).empty() && ki::ready(state, Value()));

    // a click on the option: Link's room, its materials asked for once
    click(find(window(), window()->GetID("Use the game's materials (kit)")), __LINE__);
    assert(state.on && changed && state.stage == "sea" && state.room == 11 && !state.picked);
    assert(calls.size() == 1 && calls[0].name == "list_kit_materials" && calls[0].params.get("stage").string() == "sea" &&
           calls[0].params.get("room").number == 11);
    frame();
    assert(calls.size() == 1 && "listed once per room");
    // with no check result yet: not ready to import (the check with kit is still to come)
    calls[0].then(json(R"({"ok": true, "result": {"materials": [{"material": "grass_v", "textures": ["a_kusa"], "triangles": 900},
                                                              {"material": "rock_v", "textures": ["m_ren16"], "triangles": 400}], "main": "grass_v"}})"));
    assert(state.listed.get("materials").array.size() == 2 && ki::names_text(state.listed) == "grass_v\nrock_v\n");
    Value params = ki::with_kit(file, state);
    assert(params.get("kit").get("stage").string() == "sea" && params.get("kit").get("room").number == 11 && params.get("materials").type == Value::Null);
    assert(ki::key(state) == "sea:11");
    frame();
    // Copy names: the room's names on the clipboard, one per line
    const ImGuiID tree = window()->GetID("##kit_materials");

    click(find(window(), ImGui::GetIDWithSeed("Copy names", nullptr, tree)), __LINE__);
    assert(clipboard == "grass_v\nrock_v\n");

    // the check (check_model with kit) says my_stone is none of the room's: Import waits; a mapping fixes that
    checked = json(R"({"kind": "kit", "kit_report": {"materials": [{"name": "grass_v.001", "kind": "game", "game": "grass_v"},
                    {"name": "my_sign", "kind": "own"}, {"name": "my_stone", "kind": "unknown"}],
                    "game": ["grass_v.001"], "own": ["my_sign"], "unknown": ["my_stone"]}})");
    assert(!ki::ready(state, checked));
    changed = false;
    frame();
    click(find(window(), window()->GetID("##map_my_stone")), __LINE__);
    ImGuiWindow* list = popup();
    assert(list && "the picker lists the room's materials");
    click(find(list, list->GetID("rock_v")), __LINE__);
    assert(changed && state.mapping.size() == 1 && state.mapping["my_stone"] == "rock_v");
    params = ki::with_kit(file, state);
    assert(params.get("materials").get("my_stone").string() == "rock_v" && params.get("file").string() == "/tmp/WindfallExtension.glb");
    assert(ki::key(state) == "sea:11|my_stone=rock_v");
    // the check again with the mapping: nothing unknown, Import may go
    checked["kit_report"]["materials"].array[2] = json(R"({"name": "my_stone", "kind": "game", "game": "rock_v"})");
    checked["kit_report"]["unknown"].array.clear();
    assert(ki::ready(state, checked));
    frame();

    // another room: picked in the list, its own materials asked for, the old mapping dropped
    changed = false;
    click(find(window(), window()->GetID("##kit_room")), __LINE__);
    list = popup();
    assert(list);
    click(find(list, list->GetID("Dragon Roost Island (sea 13)##sea:13")), __LINE__);
    assert(changed && state.stage == "sea" && state.room == 13 && state.picked && state.mapping.empty());
    frame();
    assert(calls.back().name == "list_kit_materials" && calls.back().params.get("room").number == 13);
    // Link moving on doesn't change a picked room
    link_place = json(R"({"valid": true, "stage": "sea", "room": 44})");
    frame();
    assert(state.room == 13);
    // a room that can't be read: said, nothing listed
    calls.back().then(json(R"({"ok": false, "error": "the kit room's materials can't be listed: no game files"})"));
    assert(state.list_error.find("no game files") != std::string::npos && state.listed.type == Value::Null);
    frame();

    // off again: the import as before
    click(find(window(), window()->GetID("Use the game's materials (kit)")), __LINE__);
    assert(!state.on && mods::json::dump(ki::with_kit(file, state)) == mods::json::dump(file) && ki::ready(state, checked));
    ImGui::DestroyContext();
    std::puts("editor_kit_import_ui: ok");
    return 0;
}
