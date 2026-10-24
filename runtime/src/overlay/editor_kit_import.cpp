// The model import's "Use the game's materials (kit)" option (editor_kit_import.h).
#include "editor_kit_import.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>

#include "imgui.h"

namespace editor_kit_import {
namespace {
const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kGood(0.45f, 0.82f, 0.52f, 1), kBad(0.95f, 0.55f, 0.45f, 1), kOwn(0.55f, 0.75f, 0.95f, 1);

void muted(const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}
void coloured(const ImVec4& c, const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}
std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
std::string room_key(const std::string& stage, int room) { return stage + ":" + std::to_string(room); }
}  // namespace

std::vector<Room> rooms(const Value& locations) {
    std::vector<Room> out;
    for (const auto& l : locations.array) {
        const std::string group = l.get("group").string();
        if (group != "Islands" && group != "Rooms") continue;
        Room r;
        r.stage = l.get("stage").string();
        r.room = int(l.get("room").number);
        std::string label = l.get("label").string();
        if (group == "Islands") {
            // "11  Windfall Island" -> "Windfall Island (sea 11)"
            const auto gap = label.find("  ");
            r.label = (gap == std::string::npos ? label : label.substr(gap + 2)) + " (" + r.stage + " " + std::to_string(r.room) + ")";
        } else {
            for (size_t at; (at = label.find("  ")) != std::string::npos;) label.erase(at, 1);
            r.label = label;
        }
        out.push_back(std::move(r));
    }
    return out;
}

std::string room_label(const std::vector<Room>& all, const std::string& stage, int room) {
    for (const auto& r : all)
        if (r.stage == stage && r.room == room) return r.label;
    return stage + " room " + std::to_string(room);
}

void follow_link(State& s, const Value& link) {
    if (s.picked || !link.get("valid").boolean || link.get("stage").string().empty()) return;
    s.stage = link.get("stage").string();
    s.room = int(link.get("room").number);
}

Value kit(const State& s) {
    Value k;
    k["stage"] = s.stage;
    k["room"] = double(s.room);
    return k;
}

Value with_kit(Value params, const State& s) {
    if (!s.on || s.stage.empty() || s.room < 0) return params;
    params["kit"] = kit(s);
    Value m;
    m.type = Value::Object;
    for (const auto& [mine, game] : s.mapping)
        if (!mine.empty() && !game.empty()) m[mine] = game;
    if (!m.object.empty()) params["materials"] = m;
    return params;
}

std::string key(const State& s) {
    if (!s.on || s.stage.empty() || s.room < 0) return {};
    std::string k = room_key(s.stage, s.room);
    for (const auto& [mine, game] : s.mapping)
        if (!game.empty()) k += "|" + mine + "=" + game;
    return k;
}

std::string names_text(const Value& listed) {
    std::string out;
    for (const auto& m : listed.get("materials").array) out += m.get("material").string() + "\n";
    return out;
}

bool ready(const State& s, const Value& checked) {
    if (!s.on) return true;
    if (s.stage.empty() || s.room < 0) return false;
    return checked.get("kit_report").get("unknown").array.empty();
}

void draw_report(const Value& report) {
    for (const auto& m : report.get("materials").array) {
        const std::string name = m.get("name").string().empty() ? std::string("(unnamed)") : m.get("name").string();
        const std::string kind = m.get("kind").string();
        if (kind == "game")
            coloured(kGood, name + ": game material " + m.get("game").string() + (m.get("default").boolean ? " (the room's default)" : ""));
        else if (kind == "own")
            coloured(kOwn, name + ": its own texture");
        else
            coloured(kBad, name + ": no texture and not a material of this room");
    }
}

bool draw(State& s, const Value& locations, const Value& link, const Value& checked, const Run& run) {
    bool changed = false;
    if (ImGui::Checkbox("Use the game's materials (kit)", &s.on)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Materials named after a game room's materials, with their image removed in Blender,\n"
                          "draw with the game's own full-size textures and the island's light.\n"
                          "Materials with an image of their own keep it.");
    if (!s.on) return changed;
    follow_link(s, link);
    const auto all = rooms(locations);
    // the room: Link's until another is picked
    ImGui::TextUnformatted("Room (its materials)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string current = s.stage.empty() ? std::string("choose a room") : room_label(all, s.stage, s.room);
    if (ImGui::BeginCombo("##kit_room", current.c_str(), ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##kit_room_filter", "Filter", s.filter, sizeof s.filter);
        const std::string filter = lower(s.filter);
        for (const auto& r : all) {
            if (!filter.empty() && lower(r.label).find(filter) == std::string::npos) continue;
            const bool on = r.stage == s.stage && r.room == s.room;
            if (ImGui::Selectable((r.label + "##" + room_key(r.stage, r.room)).c_str(), on) && !on) {
                s.stage = r.stage, s.room = r.room, s.picked = true;
                s.mapping.clear();  // the names belong to the other room
                changed = true;
            }
            if (on && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
        }
        ImGui::EndCombo();
    }
    if (s.stage.empty()) {
        muted("Choose the room whose materials the model uses.");
        return changed;
    }
    // the room's materials, listed once per room
    const std::string want = room_key(s.stage, s.room);
    if (s.listed_for != want && !s.listing) {
        s.listing = true;
        s.listed_for = want;
        s.listed = Value();
        s.list_error.clear();
        Value p = kit(s);
        run("list_kit_materials", p, [&s, want](const Value& r) {
            s.listing = false;
            if (s.listed_for != want) return;  // another room was picked meanwhile: its own list follows
            if (r.get("ok").boolean) s.listed = r.get("result");
            else s.list_error = r.get("error").string("the room's materials can't be listed");
        });
    }
    if (s.listing) muted("Reading the room's materials...");
    else if (!s.list_error.empty()) coloured(kBad, s.list_error);
    const auto& listed = s.listed.get("materials").array;
    if (!listed.empty()) {
        if (ImGui::TreeNodeEx("##kit_materials", ImGuiTreeNodeFlags_DefaultOpen, "The room's materials (%zu)", listed.size())) {
            muted("Name your Blender materials exactly like these and delete their Image Texture node.");
            if (ImGui::SmallButton("Copy names")) ImGui::SetClipboardText(names_text(s.listed).c_str());
            ImGui::BeginChild("##kit_material_list", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * float(std::min<size_t>(listed.size(), 8) + 1)),
                              ImGuiChildFlags_Borders);
            const std::string main = s.listed.get("main").string();
            for (const auto& m : listed) {
                std::string textures;
                for (const auto& t : m.get("textures").array) textures += (textures.empty() ? "" : ", ") + t.string();
                const std::string name = m.get("material").string();
                ImGui::TextUnformatted(name.c_str());
                ImGui::SameLine();
                muted((textures.empty() ? std::string("no texture") : textures) + (name == main ? "  (default)" : ""));
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }
    }
    // the model's materials: what each becomes; one the room doesn't have is mapped onto one of the room's
    const Value& report = checked.get("kit_report");
    if (checked.get("kit_report_error").type == Value::String) coloured(kBad, checked.get("kit_report_error").text);
    if (report.type == Value::Object && !report.get("materials").array.empty()) {
        ImGui::TextUnformatted("Your model's materials");
        for (const auto& m : report.get("materials").array) {
            const std::string name = m.get("name").string();
            const std::string kind = m.get("kind").string();
            const auto mapped = s.mapping.find(name);
            if (kind == "own" || name.empty() || (kind == "game" && mapped == s.mapping.end())) {
                draw_report([&] {
                    Value one;
                    one["materials"].type = Value::Array;
                    one["materials"].array.push_back(m);
                    return one;
                }());
                continue;
            }
            // not the room's, or mapped: a picker with the room's names
            coloured(kind == "game" ? kGood : kBad, name + (kind == "game" ? ": mapped to" : ": not in this room; it stands for"));
            ImGui::Indent();
            ImGui::SetNextItemWidth(-FLT_MIN);
            const std::string shown = mapped == s.mapping.end() ? std::string("choose a game material") : mapped->second;
            if (ImGui::BeginCombo(("##map_" + name).c_str(), shown.c_str())) {
                if (mapped != s.mapping.end() && ImGui::Selectable("(none: its own name)")) s.mapping.erase(name), changed = true;
                for (const auto& g : listed) {
                    const std::string game = g.get("material").string();
                    if (ImGui::Selectable(game.c_str(), mapped != s.mapping.end() && mapped->second == game)) s.mapping[name] = game, changed = true;
                }
                ImGui::EndCombo();
            }
            ImGui::Unindent();
        }
        if (!report.get("unknown").array.empty())
            muted("Rename these in Blender to one of the room's materials, map them here, or give them an image of their own.");
    }
    return changed;
}

}  // namespace editor_kit_import
