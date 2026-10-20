// Mod items (runtime/src/mods/story_items*.{h,cpp}): the save record (Bag), the manifest's "items",
// the registry, the services, the bag actions and the game's item table hooks for the pool numbers,
// on a synthetic save. The game's own functions are stubbed; no game files are needed.
#include "mods/story_items.h"
#include "mods/story_items_core.h"
#include "mods/story_manifest.h"
#include "mods/story_store.h"
#include "runtime.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

using namespace story_items;

void log_msg(const char* fmt, ...) {
    (void)fmt;
}

namespace {

constexpr uint32_t kGlobals = 0x101F0000, kArena = 0x20000000, kSave = kArena;

void* map(uint32_t at, uint32_t size) {
#ifdef _WIN32
    return VirtualAlloc(mem::ptr(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    return mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#endif
}

std::vector<std::string> calls;
void original(Cpu* c, uint32_t fn) {
    calls.push_back(fn == 0x0254DA38 ? "exec" : fn == 0x0254DA50 ? "check" : "other");
    if (fn == 0x0254DA50) c->r[3] = 7;  // the game's own answer
}
uint32_t game_call(Cpu*, uint32_t fn, std::initializer_list<uint32_t> args) {
    if (fn == 0x025E1988) calls.push_back("jingle " + std::to_string(*args.begin()));
    return 0;
}
std::vector<std::string> shown;
bool shown_ok = true;
bool show(const std::string& title, const std::string& text, const char*) {
    if (!shown_ok) return false;
    shown.push_back(title + "|" + text);
    return true;
}
std::vector<std::string> reached;
int32_t reach(const std::string& mod, const std::string& quest, uint32_t step) {
    reached.push_back(mod + ":" + quest + ":" + std::to_string(step));
    return int32_t(step);
}
bool notebook_opened = false;
bool open_notebook(const char*) { return notebook_opened = true; }

void clear_save() {
    std::memset(mem::ptr(kArena + 0xE000), 0, story_store::kMemoryBytes);
    for (unsigned i = 0; i < 8; i++) st8(kSave + 0xA6 + i, 0xFF);  // the game's delivery bag: empty
}

void test_bag() {
    clear_save();
    Bag b(story_store::book());
    const uint16_t a = item_key("mod", "a"), k = item_key("mod", "key");
    assert(a && k && a != k);
    bool placed = false;
    assert(b.give(a, 1, 1, placed) == 1 && placed && b.place(a) == kFirstPagePlace);
    assert(b.give(a, 1, 1, placed) == 1);  // a single item: still one
    assert(b.give(k, 5, 3, placed) == 3 && b.place(k) == kFirstPagePlace + 1);  // stack 3
    assert(b.take(k, 4) == -1 && b.take(k, 1) == 2);
    // a pool number: bound once, kept; the game's places and buttons hold theirs
    const uint8_t n = b.bind(a);
    assert(pool_number(n) && b.bind(a) == n && b.with_number(n) == a);
    assert(b.set_button(a, 1, true) && b.on_button(1) == a && b.buttons(a) == 2);
    assert(b.set_button(k, 1, true) && b.on_button(1) == k && !b.buttons(a));  // Y goes to the key
    // the last one taken: every record of the item goes
    assert(b.take(a, 1) == 0 && !b.count(a) && !b.place(a) && !b.number(a) && !b.with_number(n));
    // the pages fill up: the 33rd item is kept without a place
    clear_save();
    for (unsigned i = 0; i < kPages * kPageSlots; i++) {
        assert(b.give(item_key("mod", "i" + std::to_string(i)), 1, 1, placed) == 1 && placed);
    }
    assert(b.give(item_key("mod", "extra"), 1, 1, placed) == 1 && !placed && !b.place(item_key("mod", "extra")));
    assert(b.unplaced() == std::vector<uint16_t>{item_key("mod", "extra")});
    Bag two(story_store::book(), 2);  // a lower limit: no free place, the items stay where they are
    assert(!two.free_page_place() && b.place(item_key("mod", "i31")) == kFirstPagePlace + 31);
    // moving: only to a free page place, or a game place the game's save has empty
    const uint16_t i0 = item_key("mod", "i0");
    assert(!b.move(i0, kFirstPagePlace + 1, false));  // taken
    assert(!b.move(i0, 3, false) && b.move(i0, 3, true) && b.place(i0) == 3 && b.at(3) == i0);
    // the pool runs out: numbers of items with no game place and no button are taken back
    clear_save();
    std::vector<uint16_t> keys;
    for (unsigned i = 0; i < kPool.size() + 1; i++) {
        keys.push_back(item_key("mod", "p" + std::to_string(i)));
        b.give(keys.back(), 1, 1, placed);
    }
    for (unsigned i = 0; i < kPool.size(); i++) assert(b.bind(keys[i]));
    assert(b.set_button(keys[0], 0, true));          // on X: keeps its number
    assert(b.move(keys[1], 1, true));                // in a game place: keeps its number
    const uint8_t taken = b.bind(keys.back());       // the 20th: a number of another item
    assert(taken && taken != b.number(keys[0]) && taken != b.number(keys[1]) && b.with_number(taken) == keys.back());
    // every number in use (buttons and places): none
    clear_save();
    keys.clear();
    for (unsigned i = 0; i <= kPool.size(); i++) {
        keys.push_back(item_key("mod", "q" + std::to_string(i)));
        b.give(keys.back(), 1, 1, placed);
    }
    for (unsigned i = 0; i < kPool.size(); i++) assert(b.bind(keys[i]));
    // the game shows every one of them right now (the in_use callback): no number for the 20th
    assert(!b.bind(keys.back(), [](uint8_t) { return true; }));
}

void test_pool() {
    // the audited numbers: none of the game's item numbers with handling (bottles, bait, the delivery
    // bag, the HD additions) and not the port's own 0xA0 / 0xA1
    for (uint8_t n : {0x13, 0x14, 0x2B, 0x2E, 0x4F, 0x5A, 0x60, 0x77, 0x89, 0xA0, 0xA1, 0xA2, 0xB9, 0xC1, 0x01, 0xFF}) assert(!pool_number(n));
    for (uint8_t n : kPool) assert(pool_number(n));
}

void test_manifest() {
    using story_notebook::ManifestItem;
    std::vector<ManifestItem> items;
    std::string why;
    auto parse = [&](const std::string& text) { return story_notebook::parse_items(mods::json::parse(text), items, why); };
    assert(parse(R"({"items": [{"id": "old-key", "name": "Old key", "description": "Rusty.", "icon": "assets/key.png"},
                               {"id": "lamp", "name": "Lamp", "description": "Bright.\nWarm.", "icon": "assets/lamp.png", "stack": 3,
                                "use": {"action": "show"}}]})"));
    assert(items.size() == 2 && items[0].stack == 1 && items[0].use == ManifestItem::Use::None && items[1].stack == 3 &&
           items[1].use == ManifestItem::Use::Show);
    assert(parse(R"({"items": [{"id": "n", "name": "N", "description": "D", "icon": "a.png", "use": {"action": "quest_step", "quest": "q", "step": 2}}]})"));
    assert(items[0].use == ManifestItem::Use::QuestStep && items[0].use_quest == "q" && items[0].use_step == 2);
    assert(!parse(R"({"items": [{"id": "x", "name": "X", "description": "D", "icon": "../a.png"}]})"));
    assert(!parse(R"({"items": [{"id": "x", "name": "X", "description": "D", "icon": "a.jpg"}]})"));
    assert(!parse(R"({"items": [{"id": "x", "name": "X", "description": "D", "icon": "a.png", "stack": 255}]})"));
    assert(!parse(R"({"items": [{"id": "x", "name": "X", "description": "D", "icon": "a.png", "use": {"action": "fly"}}]})"));
    assert(!parse(R"({"items": [{"id": "x", "name": "X", "description": "D", "icon": "a.png"}, {"id": "x", "name": "Y", "description": "D", "icon": "a.png"}]})"));
    assert(!parse(R"({"items": [{"id": "x", "name": "", "description": "D", "icon": "a.png"}]})"));
    assert(parse(R"({"items":[{"id":"x","name":"X","description":"D","icon":"a.png","use":{"action":"event"},"held_model":"lantern","held_frames":120}]})"));
    assert(items[0].held_model=="lantern"&&items[0].held_frames==120);
    for(const char* fields:{R"("held_model":"lantern")",R"("held_frames":90)",R"("use":{"action":"event"},"held_model":"lantern","held_frames":301)",R"("use":{"action":"event"},"held_model":"../foreign")"})
        assert(!parse(std::string(R"({"items":[{"id":"x","name":"X","description":"D","icon":"a.png",)")+fields+"}]}"));
    // quests name the items they give; an undeclared one is refused
    std::vector<story_notebook::ManifestQuest> quests;
    const std::string quest = R"("quests_format": 1, "quests": [{"id": "q", "title": "Q", "reward": {"item": "item:old-key"},
        "letter": {"id": "l", "sender": "S", "text": "T", "encloses": ["item:old-key"]}, "steps": [{"n": 1, "text": "Go"}]}])";
    assert(story_notebook::parse_quests(mods::json::parse("{" + quest + R"(, "items": [{"id": "old-key", "name": "Old key", "description": "D", "icon": "k.png"}]})"),
                                        quests, why));
    assert(quests[0].reward_mod_item == "old-key" && quests[0].letter_items == std::vector<std::string>{"old-key"} && !quests[0].reward_item);
    assert(!story_notebook::parse_quests(mods::json::parse("{" + quest + "}"), quests, why));
    assert(why.find("not declared") != std::string::npos);
    // a chart's treasure: a declared item of the mod, or one of the allowed game items
    std::vector<story_notebook::ManifestChart> charts;
    const std::string items_decl = R"("items": [{"id": "old-key", "name": "Old key", "description": "D", "icon": "k.png"}])";
    assert(story_notebook::parse_charts(mods::json::parse(R"({"charts": [{"id": "a", "name": "A", "place": {"x": 0, "z": 0}, "item": "item:old-key"},
        {"id": "b", "name": "B", "place": {"x": 0, "z": 0}, "item": "game:heart_piece"}, {"id": "c", "name": "C", "place": {"x": 0, "z": 0}}], )" + items_decl + "}"),
                                        charts, why));
    assert(charts[0].item_mod == "old-key" && charts[1].item_game == 0x07 && charts[2].item_mod.empty() && !charts[2].item_game);
    assert(!story_notebook::parse_charts(mods::json::parse(R"({"charts": [{"id": "a", "name": "A", "place": {"x": 0, "z": 0}, "item": "item:none"}]})"), charts, why));
    assert(!story_notebook::parse_charts(mods::json::parse(R"({"charts": [{"id": "a", "name": "A", "place": {"x": 0, "z": 0}, "item": "game:triforce"}]})"), charts, why));
    assert(!story_notebook::parse_charts(mods::json::parse(R"({"charts": [{"id": "a", "name": "A", "place": {"x": 0, "z": 0}, "item": "game:green_rupee"}]})"), charts, why));
    // letters on their own: what they enclose must be declared; a quest of one chart find gets a warning
    std::vector<story_notebook::ManifestLetter> letters;
    const std::string decl = R"("charts": [{"id": "old-map", "name": "Old map", "description": "X", "place": {"x": 1759, "z": -202572}}],
        "items": [{"id": "old-key", "name": "Old key", "description": "D", "icon": "k.png"}])";
    assert(story_notebook::parse_letters(mods::json::parse("{" + decl + R"(, "letters": [{"id": "map-letter", "sender": "A sailor",
        "text": "Dear Link,\nI found this map.", "encloses": ["chart:old-map", "item:old-key"]}]})"), letters, why));
    assert(letters.size() == 1 && letters[0].charts == std::vector<std::string>{"old-map"} && letters[0].items == std::vector<std::string>{"old-key"});
    assert(!story_notebook::parse_letters(mods::json::parse("{" + decl + R"(, "letters": [{"id": "l", "sender": "S", "text": "T", "encloses": ["chart:none"]}]})"),
                                          letters, why));
    assert(!story_notebook::parse_letters(mods::json::parse("{" + decl + R"(, "letters": [{"id": "l", "sender": "S", "text": "T", "encloses": ["notebook"]}]})"),
                                          letters, why));
    assert(!story_notebook::parse_letters(mods::json::parse("{" + decl + R"(, "letters": [{"id": "l", "sender": "", "text": "T"}]})"), letters, why));
    std::vector<std::string> warnings;
    const std::string one = R"("quests_format": 1, "quests": [{"id": "hunt", "title": "The old map", "steps": [{"n": 1, "text": "Find it", "find": "chart:old-map"}]}])";
    assert(story_notebook::parse_quests(mods::json::parse("{" + decl + ", " + one + "}"), quests, why, story_notebook::npc_catalogue(), &warnings));
    assert(warnings.size() == 1 && warnings[0].find("letters") != std::string::npos);
    warnings.clear();
    const std::string two = R"("quests_format": 1, "quests": [{"id": "hunt", "title": "The old map", "steps": [{"n": 1, "text": "Go"},
        {"n": 2, "text": "Find it", "find": "chart:old-map"}]}])";
    assert(story_notebook::parse_quests(mods::json::parse("{" + decl + ", " + two + "}"), quests, why, story_notebook::npc_catalogue(), &warnings));
    assert(warnings.empty());  // a chart step inside a longer quest
    // an item's quest_step must name a quest of the manifest
    assert(!story_notebook::parse_quests(mods::json::parse("{" + quest + R"(, "items": [{"id": "old-key", "name": "K", "description": "D", "icon": "k.png",
        "use": {"action": "quest_step", "quest": "other", "step": 1}}]})"), quests, why));
}

