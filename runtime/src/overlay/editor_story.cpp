// The world editor's Story tab (editor_panel.h, docs/mod-editor.md "Letters, charts and items"): the open
// mod's letters on their own, treasure charts and items, each with a list, a form to add one, the selected
// one's fields, what uses it, its JSON and Remove. It reads the snapshot's manifest and changes everything
// through commands (editor/commands.h), so every edit is one undo step and tools can do the same. Render
// thread.
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "../editor/editor_game.h"
#include "../editor/mcp_game.h"
#include "editor_panel.h"
#include "editor_shell.h"
#include "hostui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "text_entry.h"

namespace editor_ui {
namespace {
const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kBad(0.95f, 0.55f, 0.45f, 1), kHead(0.95f, 0.85f, 0.55f, 1);
const char* const kKinds[] = {"letter", "chart", "item"};
const char* const kTabs[] = {"Letters", "Charts", "Items"};
const char* const kGameItems[] = {"game:purple_rupee", "game:orange_rupee", "game:silver_rupee", "game:heart_piece", "game:heart_container"};
const char* const kUses[] = {"none", "show", "open_notebook", "quest_step", "event"};

struct StoryUi {
    std::string kind = "letter";  // the shown tab
    std::map<std::string, std::string> selected;  // kind -> the selected id
    bool select_tab = false;      // show the tab of `kind` once
    std::string json, json_for;   // the selected entry as JSON text, and "kind:id" it belongs to
    char new_name[64] = {}, new_sender[64] = {}, new_description[256] = {}, new_icon[512] = {};
    char new_text[1300] = {};
    std::string text_for;         // the letter whose text is in `text`
    char text[1300] = {};
    char icon_file[512] = {};
    bool ask_remove = false;
    std::string json_error;       // why the JSON was not applied
} T;
std::mutex g_pick_mu;
std::string g_picked;  // a PNG a file chooser returned (any thread)
int g_picked_for = 0;  // 1 the new item's icon, 2 the selected item's new icon

void muted(const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
}
const Value* entry(const Value& m, const std::string& kind, const std::string& id) {
    for (const auto& v : m.get(kind + "s").array)
        if (v.get("id").string() == id) return &v;
    return nullptr;
}
std::string& selection() { return T.selected[T.kind]; }
// a one-line field showing the manifest's value unless it is being edited; true once an edit is done
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
Value with_id(const std::string& id) {
    Value p;
    p["id"] = id;
    return p;
}
void set_fields(const std::string& kind, const std::string& id, const Value& fields) {
    Value p = with_id(id);
    p["fields"] = fields;
    command("set_" + kind, p);
}
void set_field(const std::string& kind, const std::string& id, const char* field, const Value& value) {
    Value f;
    f[field] = value;
    set_fields(kind, id, f);
}
void choose_png(int purpose) {
    hostui::choose_file("Choose a picture (.png)", "png", false, [purpose](std::string path) {
        editor_mcp::user_picked(path);  // tool clients may read what the player picked
        std::lock_guard<std::mutex> lk(g_pick_mu);
        g_picked = path;
        g_picked_for = purpose;
    });
}
void take_picked() {
    std::lock_guard<std::mutex> lk(g_pick_mu);
    if (g_picked.empty()) return;
    std::snprintf(g_picked_for == 1 ? T.new_icon : T.icon_file, sizeof T.new_icon, "%s", g_picked.c_str());
    g_picked.clear();
}
// what uses the entry ("actor keeper > then", ...)
void uses(const Value& m, const std::string& kind, const std::string& id) {
    const auto refs = editor::story_references(m, kind, id);
    ImGui::TextColored(kHead, "Used by");
    if (refs.empty()) muted(kind == "letter" ? "Nothing names a letter on its own: the mod sends it while it is on." : "Nothing uses it yet.");
    for (const auto& r : refs) muted("  " + r);
}

// ---------------------------------------------------------------- the add forms
void add_form(const Value& m, const std::string& kind) {
    ImGui::TextColored(kHead, "%s", kind == "letter" ? "New letter" : kind == "chart" ? "New treasure chart" : "New item");
    if (kind == "letter") {
        ImGui::SetNextItemWidth(-FLT_MIN);
        text_entry::field("##new_sender", T.new_sender, sizeof T.new_sender, 0, "sender (the signature)");
        ImGui::InputTextMultiline("##new_text", T.new_text, sizeof T.new_text, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 5));
        ImGui::BeginDisabled(!T.new_sender[0] || !T.new_text[0]);
        if (ImGui::Button("Add letter")) {
            Value p;
            p["sender"] = std::string(T.new_sender);
            p["text"] = std::string(T.new_text);
            command("add_letter", p, [](const Value& r) {
                if (!r.get("ok").boolean) return;
                T.selected["letter"] = r.get("result").get("id").string();
                T.new_sender[0] = T.new_text[0] = 0;
            });
        }
        ImGui::EndDisabled();
        return;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    text_entry::field("##new_name", T.new_name, sizeof T.new_name, 0, "name (up to 40 bytes)");
    if (kind == "chart") {
        const Value& state = snapshot().state;
        const bool at_sea = state.get("link").get("stage").string() == "sea", camera = state.get("fly").get("on").boolean;
        ImGui::BeginDisabled(!T.new_name[0]);
        for (const char* at : {"link", "camera"}) {
            if (at[0] == 'c') ImGui::SameLine();
            ImGui::BeginDisabled(at[0] == 'l' ? !at_sea : !camera);
            if (ImGui::Button(at[0] == 'l' ? "Add with the X at Link" : "Add with the X at the camera")) {
                Value p;
                p["name"] = std::string(T.new_name);
                p["at"] = at;
                command("add_chart", p, [](const Value& r) {
                    if (!r.get("ok").boolean) return;
                    T.selected["chart"] = r.get("result").get("id").string();
                    T.new_name[0] = 0;
                });
            }
            ImGui::EndDisabled();
        }
        ImGui::EndDisabled();
        muted("The X goes where Link (or the fly camera) is on the Great Sea; move it later the same way. The chart list shows a "
              "change at once; the treasure's salvage point moves when the sea room loads again (Reload room).");
        return;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    text_entry::field("##new_description", T.new_description, sizeof T.new_description, 0, "description (its Info box)");
    ImGui::SetNextItemWidth(-90);
    text_entry::field("##new_icon", T.new_icon, sizeof T.new_icon, 0, "icon: a .png file, or a path in the package");
    ImGui::SameLine();
    if (ImGui::Button("Choose...##new_icon")) choose_png(1);
    ImGui::BeginDisabled(!T.new_name[0] || !T.new_description[0] || !T.new_icon[0]);
    if (ImGui::Button("Add item")) {
        Value p;
        p["name"] = std::string(T.new_name);
        p["description"] = std::string(T.new_description);
        const std::string icon = T.new_icon;
        // a full path is a file to copy in; anything else a path inside the package
        const bool file = icon[0] == '/' || icon.find(":\\") == 1 || icon.find(":/") == 1;
        p[file ? "icon_file" : "icon"] = icon;
        command("add_item", p, [](const Value& r) {
            if (!r.get("ok").boolean) return;
            T.selected["item"] = r.get("result").get("id").string();
            T.new_name[0] = T.new_description[0] = T.new_icon[0] = 0;
        });
    }
    ImGui::EndDisabled();
    (void)m;
}

// ---------------------------------------------------------------- the selected entry
void letter_fields(const Value& m, const Value& e) {
    const std::string id = e.get("id").string();
    std::string v;
    ImGui::TextUnformatted("Sender");
    if (edit_line("##sender", e.get("sender").string(), v, 48)) set_field("letter", id, "sender", Value(v));
    ImGui::TextUnformatted("Text");
    if (T.text_for != id || !ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        if (T.text_for != id) std::snprintf(T.text, sizeof T.text, "%s", e.get("text").string().c_str());
        T.text_for = id;
    }
    ImGui::InputTextMultiline("##text", T.text, sizeof T.text, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 6));
    if (ImGui::Button("Apply text")) set_field("letter", id, "text", Value(std::string(T.text)));
    ImGui::SameLine();
    if (ImGui::Button("Reset text")) T.text_for.clear();
    // what it encloses: the mod's charts and items
    ImGui::TextUnformatted("Encloses");
    std::vector<std::string> has;
    for (const auto& x : e.get("encloses").array) has.push_back(x.string());
    bool any = false;
    for (const char* kind : {"chart", "item"})
        for (const auto& x : m.get(std::string(kind) + "s").array) {
            const std::string ref = std::string(kind) + ":" + x.get("id").string();
            bool on = std::find(has.begin(), has.end(), ref) != has.end();
            any = true;
            if (ImGui::Checkbox((ref + "  " + x.get("name").string()).c_str(), &on)) {
                std::vector<std::string> next = has;
                if (on) next.push_back(ref);
                else next.erase(std::remove(next.begin(), next.end(), ref), next.end());
                Value list;
                list.type = Value::Array;
                for (const auto& n : next) list.array.push_back(Value(n));
                set_field("letter", id, "encloses", next.empty() ? Value() : list);
            }
        }
    if (!any) muted("The mod has no charts or items yet (the Charts and Items tabs add them).");
}

void chart_fields(const Value& m, const Value& e) {
    const std::string id = e.get("id").string();
    std::string v;
    ImGui::TextUnformatted("Name");
    if (edit_line("##name", e.get("name").string(), v, 40)) set_field("chart", id, "name", Value(v));
    ImGui::TextUnformatted("Description");
    if (edit_line("##description", e.get("description").string(), v, 200)) set_field("chart", id, "description", v.empty() ? Value() : Value(v));
    const Value& place = e.get("place");
    const int square = editor::sea_room(float(place.get("x").number), float(place.get("z").number));
    const char* island = editor::island_name(square);
    ImGui::Text("X at %.0f, %.0f  (square %d%s%s)", place.get("x").number, place.get("z").number, square, island ? ", " : "", island ? island : "");
    for (const char* at : {"link", "camera"}) {
        if (at[0] == 'c') ImGui::SameLine();
        if (ImGui::Button(at[0] == 'l' ? "X at Link" : "X at the camera")) {
            Value p = with_id(id);
            p["at"] = at;
            command("set_chart", p);
        }
    }
    // what its chest holds
    const std::string item = e.get("item").string();
    ImGui::TextUnformatted("The chest holds");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##chest", item.empty() ? "an orange rupee (default)" : item.c_str())) {
        if (ImGui::Selectable("an orange rupee (default)", item.empty())) set_field("chart", id, "item", Value());
        for (const auto& it : m.get("items").array) {
            const std::string ref = "item:" + it.get("id").string();
            if (ImGui::Selectable((ref + "  " + it.get("name").string()).c_str(), item == ref)) set_field("chart", id, "item", Value(ref));
        }
        for (const char* g : kGameItems)
            if (ImGui::Selectable(g, item == g)) set_field("chart", id, "item", Value(std::string(g)));
        ImGui::EndCombo();
    }
}

void item_fields(const Value& m, const Value& e) {
    const std::string id = e.get("id").string();
    std::string v;
    ImGui::TextUnformatted("Name");
    if (edit_line("##name", e.get("name").string(), v, 40)) set_field("item", id, "name", Value(v));
    ImGui::TextUnformatted("Description");
    if (edit_line("##description", e.get("description").string(), v, 200)) set_field("item", id, "description", Value(v));
    ImGui::Text("Icon: %s", e.get("icon").string().c_str());
    ImGui::SetNextItemWidth(-160);
    text_entry::field("##icon_file", T.icon_file, sizeof T.icon_file, 0, "a new .png file");
    ImGui::SameLine();
    if (ImGui::Button("Choose...##icon")) choose_png(2);
    ImGui::SameLine();
    ImGui::BeginDisabled(!T.icon_file[0]);
    if (ImGui::Button("Use it")) {
        Value p = with_id(id);
        p["icon_file"] = std::string(T.icon_file);
        command("set_item", p, [](const Value& r) {
            if (r.get("ok").boolean) T.icon_file[0] = 0;
        });
    }
    ImGui::EndDisabled();
    int stack = e.get("stack").type == Value::Number ? int(e.get("stack").number) : 1;
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("How many Link can carry", &stack);
    if (ImGui::IsItemDeactivatedAfterEdit()) set_field("item", id, "stack", stack == 1 ? Value() : Value(std::clamp(stack, 1, 254)));
    // what using it does
    const Value& use = e.get("use");
    const std::string action = use.get("action").string("none");
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("Use (X, Y or R)", action.c_str())) {
        for (const char* u : kUses)
            if (ImGui::Selectable(u, action == u)) {
                Value next;
                if (std::string(u) != "none") next["action"] = u;
                if (std::string(u) == "quest_step") {  // the first quest, step 1: changed below
                    next["quest"] = m.get("quests").array.empty() ? std::string() : m.get("quests").array[0].get("id").string();
                    next["step"] = 1;
                }
                Value f;
                f["use"] = next;
                if (std::string(u) == "none") f["held_model"] = Value(), f["held_frames"] = Value();
                set_fields("item", id, f);
            }
        ImGui::EndCombo();
    }
    if (action == "quest_step") {
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("Quest", use.get("quest").string().c_str())) {
            for (const auto& q : m.get("quests").array)
                if (ImGui::Selectable(q.get("id").string().c_str(), q.get("id") == use.get("quest"))) {
                    Value next = use;
                    next["quest"] = q.get("id");
                    set_field("item", id, "use", next);
                }
            ImGui::EndCombo();
        }
        int step = use.get("step").type == Value::Number ? int(use.get("step").number) : 1;
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("Step", &step);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            Value next = use;
            next["step"] = std::clamp(step, 1, 254);
            set_field("item", id, "use", next);
        }
    }
    if (action == "none") muted("Without a use the item stays in the bag (it can't go on a button).");
    muted("Its name, description, how many and use change in the game at once; a new item, its icon, held model and hold_up "
          "after a restart (save, then start the game again).");
}

