// The Inspector's Route section (editor_panel.h route_section) and what the picture shows of it
// (route_picture): one section for the mod's own skinned actors and for placed residents. Every change
// is a route command (editor/commands_route.cpp): one undo step, applied to the game at once.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "editor_panel.h"
#include "../mods/story_resident_types.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace editor_ui {
namespace {
const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kGood(0.45f, 0.82f, 0.52f, 1), kBad(0.95f, 0.55f, 0.45f, 1), kHead(0.95f, 0.85f, 0.55f, 1);

struct RouteUi {
    std::string actor;    // the actor the rest is about
    int point = 0;        // the selected point (1-based; 0 none)
    std::string checked;  // the route the ground check is for
    Value check;          // check_route's result
    bool checking = false;
    std::string status;   // route_status, refreshed every second
    double status_at = -10;
    std::string clips_for;  // the model clips are for
    Value clips;
} R;

double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void muted(const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}
float num(const Value& v, const char* k, float fallback = 0) { return v.get(k).type == Value::Number ? float(v.get(k).number) : fallback; }
Value with_id(const std::string& id) {
    Value p;
    p["id"] = id;
    return p;
}
// a number field that sends once an edit is done
bool number_field(const char* label, double current, double& out, float width) {
    static std::string active_label;
    static float buffer = 0;
    const bool active = ImGui::GetActiveID() == ImGui::GetID(label);
    if (!active || active_label != label) buffer = float(current);
    if (active) active_label = label;
    ImGui::SetNextItemWidth(width);
    ImGui::InputFloat(label, &buffer, 0, 0, "%.0f");
    out = buffer;
    return ImGui::IsItemDeactivatedAfterEdit();
}
bool clip_combo(const char* label, const std::string& current, const char* fallback, std::string& chosen) {
    bool picked = false;
    const std::string shown = current.empty() ? std::string(fallback) + " (default)" : current;
    ImGui::SetNextItemWidth(160);
    if (ImGui::BeginCombo(label, shown.c_str())) {
        if (ImGui::Selectable((std::string(fallback) + " (default)").c_str(), current.empty())) chosen.clear(), picked = !current.empty();
        for (const auto& c : R.clips.get("clips").array)
            if (ImGui::Selectable(c.string().c_str(), c.string() == current) && c.string() != current) chosen = c.string(), picked = true;
        ImGui::EndCombo();
    }
    return picked;
}
}  // namespace

