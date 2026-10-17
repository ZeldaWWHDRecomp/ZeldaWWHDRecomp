// The world editor's Editor tab (editor_panel.h). It only reads the session and calls commands.
#include "editor_panel.h"

#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <array>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "../editor/editor_game.h"
#include "extensions.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "text_entry.h"

namespace editor_ui {
namespace {
using editor::Value;

const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kGood(0.45f, 0.82f, 0.52f, 1), kBad(0.95f, 0.55f, 0.45f, 1), kHead(0.95f, 0.85f, 0.55f, 1);

struct UiState {
    int area = 0, select_area = -1;
    std::string actor, scene, quest;
    int step = 0;
    std::string message;  // the last command's outcome
    bool message_ok = true;
    char filter[64] = {};
    std::string json_scene;   // edited scene steps
    std::string json_for;     // the scene the buffer belongs to
} U;

void muted(const std::string& s) { ImGui::TextColored(kMuted, "%s", s.c_str()); }
void wrapped(const ImVec4& colour, const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}

// runs a command and keeps its outcome for the status line
Value command(const std::string& name, const Value& params = Value()) {
    Value r = editor::run(name, params);
    U.message_ok = r.get("ok").boolean;
    if (!U.message_ok) U.message = r.get("error").string();
    else if (r.get("result").get("note").type == Value::String) U.message = r.get("result").get("note").text;
    else U.message = name + ": done";
    return r;
}

Value manifest_copy() {
    Value m;
    editor::with_session([&](editor::Session& s) {
        if (auto* p = s.manifest(s.open_mod())) m = *p;
    });
    return m;
}
const Value* by_id(const Value& list, const std::string& id) {
    for (const auto& v : list.array)
        if (v.get("id").string() == id) return &v;
    return nullptr;
}
float num(const Value& v, const char* k, float fallback = 0) { return v.get(k).type == Value::Number ? float(v.get(k).number) : fallback; }
Value point(const float p[3]) {
    Value v;
    v["x"] = double(p[0]);
    v["y"] = double(p[1]);
    v["z"] = double(p[2]);
    return v;
}

// An edit field that shows the manifest's value unless it is being edited; true once an edit is done
// (one command, one undo step).
template <class Draw> bool edited(const char* label, Draw draw) {
    const ImGuiID id = ImGui::GetID(label);
    const bool active = ImGui::GetActiveID() == id;
    draw(active);
    return ImGui::IsItemDeactivatedAfterEdit();
}
bool edit_point(const char* label, const Value& current, float fallback, float out[3]) {
    static std::map<ImGuiID, std::array<float, 3>> buffers;
    const ImGuiID id = ImGui::GetID(label);
    auto& b = buffers[id];
    return edited(label, [&](bool active) {
        if (!active) b = {num(current, "x", fallback), num(current, "y", fallback), num(current, "z", fallback)};
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputFloat3(label, b.data(), "%.1f");
        out[0] = b[0], out[1] = b[1], out[2] = b[2];
    });
}
bool edit_text(const char* label, const std::string& current, std::string& out, size_t max = 240) {
    static std::map<ImGuiID, std::vector<char>> buffers;
    const ImGuiID id = ImGui::GetID(label);
    auto& b = buffers[id];
    b.resize(max + 1);
    const bool done = edited(label, [&](bool active) {
        if (!active) std::snprintf(b.data(), b.size(), "%s", current.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        text_entry::field(label, b.data(), b.size());
    });
    out = b.data();
    return done;
}
bool edit_number(const char* label, double current, double& out) {
    static std::map<ImGuiID, float> buffers;
    const ImGuiID id = ImGui::GetID(label);
    auto& b = buffers[id];
    const bool done = edited(label, [&](bool active) {
        if (!active) b = float(current);
        ImGui::SetNextItemWidth(160);
        ImGui::InputFloat(label, &b, 0, 0, "%.1f");
    });
    out = b;
    return done;
}

std::vector<std::string> lines_of(const Value& v) {
    std::vector<std::string> out;
    if (v.type == Value::String) {
        size_t a = 0;
        while (a <= v.text.size()) {
            size_t b = v.text.find('\n', a);
            if (b == std::string::npos) b = v.text.size();
            out.push_back(v.text.substr(a, b - a));
            a = b + 1;
        }
    } else
        for (const auto& l : v.array) out.push_back(l.string());
    return out;
}

// What an actor is, for the list and the inspector.
bool is_enemy(const std::string& actor) { return actor == "chuchu" || actor == "bokoblin" || actor == "moblin" || actor == "keese"; }
bool is_prop(const std::string& actor) { return actor == "pot" || actor == "barrel" || actor == "crate"; }
std::string kind_label(const Value& a) {
    const std::string actor = a.get("actor").string();
    if (a.get("kind").string() == "mod") {
        std::string k = "model " + a.get("model").string();
        if (a.get("combat").type != Value::Null) k += " (enemy)";
        else if (a.get("carry").type != Value::Null) k += " (carried)";
        return k;
    }
    if (is_enemy(actor)) return actor + " (enemy)";
    if (is_prop(actor)) return actor + " (prop)";
    return actor;
}
// kinds whose changes are applied after a restart (their game state is declared at start-up)
bool restart_only(const Value& a) {
    const std::string actor = a.get("actor").string();
    return is_enemy(actor) || is_prop(actor) || actor == "switch" || actor == "door" ||
           (a.get("kind").string() == "mod" && (a.get("combat").type != Value::Null || a.get("carry").type != Value::Null));
}
// a text field for one string property (empty removes it)
void edit_property(const Value& p, const Value& a, const char* label, const char* property, size_t max = 32) {
    std::string t;
    ImGui::TextUnformatted(label);
    if (edit_text((std::string("##") + property).c_str(), a.get(property).string(), t, max)) {
        Value s = p;
        s["property"] = property;
        s["value"] = t.empty() ? Value() : Value(t);
        command("set_actor", s);
    }
}

// ---------------------------------------------------------------- areas
void actors_area(const Value& m) {
    const float list_w = std::min(260.0f, ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::BeginChild("actor_list", ImVec2(list_w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    for (const auto& a : m.get("actors").array) {
        const std::string id = a.get("id").string();
        const std::string kind = kind_label(a);
        if (ImGui::Selectable((id + "##actor").c_str(), U.actor == id)) U.actor = id;
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "%s", kind.c_str());
    }
    if (m.get("actors").array.empty()) muted("This mod places no actors.");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("actor_props", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
    const Value* a = by_id(m.get("actors"), U.actor);
    if (!a) {
        muted("Select an actor to see and change its properties.");
        ImGui::EndChild();
        return;
    }
    Value p;
    p["id"] = U.actor;
    ImGui::TextColored(kHead, "%s", U.actor.c_str());
    ImGui::SameLine();
    muted(a->get("kind").string() == "mod" ? "mod " + kind_label(*a) : "game actor: " + kind_label(*a));
    ImGui::Text("%s room %d", a->get("stage").string().c_str(), int(num(*a, "room")));
    const std::string actor = a->get("actor").string();
    const bool is_mod = a->get("kind").string() == "mod", combat = is_mod && a->get("combat").type != Value::Null,
               carry = is_mod && a->get("carry").type != Value::Null;
    if (restart_only(*a)) wrapped(kMuted, "Changes to this actor are saved in the package and take effect after a restart.");
    float v[3];
    ImGui::TextUnformatted("Position");
    if (edit_point("##position", a->get("position"), 0, v)) {
        p["position"] = point(v);
        command("move_actor", p);
    }
    ImGui::TextUnformatted("Rotation (degrees)");
    if (edit_point("##rotation", a->get("rotation"), 0, v)) {
        p["rotation"] = point(v);
        command("rotate_actor", p);
    }
    ImGui::TextUnformatted("Scale");
    if (edit_point("##scale", a->get("scale"), 1, v)) {
        p["scale"] = point(v);
        command("scale_actor", p);
    }
    const auto place = editor::query("read_state").get("result").get("link");
    if (ImGui::Button("Move to Link") && place.get("valid").boolean) {
        p["position"] = place.get("position");
        command("move_actor", p);
    }
    ImGui::SameLine();
    if (ImGui::Button("Warp here")) {
        Value w;
        w["location"] = "actor:" + editor::query("read_state").get("result").get("open_mod").string() + ":" + U.actor;
        command("warp_to", w);
    }
    if (a->get("kind").string() == "mod") {
        ImGui::TextUnformatted("Model");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##model", a->get("model").string().c_str())) {
            for (const auto& md : m.get("models").array)
                if (ImGui::Selectable(md.get("id").string().c_str(), md.get("id") == a->get("model"))) {
                    Value s = p;
                    s["property"] = "model";
                    s["value"] = md.get("id");
                    command("set_actor", s);
                }
            ImGui::EndCombo();
        }
        double radius = 0, height = 0;
        const auto& col = a->get("collision");
        ImGui::TextUnformatted("Collision radius / height");
        const bool r = edit_number("##radius", num(col, "radius", 30), radius);
        ImGui::SameLine();
        const bool h = edit_number("##height", num(col, "height", 100), height);
        if (r || h) {
            Value c = col;
            if (r) c["radius"] = radius;
            if (h) c["height"] = height;
            Value s = p;
            s["property"] = "collision";
            s["value"] = c;
            command("set_actor", s);
        }
        std::string anim;
        ImGui::TextUnformatted("Animation (skinned models)");
        if (edit_text("##animation", a->get("animation").string(), anim, 32)) {
            Value s = p;
            s["property"] = "animation";
            s["value"] = anim.empty() ? Value() : Value(anim);
            command("set_actor", s);
        }
    }
    // the phase-specific flags and links (drops, combat and carry settings stay in the manifest text)
    if (is_enemy(actor) || combat) edit_property(p, *a, "Defeated flag (set when it is beaten)", "defeated_flag");
    if (is_prop(actor) || (carry && a->get("carry").get("break_on_impact").boolean)) edit_property(p, *a, "Broken flag (set when it breaks)", "broken_flag");
    if (is_prop(actor)) edit_property(p, *a, "Texture (a PNG in textures/ or assets/, empty: the game's)", "texture", 120);
    if (actor == "switch") edit_property(p, *a, "Switch flag (set when pressed)", "switch_flag");
    if (actor == "door") edit_property(p, *a, "Switch (the id of the switch that opens it)", "switch");
    if (a->get("drops").type == Value::Array) {
        std::string d;
        for (const auto& drop : a->get("drops").array) d += (d.empty() ? "" : ", ") + drop.get("item").string() + " x" + std::to_string(int(num(drop, "quantity", 1)));
        muted("Drops: " + (d.empty() ? std::string("none") : d));
    }
    // dialogue and the generic flag: only kinds that talk or are opened
    const bool talks = actor == "sign" || actor == "chest" || actor == "resident" || (is_mod && !combat && !carry);
    if (!talks) {
        ImGui::EndChild();
        return;
    }
    ImGui::TextUnformatted("Dialogue (up to three lines)");
    auto lines = lines_of(a->get("lines"));
    lines.resize(3);
    for (int i = 0; i < 3; i++) {
        std::string t;
        char label[16];
        std::snprintf(label, sizeof label, "##line%d", i);
        if (edit_text(label, lines[size_t(i)], t)) {
            lines[size_t(i)] = t;
            Value l;
            l.type = Value::Array;
            for (const auto& s : lines)
                if (!s.empty()) l.array.push_back(Value(s));
            p["lines"] = l;
            command("set_dialogue", p);
        }
    }
    std::string flag;
    ImGui::TextUnformatted("Flag (set when talked to or opened)");
    if (edit_text("##flag", a->get("flag").string(), flag, 32)) {
        Value s = p;
        s["property"] = "flag";
        s["value"] = flag.empty() ? Value() : Value(flag);
        command("set_actor", s);
    }
    ImGui::EndChild();
}

void models_area(const Value& m) {
    const Value r = editor::query("list_models").get("result");
    if (r.array.empty()) muted("This mod has no models of its own.");
    for (const auto& md : r.array) {
        ImGui::TextColored(kHead, "%s", md.get("id").string().c_str());
        ImGui::SameLine();
        muted(md.get("kind").string("static") + ", " + md.get("source").string());
        std::string users;
        for (const auto& u : md.get("used_by").array) users += (users.empty() ? "" : ", ") + u.string();
        ImGui::Indent();
        muted(users.empty() ? "not used by an actor (prepared when an actor uses it and the game starts)" : "used by " + users);
        ImGui::Unindent();
    }
    (void)m;
    ImGui::Spacing();
    muted("Models are prepared when the game starts: a model added to the package shows after a restart. "
          "Choosing another prepared model for an actor shows at once.");
}

void scenes_area(const Value& m) {
    for (const auto& s : m.get("scenes").array) {
        const std::string id = s.get("id").string();
        if (ImGui::Selectable((id + "##scene").c_str(), U.scene == id)) U.scene = id;
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "%zu steps, trigger %s", s.get("steps").array.size(), s.get("trigger").get("type").string("none").c_str());
    }
    if (m.get("scenes").array.empty()) muted("This mod has no scenes.");
    const Value* s = by_id(m.get("scenes"), U.scene);
    if (!s) return;
    ImGui::Separator();
    if (U.json_for != U.scene) {
        U.json_scene = editor::pretty(s->get("steps"));
        U.json_for = U.scene;
    }
    muted("Steps (JSON). Scene changes take effect when the game starts again.");
    static std::vector<char> buf;
    buf.assign(U.json_scene.begin(), U.json_scene.end());
    buf.resize(U.json_scene.size() + 4096, 0);
    if (ImGui::InputTextMultiline("##steps", buf.data(), buf.size(), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 12)))
        U.json_scene = buf.data();
    if (ImGui::Button("Apply steps")) {
        try {
            Value p;
            p["id"] = U.scene;
            p["property"] = "steps";
            p["value"] = mods::json::parse(U.json_scene);
            command("set_scene", p);
        } catch (const std::exception& e) {
            U.message_ok = false;
            U.message = std::string("Steps: ") + e.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset text")) U.json_for.clear();
}

void quests_area(const Value& m) {
    for (const auto& q : m.get("quests").array) {
        const std::string id = q.get("id").string();
        if (!ImGui::TreeNode((id + "##quest").c_str(), "%s  %s", id.c_str(), q.get("title").string().c_str())) continue;
        for (const auto& s : q.get("steps").array) {
            const int n = int(num(s, "n"));
            const std::string label = "Step " + std::to_string(n) + ": " + s.get("text").string() + "##" + id + std::to_string(n);
            if (ImGui::Selectable(label.c_str(), U.quest == id && U.step == n)) U.quest = id, U.step = n;
        }
        ImGui::TreePop();
    }
    if (m.get("quests").array.empty()) muted("This mod has no quests.");
    const Value* q = by_id(m.get("quests"), U.quest);
    if (!q) return;
    const Value* step = nullptr;
    for (const auto& s : q->get("steps").array)
        if (int(num(s, "n")) == U.step) step = &s;
    if (!step) return;
    ImGui::Separator();
    ImGui::TextColored(kHead, "%s step %d", U.quest.c_str(), U.step);
    auto set = [&](const char* property, const Value& value) {
        Value p;
        p["quest"] = U.quest;
        p["step"] = U.step;
        p["property"] = property;
        p["value"] = value;
        command("set_quest_step", p);
    };
    std::string t;
    ImGui::TextUnformatted("Text");
    if (edit_text("##step_text", step->get("text").string(), t, 120)) set("text", t);
    ImGui::TextUnformatted("Where");
    if (edit_text("##step_where", step->get("where").string(), t, 60)) set("where", t.empty() ? Value() : Value(t));
    const auto& place = step->get("place");
    double x, z;
    ImGui::TextUnformatted("Place on the sea chart (x, z)");
    const bool ex = edit_number("##place_x", num(place, "x"), x);
    ImGui::SameLine();
    const bool ez = edit_number("##place_z", num(place, "z"), z);
    if (ex || ez) {
        Value p = place.type == Value::Object ? place : Value();
        if (ex) p["x"] = x;
        if (ez) p["z"] = z;
        if (p.get("x").type != Value::Number) p["x"] = 0;
        if (p.get("z").type != Value::Number) p["z"] = 0;
        set("place", p);
    }
    muted("Quest changes take effect when the game starts again (save the package first).");
}

void warp_area() {
    const Value state = editor::query("read_state").get("result");
    const auto& link = state.get("link");
    if (link.get("valid").boolean)
        ImGui::Text("Link: %s room %d at %.0f %.0f %.0f", link.get("stage").string().c_str(), int(num(link, "room")),
                    num(link.get("position"), "x"), num(link.get("position"), "y"), num(link.get("position"), "z"));
    if (state.get("busy").type == Value::String) wrapped(kBad, "No warps now: " + state.get("busy").text + ".");
    ImGui::BeginDisabled(!state.get("last_place").get("valid").boolean);
    if (ImGui::Button("Back to last position")) command("warp_back");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    text_entry::field("##warp_filter", U.filter, sizeof U.filter, 0, "Filter");
    const Value list = editor::query("list_locations").get("result");
    std::string filter = U.filter;
    for (auto& ch : filter) ch = char(std::tolower((unsigned char)ch));
    for (const char* group : {"Quest", "Islands", "Rooms"}) {
        if (!ImGui::CollapsingHeader(group, std::strcmp(group, "Quest") == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
        for (const auto& l : list.array) {
            if (l.get("group").string() != group) continue;
            std::string label = l.get("label").string();
            std::string low = label;
            for (auto& ch : low) ch = char(std::tolower((unsigned char)ch));
            if (!filter.empty() && low.find(filter) == std::string::npos) continue;
            if (ImGui::Selectable((label + "##" + l.get("id").string()).c_str())) {
                Value p;
                p["location"] = l.get("id");
                command("warp_to", p);
            }
        }
    }
}

void camera_test_area() {
    const Value state = editor::query("read_state").get("result");
    const auto& fly = state.get("fly");
    ImGui::TextColored(kHead, "Fly camera");
    if (!fly.get("on").boolean) {
        if (ImGui::Button("Start fly camera")) {
            Value p;
            p["on"] = true;
            command("fly_camera", p);
        }
        muted("Link stays where he is and gets no input. Fly with W A S D (left stick), E / Q (R / L) up and down, "
              "the arrow keys or the right stick to look, the mouse when the mouse camera holds it, Shift (ZL) faster, "
              "C (ZR) slower, the wheel for the speed. B (J) ends it, A (K or Space) ends it with Link at the camera.");
    } else {
        ImGui::Text("Camera at %.0f %.0f %.0f, speed %.0f", num(fly.get("position"), "x"), num(fly.get("position"), "y"),
                    num(fly.get("position"), "z"), num(fly, "speed"));
        Value p;
        p["on"] = false;
        if (ImGui::Button("Stop")) command("fly_camera", p);
        ImGui::SameLine();
        p["place_link"] = true;
        if (ImGui::Button("Stop and place Link here")) command("fly_camera", p);
    }
    ImGui::Spacing();
    ImGui::TextColored(kHead, "Room");
    if (ImGui::Button("Reload room")) command("reload_room");
    muted("Leaves the room and enters it again where Link stands: every actor is created again. Needed when an actor "
          "could not be created (for example a model over the room's memory budget).");
    ImGui::Spacing();
    ImGui::TextColored(kHead, "Test mode");
    const bool test = state.get("test_mode").boolean;
    bool on = test;
    if (ImGui::Checkbox("Test mode##editor_test", &on)) {
        Value p;
        p["on"] = on;
        command("test_mode", p);
    }
    muted("While it is on, saving in the game writes a separate test save, never your own. Turning it off goes back to "
          "your progress and place from before the test.");
    ImGui::BeginDisabled(!test);
    if (ImGui::Button("Reset this mod's quest...")) ImGui::OpenPopup("Reset quest?");
    for (int slot = 1; slot <= editor::kTestSlots; slot++) {
        Value p;
        p["slot"] = slot;
        char label[32];
        std::snprintf(label, sizeof label, "Save test %d", slot);
        if (ImGui::Button(label)) command("save_state", p);
        ImGui::SameLine();
        std::snprintf(label, sizeof label, "Load test %d", slot);
        if (ImGui::Button(label)) command("load_state", p);
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("Reset quest?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Erase this mod's quest progress and flags from the loaded Quest Log?");
        muted("Only the open mod's own records change; other mods and the game's own progress stay as they are.");
        if (ImGui::Button("Reset")) {
            Value p;
            p["confirm"] = true;
            command("reset_quest", p);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void log_area() {
    editor::with_session([](editor::Session& s) {
        const auto& log = s.log();
        for (size_t i = log.size(); i-- > 0;) {
            const auto& e = log[i];
            const bool ok = e.result.get("ok").boolean;
            ImGui::PushID(int(e.seq));
            if (ImGui::TreeNode("entry", "%llu  %s  %s", (unsigned long long)e.seq, e.command.c_str(), ok ? "ok" : e.result.get("error").string().c_str())) {
                ImGui::TextWrapped("params: %s", mods::json::dump(e.params).c_str());
                ImGui::TextWrapped("result: %s", mods::json::dump(e.result).substr(0, 4000).c_str());
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    });
}

bool fly_wanted(std::string&) { return editor::fly_view().on; }
bool fly_hud(float w, float h) {
    const auto v = editor::fly_view();
    ImGui::SetNextWindowPos(ImVec2(12, h - 12), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##fly_hud", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::Text("Fly camera  %s  (Link in room %d)", v.stage.c_str(), v.room);
    ImGui::Text("%.0f  %.0f  %.0f   speed %.0f", v.eye[0], v.eye[1], v.eye[2], v.speed);
    ImGui::TextColored(kMuted, "WASD move  E/Q up/down  arrows/mouse look  Shift faster  J (B) back  K (A) Link here");
    ImGui::End();
    (void)w;
    return false;
}
}  // namespace

void register_tab() {
    overlay::ext::Tab tab{"editor", "Editor", [] { draw(); }, nullptr};
    tab.shown = [] { return editor::enabled(); };
    overlay::ext::add_tab(std::move(tab), "about");
    overlay::ext::add_frame_layer({fly_wanted, fly_hud});
}

void draw() {
    // test aids: WWHD_TEST_EDITOR_AREA=<0..6> and WWHD_TEST_EDITOR_ACTOR=<id> preselect (screenshots)
    static bool test_aids = [] {
        if (const char* a = std::getenv("WWHD_TEST_EDITOR_AREA")) U.select_area = std::atoi(a);
        if (const char* a = std::getenv("WWHD_TEST_EDITOR_ACTOR")) U.actor = a;
        return true;
    }();
    (void)test_aids;
    if (!editor::enabled()) return;  // the tab exists only in the editor launch mode
    // the open mod, undo / redo, validate, save
    std::vector<std::string> mods;
    std::string open;
    bool dirty = false;
    std::string undo, redo;
    editor::with_session([&](editor::Session& s) {
        mods = s.mods();
        open = s.open_mod();
        dirty = s.dirty(open);
        undo = s.undo_label();
        redo = s.redo_label();
    });
    if (mods.empty()) {
        wrapped(kMuted, "None of the active mods places actors, has models, scenes or quests. Turn on a mod that does "
                        "(for example the SDK v3 examples) and start the game again.");
        return;
    }
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("##mod", open.c_str())) {
        for (const auto& m : mods)
            if (ImGui::Selectable(m.c_str(), m == open)) {
                Value p;
                p["mod"] = m;
                command("open_mod", p);
                U.actor.clear(), U.scene.clear(), U.quest.clear();
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(undo.empty());
    if (ImGui::Button("Undo")) command("undo");
    if (!undo.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Undo: %s", undo.c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(redo.empty());
    if (ImGui::Button("Redo")) command("redo");
    if (!redo.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Redo: %s", redo.c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Check")) {
        const Value r = command("validate");
        if (r.get("ok").boolean) {
            U.message_ok = r.get("result").get("valid").boolean;
            U.message = U.message_ok ? "The package is valid." : r.get("result").get("problem").string();
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Save package...")) ImGui::OpenPopup("Save package?");
    ImGui::SameLine();
    if (ImGui::Button("Revert...")) ImGui::OpenPopup("Revert changes?");
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("Save package?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Replace manifest.json of %s with the edited version?", open.c_str());
        muted("Only that file changes. The fields are written in alphabetical order.");
        if (ImGui::Button("Save")) {
            Value p;
            p["confirm"] = true;
            command("save_package", p);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Revert changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Forget the unsaved changes to %s?", open.c_str());
        muted("Undo brings them back.");
        if (ImGui::Button("Revert")) {
            Value p;
            p["confirm"] = true;
            command("revert_package", p);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (dirty) {
        ImGui::SameLine();
        ImGui::TextColored(kHead, "unsaved");
    }
    const std::string status = editor::status();
    if (!U.message.empty()) wrapped(U.message_ok ? kGood : kBad, U.message);
    if (!status.empty()) wrapped(kMuted, status);

    static const char* const kAreas[] = {"Actors", "Models", "Scenes", "Quests", "Warp", "Camera & test", "Log"};
    if (ImGui::BeginTabBar("editor_areas", ImGuiTabBarFlags_FittingPolicyShrink)) {
        for (int i = 0; i < int(sizeof kAreas / sizeof kAreas[0]); i++) {
            const ImGuiTabItemFlags f = U.select_area == i ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(kAreas[i], nullptr, f)) {
                U.area = i;
                ImGui::EndTabItem();
            }
        }
        U.select_area = -1;
        ImGui::EndTabBar();
    }
    const Value m = manifest_copy();
    ImGui::BeginChild("editor_area", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
    switch (U.area) {
    case 0: actors_area(m); break;
    case 1: models_area(m); break;
    case 2: scenes_area(m); break;
    case 3: quests_area(m); break;
    case 4: warp_area(); break;
    case 5: camera_test_area(); break;
    default: log_area(); break;
    }
    ImGui::EndChild();
}

}  // namespace editor_ui