void selected_entry(const Value& m, const std::string& kind, const Value& e) {
    const std::string id = e.get("id").string();
    ImGui::PushID((kind + ":" + id).c_str());
    ImGui::TextColored(kHead, "%s %s", kind.c_str(), id.c_str());
    if (kind == "letter") letter_fields(m, e);
    else if (kind == "chart") chart_fields(m, e);
    else item_fields(m, e);
    ImGui::Spacing();
    uses(m, kind, id);
    // the whole entry as JSON (the id stays)
    ImGui::Spacing();
    if (ImGui::TreeNode("JSON")) {
        if (T.json_for != kind + ":" + id) {
            Value shown = e;
            shown.object.erase("id");
            T.json = editor::pretty(shown);
            T.json_for = kind + ":" + id;
        }
        static std::vector<char> buf;
        buf.assign(T.json.begin(), T.json.end());
        buf.resize(T.json.size() + 2048, 0);
        if (ImGui::InputTextMultiline("##json", buf.data(), buf.size(), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 8))) T.json = buf.data();
        if (ImGui::Button("Apply JSON")) {
            try {
                Value next = mods::json::parse(T.json);
                if (next.type != Value::Object) throw std::runtime_error("an object {field: value} is needed");
                next.object.erase("id");
                for (const auto& [k, x] : e.object)  // a field left out is removed
                    if (k != "id" && !next.object.contains(k)) next[k] = Value();
                set_fields(kind, id, next);
                T.json_for.clear();
                T.json_error.clear();
            } catch (const std::exception& ex) {
                T.json_error = std::string("JSON: ") + ex.what();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset##json")) T.json_for.clear(), T.json_error.clear();
        if (!T.json_error.empty()) ImGui::TextColored(kBad, "%s", T.json_error.c_str());
        ImGui::TreePop();
    }
    ImGui::Spacing();
    if (ImGui::Button("Remove...")) T.ask_remove = true;
    if (T.ask_remove) {
        ImGui::OpenPopup("remove_story");
        T.ask_remove = false;
    }
    if (ImGui::BeginPopup("remove_story")) {
        ImGui::Text("Remove %s %s?", kind.c_str(), id.c_str());
        if (!editor::story_references(m, kind, id).empty())
            ImGui::TextColored(kBad, "It is still used (above): change those first, or the remove is refused.");
        if (ImGui::Button("Remove")) {
            Value p = with_id(id);
            p["confirm"] = true;
            command("remove_" + kind, p);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void kind_area(const Value& m, const std::string& kind) {
    const Value& list = m.get(kind + "s");
    std::string& sel = selection();
    for (const auto& e : list.array) {
        const std::string id = e.get("id").string();
        const std::string label = id + "   " + (kind == "letter" ? "from " + e.get("sender").string() : e.get("name").string());
        if (ImGui::Selectable((label + "##" + kind).c_str(), sel == id)) sel = id;
    }
    if (list.array.empty())
        muted(kind == "letter"  ? "No letters on their own yet. A letter's sender, text and what it encloses (charts, items): the postbox hands it out while the mod is on."
              : kind == "chart" ? "No treasure charts yet. A chart shows in the MAP screen's chart list while Link owns it, with an X at its place."
                                : "No items yet. Items are kept in the Delivery Bag; actors give them, enemies and props drop them.");
    ImGui::Separator();
    if (const Value* e = entry(m, kind, sel)) {
        selected_entry(m, kind, *e);
        ImGui::Separator();
    }
    add_form(m, kind);
}

}  // namespace

void story_area(const Value& m) {
    take_picked();
    if (!ImGui::BeginTabBar("story_tabs")) return;
    for (int i = 0; i < 3; i++) {
        const bool pick = T.select_tab && T.kind == kKinds[i];
        if (ImGui::BeginTabItem(kTabs[i], nullptr, pick ? ImGuiTabItemFlags_SetSelected : 0)) {
            if (!T.select_tab) T.kind = kKinds[i];
            kind_area(m, kKinds[i]);
            ImGui::EndTabItem();
        }
    }
    T.select_tab = false;
    ImGui::EndTabBar();
}

void show_story(const std::string& kind, const std::string& id) {
    if (kind != "letter" && kind != "chart" && kind != "item") return;
    T.kind = kind;
    T.selected[kind] = id;
    T.select_tab = true;
    editor_shell::show_story();
}

}  // namespace editor_ui