void route_section(const Value& m, const Value& a) {
    const std::string id = a.get("id").string();
    const bool patrol = (a.get("actor").string() == "bokoblin" || a.get("actor").string() == "moblin") && a.get("kind").type == Value::Null;
    const bool resident = a.get("actor").string() == "resident" || patrol, model = a.get("kind").string() == "mod";
    if (!resident && !model) return;
    if (R.actor != id) R = RouteUi(), R.actor = id;
    ImGui::Separator();
    ImGui::TextColored(kHead, "Route");
    const std::string refusal = editor::route_refusal(a);
    if (!refusal.empty()) {
        ImGui::BeginDisabled();
        bool off = false;
        ImGui::Checkbox("Walks a route", &off);
        ImGui::EndDisabled();
        muted("This villager can't walk a route: " + refusal + ".");
        return;
    }
    if (model) {
        const std::string md = a.get("model").string();
        if (R.clips_for != md) {
            R.clips_for = md;
            R.clips = Value();
            Value p;
            p["model"] = md;
            command("model_clips", p, [md](const Value& r) {
                if (R.clips_for == md) R.clips = r.get("result");
            });
        }
        if (R.clips.type == Value::Object && !R.clips.get("skinned").boolean) {
            muted("Only a skinned model (one with bones and walk and idle clips) walks a route.");
            return;
        }
    }
    const Value& path = a.get("path");
    const bool has = path.type == Value::Object;
    const auto& pts = path.get("points").array;
    Value p = with_id(id);

    // how it walks and looks
    if (patrol) {
        muted("Patrols its route with the game's own patrol: it walks from point to point, turns at the ends (back and forth) "
              "or goes round (loop), and fights Link when it sees him.");
    } else if (resident) {
        const auto& who = a.get("parameters").get("resident");
        const auto* type = who.type == Value::String ? story_resident_types::find(who.text) : nullptr;
        const auto* lent = type ? story_resident_types::borrowed_walk(type->subtype) : nullptr;
        if (lent)
            muted(std::string("Has no walk of its own: walks with ") + lent->lender + "'s walk, fitted to its own body by the port "
                  "from your game files, and stands with its own idle. Like the game's own walkers it stops and looks at Link "
                  "when he is close in front of it and talks as usual.");
        else
            muted("Walks with its own walk and stands with its own idle. Like the game's own walkers it stops and looks at Link "
                  "when he is close in front of it, turns on the spot at corners and talks as usual.");
        ImGui::BeginDisabled();
        bool on = true;
        ImGui::Checkbox("Look at Link", &on);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(always on: the game's own)");
    } else {
        std::string chosen;
        ImGui::TextUnformatted("Walk clip");
        ImGui::SameLine(110);
        if (clip_combo("##route_walk", path.get("walk").string(), "walk", chosen) && has) {
            Value q = p;
            q["walk"] = chosen.empty() ? Value() : Value(chosen);
            command("set_route", q);
        }
        ImGui::TextUnformatted("Idle clip");
        ImGui::SameLine(110);
        if (clip_combo("##route_idle", path.get("idle").string(), "idle", chosen) && has) {
            Value q = p;
            q["idle"] = chosen.empty() ? Value() : Value(chosen);
            command("set_route", q);
        }
        bool look = a.get("look_at").type == Value::Object;
        if (ImGui::Checkbox("Look at Link", &look)) {
            Value q = p;
            q["property"] = "look_at";
            if (look) {
                Value v;
                v["head"] = "head";
                v["yaw_degrees"] = 70.0;
                v["pitch_degrees"] = 35.0;
                v["rate_degrees"] = 240.0;
                v["radius"] = 600.0;
                q["value"] = v;
            } else
                q["value"] = Value();
            command("set_actor", q);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(turns its head bone \"head\")");
    }

    // the points
    if (resident) muted(has ? "Start: its position (move the actor to move it)" : "It starts at its position; add the points it walks to.");
    if (!pts.empty()) muted("Click a point to select it, then drag its handle in the picture.");
    for (size_t i = 0; i < pts.size(); i++) {
        const int n = int(i + 1);
        char label[96];
        std::snprintf(label, sizeof label, "%d   %.0f, %.0f, %.0f##route_point%d", n, num(pts[i], "x"), num(pts[i], "y"), num(pts[i], "z"), n);
        if (ImGui::Selectable(label, R.point == n)) R.point = R.point == n ? 0 : n;
    }
    if (R.point > 0 && R.point <= int(pts.size())) {  // the selected point: its order and removal
        const int n = R.point;
        ImGui::BeginDisabled(n == 1);
        if (ImGui::SmallButton("Up")) {
            Value q = p;
            q["point"] = n;
            q["to"] = n - 1;
            command("reorder_route_point", q);
            R.point = n - 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(n == int(pts.size()));
        if (ImGui::SmallButton("Down")) {
            Value q = p;
            q["point"] = n;
            q["to"] = n + 1;
            command("reorder_route_point", q);
            R.point = n + 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove point")) {
            Value q = p;
            q["point"] = n;
            command("remove_route_point", q);
            R.point = 0;
        }
    }
    if (R.point > int(pts.size())) R.point = 0;
    auto add = [&](const char* at) {
        Value q = p;
        q["at"] = at;
        if (R.point > 0) q["after"] = R.point;
        command("add_route_point", q, [](const Value& r) {
            if (r.get("ok").boolean) R.point = int(num(r.get("result"), "point"));
        });
    };
    if (ImGui::SmallButton("Add point at Link")) add("link");
    ImGui::SameLine();
    if (ImGui::SmallButton("Add point at camera")) add("camera");
    if (R.point > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(after point %d)", R.point);
    }
    if (!has) return;

    // speed, mode, pauses
    const double pace = resident && !patrol ? editor::resident_pace(a) : 0;
    if (patrol) {
        ImGui::TextDisabled("It patrols at its own pace; when it sees Link it fights, then goes back to its route.");
    } else if (resident) {
        bool own = path.get("speed").type != Value::Number;
        char label[64];
        std::snprintf(label, sizeof label, "Own pace (%.0f units/s)", pace);
        if (ImGui::Checkbox(label, &own)) {
            Value q = p;
            q["speed"] = own ? Value() : Value(pace);
            command("set_route", q);
        }
        if (!own) {
            double v = 0;
            ImGui::TextUnformatted("Speed");
            ImGui::SameLine(110);
            if (number_field("##route_speed", num(path, "speed", float(pace)), v, 100)) {
                Value q = p;
                q["speed"] = v;
                command("set_route", q);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("units/s (%.0f to %.0f)", pace * 0.5, pace * 2);
        }
    } else {
        double v = 0;
        ImGui::TextUnformatted("Speed");
        ImGui::SameLine(110);
        if (number_field("##route_speed", num(path, "speed", 60), v, 100)) {
            Value q = p;
            q["speed"] = v;
            command("set_route", q);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("units/s");
    }
    static const char* kModes[] = {"once", "loop", "back_and_forth"};
    static const char* kModeLabels[] = {"Once (stops at the end)", "Loop (back to the start)", "Back and forth"};
    const std::string mode = path.get("mode").string("once");
    int current = 0;
    for (int i = 0; i < 3; i++)
        if (mode == kModes[i]) current = i;
    ImGui::TextUnformatted("Mode");
    ImGui::SameLine(110);
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("##route_mode", kModeLabels[current])) {
        for (int i = patrol ? 1 : 0; i < 3; i++)
            if (ImGui::Selectable(kModeLabels[i], i == current) && i != current) {
                Value q = p;
                q["mode"] = kModes[i];
                command("set_route", q);
            }
        ImGui::EndCombo();
    }
    if (!patrol) {
        bool pauses = path.get("pauses").boolean;
        if (ImGui::Checkbox("Strolling pauses", &pauses)) {
            Value q = p;
            q["pauses"] = pauses;
            command("set_route", q);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(now and then it stands for a moment)");
    }

    // the ground under it (checked again when the route or the place changes)
    const std::string key = mods::json::dump(path) + mods::json::dump(a.get("position")) + a.get("stage").string();
    if (key != R.checked && !R.checking) {
        R.checked = key;
        R.checking = true;
        command("check_route", p, [id](const Value& r) {
            if (R.actor != id) return;
            R.checking = false;
            R.check = r.get("ok").boolean ? r.get("result") : Value();
        });
    }
    bool all_good = true;
    for (const auto& s : R.check.get("segments").array)
        if (!s.get("ok").boolean) {
            all_good = false;
            const int from = int(num(s, "from")), to = int(num(s, "to"));
            ImGui::PushStyleColor(ImGuiCol_Text, kBad);
            ImGui::TextWrapped("%s to %s: %s near %.0f, %.0f, %.0f (drawn red)", from ? ("point " + std::to_string(from)).c_str() : "the start",
                               to ? ("point " + std::to_string(to)).c_str() : "the start", s.get("problem").string().c_str(), num(s.get("at"), "x"),
                               num(s.get("at"), "y"), num(s.get("at"), "z"));
            ImGui::PopStyleColor();
        }
    if (all_good && R.check.get("segments").type == Value::Array) ImGui::TextColored(kGood, "Ground: every segment has walkable ground.");

    // a walking resident in the game: walking, skipped, stopped, finished
    if (resident && !patrol) {
        if (now() - R.status_at > 1.0) {
            R.status_at = now();
            command("route_status", p, [id](const Value& r) {
                if (R.actor == id) R.status = r.get("ok").boolean ? r.get("result").get("status").string() : std::string();
            });
        }
        if (!R.status.empty()) {
            const bool bad = R.status.find("stopped") != std::string::npos || R.status.find("skipped") != std::string::npos;
            ImGui::TextColored(bad ? kBad : kMuted, "In the game: %s", R.status.c_str());
        }
    }
    if (ImGui::SmallButton("Remove route")) command("remove_route", p);
    (void)m;
}

bool route_picture(std::string& actor, int& point, std::vector<std::pair<int, int>>& bad) {
    bad.clear();
    if (R.actor.empty() || R.actor != selected()) return false;
    actor = R.actor;
    point = R.point;
    for (const auto& s : R.check.get("segments").array)
        if (!s.get("ok").boolean) bad.push_back({int(num(s, "from")), int(num(s, "to"))});
    return true;
}
void route_select_point(int point) { R.point = point; }

}  // namespace editor_ui
