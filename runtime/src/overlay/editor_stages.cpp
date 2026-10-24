// SPDX-License-Identifier: MPL-2.0
// The editor's Stages tab (editor_panel.h stages_area): the open mod's stages (docs/mod-stages.md). A new stage from
// a ground model (the wizard: the .glb, the template, a title), each stage's rooms, Rebuild, Warp in and Check
// (the problems as a list), and placing what a stage needs where Link stands: spawns, exits (linked to a place of
// the game or a room of the stage), doors with their lock, chests, enemies, return warps, and, in a room of the
// game, the way in. Every change is a command (editor/commands_stages.cpp): one undo step; Rebuild shows it.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "editor_panel.h"
#include "imgui.h"
#include "text_entry.h"

namespace editor_ui {
namespace {
const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kHead(0.95f, 0.85f, 0.55f, 1), kWarn(0.95f, 0.75f, 0.35f, 1), kBad(1, 0.45f, 0.4f, 1), kGood(0.5f, 0.9f, 0.55f, 1);
void muted(const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}
float num(const Value& v, const char* k, float fallback = 0) { return v.get(k).type == Value::Number ? float(v.get(k).number) : fallback; }

struct StagesUi {
    char new_id[33] = "dungeon", new_file[512] = "", new_template[16] = "Cave01", new_title[41] = "";
    char room_file[512] = "";
    std::string stage;   // the selected stage
    Value problems;      // the last check_stage's problems
    std::string checked;
    Value built;         // the last rebuild's results
    int exit_target = 0, door_room = 1, door_lock = 0, chest_item = 0, enemy = 0;
    float lava_size[2] = {1000, 1000};
    Value templates;  // list_stage_templates (asked once)
    bool templates_asked = false, sky_only = false, updraft = false;
} U;
const char* const kLocks[] = {"none", "small_key", "boss", "shutter"};
const char* const kItems[] = {"small_key", "map", "compass", "boss_key", "heart_container"};
const char* const kEnemies[] = {"bokoblin", "chuchu", "keese", "darknut"};
// a template's facts for its line in the list: "GTower  (1 room, sky, doors: boss knob)"
std::string template_line(const Value& t) {
    std::string out = t.get("stage").string() + "  (" + std::to_string(t.get("rooms").array.size()) + (t.get("rooms").array.size() == 1 ? " room" : " rooms");
    if (t.get("sky").boolean) out += ", sky";
    if (!t.get("doors").array.empty()) {
        out += ", doors:";
        for (const auto& d : t.get("doors").array) out += " " + d.string();
    }
    return out + ")";
}

// the open mod's stages (list_stages, the snapshot's: refreshed once a second)
const Value& stage_list() { return snapshot().stages; }
const Value* selected_stage() {
    for (const auto& s : stage_list().get("stages").array)
        if (s.get("id").string() == U.stage) return &s;
    return nullptr;
}
void place(const char* what, Value p) {
    p["stage"] = U.stage;
    p["what"] = what;
    command("place_in_stage", p, nullptr);
}
}  // namespace

void stages_area(const Value& m) {
    ImGui::TextColored(kHead, "Stages");
    muted("Caves and dungeons from your own room models.");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Each room is a .glb with named materials (floor, wall, ...) and UVs, drawn with the materials of a\n"
                          "room of the game. Change, Rebuild, Warp in, look.");
    // ---- the wizard
    if (ImGui::CollapsingHeader("New stage", m.get("stages").array.empty() ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        ImGui::SetNextItemWidth(160);
        text_entry::field("Id##new_stage", U.new_id, sizeof U.new_id, 0, "dungeon");
        ImGui::SetNextItemWidth(-FLT_MIN);
        text_entry::field("##new_stage_file", U.new_file, sizeof U.new_file, 0, "the first room's .glb (a full path)");
        ImGui::SetNextItemWidth(160);
        if (!U.templates_asked) {
            U.templates_asked = true;
            command("list_stage_templates", Value(), [](const Value& r) { U.templates = r.get("result").get("templates"); });
        }
        if (ImGui::BeginCombo("Template##new_stage", U.new_template, ImGuiComboFlags_HeightLarge)) {
            ImGui::Checkbox("Only stages with a sky", &U.sky_only);
            for (const auto& t : U.templates.array) {
                if (U.sky_only && !t.get("sky").boolean) continue;
                const std::string name = t.get("stage").string();
                if (ImGui::Selectable((template_line(t) + "##" + name).c_str(), name == U.new_template))
                    std::snprintf(U.new_template, sizeof U.new_template, "%s", name.c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        text_entry::field("Map title##new_stage", U.new_title, sizeof U.new_title, 0, "(no map)");
        muted("The template gives the light, the camera, the doors and the kit room (its materials). With a map title the "
              "stage has a dungeon map.");
        ImGui::BeginDisabled(!*U.new_id || !*U.new_file);
        if (ImGui::Button("Create, build and warp in")) {
            Value imp;
            imp["file"] = std::string(U.new_file);
            imp["confirm"] = true;
            const std::string id = U.new_id, tmpl = U.new_template, title = U.new_title;
            command("import_ground", imp, [id, tmpl, title](const Value& r) {
                if (!r.get("ok").boolean) return;
                Value add;
                add["id"] = id;
                add["ground"] = r.get("result").get("ground");
                add["template"] = tmpl;
                if (!title.empty()) add["title"] = title;
                command("add_stage", add, [id](const Value& a) {
                    if (!a.get("ok").boolean) return;
                    U.stage = id;
                    Value b;
                    b["stage"] = id;
                    command("rebuild_stage", b, [id](const Value& built) {
                        U.built = built.get("result");
                       
                        Value w;
                        w["stage"] = "stage:" + snapshot().open + "/" + id;
                        w["room"] = 0;
                        w["point"] = 0;
                        command("warp", w);
                    });
                });
            });
        }
        ImGui::EndDisabled();
    }
    // ---- the stages
    const auto& stages = stage_list().get("stages").array;
    if (stages.empty()) {
        muted("The mod has no stage yet.");
        return;
    }
    if (!selected_stage()) U.stage = stages.front().get("id").string();  // none yet, or one of another mod
    ImGui::Separator();
    for (const auto& s : stages) {
        const std::string id = s.get("id").string();
        std::string label = id + (s.get("built").boolean ? "" : "  (not built)");
        if (ImGui::Selectable((label + "##stage").c_str(), id == U.stage)) U.stage = id;
    }
    const Value* s = selected_stage();
    if (!s) return;
    ImGui::Separator();
    if (ImGui::Button("Rebuild")) {
        Value p;
        p["stage"] = U.stage;
        command("rebuild_stage", p, [](const Value& r) { U.built = r.get("result"); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Warp in")) {
        Value p;
        p["stage"] = "stage:" + snapshot().open + "/" + U.stage;
        p["room"] = 0;
        const auto& rooms = s->get("rooms").array;
        p["point"] = rooms.empty() || rooms[0].get("spawns").array.empty() ? 0.0 : rooms[0].get("spawns").array[0].number;
        command("warp", p);
    }
    ImGui::SameLine();
    if (ImGui::Button("Check")) {
        Value p;
        p["stage"] = U.stage;
        U.checked = U.stage;
        command("check_stage", p, [](const Value& r) { U.problems = r.get("result").get("problems"); });
    }
    for (const auto& b : U.built.get("stages").array) {
        if (b.get("id").string() != U.stage) continue;
        if (b.get("ok").boolean) ImGui::TextColored(kGood, "Built: %d rooms", int(num(b, "rooms")));
        else ImGui::TextColored(kBad, "Not built: %s", b.get("problem").string().c_str());
        for (const auto& n : b.get("notes").array) muted(n.string());
    }
    if (U.checked == U.stage) {
        if (U.problems.array.empty()) ImGui::TextColored(kGood, "No problems.");
        for (const auto& pr : U.problems.array) {
            ImGui::PushStyleColor(ImGuiCol_Text, pr.get("severity").string() == "error" ? kBad : kWarn);
            ImGui::TextWrapped("%s", pr.get("message").string().c_str());
            ImGui::PopStyleColor();
        }
    }
    // ---- its rooms
    ImGui::Spacing();
    ImGui::TextColored(kHead, "Rooms");
    for (const auto& r : s->get("rooms").array)
        ImGui::BulletText("room %d: %s (kit %s, %dF, %d exits, %d actors)", int(num(r, "room")), r.get("ground").string().c_str(), r.get("kit").string().c_str(),
                          int(num(r, "floor", 1)), int(num(r, "exits")), int(num(r, "actors")));
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::SetNextItemWidth(-(ImGui::CalcTextSize("Add room").x + st.FramePadding.x * 2 + st.ItemSpacing.x));
    text_entry::field("##room_file", U.room_file, sizeof U.room_file, 0, "another room's .glb (a full path)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!*U.room_file);
    if (ImGui::Button("Add room")) {
        Value imp;
        imp["file"] = std::string(U.room_file);
        imp["confirm"] = true;
        const std::string stage = U.stage;
        command("import_ground", imp, [stage](const Value& r) {
            if (!r.get("ok").boolean) return;
            Value add;
            add["stage"] = stage;
            add["ground"] = r.get("result").get("ground");
            command("add_room", add, nullptr);
        });
    }
    ImGui::EndDisabled();
    // ---- placing where Link stands
    ImGui::Spacing();
    ImGui::TextColored(kHead, "Where Link stands");
    const Value& link = snapshot().state.get("link");
    std::string here;
    for (const auto& x : stages)
        if (x.get("game_name").string() == link.get("stage").string()) here = x.get("id").string();
    if (here != U.stage) {
        muted(here.empty() ? "Link is in a room of the game: the way into the stage goes here." : "Link is in stage " + here + ".");
        if (here.empty() && link.get("valid").boolean) {
            ImGui::Checkbox("An updraft (the wind lifts Link first)", &U.updraft);
            if (ImGui::Button("Entry here (the way into room 0)")) {
                Value p;
                if (U.updraft) p["style"] = "updraft";
                place("entry", p);
            }
        }
        muted("Warp in to place spawns, exits, doors, chests and enemies.");
        return;
    }
    if (ImGui::Button("Spawn here")) place("spawn", Value());
    ImGui::SameLine();
    if (ImGui::Button("Return warp here")) place("warp", Value());
    // an exit: to the stage's entrance, to a room of the stage
    std::vector<std::pair<std::string, Value>> targets;
    {
        // out: the stage's entrance (the place of the game a save made inside continues at)
        for (const auto& ms : m.get("stages").array)
            if (ms.get("id").string() == U.stage && ms.get("entrance").type == Value::Object) {
                const Value& to = ms.get("entrance");
                targets.push_back({"out: " + to.get("stage").string() + " room " + std::to_string(int(num(to, "room"))) + ", point " +
                                       std::to_string(int(num(to, "point"))),
                                   to});
            }
        for (const auto& r : s->get("rooms").array)
            for (const auto& sp : r.get("spawns").array) {
                Value t;
                t["room"] = r.get("room"), t["point"] = sp;
                targets.push_back({"room " + std::to_string(int(num(r, "room"))) + ", spawn " + std::to_string(int(sp.number)), t});
            }
    }
    if (targets.empty()) return;
    if (U.exit_target >= int(targets.size())) U.exit_target = 0;
    ImGui::SetNextItemWidth(280);
    if (ImGui::BeginCombo("##exit_to", targets[size_t(U.exit_target)].first.c_str())) {
        for (size_t i = 0; i < targets.size(); ++i)
            if (ImGui::Selectable(targets[i].first.c_str(), int(i) == U.exit_target)) U.exit_target = int(i);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Exit here")) {
        Value p;
        p["to"] = targets[size_t(U.exit_target)].second;
        place("exit", p);
    }
    // a door to another room, with its lock
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("##door_room", &U.door_room);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::Combo("##door_lock", &U.door_lock, kLocks, 4);
    ImGui::SameLine();
    if (ImGui::Button("Door here (to that room)")) {
        Value p;
        p["to_room"] = U.door_room;
        p["lock"] = kLocks[U.door_lock];
        if (U.door_lock == 3) p["switch"] = 0;
        place("door", p);
    }
    ImGui::SetNextItemWidth(120);
    ImGui::Combo("##chest_item", &U.chest_item, kItems, 5);
    ImGui::SameLine();
    if (ImGui::Button("Chest ahead")) {
        Value p;
        p["item"] = kItems[U.chest_item];
        place("chest", p);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("##enemy", &U.enemy, kEnemies, 4);
    ImGui::SameLine();
    if (ImGui::Button("Enemy ahead")) {
        Value p;
        p["actor"] = kEnemies[U.enemy];
        place("enemy", p);
    }
    ImGui::SameLine();
    if (ImGui::Button("Pot ahead")) place("pot", Value());
    // the game's lava floor at Link's feet (its size: the fields, or the corner handle in the picture) and its heat haze
    ImGui::SetNextItemWidth(160);
    ImGui::DragFloat2("Lava size (x, z)", U.lava_size, 10, 100, 25500, "%.0f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The area of the game's lava floor, in game units (100 per metre)");
    if (ImGui::Button("Lava floor here")) {
        Value p;
        p["size"]["x"] = double(int(U.lava_size[0])), p["size"]["z"] = double(int(U.lava_size[1]));
        place("lava", p);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game's moving lava, its surface at Link's feet: give the room's model a col_lava surface there");
    ImGui::SameLine();
    if (ImGui::Button("Heat shimmer here")) place("heat_shimmer", Value());
    // the selected lava floor (its handle in the picture): its size
    {
        std::string st, field;
        if (stage_item(st, field) && st == U.stage && field.size() > 9 && field.compare(field.size() - 9, 9, "/position") == 0) {
            const std::string base = field.substr(0, field.size() - 9);
            const Value* item = nullptr;
            for (const auto& ms : m.get("stages").array)
                if (ms.get("id").string() == st) {
                    const Value* at = &ms;
                    size_t from = 0;
                    while (at && from <= base.size()) {
                        const size_t end = base.find('/', from);
                        const std::string k = base.substr(from, end == std::string::npos ? std::string::npos : end - from);
                        if (at->type == Value::Array) {
                            const size_t n = size_t(std::atoi(k.c_str()));
                            at = n < at->array.size() ? &at->array[n] : nullptr;
                        } else
                            at = at->get(k).type == Value::Null ? nullptr : &at->get(k);
                        if (end == std::string::npos) break;
                        from = end + 1;
                    }
                    item = at;
                }
            if (item && item->get("actor").string() == "lava") {
                ImGui::TextColored(kHead, "Lava floor %s", item->get("id").string().c_str());
                float size[2] = {num(item->get("size"), "x", 1000), num(item->get("size"), "z", 1000)};
                ImGui::SetNextItemWidth(160);
                ImGui::DragFloat2("Size (x, z)", size, 10, 100, 25500, "%.0f");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    Value p;
                    p["stage"] = st, p["field"] = base + "/size";
                    p["value"]["x"] = double(int(size[0])), p["value"]["z"] = double(int(size[1]));
                    command("set_stage_field", p, nullptr);
                }
                if (ImGui::Button("Remove this lava floor")) {
                    Value p;
                    p["stage"] = st, p["field"] = base, p["value"] = Value();
                    command("set_stage_field", p, nullptr);
                }
            }
        }
    }
    muted("An exit's box is 4 m wide around where Link stands: walking onto its floor takes him to the target. The picture shows the stage's spawns (green), exits (blue), doors (orange), chests (yellow) and warps (purple). "
          "Rebuild to see a change in the game.");
}

}  // namespace editor_ui