// Letters on their own that wait: "when" (quest steps, a done quest, the mod's flags) and "after_days"
// on the game's calendar, kept in the story record.
void test_letter_timing() {
    std::string why;
    std::vector<story_notebook::ManifestLetter> letters;
    const std::string decl = R"("quests_format": 1, "quests": [{"id": "light", "title": "The light", "steps": [{"n": 1, "text": "A"}, {"n": 2, "text": "B"}]}],
        "actors": [{"id": "keeper", "actor": "sign", "stage": "sea", "room": 11, "position": {"x": 0, "y": 0, "z": 0}, "lines": ["Hi."],
                    "then": [{"action": "flag", "id": "thanked"}]}])";
    auto parse = [&](const std::string& letter) {
        return story_notebook::parse_letters(mods::json::parse("{" + decl + R"(, "letters": [{"id": "thanks", "sender": "The keeper", "text": "Thank you.")" + letter + "}]}"),
                                             letters, why);
    };
    assert(parse("") && !letters[0].timed());  // as before: sent at once
    assert(parse(R"(, "when": {"quest": "light", "done": true}, "after_days": 3)"));
    assert(letters[0].timed() && letters[0].after_days == 3 && letters[0].when.quests.size() == 1 && letters[0].when.quests[0].done);
    assert(parse(R"(, "when": {"quest": "light", "step": 2, "quests": [{"quest": "light", "done": true}], "flag": "thanked"})"));
    assert(letters[0].when.quests.size() == 2 && letters[0].when.quests[0].step == 2 && letters[0].when.flag == "thanked" && !letters[0].after_days);
    assert(parse(R"(, "after_days": 1)") && letters[0].timed() && !letters[0].when.any());  // days after the mod is on
    // refused, each with its reason
    assert(!parse(R"(, "when": {"quest": "nope", "done": true})") && why.find("doesn't declare") != std::string::npos);
    assert(!parse(R"(, "when": {"quest": "light", "step": 3})") && why.find("step") != std::string::npos);
    assert(!parse(R"(, "when": {"quest": "light"})") && why.find("\"step\" or \"done\"") != std::string::npos);
    assert(!parse(R"(, "when": {"quest": "light", "step": 1, "done": true})"));
    assert(!parse(R"(, "when": {"quest": "light", "done": false})") && why.find("true") != std::string::npos);
    assert(!parse(R"(, "when": {"step": 1})") && why.find("need \"quest\"") != std::string::npos);
    assert(!parse(R"(, "when": {"flag": "nobody-sets-it"})") && why.find("flag") != std::string::npos);
    assert(!parse(R"(, "when": {"day": 3})") && why.find("when") != std::string::npos);
    assert(!parse(R"(, "when": {})"));
    assert(!parse(R"(, "when": {"quests": []})"));
    assert(!parse(R"(, "after_days": 0)") && why.find("1..255") != std::string::npos);
    assert(!parse(R"(, "after_days": 256)") && !parse(R"(, "after_days": 1.5)") && !parse(R"(, "after_days": "2")"));
    assert(why.rfind("letter thanks: ", 0) == 0);
    // the condition: a step reached or passed, a done quest, the flags
    story_notebook::LetterWhen w;
    w.quests = {{"light", 2, false}};
    int32_t step = 0;
    bool thanked = false;
    auto quest = [&](const std::string&) { return step; };
    auto flag = [&](const std::string&) { return thanked; };
    assert(!story_notebook::letter_condition(w, quest, flag));
    step = 1;
    assert(!story_notebook::letter_condition(w, quest, flag));
    step = 2;
    assert(story_notebook::letter_condition(w, quest, flag));
    step = 255;
    assert(story_notebook::letter_condition(w, quest, flag));  // done is past every step
    w.quests = {{"light", 0, true}};
    step = 254;
    assert(!story_notebook::letter_condition(w, quest, flag));
    step = 255;
    assert(story_notebook::letter_condition(w, quest, flag));
    w.flag = "thanked";
    assert(!story_notebook::letter_condition(w, quest, flag));
    thanked = true;
    assert(story_notebook::letter_condition(w, quest, flag));
    w.flag.clear(), w.not_flag = "thanked";
    assert(!story_notebook::letter_condition(w, quest, flag));
    // the days: counted from the first day the condition holds, in the story record, across the day
    // counter's wrap; without after_days at once
    clear_save();
    story_mail::Book b = story_store::book();
    const uint16_t key = story_mail::letter_key("m", "thanks");
    assert(story_notebook::letter_due(b, key, 0, 7));
    assert(!b.has(story_mail::kRecordLetterDayLow, key));
    assert(!story_notebook::letter_due(b, key, 2, 0xFFFF));  // the first day: kept
    assert(b.get(story_mail::kRecordLetterDayLow, key) == 0xFF && b.get(story_mail::kRecordLetterDayHigh, key) == 0xFF);
    assert(!story_notebook::letter_due(b, key, 2, 0xFFFF));
    assert(!story_notebook::letter_due(b, key, 2, 0));
    assert(story_notebook::letter_due(b, key, 2, 1));  // two days later (the counter wrapped)
    assert(story_notebook::letter_due(b, key, 2, 40));
    // a save state holds the story area: the start day comes back with it
    std::vector<uint8_t> state(mem::ptr(kArena + 0xE000), mem::ptr(kArena + 0xE000) + story_store::kMemoryBytes);
    story_notebook::erase_letter_day(b, key);
    assert(!b.has(story_mail::kRecordLetterDayLow, key) && !b.has(story_mail::kRecordLetterDayHigh, key));
    std::memcpy(mem::ptr(kArena + 0xE000), state.data(), state.size());
    assert(story_notebook::letter_due(b, key, 2, 1));
    // a full record: the count starts when there is room (never half a day kept)
    clear_save();
    unsigned filled = 0;
    while (b.set(30, uint16_t(++filled), 1)) {
    }
    assert(!story_notebook::letter_due(b, key, 2, 5) && !b.has(story_mail::kRecordLetterDayLow, key));
    b.erase(30, 1);
    assert(!story_notebook::letter_due(b, key, 2, 5) && !b.has(story_mail::kRecordLetterDayLow, key));  // one record free: not both
    b.erase(30, 2);
    assert(!story_notebook::letter_due(b, key, 2, 5) && b.has(story_mail::kRecordLetterDayHigh, key));
    assert(story_notebook::letter_due(b, key, 2, 7));
    clear_save();
}

