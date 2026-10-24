// SPDX-License-Identifier: MPL-2.0
// The water tool's window and Inspector section (editor_panel.h, docs/mod-editor.md "Water"): a pond, a river or a
// waterfall drawn in the picture (the points and the handles are drawn by editor_shell.cpp's water_overlay), checked
// as it changes (check_water: errors red, warnings yellow) and made with one command (add_pond, add_river,
// add_waterfall) or changed with one (edit_water): one undo step each.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "editor_panel.h"
#include "imgui.h"

namespace editor_ui {
namespace {
const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kGood(0.45f, 0.82f, 0.52f, 1), kBad(0.95f, 0.45f, 0.40f, 1), kWarn(0.95f, 0.80f, 0.40f, 1), kHead(0.95f, 0.85f, 0.55f, 1);
WaterDraft D;
int water_select_after_edit = -1;  // test aid: the point selected once water_edit's answer is in
float num(const Value& v, const char* k, float fallback = 0) { return v.get(k).type == Value::Number ? float(v.get(k).number) : fallback; }
void muted(const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}
std::string hex(const float c[3]) {
    char b[8];
    std::snprintf(b, sizeof b, "#%02x%02x%02x", int(std::lround(std::clamp(c[0], 0.f, 1.f) * 255)), int(std::lround(std::clamp(c[1], 0.f, 1.f) * 255)),
                  int(std::lround(std::clamp(c[2], 0.f, 1.f) * 255)));
    return b;
}
void colour_from(const std::string& s, float c[3]) {
    if (s.size() != 7 || s[0] != '#') return;
    for (int i = 0; i < 3; i++) c[i] = float(std::strtol(s.substr(size_t(1 + 2 * i), 2).c_str(), nullptr, 16)) / 255.f;
}
const Value* actor_by_id(const Value& m, const std::string& id) {
    for (const auto& a : m.get("actors").array)
        if (a.get("id").string() == id) return &a;
    return nullptr;
}
const char* kind_label(const std::string& k) { return k == "pond" ? "Pond" : k == "river" ? "River" : "Waterfall"; }
}  // namespace

WaterDraft& water_draft() { return D; }

// a body of water made with the tool: its main model's file is under models/water/
bool water_actor(const Value& m, const Value& a) {
    if (a.get("kind").string() != "mod") return false;
    for (const auto& md : m.get("models").array)
        if (md.get("id").string() == a.get("model").string()) return md.get("source").string().rfind("models/water/", 0) == 0;
    return false;
}

Value water_params() {
    Value p;
    p["kind"] = D.kind;
    p["points"].type = Value::Array;
    for (const auto& q : D.points) {
        Value v;
        v["x"] = double(std::lround(q[0])), v["y"] = double(std::lround(q[1])), v["z"] = double(std::lround(q[2]));
        p["points"].array.push_back(v);
    }
    p["floor"] = hex(D.floor);
    if (D.kind == "pond") {
        if (D.level_set) p["level"] = double(std::lround(D.level));
        p["depth"] = double(D.depth), p["rim"] = double(D.rim), p["foam"] = D.foam && !D.lava, p["lava"] = D.lava;
    } else if (D.kind == "river") {
        p["width"] = double(D.width), p["depth"] = double(D.depth);
        p["shallow"].type = Value::Array;
        for (size_t i = 0; i + 1 < D.points.size(); i++) p["shallow"].array.push_back(Value(i < D.shallow.size() && D.shallow[i]));
    } else {
        if (D.top_set) p["top"] = double(D.top);
        if (D.bottom_set) p["bottom"] = double(D.bottom);
        if (D.side) p["side"] = double(D.side);
    }
    return p;
}

void water_start(const std::string& kind) {
    D = WaterDraft();
    D.open = true;
    D.kind = kind;
    D.drawing = true;
}

// "Edit shape": the body's parameters into the draft (get_water), the window open, its points shown
void water_edit(const std::string& id) {
    Value p;
    p["id"] = id;
    command("get_water", p, [id](const Value& r) {
        if (!r.get("ok").boolean) return;
        const Value& w = r.get("result");
        D = WaterDraft();
        D.open = true;
        D.editing = id;
        D.kind = w.get("kind").string();
        // a point's height: the body's own level (pond), the top (waterfall), the levels (river) are only for drawing
        const float y = D.kind == "pond" ? num(w, "level") : D.kind == "waterfall" ? num(w, "top") : 0;
        const auto& levels = w.get("levels").array;
        for (size_t i = 0; i < w.get("points").array.size(); i++) {
            const auto& q = w.get("points").array[i];
            float py = y;
            if (D.kind == "river" && !levels.empty()) py = float(levels[std::min(levels.size() - 1, i * (levels.size() - 1) / std::max<size_t>(1, w.get("points").array.size() - 1))].number);
            D.points.push_back({num(q, "x"), py, num(q, "z")});
        }
        if (D.kind == "pond") D.level = num(w, "level"), D.level_set = true, D.depth = int(num(w, "depth", 200)), D.rim = int(num(w, "rim")), D.foam = w.get("foam").boolean, D.lava = w.get("lava").boolean;
        if (D.kind == "river") {
            D.width = int(num(w, "width", 400)), D.depth = int(num(w, "depth", 200));
            for (const auto& b : w.get("shallow").array) D.shallow.push_back(b.boolean);
        }
        if (D.kind == "waterfall") D.top = int(num(w, "top")), D.top_set = true, D.bottom = int(num(w, "bottom")), D.bottom_set = true, D.side = int(num(w, "side", 1));
        colour_from(w.get("floor").string(), D.floor);
        if (water_select_after_edit >= 0 && water_select_after_edit < int(D.points.size())) D.selected = water_select_after_edit;
    });
}

// a click on the ground while drawing: a new point (after the selected one, else at the end)
void water_add_point(float x, float y, float z) {
    if (!D.drawing) return;
    const size_t at = D.selected >= 0 && D.selected + 1 < int(D.points.size()) ? size_t(D.selected + 1) : D.points.size();
    if (D.kind == "waterfall" && D.points.size() >= 2) D.points.pop_back();
    D.points.insert(D.points.begin() + long(std::min(at, D.points.size())), {x, y, z});
    D.selected = int(std::min(at, D.points.size() - 1));
    if (D.kind == "river" && D.shallow.size() + 1 < D.points.size()) D.shallow.resize(D.points.size() - 1, false);
    if (D.kind == "pond" && !D.level_set && D.points.size() == 1) D.level = y + float(D.depth);  // shown until the check says better
    if (D.kind == "waterfall" && D.points.size() == 2) D.drawing = false;
}

// checks the draft when it changed (one at a time; the last answer is kept)
void water_check_tick() {
    if (!D.open || D.points.empty() || D.dragging) return;
    Value q = water_params();
    if (!D.editing.empty()) q["id"] = D.editing;  // the body being changed: its own models and place don't count
    const std::string key = mods::json::dump(q);
    if (key == D.checked || D.checking) return;
    D.checked = key;
    D.checking = true;
    command("check_water", q, [key](const Value& r) {
        D.checking = false;
        if (D.checked != key) return;
        D.check = r.get("ok").boolean ? r.get("result") : Value();
        if (!r.get("ok").boolean) {
            Value e;
            e["message"] = r.get("error").string();
            e["severity"] = "error";
            D.check["problems"].type = Value::Array;
            D.check["problems"].array.push_back(e);
            D.check["ok"] = false;
        }
        // the level the ground gives (a pond without a level of its own: its depth above the lowest ground at its shore)
        if (D.kind == "pond" && !D.level_set && D.check.get("position").type == Value::Object) D.level = num(D.check.get("position"), "y", D.level);
    });
}

void water_window(const Value& m) {
    if (!D.open) return;
    water_check_tick();
    ImGui::SetNextWindowSize(ImVec2(380, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Water", &D.open)) {
        ImGui::End();
        return;
    }
    if (!D.editing.empty()) ImGui::TextColored(kHead, "Editing %s (%s)", D.editing.c_str(), kind_label(D.kind));
    else {
        int kind = D.kind == "river" ? 1 : D.kind == "waterfall" ? 2 : 0;
        const int before = kind;
        ImGui::RadioButton("Pond", &kind, 0);
        ImGui::SameLine();
        ImGui::RadioButton("River", &kind, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Waterfall", &kind, 2);
        if (kind != before) water_start(kind == 0 ? "pond" : kind == 1 ? "river" : "waterfall");
    }
    // drawing: clicks into the picture add points
    if (D.drawing) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.70f, 1));
        if (ImGui::Button("Drawing: click the ground (stop)")) D.drawing = false;
        ImGui::PopStyleColor();
    } else if (ImGui::Button(D.points.empty() ? "Draw: click the ground" : "Add points")) D.drawing = true;
    muted(D.kind == "pond"    ? "Click the shore's points in order round the pond. Drag a point's handle to move it; the blue handle in the middle sets the water's level."
          : D.kind == "river" ? "Click the river's middle from where it starts to where it flows (the order is the flow). It follows the ground and steps down where the ground drops."
                              : "Click the two ends of the edge the water falls over (or make it at a river's step: select the river).");
    // the points
    for (size_t i = 0; i < D.points.size(); i++) {
        char label[96];
        std::snprintf(label, sizeof label, "%zu   %.0f, %.0f, %.0f##water_point%zu", i + 1, D.points[i][0], D.points[i][1], D.points[i][2], i);
        if (ImGui::Selectable(label, D.selected == int(i))) D.selected = D.selected == int(i) ? -1 : int(i);
    }
    if (D.selected >= 0 && D.selected < int(D.points.size())) {
        if (ImGui::SmallButton("Remove point")) {
            D.points.erase(D.points.begin() + D.selected);
            if (D.kind == "river" && size_t(D.selected) < D.shallow.size()) D.shallow.erase(D.shallow.begin() + D.selected);
            D.selected = -1;
        }
        ImGui::SameLine();
    }
    if (!D.points.empty() && ImGui::SmallButton("Clear")) D.points.clear(), D.selected = -1, D.check = Value(), D.checked.clear();
    if (D.kind == "river" && D.points.size() >= 2) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Reverse flow")) std::reverse(D.points.begin(), D.points.end()), std::reverse(D.shallow.begin(), D.shallow.end());
    }
    // what the check says
    bool errors = D.points.empty();
    if (D.checking) ImGui::TextColored(kMuted, "checking...");
    for (const auto& pr : D.check.get("problems").array) {
        const bool error = pr.get("severity").string() != "warning";
        errors |= error;
        ImGui::PushStyleColor(ImGuiCol_Text, error ? kBad : kWarn);
        ImGui::TextWrapped("%s %s", error ? "Error:" : "Note:", pr.get("message").string().c_str());
        ImGui::PopStyleColor();
    }
    if (D.check.get("ok").type == Value::Bool && !D.check.get("ok").boolean) errors = true;
    if (D.check.get("surface_triangles").type == Value::Number) {
        ImGui::TextColored(D.check.get("ok").boolean ? kGood : kMuted, "%s: %d surface triangles, collision %d triangles, room %d", kind_label(D.kind),
                           int(num(D.check, "surface_triangles")), int(num(D.check, "collision_triangles")), int(num(D.check, "room")));
        ImGui::TextColored(kMuted, "Models: %.0f KiB in the package (two models of the mod's eight)", num(D.check, "file_bytes") / 1024);
    }
    ImGui::Separator();
    // the parameters
    auto slider = [](const char* label, int& v, int lo, int hi, const char* help) {
        ImGui::SetNextItemWidth(160);
        ImGui::SliderInt(label, &v, lo, hi);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", help);
    };
    if (D.kind == "pond") {
        ImGui::SetNextItemWidth(160);
        if (ImGui::DragFloat("Level", &D.level, 2, -2000, 20000, "%.0f")) D.level_set = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The water's height (or drag the blue handle in the picture)");
        if (!D.level_set) {
            ImGui::SameLine();
            ImGui::TextDisabled("(depth above the shore)");
        }
        slider("Depth", D.depth, 20, 480, "The floor below the water");
        slider("Banks above the water", D.rim, 0, 300, "How high the rim rises above the water before it slopes down to the ground");
        ImGui::Checkbox("Lava", &D.lava);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A lava pool instead of water: the game's lava (Link burns in it), rock floor and banks");
        if (!D.lava) ImGui::Checkbox("Foam at the shore", &D.foam);
        ImGui::ColorEdit3("Floor colour", D.floor, ImGuiColorEditFlags_NoInputs);
    } else if (D.kind == "river") {
        slider("Width", D.width, 100, 2000, "The river's width");
        slider("Depth", D.depth, 20, 480, "Water above the bed in its deep sections (where Link swims)");
        for (size_t i = 0; i + 1 < D.points.size(); i++) {
            if (D.shallow.size() < i + 1) D.shallow.resize(i + 1, false);
            bool s = D.shallow[i];
            char label[64];
            std::snprintf(label, sizeof label, "Section %zu to %zu: shallow (wading)", i + 1, i + 2);
            if (ImGui::Checkbox(label, &s)) D.shallow[i] = s;
        }
        ImGui::ColorEdit3("Bed colour", D.floor, ImGuiColorEditFlags_NoInputs);
        // its steps: where a waterfall goes (for a river in the package)
        if (!D.editing.empty())
            for (size_t i = 0; i < D.check.get("steps").array.size(); i++) {
                const auto& s = D.check.get("steps").array[i];
                ImGui::Text("Step %zu: %.0f down to %.0f", i + 1, num(s, "top"), num(s, "bottom"));
                ImGui::SameLine();
                char label[48];
                std::snprintf(label, sizeof label, "Waterfall here##step%zu", i);
                if (ImGui::SmallButton(label)) {
                    Value q;
                    q["river"] = D.editing;
                    q["step"] = double(i + 1);
                    command("add_waterfall", q, [](const Value& r) {
                        if (r.get("ok").boolean) selected() = r.get("result").get("id").string();
                    });
                }
            }
    } else {
        ImGui::SetNextItemWidth(160);
        if (ImGui::DragInt("Top", &D.top, 2, -2000, 20000)) D.top_set = true;
        ImGui::SetNextItemWidth(160);
        if (ImGui::DragInt("Ground below", &D.bottom, 2, -2000, 20000)) D.bottom_set = true;
        const char* sides[] = {"Falls to the lower side", "Falls to side 1", "Falls to side 2"};
        int side = D.side == 0 ? 0 : D.side == 1 ? 1 : 2;
        ImGui::SetNextItemWidth(200);
        if (ImGui::Combo("##side", &side, sides, 3)) D.side = side == 0 ? 0 : side == 1 ? 1 : -1;
        ImGui::ColorEdit3("Rock colour", D.floor, ImGuiColorEditFlags_NoInputs);
    }
    ImGui::Separator();
    // make it, or change it
    ImGui::BeginDisabled(errors || D.checking || D.points.empty());
    if (ImGui::Button(D.editing.empty() ? "Create" : "Apply")) {
        Value p = water_params();
        p.object.erase("kind");
        std::string name = D.editing.empty() ? (D.kind == "pond" ? "add_pond" : D.kind == "river" ? "add_river" : "add_waterfall") : "edit_water";
        if (!D.editing.empty()) p["id"] = D.editing;
        command(name, p, [](const Value& r) {
            if (!r.get("ok").boolean) return;
            selected() = r.get("result").get("id").string();
            const std::string kind = D.kind;
            D = WaterDraft();
            D.open = true;
            D.kind = kind;
        });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        const std::string kind = D.kind;
        D = WaterDraft();
        D.open = true;
        D.kind = kind;
    }
    (void)m;
    ImGui::End();
}

