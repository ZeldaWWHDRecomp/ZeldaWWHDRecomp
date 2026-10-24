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

#include "../editor/demo_names.h"
#include "../editor/demo_tracks.h"
#include "../editor/editor_game.h"
#include "../editor/scenes.h"
#include "../gfx/renderer.h"
#include "../runtime.h"
#include "editor_panel.h"
#include "editor_shell.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "text_entry.h"

namespace editor_ui {
namespace {
const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kGood(0.45f, 0.82f, 0.52f, 1), kBad(0.95f, 0.55f, 0.45f, 1), kHead(0.95f, 0.85f, 0.55f, 1);
const char* const kTypes[] = {"camera", "camera_path", "camera_follow", "say", "choice", "move", "face", "walk",
                              "animation", "wait", "fade", "sound", "music", "particle", "demo", "end"};

struct SceneUi {
    std::string scene;
    int step = 0;       // the selected step (1 = first; 0 none)
    int range_end = 0;  // Shift+click: the last step of a range (copy, preview)
    int key = 0;        // the selected camera key of the step (1 = first)
    int point = 0;      // the selected point of a walk step (1 = first): its handle shows in the picture
    float px_per_frame = 0.5f;
    bool fit = true;
    bool zoomed = false;     // zoomed by hand: the timeline keeps its scale when the scene's length changes
    double fitted_total = 0;
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
    // a demo step's lanes: the selected key (lane and frame) or event (list, actor, index), a key's drag
    std::string d_lane, d_list, d_actor;
    int d_key = -1, d_event = -1;
    bool d_drag = false;
    double d_drag_to = 0;
    double d_start = -1, d_len = 0;  // the selected demo step on the timeline (scene frames, 60 a second)
    std::string d_new_lane = "camera.eye", d_new_list = "messages", d_new_actor;
    std::string d_actor_id, d_actor_name;
    int d_actor_kind = 0;
    // the demo pickers: what a new actor is (its name, our label, its archive; a model's archive and file),
    // whether its id was typed by hand, the hint after Add actor; the new event's picked values
    std::string d_actor_label, d_actor_archive, d_actor_query, d_model_query, d_model_file;
    bool d_id_typed = false;
    int d_kind_shown = 0;  // the kind the fields below were filled for (a change starts them afresh)
    std::string d_added;  // the actor just added: the hint shows for it
    std::string e_query, e_actor, e_to, e_joint, e_text = "...", e_archive, e_file, e_mode = "walk", e_clip;
    bool e_once = false, e_face = false;  // a clip played once (held at its end); Link's clip with his face animations
    int e_blend = -1;                     // Link's clip: blend frames (-1: the game's default)
    int e_attach_blend = 0;               // attach: frames onto the joint (0: at once)
    uint32_t e_sound = 0, e_particle = 0;
    int e_hour = 12;
    float e_rain = 1;
    std::string e_generated;  // the JSON the pickers made last (the Advanced box follows them until edited)
    std::string test_scroll;  // test aid (pictures): the inspector keeps "actors" or "event" in view
    bool test_pick = false;   // test aid: the next search list picks its first match (as a click)
    bool test_advanced = false;  // test aid: Advanced (the event as JSON) open
    // demo_choices answers by "what:archive" (asked once each; why when refused)
    std::map<std::string, Value> lists;
    std::map<std::string, std::string> list_why;
    std::set<std::string> asked;
    std::string d_fields, d_fields_for;
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
bool edit_line(const char* label, const std::string& current, std::string& out, size_t max = 240, const char* hint = nullptr) {
    static std::map<ImGuiID, std::vector<char>> buffers;
    auto& b = buffers[ImGui::GetID(label)];
    b.resize(max + 1);
    if (ImGui::GetActiveID() != ImGui::GetID(label)) std::snprintf(b.data(), b.size(), "%s", current.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    text_entry::field(label, b.data(), b.size(), 0, hint);
    out = b.data();
    return ImGui::IsItemDeactivatedAfterEdit();
}
// a short label in front of the next input (as the step's "id" and "next" rows)
void field_label(const char* text, float at = 70) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(at);
}
// the room a help_mark takes on its line (for the width of the input before it)
float help_width() { return ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x + 2; }
// a "(?)" after the last item with a tooltip
void help_mark(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
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
    if (C.d_start >= 0 && C.scrub >= C.d_start && C.scrub <= C.d_start + C.d_len) {
        // inside the selected demo step: its pose at that frame (30 a second), without playing it
        Value p = prm_step(C.step);
        p["frame"] = std::round((C.scrub - C.d_start) / 2);
        command("demo_pose", p);
        return;
    }
    Value p = prm();
    p["time"] = std::round(C.scrub);
    command("scrub_scene", p);
}
// the demo frame at the timeline's cursor (0 when it is outside the selected demo step)
unsigned demo_cursor() {
    if (C.d_start < 0 || C.scrub < C.d_start) return 0;
    return unsigned(std::lround(std::min(C.scrub - C.d_start, C.d_len) / 2));
}
// the cursor while a demo step is selected: its time and the demo's own frame (30 a second), never a
// scene frame next to it (the same label on the timeline and in the inspector)
std::string demo_cursor_label() {
    char b[96];
    if (C.scrub >= C.d_start && C.scrub <= C.d_start + C.d_len)
        std::snprintf(b, sizeof b, "cursor %s \xC2\xB7 demo frame %u", seconds(C.scrub).c_str(), demo_cursor());
    else
        std::snprintf(b, sizeof b, "cursor %s \xC2\xB7 outside this demo (new keys go to demo frame %u)", seconds(C.scrub).c_str(), demo_cursor());
    return b;
}
// a lane's track in a demo step ("camera.eye", "<actor>.position")
const Value* demo_track(const Value& st, const std::string& lane) {
    const size_t dot = lane.rfind('.');
    if (dot == std::string::npos) return nullptr;
    const std::string owner = lane.substr(0, dot), what = lane.substr(dot + 1);
    if (owner == "camera") return st.get("camera").get(what).type == Value::Object ? &st.get("camera").get(what) : nullptr;
    const Value* a = by_id(st.get("actors"), owner);
    return a && a->get(what).type == Value::Object ? &a->get(what) : nullptr;
}
const Value* demo_event(const Value& st, const std::string& list, const std::string& actor, int index) {
    const Value& l = actor.empty() ? st.get(list) : (by_id(st.get("actors"), actor) ? by_id(st.get("actors"), actor)->get(list) : st.get("-"));
    return index >= 0 && size_t(index) < l.array.size() ? &l.array[size_t(index)] : nullptr;
}
void demo_select_key(const std::string& lane, int frame) { C.d_lane = lane, C.d_key = frame, C.d_list.clear(), C.d_actor.clear(), C.d_event = -1; }
void demo_select_event(const std::string& list, const std::string& actor, int index) { C.d_list = list, C.d_actor = actor, C.d_event = index, C.d_lane.clear(), C.d_key = -1; }
void demo_remove_selected() {
    if (C.d_key >= 0 && !C.d_lane.empty()) {
        Value p = prm_step(C.step);
        p["lane"] = C.d_lane;
        p["frame"] = C.d_key;
        command("remove_demo_key", p);
        C.d_key = -1;
    } else if (C.d_event >= 0) {
        Value p = prm_step(C.step);
        p["list"] = C.d_list;
        if (!C.d_actor.empty()) p["actor"] = C.d_actor;
        p["index"] = C.d_event;
        command("remove_demo_event", p);
        C.d_event = -1;
    }
}
void preview(int from, int to) {
    Value p = prm();
    if (from) p["from"] = from;
    if (to) p["to"] = to;
    command("preview_scene", p);
}

// ---------------------------------------------------------------- the step list
// the selected step's own commands (the row's buttons, the list's right-click menu, Delete); removing is
// refused while another step leads to it (the command says so in the status bar)
void duplicate_step(int n) {
    command("duplicate_scene_step", prm_step(n), [](const Value& r) {
        if (r.get("ok").boolean) C.step = int(r.get("result").get("step").number), C.range_end = 0;
    });
}
void remove_step(int n) {
    command("remove_scene_step", prm_step(n), [](const Value& r) {
        if (r.get("ok").boolean) C.step = std::max(1, C.step - 1), C.range_end = 0;
    });
}
// A row of buttons that wraps: the next one goes on this line when it fits up to `right` (the row's
// right edge, taken where the row starts), else it starts a new line. `width` is the next item's width.
void flow(float right, float width) {
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right) ImGui::SameLine();
}
float button_width(const char* label) { return ImGui::CalcTextSize(label, nullptr, true).x + 2 * ImGui::GetStyle().FramePadding.x; }
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
        // right-click: the step's own menu (it selects the step too)
        if (ImGui::BeginPopupContextItem("step_menu")) {
            if (n != C.step) C.step = n, C.range_end = 0, C.key = 0, C.point = 0;
            if (ImGui::MenuItem("Duplicate step")) duplicate_step(n);
            if (ImGui::MenuItem("Remove step", "Delete")) remove_step(n);
            ImGui::EndPopup();
        }
        ImGui::SameLine(120);
        std::string text = summary(s);
        if (s.get("id").type == Value::String) text = "[" + s.get("id").text + "] " + text;
        if (i < rows.size() && rows[i].get("branch").type == Value::String) text = "(branch " + rows[i].get("branch").text + ") " + text;
        ImGui::TextColored(kMuted, "%s", text.c_str());
        if (off_path) ImGui::Unindent(14);
        ImGui::PopID();
    }
    ImGui::Spacing();
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;  // the row wraps here
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
    const float arrow = ImGui::GetFrameHeight();
    flow(right, arrow);
    if (ImGui::ArrowButton("##up", ImGuiDir_Up) && C.step > 1) {
        Value p = prm_step(C.step);
        p["to"] = C.step - 1;
        command("move_scene_step", p, [](const Value& r) {
            if (r.get("ok").boolean) C.step = int(r.get("result").get("step").number);
        });
    }
    flow(right, arrow);
    if (ImGui::ArrowButton("##down", ImGuiDir_Down) && C.step < int(steps.size())) {
        Value p = prm_step(C.step);
        p["to"] = C.step + 1;
        command("move_scene_step", p, [](const Value& r) {
            if (r.get("ok").boolean) C.step = int(r.get("result").get("step").number);
        });
    }
    flow(right, button_width("Duplicate step"));
    if (ImGui::Button("Duplicate step")) duplicate_step(C.step);
    flow(right, button_width("Remove step"));
    if (ImGui::Button("Remove step")) remove_step(C.step);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also Delete, or right-click a step. Refused while another step leads to it.");
}

