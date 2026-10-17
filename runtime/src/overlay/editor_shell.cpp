// The world editor's own overlays in the launch mode (editor_shell.h).
#include "editor_shell.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../editor/editor_game.h"
#include "../input_map.h"
#include "../platform/keycodes.h"
#include "editor_panel.h"
#include "extensions.h"
#include "hostui.h"
#include "imgui.h"
#include "imgui_internal.h"
// ImGuizmo needs Dear ImGui declared first
#include "ImGuizmo.h"

namespace editor_shell {
namespace {
using editor::Value;
const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kGood(0.45f, 0.82f, 0.52f, 1), kBad(0.95f, 0.55f, 0.45f, 1), kHead(0.95f, 0.85f, 0.55f, 1);
constexpr double kPi = 3.14159265358979323846;

struct ShellState {
    bool outliner = true, inspector = true, problems = false, test = false, log = false;
    bool focus_inspector = false, focus_problems = false;
    int gizmo = 0;           // 0 move, 1 turn
    int grid = 0, angle = 1;  // snapping: indexes into kGrid / kAngle
    std::string placing;      // a type from list_actor_types while placing by click
    std::string place_model;  // its model
    bool play_restore = true;
    bool dragging = false;    // the gizmo was used last frame
    float drag[16] = {};
    std::string drag_actor;
    std::vector<std::string> types, type_labels, type_groups;
    std::vector<bool> type_model;
    bool layout_loaded = false;
} S;
const float kGrid[] = {0, 10, 50, 100};
const char* const kGridNames[] = {"Grid off", "Grid 10", "Grid 50", "Grid 100"};
const float kAngle[] = {0, 15, 45, 90};
const char* const kAngleNames[] = {"Angle off", "Angle 15", "Angle 45", "Angle 90"};

float num(const Value& v, const char* k, float fallback = 0) { return v.get(k).type == Value::Number ? float(v.get(k).number) : fallback; }

// ---------------------------------------------------------------- the camera: projection into the picture
struct View {
    bool ok = false;
    float eye[3], right[3], up[3], back[3];  // camera axes (back = from the center towards the eye)
    float sx = 1, sy = 1, near_z = 10, far_z = 100000;
    ImVec2 pos, size;  // the picture inside the display
};
void normalize(float v[3]) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f)
        for (int i = 0; i < 3; i++) v[i] /= l;
}
View view_of(float w, float h) {
    View v;
    const editor::CameraView c = editor::camera_view();
    if (!c.valid) return v;
    for (int i = 0; i < 3; i++) {
        v.eye[i] = c.eye[i];
        v.back[i] = c.eye[i] - c.center[i];
    }
    normalize(v.back);
    float upv[3] = {c.up[0], c.up[1], c.up[2]};
    // right = up x back, up = back x right
    v.right[0] = upv[1] * v.back[2] - upv[2] * v.back[1];
    v.right[1] = upv[2] * v.back[0] - upv[0] * v.back[2];
    v.right[2] = upv[0] * v.back[1] - upv[1] * v.back[0];
    normalize(v.right);
    v.up[0] = v.back[1] * v.right[2] - v.back[2] * v.right[1];
    v.up[1] = v.back[2] * v.right[0] - v.back[0] * v.right[2];
    v.up[2] = v.back[0] * v.right[1] - v.back[1] * v.right[0];
    // the scales the game's projection uses (its matrix when it is a plain perspective one)
    if (c.proj[14] == -1 && c.proj[0] > 0 && c.proj[5] > 0) v.sx = c.proj[0], v.sy = c.proj[5];
    else {
        const float t = 1.0f / std::tan(float(c.fovy * kPi / 360.0));
        v.sy = t;
        v.sx = t / std::max(0.1f, c.aspect);
    }
    v.near_z = std::max(1.0f, c.near_z);
    v.far_z = std::max(v.near_z + 1, c.far_z);
    // the picture keeps the projection's shape in the display (pillar or letter boxes around it)
    const float aspect = v.sy / v.sx;
    if (w / h > aspect) v.size = ImVec2(h * aspect, h), v.pos = ImVec2((w - h * aspect) / 2, 0);
    else v.size = ImVec2(w, w / aspect), v.pos = ImVec2(0, (h - w / aspect) / 2);
    v.ok = true;
    return v;
}
bool project(const View& v, const float p[3], ImVec2& out) {
    const float d[3] = {p[0] - v.eye[0], p[1] - v.eye[1], p[2] - v.eye[2]};
    const float x = d[0] * v.right[0] + d[1] * v.right[1] + d[2] * v.right[2];
    const float y = d[0] * v.up[0] + d[1] * v.up[1] + d[2] * v.up[2];
    const float z = -(d[0] * v.back[0] + d[1] * v.back[1] + d[2] * v.back[2]);  // distance in front
    if (z < v.near_z) return false;
    out = ImVec2(v.pos.x + v.size.x * 0.5f * (1 + v.sx * x / z), v.pos.y + v.size.y * 0.5f * (1 - v.sy * y / z));
    return true;
}
void ray(const View& v, ImVec2 m, float dir[3]) {
    const float nx = (m.x - v.pos.x) / v.size.x * 2 - 1, ny = 1 - (m.y - v.pos.y) / v.size.y * 2;
    const float x = nx / v.sx, y = ny / v.sy;
    for (int i = 0; i < 3; i++) dir[i] = v.right[i] * x + v.up[i] * y - v.back[i];
    normalize(dir);
}
// ImGuizmo's matrices (column-major, OpenGL conventions)
void gl_view(const View& v, float m[16]) {
    const float* r = v.right;
    const float* u = v.up;
    const float* b = v.back;
    const float* e = v.eye;
    const float out[16] = {r[0], u[0], b[0], 0, r[1], u[1], b[1], 0, r[2], u[2], b[2], 0,
                           -(r[0] * e[0] + r[1] * e[1] + r[2] * e[2]), -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]), -(b[0] * e[0] + b[1] * e[1] + b[2] * e[2]), 1};
    std::memcpy(m, out, sizeof out);
}
void gl_projection(const View& v, float m[16]) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = v.sx;
    m[5] = v.sy;
    m[10] = -(v.far_z + v.near_z) / (v.far_z - v.near_z);
    m[11] = -1;
    m[14] = -2 * v.far_z * v.near_z / (v.far_z - v.near_z);
}