void water_section(const Value& m, const Value& a) {
    if (!water_actor(m, a)) return;
    ImGui::Separator();
    ImGui::TextColored(kHead, "Water");
    const std::string material = a.get("shoreline").get("material").string();
    muted(material == "river" ? "A river made with the water tool." : material == "waterfall" ? "A waterfall made with the water tool." : material == "lava" ? "A lava pool made with the water tool." : "A pond made with the water tool.");
    if (ImGui::SmallButton("Edit shape...")) water_edit(a.get("id").string());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Its points, level and settings in the Water window; Apply builds it again (one undo step)");
    ImGui::SameLine();
    ImGui::TextDisabled("(moving or copying it works like any object)");
}

// test aid: WWHD_TEST_WATER=<id>[#<point>] opens the Water window on that body of water once it is in the open mod
// (crops of the outline, the height handle and a river's band; the point selected), or kind:<json> a new draft (with
// "after": "<id>" once that actor is in the open mod: drawn on a world object of its own)
void water_test_tick() {
    static bool done = false;
    if (done) return;
    const char* e = std::getenv("WWHD_TEST_WATER");
    if (!e) return done = true, void();
    const std::string s = e;
    const auto colon = s.find(':');
    if (colon == std::string::npos) {  // <id> or <id>#<point>: that body of water, that point selected
        const std::string id = s.substr(0, s.find('#'));
        if (!actor_by_id(snapshot().manifest, id)) return;
        done = true;
        water_edit(id);
        if (s.find('#') != std::string::npos) water_select_after_edit = std::atoi(s.c_str() + s.find('#') + 1) - 1;
        return;
    }
    try {
        const Value p = mods::json::parse(s.substr(colon + 1));
        if (p.get("after").type == Value::String && !actor_by_id(snapshot().manifest, p.get("after").text)) return;
        done = true;
        water_start(s.substr(0, colon));
        D.drawing = false;
        for (const auto& q : p.get("points").array) D.points.push_back({num(q, "x"), num(q, "y"), num(q, "z")});
        if (p.get("level").type == Value::Number) D.level = num(p, "level"), D.level_set = true;
        if (p.get("width").type == Value::Number) D.width = int(num(p, "width"));
        if (p.get("depth").type == Value::Number) D.depth = int(num(p, "depth"));
        for (const auto& b : p.get("shallow").array) D.shallow.push_back(b.boolean);
        D.foam = p.get("foam").boolean;
        D.lava = p.get("lava").boolean;
        if (p.get("selected").type == Value::Number) D.selected = int(num(p, "selected"));
    } catch (...) {
        done = true;
    }
}
}  // namespace editor_ui
