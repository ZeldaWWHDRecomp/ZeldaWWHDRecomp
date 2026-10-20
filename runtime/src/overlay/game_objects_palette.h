// The world editor's "Game objects" palette and Inspector picker (docs/mod-editor.md): the game's own
// static objects a mod places by reference ("actor": "game:palm"). The palette shows list_game_objects
// (editor/commands_game_objects.cpp) by category with a search field; an object the player's game files
// don't have shows greyed out with "not available in your game files". Picking one starts placing it
// (click into the picture). The Inspector picker changes a placed game object's variant, its turn and
// its uniform scale within the catalogue's limits, through the ordinary editor commands (undoable).
// Render thread only; offscreen-tested in runtime/tools/game_objects_palette_ui_test.cpp.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../mods/mod_json.h"
#include "imgui.h"

namespace overlay::game_objects {
using mods::json::Value;

struct Palette {
    char query[64] = {};
    std::string category;          // "" all
    std::map<std::string, ImVec2> shown;  // test aid: where each listed type's row was drawn (centre)
};

inline std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
inline bool matches(const Value& o, const std::string& query, const std::string& category) {
    if (!category.empty() && o.get("category").string() != category) return false;
    if (query.empty()) return true;
    const std::string q = lower(query);
    return lower(o.get("id").string() + " " + o.get("label").string() + " " + o.get("description").string() + " " + o.get("category").string()).find(q) !=
           std::string::npos;
}

// The palette from a list_game_objects result. Returns the chosen type ("game:palm") or "".
inline std::string draw(Palette& p, const Value& list) {
    std::string chosen;
    p.shown.clear();
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##game_objects_search", "search", p.query, sizeof p.query);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("##game_objects_category", p.category.empty() ? "All categories" : p.category.c_str())) {
        if (ImGui::Selectable("All categories", p.category.empty())) p.category.clear();
        for (const auto& c : list.get("categories").array)
            if (ImGui::Selectable(c.string().c_str(), p.category == c.string())) p.category = c.string();
        ImGui::EndCombo();
    }
    std::string heading;
    unsigned listed = 0;
    for (const auto& c : list.get("categories").array) {
        for (const auto& o : list.get("objects").array) {
            if (o.get("category").string() != c.string() || !matches(o, p.query, p.category)) continue;
            if (heading != c.string()) ImGui::SeparatorText((heading = c.string()).c_str());
            ++listed;
            const bool missing = o.get("available").type == Value::Bool && !o.get("available").boolean;
            const std::string type = o.get("type").string();
            std::string label = o.get("label").string();
            if (o.get("live").type == Value::Bool && !o.get("live").boolean && !missing) label += "  (after a restart)";
            // an island building is made from the player's game files when first placed (story_game_pieces.h)
            if (o.get("preparing").boolean) label += "  (preparing...)";
            ImGui::PushID(type.c_str());
            ImGui::BeginDisabled(missing);
            if (ImGui::Selectable(label.c_str(), false) && !missing) chosen = type;
            ImGui::EndDisabled();
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            p.shown[type] = ImVec2((a.x + b.x) / 2, (a.y + b.y) / 2);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s%s%s", o.get("description").string().c_str(), missing ? "\nnot available in your game files" : "",
                                  o.get("building").boolean ? "\nmade from your game files when you place it" : "");
            if (missing) {
                ImGui::SameLine();
                ImGui::TextDisabled("not available in your game files");
            }
            ImGui::PopID();
        }
    }
    if (!listed) ImGui::TextDisabled(list.get("objects").array.empty() ? "no game objects in this build" : "nothing matches");
    return chosen;
}

// What a slider holds while it is dragged (the manifest changes only when it is let go).
struct Picker {
    std::string id;
    float yaw = 0, scale = 1;
    bool yaw_held = false, scale_held = false;
};
// The Inspector picker for a placed game object `a` (its manifest entry) with its catalogue entry `info`
// (game_object_info). Returns the commands to run, each [name, params] (params carry "id").
inline std::vector<std::pair<std::string, Value>> inspector(Picker& k, const Value& a, const Value& info) {
    std::vector<std::pair<std::string, Value>> out;
    const std::string id = a.get("id").string();
    if (k.id != id) k = Picker{id};
    ImGui::TextUnformatted("Variant");
    const auto& variants = info.get("variants").array;
    std::string current = a.get("variant").string();
    if (current.empty() && !variants.empty()) current = variants.front().get("id").string();
    std::string shown = current;
    for (const auto& v : variants)
        if (v.get("id").string() == current) shown = v.get("label").string();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::BeginDisabled(variants.size() < 2);
    if (ImGui::BeginCombo("##game_object_variant", shown.c_str())) {
        for (size_t i = 0; i < variants.size(); i++) {
            const std::string vid = variants[i].get("id").string();
            if (ImGui::Selectable(variants[i].get("label").string().c_str(), vid == current) && vid != current) {
                Value s;
                s["id"] = id, s["property"] = "variant";
                s["value"] = i == 0 ? Value() : Value(vid);  // the first variant is the default: no field
                out.push_back({"set_actor", s});
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    const Value& rot = a.get("rotation");
    if (!k.yaw_held) k.yaw = rot.get("y").type == Value::Number ? float(rot.get("y").number) : 0.f;
    ImGui::TextUnformatted("Turn (degrees)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##game_object_yaw", &k.yaw, 0, 360, "%.0f");
    k.yaw_held = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        Value r, q;
        r["x"] = 0, r["y"] = std::round(double(k.yaw) * 10) / 10, r["z"] = 0;
        q["id"] = id, q["rotation"] = r;
        out.push_back({"rotate_actor", q});
    }
    const double lo = info.get("scale").get("min").number, hi = info.get("scale").get("max").number;
    const Value& sc = a.get("scale");
    if (!k.scale_held) k.scale = sc.get("x").type == Value::Number ? float(sc.get("x").number) : 1.f;
    ImGui::TextUnformatted("Scale");
    if (lo >= hi) {
        ImGui::TextDisabled("1 (this object keeps its size: its collision doesn't scale)");
    } else {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##game_object_scale", &k.scale, float(lo), float(hi), "%.2f");
        k.scale_held = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const double v = std::round(std::clamp(double(k.scale), lo, hi) * 100) / 100;
            Value q, p;
            p["x"] = v, p["y"] = v, p["z"] = v;
            q["id"] = id, q["scale"] = p;
            out.push_back({"scale_actor", q});
        }
    }
    ImGui::TextDisabled("%s", info.get("description").string().c_str());
    if (!info.get("collision").boolean) ImGui::TextDisabled("Link walks through it.");
    return out;
}

}  // namespace overlay::game_objects