// ---------------------------------------------------------------- actors in the picture
// a placed actor's collision cylinder (its own for models; the game's usual size for game actors)
void cylinder_of(const Value& a, float& radius, float& height) {
    const std::string actor = a.get("actor").string();
    if (a.get("kind").string() == "mod") {
        radius = num(a.get("collision"), "radius", 30);
        height = num(a.get("collision"), "height", 100);
        return;
    }
    radius = 40, height = 120;
    if (actor == "chest") radius = 60, height = 80;
    else if (actor == "pot" || actor == "barrel" || actor == "crate") radius = 35, height = 70;
    else if (actor == "switch") radius = 60, height = 20;
    else if (actor == "door") radius = 150, height = 300;
    else if (actor == "resident") radius = 40, height = 150;
    const float s = num(a.get("scale"), "y", 1);
    height *= s;
    radius *= num(a.get("scale"), "x", 1);
}
bool position_of(const Value& a, float p[3]) {
    const Value& v = a.get("position");
    if (v.get("x").type != Value::Number) return false;
    p[0] = num(v, "x"), p[1] = num(v, "y"), p[2] = num(v, "z");
    return true;
}
void draw_cylinder(ImDrawList* dl, const View& v, const float p[3], float radius, float height, ImU32 colour) {
    constexpr int kSeg = 28;
    ImVec2 lo[kSeg], hi[kSeg];
    bool ok[kSeg];
    for (int i = 0; i < kSeg; i++) {
        const double t = 2 * kPi * i / kSeg;
        const float b[3] = {p[0] + float(std::sin(t)) * radius, p[1], p[2] + float(std::cos(t)) * radius};
        const float u[3] = {b[0], p[1] + height, b[2]};
        ok[i] = project(v, b, lo[i]) && project(v, u, hi[i]);
    }
    for (int i = 0; i < kSeg; i++) {
        const int j = (i + 1) % kSeg;
        if (!ok[i] || !ok[j]) continue;
        dl->AddLine(lo[i], lo[j], colour, 1.5f);
        dl->AddLine(hi[i], hi[j], colour, 1.5f);
        if (i % 7 == 0) dl->AddLine(lo[i], hi[i], colour, 1.0f);
    }
}
std::string current_stage(const Value& state) {
    const Value& fly = state.get("fly");
    if (fly.get("on").boolean) return fly.get("stage").string();
    return state.get("link").get("stage").string();
}