// "hold_up": true or false, needs held_model; with it a held model needs no use action
void test_hold_up_manifest() {
    std::vector<story_notebook::ManifestItem> items;
    std::string why;
    auto parse = [&](const std::string& fields) {
        return story_notebook::parse_items(mods::json::parse(R"({"items": [{"id": "crystal", "name": "Crystal", "description": "It glows.", "icon": "c.png")" + fields + "}]}"),
                                           items, why);
    };
    assert(parse("") && !items[0].hold_up);
    assert(parse(R"(, "held_model": "crystal", "hold_up": true)") && items[0].hold_up && items[0].held_model == "crystal" &&
           items[0].use == story_notebook::ManifestItem::Use::None);
    assert(parse(R"(, "held_model": "crystal", "hold_up": true, "use": {"action": "show"}, "held_frames": 30)") && items[0].held_frames == 30);
    assert(!parse(R"(, "hold_up": true)") && why.find("held_model") != std::string::npos);
    assert(!parse(R"(, "held_model": "crystal", "hold_up": "yes")") && why.find("true or false") != std::string::npos);
    assert(!parse(R"(, "held_model": "crystal", "hold_up": false)") && why.find("use action") != std::string::npos);
    assert(!parse(R"(, "held_model": "crystal")"));  // as before: a held model needs a use (or hold_up)
}

