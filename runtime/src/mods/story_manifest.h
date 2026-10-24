// Story quests declared in a mod's manifest.json ("quests_format": 1, "quests": [...]), read when the mod
// is enabled, without running mod code (docs/mod-sdk-v2.md, "Quests in the manifest"). The port then
// sends the quest's letter, starts the quest when the letter is read, hands Link the enclosed letter,
// shows each step's texts and place, and runs the declared resident talks; the mod's code only reports
// progress the port can't see. Pure code, unit-tested in runtime/tools/story_notebook_test.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

#include "mod_json.h"
#include "story_mail_book.h"
#include "story_npc_catalogue.h"
#include "story_notebook_list.h"
#include "story_sail.h"
#include "story_takeover.h"
#include "story_world_manifest.h"

namespace story_notebook {

// The story system's required package (the mod repository's "story-notebook"): its letter brings the
// story notebook, its manifest's "notebook" block gives the notebook's look, and every quest mod
// depends on it (packages.cpp).
inline const std::string kNotebookPackage = "story-notebook";

struct QuestDestination {
    std::string stage="sea";unsigned room=0;int point=-2;
    WorldPoint position;float sea_x=0,sea_z=0;
};
// Shared with the editor warp list. Interior chart coordinates must be explicit;
// a stage name alone does not uniquely identify a sea entrance.
inline bool parse_quest_destination(const mods::json::Value&v,QuestDestination&out,std::string&why){
    using namespace world_manifest;QuestDestination d;
    const bool interior=v.object.contains("stage")||v.object.contains("room");
    if(!interior){
        if(v.type!=Value::Object||!number(v.get("x"),-350000,350000,d.sea_x)||!number(v.get("z"),-350000,350000,d.sea_z))return why="quest place requires finite sea x/z",false;
        const int col=std::clamp(int(std::floor((d.sea_x+350000)/100000)),0,6),row=std::clamp(int(std::floor((d.sea_z+350000)/100000)),0,6);
        d.room=unsigned(row*7+col+1);d.point=-1;d.position={d.sea_x,2000,d.sea_z};
    }else{
        // "stage:<id>": one of the package's own stages (mod_stages.h), by its id in the manifest
        const std::string own=v.get("stage").string();
        if(own.rfind("stage:",0)==0){
            const std::string sid=own.substr(6);
            bool good=!sid.empty()&&sid.size()<=32&&integer(v.get("room"),0,63);
            for(char c:sid)good=good&&((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.');
            if(!good||!keys(v,{"stage","room","point","x","y","z","sea"}))return why="a place in the package's own stage is {\"stage\": \"stage:<id>\", \"room\": 0..63, \"sea\": {x, z}}",false;
            d.stage=own;d.room=unsigned(v.get("room").number);
        }else if(!keys(v,{"stage","room","point","x","y","z","sea"})||!place(v,d.stage,d.room)||d.stage=="sea")return why="interior place requires stage and room 0..63",false;
        const auto&sea=v.get("sea");
        if(!keys(sea,{"x","z"})||!number(sea.get("x"),-350000,350000,d.sea_x)||!number(sea.get("z"),-350000,350000,d.sea_z))return why="interior place requires a sea hint with finite x/z",false;
        const bool at=v.object.contains("x")||v.object.contains("y")||v.object.contains("z");
        if(at){if(v.object.contains("point")||!point(v,d.position))return why="interior place uses either point or all of x/y/z",false;d.point=-1;}
        else if(v.object.contains("point")){if(!integer(v.get("point"),0,255))return why="interior point must be 0..255",false;d.point=int(v.get("point").number);}
    }
    out=std::move(d);return true;
}
struct ManifestStep {
    unsigned n = 0;
    std::string text, where;
    bool has_place = false;
    float x = 0, z = 0;
    QuestDestination destination;
    bool has_talk = false;
    Resident resident = kResidentNone;
    bool needs_letter = false;
    std::string lines;   // joined with '\n'
    uint8_t reward = 0;  // a rupee item
    bool follow = true;  // npc "follow": false keeps the step's place fixed
    Availability when;   // "available": the step's prerequisites
    std::string sail_line;  // "sail_line": the King of Red Lions says it once (story_sail.h); '\n' between lines
    std::string give_chart;  // npc "then": {"give": "chart:<id>"}: the talk gives Link the mod's chart
    std::string give_item;   // npc "then": {"give": "item:<id>"}: the talk gives Link one of the mod's items
    std::string find_chart;  // "find": "chart:<id>": the chart's treasure; the chart is finished when the step is
};
struct ManifestQuest {
    std::string id, title, reward_text, start_text, journal = "on_enable";
    uint8_t reward_item = 0;
    bool has_letter = false, letter_encloses_letter = false;
    bool letter_encloses_notebook = false;  // "encloses": ["notebook"]: the letter brings the story notebook (the Story Notebook package's)
    std::string letter_id, letter_sender, letter_text;
    // the enclosed letter as the Delivery Bag describes it (A or a touch on it): its name and its
    // description ("encloses": [{"id": "letter:friend", "name": ..., "description": ...}]); empty:
    // the defaults (enclosure_name / enclosure_description)
    std::string enclosure_name, enclosure_description;
    std::vector<std::string> letter_charts;  // "encloses": ["chart:<id>"]: the letter gives Link these charts
    std::string reward_chart;                // "reward": {"chart": "chart:<id>"}: given when the quest is done
    std::vector<std::string> letter_items;   // "encloses": ["item:<id>"]: the letter gives Link these items (one each)
    std::string reward_mod_item;             // "reward": {"item": "item:<id>"}: one of the mod's items, given when the quest is done
    std::vector<ManifestStep> steps;
};
// A chart declared in the manifest ("charts": [...]): what the MAP screen's chart list shows for it
struct ManifestChart {
    std::string id, name, description;
    float x = 0, z = 0;
    // "item": what its treasure chest holds: "item:<id>" one of the mod's items (item_mod), or
    // "game:<name>" a game item (item_game, chart_game_item); none: an orange rupee
    std::string item_mod;
    uint8_t item_game = 0;
};
// The game items a mod chart's chest can hold ("game:<name>"): the rupees a salvage chest keeps as they
// are (green to red become purple there) and the heart pieces; nothing that sets the game's own bits
inline uint8_t chart_game_item(const std::string& name) {
    static const std::pair<const char*, uint8_t> kItems[] = {
        {"purple_rupee", 0x05}, {"orange_rupee", 0x06}, {"silver_rupee", 0x0F}, {"heart_piece", 0x07}, {"heart_container", 0x08}};
    for (const auto& [n, v] : kItems)
        if (name == n) return v;
    return 0;
}
inline constexpr uint8_t kChartDefaultItem = 0x06;  // an orange rupee
// "chart:<id>": the id, or empty
inline std::string chart_ref(const mods::json::Value& v) {
    if (v.type != mods::json::Value::String || v.text.rfind("chart:", 0) != 0) return {};
    return v.text.substr(6);
}
// "item:<id>": the id, or empty
inline std::string item_ref(const mods::json::Value& v) {
    if (v.type != mods::json::Value::String || v.text.rfind("item:", 0) != 0) return {};
    return v.text.substr(5);
}

// When a letter on its own is sent ("when"): every condition holds. A quest condition is met once the
// quest has reached its step or gone past it ("step"), or once it is done ("done": true).
struct LetterQuestCondition {
    std::string quest;
    unsigned step = 0;  // 1..254; 0 with done
    bool done = false;
    bool operator==(const LetterQuestCondition&) const = default;
};
struct LetterWhen {
    std::vector<LetterQuestCondition> quests;  // "quest" with "step" or "done", and "quests": [...]
    std::string flag, not_flag;                // the mod's own flags
    bool any() const { return !quests.empty() || !flag.empty() || !not_flag.empty(); }
    bool operator==(const LetterWhen&) const = default;
};
// A letter on its own ("letters": [...]): no quest, no journal entry. The port sends it while the mod
// is enabled (or once its "when" holds and "after_days" game days have passed since); once Link has
// read it, he has what it encloses (charts, items), once.
struct ManifestLetter {
    std::string id, sender, text;
    std::vector<std::string> charts, items;  // "encloses": ["chart:<id>", "item:<id>"]
    LetterWhen when;          // "when": nothing: sent at once
    unsigned after_days = 0;  // "after_days": 1..255 days of the game's calendar after "when" first holds
    bool timed() const { return when.any() || after_days != 0; }
};
inline constexpr unsigned kMaxLettersPerMod = 8, kMaxLetterDays = 255;
// The game's calendar: the day counter of the save file (dSv_player_status_b_c date, save base + 0x48),
// counted up each time the clock passes midnight (also by the Song of Passing).
inline constexpr uint32_t kSaveDay = 0x48;

// A timed letter's condition: `quest(id)` the quest's saved step (0 not started, 1..254, 255 done),
// `flag(id)` one of the mod's flags.
template <class Quest, class Flag>
inline bool letter_condition(const LetterWhen& w, Quest&& quest, Flag&& flag) {
    for (const LetterQuestCondition& q : w.quests) {
        const int32_t now = quest(q.quest);
        if (q.done ? now != 255 : (now <= 0 || unsigned(now) < q.step)) return false;
    }
    if (!w.flag.empty() && !flag(w.flag)) return false;
    if (!w.not_flag.empty() && flag(w.not_flag)) return false;
    return true;
}
// Whether a timed letter whose condition holds is due on `today` (the game's day counter). The first
// day its condition holds is kept in the save's story record (kRecordLetterDayLow/High, key the
// letter's key), so the count goes on across saves and save states; erase_letter_day frees it once
// the letter is sent. Without "after_days" the letter is due at once.
inline bool letter_due(story_mail::Book& b, uint16_t key, unsigned after_days, uint16_t today) {
    if (!after_days) return true;
    if (!b.has(story_mail::kRecordLetterDayLow, key)) {
        if (b.set(story_mail::kRecordLetterDayLow, key, uint8_t(today)) && !b.set(story_mail::kRecordLetterDayHigh, key, uint8_t(today >> 8)))
            b.erase(story_mail::kRecordLetterDayLow, key);  // no room for both: next time
        return false;
    }
    const uint16_t start = uint16_t(b.get(story_mail::kRecordLetterDayHigh, key) << 8 | b.get(story_mail::kRecordLetterDayLow, key));
    return uint16_t(today - start) >= after_days;
}
inline void erase_letter_day(story_mail::Book& b, uint16_t key) {
    b.erase(story_mail::kRecordLetterDayLow, key);
    b.erase(story_mail::kRecordLetterDayHigh, key);
}

// An item declared in the manifest ("items": [...], docs/mod-sdk-v2.md "Items"): kept in the Delivery
// Bag (its extra pages), owned in the save's story block (story_items_core.h)
struct ManifestItem {
    enum class Use : uint8_t { None, Show, OpenNotebook, QuestStep, Event };
    std::string id, name, description, icon;  // icon: a PNG in the package, relative to it
    uint8_t stack = 1;                        // how many Link can carry, 1..254
    Use use = Use::None;                      // what using it (on X, Y or R) does; None: it can't go on a button
    std::string use_quest;                    // Use::QuestStep: the mod's quest ...
    unsigned use_step = 0;                    // ... reaches this step
    std::string held_model; // same-mod static models entry, separate hand presentation
    unsigned held_frames = 90; // logic frames at30Hz, 1..300
    bool hold_up = false;      // "hold_up": true: getting it plays the game's item-get pose with held_model over Link's head
};
inline constexpr size_t kMaxItemNameBytes = 40, kMaxItemDescriptionBytes = 200, kMaxItemIconBytes = 128;
inline constexpr unsigned kMaxItemsPerMod = 32;
// Parses the manifest's items ("items": [{"id", "name", "description", "icon", "stack", "use": {"action", ...}}]).
inline bool parse_items(const mods::json::Value& manifest, std::vector<ManifestItem>& out, std::string& why) {
    using mods::json::Value;
    out.clear();
    const Value& items = manifest.get("items");
    if (items.type == Value::Null) return true;
    if (items.type != Value::Array || items.array.size() > kMaxItemsPerMod) return why = "items must be a list of up to 32", false;
    for (const Value& it : items.array) {
        ManifestItem m;
        if (it.type != Value::Object || it.get("id").type != Value::String || !story_mail::valid_id(it.get("id").text))
            return why = "invalid item id", false;
        m.id = it.get("id").text;
        std::u16string ignored;
        auto text = [&](const Value& v, size_t max, bool lines, std::string& to) {
            if (v.type != Value::String || v.text.empty() || v.text.size() > max || !story_mail::append_utf16(v.text, lines, ignored)) return false;
            to = v.text;
            return true;
        };
        if (!text(it.get("name"), kMaxItemNameBytes, false, m.name)) return why = "item " + m.id + ": a name of up to 40 bytes is needed", false;
        if (!text(it.get("description"), kMaxItemDescriptionBytes, true, m.description))
            return why = "item " + m.id + ": a description of up to 200 bytes is needed", false;
        const Value& icon = it.get("icon");
        if (icon.type != Value::String || icon.text.empty() || icon.text.size() > kMaxItemIconBytes || icon.text.find("..") != std::string::npos ||
            icon.text[0] == '/' || icon.text.find('\\') != std::string::npos || icon.text.size() < 5 ||
            icon.text.compare(icon.text.size() - 4, 4, ".png") != 0)
            return why = "item " + m.id + ": \"icon\" must name a PNG in the package", false;
        m.icon = icon.text;
        const Value& stack = it.get("stack");
        if (stack.type != Value::Null) {
            if (stack.type != Value::Number || stack.number < 1 || stack.number > 254 || std::floor(stack.number) != stack.number)
                return why = "item " + m.id + ": \"stack\" must be 1..254", false;
            m.stack = uint8_t(stack.number);
        }
        const Value& use = it.get("use");
        if (use.type == Value::Object) {
            const std::string action = use.get("action").type == Value::String ? use.get("action").text : "";
            if (action == "show") m.use = ManifestItem::Use::Show;
            else if (action == "open_notebook") m.use = ManifestItem::Use::OpenNotebook;
            else if (action == "event") m.use = ManifestItem::Use::Event;
            else if (action == "quest_step") {
                m.use = ManifestItem::Use::QuestStep;
                const Value& step = use.get("step");
                if (use.get("quest").type != Value::String || !story_mail::valid_id(use.get("quest").text) || step.type != Value::Number ||
                    step.number < 1 || step.number > 254 || std::floor(step.number) != step.number)
                    return why = "item " + m.id + ": \"quest_step\" needs \"quest\" and \"step\"", false;
                m.use_quest = use.get("quest").text, m.use_step = unsigned(step.number);
            } else
                return why = "item " + m.id + ": unknown use action (show, open_notebook, quest_step, event)", false;
        } else if (use.type != Value::Null)
            return why = "item " + m.id + ": \"use\" must be an object", false;
        const Value& hold=it.get("hold_up");
        if(hold.type!=Value::Null){
            if(hold.type!=Value::Bool)return why="item "+m.id+": \"hold_up\" must be true or false",false;
            m.hold_up=hold.boolean;
        }
        if(it.get("held_model").type!=Value::Null) {
            if(it.get("held_model").type!=Value::String||!story_mail::valid_id(it.get("held_model").text)||(m.use==ManifestItem::Use::None&&!m.hold_up))
                return why="held_model requires a valid same-mod model id and a use action (or \"hold_up\": true)",false;
            m.held_model=it.get("held_model").text;
        }
        if(m.hold_up&&m.held_model.empty())return why="item "+m.id+": \"hold_up\" needs \"held_model\", the model Link holds up",false;
        if(it.get("held_frames").type!=Value::Null) {
            const auto& n=it.get("held_frames");
            if(m.held_model.empty()||n.type!=Value::Number||n.number<1||n.number>300||std::floor(n.number)!=n.number)
                return why="held_frames requires held_model and integer1..300",false;
            m.held_frames=unsigned(n.number);
        }
        for (const ManifestItem& o : out)
            if (o.id == m.id) return why = "item " + m.id + " is declared twice", false;
        out.push_back(std::move(m));
    }
    return true;
}
// Parses the manifest's charts ("charts": [{"id", "name", "description", "place": {"x", "z"}}]).
inline bool parse_charts(const mods::json::Value& manifest, std::vector<ManifestChart>& out, std::string& why) {
    using mods::json::Value;
    out.clear();
    const Value& charts = manifest.get("charts");
    if (charts.type == Value::Null) return true;
    if (charts.type != Value::Array || charts.array.size() > 8) return why = "charts must be a list of up to 8", false;
    for (const Value& c : charts.array) {
        ManifestChart m;
        std::u16string ignored;
        if (c.type != Value::Object || c.get("id").type != Value::String || !story_mail::valid_id(c.get("id").text))
            return why = "invalid chart id", false;
        m.id = c.get("id").text;
        if (c.get("name").type != Value::String || c.get("name").text.empty() || c.get("name").text.size() > 40 ||
            !story_mail::append_utf16(c.get("name").text, false, ignored))
            return why = "chart " + m.id + ": a name of up to 40 bytes is needed", false;
        m.name = c.get("name").text;
        if (c.get("description").type != Value::Null) {
            if (c.get("description").type != Value::String || c.get("description").text.size() > 200 ||
                !story_mail::append_utf16(c.get("description").text, true, ignored))
                return why = "chart " + m.id + ": invalid description", false;
            m.description = c.get("description").text;
        }
        const Value& place = c.get("place");
        if (place.type != Value::Object || place.get("x").type != Value::Number || place.get("z").type != Value::Number)
            return why = "chart " + m.id + ": the place of its mark is needed", false;
        m.x = float(place.get("x").number), m.z = float(place.get("z").number);
        if (!(std::fabs(m.x) <= 350000 && std::fabs(m.z) <= 350000)) return why = "chart " + m.id + ": place off the sea", false;
        const Value& item = c.get("item");
        if (item.type != Value::Null) {
            if (!(m.item_mod = item_ref(item)).empty()) {
                std::vector<ManifestItem> items;
                if (!parse_items(manifest, items, why)) return false;
                bool found = false;
                for (const ManifestItem& i : items) found |= i.id == m.item_mod;
                if (!found) return why = "chart " + m.id + ": item \"" + m.item_mod + "\" is not declared in \"items\"", false;
            } else if (item.type != Value::String || item.text.rfind("game:", 0) != 0 || !(m.item_game = chart_game_item(item.text.substr(5))))
                return why = "chart " + m.id + ": \"item\" must be \"item:<id>\" or one of game:purple_rupee, game:orange_rupee, "
                             "game:silver_rupee, game:heart_piece, game:heart_container", false;
        }
        for (const ManifestChart& o : out)
            if (o.id == m.id) return why = "chart " + m.id + " is declared twice", false;
        out.push_back(m);
    }
    return true;
}
inline constexpr size_t kMaxEnclosureNameBytes = 40, kMaxEnclosureDescriptionBytes = 200;
// What the Delivery Bag says about a quest's sealed letter
inline std::string enclosure_name(const ManifestQuest& q) { return q.enclosure_name.empty() ? "Sealed Letter" : q.enclosure_name; }
inline std::string enclosure_description(const ManifestQuest& q) {
    if (!q.enclosure_description.empty()) return q.enclosure_description;
    return q.letter_sender.empty() ? "A sealed letter. It isn't addressed to you." : "A sealed letter from " + q.letter_sender + ". It isn't addressed to you.";
}

// "rupee_green" .. "rupee_orange"; 0 if not one
inline uint8_t rupee_item(const std::string& name) {
    static const char* const kNames[] = {"rupee_green", "rupee_blue", "rupee_yellow", "rupee_red", "rupee_purple", "rupee_orange"};
    for (uint8_t i = 0; i < 6; i++)
        if (name == kNames[i]) return uint8_t(i + 1);
    return 0;
}
// an event flag written as "0x2D01" (the game's flag number); false if not one
inline bool event_number(const mods::json::Value& v, uint16_t& out) {
    if (v.type != mods::json::Value::String || v.text.size() < 3 || v.text.size() > 6 || v.text.rfind("0x", 0) != 0) return false;
    char* end = nullptr;
    const unsigned long n = strtoul(v.text.c_str() + 2, &end, 16);
    if (*end || n > 0xFFFF || !(n & 0xFF)) return false;
    out = uint16_t(n);
    return true;
}
inline bool parse_available(const mods::json::Value& v, Availability& a, std::string& why) {
    using mods::json::Value;
    if (v.type == Value::Null) return true;
    if (v.type != Value::Object) return why = "available must be an object", false;
    for (const char* key : {"event", "not_event"})
        for (const Value& e : v.get(key).type == Value::Array ? v.get(key).array : std::vector<Value>{v.get(key)}) {
            if (e.type == Value::Null) continue;
            uint16_t n;
            if (!event_number(e, n)) return why = std::string("invalid ") + key + " flag", false;
            (std::string(key) == "event" ? a.events : a.not_events).push_back(n);
        }
    for(const char* key:{"flag","not_flag"}){
        const auto& value=v.get(key);if(value.type==Value::Null)continue;
        const auto entries=value.type==Value::Array?value.array:std::vector<Value>{value};
        if(entries.empty()||entries.size()>8)return why="flag conditions need 1..8 flag ids",false;
        for(const auto&entry:entries){if(!world_manifest::flag_id(entry))return why="invalid mod-owned flag condition",false;
            (std::string(key)=="flag"?a.flags:a.not_flags).push_back(entry.text);}
    }
    if (v.get("time").type == Value::String) {
        const std::string& t = v.get("time").text;
        if (t == "day") a.time = 1;
        else if (t == "night") a.time = 2;
        else if (t != "any") return why = "time must be day, night or any", false;
    }
    std::u16string ignored;
    if (v.get("text").type != Value::String || v.get("text").text.empty() || v.get("text").text.size() > kMaxConditionBytes ||
        !story_mail::append_utf16(v.get("text").text, false, ignored))
        return why = "available needs a text for the player (up to 96 bytes)", false;
    a.text = v.get("text").text;
    return true;
}

// Parses the manifest's quests. Returns false with a reason for anything it doesn't accept (the mod's
// quests are then not in the notebook; the mod itself still loads).
// NPCs are named from the catalogue ("who": "windfall-um1"); a name it doesn't know, or an
// "npc_catalogue" version newer than it, refuses the quests (and the mod, packages.cpp). Steps whose
// time of day never meets their resident's are allowed, with a warning.
inline bool parse_quests(const mods::json::Value& manifest, std::vector<ManifestQuest>& out, std::string& why,
                         const NpcCatalogue& catalogue = npc_catalogue(), std::vector<std::string>* warnings = nullptr) {
    using mods::json::Value;
    out.clear();
    const Value& needs = manifest.get("npc_catalogue");
    if (needs.type != Value::Null) {
        if (needs.type != Value::Number || needs.number < 1 || std::floor(needs.number) != needs.number)
            return why = "npc_catalogue must be a version number", false;
        if (needs.number > catalogue.version)
            return why = "needs NPC catalogue version " + std::to_string(unsigned(needs.number)) + "; this port has version " +
                         std::to_string(catalogue.version) + " (update the port)",
                   false;
    }
    const Value& quests = manifest.get("quests");
    if (quests.type == Value::Null) return true;
    const Value& format = manifest.get("quests_format");
    if (format.type != Value::Number || format.number != 1) return why = "unsupported quests_format", false;
    if (quests.type != Value::Array || quests.array.size() > kMaxQuestsPerMod) return why = "quests must be a list of up to 8", false;
    auto text = [&](const Value& v, size_t max, bool lines, std::string& to, const char* what) {
        if (v.type == Value::Null) return true;
        std::u16string ignored;
        if (v.type != Value::String || v.text.size() > max || !story_mail::append_utf16(v.text, lines, ignored))
            return why = std::string("invalid ") + what, false;
        to = v.text;
        return true;
    };
    for (const Value& q : quests.array) {
        ManifestQuest m;
        if (q.type != Value::Object) return why = "a quest must be an object", false;
        if (q.get("id").type != Value::String || !story_mail::valid_id(q.get("id").text)) return why = "invalid quest id", false;
        m.id = q.get("id").text;
        if (!text(q.get("title"), kMaxTitleBytes, false, m.title, "title") || m.title.empty()) return why = "quest " + m.id + ": invalid title", false;
        if (q.get("journal").type == Value::String) {
            m.journal = q.get("journal").text;
            if (m.journal != "on_enable" && m.journal != "on_trigger" && m.journal != "hidden") return why = "quest " + m.id + ": invalid journal", false;
        }
        if (!text(q.get("start").get("text"), kMaxNextBytes, false, m.start_text, "start text")) return false;
        if (!text(q.get("reward").get("text"), kMaxRewardBytes, false, m.reward_text, "reward text")) return false;
        if (const Value& ri = q.get("reward").get("item"); ri.type == Value::String) {
            if (!(m.reward_mod_item = item_ref(ri)).empty()) {
            } else if (!(m.reward_item = rupee_item(ri.text)))
                return why = "quest " + m.id + ": unsupported reward item", false;
        }
        if (q.get("reward").get("chart").type != Value::Null && (m.reward_chart = chart_ref(q.get("reward").get("chart"))).empty())
            return why = "quest " + m.id + ": the reward chart must be \"chart:<id>\"", false;
        const Value& letter = q.get("letter");
        if (letter.type == Value::Object) {
            m.has_letter = true;
            if (letter.get("id").type != Value::String || !story_mail::valid_id(letter.get("id").text)) return why = "invalid letter id", false;
            m.letter_id = letter.get("id").text;
            if (!text(letter.get("sender"), story_mail::kMaxSenderBytes, false, m.letter_sender, "letter sender") ||
                !text(letter.get("text"), story_mail::kMaxTextBytes, true, m.letter_text, "letter text") || m.letter_sender.empty() ||
                m.letter_text.empty())
                return why = "quest " + m.id + ": invalid letter", false;
            for (const Value& e : letter.get("encloses").array) {
                if (e.type == Value::String && e.text.rfind("letter:", 0) == 0) m.letter_encloses_letter = true;
                if (e.type == Value::String && e.text == "notebook") m.letter_encloses_notebook = true;
                if (const std::string chart = chart_ref(e); !chart.empty()) m.letter_charts.push_back(chart);
                if (const std::string item = item_ref(e); !item.empty()) m.letter_items.push_back(item);
                // the object form: the enclosure with what the Delivery Bag says about it
                if (e.type == Value::Object && e.get("id").type == Value::String && e.get("id").text.rfind("letter:", 0) == 0) {
                    m.letter_encloses_letter = true;
                    if (!text(e.get("name"), kMaxEnclosureNameBytes, false, m.enclosure_name, "enclosure name") ||
                        !text(e.get("description"), kMaxEnclosureDescriptionBytes, true, m.enclosure_description, "enclosure description"))
                        return false;
                }
            }
        }
        for (const Value& st : q.get("steps").array) {
            ManifestStep s;
            if (st.get("n").type != Value::Number || st.get("n").number < 1 || st.get("n").number > kMaxStep || std::floor(st.get("n").number) != st.get("n").number)
                return why = "quest " + m.id + ": invalid step number", false;
            s.n = unsigned(st.get("n").number);
            if (!text(st.get("text"), kMaxNextBytes, false, s.text, "step text") || !text(st.get("where"), kMaxWhereBytes, false, s.where, "where"))
                return false;
            const Value& place = st.get("place");
            if(place.type!=Value::Null){
                if(!parse_quest_destination(place,s.destination,why))return why="quest "+m.id+": "+why,false;
                s.x=s.destination.sea_x;s.z=s.destination.sea_z;s.has_place=true;
            }
            if (!parse_available(st.get("available"), s.when, why)) return why = "quest " + m.id + ": " + why, false;
            for(const auto* flags:{&s.when.flags,&s.when.not_flags})for(const auto&flag:*flags)
                if(!world_manifest::known_flag(manifest,flag))return why="quest "+m.id+": flag condition requires a same-manifest flag producer",false;
            if (st.get("find").type != Value::Null && (s.find_chart = chart_ref(st.get("find"))).empty())
                return why = "quest " + m.id + ": \"find\" must be \"chart:<id>\"", false;
            // "sail_line": a string, or up to three strings (one message box)
            const Value& sail = st.get("sail_line");
            if (sail.type != Value::Null) {
                const std::vector<Value> parts = sail.type == Value::Array ? sail.array : std::vector<Value>{sail};
                for (const Value& l : parts) {
                    if (l.type != Value::String) return why = "quest " + m.id + ": invalid sail_line", false;
                    if (!s.sail_line.empty()) s.sail_line += '\n';
                    s.sail_line += l.text;
                }
                size_t breaks = 0;
                for (char ch : s.sail_line) breaks += ch == '\n';
                std::u16string ignored;
                if (s.sail_line.empty() || s.sail_line.size() > kMaxSailLineBytes || breaks > 2 ||
                    !story_mail::append_utf16(s.sail_line, true, ignored))
                    return why = "quest " + m.id + ": the sail_line must be up to three lines, 240 bytes", false;
            }
            const Value& npc = st.get("npc");
            if (npc.type == Value::Object) {
                s.has_talk = true;
                const Value& who = npc.get("who");
                if (who.type != Value::String) return why = "quest " + m.id + ": \"who\" must name an NPC from the catalogue", false;
                s.resident = catalogue.find(who.text);
                if (s.resident == kResidentNone)
                    return why = "quest " + m.id + ": this port's NPC catalogue (version " + std::to_string(catalogue.version) +
                                 ") has no NPC named \"" + who.text + "\"",
                           false;
                const NpcEntry& e = *catalogue.entry(s.resident);
                if (!e.talks())
                    return why = "quest " + m.id + ": " + e.name + " can't speak a quest's lines in this port yet (only townspeople of class people can)",
                           false;
                if (s.where.empty()) s.where = e.where;
                if (!s.has_place && e.has_spot) s.has_place = true, s.x = e.x, s.z = e.z;
                if (warnings && ((s.when.time == 1 && !e.day) || (s.when.time == 2 && !e.night)))
                    warnings->push_back("quest " + m.id + " step " + std::to_string(s.n) + ": " + e.name + " is only out " +
                                        (e.day ? "by day" : "at night") + ", but the step is available only " +
                                        (s.when.time == 1 ? "by day" : "at night"));
                s.needs_letter = npc.get("when").get("carries").type == Value::String;
                for (const Value& l : npc.get("lines").array) {
                    if (l.type != Value::String) return why = "invalid line", false;
                    if (!s.lines.empty()) s.lines += '\n';
                    s.lines += l.text;
                }
                std::u16string ignored;
                if (s.lines.empty() || s.lines.size() > kMaxLinesBytes || npc.get("lines").array.size() > 3 ||
                    !story_mail::append_utf16(s.lines, true, ignored))
                    return why = "quest " + m.id + ": the lines must be up to three, 240 bytes", false;
                if (npc.get("follow").type == Value::Bool) s.follow = npc.get("follow").boolean;
                else if (npc.get("follow").type != Value::Null) return why = "quest " + m.id + ": follow must be true or false", false;
                const Value& give = npc.get("then").get("give");
                if (give.type == Value::String && !(s.give_chart = chart_ref(give)).empty()) {
                } else if (give.type == Value::String && !(s.give_item = item_ref(give)).empty()) {
                } else if (give.type == Value::String && !(s.reward = rupee_item(give.text)))
                    return why = "quest " + m.id + ": unsupported item to give", false;
            }
            m.steps.push_back(s);
        }
        out.push_back(std::move(m));
    }
    // the charts the quests name must be declared
    std::vector<ManifestChart> charts;
    if (!parse_charts(manifest, charts, why)) return false;
    auto declared = [&](const std::string& id) {
        for (const ManifestChart& c : charts)
            if (c.id == id) return true;
        return false;
    };
    for (const ManifestQuest& q : out) {
        std::vector<std::string> refs = q.letter_charts;
        if (!q.reward_chart.empty()) refs.push_back(q.reward_chart);
        for (const ManifestStep& st : q.steps) {
            if (!st.give_chart.empty()) refs.push_back(st.give_chart);
            if (!st.find_chart.empty()) refs.push_back(st.find_chart);
        }
        for (const std::string& r : refs)
            if (!declared(r)) return why = "quest " + q.id + ": chart \"" + r + "\" is not declared in \"charts\"", false;
    }
    // a quest that is nothing but one chart to find is a treasure chart: a letter can bring the chart
    // ("letters"), with no journal entry (a chart step inside a longer quest is fine)
    if (warnings)
        for (const ManifestQuest& q : out)
            if (q.steps.size() == 1 && !q.steps[0].find_chart.empty() && !q.steps[0].has_talk)
                warnings->push_back("quest " + q.id + " is only one chart to find (\"" + q.steps[0].find_chart +
                                    "\"): a treasure chart needs no quest; send the chart in a letter (\"letters\") instead");
    // so must the items they name, and an item's quest_step names one of the mod's quests
    std::vector<ManifestItem> items;
    if (!parse_items(manifest, items, why)) return false;
    auto item_declared = [&](const std::string& id) {
        for (const ManifestItem& i : items)
            if (i.id == id) return true;
        return false;
    };
    for (const ManifestQuest& q : out) {
        std::vector<std::string> refs = q.letter_items;
        if (!q.reward_mod_item.empty()) refs.push_back(q.reward_mod_item);
        for (const ManifestStep& st : q.steps)
            if (!st.give_item.empty()) refs.push_back(st.give_item);
        for (const std::string& r : refs)
            if (!item_declared(r)) return why = "quest " + q.id + ": item \"" + r + "\" is not declared in \"items\"", false;
    }
    for (const ManifestItem& i : items) {
        if (i.use != ManifestItem::Use::QuestStep) continue;
        bool found = false;
        for (const ManifestQuest& q : out) found |= q.id == i.use_quest;
        if (!found) return why = "item " + i.id + ": its use names quest \"" + i.use_quest + "\", which the manifest doesn't declare", false;
    }
    return true;
}

// A letter's "when" and "after_days" (ManifestLetter). The quests, steps and flags it names are the
// manifest's own.
inline bool parse_letter_when(const mods::json::Value& manifest, const mods::json::Value& l, ManifestLetter& m, std::string& why) {
    using mods::json::Value;
    using namespace world_manifest;
    const Value& days = l.get("after_days");
    if (days.type != Value::Null) {
        if (!integer(days, 1, kMaxLetterDays)) return why = "\"after_days\" must be a whole number of days, 1..255", false;
        m.after_days = unsigned(days.number);
    }
    const Value& w = l.get("when");
    if (w.type == Value::Null) return true;
    if (!keys(w, {"quest", "step", "done", "quests", "flag", "not_flag"}) || w.object.empty())
        return why = "\"when\" is an object with quest and step (or done), quests, flag or not_flag", false;
    auto quest_condition = [&](const Value& q, LetterQuestCondition& out) {
        if (!id(q.get("quest")) || !declared(manifest, "quests", q.get("quest").text))
            return why = "\"when\" names a quest the manifest doesn't declare", false;
        out.quest = q.get("quest").text;
        const bool step = q.get("step").type != Value::Null, done = q.get("done").type != Value::Null;
        if (step == done) return why = "quest " + out.quest + " in \"when\": give \"step\" or \"done\": true", false;
        if (done) {
            if (q.get("done").type != Value::Bool || !q.get("done").boolean) return why = "\"done\" must be true", false;
            out.done = true;
            return true;
        }
        if (!integer(q.get("step"), 1, 254) || !quest_step(manifest, out.quest, unsigned(q.get("step").number)))
            return why = "\"when\" names a step quest " + out.quest + " doesn't declare", false;
        out.step = unsigned(q.get("step").number);
        return true;
    };
    if (w.get("quest").type != Value::Null) {
        LetterQuestCondition q;
        if (!quest_condition(w, q)) return false;
        m.when.quests.push_back(q);
    } else if (w.get("step").type != Value::Null || w.get("done").type != Value::Null)
        return why = "\"step\" and \"done\" in \"when\" need \"quest\"", false;
    const Value& list = w.get("quests");
    if (list.type != Value::Null) {
        if (list.type != Value::Array || list.array.empty() || list.array.size() > 8) return why = "\"quests\" in \"when\" lists 1..8 quests", false;
        for (const Value& q : list.array) {
            LetterQuestCondition c;
            if (!keys(q, {"quest", "step", "done"})) return why = "each of \"quests\" is {quest, step} or {quest, done: true}", false;
            if (!quest_condition(q, c)) return false;
            m.when.quests.push_back(c);
        }
    }
    if (!read_flag(manifest, w.get("flag"), m.when.flag, why) || !read_flag(manifest, w.get("not_flag"), m.when.not_flag, why)) return false;
    return true;
}

// Parses the manifest's letters on their own ("letters": [{"id", "sender", "text", "encloses"}]); the
// charts and items they enclose must be declared, and their ids must not be a quest letter's.
inline bool parse_letters(const mods::json::Value& manifest, std::vector<ManifestLetter>& out, std::string& why) {
    using mods::json::Value;
    out.clear();
    const Value& letters = manifest.get("letters");
    if (letters.type == Value::Null) return true;
    if (letters.type != Value::Array || letters.array.size() > kMaxLettersPerMod) return why = "letters must be a list of up to 8", false;
    std::vector<ManifestChart> charts;
    std::vector<ManifestItem> items;
    if (!parse_charts(manifest, charts, why) || !parse_items(manifest, items, why)) return false;
    std::vector<std::string> quest_letters;
    for (const Value& q : manifest.get("quests").array)
        if (q.get("letter").get("id").type == Value::String) quest_letters.push_back(q.get("letter").get("id").text);
    for (const Value& l : letters.array) {
        ManifestLetter m;
        if (l.type != Value::Object || l.get("id").type != Value::String || !story_mail::valid_id(l.get("id").text))
            return why = "invalid letter id", false;
        m.id = l.get("id").text;
        std::u16string ignored;
        if (l.get("sender").type != Value::String || l.get("text").type != Value::String) return why = "letter " + m.id + ": sender and text are needed", false;
        m.sender = l.get("sender").text, m.text = l.get("text").text;
        if (story_mail::compose(m.sender, m.text).empty()) return why = "letter " + m.id + ": invalid sender or text", false;
        for (const Value& e : l.get("encloses").array) {
            if (const std::string c = chart_ref(e); !c.empty()) {
                bool found = false;
                for (const ManifestChart& d : charts) found |= d.id == c;
                if (!found) return why = "letter " + m.id + ": chart \"" + c + "\" is not declared in \"charts\"", false;
                m.charts.push_back(c);
            } else if (const std::string i = item_ref(e); !i.empty()) {
                bool found = false;
                for (const ManifestItem& d : items) found |= d.id == i;
                if (!found) return why = "letter " + m.id + ": item \"" + i + "\" is not declared in \"items\"", false;
                m.items.push_back(i);
            } else
                return why = "letter " + m.id + ": it can enclose \"chart:<id>\" and \"item:<id>\"", false;
        }
        if (!parse_letter_when(manifest, l, m, why)) return why = "letter " + m.id + ": " + why, false;
        for (const std::string& q : quest_letters)
            if (q == m.id) return why = "letter " + m.id + " is also a quest's letter", false;
        for (const ManifestLetter& o : out)
            if (o.id == m.id) return why = "letter " + m.id + " is declared twice", false;
        out.push_back(std::move(m));
    }
    return true;
}

}  // namespace story_notebook