// ---------------------------------------------------------------- the picture: labels, cylinders, handles, clicks
void world(float w, float h) {
    const editor_ui::Snapshot& snap = editor_ui::snapshot();
    const View v = view_of(w, h);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##editor_world", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered();
    // look with the right button held, the wheel for the speed
    if (hovered && ImGui::IsMouseDown(1)) editor::add_look(io.MouseDelta.x, io.MouseDelta.y);
    if (hovered && io.MouseWheel != 0) editor::add_speed(io.MouseWheel);
    if (!v.ok) {
        ImGui::End();
        return;
    }
    const std::string stage = current_stage(snap.state);
    const std::string selected = editor_ui::selected();
    struct Label {
        std::string id;
        ImVec2 at;
        float depth;
    };
    std::vector<Label> labels;
    const Value* sel = nullptr;
    for (const auto& a : snap.manifest.get("actors").array) {
        float p[3];
        if (a.get("stage").string() != stage || !position_of(a, p)) continue;
        const float d2 = (p[0] - v.eye[0]) * (p[0] - v.eye[0]) + (p[1] - v.eye[1]) * (p[1] - v.eye[1]) + (p[2] - v.eye[2]) * (p[2] - v.eye[2]);
        if (d2 > 12000.0f * 12000.0f) continue;
        float radius, height;
        cylinder_of(a, radius, height);
        const float top[3] = {p[0], p[1] + height, p[2]};
        ImVec2 at;
        if (!project(v, top, at)) continue;
        const std::string id = a.get("id").string();
        labels.push_back({id, at, std::sqrt(d2)});
        if (id == selected) sel = &a;
    }
    // the selected actor: its cylinder and handles
    float model[16] = {};
    bool gizmo = false;
    if (sel) {
        float p[3], radius, height;
        position_of(*sel, p);
        cylinder_of(*sel, radius, height);
        draw_cylinder(dl, v, p, radius, height, IM_COL32(255, 210, 90, 230));
        const double yaw = num(sel->get("rotation"), "y") * kPi / 180;
        const float c = float(std::cos(yaw)), s = float(std::sin(yaw));
        const float m[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, p[0], p[1], p[2], 1};
        std::memcpy(model, S.dragging && S.drag_actor == selected ? S.drag : m, sizeof model);
        float view[16], proj[16];
        gl_view(v, view);
        gl_projection(v, proj);
        ImGuizmo::SetDrawlist(dl);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetRect(v.pos.x, v.pos.y, v.size.x, v.size.y);
        float snap_values[3] = {kGrid[S.grid], kGrid[S.grid], kGrid[S.grid]};
        if (S.gizmo == 1) snap_values[0] = kAngle[S.angle];
        const bool snapping = S.gizmo == 0 ? S.grid > 0 : S.angle > 0;
        ImGuizmo::Manipulate(view, proj, S.gizmo == 0 ? ImGuizmo::TRANSLATE : ImGuizmo::ROTATE_Y, ImGuizmo::WORLD, model, nullptr,
                             snapping ? snap_values : nullptr);
        gizmo = true;
        const bool using_now = ImGuizmo::IsUsing();
        if (using_now) {
            S.dragging = true;
            S.drag_actor = selected;
            std::memcpy(S.drag, model, sizeof model);
            const float q[3] = {model[12], model[13], model[14]};
            draw_cylinder(dl, v, q, radius, height, IM_COL32(120, 220, 255, 230));
        } else if (S.dragging && S.drag_actor == selected) {
            // released: one command, one undo step
            S.dragging = false;
            Value prm;
            prm["id"] = selected;
            if (S.gizmo == 0) {
                Value pos;
                pos["x"] = std::round(S.drag[12] * 10) / 10;
                pos["y"] = std::round(S.drag[13] * 10) / 10;
                pos["z"] = std::round(S.drag[14] * 10) / 10;
                prm["position"] = pos;
                editor_ui::command("move_actor", prm);
            } else {
                double deg = std::atan2(S.drag[8], S.drag[10]) * 180 / kPi;
                if (deg < 0) deg += 360;
                Value rot = sel->get("rotation").type == Value::Object ? sel->get("rotation") : Value();
                rot["x"] = num(rot, "x");
                rot["y"] = std::round(deg * 10) / 10;
                rot["z"] = num(rot, "z");
                prm["rotation"] = rot;
                editor_ui::command("rotate_actor", prm);
            }
        }
    }
    // labels: far ones fainter; the selected one highlighted
    std::sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) { return a.depth > b.depth; });
    for (const auto& l : labels) {
        const bool me = l.id == selected;
        const ImVec2 sz = ImGui::CalcTextSize(l.id.c_str());
        const ImVec2 a(l.at.x - sz.x / 2 - 4, l.at.y - sz.y - 8), b(l.at.x + sz.x / 2 + 4, l.at.y - 6);
        dl->AddRectFilled(a, b, me ? IM_COL32(120, 90, 20, 220) : IM_COL32(20, 24, 30, l.depth > 6000 ? 110 : 180), 4);
        dl->AddText(ImVec2(a.x + 4, a.y + 1), me ? IM_COL32(255, 230, 160, 255) : IM_COL32(220, 225, 235, 255), l.id.c_str());
        dl->AddCircleFilled(l.at, 3, me ? IM_COL32(255, 210, 90, 255) : IM_COL32(200, 210, 225, 220));
    }
    // a click: place the chosen type on the ground there, or select the nearest label (or nothing)
    const bool over_gizmo = gizmo && (ImGuizmo::IsOver() || ImGuizmo::IsUsing());
    if (hovered && !over_gizmo && ImGui::IsMouseClicked(0)) {
        const ImVec2 m = io.MousePos;
        if (!S.placing.empty()) {
            float dir[3];
            ray(v, m, dir);
            Value prm, o, d;
            o["x"] = v.eye[0], o["y"] = v.eye[1], o["z"] = v.eye[2];
            d["x"] = dir[0], d["y"] = dir[1], d["z"] = dir[2];
            prm["origin"] = o;
            prm["direction"] = d;
            const std::string type = S.placing, model_id = S.place_model;
            editor_ui::command("pick", prm, [type, model_id](const Value& r) {
                if (!r.get("ok").boolean) return;
                Value add;
                add["type"] = type;
                add["at"] = "position";
                add["position"] = r.get("result").get("position");
                if (r.get("result").get("stage").type == Value::String) add["stage"] = r.get("result").get("stage");
                if (r.get("result").get("room").type == Value::Number) add["room"] = r.get("result").get("room");
                if (!model_id.empty()) add["model"] = model_id;
                editor_ui::command("add_actor", add, [](const Value& a) {
                    if (a.get("ok").boolean) editor_ui::selected() = a.get("result").get("id").string();
                });
            });
            if (!io.KeyShift) S.placing.clear();  // Shift: place more of the same
        } else {
            const Label* best = nullptr;
            float best_d = 26 * 26;
            for (const auto& l : labels) {
                const float dx = l.at.x - m.x, dy = l.at.y - 12 - m.y, d = dx * dx + dy * dy;
                if (d < best_d) best_d = d, best = &l;
            }
            editor_ui::selected() = best ? best->id : std::string();
            editor_ui::focus_field().clear();
        }
    }
    if (!S.placing.empty()) {
        dl->AddText(ImVec2(io.MousePos.x + 14, io.MousePos.y + 10), IM_COL32(255, 230, 160, 255), ("place " + S.placing).c_str());
    }
    ImGui::End();
}

