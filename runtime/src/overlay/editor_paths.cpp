// The Inspector's Path section (editor_panel.h path_section) for an actor that uses one of the mod's
// room paths (a wall cannon's firing area), and what the picture shows of the mod's paths
// (path_picture). Every change is a path command (editor/commands_paths.cpp): one undo step, applied
// to the game at once.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "editor_panel.h"
#include "imgui.h"

namespace editor_ui {
namespace {
const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kHead(0.95f, 0.85f, 0.55f, 1), kWarn(0.95f, 0.75f, 0.35f, 1);

struct PathUi {
    std::string actor;  // the actor the section is about
    std::string path;   // the path whose points are shown (the actor's)
    int point = 0;      // the selected point (1-based; 0 none)
    std::string checked;  // the route the warning check is for
    Value check;          // check_path's result
    bool checking = false;
} P;

void muted(const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}
float num(const Value& v, const char* k, float fallback = 0) { return v.get(k).type == Value::Number ? float(v.get(k).number) : fallback; }
const Value* path_by_id(const Value& m, const std::string& id) {
    for (const auto& v : m.get("paths").array)
        if (v.get("id").string() == id) return &v;
    return nullptr;
}
}  // namespace

void path_section(const Value& m, const Value& a) {
    const std::string use = editor::room_path_use(a);
    if (use.empty()) return;
    const std::string id = a.get("id").string(), stage = a.get("stage").string();
    const double room = a.get("room").number;
    if (P.actor != id) P = PathUi(), P.actor = id;
    ImGui::Separator();
    ImGui::TextColored(kHead, "Path");
    muted(use);
    const std::string current = a.get("path").string();
    P.path = current;
    // the mod's paths in its room
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("##path_choice", current.empty() ? "(none)" : current.c_str())) {
        for (const auto& v : m.get("paths").array) {
            if (v.get("stage").string() != stage || v.get("room").number != room) continue;
            const std::string pid = v.get("id").string();
            const std::string label = pid + (v.get("closed").boolean ? "  (area)" : "  (line)");
            if (ImGui::Selectable(label.c_str(), pid == current) && pid != current) {
                Value q;
                q["id"] = id;
                q["property"] = "path";
                q["value"] = pid;
                command("set_actor", q);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("New area in front")) {
        // a square of 1600 units whose near edge is about 400 units in front of the actor
        const double yaw = num(a.get("rotation"), "y") * 3.14159265358979323846 / 180, ahead = 1200;
        Value q;
        q["at"] = "position";
        Value pos;
        pos["x"] = num(a.get("position"), "x") + std::sin(yaw) * ahead;
        pos["y"] = num(a.get("position"), "y");
        pos["z"] = num(a.get("position"), "z") + std::cos(yaw) * ahead;
        q["position"] = pos;
        q["stage"] = stage;
        q["room"] = room;
        q["size"] = 1600.0;
        q["snap"] = false;
        q["id"] = id + "-area";
        command("add_path", q, [id](const Value& r) {
            if (!r.get("ok").boolean) return;
            Value s;
            s["id"] = id;
            s["property"] = "path";
            s["value"] = r.get("result").get("id");
            command("set_actor", s);
        });
    }
    const Value* path = current.empty() ? nullptr : path_by_id(m, current);
    if (!path) {
        if (!current.empty()) muted("The mod has no path \"" + current + "\".");
        return;
    }
    bool closed = path->get("closed").boolean;
    if (ImGui::Checkbox("Area (closed)", &closed)) {
        Value q;
        q["id"] = current;
        q["closed"] = closed;
        command("set_path", q);
    }
    const auto& pts = path->get("points").array;
    muted("Click a point to select it, then drag its handle in the picture.");
    for (size_t i = 0; i < pts.size(); i++) {
        const int n = int(i + 1);
        char label[96];
        std::snprintf(label, sizeof label, "%d   %.0f, %.0f, %.0f%s##path_point%d", n, num(pts[i], "x"), num(pts[i], "y"), num(pts[i], "z"), pts[i].get("release").boolean ? "  (release)" : "", n);
        if (ImGui::Selectable(label, P.point == n)) P.point = P.point == n ? 0 : n;
    }
    if (P.point > int(pts.size())) P.point = 0;
    // a Kargaroc's rider: the selected point may be its release point
    const std::string kind = a.get("actor").string(), variant = a.get("parameters").get("variant").string();
    if (P.point > 0 && (variant == "moblin_rider" || variant == "bokoblin_rider") && kind == "kargaroc") {
        bool release = pts[size_t(P.point - 1)].get("release").boolean;
        if (ImGui::Checkbox("Lets its rider go here", &release)) {
            Value q;
            q["id"] = current;
            q["point"] = P.point;
            q["release"] = release;
            command("set_path_point", q);
        }
    }
    if (P.point > 0 && ImGui::SmallButton("Remove point")) {
        Value q;
        q["id"] = current;
        q["point"] = P.point;
        command("remove_path_point", q);
        P.point = 0;
    }
    auto add = [&](const char* at) {
        Value q;
        q["id"] = current;
        q["at"] = at;
        if (P.point > 0) q["after"] = P.point;
        command("add_path_point", q, [](const Value& r) {
            if (r.get("ok").boolean) P.point = int(num(r.get("result"), "point"));
        });
    };
    if (ImGui::SmallButton("Add point at Link")) add("link");
    ImGui::SameLine();
    if (ImGui::SmallButton("Add point at camera")) add("camera");
    if (P.point > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(after point %d)", P.point);
    }
    // a warship's or a Floormaster's route: the warning check (over land, off the floor), again when it changes
    const std::string type = a.get("actor").string();
    if (type == "warship" || type == "floormaster") {
        const std::string key = mods::json::dump(*path) + type;
        if (key != P.checked && !P.checking) {
            P.checked = key;
            P.checking = true;
            Value q;
            q["id"] = current;
            q["for"] = type;
            command("check_path", q, [id](const Value& r) {
                if (P.actor != id) return;
                P.checking = false;
                P.check = r.get("ok").boolean ? r.get("result") : Value();
            });
        }
        for (const auto& sg : P.check.get("segments").array)
            if (!sg.get("ok").boolean) {
                ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
                ImGui::TextWrapped("Point %.0f to %.0f: %s near %.0f, %.0f (a warning: it is kept)", num(sg, "from"), num(sg, "to"), sg.get("problem").string().c_str(),
                                   num(sg.get("at"), "x"), num(sg.get("at"), "z"));
                ImGui::PopStyleColor();
            }
    }
}

bool path_picture(std::string& path, int& point) {
    if (P.actor.empty() || P.actor != selected() || P.path.empty()) return false;
    path = P.path;
    point = P.point;
    return true;
}
void path_select_point(int point) { P.point = point; }

}  // namespace editor_ui
