// The world editor's scene editor (editor_panel.h, docs/mod-editor.md "Scenes"): the open mod's scenes,
// a scene's step list (branches indented), the timeline (steps and camera keys on a time axis: scrub to
// see the scene's camera in the picture, drag a step's end or a key to retime), the step inspector
// (camera keys from the fly camera, choices, walking paths), the trigger and the native version, and
// previews in the game. Everything it changes goes through commands (editor/commands.h): one undo step
// each, and tools can do the same. Render thread.
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../editor/editor_game.h"
#include "../editor/scenes.h"
#include "../gfx/renderer.h"
#include "editor_panel.h"
#include "editor_shell.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "text_entry.h"

namespace editor_ui {
namespace {
const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kGood(0.45f, 0.82f, 0.52f, 1), kBad(0.95f, 0.55f, 0.45f, 1), kHead(0.95f, 0.85f, 0.55f, 1);
const char* const kTypes[] = {"camera", "camera_path", "camera_follow", "say", "choice", "move", "face", "walk",
                              "animation", "wait", "fade", "sound", "music", "particle", "end"};

struct SceneUi {
    std::string scene;
    int step = 0;       // the selected step (1 = first; 0 none)
    int range_end = 0;  // Shift+click: the last step of a range (copy, preview)
    int key = 0;        // the selected camera key of the step (1 = first)
    int point = 0;      // the selected point of a walk step (1 = first): its handle shows in the picture
    float px_per_frame = 0.5f;
    bool fit = true;
    double scrub = -1;  // the timeline's cursor (frames; < 0 none)
    bool follow = true;  // the fly camera shows the scene's camera while scrubbing
    double sent = -2;    // the last time sent to the game
    int drag_step = 0, drag_key = 0;  // retiming by dragging a bar's end or a key
    float drag_frames = 0;
    bool scrubbing = false;
    std::string json, json_for;
    Value native;  // native_scene's answer for the current scene
    std::string native_for;
    std::string want_scene;  // show_scene / test aid
    int want_step = 0;
    double want_scrub = -1;
    bool focused = false;
    bool ask_remove = false;
    char new_id[64] = {};
    int trigger_type = 0;
    char chart[64] = {};
    float radius = 500;
    bool open_settings = false;  // test aid: the scene's settings open (pictures)
} C;

void muted(const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}
float num(const Value& v, const char* k, float fallback = 0) { return v.get(k).type == Value::Number ? float(v.get(k).number) : fallback; }
const Value* by_id(const Value& list, const std::string& id) {
    for (const auto& v : list.array)
        if (v.get("id").string() == id) return &v;
    return nullptr;
}
Value query(const std::string& command, const Value& params) {
    Value out;
    editor::try_with_session([&](editor::Session& s) { out = s.run(command, params, false); });
    return out;
}
Value prm() {
    Value p;
    p["scene"] = C.scene;
    return p;
}
Value prm_step(int step) {
    Value p = prm();
    p["step"] = step;
    return p;
}
bool edit_line(const char* label, const std::string& current, std::string& out, size_t max = 240) {
    static std::map<ImGuiID, std::vector<char>> buffers;
    auto& b = buffers[ImGui::GetID(label)];
    b.resize(max + 1);
    if (ImGui::GetActiveID() != ImGui::GetID(label)) std::snprintf(b.data(), b.size(), "%s", current.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    text_entry::field(label, b.data(), b.size());
    out = b.data();
    return ImGui::IsItemDeactivatedAfterEdit();
}
void edit_fields(int step, const Value& fields) {
    Value p = prm_step(step);
    p["fields"] = fields;
    command("edit_scene_step", p);
}
void edit_field(int step, const char* field, const Value& value) {
    Value f;
    f[field] = value;
    edit_fields(step, f);
}
std::string lines_of(const Value& v) {
    if (v.type == Value::String) return v.text;
    std::string s;
    for (const auto& l : v.array) s += (s.empty() ? "" : " / ") + l.string();
    return s;
}
Value lines_value(const std::string& text) {
    Value v;
    v.type = Value::Array;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find(" / ", at);
        if (end == std::string::npos) end = text.size();
        v.array.push_back(Value(text.substr(at, end - at)));
        at = end + 3;
    }
    return v;
}
std::string summary(const Value& s) {
    const std::string t = s.get("type").string();
    char b[160];
    if (t == "say" || t == "choice") return s.get("actor").string(t == "say" ? "link" : "") + (t == "say" ? ": " : "") + lines_of(s.get("text"));
    if (t == "camera_path") return std::to_string(s.get("points").array.size()) + " keys" + (s.get("easing").string() == "smoothstep" ? ", smooth" : "");
    if (t == "camera") return s.get("pan").boolean ? "pan" : s.get("target_actor").type == Value::String ? "at " + s.get("target_actor").text : "fixed view";
    if (t == "move" || t == "face" || t == "walk" || t == "animation" || t == "camera_follow") return s.get("actor").string() + (t == "animation" ? " " + s.get("clip").string() : "");
    if (t == "fade") return s.get("direction").string();
    if (t == "sound") return s.get("name").string();
    if (t == "music") return s.get("action").string() + " " + s.get("cue").string();
    if (t == "particle") return s.get("effect").string();
    if (t == "end") {
        std::snprintf(b, sizeof b, "%zu action(s)", s.get("then").array.size());
        return b;
    }
    return "";
}
ImU32 type_colour(const std::string& t, int alpha = 255) {
    if (t == "camera" || t == "camera_path" || t == "camera_follow") return IM_COL32(90, 160, 235, alpha);
    if (t == "say" || t == "choice") return IM_COL32(235, 195, 90, alpha);
    if (t == "move" || t == "face" || t == "walk" || t == "animation") return IM_COL32(120, 210, 130, alpha);
    if (t == "end") return IM_COL32(220, 110, 110, alpha);
    return IM_COL32(170, 170, 185, alpha);
}
std::string seconds(double frames) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2fs", frames / 60.0);
    return b;
}
// the fly camera's view (eye, target) from the editor's state
bool camera_view(editor::WorldPoint& eye, editor::WorldPoint& target) {
    const Value& fly = snapshot().state.get("fly");
    if (!fly.get("on").boolean) return false;
    eye = {num(fly.get("position"), "x"), num(fly.get("position"), "y"), num(fly.get("position"), "z")};
    target = {num(fly.get("target"), "x"), num(fly.get("target"), "y"), num(fly.get("target"), "z")};
    return true;
}
void scrub_to(double t) {
    C.scrub = std::max(0.0, t);
    if (!C.follow || std::fabs(C.sent - C.scrub) < 1 || busy()) return;
    C.sent = C.scrub;
    Value p = prm();
    p["time"] = std::round(C.scrub);
    command("scrub_scene", p);
}
void preview(int from, int to) {
    Value p = prm();
    if (from) p["from"] = from;
    if (to) p["to"] = to;
    command("preview_scene", p);
}

// ---------------------------------------------------------------- the step list
void step_list(const Value& sc, const Value& timeline) {
    const auto& steps = sc.get("steps").array;
    const auto& rows = timeline.get("steps").array;
    for (size_t i = 0; i < steps.size(); i++) {
        const Value& s = steps[i];
        const int n = int(i + 1);
        const bool off_path = i < rows.size() && !rows[i].get("default_path").boolean;
        const int lo = std::min(C.step, C.range_end ? C.range_end : C.step), hi = std::max(C.step, C.range_end ? C.range_end : C.step);
        const bool selected = n == C.step || (C.range_end && n >= lo && n <= hi);
        ImGui::PushID(n);
        if (off_path) ImGui::Indent(14);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(type_colour(s.get("type").string())));
        char label[64];
        std::snprintf(label, sizeof label, "%2d  %s", n, s.get("type").string().c_str());
        if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 0))) {
            if (ImGui::GetIO().KeyShift && C.step) C.range_end = n;
            else C.step = n, C.range_end = 0, C.key = 0, C.point = 0;
            if (i < rows.size() && rows[i].get("start").number >= 0 && ImGui::IsMouseDoubleClicked(0)) scrub_to(rows[i].get("start").number);
        }
        ImGui::PopStyleColor();
        ImGui::SameLine(120);
        std::string text = summary(s);
        if (s.get("id").type == Value::String) text = "[" + s.get("id").text + "] " + text;
        if (i < rows.size() && rows[i].get("branch").type == Value::String) text = "(branch " + rows[i].get("branch").text + ") " + text;
        ImGui::TextColored(kMuted, "%s", text.c_str());
        if (off_path) ImGui::Unindent(14);
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::Button("Add step")) ImGui::OpenPopup("add_step");
    if (ImGui::BeginPopup("add_step")) {
        for (const char* t : kTypes)
            if (ImGui::MenuItem(t)) {
                Value p = prm();
                p["type"] = t;
                if (C.step && C.step < int(steps.size())) p["at"] = C.step + 1;  // after the selected step (before the end)
                command("add_scene_step", p, [](const Value& r) {
                    if (r.get("ok").boolean) C.step = int(r.get("result").get("step").number), C.range_end = 0;
                });
            }
        ImGui::Separator();
        ImGui::TextDisabled("a camera takes the fly camera's view");
        ImGui::EndPopup();
    }
    if (!C.step) return;
    ImGui::SameLine();
    if (ImGui::ArrowButton("##up", ImGuiDir_Up) && C.step > 1) {
        Value p = prm_step(C.step);
        p["to"] = C.step - 1;
        command("move_scene_step", p, [](const Value& r) {
            if (r.get("ok").boolean) C.step = int(r.get("result").get("step").number);
        });
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##down", ImGuiDir_Down) && C.step < int(steps.size())) {
        Value p = prm_step(C.step);
        p["to"] = C.step + 1;
        command("move_scene_step", p, [](const Value& r) {
            if (r.get("ok").boolean) C.step = int(r.get("result").get("step").number);
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) command("duplicate_scene_step", prm_step(C.step), [](const Value& r) {
        if (r.get("ok").boolean) C.step = int(r.get("result").get("step").number);
    });
    ImGui::SameLine();
    if (ImGui::Button("Remove")) command("remove_scene_step", prm_step(C.step), [](const Value& r) {
        if (r.get("ok").boolean) C.step = std::max(1, C.step - 1), C.range_end = 0;
    });
}

// ---------------------------------------------------------------- the timeline
void timeline_view(const Value& sc, const Value& timeline) {
    const auto& rows = timeline.get("steps").array;
    const double total = std::max(60.0, std::max(timeline.get("duration").number, timeline.get("longest").number));
    ImGui::TextColored(kHead, "%s on the default path", seconds(timeline.get("duration").number).c_str());
    ImGui::SameLine();
    if (timeline.get("longest").number > timeline.get("duration").number) ImGui::TextColored(kMuted, "(longest %s)", seconds(timeline.get("longest").number).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit")) C.fit = true;
    ImGui::SameLine();
    ImGui::Checkbox("Camera follows the cursor", &C.follow);
    if (C.scrub >= 0) ImGui::TextColored(kHead, "cursor %s (frame %d)", seconds(C.scrub).c_str(), int(std::lround(C.scrub)));
    else ImGui::TextColored(kMuted, "click the ruler to put the cursor");
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float avail = std::max(200.0f, ImGui::GetContentRegionAvail().x);
    if (C.fit) C.px_per_frame = float((avail - 20) / total), C.fit = false;
    const float row_h = std::max(14.0f, ImGui::GetTextLineHeight() + 2), ruler_h = ImGui::GetTextLineHeight() + 6;
    const float width = float(total * C.px_per_frame) + 20, height = ruler_h + row_h * float(std::max<size_t>(1, rows.size())) + 6;
    ImGui::BeginChild("##timeline_canvas", ImVec2(0, std::max(80.0f, ImGui::GetContentRegionAvail().y)), false, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("##timeline", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();
    if (hovered && io.KeyCtrl && io.MouseWheel != 0) C.px_per_frame = std::clamp(C.px_per_frame * std::pow(1.2f, io.MouseWheel), 0.02f, 8.0f);
    auto x_of = [&](double f) { return o.x + 10 + float(f) * C.px_per_frame; };
    auto f_of = [&](float x) { return double(x - o.x - 10) / C.px_per_frame; };
    // the ruler: a tick a second, labels every few
    dl->AddRectFilled(o, ImVec2(o.x + width, o.y + ruler_h), IM_COL32(30, 34, 42, 230));
    const int every = C.px_per_frame * 60 < 30 ? 5 : 1;
    for (int s = 0; s * 60 <= total + 60; s++) {
        const float x = x_of(s * 60.0);
        dl->AddLine(ImVec2(x, o.y + (s % every ? 14 : 8)), ImVec2(x, o.y + ruler_h), IM_COL32(140, 150, 165, 200));
        if (s % every == 0) dl->AddText(ImVec2(x + 2, o.y + 1), IM_COL32(170, 180, 195, 255), (std::to_string(s) + "s").c_str());
    }
    // the bars
    struct Hit {
        int step = 0, key = 0;
        bool end = false;
    } hit;
    for (size_t i = 0; i < rows.size(); i++) {
        const Value& r = rows[i];
        const int n = int(i + 1);
        const float y = o.y + ruler_h + row_h * float(i);
        if (n == C.step) dl->AddRectFilled(ImVec2(o.x, y), ImVec2(o.x + width, y + row_h), IM_COL32(80, 70, 30, 90));
        const double start = r.get("start").number;
        if (start < 0) continue;
        double frames = r.get("frames").number;
        if (C.drag_step == n && !C.drag_key) frames = C.drag_frames;
        const std::string type = r.get("type").string();
        const bool path = r.get("default_path").boolean;
        const float x0 = x_of(start), x1 = std::max(x_of(start + frames), x0 + 3);
        dl->AddRectFilled(ImVec2(x0, y + 3), ImVec2(x1, y + row_h - 3), type_colour(type, path ? 220 : 110), 3);
        if (!r.get("timed").boolean) dl->AddCircleFilled(ImVec2(x0, y + row_h / 2), 4, type_colour(type));
        const std::string label = std::to_string(n) + " " + type;
        dl->AddText(ImVec2(x0 + 4, y + 2), IM_COL32(15, 18, 22, 255), label.c_str());
        // the keys of a camera path (a key's diamond dragged: the time from the key before it)
        const auto& kt = r.get("keys").array;
        for (size_t k = 0; type == "camera_path" && k < kt.size(); k++) {
            double t = kt[k].number;
            if (C.drag_step == n && C.drag_key && int(k) == C.drag_key) t = kt[k - 1].number + C.drag_frames;
            const ImVec2 c(x_of(t), y + row_h / 2);
            const bool sel = n == C.step && int(k + 1) == C.key;
            dl->AddQuadFilled(ImVec2(c.x, c.y - 6), ImVec2(c.x + 6, c.y), ImVec2(c.x, c.y + 6), ImVec2(c.x - 6, c.y), sel ? IM_COL32(255, 230, 120, 255) : IM_COL32(230, 240, 255, 255));
            if (hovered && std::fabs(io.MousePos.x - c.x) < 6 && std::fabs(io.MousePos.y - c.y) < 7) hit = {n, int(k + 1), false};
        }
        // the end of a timed bar: drag to retime
        if (r.get("timed").boolean && type != "camera_path" && hovered && std::fabs(io.MousePos.x - x1) < 5 && io.MousePos.y >= y && io.MousePos.y < y + row_h)
            hit = {n, 0, true};
        else if (!hit.step && hovered && io.MousePos.y >= y && io.MousePos.y < y + row_h && io.MousePos.x >= x0 - 3 && io.MousePos.x <= x1 + 3)
            hit = {n, 0, false};
    }
    if (hit.end || hit.key > 1) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    // clicks: the ruler scrubs; a bar selects; its end or a key retimes
    if (ImGui::IsItemActivated()) {
        if (io.MousePos.y < o.y + ruler_h || !hit.step) C.scrubbing = true;
        else {
            C.step = hit.step, C.range_end = 0, C.key = hit.key, C.point = 0;
            const Value& r = rows[size_t(hit.step - 1)];
            if (hit.end) C.drag_step = hit.step, C.drag_key = 0, C.drag_frames = float(r.get("frames").number);
            else if (hit.key > 1) {
                const auto& keys = r.get("keys").array;
                C.drag_step = hit.step, C.drag_key = hit.key - 1, C.drag_frames = float(keys[size_t(hit.key - 1)].number - keys[size_t(hit.key - 2)].number);
            } else if (hit.key)
                scrub_to(r.get("keys").array[size_t(hit.key - 1)].number);
        }
    }
    if (active && C.scrubbing) scrub_to(std::clamp(f_of(io.MousePos.x), 0.0, total));
    if (active && C.drag_step) C.drag_frames = std::max(1.0f, C.drag_frames + io.MouseDelta.x / C.px_per_frame);
    if (ImGui::IsItemDeactivated()) {
        if (C.drag_step) {
            Value p = prm_step(C.drag_step);
            p["frames"] = int(std::lround(C.drag_frames));
            if (C.drag_key) p["key"] = C.drag_key;
            command("retime_scene_step", p);
        }
        C.drag_step = C.drag_key = 0;
        C.scrubbing = false;
    }
    // the cursor
    if (C.scrub >= 0) {
        const float x = x_of(C.scrub);
        dl->AddLine(ImVec2(x, o.y), ImVec2(x, o.y + height), IM_COL32(255, 220, 90, 255), 2);
        dl->AddTriangleFilled(ImVec2(x - 6, o.y), ImVec2(x + 6, o.y), ImVec2(x, o.y + 8), IM_COL32(255, 220, 90, 255));
    }
    if (C.drag_step) {
        char b[64];
        std::snprintf(b, sizeof b, "%d frames (%s)", int(std::lround(C.drag_frames)), seconds(C.drag_frames).c_str());
        ImGui::SetTooltip("%s", b);
    }
    ImGui::EndChild();
    (void)origin;
}

// ---------------------------------------------------------------- the inspector
void actor_combo(const char* label, int step, const Value& m, const std::string& current, bool with_link) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(label, current.empty() ? "(none)" : current.c_str())) {
        if (with_link && ImGui::Selectable("link", current == "link")) edit_field(step, "actor", Value("link"));
        for (const auto& a : m.get("actors").array) {
            const std::string id = a.get("id").string();
            if (ImGui::Selectable(id.c_str(), id == current)) edit_field(step, "actor", Value(id));
        }
        ImGui::EndCombo();
    }
}
void point_row(const char* label, int step, const char* field, const Value& value) {
    ImGui::Text("%s  %.0f %.0f %.0f", label, num(value, "x"), num(value, "y"), num(value, "z"));
    ImGui::SameLine();
    if (ImGui::SmallButton((std::string("where the camera looks##") + field).c_str())) {
        editor::WorldPoint eye, target;
        if (!camera_view(eye, target)) return;
        Value p, o, d;
        o["x"] = eye.x, o["y"] = eye.y, o["z"] = eye.z;
        const double dx = target.x - eye.x, dy = target.y - eye.y, dz = target.z - eye.z, len = std::max(1e-3, std::sqrt(dx * dx + dy * dy + dz * dz));
        d["x"] = dx / len, d["y"] = dy / len, d["z"] = dz / len;
        p["origin"] = o;
        p["direction"] = d;
        const std::string f = field;
        command("pick", p, [step, f](const Value& r) {
            if (r.get("ok").boolean) edit_field(step, f.c_str(), r.get("result").get("position"));
        });
    }
    ImGui::SameLine();
    if (ImGui::SmallButton((std::string("at Link##") + field).c_str())) {
        const Value& link = snapshot().state.get("link");
        if (link.get("valid").boolean) edit_field(step, field, link.get("position"));
    }
}
void camera_inspector(int step, const Value& s) {
    const std::string t = s.get("type").string();
    if (t == "camera") {
        ImGui::Text("eye %.0f %.0f %.0f", num(s.get("eye"), "x"), num(s.get("eye"), "y"), num(s.get("eye"), "z"));
        if (s.get("target").type == Value::Object) ImGui::Text("target %.0f %.0f %.0f", num(s.get("target"), "x"), num(s.get("target"), "y"), num(s.get("target"), "z"));
        else ImGui::Text("looks at %s", s.get("target_actor").string().c_str());
        if (ImGui::Button("View from the fly camera")) {
            Value p = prm_step(step);
            p["key"] = 1;
            command("set_camera_key", p);
        }
        ImGui::SameLine();
        if (ImGui::Button("Add a key (camera path)")) command("add_camera_key", prm_step(step));
        bool pan = s.get("pan").boolean;
        if (ImGui::Checkbox("Pan to the fly camera's view", &pan)) {
            Value f;
            if (pan) {
                editor::WorldPoint eye, target;
                if (camera_view(eye, target)) {
                    f["pan"] = true;
                    f["eye_to"] = editor::point_value(eye);
                    f["target_to"] = editor::point_value(target);
                    edit_fields(step, f);
                }
            } else {
                f["pan"] = Value();
                f["eye_to"] = Value();
                f["target_to"] = Value();
                edit_fields(step, f);
            }
        }
        return;
    }
    // a camera path: its keys
    const char* const easings[] = {"linear", "smoothstep"};
    int easing = s.get("easing").string() == "smoothstep" ? 1 : 0;
    ImGui::SetNextItemWidth(140);
    if (ImGui::Combo("easing", &easing, easings, 2)) edit_field(step, "easing", Value(easings[easing]));
    const auto& points = s.get("points").array;
    for (size_t k = 0; k < points.size(); k++) {
        ImGui::PushID(int(k));
        const Value& key = points[k];
        const bool sel = int(k + 1) == C.key;
        char label[96];
        std::snprintf(label, sizeof label, "key %zu  eye %.0f %.0f %.0f", k + 1, num(key.get("eye"), "x"), num(key.get("eye"), "y"), num(key.get("eye"), "z"));
        if (ImGui::Selectable(label, sel, 0, ImVec2(230, 0))) C.key = int(k + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("from camera")) {
            Value p = prm_step(step);
            p["key"] = int(k + 1);
            command("set_camera_key", p);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            Value p = prm_step(step);
            p["key"] = int(k + 1);
            command("remove_camera_key", p);
        }
        if (k + 1 < points.size()) {
            int frames = int(num(key, "frames"));
            ImGui::SetNextItemWidth(120);
            if (ImGui::DragInt("frames to the next", &frames, 1, 1, 18000) || ImGui::IsItemActive()) {
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                Value p = prm_step(step);
                p["key"] = int(k + 1);
                p["frames"] = std::max(1, frames);
                command("retime_scene_step", p);
            }
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Add a key from the fly camera")) {
        Value p = prm_step(step);
        if (C.key) p["at"] = C.key + 1;
        command("add_camera_key", p);
    }
}
void walk_inspector(int step, const Value& s) {
    const auto& points = s.get("points").array;
    if (s.get("quest_place").type == Value::String) {
        ImGui::Text("walks to the place of quest %s", s.get("quest_place").text.c_str());
        return;
    }
    float speed = num(s, "speed", 60);
    ImGui::SetNextItemWidth(120);
    ImGui::DragFloat("speed", &speed, 1, 1, 1500, "%.0f");
    if (ImGui::IsItemDeactivatedAfterEdit()) edit_field(step, "speed", Value(double(speed)));
    for (size_t k = 0; k < points.size(); k++) {
        ImGui::PushID(int(k));
        char label[96];
        std::snprintf(label, sizeof label, "point %zu  %.0f %.0f %.0f", k + 1, num(points[k], "x"), num(points[k], "y"), num(points[k], "z"));
        if (ImGui::Selectable(label, int(k + 1) == C.point, 0, ImVec2(230, 0))) C.point = int(k + 1) == C.point ? 0 : int(k + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            Value p = prm_step(step);
            p["point"] = int(k + 1);
            command("remove_walk_point", p);
        }
        ImGui::PopID();
    }
    muted("Select a point to move it with the handle in the picture.");
    if (ImGui::Button("Add where the camera looks")) command("add_walk_point", prm_step(step), [](const Value& r) {
        if (r.get("ok").boolean) C.point = int(r.get("result").get("point").number);
    });
    ImGui::SameLine();
    if (ImGui::Button("Add at Link")) {
        Value p = prm_step(step);
        p["from"] = "link";
        command("add_walk_point", p);
    }
}
void choice_inspector(int step, const Value& sc, const Value& s) {
    const auto& options = s.get("options").array;
    const int def = int(num(s, "default"));
    for (size_t o = 0; o < options.size() && o < 2; o++) {
        ImGui::PushID(int(o));
        ImGui::TextColored(kHead, "Option %zu", o + 1);
        ImGui::SameLine();
        if (ImGui::RadioButton("default", def == int(o))) {
            Value p = prm_step(step);
            p["default"] = int(o + 1);
            command("set_scene_choice", p);
        }
        std::string label;
        if (edit_line("##label", options[o].get("text").string(), label, 32)) {
            Value p = prm_step(step);
            p["option"] = int(o + 1);
            p["label"] = label;
            command("set_scene_choice", p);
        }
        // where it leads: a later step
        const std::string next = options[o].get("next").string();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##next", ("leads to " + next).c_str())) {
            const auto& steps = sc.get("steps").array;
            for (size_t j = size_t(step); j < steps.size(); j++) {
                const std::string text = std::to_string(j + 1) + " " + steps[j].get("type").string() + (steps[j].get("id").type == Value::String ? " [" + steps[j].get("id").text + "]" : "");
                if (ImGui::Selectable(text.c_str(), steps[j].get("id").string() == next)) {
                    Value p = prm_step(step);
                    p["option"] = int(o + 1);
                    p["next"] = int(j + 1);
                    command("set_scene_choice", p);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
}
void json_area(int step, const Value& s) {
    const std::string key = C.scene + ":" + std::to_string(step) + ":" + editor::pretty(s);
    if (C.json_for != key) C.json_for = key, C.json = editor::pretty(s);
    static std::vector<char> buf;
    buf.assign(C.json.begin(), C.json.end());
    buf.resize(C.json.size() + 4096, 0);
    if (ImGui::InputTextMultiline("##step_json", buf.data(), buf.size(), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 8))) C.json = buf.data();
    if (ImGui::Button("Apply fields")) {
        try {
            const Value now = mods::json::parse(C.json);
            Value fields;
            fields.type = Value::Object;
            for (const auto& [k, v] : s.object)
                if (!now.object.contains(k)) fields[k] = Value();
            for (const auto& [k, v] : now.object)
                if (!(s.get(k) == v)) fields[k] = v;
            edit_fields(step, fields);
        } catch (const std::exception&) {
            muted("That is not JSON.");
        }
    }
}
void step_inspector(const Value& m, const Value& sc, const Value& timeline) {
    const auto& steps = sc.get("steps").array;
    if (C.step < 1 || C.step > int(steps.size())) {
        muted("Select a step in the list or on the timeline.");
        return;
    }
    const int n = C.step;
    const Value& s = steps[size_t(n - 1)];
    const std::string t = s.get("type").string();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(type_colour(t)), "Step %d: %s", n, t.c_str());
    const auto& rows = timeline.get("steps").array;
    if (size_t(n) <= rows.size() && rows[size_t(n - 1)].get("start").number >= 0)
        ImGui::TextColored(kMuted, "starts at %s", seconds(rows[size_t(n - 1)].get("start").number).c_str());
    std::string id;
    ImGui::TextUnformatted("id");
    ImGui::SameLine(70);
    if (edit_line("##id", s.get("id").string(), id, 32)) edit_field(n, "id", id.empty() ? Value() : Value(id));
    if (t != "end" && t != "choice") {
        ImGui::TextUnformatted("next");
        ImGui::SameLine(70);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const std::string next = s.get("next").string();
        if (ImGui::BeginCombo("##next", next.empty() ? "(the following step)" : next.c_str())) {
            if (ImGui::Selectable("(the following step)", next.empty())) edit_field(n, "next", Value());
            for (size_t j = size_t(n); j < steps.size(); j++)
                if (steps[j].get("id").type == Value::String && ImGui::Selectable((std::to_string(j + 1) + " " + steps[j].get("id").text).c_str(), steps[j].get("id").text == next))
                    edit_field(n, "next", steps[j].get("id"));
            ImGui::EndCombo();
        }
    }
    if (editor::scene_step_timed(t) && t != "camera_path") {
        int frames = int(editor::scene_step_frames(s));
        ImGui::SetNextItemWidth(140);
        ImGui::DragInt("frames (60 a second)", &frames, 1, 1, 18000);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            Value p = prm_step(n);
            p["frames"] = std::max(1, frames);
            command("retime_scene_step", p);
        }
    }
    ImGui::Separator();
    if (t == "camera" || t == "camera_path") camera_inspector(n, s);
    else if (t == "say") {
        actor_combo("##say_actor", n, m, s.get("actor").string(), true);
        std::string text;
        if (edit_line("##say_text", lines_of(s.get("text")), text)) edit_field(n, "text", lines_value(text));
        muted("Lines are separated by \" / \".");
    } else if (t == "move" || t == "face") {
        actor_combo("##actor", n, m, s.get("actor").string(), false);
        if (s.get("position").type == Value::Object) point_row("to", n, "position", s.get("position"));
    } else if (t == "walk") {
        actor_combo("##walk_actor", n, m, s.get("actor").string(), false);
        walk_inspector(n, s);
    } else if (t == "choice") {
        std::string text;
        if (edit_line("##choice_text", lines_of(s.get("text")), text)) edit_field(n, "text", lines_value(text));
        choice_inspector(n, sc, s);
    } else if (t == "animation" || t == "camera_follow")
        actor_combo("##anim_actor", n, m, s.get("actor").string(), false);
    else if (t == "particle" && s.get("position").type == Value::Object)
        point_row("at", n, "position", s.get("position"));
    ImGui::Separator();
    if (ImGui::Button("Play this step")) preview(n, n);
    ImGui::SameLine();
    if (ImGui::Button("Play from here")) preview(n, int(steps.size()));
    if (ImGui::CollapsingHeader("Fields (JSON)")) json_area(n, s);
}

// ---------------------------------------------------------------- the scene: trigger and native version
void scene_settings(const Value& m, const Value& sc) {
    const Value& trigger = sc.get("trigger");
    const char* const types[] = {"none", "talk", "quest_step", "room", "area", "salvage"};
    const std::string now = trigger.get("type").string("none");
    std::string text = "starts: " + now;
    if (now == "talk") text += " to " + trigger.get("actor").string();
    if (now == "quest_step") text += " " + trigger.get("quest").string() + " step " + std::to_string(int(num(trigger, "step")));
    if (now == "room" || now == "area") text += " " + trigger.get("stage").string() + " room " + std::to_string(int(num(trigger, "room")));
    ImGui::TextColored(kHead, "%s", text.c_str());
    ImGui::SetNextItemWidth(130);
    ImGui::Combo("##trigger_type", &C.trigger_type, types, 6);
    ImGui::SameLine();
    const std::string type = types[C.trigger_type];
    Value p = prm();
    p["type"] = type;
    if (type == "talk") {
        static std::string actor;
        ImGui::SetNextItemWidth(160);
        if (ImGui::BeginCombo("##talk_actor", actor.empty() ? "actor" : actor.c_str())) {
            for (const auto& a : m.get("actors").array)
                if (ImGui::Selectable(a.get("id").string().c_str(), a.get("id").string() == actor)) actor = a.get("id").string();
            ImGui::EndCombo();
        }
        p["actor"] = actor;
    } else if (type == "quest_step") {
        static std::string quest;
        static int step = 1;
        ImGui::SetNextItemWidth(140);
        if (ImGui::BeginCombo("##trigger_quest", quest.empty() ? "quest" : quest.c_str())) {
            for (const auto& q : m.get("quests").array)
                if (ImGui::Selectable(q.get("id").string().c_str(), q.get("id").string() == quest)) quest = q.get("id").string();
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ImGui::InputInt("##trigger_step", &step);
        p["quest"] = quest;
        p["step"] = std::max(1, step);
    } else if (type == "area") {
        ImGui::SetNextItemWidth(90);
        ImGui::DragFloat("radius", &C.radius, 5, 10, 10000, "%.0f");
        p["radius"] = double(C.radius);
    } else if (type == "salvage") {
        ImGui::SetNextItemWidth(140);
        text_entry::field("##chart", C.chart, sizeof C.chart, 0, "chart id");
        p["chart"] = std::string(C.chart);
    }
    ImGui::SameLine();
    if (ImGui::Button(type == "room" || type == "area" ? "Set where Link is" : "Set")) command("set_scene_trigger", p);
    // the native version
    const std::string key = C.scene + ":" + editor::pretty(sc);
    if (C.native_for != key) {
        Value q = prm();
        C.native = query("native_scene", q).get("result");
        C.native_for = key;
    }
    ImGui::Spacing();
    if (sc.get("native").type == Value::Null) {
        ImGui::TextColored(kMuted, "Native version: none");
        ImGui::SameLine();
        if (ImGui::SmallButton("Make it from the steps")) {
            Value q = prm();
            q["from_steps"] = true;
            command("native_scene", q);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Two placed uo2 residents turn (face steps), the camera holds and moves (camera steps), waits:\nthe game's own event plays that instead of the steps");
    } else {
        const bool plays = C.native.get("eligible").boolean;
        ImGui::TextColored(plays ? kGood : kBad, "%s", plays ? "Native version: plays (the game's own event)" : ("Native version: doesn't play: " + C.native.get("why").string()).c_str());
        const Value& n = sc.get("native");
        ImGui::TextColored(kMuted, "turns %s and %s, camera holds %d and moves %d ticks, %d ticks in all", n.get("actors").array.size() > 0 ? n.get("actors").array[0].get("id").string().c_str() : "?",
                           n.get("actors").array.size() > 1 ? n.get("actors").array[1].get("id").string().c_str() : "?", int(num(n.get("camera"), "hold_ticks")),
                           int(num(n.get("camera"), "move_ticks")), int(num(n, "timekeeper_ticks")));
        if (ImGui::SmallButton("Make it again from the steps")) {
            Value q = prm();
            q["from_steps"] = true;
            command("native_scene", q);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove the native version")) {
            Value q = prm();
            q["value"] = Value();
            command("native_scene", q);
        }
        muted("An edited scene plays its steps in the preview; the native version plays once the package is saved and the game starts again.");
    }
}

}  // namespace

// ---------------------------------------------------------------- the window
void show_scene(const std::string& scene, int step) {
    C.want_scene = scene;
    C.want_step = step;
    editor_shell::show_scenes();
}
bool scenes_focused() { return C.focused; }
bool scene_picture(std::string& scene, int& step, int& point, double& scrub) {
    if (C.scene.empty()) return false;
    scene = C.scene, step = C.step, point = C.point, scrub = C.scrub;
    return true;
}
void scene_editor(const Value& m) {
    static bool test_aids = [] {
        if (std::getenv("WWHD_TEST_EDITOR_SCENE_SETTINGS")) C.open_settings = true;
        if (const char* e = std::getenv("WWHD_TEST_EDITOR_SCENE")) {  // scene[:step[:scrub frames[:point]]] (pictures)
            const std::string v = e;
            size_t a = v.find(':');
            C.want_scene = v.substr(0, a);
            if (a != std::string::npos) {
                C.want_step = std::atoi(v.c_str() + a + 1);
                size_t b = v.find(':', a + 1);
                if (b != std::string::npos) {
                    C.want_scrub = std::atof(v.c_str() + b + 1);
                    size_t c = v.find(':', b + 1);
                    if (c != std::string::npos) C.point = std::atoi(v.c_str() + c + 1);
                }
            }
        }
        return true;
    }();
    (void)test_aids;
    C.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!C.want_scene.empty() && by_id(m.get("scenes"), C.want_scene)) {
        C.scene = C.want_scene, C.step = C.want_step, C.range_end = 0, C.fit = true;
        if (C.want_scrub >= 0) C.scrub = C.want_scrub, C.want_scrub = -1;
        C.want_scene.clear();
    }
    if (C.scene.empty() || !by_id(m.get("scenes"), C.scene)) {
        C.scene = m.get("scenes").array.empty() ? std::string() : m.get("scenes").array[0].get("id").string();
        C.step = 0, C.range_end = 0, C.scrub = -1;
    }
    // the bar: the scene, new, duplicate, remove, previews
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("##scene", C.scene.empty() ? "(no scene)" : C.scene.c_str())) {
        for (const auto& s : m.get("scenes").array)
            if (ImGui::Selectable(s.get("id").string().c_str(), s.get("id").string() == C.scene)) C.scene = s.get("id").string(), C.step = 0, C.range_end = 0, C.scrub = -1, C.fit = true;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("New scene...")) ImGui::OpenPopup("new_scene");
    if (ImGui::BeginPopup("new_scene")) {
        ImGui::SetNextItemWidth(200);
        text_entry::field("##new_scene_id", C.new_id, sizeof C.new_id, 0, "id (a-z, 0-9, -)");
        if (ImGui::Button("Add")) {
            Value p;
            if (C.new_id[0]) p["id"] = std::string(C.new_id);
            command("add_scene", p, [](const Value& r) {
                if (r.get("ok").boolean) C.want_scene = r.get("result").get("scene").string(), C.want_step = 1;
            });
            C.new_id[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    const Value* sc = by_id(m.get("scenes"), C.scene);
    if (!sc) {
        muted("This mod has no scenes: New scene adds one, then add steps (a camera takes the fly camera's view).");
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) {
        Value p;
        p["id"] = C.scene;
        command("duplicate_scene", p, [](const Value& r) {
            if (r.get("ok").boolean) C.want_scene = r.get("result").get("scene").string();
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove...")) C.ask_remove = true;
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.25f, 1));
    if (ImGui::Button("Play scene")) preview(0, 0);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Test mode: the scene plays in the game; afterwards the world is as it was and the editor's view returns");
    if (C.step) {
        ImGui::SameLine();
        const int lo = C.range_end ? std::min(C.step, C.range_end) : C.step, hi = C.range_end ? std::max(C.step, C.range_end) : C.step;
        if (ImGui::Button(lo == hi ? "Play step" : ("Play steps " + std::to_string(lo) + "-" + std::to_string(hi)).c_str())) preview(lo, hi);
    }
    ImGui::SameLine();
    if (ImGui::Button("Camera key")) {
        Value p = prm();
        const std::string t = C.step ? sc->get("steps").array[size_t(C.step - 1)].get("type").string() : "";
        if (t == "camera" || t == "camera_path") p["step"] = C.step;
        command("add_camera_key", p, [](const Value& r) {
            if (r.get("ok").boolean) C.step = int(r.get("result").get("step").number);
        });
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The fly camera's view as a camera key: on the selected camera step (it becomes a path), else a new camera step");
    if (C.ask_remove) {
        ImGui::OpenPopup("remove_scene");
        C.ask_remove = false;
    }
    if (ImGui::BeginPopupModal("remove_scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Remove scene %s? (Undo brings it back.)", C.scene.c_str());
        if (ImGui::Button("Remove")) {
            Value p;
            p["id"] = C.scene;
            p["confirm"] = true;
            command("remove_scene", p);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    const Value timeline = editor::scene_timeline(*sc);
    // three columns: the steps, the timeline, the inspector
    const float h = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##scene_steps", ImVec2(250, h), true);
    step_list(*sc, timeline);
    ImGui::EndChild();
    ImGui::SameLine();
    const float right = 330;
    ImGui::BeginChild("##scene_timeline", ImVec2(std::max(200.0f, ImGui::GetContentRegionAvail().x - right - 8), h), true);
    timeline_view(*sc, timeline);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##scene_inspector", ImVec2(0, h), true);
    step_inspector(m, *sc, timeline);
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Scene: what starts it, native version", C.open_settings ? ImGuiTreeNodeFlags_DefaultOpen : 0)) scene_settings(m, *sc);
    ImGui::EndChild();
    // copy and paste of steps while the window has the keyboard (Ctrl+C / Ctrl+V)
    ImGuiIO& io = ImGui::GetIO();
    if (C.focused && !io.WantTextInput && (io.KeyCtrl || io.KeySuper)) {
        if (ImGui::IsKeyPressed(ImGuiKey_C, false) && C.step) {
            Value p = prm(), list;
            list.type = Value::Array;
            const int lo = C.range_end ? std::min(C.step, C.range_end) : C.step, hi = C.range_end ? std::max(C.step, C.range_end) : C.step;
            for (int i = lo; i <= hi; i++) list.array.push_back(Value(i));
            p["steps"] = list;
            command("copy", p);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            Value p = prm();
            if (C.step) p["before"] = C.step + 1 <= int(sc->get("steps").array.size()) ? C.step + 1 : C.step;
            command("paste", p);
        }
    }
}

}  // namespace editor_ui