// ---------------------------------------------------------------- windows
void add_menu() {
    if (S.types.empty()) {
        editor_ui::command("list_actor_types", Value(), [](const Value& r) {
            if (!r.get("ok").boolean) return;
            S.types.clear(), S.type_labels.clear(), S.type_groups.clear(), S.type_model.clear();
            for (const auto& t : r.get("result").array) {
                S.types.push_back(t.get("type").string());
                S.type_labels.push_back(t.get("label").string() + (t.get("live").boolean ? "" : "  (after a restart)"));
                S.type_groups.push_back(t.get("group").string());
                S.type_model.push_back(t.get("needs_model").boolean);
            }
        });
        S.types.push_back("");  // asked once
    }
    const Value& m = editor_ui::snapshot().manifest;
    std::string group;
    for (size_t i = 0; i < S.types.size(); i++) {
        if (S.types[i].empty()) continue;
        if (S.type_groups[i] != group) {
            group = S.type_groups[i];
            ImGui::SeparatorText(group.c_str());
        }
        if (!S.type_model[i]) {
            if (ImGui::MenuItem(S.type_labels[i].c_str())) S.placing = S.types[i], S.place_model.clear();
            continue;
        }
        if (ImGui::BeginMenu(S.type_labels[i].c_str())) {
            for (const auto& md : m.get("models").array) {
                const std::string id = md.get("id").string();
                if (ImGui::MenuItem((id + "  (" + md.get("kind").string("static") + ")").c_str())) S.placing = S.types[i], S.place_model = id;
            }
            if (m.get("models").array.empty()) ImGui::TextDisabled("no models yet: import one (Outliner > Models)");
            ImGui::EndMenu();
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("then click into the picture (Shift: more than one)");
}

void toolbar(float w) {
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(w - 16, 1260.0f), 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    editor_ui::mod_bar();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (S.placing.empty() ? ImGui::Button("Add") : ImGui::Button("Stop placing")) {
        if (S.placing.empty()) ImGui::OpenPopup("add_menu");
        else S.placing.clear();
    }
    if (ImGui::BeginPopup("add_menu")) {
        add_menu();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    const char* const modes[] = {"Move", "Turn"};
    ImGui::Combo("##gizmo", &S.gizmo, modes, 2);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(95);
    ImGui::Combo("##grid", &S.grid, kGridNames, 4);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::Combo("##angle", &S.angle, kAngleNames, 4);
    // second row: play and test, the panels
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.25f, 1));
    if (ImGui::Button("Play")) {
        Value p;
        p["from"] = "camera";
        p["restore"] = S.play_restore;
        editor_ui::command("play", p);
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play as Link from the camera's place (Tab)");
    ImGui::SameLine();
    if (ImGui::Button("from Link")) {
        Value p;
        p["from"] = "link";
        p["restore"] = S.play_restore;
        editor_ui::command("play", p);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Restore at Stop", &S.play_restore);
    ImGui::SameLine();
    bool test = editor_ui::snapshot().state.get("test_mode").boolean;
    if (ImGui::Checkbox("Test mode", &test)) {
        Value p;
        p["on"] = test;
        editor_ui::command("test_mode", p);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Checkbox("Outliner", &S.outliner);
    ImGui::SameLine();
    ImGui::Checkbox("Inspector", &S.inspector);
    ImGui::SameLine();
    ImGui::Checkbox("Problems & export", &S.problems);
    ImGui::SameLine();
    ImGui::Checkbox("Camera & test", &S.test);
    ImGui::SameLine();
    ImGui::Checkbox("Log", &S.log);
    ImGui::SameLine();
    ImGui::TextDisabled("  Esc: Settings");
    ImGui::End();
}

void status_bar(float w, float h) {
    const editor_ui::Snapshot& snap = editor_ui::snapshot();
    ImGui::SetNextWindowPos(ImVec2(0, h), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::SetNextWindowBgAlpha(0.8f);
    ImGui::Begin("##editor_status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    const Value& fly = snap.state.get("fly");
    const Value& link = snap.state.get("link");
    ImGui::Text("%s room %d   camera %.0f %.0f %.0f   speed %.0f   Link %.0f %.0f %.0f", fly.get("stage").string(link.get("stage").string()).c_str(),
                int(num(link, "room")), num(fly.get("position"), "x"), num(fly.get("position"), "y"), num(fly.get("position"), "z"), num(fly, "speed"),
                num(link.get("position"), "x"), num(link.get("position"), "y"), num(link.get("position"), "z"));
    bool ok = true;
    const std::string& message = editor_ui::message(ok);
    const std::string status = editor::status();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (editor_ui::busy()) ImGui::TextColored(kHead, "working...  ");
    else if (!message.empty()) ImGui::TextColored(ok ? kGood : kBad, "%s", message.c_str());
    if (!status.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "  %s", status.c_str());
    }
    ImGui::TextColored(kMuted, "%s", "W A S D fly   E / Q up / down   right mouse look   wheel speed   click select   Tab play");
    ImGui::End();
}

void panels(float w, float h) {
    const editor_ui::Snapshot& snap = editor_ui::snapshot();
    if (S.outliner) {
        ImGui::SetNextWindowPos(ImVec2(8, 110), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, h * 0.62f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.9f);
        if (ImGui::Begin("Outliner", &S.outliner)) {
            if (ImGui::BeginTabBar("outliner_tabs")) {
                if (ImGui::BeginTabItem("Actors")) {
                    editor_ui::actor_list(snap.manifest);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Models")) {
                    editor_ui::models_area(snap.manifest);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Scenes")) {
                    editor_ui::scenes_area(snap.manifest);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Quests")) {
                    editor_ui::quests_area(snap.manifest);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Warp")) {
                    editor_ui::warp_area();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }
    if (S.inspector) {
        ImGui::SetNextWindowPos(ImVec2(w - 388, 110), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, h * 0.7f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.9f);
        if (S.focus_inspector) ImGui::SetNextWindowFocus(), S.focus_inspector = false;
        if (ImGui::Begin("Inspector", &S.inspector)) editor_ui::actor_inspector(snap.manifest);
        ImGui::End();
    }
    if (S.problems) {
        ImGui::SetNextWindowPos(ImVec2(320, h - 330), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520, 260), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.92f);
        if (S.focus_problems) ImGui::SetNextWindowFocus(), S.focus_problems = false;
        if (ImGui::Begin("Problems & export", &S.problems)) {
            editor_ui::problems_area();
            ImGui::Separator();
            editor_ui::export_area();
        }
        ImGui::End();
    }
    if (S.test) {
        ImGui::SetNextWindowPos(ImVec2(320, 140), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Camera & test", &S.test)) editor_ui::camera_test_area();
        ImGui::End();
    }
    if (S.log) {
        ImGui::SetNextWindowPos(ImVec2(320, 140), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Log", &S.log)) editor_ui::log_area();
        ImGui::End();
    }
}

// the panels' places and sizes, remembered between sessions (the host's settings, never next to the game)
void layout(ImGuiIO& io) {
    if (!S.layout_loaded) {
        S.layout_loaded = true;
        std::string saved;
        if (hostui::get("editorPanels", saved) && !saved.empty()) {
            for (auto& ch : saved)
                if (ch == '|') ch = '\n';
            ImGui::LoadIniSettingsFromMemory(saved.c_str(), saved.size());
        }
    }
    if (io.WantSaveIniSettings) {
        io.WantSaveIniSettings = false;
        size_t n = 0;
        std::string text = ImGui::SaveIniSettingsToMemory(&n);
        // only the editor's windows
        std::string kept, block;
        bool keep = false;
        auto flush = [&] {
            if (keep) kept += block;
            block.clear();
        };
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            const std::string line = text.substr(at, end - at);
            if (!line.empty() && line[0] == '[') {
                flush();
                keep = line == "[Window][Editor]" || line == "[Window][Outliner]" || line == "[Window][Inspector]" ||
                       line == "[Window][Problems & export]" || line == "[Window][Camera & test]" || line == "[Window][Log]";
            }
            block += line + "|";
            at = end + 1;
        }
        flush();
        hostui::post([kept] { hostui::set("editorPanels", kept); });
    }
}

bool wanted(std::string&) { return editor::editor_view() || editor::playing(); }
bool draw(float w, float h) {
    ImGuiIO& io = ImGui::GetIO();
    editor_ui::refresh();
    if (editor::playing()) {
        // only Stop over the running game
        ImGui::SetNextWindowPos(ImVec2(w * 0.5f, 10), ImGuiCond_Always, ImVec2(0.5f, 0));
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("##editor_play", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav);
        ImGui::TextColored(kGood, "Playing (test mode)");
        ImGui::SameLine();
        if (ImGui::Button("Stop / back to editor")) editor_ui::command("stop");
        ImGui::SameLine();
        ImGui::TextDisabled("Tab");
        ImGui::End();
        return false;
    }
    layout(io);
    ImGuizmo::BeginFrame();
    world(w, h);
    toolbar(w);
    panels(w, h);
    status_bar(w, h);
    // undo / redo / remove from the keyboard (not while typing)
    if (!io.WantTextInput) {
        const bool cmd = io.KeyCtrl || io.KeySuper;
        if (cmd && ImGui::IsKeyPressed(ImGuiKey_Z, false)) editor_ui::command(io.KeyShift ? "redo" : "undo");
        if (cmd && ImGui::IsKeyPressed(ImGuiKey_Y, false)) editor_ui::command("redo");
        if (cmd && ImGui::IsKeyPressed(ImGuiKey_D, false) && !editor_ui::selected().empty()) {
            Value p;
            p["id"] = editor_ui::selected();
            editor_ui::command("duplicate_actor", p, [](const Value& r) {
                if (r.get("ok").boolean) editor_ui::selected() = r.get("result").get("id").string();
            });
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !editor_ui::selected().empty()) {
            Value p;
            p["id"] = editor_ui::selected();
            editor_ui::command("focus", p);
        }
    }
    return false;
}

// Tab (when not typing) and the controller's View button: Play / Stop
bool key(int code, bool down, bool repeat, bool typing) {
    if (code != kVK_Tab || typing || !(editor::editor_view() || editor::playing())) return false;
    if (down && !repeat) editor::toggle_play();
    return true;
}
bool view_button(const overlay::PadValues& pad, bool menu_open) {
    static bool held = false;
    const bool now = pad[input_map::kPadOptions] > 0.5f;
    const bool press = now && !held;
    held = now;
    if (menu_open || !editor::launch_mode() || !(editor::editor_view() || editor::playing())) return false;
    if (press) editor::toggle_play();
    return now;
}
}  // namespace

void register_layers() {
    overlay::ext::add_frame_layer({wanted, draw});
    overlay::ext::InputLayer layer;
    layer.mode = [] { return editor::editor_view() ? 1 : editor::playing() ? 2 : 0; };
    layer.key = key;
    overlay::ext::set_input_layer(std::move(layer));
    overlay::ext::add_shortcut(view_button);
    // test aid: WWHD_TEST_EDITOR_WINDOWS=problems,test,log opens more windows (screenshots)
    if (const char* e = std::getenv("WWHD_TEST_EDITOR_WINDOWS")) {
        const std::string w = e;
        S.problems = w.find("problems") != std::string::npos;
        S.test = w.find("test") != std::string::npos;
        S.log = w.find("log") != std::string::npos;
    }
    if (const char* e = std::getenv("WWHD_TEST_EDITOR_PLACING")) S.placing = e;
    if (const char* e = std::getenv("WWHD_TEST_EDITOR_ACTOR")) editor_ui::selected() = e;
    if (const char* e = std::getenv("WWHD_TEST_EDITOR_FIELD")) editor_ui::focus_field() = e;
}
void show_inspector() {
    S.inspector = true;
    S.focus_inspector = true;
}
void show_problems() {
    S.problems = true;
    S.focus_problems = true;
}

}  // namespace editor_shell
