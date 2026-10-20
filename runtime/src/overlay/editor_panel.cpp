// The world editor's panels (editor_panel.h). They only read the session's snapshot and run commands.
#include "editor_panel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "../editor/editor_game.h"
#include "../editor/mcp_game.h"
#include "editor_live_kinds.h"
#include "editor_resident.h"
#include "../mods/story_enemy_variants.h"
#include "editor_shell.h"
#include "extensions.h"
#include "game_objects_palette.h"
#include "hostui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "text_entry.h"

namespace editor_ui {
namespace {
const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kGood(0.45f, 0.82f, 0.52f, 1), kBad(0.95f, 0.55f, 0.45f, 1), kHead(0.95f, 0.85f, 0.55f, 1);

struct UiState {
    int area = 0, select_area = -1;
    std::string actor, scene, quest;
    int step = 0;
    std::string message;  // the last command's outcome
    bool message_ok = true;
    char filter[64] = {};
    std::string json_scene;  // edited scene steps
    std::string json_for;    // the scene the buffer belongs to
    Value problems;          // the last check_package
    std::string field;       // the field a clicked problem names (marked, scrolled to once)
    bool field_shown = false;
    char import_path[512] = {};
    char export_path[512] = {};
    Value import_info;       // check_model's result for import_path
    std::string import_checked;
} U;

Snapshot g_snap;
// finished commands, for the render thread
struct Done {
    std::string command;
    Value result;
    std::function<void(const Value&)> then;
};
std::mutex g_done_mu;
std::deque<Done> g_done;
std::mutex g_pick_mu;
std::string g_picked;  // a path a file chooser returned (any thread)
int g_picked_for = 0;  // 1 import, 2 export

void muted(const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}
void wrapped(const ImVec4& colour, const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
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
Value list() {
    Value v;
    v.type = Value::Array;
    return v;
}

// An edit field that shows the manifest's value unless it is being edited; true once an edit is done
// (one command, one undo step).
template <class Draw> bool edited(const char* label, Draw draw) {
    draw(ImGui::GetActiveID() == ImGui::GetID(label));
    return ImGui::IsItemDeactivatedAfterEdit();
}
bool edit_point(const char* label, const Value& current, float fallback, float out[3]) {
    static std::map<ImGuiID, std::array<float, 3>> buffers;
    auto& b = buffers[ImGui::GetID(label)];
    return edited(label, [&](bool active) {
        if (!active) b = {num(current, "x", fallback), num(current, "y", fallback), num(current, "z", fallback)};
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputFloat3(label, b.data(), "%.1f");
        out[0] = b[0], out[1] = b[1], out[2] = b[2];
    });
}
bool edit_text(const char* label, const std::string& current, std::string& out, size_t max = 240) {
    static std::map<ImGuiID, std::vector<char>> buffers;
    auto& b = buffers[ImGui::GetID(label)];
    b.resize(max + 1);
    const bool done = edited(label, [&](bool active) {
        if (!active) std::snprintf(b.data(), b.size(), "%s", current.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        text_entry::field(label, b.data(), b.size());
    });
    out = b.data();
    return done;
}
bool edit_number(const char* label, double current, double& out, float width = 160) {
    static std::map<ImGuiID, float> buffers;
    auto& b = buffers[ImGui::GetID(label)];
    const bool done = edited(label, [&](bool active) {
        if (!active) b = float(current);
        ImGui::SetNextItemWidth(width);
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
// a field a clicked problem names: shown in the problem colour once
void mark(const char* field) {
    if (U.field == field) {
        ImGui::SameLine();
        ImGui::TextColored(kBad, "<- the problem");
        if (!U.field_shown) {
            ImGui::SetScrollHereY(0.1f);
            U.field_shown = true;
        }
    }
}

// What an actor is, for the list and the inspector.
bool is_enemy(const std::string& actor) { return actor == "chuchu" || actor == "bokoblin" || actor == "moblin" || actor == "keese" || actor == "miniblin" || actor == "peahat" || actor == "darknut" || actor == "rat_hole" || actor == "bokobaba" || actor == "bubble" || actor == "octorok" || story_enemy_variants::more_enemy(actor); }
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
// whether the running game shows changes to this actor's kind at once (list_actor_types, asked again now
// and then: the game installs its kinds after the overlay starts; while the session is busy the next
// frame asks again, so an early or missed answer never sticks)
bool restart_only(const Value& a) {
    static editor_live_kinds::Live live;
    static int asked = -1000;
    const int frame = ImGui::GetFrameCount();
    if (live.empty() || frame - asked > 120)
        if (editor::try_with_session([&](editor::Session& s) {
                const Value reply = s.run("list_actor_types", Value(), false);  // kept: the answers live in it
                live = editor_live_kinds::read(reply);
            }))
            asked = frame;
    const std::string actor = a.get("actor").string();
    return editor_live_kinds::restart_only(a, live, is_enemy(actor), is_prop(actor));
}
// a text field for one string property (empty removes it)
void edit_property(const Value& p, const Value& a, const char* label, const char* property, size_t max = 32) {
    std::string t;
    ImGui::TextUnformatted(label);
    mark(property);
    if (edit_text((std::string("##") + property).c_str(), a.get(property).string(), t, max)) {
        Value s = p;
        s["property"] = property;
        s["value"] = t.empty() ? Value() : Value(t);
        command("set_actor", s);
    }
}
bool confirm_popup(const char* id, const std::string& question, const char* yes) {
    bool chosen = false;
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(question.c_str());
        if (ImGui::Button(yes)) {
            chosen = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return chosen;
}

// ---------------------------------------------------------------- the "then" actions form
struct ActionRow {
    int kind = 0;  // give, take, quest_step, flag, scene, give a chart, quest_done
    char id[64] = {};
    int value = 1;  // count, step, flag value (1 set, 0 clear)
};
constexpr int kActionCount = 7, kGiveChart = 5, kQuestDone = 6;
const char* const kActionKinds[kActionCount] = {"give an item", "take an item", "set a quest step", "set a flag", "start a scene", "give a chart",
                                                "finish a quest"};
const char* const kActionNames[kActionCount] = {"give", "take", "quest_step", "flag", "scene", "give", "quest_done"};
std::vector<ActionRow> rows_of(const Value& then) {
    std::vector<ActionRow> rows;
    for (const auto& a : then.array) {
        ActionRow r;
        const std::string k = a.get("action").string();
        for (int i = kActionCount - 1; i >= 0; i--)
            if (k == kActionNames[i] && i != kGiveChart) r.kind = i;
        if (k == "give" && a.get("id").string().rfind("chart:", 0) == 0) r.kind = kGiveChart;
        std::snprintf(r.id, sizeof r.id, "%s", a.get("id").string().c_str());
        if (r.kind <= 1) r.value = int(num(a, "count", 1));
        else if (r.kind == 2) r.value = int(num(a, "step", 1));
        else if (r.kind == 3) r.value = a.get("value").type == Value::Bool ? a.get("value").boolean : 1;
        rows.push_back(r);
    }
    return rows;
}
Value then_of(const std::vector<ActionRow>& rows) {
    Value out = list();
    for (const auto& r : rows) {
        Value a;
        a["action"] = kActionNames[r.kind];
        a["id"] = std::string(r.id);
        if (r.kind <= 1 && r.value != 1) a["count"] = r.value;
        if (r.kind == 2) a["step"] = r.value;
        if (r.kind == 3 && !r.value) a["value"] = false;
        out.array.push_back(a);
    }
    return out;
}
// the ids an action may name: the mod's items, quests, scenes; flags are free text
std::vector<std::string> choices(const Value& m, int kind) {
    std::vector<std::string> out;
    if (kind <= 1)
        for (const auto& i : m.get("items").array) out.push_back("item:" + i.get("id").string());
    else if (kind == 2)
        for (const auto& q : m.get("quests").array) out.push_back(q.get("id").string());
    else if (kind == 4)
        for (const auto& s : m.get("scenes").array) out.push_back(s.get("id").string());
    else if (kind == kGiveChart)
        for (const auto& c : m.get("charts").array) out.push_back("chart:" + c.get("id").string());
    else if (kind == kQuestDone)
        for (const auto& q : m.get("quests").array) out.push_back(q.get("id").string());
    return out;
}
void then_form(const Value& m, const Value& a, const Value& p) {
    static std::string form_for, form_json;
    static std::vector<ActionRow> rows;
    const std::string json = mods::json::dump(a.get("then"));
    if (form_for != U.actor || form_json != json) {  // another actor, or the package changed (undo)
        rows = rows_of(a.get("then"));
        form_for = U.actor;
        form_json = json;
    }
    ImGui::TextColored(kHead, "After the dialogue");
    mark("then");
    int remove = -1;
    for (size_t i = 0; i < rows.size(); i++) {
        auto& r = rows[i];
        ImGui::PushID(int(i));
        const float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(avail * 0.42f);
        ImGui::Combo("##kind", &r.kind, kActionKinds, kActionCount);
        ImGui::SameLine();
        const auto ids = choices(m, r.kind);
        ImGui::SetNextItemWidth(avail * (r.kind == 4 ? 0.48f : 0.32f));
        if (ids.empty()) text_entry::field("##id", r.id, sizeof r.id, 0, r.kind == 3 ? "flag id" : "id");
        else if (ImGui::BeginCombo("##id", r.id)) {
            for (const auto& id : ids)
                if (ImGui::Selectable(id.c_str(), id == r.id)) std::snprintf(r.id, sizeof r.id, "%s", id.c_str());
            ImGui::EndCombo();
        }
        if (r.kind <= 2) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(avail * 0.12f);
            ImGui::InputInt("##value", &r.value, 0, 0);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(r.kind == 2 ? "the quest step" : "how many");
            r.value = std::clamp(r.value, 1, 254);
        } else if (r.kind == 3) {
            ImGui::SameLine();
            bool on = r.value != 0;
            ImGui::Checkbox("##set", &on);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("on: set the flag; off: clear it");
            r.value = on;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) remove = int(i);
        ImGui::PopID();
    }
    if (remove >= 0) rows.erase(rows.begin() + remove);
    if (rows.size() < 8 && ImGui::SmallButton("+ action")) rows.push_back({});
    ImGui::SameLine();
    if (ImGui::SmallButton("Apply actions")) {
        Value q = p;
        q["actions"] = then_of(rows);
        command("set_then", q);
    }
    if (a.get("lines").type == Value::Null && !rows.empty()) muted("Actions run after dialogue: give the actor lines first.");
}
// what a placed chest holds (parameters.item: story_world_manifest.h chest_game_item, or item:<id>)
void chest_form(const Value& m, const Value& a, const Value& p) {
    static const std::pair<const char*, const char*> kGame[] = {
        {"rupee_green", "Green rupee (1)"},   {"rupee_blue", "Blue rupee (5)"},       {"rupee_yellow", "Yellow rupee (10)"},
        {"rupee_red", "Red rupee (20)"},      {"rupee_purple", "Purple rupee (50)"},  {"rupee_orange", "Orange rupee (100)"},
        {"rupee_silver", "Silver rupee (200)"}, {"heart_piece", "Piece of heart"},    {"heart_container", "Heart container"}};
    std::vector<std::pair<std::string, std::string>> choices;
    for (const auto& [v, label] : kGame) choices.push_back({v, label});
    for (const auto& i : m.get("items").array) choices.push_back({"item:" + i.get("id").string(), i.get("name").string(i.get("id").string()) + " (your item)"});
    const std::string now = a.get("parameters").get("item").string();
    std::string shown = "Orange rupee (100), the default";
    for (const auto& [v, label] : choices)
        if (v == now) shown = label;
    ImGui::TextColored(kHead, "Chest holds");
    mark("parameters");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##chest_item", shown.c_str())) {
        for (const auto& [v, label] : choices)
            if (ImGui::Selectable(label.c_str(), v == now)) {
                Value params = a.get("parameters").type == Value::Object ? a.get("parameters") : Value();
                params["item"] = v;
                Value q = p;
                q["property"] = "parameters";
                q["value"] = params;
                command("set_actor", q);
            }
        ImGui::EndCombo();
    }
}
// drops of props and enemies
void drops_form(const Value& m, const Value& a, const Value& p) {
    static std::string form_for, form_json;
    struct Row {
        char item[64] = {};
        int quantity = 1;
    };
    static std::vector<Row> rows;
    const std::string json = mods::json::dump(a.get("drops"));
    if (form_for != U.actor || form_json != json) {
        rows.clear();
        for (const auto& d : a.get("drops").array) {
            Row r;
            std::snprintf(r.item, sizeof r.item, "%s", d.get("item").string().c_str());
            r.quantity = int(num(d, "quantity", 1));
            rows.push_back(r);
        }
        form_for = U.actor;
        form_json = json;
    }
    ImGui::TextColored(kHead, "Drops");
    mark("drops");
    const auto items = choices(m, 0);
    if (items.empty()) muted("The mod declares no items to drop.");
    int remove = -1;
    for (size_t i = 0; i < rows.size(); i++) {
        ImGui::PushID(int(i) + 100);
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("##item", rows[i].item)) {
            for (const auto& id : items)
                if (ImGui::Selectable(id.c_str(), id == rows[i].item)) std::snprintf(rows[i].item, sizeof rows[i].item, "%s", id.c_str());
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("##quantity", &rows[i].quantity);
        rows[i].quantity = std::clamp(rows[i].quantity, 1, 254);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) remove = int(i);
        ImGui::PopID();
    }
    if (remove >= 0) rows.erase(rows.begin() + remove);
    if (!items.empty() && rows.size() < 8 && ImGui::SmallButton("+ drop")) rows.push_back({});
    ImGui::SameLine();
    if (ImGui::SmallButton("Apply drops")) {
        Value q = p, d = list();
        for (const auto& r : rows) {
            Value v;
            v["item"] = std::string(r.item);
            v["quantity"] = r.quantity;
            d.array.push_back(v);
        }
        q["drops"] = d;
        command("set_drops", q);
    }
}
}  // namespace

// ---------------------------------------------------------------- snapshot and commands
void refresh() {
    static int frame = -1;
    if (frame == ImGui::GetFrameCount()) return;
    frame = ImGui::GetFrameCount();
    static int locations_at = -100, villagers_at = -100;
    const bool want_locations = frame - locations_at >= 60;
    editor::try_with_session([&](editor::Session& s) {
        g_snap.valid = true;
        g_snap.mods = s.mods();
        g_snap.open = s.open_mod();
        g_snap.dirty = s.dirty(g_snap.open);
        g_snap.undo = s.undo_label();
        g_snap.redo = s.redo_label();
        const Value* m = s.manifest(g_snap.open);
        g_snap.manifest = m ? *m : Value();
        g_snap.state = s.run("read_state", Value(), false).get("result");
        if (want_locations) {
            g_snap.locations = s.run("list_locations", Value(), false).get("result");
            locations_at = frame;
            g_snap.stages = m && m->get("stages").type == Value::Array ? s.run("list_stages", Value(), false).get("result") : Value();
        }
        if (frame - villagers_at >= 30) {
            g_snap.villagers = s.run("list_villagers", Value(), false).get("result");
            villagers_at = frame;
        }
        g_snap.log.assign(s.log().begin(), s.log().end());
    });
    std::deque<Done> done;
    {
        std::lock_guard<std::mutex> lk(g_done_mu);
        done.swap(g_done);
    }
    for (auto& d : done) {
        // the pickers' lookups say nothing in the status bar when they work
        if (d.command == "demo_choices" && d.result.get("ok").boolean) {
            if (d.then) d.then(d.result);
            continue;
        }
        U.message_ok = d.result.get("ok").boolean;
        if (!U.message_ok) U.message = d.result.get("error").string();
        else if (d.result.get("result").get("note").type == Value::String) U.message = d.result.get("result").get("note").text;
        else U.message = d.command + ": done";
        if (d.then) d.then(d.result);
    }
    std::lock_guard<std::mutex> lk(g_pick_mu);
    if (!g_picked.empty()) {
        std::snprintf(g_picked_for == 1 ? U.import_path : U.export_path, sizeof U.import_path, "%s", g_picked.c_str());
        g_picked.clear();
    }
}
const Snapshot& snapshot() { return g_snap; }
void command(const std::string& name, const Value& params, std::function<void(const Value&)> then) {
    editor::run_async(name, params, [name, then](const Value& r) {
        std::lock_guard<std::mutex> lk(g_done_mu);
        g_done.push_back({name, r, then});
    });
}
bool busy() { return editor::commands_pending(); }
const std::string& message(bool& ok) {
    ok = U.message_ok;
    return U.message;
}
std::string& selected() { return U.actor; }
Value& problems() { return U.problems; }
std::string& focus_field() { return U.field; }

// ---------------------------------------------------------------- areas
void actor_list(const Value& m) {
    for (const auto& a : m.get("actors").array) {
        const std::string id = a.get("id").string();
        auto& multi = multi_selection();
        const bool in_multi = std::find(multi.begin(), multi.end(), id) != multi.end();
        if (ImGui::Selectable((id + "##actor").c_str(), U.actor == id || in_multi)) {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl || io.KeySuper || io.KeyShift) {  // more than one: Ctrl (Cmd) or Shift adds or takes away
                if (multi.empty() && !U.actor.empty()) multi.push_back(U.actor);
                if (in_multi) multi.erase(std::find(multi.begin(), multi.end(), id));
                else multi.push_back(id);
                U.actor = multi.empty() ? std::string() : (in_multi ? multi.back() : id);
            } else {
                U.actor = id;
                multi = {id};
            }
            U.field.clear();
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            Value p;
            p["id"] = id;
            command("focus", p);
        }
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "%s", kind_label(a).c_str());
    }
    if (m.get("actors").array.empty()) muted("This mod places no actors yet: Add places one.");
}

void actor_inspector(const Value& m) {
    const Value* a = by_id(m.get("actors"), U.actor);
    if (!a) {
        muted("Select an actor (in the list or by clicking it in the picture) to see and change it.");
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
    if (restart_only(*a)) wrapped(kMuted, "Changes to this kind of actor are saved in the package and take effect after a restart.");
    // place
    if (ImGui::SmallButton("Focus")) command("focus", p);
    ImGui::SameLine();
    auto place = [&](const char* at) {
        Value q = p;
        q["at"] = at;
        command("place_actor", q);
    };
    if (ImGui::SmallButton("To Link")) place("link");
    ImGui::SameLine();
    if (ImGui::SmallButton("To camera")) place("camera");
    ImGui::SameLine();
    if (ImGui::SmallButton("Snap to ground")) place("ground");
    if (ImGui::SmallButton("Warp here")) {
        Value w;
        w["location"] = "actor:" + snapshot().open + ":" + U.actor;
        command("warp_to", w);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicate"))
        command("duplicate_actor", p, [](const Value& r) {
            if (r.get("ok").boolean) U.actor = r.get("result").get("id").string();
        });
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove...")) ImGui::OpenPopup("Remove actor?");
    if (confirm_popup("Remove actor?", "Remove " + U.actor + " from the package? (Undo brings it back.)", "Remove")) {
        Value q = p;
        q["confirm"] = true;
        command("remove_actor", q);
    }
    float v[3];
    ImGui::TextUnformatted("Position");
    mark("position");
    if (edit_point("##position", a->get("position"), 0, v)) {
        p["position"] = point(v);
        command("move_actor", p);
    }
    if (actor.rfind("game:", 0) == 0) {  // a game object: its variant, turn and scale within the catalogue's limits
        static Value catalogue;
        static overlay::game_objects::Picker picker;
        if (catalogue.type == Value::Null)
            editor::try_with_session([&](editor::Session& s) { catalogue = s.run("list_game_objects", Value(), false).get("result"); });
        const Value* info = nullptr;
        for (const auto& o : catalogue.get("objects").array)
            if (o.get("type").string() == actor) info = &o;
        if (!info) return muted("\"" + actor + "\" is not in this port's game objects catalogue.");
        for (auto& [name, params] : overlay::game_objects::inspector(picker, *a, *info)) command(name, params);
        return;
    }
    ImGui::TextUnformatted("Rotation (degrees)");
    mark("rotation");
    if (edit_point("##rotation", a->get("rotation"), 0, v)) {
        Value q = p;
        q["rotation"] = point(v);
        command("rotate_actor", q);
    }
    ImGui::TextUnformatted("Scale");
    mark("scale");
    if (edit_point("##scale", a->get("scale"), 1, v)) {
        Value q = p;
        q["scale"] = point(v);
        command("scale_actor", q);
    }
    p.object.erase("position");
    if (is_mod) {
        ImGui::TextUnformatted("Model");
        mark("model");
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
        mark("collision");
        const bool r = edit_number("##radius", num(col, "radius", 30), radius, 120);
        ImGui::SameLine();
        const bool h = edit_number("##height", num(col, "height", 100), height, 120);
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
        mark("animation");
        if (edit_text("##animation", a->get("animation").string(), anim, 32)) {
            Value s = p;
            s["property"] = "animation";
            s["value"] = anim.empty() ? Value() : Value(anim);
            command("set_actor", s);
        }
    }
    if (actor == "resident") {  // its type (parameters.resident): changing it creates it again in its room
        ImGui::TextUnformatted("Type");
        mark("parameters");
        ImGui::SetNextItemWidth(-FLT_MIN);
        std::string code;
        if (editor_resident::combo("##resident_type", editor_resident::code_of(*a), code)) {
            Value params = a->get("parameters").type == Value::Object ? a->get("parameters") : Value();
            params["resident"] = code;
            Value s = p;
            s["property"] = "parameters";
            s["value"] = params;
            command("set_actor", s);
        }
    }
    if (story_enemy_variants::has_variants(actor)) {  // its declared variant (parameters.variant): changing it creates it again
        ImGui::TextUnformatted("Variant");
        mark("parameters");
        ImGui::SetNextItemWidth(-FLT_MIN);
        const std::string current = a->get("parameters").get("variant").string();
        const auto* shown = story_enemy_variants::find(actor, current);
        if (ImGui::BeginCombo("##enemy_variant", shown ? shown->label : current.c_str())) {
            for (const auto& v : story_enemy_variants::kVariants) {
                if (actor != v.kind) continue;
                const bool selected = shown == &v;
                if (ImGui::Selectable(v.label, selected) && !selected) {
                    Value s = p;
                    s["property"] = "parameters";
                    if (&v == story_enemy_variants::find(actor, "")) s["value"] = Value();  // the ordinary one: no parameter
                    else {
                        Value params;
                        params["variant"] = std::string(v.name);
                        s["value"] = params;
                    }
                    command("set_actor", s);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", v.help);
            }
            ImGui::EndCombo();
        }
    }
    if (actor == "resident" || ((actor == "bokoblin" || actor == "moblin") && !is_mod) || (is_mod && !combat && !carry && a->get("behaviour").type == Value::Null)) route_section(m, *a);
    path_section(m, *a);  // only for an actor that uses one of the mod's room paths
    water_section(m, *a);
    if (is_enemy(actor) || combat) edit_property(p, *a, "Defeated flag (set when it is beaten)", "defeated_flag");
    if (is_enemy(actor) || combat) {  // a mod switch set when every enemy naming it is defeated (doors follow it)
        ImGui::TextUnformatted("Sets switch when defeated");
        mark("defeat_switch");
        ImGui::SetNextItemWidth(-FLT_MIN);
        const std::string current = a->get("defeat_switch").string();
        if (ImGui::BeginCombo("##defeat_switch", current.empty() ? "(none)" : current.c_str())) {
            auto pick = [&](const std::string& id) {
                Value s = p;
                s["property"] = "defeat_switch";
                s["value"] = id.empty() ? Value() : Value(id);
                command("set_actor", s);
            };
            if (ImGui::Selectable("(none)", current.empty()) && !current.empty()) pick("");
            for (const auto& other : m.get("actors").array)
                if (other.get("actor").string() == "switch") {
                    const std::string id = other.get("id").string();
                    if (ImGui::Selectable(id.c_str(), id == current) && id != current) pick(id);
                }
            ImGui::EndCombo();
        }
    }
    if (actor == "switch" || actor == "door") {  // the enemies whose defeat sets this switch (or the door's)
        const std::string sw = actor == "switch" ? U.actor : a->get("switch").string();
        std::string who;
        for (const auto& other : m.get("actors").array)
            if (!sw.empty() && other.get("defeat_switch").string() == sw) who += (who.empty() ? "" : ", ") + other.get("id").string();
        if (!who.empty()) wrapped(kMuted, ("Set when all of these are defeated: " + who).c_str());
    }
    if (is_prop(actor) || (carry && a->get("carry").get("break_on_impact").boolean)) edit_property(p, *a, "Broken flag (set when it breaks)", "broken_flag");
    if (is_prop(actor)) edit_property(p, *a, "Texture (a PNG in textures/ or assets/, empty: the game's)", "texture", 120);
    if (actor == "switch") edit_property(p, *a, "Switch flag (set when pressed)", "switch_flag");
    if (actor == "door") edit_property(p, *a, "Switch (the id of the switch that opens it)", "switch");
    if (is_enemy(actor) || is_prop(actor) || combat || (carry && a->get("carry").get("break_on_impact").boolean)) drops_form(m, *a, p);
    if (actor == "chest") chest_form(m, *a, p);
    const bool talks = actor == "sign" || actor == "chest" || actor == "resident" || (is_mod && !combat && !carry);
    if (!talks) return;
    ImGui::Separator();
    ImGui::TextColored(kHead, "Dialogue (up to three lines)");
    mark("lines");
    auto lines = lines_of(a->get("lines"));
    lines.resize(3);
    for (int i = 0; i < 3; i++) {
        std::string t;
        char label[16];
        std::snprintf(label, sizeof label, "##line%d", i);
        if (edit_text(label, lines[size_t(i)], t)) {
            lines[size_t(i)] = t;
            Value l = list();
            for (const auto& s : lines)
                if (!s.empty()) l.array.push_back(Value(s));
            Value q = p;
            q["lines"] = l;
            command("set_dialogue", q);
        }
    }
    if (ImGui::SmallButton("Preview in the game")) command("preview_dialogue", p);
    ImGui::SameLine();
    if (ImGui::SmallButton("Close preview")) {
        Value q;
        q["close"] = true;
        command("preview_dialogue", q);
    }
    edit_property(p, *a, "Flag (set when talked to or opened)", "flag");
    then_form(m, *a, p);
    // when it appears and its ordered talks, as the package writes them
    if (ImGui::CollapsingHeader("Conditions and talks (JSON)")) {
        for (const char* property : {"when", "talks"}) {
            static std::map<std::string, std::string> texts;
            const std::string key = a->get("id").string() + ":" + property + ":" + mods::json::dump(a->get(property));
            auto& t = texts[std::string(property)];
            static std::map<std::string, std::string> shown_for;
            if (shown_for[property] != key) shown_for[property] = key, t = a->get(property).type == Value::Null ? std::string() : editor::pretty(a->get(property));
            static std::vector<char> buf;
            buf.assign(t.begin(), t.end());
            buf.resize(t.size() + 2048, 0);
            ImGui::TextUnformatted(property);
            if (ImGui::InputTextMultiline((std::string("##json_") + property).c_str(), buf.data(), buf.size(), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 5)))
                t = buf.data();
            if (ImGui::SmallButton((std::string("Apply ") + property).c_str())) {
                try {
                    Value q = p;
                    q["property"] = property;
                    q["value"] = t.find_first_not_of(" \n\t") == std::string::npos ? Value() : mods::json::parse(t);
                    command("set_actor", q);
                } catch (const std::exception& e) {
                    U.message_ok = false;
                    U.message = std::string(property) + ": " + e.what();
                }
            }
        }
        muted("when: quest and step, quests (all must hold), item, time, flag, not_flag. talks: up to eight "
              "{when, lines, then}; the first that matches is said (one without when last).");
    }
}

void models_area(const Value& m) {
    for (const auto& md : m.get("models").array) {
        ImGui::TextColored(kHead, "%s", md.get("id").string().c_str());
        ImGui::SameLine();
        muted(md.get("kind").string("static") + ", " + md.get("source").string());
        std::string users;
        for (const auto& a : m.get("actors").array)
            if (a.get("model") == md.get("id")) users += (users.empty() ? "" : ", ") + a.get("id").string();
        ImGui::Indent();
        muted(users.empty() ? "not used by an actor yet" : "used by " + users);
        ImGui::Unindent();
    }
    if (m.get("models").array.empty()) muted("This mod has no models of its own yet.");
    ImGui::Spacing();
    import_area();
}

void import_area() {
    ImGui::TextColored(kHead, "Import a model (.glb)");
    ImGui::SetNextItemWidth(-90);
    text_entry::field("##import_path", U.import_path, sizeof U.import_path, 0, "a .glb file");
    ImGui::SameLine();
    if (ImGui::Button("Choose...##import"))
        hostui::choose_file("Choose a model (.glb)", "glb", false, [](std::string path) {
            editor_mcp::user_picked(path);  // tool clients may read what the player picked
            std::lock_guard<std::mutex> lk(g_pick_mu);
            g_picked = path;
            g_picked_for = 1;
        });
    if (U.import_path[0] && U.import_checked != U.import_path) {
        U.import_checked = U.import_path;
        Value p;
        p["file"] = std::string(U.import_path);
        command("check_model", p, [](const Value& r) { U.import_info = r; });
    }
    const Value& r = U.import_info;
    if (U.import_path[0] && r.type == Value::Object) {
        if (!r.get("ok").boolean) wrapped(kBad, r.get("error").string());
        else {
            const Value& i = r.get("result");
            std::string clips;
            for (const auto& c : i.get("clips").array) clips += (clips.empty() ? "" : ", ") + c.string();
            ImGui::TextColored(kGood, "%s model: %.0f KB, %d vertices, %d triangles, texture %s, %d bones", i.get("kind").string().c_str(),
                               num(i, "size") / 1024, int(num(i, "vertices")), int(num(i, "triangles")), i.get("texture").string().c_str(),
                               int(num(i, "bones")));
            if (!clips.empty()) muted("clips: " + clips);
            if (ImGui::Button("Import into the package")) {
                Value p;
                p["file"] = std::string(U.import_path);
                command("import_model", p);
            }
            muted("The file is copied into the mod's models folder; the model is prepared for the game while you work.");
        }
    }
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
    if (ImGui::InputTextMultiline("##steps", buf.data(), buf.size(), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 12))) U.json_scene = buf.data();
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
    if (ImGui::Button("Open the quest editor")) show_quest(U.quest, "");
    for (const auto& q : m.get("quests").array) {
        const std::string id = q.get("id").string();
        if (!ImGui::TreeNode((id + "##quest").c_str(), "%s  %s", id.c_str(), q.get("title").string().c_str())) continue;
        for (const auto& s : q.get("steps").array) {
            const int n = int(num(s, "n"));
            const std::string label = "Step " + std::to_string(n) + ": " + s.get("text").string() + "##" + id + std::to_string(n);
            if (ImGui::Selectable(label.c_str(), U.quest == id && U.step == n)) {
                U.quest = id, U.step = n;
                show_quest(id, "step:" + std::to_string(n));
            }
        }
        ImGui::TreePop();
    }
    if (m.get("quests").array.empty()) muted("This mod has no quests: the quest editor (Quests in the toolbar) adds one.");
}

void warp_area() {
    const Value& state = snapshot().state;
    const auto& link = state.get("link");
    if (link.get("valid").boolean)
        ImGui::Text("Link: %s room %d at %.0f %.0f %.0f", link.get("stage").string().c_str(), int(num(link, "room")), num(link.get("position"), "x"),
                    num(link.get("position"), "y"), num(link.get("position"), "z"));
    if (state.get("busy").type == Value::String) wrapped(kBad, "No warps now: " + state.get("busy").text + ".");
    ImGui::BeginDisabled(!state.get("last_place").get("valid").boolean);
    if (ImGui::Button("Back to last position")) command("warp_back");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    text_entry::field("##warp_filter", U.filter, sizeof U.filter, 0, "Filter");
    std::string filter = U.filter;
    for (auto& ch : filter) ch = char(std::tolower((unsigned char)ch));
    for (const char* group : {"Quest", "Islands", "Rooms"}) {
        if (!ImGui::CollapsingHeader(group, std::strcmp(group, "Quest") == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
        for (const auto& l : snapshot().locations.array) {
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
    const Value& state = snapshot().state;
    const auto& fly = state.get("fly");
    ImGui::TextColored(kHead, "Editor camera");
    if (fly.get("on").boolean)
        ImGui::Text("at %.0f %.0f %.0f, speed %.0f", num(fly.get("position"), "x"), num(fly.get("position"), "y"), num(fly.get("position"), "z"),
                    num(fly, "speed"));
    muted("Fly with W A S D (left stick), E / Q (R / L) up and down, the right mouse button held or the arrow keys "
          "(right stick) to look, Shift (ZL) faster, C (ZR) slower, the wheel for the speed. Tab, or the View button, "
          "plays as Link and back.");
    ImGui::Spacing();
    ImGui::TextColored(kHead, "Room");
    if (ImGui::Button("Reload room")) command("reload_room");
    muted("Leaves the room and enters it again where Link stands: every actor is created again.");
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
          "your progress and place from before the test. Play turns it on.");
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
        if (ImGui::Button(label)) {  // the button is the user's own choice: confirmed
            Value q = p;
            q["confirm"] = true;
            command("load_state", q);
        }
    }
    ImGui::EndDisabled();
    if (confirm_popup("Reset quest?", "Erase this mod's quest progress and flags from the loaded Quest Log?\n"
                                      "Only the open mod's own records change.",
                      "Reset")) {
        Value p;
        p["confirm"] = true;
        command("reset_quest", p);
    }
}

void log_area() {
    const auto& log = snapshot().log;
    for (size_t i = log.size(); i-- > 0;) {
        const auto& e = log[i];
        const bool ok = e.result.get("ok").boolean;
        ImGui::PushID(int(e.seq));
        if (ImGui::TreeNode("entry", "%llu  %s%s  %s", (unsigned long long)e.seq, e.source.empty() ? "" : "[MCP] ", e.command.c_str(),
                            ok ? "ok" : e.result.get("error").string().c_str())) {
            ImGui::TextWrapped("params: %s", mods::json::dump(e.params).c_str());
            ImGui::TextWrapped("result: %s", mods::json::dump(e.result).substr(0, 4000).c_str());
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void problems_area() {
    if (ImGui::Button("Check the package"))
        command("check_package", Value(), [](const Value& r) {
            if (!r.get("ok").boolean) return;
            U.problems = r.get("result").get("problems");
            U.message_ok = U.problems.array.empty();
            U.message = U.message_ok ? "No problems: the package is valid." : std::to_string(U.problems.array.size()) + " problem(s)";
        });
    if (U.problems.type != Value::Array) {
        muted("Checks every actor, model, scene, quest, letter, chart and item as the mod manager does.");
        return;
    }
    if (U.problems.array.empty()) ImGui::TextColored(kGood, "No problems.");
    for (size_t i = 0; i < U.problems.array.size(); i++) {
        const auto& pr = U.problems.array[i];
        const std::string what = pr.get("kind").string() + (pr.get("id").type == Value::String ? " " + pr.get("id").text : std::string());
        ImGui::PushID(int(i));
        // the quest editor's own checks are warnings (yellow); the mod manager's refusals are problems (red)
        const std::string severity = pr.get("severity").string();
        ImGui::PushStyleColor(ImGuiCol_Text, severity == "warning" ? ImVec4(0.98f, 0.78f, 0.35f, 1) : severity == "info" ? kMuted : kBad);
        if (ImGui::Selectable((what + ": " + pr.get("message").string()).c_str())) {
            // jump: select the actor and look at it, and mark the field
            if (pr.get("kind").string() == "actor") {
                U.actor = pr.get("id").string();
                U.field = pr.get("field").string();
                U.field_shown = false;
                U.select_area = 0;
                Value p;
                p["id"] = U.actor;
                command("focus", p);
                editor_shell::show_inspector();
            } else if (pr.get("kind").string() == "scene") {
                U.scene = pr.get("id").string();
                U.select_area = 2;
            } else if (pr.get("kind").string() == "quest") {
                U.quest = pr.get("id").string();
                U.select_area = 3;
                show_quest(U.quest, pr.get("field").string());
            } else if (pr.get("kind").string() == "model")
                U.select_area = 1;
            else if (pr.get("kind").string() == "item" || pr.get("kind").string() == "chart" || pr.get("kind").string() == "letter") {
                U.select_area = 4;
                show_story(pr.get("kind").string(), pr.get("id").string());
            }
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

void export_area() {
    ImGui::TextColored(kHead, "Export the package");
    muted("Checks the mod, then writes its manifest and its own files (models, textures, assets) into a new or "
          "empty folder. Game files are never included.");
    ImGui::SetNextItemWidth(-90);
    text_entry::field("##export_path", U.export_path, sizeof U.export_path, 0, "a new folder");
    ImGui::SameLine();
    if (ImGui::Button("Choose...##export"))
        hostui::choose_file("Choose a folder for the package", "", true, [](std::string path) {
            editor_mcp::user_picked(path);  // tool clients may export into a folder the player picked
            std::lock_guard<std::mutex> lk(g_pick_mu);
            g_picked = path + "/" + g_snap.open;
            g_picked_for = 2;
        });
    ImGui::BeginDisabled(!U.export_path[0]);
    if (ImGui::Button("Export...")) ImGui::OpenPopup("Export package?");
    ImGui::EndDisabled();
    if (confirm_popup("Export package?", std::string("Write the package into ") + U.export_path + "?", "Export")) {
        Value p;
        p["path"] = std::string(U.export_path);
        p["confirm"] = true;
        command("export_package", p, [](const Value& r) {
            if (r.get("ok").boolean) U.message = "Exported " + std::to_string(r.get("result").get("files").array.size()) + " file(s) to " + U.export_path;
            else  // the problems, listed
                command("check_package", Value(), [](const Value& c) {
                    if (c.get("ok").boolean) U.problems = c.get("result").get("problems");
                });
        });
    }
}

namespace {
// "New mod...": a small form (id, name, author), the error under it, Create runs new_mod
struct NewModForm {
    char id[65] = {}, name[129] = {}, author[257] = {};
    std::string error;
    bool running = false, open = false;
} g_new_mod;
void new_mod_button() {
    if (ImGui::Button("New mod...")) {
        g_new_mod = NewModForm();
        g_new_mod.open = true;
        ImGui::OpenPopup("New mod");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start a new, empty mod of your own");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(460.0f, vp->WorkSize.x - 32), 0));
    if (!ImGui::BeginPopupModal("New mod", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    muted("A new, empty mod: its own package in the mod manager, turned on in your current mod profile. "
          "Add actors, quests or story items, then Save.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Id");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    text_entry::field("##new_mod_id", g_new_mod.id, sizeof g_new_mod.id, 0, "my-first-mod");
    ImGui::TextDisabled("lower-case letters, digits, '-', '_' and '.'; it can't be changed later");
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-FLT_MIN);
    text_entry::field("##new_mod_name", g_new_mod.name, sizeof g_new_mod.name, 0, "My first mod");
    ImGui::TextUnformatted("Author (optional)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    text_entry::field("##new_mod_author", g_new_mod.author, sizeof g_new_mod.author);
    // the same checks as the command, at once (the command checks again, and the mod manager after it)
    std::string hint;
    const std::string id = g_new_mod.id;
    if (!id.empty() && !editor::package_id_valid(id)) hint = "The id may only use a-z, 0-9, '-', '_' and '.' (not starting with '.').";
    else
        for (const auto& m : snapshot().mods)
            if (m == id) hint = "A mod with this id is open in the editor already.";
    if (!hint.empty()) wrapped(kBad, hint);
    else if (!g_new_mod.error.empty()) wrapped(kBad, g_new_mod.error);
    ImGui::Spacing();
    const bool ready = !g_new_mod.running && !id.empty() && hint.empty() && std::string(g_new_mod.name).find_first_not_of(' ') != std::string::npos;
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button(g_new_mod.running ? "Creating..." : "Create")) {
        Value p;
        p["id"] = id;
        p["name"] = std::string(g_new_mod.name);
        if (g_new_mod.author[0]) p["author"] = std::string(g_new_mod.author);
        p["confirm"] = true;
        g_new_mod.running = true;
        g_new_mod.error.clear();
        command("new_mod", p, [](const Value& r) {
            g_new_mod.running = false;
            if (!r.get("ok").boolean) {
                g_new_mod.error = r.get("error").string();
                return;
            }
            g_new_mod.open = false;  // closes on the next frame
            U.actor.clear(), U.scene.clear(), U.quest.clear();
            U.problems = Value();
        });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(g_new_mod.running);
    if (ImGui::Button("Cancel")) g_new_mod.open = false;
    ImGui::EndDisabled();
    if (!g_new_mod.open) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
}  // namespace

void mod_bar() {
    const Snapshot& s = snapshot();
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("##mod", s.open.empty() ? "(no mod)" : s.open.c_str())) {
        for (const auto& m : s.mods)
            if (ImGui::Selectable(m.c_str(), m == s.open)) {
                Value p;
                p["mod"] = m;
                command("open_mod", p);
                U.actor.clear(), U.scene.clear(), U.quest.clear();
                U.problems = Value();
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    new_mod_button();
    ImGui::SameLine();
    ImGui::BeginDisabled(s.undo.empty());
    if (ImGui::Button("Undo")) command("undo");
    if (!s.undo.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Undo: %s", s.undo.c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(s.redo.empty());
    if (ImGui::Button("Redo")) command("redo");
    if (!s.redo.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Redo: %s", s.redo.c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Check"))
        command("check_package", Value(), [](const Value& r) {
            if (!r.get("ok").boolean) return;
            U.problems = r.get("result").get("problems");
            U.message_ok = U.problems.array.empty();
            U.message = U.message_ok ? "No problems: the package is valid." : std::to_string(U.problems.array.size()) + " problem(s): see Problems";
            if (!U.message_ok) editor_shell::show_problems();
        });
    ImGui::SameLine();
    ImGui::BeginDisabled(!s.dirty);
    if (ImGui::Button("Save...")) ImGui::OpenPopup("Save package?");
    ImGui::SameLine();
    if (ImGui::Button("Revert...")) ImGui::OpenPopup("Revert changes?");
    ImGui::EndDisabled();
    if (confirm_popup("Save package?", "Replace manifest.json of " + s.open + " with the edited version?\nOnly that file changes.", "Save")) {
        Value p;
        p["confirm"] = true;
        command("save_package", p);
    }
    if (confirm_popup("Revert changes?", "Forget the unsaved changes to " + s.open + "? Undo brings them back.", "Revert")) {
        Value p;
        p["confirm"] = true;
        command("revert_package", p);
    }
    if (s.dirty) {
        ImGui::SameLine();
        ImGui::TextColored(kHead, "unsaved");
    }
}

// ---------------------------------------------------------------- Claude / MCP
void mcp_area() {
    ImGui::TextColored(kHead, "Claude / MCP access");
    if (!editor_mcp::available()) {
        muted("This build has no MCP interface.");
        return;
    }
    muted("Lets Claude Code, or another MCP client on this computer, use the editor's commands: warp, place and move "
          "actors, write dialogue, check the package, take screenshots. Off at every start.");
    editor_mcp::Status st = editor_mcp::status();
    bool on = st.on;
    if (ImGui::Checkbox("Claude / MCP access", &on)) {
        std::string why;
        if (!editor_mcp::set_on(on, why)) {
            U.message = why;
            U.message_ok = false;
        }
        st = editor_mcp::status();
    }
    if (!st.error.empty()) wrapped(kBad, "Could not start: " + st.error);
    if (!st.on) return;
    ImGui::TextColored(st.connected ? kGood : kMuted, "%s", st.connected ? "A client is connected." : "Waiting for a client.");
    ImGui::SameLine();
    ImGui::TextDisabled("(%llu call(s))", (unsigned long long)st.calls);
    ImGui::Text("Address: http://127.0.0.1:%d/mcp  (this computer only)", st.port);
    ImGui::TextUnformatted("Token:");
    ImGui::SameLine();
    ImGui::TextUnformatted(st.token.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy##mcp_token")) ImGui::SetClipboardText(st.token.c_str());
    ImGui::Spacing();
    ImGui::TextUnformatted("For Claude Code, run this in a terminal:");
    const std::string cmd = editor_mcp::client_command(st);
    wrapped(ImVec4(0.85f, 0.88f, 0.92f, 1), cmd);
    if (ImGui::Button("Copy command##mcp_command")) ImGui::SetClipboardText(cmd.c_str());
    muted("Exports over MCP go into " + editor_mcp::export_folder() + " or a folder you choose under Export.");
    ImGui::Spacing();
    muted("The token is new each time you turn this on and is never saved. After a restart, run "
          "\"claude mcp remove wwhd-editor\" and add it again with the new command.");
    muted("Creating a mod, removing actors, reverting, saving, exporting, resetting a quest and loading a test state ask you first "
          "(Allow / Deny). Claude reads files only inside the open package or ones you picked here; anything else asks "
          "you. Every call shows in the Log, marked [MCP].");
}

// Claude wants to: ... Allow / Deny (drawn by the editor's overlays and by this tab, once per frame)
void mcp_prompt() {
    static int drawn = -1;
    if (drawn == ImGui::GetFrameCount()) return;
    uint64_t id = 0;
    std::string text;
    if (!editor_mcp::prompt(id, text)) return;
    drawn = ImGui::GetFrameCount();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(600.0f, vp->WorkSize.x - 32), 0));
    ImGui::SetNextWindowFocus();
    ImGui::Begin("Claude / MCP##mcp_prompt", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("Allow")) editor_mcp::answer(id, true);
    ImGui::SameLine();
    if (ImGui::Button("Deny")) editor_mcp::answer(id, false);
    ImGui::End();
}

// ---------------------------------------------------------------- the Editor tab
void register_tab() {
    overlay::ext::Tab tab{"editor", "Editor", [] { draw(); }, nullptr};
    tab.shown = [] { return editor::enabled(); };
    overlay::ext::add_tab(std::move(tab), "about");
    editor_shell::register_layers();
    editor_mcp::start_for_tests();  // test runs only (WWHD_TEST_EDITOR_MCP)
}

void draw() {
    // test aids: WWHD_TEST_EDITOR_AREA=<0..10> and WWHD_TEST_EDITOR_ACTOR=<id> preselect (screenshots)
    static bool test_aids = [] {
        if (const char* a = std::getenv("WWHD_TEST_EDITOR_AREA")) U.select_area = std::atoi(a);
        if (const char* a = std::getenv("WWHD_TEST_EDITOR_ACTOR")) U.actor = a;
        if (const char* a = std::getenv("WWHD_TEST_EDITOR_FIELD")) U.field = a;
        return true;
    }();
    (void)test_aids;
    if (!editor::enabled()) return;  // the tab exists only in the editor launch mode
    refresh();
    const Snapshot& s = snapshot();
    if (s.valid && s.mods.empty()) {
        wrapped(kMuted, "None of the active mods places actors, has models, scenes or quests. Start a new mod of your own, "
                        "or turn on a mod that does (for example the SDK v3 examples) and start the game again.");
        new_mod_button();
        if (!U.message.empty()) wrapped(U.message_ok ? kGood : kBad, U.message);
        return;
    }
    mod_bar();
    const std::string status = editor::status();
    if (!U.message.empty()) wrapped(U.message_ok ? kGood : kBad, U.message);
    if (!status.empty()) wrapped(kMuted, status);
    mcp_prompt();
    static const char* const kAreas[] = {"Actors", "Models", "Scenes", "Quests", "Story", "Warp", "Camera & test", "Problems", "Export", "Log", "Stages", "Claude / MCP"};
    const int areas = int(sizeof kAreas / sizeof kAreas[0]) - (editor_mcp::available() ? 0 : 1);
    if (ImGui::BeginTabBar("editor_areas", ImGuiTabBarFlags_FittingPolicyShrink)) {
        for (int i = 0; i < areas; i++) {
            const ImGuiTabItemFlags f = U.select_area == i ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(kAreas[i], nullptr, f)) {
                U.area = i;
                ImGui::EndTabItem();
            }
        }
        U.select_area = -1;
        ImGui::EndTabBar();
    }
    const Value& m = s.manifest;
    ImGui::BeginChild("editor_area", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
    switch (U.area) {
    case 0: {
        const float list_w = std::min(260.0f, ImGui::GetContentRegionAvail().x * 0.35f);
        ImGui::BeginChild("actor_list", ImVec2(list_w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
        actor_list(m);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("actor_props", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
        actor_inspector(m);
        ImGui::EndChild();
        break;
    }
    case 1: models_area(m); break;
    case 2: scenes_area(m); break;
    case 3: quests_area(m); break;
    case 4: story_area(m); break;
    case 5: warp_area(); break;
    case 6: camera_test_area(); break;
    case 7: problems_area(); break;
    case 8: export_area(); break;
    case 10: stages_area(m); break;
    case 11: mcp_area(); break;
    default: log_area(); break;
    }
    ImGui::EndChild();
}

}  // namespace editor_ui