void test_services() {
    clear_save();
    reset();
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "wwhd_story_items_test";
    std::filesystem::create_directories(dir / "assets");
    std::ofstream(dir / "assets" / "key.png", std::ios::binary) << "not really a png";
    using story_notebook::ManifestItem;
    ManifestItem key;
    key.id = "old-key", key.name = "Old key", key.description = "Rusty.", key.icon = "assets/key.png";
    ManifestItem lamp;
    lamp.id = "lamp", lamp.name = "Lamp", lamp.description = "Bright.", lamp.icon = "assets/key.png", lamp.stack = 3,
    lamp.use = ManifestItem::Use::Event;
    lamp.held_model="lantern";lamp.held_frames=120;
    ManifestItem step = lamp;
    step.id = "map", step.use = ManifestItem::Use::QuestStep, step.use_quest = "hunt", step.use_step = 2;
    ManifestItem missing = lamp;
    missing.id = "gone", missing.icon = "assets/none.png";
    std::string why;
    assert(define("mod", dir.string(), key, why) && define("mod", dir.string(), lamp, why) && define("mod", dir.string(), step, why));
    assert(!define("mod", dir.string(), missing, why) && why.find("icon") != std::string::npos);
    assert(installed().size() == 3 && find("mod", "lamp")->stack == 3 && find(item_key("mod", "old-key"))->name == "Old key");
    set_presenter(show);
    set_use_calls(open_notebook, reach);
    set_game_calls(original, game_call);
    // no save loaded: nothing
    st32(0x101F84DC, 0);
    assert(give("mod", "old-key", 1, "test") == kResultNoRoom && count("mod", "old-key") == 0);
    st32(0x101F84DC, kSave);
    assert(give("mod", "nothing", 1, "test") == kResultInvalid && give("other", "old-key", 1, "test") == kResultInvalid);
    assert(give("mod", "old-key", 1, "test") == 1 && count("mod", "old-key") == 1);
    assert(give("mod", "lamp", 2, "test") == 2 && give("mod", "lamp", 5, "test") == 3);
    // the gets' captions, one at a time, with the game's small get jingle
    Cpu cpu{};
    shown.clear(), calls.clear();
    assert(present(&cpu) && shown.size() == 1 && shown[0] == "Old key|Rusty." && calls == std::vector<std::string>{"jingle 2293"});
    shown_ok = false;
    assert(!present(&cpu) && shown.size() == 1);  // the page is open: later
    shown_ok = true;
    assert(present(&cpu) && present(&cpu) && !present(&cpu) && shown.size() == 3 && shown[1] == "Lamp (2)|Bright." && shown[2] == "Lamp (3)|Bright.");
    assert(take("mod", "lamp", 4) == kResultNotEnough && take("mod", "lamp", 1) == 2);
    // the bag's actions
    const uint16_t k = item_key("mod", "old-key"), l = item_key("mod", "lamp"), m = item_key("mod", "map");
    assert(act(k, Action::Info) && shown.back() == "Old key|Rusty.");
    std::vector<uint16_t> presentations;set_model_presenter([&](uint16_t key){presentations.push_back(key);});
    assert(!act(k, Action::Use) && presentations.empty() && !reason().empty());           // no use action
    assert(!act(k, Action::Assign, 1));                           // can't go on a button
    assert(act(l, Action::Use) && act(l, Action::Use) && used("mod", "lamp") == 2 && used("mod", "lamp") == 0);
    assert(presentations==std::vector<uint16_t>({l,l}));
    assert(!act(m, Action::Use)&&presentations.size()==2);                                 // Link doesn't have it
    give("mod", "map", 1, "test");
    reached.clear();
    assert(act(m, Action::Use) && reached == std::vector<std::string>{"mod:hunt:2"});
    set_model_presenter({});
    assert(act(l, Action::Assign, 2) && on_button(2) == l && number(l) && pool_number(number(l)));
    assert(act(l, Action::Assign, -1) && !on_button(2));
    // a game place: only an empty one of the game's save
    st8(kSave + 0xA6 + 4, 0x99);                                  // the game holds a letter there
    assert(!act(k, Action::MoveToGamePlace, 4) && act(k, Action::MoveToGamePlace, 5) && at_game_place(5) == k && number(k));
    assert(act(k, Action::MoveToPage) && !at_game_place(5) && page_place(place(k)));
    // the owned list, a game place's number, the page limit
    assert(owned().size() == 3 && unplaced().empty());
    assert(act(l, Action::MoveToGamePlace, 0) && number_at_game_place(0) == number(l) && pool_number(number_at_game_place(0)));
    assert(!number_at_game_place(1));
    assert(act(l, Action::MoveToPage));
    set_page_limit(0);
    assert(page_limit() == 1);
    set_page_limit(99);
    assert(page_limit() == kMaxPages);
    set_page_limit(kPages);
    assert(frame(k) == -1);
    set_frame_source([](uint16_t) { return 60; });
    assert(frame(k) == 60);
    set_frame_source(nullptr);
    // the game's item tables: a pool number bound to an item is that item
    calls.clear();
    cpu.r[3] = number(k), cpu.r[4] = 9;
    hooks::check_item_get(&cpu);
    assert(cpu.r[3] == 1 && calls.empty());
    cpu.r[3] = number(k);
    hooks::exec_item_get(&cpu);
    assert(count(k) == 1 && calls.empty());  // a single item: still one (no second caption either)
    cpu.r[3] = 0x31, cpu.r[4] = 9;            // a game item: the game's
    hooks::check_item_get(&cpu);
    assert(cpu.r[3] == 7 && calls == std::vector<std::string>{"check"});
    cpu.r[3] = 0x31;
    hooks::exec_item_get(&cpu);
    assert(calls.back() == "exec");
    cpu.r[3] = kPool.back();  // a pool number bound to nothing: nothing has it
    hooks::check_item_get(&cpu);
    assert(cpu.r[3] == 0);
    // an item Link holds up ("hold_up"): the item-get presenter takes the give, no light caption; without
    // a model (the presenter says no) the caption as before; a quiet give, nothing
    ManifestItem crystal = key;
    crystal.id = "crystal", crystal.name = "Crystal", crystal.description = "It glows.", crystal.held_model = "crystal-model", crystal.hold_up = true;
    assert(define("mod", dir.string(), crystal, why) && find("mod", "crystal")->hold_up);
    static std::vector<uint16_t> held, chest;
    static bool hold_ok = true;
    set_hold_up_presenter([](uint16_t key) { held.push_back(key); return hold_ok; }, [](uint16_t key, bool on) { chest.push_back(on ? key : 0); });
    const uint16_t c = item_key("mod", "crystal");
    while (present(&cpu)) {
    }
    shown.clear();
    assert(give("mod", "crystal", 1, "test") == 1 && held == std::vector<uint16_t>{c} && !present(&cpu) && shown.empty());
    take("mod", "crystal", 1), held.clear();
    hold_ok = false;
    assert(give("mod", "crystal", 1, "test") == 1 && held.size() == 1 && present(&cpu) && shown.back() == "Crystal|It glows.");
    take("mod", "crystal", 1), held.clear(), hold_ok = true;
    assert(give("mod", "crystal", 1, "the editor", false) == 1 && held.empty() && !present(&cpu));
    take("mod", "crystal", 1);
    assert(give("mod", "old-key", 1, "test") >= 1 && held.empty());  // an item without hold_up: its caption
    while (present(&cpu)) {
    }
    // a chest's own demo: only items with hold_up
    chest_hold_up(c, true), chest_hold_up(k, true), chest_hold_up(c, false);
    assert(chest == std::vector<uint16_t>({c, 0}));
    // the get message: the stand-in's, with the item's name and description
    static uint32_t armed_number = 1;
    static std::u16string armed_text;
    assert(arm_get_text(c, false));  // no letter service: nothing to disarm
    assert(!arm_get_text(c, true));
    set_get_text([](uint32_t number, const std::u16string* text) {
        armed_number = number, armed_text = text ? *text : u"";
        return true;
    });
    assert(arm_get_text(c, true) && armed_number == 0x65 + 0x9D && armed_text == u"You got Crystal!\nIt glows.");
    assert(arm_get_text(c, false) && armed_number == 0 && armed_text.empty());
    assert(!arm_get_text(0x1234, true));
    set_get_text(nullptr), set_hold_up_presenter(nullptr, nullptr);
    // a mod that isn't enabled any more: its items stay in the save, hidden
    reset();
    assert(!find(k) && count(k) == 1 && !at_game_place(5));
    std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
    assert(map(kGlobals, 0x10000) && map(kArena, 0x10000));
    story_store::set_memory(kArena + 0xE000);
    st32(0x101F84DC, kSave);
    test_pool();
    test_bag();
    test_manifest();
    test_letter_timing();
    test_hold_up_manifest();
    test_services();
    std::puts("story_items_test: ok");
    return 0;
}