// ---------------------------------------------------------------- the timeline
void timeline_view(const Value& m, const Value& sc, const Value& timeline) {
    const auto& rows = timeline.get("steps").array;
    // the selected demo step: its lanes and events under the step bars, on its own frames (30 a second)
    const auto& steps = sc.get("steps").array;
    const Value* demo = nullptr;
    Value lanes;
    C.d_start = -1, C.d_len = 0;
    if (C.step >= 1 && size_t(C.step) <= steps.size() && size_t(C.step) <= rows.size() && steps[size_t(C.step - 1)].get("type").string() == "demo" &&
        rows[size_t(C.step - 1)].get("start").number >= 0) {
        demo = &steps[size_t(C.step - 1)];
        lanes = editor::demo_lanes(m, *demo);
        C.d_start = rows[size_t(C.step - 1)].get("start").number, C.d_len = 2 * lanes.get("frames").number;
    }
    struct DemoRow {
        std::string label, lane, list, actor;
        const Value* v = nullptr;
    };
    std::vector<DemoRow> drows;
    if (demo) {
        for (const auto& l : lanes.get("lanes").array) drows.push_back({l.get("lane").string() + " (" + l.get("curve").string("linear") + ")", l.get("lane").string(), "", "", &l});
        for (const auto& e : lanes.get("events").array)
            drows.push_back({(e.get("actor").type == Value::String ? e.get("actor").text + "." : std::string()) + e.get("list").string(), "", e.get("list").string(), e.get("actor").string(), &e});
    }
    const double total = std::max(60.0, std::max(timeline.get("duration").number, timeline.get("longest").number));
    ImGui::TextColored(kHead, "%s on the default path", seconds(timeline.get("duration").number).c_str());
    ImGui::SameLine();
    if (timeline.get("longest").number > timeline.get("duration").number) ImGui::TextColored(kMuted, "(longest %s)", seconds(timeline.get("longest").number).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit")) C.fit = true, C.zoomed = false;
    ImGui::SameLine();
    ImGui::Checkbox("Camera follows the cursor", &C.follow);
    ImGui::SameLine();
    if (C.scrub >= 0 && demo) ImGui::TextColored(kHead, "%s", demo_cursor_label().c_str());
    else if (C.scrub >= 0) ImGui::TextColored(kHead, "cursor %s (frame %d)", seconds(C.scrub).c_str(), int(std::lround(C.scrub)));
    else ImGui::TextColored(kMuted, "click the ruler to put the cursor");
    if (demo) {
        if (lanes.get("problem").type == Value::String) ImGui::TextColored(kBad, "The game would refuse this demo: %s", lanes.get("problem").text.c_str());
        for (const auto& w : lanes.get("warnings").array) ImGui::TextColored(kHead, "Not in the picture: %s", w.string().c_str());
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float avail = std::max(200.0f, ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize);
    // a left margin for the demo lanes' names
    float gutter = 10;
    for (const auto& r : drows) gutter = std::max(gutter, ImGui::CalcTextSize(r.label.c_str()).x + 14);
    if (!C.zoomed && C.fitted_total != total) C.fit = true;
    if (C.fit) C.px_per_frame = float((avail - gutter - 10) / total), C.fit = false, C.fitted_total = total;
    const float row_h = std::max(14.0f, ImGui::GetTextLineHeight() + 2), ruler_h = ImGui::GetTextLineHeight() + 6;
    const float steps_h = ruler_h + row_h * float(std::max<size_t>(1, rows.size()));
    const float width = float(total * C.px_per_frame) + gutter + 10, height = steps_h + (demo ? row_h * float(drows.size() + 1) : 0.0f) + 6;
    ImGui::BeginChild("##timeline_canvas", ImVec2(0, std::max(80.0f, ImGui::GetContentRegionAvail().y)), false, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("##timeline", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();
    if (hovered && io.KeyCtrl && io.MouseWheel != 0) C.px_per_frame = std::clamp(C.px_per_frame * std::pow(1.2f, io.MouseWheel), 0.02f, 8.0f), C.zoomed = true;
    auto x_of = [&](double f) { return o.x + gutter + float(f) * C.px_per_frame; };
    auto f_of = [&](float x) { return double(x - o.x - gutter) / C.px_per_frame; };
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
    // the demo's lanes: keys as diamonds; events as blocks (clips, modes), points (messages, sounds,
    // particles), markers (attach, detach) and keys (time, weather)
    struct DemoHit {
        int row = -1, key = -1, event = -1;
    } dhit;
    if (demo) {
        auto dx = [&](double f) { return x_of(C.d_start + 2 * f); };
        const float y0 = o.y + steps_h;
        const float label_x = o.x + 4;
        dl->AddRectFilled(ImVec2(o.x, y0), ImVec2(o.x + width, y0 + row_h), IM_COL32(60, 45, 80, 160));
        dl->AddRectFilled(ImVec2(dx(0), y0 + 2), ImVec2(dx(lanes.get("frames").number), y0 + row_h - 2), IM_COL32(150, 110, 210, 120), 2);
        dl->AddText(ImVec2(label_x, y0 + 1), IM_COL32(220, 205, 245, 255), "demo step");
        for (int f = 0; f <= int(lanes.get("frames").number); f += 30) dl->AddLine(ImVec2(dx(f), y0 + row_h - 5), ImVec2(dx(f), y0 + row_h), IM_COL32(200, 180, 240, 200));
        for (size_t i = 0; i < drows.size(); i++) {
            const DemoRow& r = drows[i];
            const float y = y0 + row_h * float(i + 1), cy = y + row_h / 2;
            dl->AddRectFilled(ImVec2(o.x, y), ImVec2(o.x + width, y + row_h), i % 2 ? IM_COL32(40, 36, 52, 120) : IM_COL32(30, 28, 40, 120));
            const bool row_hover = hovered && io.MousePos.y >= y && io.MousePos.y < y + row_h;
            if (!r.lane.empty()) {
                const auto& keys = r.v->get("keys").array;
                const ImU32 c = r.lane.rfind("camera.", 0) == 0 ? IM_COL32(120, 180, 245, 255) : IM_COL32(130, 220, 140, 255);
                for (size_t k = 0; k < keys.size(); k++) {
                    double f = keys[k].number;
                    const bool sel = C.d_lane == r.lane && C.d_key == int(keys[k].number);
                    if (sel && C.d_drag) f = C.d_drag_to;
                    const ImVec2 p(dx(f), cy);
                    if (k) dl->AddLine(ImVec2(dx(keys[k - 1].number), cy), p, (c & 0x00FFFFFFu) | 0x60000000u, 1.5f);
                    dl->AddQuadFilled(ImVec2(p.x, p.y - 5), ImVec2(p.x + 5, p.y), ImVec2(p.x, p.y + 5), ImVec2(p.x - 5, p.y), sel ? IM_COL32(255, 230, 120, 255) : c);
                    if (row_hover && std::fabs(io.MousePos.x - p.x) < 6) dhit = {int(i), int(keys[k].number), -1};
                }
            } else {
                const auto& items = r.v->get("items").array;
                const bool blocks = r.list == "clips" || r.list == "modes";
                for (size_t k = 0; k < items.size(); k++) {
                    const Value& it = items[k];
                    const double f = it.get("frame").number;
                    const bool sel = C.d_list == r.list && C.d_actor == r.actor && C.d_event == int(it.get("index").number);
                    const ImU32 c = sel ? IM_COL32(255, 230, 120, 255)
                                    : r.list == "messages"                        ? IM_COL32(235, 195, 90, 255)
                                    : r.list == "attach" || r.list == "detach"    ? IM_COL32(235, 130, 200, 255)
                                    : r.list == "time" || r.list == "weather"     ? IM_COL32(110, 210, 220, 255)
                                    : blocks                                      ? IM_COL32(130, 220, 140, 255)
                                                                                  : IM_COL32(190, 190, 200, 255);
                    const float x = dx(f);
                    float x_end = x + 6;
                    if (blocks) {
                        const double next = k + 1 < items.size() ? items[k + 1].get("frame").number : lanes.get("frames").number;
                        x_end = std::max(dx(next) - 1, x + 4);
                        dl->AddRectFilled(ImVec2(x, y + 3), ImVec2(x_end, y + row_h - 3), (c & 0x00FFFFFFu) | 0xB0000000u, 3);
                    } else if (r.list == "attach" || r.list == "detach") {
                        const float d = r.list == "attach" ? 1.0f : -1.0f;
                        dl->AddTriangleFilled(ImVec2(x - 5 * d, y + 3), ImVec2(x - 5 * d, y + row_h - 3), ImVec2(x + 5 * d, cy), c);
                    } else if (r.list == "time" || r.list == "weather") {
                        dl->AddQuadFilled(ImVec2(x, cy - 5), ImVec2(x + 5, cy), ImVec2(x, cy + 5), ImVec2(x - 5, cy), c);
                    } else {
                        dl->AddCircleFilled(ImVec2(x, cy), 4.5f, c);
                    }
                    dl->AddText(ImVec2((blocks ? x : x + 7) + 3, y + 1), blocks ? IM_COL32(15, 18, 22, 255) : c, it.get("label").string().c_str());
                    if (row_hover && io.MousePos.x >= x - 6 && io.MousePos.x <= std::max(x_end, x + 6)) dhit = {int(i), -1, int(it.get("index").number)};
                }
            }
            dl->AddText(ImVec2(label_x, y + 1), IM_COL32(150, 150, 170, 200), r.label.c_str());
        }
        if (dhit.row >= 0) hit = {};
    }
    if (hit.end || hit.key > 1) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (dhit.key >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    // clicks: the ruler scrubs; a bar selects; its end or a key retimes
    if (ImGui::IsItemActivated() && dhit.row >= 0) {
        const DemoRow& r = drows[size_t(dhit.row)];
        if (dhit.key >= 0) {
            demo_select_key(r.lane, dhit.key);
            C.d_drag = true, C.d_drag_to = dhit.key;
            scrub_to(C.d_start + 2.0 * dhit.key);
        } else {
            demo_select_event(r.list, r.actor, dhit.event);
            for (const auto& it : r.v->get("items").array)
                if (int(it.get("index").number) == dhit.event) scrub_to(C.d_start + 2 * it.get("frame").number);
        }
    } else if (ImGui::IsItemActivated()) {
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
    if (active && C.d_drag) C.d_drag_to = std::clamp(C.d_drag_to + io.MouseDelta.x / (2 * C.px_per_frame), 0.0, C.d_len / 2);
    if (ImGui::IsItemDeactivated() && C.d_drag) {
        C.d_drag = false;
        const int to = int(std::lround(C.d_drag_to));
        if (to != C.d_key) {
            Value p = prm_step(C.step);
            p["lane"] = C.d_lane;
            p["frame"] = C.d_key;
            p["to"] = to;
            const int from = C.d_key;
            C.d_key = to;
            command("move_demo_key", p, [from](const Value& r) {
                if (!r.get("ok").boolean) C.d_key = from;
            });
        }
    }
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
    if (C.d_drag) ImGui::SetTooltip("frame %d (%s)", int(std::lround(C.d_drag_to)), seconds(2 * C.d_drag_to).c_str());
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
        field_label("text");
        if (edit_line("##label", options[o].get("text").string(), label, 32, "the answer's words")) {
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
// a demo step: its actors, the selected key or event, keys and events added at the cursor
const char* const kDemoCurves[] = {"linear", "smooth", "bspline", "step"};
const char* const kDemoLists[] = {"messages", "weather", "time", "sounds", "particles", "attach", "detach", "clips", "modes", "data"};
const char* const kActorKinds[] = {"game actor", "Link", "model (archive/file)", "mod character"};
// a template for a new event of a list (its frame comes from the cursor)
std::string demo_event_template(const std::string& list, const Value& st) {
    const std::string first = st.get("actors").array.empty() ? "bird" : st.get("actors").array[0].get("id").string();
    if (list == "messages") return R"({"text": "..."})";
    if (list == "weather") return R"({"rain": 1})";
    if (list == "time") return R"({"hour": 12})";
    if (list == "sounds") return R"({"id": 0, "actor": ")" + first + R"("})";
    if (list == "particles") return R"({"id": 0, "actor": ")" + first + R"("})";
    if (list == "attach") return R"({"actor": "", "to": ")" + first + R"(", "joint": ""})";
    if (list == "detach") return R"({"actor": ""})";
    if (list == "clips") return R"({"archive": "", "file": ".bck"})";
    if (list == "modes") return R"({"mode": "walk"})";
    return R"({"data": ""})";
}
bool ends_with(const std::string& s, const char* tail) {
    const size_t k = std::strlen(tail);
    return s.size() >= k && s.compare(s.size() - k, k, tail) == 0;
}
// a demo_choices answer ("what", for files and joints of an archive), asked once; empty until it comes
const Value& choices(const std::string& what, const std::string& archive = "") {
    static const Value empty;
    const std::string key = what + ":" + archive;
    auto it = C.lists.find(key);
    if (it != C.lists.end()) return it->second;
    if (C.asked.insert(key).second) {
        Value p;
        p["what"] = what;
        if (!archive.empty()) p["archive"] = archive;
        command("demo_choices", p, [key](const Value& r) {
            if (r.get("ok").boolean) C.lists[key] = r.get("result").get("items");
            else C.lists[key] = Value(), C.list_why[key] = r.get("error").string();
        });
    }
    return empty;
}
// under a picker: still loading, or why the game gave no list
void list_state(const std::string& what, const std::string& archive = "") {
    const std::string key = what + ":" + archive;
    if (!C.lists.count(key)) muted("loading the list...");
    else if (C.list_why.count(key)) muted(C.list_why[key]);
}
// A search box and a list of `labels` (at most `rows` high, only those matching the search). The row whose
// value (value_of) is `selected` is highlighted. The index of the clicked row, or -1.
template <class ValueOf>
int pick_list(const char* id, std::string& query, const std::vector<std::string>& labels, const std::string& selected, ValueOf value_of, float rows, const char* hint) {
    ImGui::PushID(id);
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s", query.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (text_entry::field("##search", buf, sizeof buf, 0, hint)) query = buf;
    query = buf;
    int picked = -1;
    if (ImGui::BeginListBox("##list", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * rows + 4))) {
        int shown = 0;
        for (size_t i = 0; i < labels.size() && shown < 400; i++) {
            if (!query.empty() && !editor::matches_query(labels[i], query)) continue;
            shown++;
            const bool on = !selected.empty() && value_of(i) == selected;
            if (ImGui::Selectable((labels[i] + "##" + std::to_string(i)).c_str(), on)) picked = int(i);
            if (on && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
        }
        if (!shown) ImGui::TextDisabled(labels.empty() ? "(none)" : "(nothing matches)");
        if (C.test_pick && picked < 0)
            for (size_t i = 0; i < labels.size(); i++)
                if (query.empty() || editor::matches_query(labels[i], query)) {
                    picked = int(i), C.test_pick = false;
                    break;
                }
        ImGui::EndListBox();
    }
    ImGui::PopID();
    return picked;
}
// the object archive a demo actor of the step uses (its model's, Link's, a game actor's known one or its
// own name), "" for a mod character
std::string actor_archive(const Value& st, const std::string& id) {
    for (const auto& a : st.get("actors").array) {
        if (a.get("id").string() != id) continue;
        if (a.get("model").type == Value::Object) return a.get("model").get("archive").string();
        if (a.get("character").type == Value::String) return {};
        const std::string name = a.get("actor").string();
        if (name == "Link") return "Link";
        for (const auto& c : choices("actors").array)
            if (c.get("actor").string() == name && !c.get("archive").string().empty()) return c.get("archive").string();
        return name;
    }
    return {};
}
// an actor of the step for an event field: a dropdown of its actors (with `none` as the first entry when given)
void demo_actor_pick(const char* id, const Value& st, std::string& value, const char* none) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(id, value.empty() ? (none ? none : "(an actor of the demo)") : value.c_str())) {
        if (none && ImGui::Selectable(none, value.empty())) value.clear();
        for (const auto& a : st.get("actors").array) {
            const std::string aid = a.get("id").string();
            if (ImGui::Selectable(aid.c_str(), aid == value)) value = aid;
        }
        ImGui::EndCombo();
    }
}
// a joint of an archive's models (`none`: the entry for no joint)
void joint_pick(const char* id, const std::string& archive, std::string& value, const char* none) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    const Value& joints = archive.empty() ? Value() : choices("joints", archive);
    if (ImGui::BeginCombo(id, value.empty() ? none : value.c_str())) {
        if (ImGui::Selectable(none, value.empty())) value.clear();
        for (const auto& j : joints.array)
            if (ImGui::Selectable(j.string().c_str(), j.string() == value)) value = j.string();
        ImGui::EndCombo();
    }
    if (!archive.empty()) list_state("joints", archive);
}
void demo_inspector(int n, const Value& m, const Value& st) {
    const Value lanes = editor::demo_lanes(m, st);
    if (lanes.get("problem").type == Value::String) {
        ImGui::PushStyleColor(ImGuiCol_Text, kBad);
        ImGui::TextWrapped("The game would refuse it: %s", lanes.get("problem").text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::TextColored(kMuted, "origin: %s", st.get("origin").type == Value::String ? "at Link" : st.get("origin").type == Value::Object ? "a fixed point" : "the world");
    const unsigned at = demo_cursor();
    if (C.scrub >= 0) ImGui::TextColored(kHead, "%s", demo_cursor_label().c_str());
    else ImGui::TextColored(kMuted, "no cursor: keys and events go to demo frame 0");
    muted("Its lanes are under the steps on the timeline (30 frames a second): click a key or an event to select it, drag a key to another frame, Delete removes it.");
    // actors
    if (ImGui::CollapsingHeader("Actors", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& a : lanes.get("actors").array) {
            ImGui::PushID(a.get("id").string().c_str());
            ImGui::Text("%s: %s %s", a.get("id").string().c_str(), a.get("kind").string().c_str(), a.get("name").string().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                Value p = prm_step(n);
                p["id"] = a.get("id");
                command("remove_demo_actor", p);
            }
            ImGui::PopID();
        }
        if (!C.d_added.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, kGood);
            ImGui::TextWrapped("%s added. To move it: put the cursor on a time, select its position lane (%s.position), press Add key.", C.d_added.c_str(), C.d_added.c_str());
            ImGui::PopStyleColor();
        }
        if (C.test_scroll == "actors") ImGui::SetScrollHereY(C.d_added.empty() ? 0.0f : 0.25f);
        ImGui::TextColored(kHead, "Add an actor");
        std::vector<std::string> taken;
        for (const auto& a : st.get("actors").array) taken.push_back(a.get("id").string());
        // what it is
        field_label("What it is", 110);
        ImGui::SetNextItemWidth(-help_width());
        ImGui::Combo("##demo_actor_kind", &C.d_actor_kind, kActorKinds, IM_ARRAYSIZE(kActorKinds));
        help_mark("game actor: one of the game's characters or objects, by name (it is created for the demo when the room has none).\n"
                  "Link: Link himself.\n"
                  "model: a model from one of the game's object archives, without the actor's own behaviour.\n"
                  "mod character: one of this mod's placed actors.");
        if (C.d_actor_kind != C.d_kind_shown) {
            C.d_kind_shown = C.d_actor_kind;
            C.d_actor_name.clear(), C.d_actor_label.clear(), C.d_actor_archive.clear(), C.d_model_file.clear();
            if (!C.d_id_typed) C.d_actor_id.clear();
        }
        if (C.d_actor_kind == 0) {
            const Value& all = choices("actors");
            std::vector<std::string> labels;
            std::vector<const Value*> rows;
            for (const auto& a : all.array) {
                const std::string label = a.get("label").string() == a.get("actor").string() ? a.get("actor").string() : a.get("label").string() + " (" + a.get("actor").string() + ")";
                labels.push_back(label + "  [" + a.get("group").string() + "]");
                rows.push_back(&a);
            }
            const int picked = pick_list("##demo_actor_list", C.d_actor_query, labels, C.d_actor_name, [&](size_t i) { return rows[i]->get("actor").string(); }, 6, "search: e.g. helmaroc, pig, seagull");
            if (picked >= 0) {
                const Value& a = *rows[size_t(picked)];
                C.d_actor_name = a.get("actor").string(), C.d_actor_label = a.get("label").string(), C.d_actor_archive = a.get("archive").string();
                if (!C.d_id_typed) C.d_actor_id = editor::suggest_demo_id(a.get("id").string().empty() ? a.get("label").string() : a.get("id").string(), taken);
            }
            list_state("actors");
        } else if (C.d_actor_kind == 1) {
            muted("Link needs no name.");
            if (!C.d_id_typed && (C.d_actor_id.empty() || C.d_actor_id.rfind("link", 0) != 0)) C.d_actor_id = editor::suggest_demo_id("link", taken);
        } else if (C.d_actor_kind == 2) {
            // the archive, then one of its models
            const Value& archives = choices("archives");
            std::vector<std::string> names;
            for (const auto& a : archives.array) names.push_back(a.string());
            const int picked = pick_list("##demo_model_archive", C.d_model_query, names, C.d_actor_archive, [&](size_t i) { return names[i]; }, 5, "search the game's archives: e.g. Kamome");
            if (picked >= 0) C.d_actor_archive = names[size_t(picked)], C.d_model_file.clear();
            list_state("archives");
            if (!C.d_actor_archive.empty()) {
                const Value& files = choices("files", C.d_actor_archive);
                std::vector<std::string> models;
                for (const auto& f : files.array)
                    if (ends_with(f.string(), ".bdl") || ends_with(f.string(), ".bmd")) models.push_back(f.string());
                field_label("Model", 110);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##demo_model_file", C.d_model_file.empty() ? "(a model of the archive)" : C.d_model_file.c_str())) {
                    for (const auto& f : models)
                        if (ImGui::Selectable(f.c_str(), f == C.d_model_file)) {
                            C.d_model_file = f;
                            if (!C.d_id_typed) C.d_actor_id = editor::suggest_demo_id(f.substr(0, f.rfind('.')), taken);
                        }
                    ImGui::EndCombo();
                }
                list_state("files", C.d_actor_archive);
                C.d_actor_name = C.d_actor_archive + "/" + C.d_model_file;
            }
        } else {
            field_label("Which", 110);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##demo_character", C.d_actor_name.empty() ? "(one of the mod's placed actors)" : C.d_actor_name.c_str())) {
                for (const auto& a : m.get("actors").array) {
                    const std::string id = a.get("id").string();
                    if (ImGui::Selectable(id.c_str(), id == C.d_actor_name)) {
                        C.d_actor_name = id;
                        if (!C.d_id_typed) C.d_actor_id = editor::suggest_demo_id(id, taken);
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::TextUnformatted("Name in this demo (keys and events use it)");
        std::string v;
        if (edit_line("##demo_actor_id", C.d_actor_id, v, 24, "e.g. helmaroc") || v != C.d_actor_id) {
            if (v != C.d_actor_id) C.d_id_typed = !v.empty();
            C.d_actor_id = v;
        }
        const bool ready = !C.d_actor_id.empty() && (C.d_actor_kind == 1 || (C.d_actor_kind == 2 ? !C.d_model_file.empty() : !C.d_actor_name.empty()));
        ImGui::BeginDisabled(!ready);
        if (ImGui::Button("Add actor")) {
            Value a;
            a["id"] = C.d_actor_id;
            if (C.d_actor_kind == 0) a["actor"] = C.d_actor_name;
            else if (C.d_actor_kind == 1) a["actor"] = std::string("Link");
            else if (C.d_actor_kind == 2) {
                Value md;
                md["archive"] = C.d_actor_archive;
                md["file"] = C.d_model_file;
                a["model"] = md;
            } else a["character"] = C.d_actor_name;
            Value p = prm_step(n);
            p["actor"] = a;
            command("add_demo_actor", p, [id = C.d_actor_id](const Value& r) {
                if (!r.get("ok").boolean) return;
                C.d_added = id, C.d_new_lane = id + ".position", C.d_new_actor = id, C.d_id_typed = false, C.d_actor_id.clear();
            });
        }
        ImGui::EndDisabled();
        if (!ready) {
            ImGui::SameLine();
            ImGui::TextDisabled(C.d_actor_id.empty() ? "give it a name" : "choose what it is");
        }
    }
    // the selected key
    if (const Value* tr = C.d_key >= 0 ? demo_track(st, C.d_lane) : nullptr) {
        ImGui::Separator();
        ImGui::TextColored(kHead, "Key %s at frame %d", C.d_lane.c_str(), C.d_key);
        const Value* key = nullptr;
        for (const auto& k : tr->get("keys").array)
            if (int(k.get("frame").number) == C.d_key) key = &k;
        if (key) {
            Value value;
            bool changed = false;
            if (key->get("value").type == Value::Number) {
                float f = float(key->get("value").number);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##demo_key_n", &f, 0.5f);
                if (ImGui::IsItemDeactivatedAfterEdit()) value = Value(double(f)), changed = true;
            } else {
                float v3[3] = {num(key->get("value"), "x"), num(key->get("value"), "y"), num(key->get("value"), "z")};
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat3("##demo_key_v", v3, 5);
                if (ImGui::IsItemDeactivatedAfterEdit()) value["x"] = v3[0], value["y"] = v3[1], value["z"] = v3[2], changed = true;
            }
            if (changed) {
                Value p = prm_step(n);
                p["lane"] = C.d_lane;
                p["frame"] = C.d_key;
                p["value"] = value;
                command("set_demo_key", p);
            }
            const std::string curve = tr->get("curve").string("linear");
            ImGui::SetNextItemWidth(120);
            if (ImGui::BeginCombo("curve", curve.c_str())) {
                for (const char* c : kDemoCurves)
                    if (ImGui::Selectable(c, curve == c)) {
                        Value p = prm_step(n);
                        p["lane"] = C.d_lane;
                        p["curve"] = std::string(c);
                        command("set_demo_curve", p);
                    }
                ImGui::EndCombo();
            }
            const bool camera_lane = C.d_lane == "camera.eye" || C.d_lane == "camera.target";
            if ((camera_lane || C.d_lane.size() > 9 && C.d_lane.compare(C.d_lane.size() - 9, 9, ".position") == 0) && ImGui::Button(camera_lane ? "From the fly camera" : "Where the camera looks")) {
                Value p = prm_step(n);
                p["lane"] = C.d_lane;
                p["frame"] = C.d_key;
                p["from"] = std::string("camera");
                command("set_demo_key", p);
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove key")) demo_remove_selected();
        }
    }
    // the selected event
    if (const Value* ev = C.d_event >= 0 ? demo_event(st, C.d_list, C.d_actor, C.d_event) : nullptr) {
        ImGui::Separator();
        ImGui::TextColored(kHead, "%s%s event %d", C.d_actor.empty() ? "" : (C.d_actor + ".").c_str(), C.d_list.c_str(), C.d_event);
        const std::string key = C.scene + ":" + std::to_string(n) + ":" + C.d_list + ":" + C.d_actor + ":" + std::to_string(C.d_event) + editor::pretty(*ev);
        if (C.d_fields_for != key) C.d_fields_for = key, C.d_fields = editor::pretty(*ev);
        static std::vector<char> buf;
        buf.assign(C.d_fields.begin(), C.d_fields.end());
        buf.resize(C.d_fields.size() + 1024, 0);
        if (ImGui::InputTextMultiline("##demo_event", buf.data(), buf.size(), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 5))) C.d_fields = buf.data();
        if (ImGui::Button("Apply")) {
            try {
                const Value now = mods::json::parse(C.d_fields);
                Value fields;
                fields.type = Value::Object;
                for (const auto& [k, v] : ev->object)
                    if (!now.object.contains(k)) fields[k] = Value();
                for (const auto& [k, v] : now.object)
                    if (!(ev->get(k) == v)) fields[k] = v;
                Value p = prm_step(n);
                p["list"] = C.d_list;
                if (!C.d_actor.empty()) p["actor"] = C.d_actor;
                p["index"] = C.d_event;
                p["fields"] = fields;
                command("edit_demo_event", p);
            } catch (const std::exception&) {
                muted("That is not JSON.");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove event")) demo_remove_selected();
    }
    // a key at the cursor
    ImGui::Separator();
    ImGui::TextUnformatted("Add a key at the cursor");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##demo_new_lane", C.d_new_lane.c_str())) {
        std::vector<std::string> all = {"camera.eye", "camera.target", "camera.fovy", "camera.roll"};
        for (const auto& a : lanes.get("actors").array)
            for (const char* w : {".position", ".rotation", ".scale"}) all.push_back(a.get("id").string() + w);
        for (const auto& l : all)
            if (ImGui::Selectable(l.c_str(), l == C.d_new_lane)) C.d_new_lane = l;
        ImGui::EndCombo();
    }
    auto add_key = [&](const char* from) {
        Value p = prm_step(n);
        p["lane"] = C.d_new_lane;
        p["frame"] = int(at);
        if (from) p["from"] = std::string(from);
        else {
            // the lane's value at the cursor (its own coordinates), or a start value
            const Value* tr = demo_track(st, C.d_new_lane);
            const bool number = C.d_new_lane == "camera.fovy" || C.d_new_lane == "camera.roll";
            Value v;
            if (number) v = Value(C.d_new_lane == "camera.fovy" ? 55.0 : 0.0);
            else v["x"] = 0.0, v["y"] = 0.0, v["z"] = 0.0;
            if (C.d_new_lane.size() > 6 && C.d_new_lane.compare(C.d_new_lane.size() - 6, 6, ".scale") == 0 && !number) v["x"] = 1.0, v["y"] = 1.0, v["z"] = 1.0;
            if (tr) {
                Value local = st, pose;
                local.object.erase("origin");
                local.object.erase("angle");
                std::string why;
                if (editor::demo_pose(local, at, {}, 0, pose, why)) {
                    const std::string owner = C.d_new_lane.substr(0, C.d_new_lane.rfind('.')), what = C.d_new_lane.substr(C.d_new_lane.rfind('.') + 1);
                    if (owner == "camera" && pose.get("camera").get(what).type != Value::Null) v = pose.get("camera").get(what);
                    for (const auto& a : pose.get("actors").array)
                        if (a.get("id").string() == owner && a.get(what).type != Value::Null) v = a.get(what);
                }
            }
            p["value"] = v;
            // an actor's first position key: at Link (in the demo's own coordinates), not at the world's
            // (0,0,0), far out at sea (the command falls back to the value when the game isn't running)
            if (!tr && C.d_new_lane.size() > 9 && C.d_new_lane.compare(C.d_new_lane.size() - 9, 9, ".position") == 0) p["from"] = std::string("link");
        }
        command("set_demo_key", p, [lane = C.d_new_lane, at](const Value& r) {
            if (r.get("ok").boolean) demo_select_key(lane, int(at));
        });
    };
    if (ImGui::Button("Add key")) add_key(nullptr);
    const bool point_lane = C.d_new_lane.find(".position") != std::string::npos || C.d_new_lane == "camera.eye" || C.d_new_lane == "camera.target";
    if (point_lane) {
        ImGui::SameLine();
        if (ImGui::Button(C.d_new_lane == "camera.eye" ? "From the fly camera" : "Where the camera looks")) add_key("camera");
        if (C.d_new_lane.find(".position") != std::string::npos) {
            ImGui::SameLine();
            if (ImGui::Button("At Link")) add_key("link");
        }
    }
    if (C.d_new_lane.rfind("camera.", 0) == 0 && ImGui::Button("Camera key: eye and target from the fly camera")) {
        for (const char* l : {"camera.eye", "camera.target"}) {
            Value p = prm_step(n);
            p["lane"] = std::string(l);
            p["frame"] = int(at);
            p["from"] = std::string("camera");
            command("set_demo_key", p);
        }
    }
    // an event at the cursor: pickers per kind of event; they write the event's fields, which Advanced
    // shows as JSON (and takes edits there)
    ImGui::Separator();
    if (C.test_scroll == "event") ImGui::SetScrollHereY(0);
    ImGui::TextUnformatted("Add an event at the cursor");
    field_label("Kind", 70);
    ImGui::SetNextItemWidth(120);
    if (ImGui::BeginCombo("##demo_new_list", C.d_new_list.c_str())) {
        for (const char* l : kDemoLists)
            if (ImGui::Selectable(l, C.d_new_list == l)) C.d_new_list = l, C.d_fields_for.clear(), C.e_query.clear();
        ImGui::EndCombo();
    }
    const bool per_actor = C.d_new_list == "clips" || C.d_new_list == "modes" || C.d_new_list == "data";
    if (per_actor) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##demo_new_actor", C.d_new_actor.empty() ? "(whose: an actor)" : C.d_new_actor.c_str())) {
            for (const auto& a : lanes.get("actors").array)
                if (ImGui::Selectable(a.get("id").string().c_str(), a.get("id").string() == C.d_new_actor)) C.d_new_actor = a.get("id").string();
            ImGui::EndCombo();
        }
    }
    const std::string& list = C.d_new_list;
    Value ev;  // what the pickers make (Null: no picker for this kind)
    ev.type = Value::Object;
    bool picked = true;
    if (list == "sounds") {
        const auto names = editor::sound_names();
        std::vector<std::string> labels;
        for (const auto& x : names) labels.push_back(std::string(x.group) + ": " + x.name);
        char hex[16];
        std::snprintf(hex, sizeof hex, "%u", unsigned(C.e_sound));
        const int i = pick_list("##sound_list", C.e_query, labels, C.e_sound ? std::string(hex) : std::string(), [&](size_t k) { return std::to_string(names[k].id); }, 6, "search sounds: e.g. door, voice, splash");
        if (i >= 0) C.e_sound = names[size_t(i)].id;
        ImGui::BeginDisabled(!C.e_sound);
        if (ImGui::Button("Play")) {
            Value p;
            p["id"] = double(C.e_sound);
            command("play_sound", p);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "%s", C.e_sound ? editor::sound_label(C.e_sound).c_str() : "no sound chosen");
        field_label("From", 70);
        demo_actor_pick("##sound_actor", st, C.e_actor, "(no actor: everywhere)");
        if (C.e_sound) ev["id"] = double(C.e_sound);
        if (!C.e_actor.empty()) ev["actor"] = C.e_actor;
    } else if (list == "particles") {
        const auto names = editor::particle_names();
        std::vector<std::string> labels;
        for (const auto& x : names) labels.push_back(std::string(x.name) + "  [" + x.group + "]");
        const int i = pick_list("##particle_list", C.e_query, labels, C.e_particle ? std::to_string(C.e_particle) : std::string(), [&](size_t k) { return std::to_string(names[k].id); }, 6, "search particles: e.g. smoke, flash");
        if (i >= 0) C.e_particle = names[size_t(i)].id;
        ImGui::TextColored(kMuted, "%s", C.e_particle ? editor::particle_label(C.e_particle).c_str() : "no particle chosen");
        help_mark("everywhere: the game always has it. stage: only where the stage loads it.");
        field_label("On", 70);
        demo_actor_pick("##particle_actor", st, C.e_actor, "(at the demo's origin)");
        if (!C.e_actor.empty()) {
            field_label("Joint", 70);
            joint_pick("##particle_joint", actor_archive(st, C.e_actor), C.e_joint, "(the actor's position)");
        }
        if (C.e_particle) ev["id"] = double(C.e_particle);
        if (!C.e_actor.empty()) {
            ev["actor"] = C.e_actor;
            if (!C.e_joint.empty()) ev["joint"] = C.e_joint;
        } else {
            Value o;
            o["x"] = 0.0, o["y"] = 0.0, o["z"] = 0.0;
            ev["position"] = o;
        }
    } else if (list == "messages") {
        field_label("Text", 70);
        std::string v;
        if (edit_line("##message_text", C.e_text, v, 200, "what the message window says") || v != C.e_text) C.e_text = v;
        ev["text"] = C.e_text;
    } else if (list == "time") {
        field_label("Hour", 70);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##hour", &C.e_hour, 0, 23, "%d:00");
        ev["hour"] = double(C.e_hour);
    } else if (list == "weather") {
        field_label("Rain", 70);
        ImGui::SetNextItemWidth(-help_width());
        ImGui::SliderFloat("##rain", &C.e_rain, 0, 1, "%.2f");
        help_mark("0: dry, 1: full rain");
        ev["rain"] = double(std::round(C.e_rain * 100) / 100);
    } else if (list == "attach" || list == "detach") {
        field_label(list == "attach" ? "Carried" : "Let go", 70);
        demo_actor_pick("##attach_actor", st, C.e_actor, nullptr);
        if (!C.e_actor.empty()) ev["actor"] = C.e_actor;
        if (list == "attach") {
            field_label("By", 70);
            demo_actor_pick("##attach_to", st, C.e_to, nullptr);
            field_label("Joint", 70);
            joint_pick("##attach_joint", C.e_to.empty() ? std::string() : actor_archive(st, C.e_to), C.e_joint, "(choose a joint)");
            ev["to"] = C.e_to;
            ev["joint"] = C.e_joint;
            field_label("Blend", 70);
            ImGui::SetNextItemWidth(-help_width());
            ImGui::SliderInt("##attach_blend", &C.e_attach_blend, 0, 30, C.e_attach_blend ? "%d frames" : "at once");
            help_mark("Frames over which the carried actor moves from where it is onto the joint (a smooth grab).");
            if (C.e_attach_blend) ev["blend"] = C.e_attach_blend;
        }
    } else if (list == "clips" && !C.d_new_actor.empty()) {
        // a game actor or model: an animation of an archive (its own first); a mod character: its model's clips
        const Value* who = nullptr;
        for (const auto& a : st.get("actors").array)
            if (a.get("id").string() == C.d_new_actor) who = &a;
        if (who && who->get("character").type == Value::String) {
            const Value* placed = by_id(m.get("actors"), who->get("character").text);
            const std::string model = placed ? placed->get("model").string() : std::string();
            static std::map<std::string, Value> clips;
            if (!model.empty() && !clips.count(model)) {
                clips[model] = Value();
                Value p;
                p["model"] = model;
                command("model_clips", p, [model](const Value& r) { clips[model] = r.get("result").get("clips"); });
            }
            field_label("Clip", 70);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##character_clip", C.e_clip.empty() ? "(a clip of its model)" : C.e_clip.c_str())) {
                if (!model.empty())
                    for (const auto& c : clips[model].array) {
                        const std::string name = c.type == Value::String ? c.text : c.get("name").string();
                        if (ImGui::Selectable(name.c_str(), name == C.e_clip)) C.e_clip = name;
                    }
                ImGui::EndCombo();
            }
            ev["clip"] = C.e_clip;
        } else if (who && who->get("actor").string() == "Link") {
            muted("Link plays demo modes: choose the kind \"modes\".");
            picked = false;
        } else {
            const std::string own = actor_archive(st, C.d_new_actor);
            if (C.e_archive.empty()) C.e_archive = own;
            field_label("Archive", 70);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##clip_archive", C.e_archive.empty() ? "(an archive)" : C.e_archive.c_str())) {
                if (!own.empty() && ImGui::Selectable((own + " (its own)").c_str(), C.e_archive == own)) C.e_archive = own, C.e_file.clear();
                for (const auto& a : choices("archives").array)
                    if (a.string() != own && ImGui::Selectable(a.string().c_str(), a.string() == C.e_archive)) C.e_archive = a.string(), C.e_file.clear();
                ImGui::EndCombo();
            }
            std::vector<std::string> anims;
            if (!C.e_archive.empty())
                for (const auto& f : choices("files", C.e_archive).array)
                    if (ends_with(f.string(), ".bck")) anims.push_back(f.string());
            const int i = pick_list("##clip_files", C.e_query, anims, C.e_file, [&](size_t k) { return anims[k]; }, 5, "search animations");
            if (i >= 0) C.e_file = anims[size_t(i)];
            if (!C.e_archive.empty()) list_state("files", C.e_archive);
            ev["archive"] = C.e_archive;
            ev["file"] = C.e_file;
            ImGui::Checkbox("Play once##clip_once", &C.e_once);
            help_mark("Plays the animation once and holds its last pose (a grab, a landing); otherwise it repeats. Follow it with the next clip.");
            if (C.e_once) ev["loop"] = false;
        }
    } else if (list == "modes") {
        field_label("Mode", 70);
        ImGui::SetNextItemWidth(-help_width());
        if (ImGui::BeginCombo("##link_mode", C.e_mode.c_str())) {
            for (const char* mode : editor::link_mode_names())
                if (ImGui::Selectable(mode, C.e_mode == mode)) C.e_mode = mode;
            ImGui::EndCombo();
        }
        help_mark("Link's demo modes (the game's). clip: one of his demo animations.");
        if (C.e_mode == "clip") {
            std::vector<std::string> anims;
            for (const char* archive : {"LkD00", "LkD01"})
                for (const auto& f : choices("files", archive).array)
                    if (ends_with(f.string(), ".bck") && std::find(anims.begin(), anims.end(), f.string()) == anims.end()) anims.push_back(f.string());
            const int i = pick_list("##link_clips", C.e_query, anims, C.e_clip, [&](size_t k) { return anims[k]; }, 5, "search Link's demo animations");
            if (i >= 0) C.e_clip = anims[size_t(i)];
            ev["mode"] = std::string("clip");
            ev["clip"] = C.e_clip;
            ImGui::Checkbox("Play once##link_once", &C.e_once);
            help_mark("Plays the animation once and holds its last pose; otherwise it repeats.");
            if (C.e_once) ev["loop"] = false;
            // his face: the clip's own .btp (eyes and mouth) and a .btk of the same cutscene, when the archive has them
            std::string btp, btk;
            if (!C.e_clip.empty()) {
                const std::string base = C.e_clip.substr(0, C.e_clip.size() - 4);
                for (const char* archive : {"LkD00", "LkD01"})
                    for (const auto& f : choices("files", archive).array) {
                        const std::string n = f.string();
                        if (btp.empty() && n == base + ".btp") btp = n;
                        // a btk often covers several cuts (07b_cl_cut4_5_6_7_10.btk): the longest shared prefix wins
                        if (ends_with(n, ".btk") && n.substr(0, 6) == base.substr(0, 6)) {
                            auto common = [&](const std::string& x) { size_t k = 0; while (k < x.size() && k < base.size() && x[k] == base[k]) ++k; return k; };
                            if (btk.empty() || common(n) > common(btk)) btk = n;
                        }
                    }
            }
            if (!btp.empty() && !btk.empty()) {
                ImGui::Checkbox("His face too##link_face", &C.e_face);
                help_mark(("The cutscene's face animations: " + btp + ", " + btk).c_str());
                if (C.e_face) ev["btp"] = btp, ev["btk"] = btk;
            }
            field_label("Blend", 70);
            ImGui::SetNextItemWidth(-help_width());
            ImGui::SliderInt("##link_blend", &C.e_blend, -1, 30, C.e_blend < 0 ? "default" : "%d frames");
            help_mark("Frames to blend from his previous pose into this clip.");
            if (C.e_blend >= 0) ev["blend"] = C.e_blend;
        } else
            ev["mode"] = C.e_mode;
    } else
        picked = false;
    // the JSON of the event: the pickers write it; Advanced shows it for other fields and edits
    static std::string new_fields, new_fields_for;
    if (new_fields_for != C.d_new_list) new_fields_for = C.d_new_list, new_fields = demo_event_template(C.d_new_list, st), C.e_generated.clear();
    if (picked) {
        const std::string made = mods::json::dump(ev);
        if (made != C.e_generated) new_fields = C.e_generated = made;
    }
    if (C.test_advanced) ImGui::SetNextItemOpen(true);
    const bool advanced = ImGui::CollapsingHeader(picked ? "Advanced (the event as JSON)" : "The event (JSON)", picked ? 0 : ImGuiTreeNodeFlags_DefaultOpen);
    if (advanced) {
        std::string v;
        if (edit_line("##demo_new_fields", new_fields, v, 400) || v != new_fields) new_fields = v;
    }
    if (ImGui::Button("Add event")) {
        try {
            Value e = mods::json::parse(new_fields);
            e["frame"] = int(at);
            Value p = prm_step(n);
            p["list"] = C.d_new_list;
            if (per_actor) p["actor"] = C.d_new_actor;
            p["event"] = e;
            command("add_demo_event", p, [list = C.d_new_list, actor = per_actor ? C.d_new_actor : std::string()](const Value& r) {
                if (r.get("ok").boolean) demo_select_event(list, actor, int(r.get("result").get("index").number));
            });
        } catch (const std::exception&) {
            muted("The fields are not JSON.");
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
        field_label("who");
        actor_combo("##say_actor", n, m, s.get("actor").string(), true);
        std::string text;
        field_label("text");
        if (edit_line("##say_text", lines_of(s.get("text")), text, 240, "what they say")) edit_field(n, "text", lines_value(text));
        muted("Lines are separated by \" / \".");
    } else if (t == "move" || t == "face") {
        field_label("actor");
        actor_combo("##actor", n, m, s.get("actor").string(), false);
        if (s.get("position").type == Value::Object) point_row("to", n, "position", s.get("position"));
    } else if (t == "walk") {
        field_label("actor");
        actor_combo("##walk_actor", n, m, s.get("actor").string(), false);
        walk_inspector(n, s);
    } else if (t == "choice") {
        std::string text;
        field_label("question");
        if (edit_line("##choice_text", lines_of(s.get("text")), text, 240, "what is asked")) edit_field(n, "text", lines_value(text));
        choice_inspector(n, sc, s);
    } else if (t == "animation" || t == "camera_follow") {
        field_label("actor");
        actor_combo("##anim_actor", n, m, s.get("actor").string(), false);
    } else if (t == "particle" && s.get("position").type == Value::Object)
        point_row("at", n, "position", s.get("position"));
    else if (t == "demo")
        demo_inspector(n, m, s);
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
    field_label("starts by");
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
        ImGui::InputInt("step##trigger_step", &step);
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
bool demo_picture(double& frame, std::string& lane, int& key) {
    if (C.d_start < 0) return false;
    frame = C.scrub >= C.d_start && C.scrub <= C.d_start + C.d_len ? (C.scrub - C.d_start) / 2 : -1;
    lane = C.d_lane, key = C.d_key;
    return true;
}
void scene_editor(const Value& m) {
    static bool test_aids = [] {
        if (std::getenv("WWHD_TEST_EDITOR_SCENE_SETTINGS")) C.open_settings = true;
        if (const char* e = std::getenv("WWHD_TEST_EDITOR_DEMO_KEY")) {  // lane:frame, the selected demo key (pictures)
            const std::string v = e;
            const size_t a = v.rfind(':');
            if (a != std::string::npos) C.d_lane = v.substr(0, a), C.d_key = std::atoi(v.c_str() + a + 1);
        }
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
    // test aid (pictures): WWHD_TEST_EDITOR_DEMO_PICKERS=frame:what[=search][@actor][+carried][#joint],... from that TV frame on
    // shows a picker of the demo inspector: an event kind ("sounds", "attach", ...), "actor0".."actor3"
    // (What it is) or "added" (the hint after Add actor, for @actor)
    static std::vector<std::pair<uint64_t, std::string>> pickers = [] {
        std::vector<std::pair<uint64_t, std::string>> v;
        if (const char* e = std::getenv("WWHD_TEST_EDITOR_DEMO_PICKERS")) {
            const std::string all = e;
            for (size_t a = 0; a < all.size();) {
                size_t b = all.find(',', a);
                if (b == std::string::npos) b = all.size();
                const std::string item = all.substr(a, b - a);
                const size_t c = item.find(':');
                if (c != std::string::npos) v.push_back({std::strtoull(item.c_str(), nullptr, 10), item.substr(c + 1)});
                a = b + 1;
            }
        }
        return v;
    }();
    while (!pickers.empty() && render::frame_count() >= pickers.front().first) {
        std::string what = pickers.front().second, search, actor, joint, carried;
        pickers.erase(pickers.begin());
        C.test_advanced = !what.empty() && what.back() == '!';
        if (C.test_advanced) what.pop_back();
        if (const size_t at = what.find('#'); at != std::string::npos) joint = what.substr(at + 1), what.erase(at);
        if (const size_t at = what.find('+'); at != std::string::npos) carried = what.substr(at + 1), what.erase(at);
        if (const size_t at = what.find('@'); at != std::string::npos) actor = what.substr(at + 1), what.erase(at);
        if (const size_t eq = what.find('='); eq != std::string::npos) search = what.substr(eq + 1), what.erase(eq);
        C.test_scroll = what.rfind("actor", 0) == 0 || what == "added" ? "actors" : "event";
        if (what.rfind("actor", 0) == 0 && what.size() == 6) {
            C.d_actor_kind = C.d_kind_shown = what[5] - '0';
            C.d_actor_name.clear(), C.d_actor_label.clear(), C.d_actor_archive.clear(), C.d_model_file.clear(), C.d_actor_id.clear();
            (C.d_actor_kind == 2 ? C.d_model_query : C.d_actor_query) = search;
            if (C.d_actor_kind == 2) {
                const size_t slash = actor.find('/');
                C.d_actor_archive = actor.substr(0, slash);
                C.d_model_file = slash == std::string::npos ? std::string() : actor.substr(slash + 1);
                if (!C.d_model_file.empty()) C.d_actor_id = editor::suggest_demo_id(C.d_model_file.substr(0, C.d_model_file.rfind('.')), {});
            } else if (!actor.empty())
                C.d_actor_name = actor;
            C.test_pick = C.d_actor_kind == 0 && !search.empty();
        } else if (what == "added")
            C.d_added = actor;
        else {
            C.d_new_list = what, C.e_query = search, C.e_actor = actor, C.e_to = actor, C.d_new_actor = actor, C.e_joint = joint;
            C.test_pick = !search.empty();
            if (what == "attach") C.e_actor = carried;
        }
        LOG("[editor] test: the demo picker %s (%s, %s)", what.c_str(), search.c_str(), actor.c_str());
    }
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
    if (ImGui::Button("Duplicate scene")) {
        Value p;
        p["id"] = C.scene;
        command("duplicate_scene", p, [](const Value& r) {
            if (r.get("ok").boolean) C.want_scene = r.get("result").get("scene").string();
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove scene...")) C.ask_remove = true;
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
    timeline_view(m, *sc, timeline);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##scene_inspector", ImVec2(0, h), true);
    step_inspector(m, *sc, timeline);
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Scene: what starts it, native version", C.open_settings ? ImGuiTreeNodeFlags_DefaultOpen : 0)) scene_settings(m, *sc);
    ImGui::EndChild();
    // copy and paste of steps while the window has the keyboard (Ctrl+C / Ctrl+V); Delete removes the
    // selected demo key or event, else the selected step
    ImGuiIO& io = ImGui::GetIO();
    if (C.focused && !io.WantTextInput && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
        if (C.d_start >= 0 && (C.d_key >= 0 || C.d_event >= 0)) demo_remove_selected();
        else if (C.step) remove_step(C.step);
    }
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
