// Mod treasure charts, part (c): the chest's item (runtime/src/mods/story_treasure_chest.*) on a synthetic
// save. Part (a)'s side (which point is being salvaged, the chart of a save number, the game calls) and the
// game's own functions are stubbed; no game files are needed.
#include "mods/story_chart.h"
#include "mods/story_items.h"
#include "mods/story_mail.h"
#include "mods/story_manifest.h"
#include "mods/story_store.h"
#include "mods/story_treasure.h"
#include "mods/story_treasure_chest.h"
#include "runtime.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

using namespace story_treasure_chest;

void log_msg(const char* fmt, ...) {
    (void)fmt;
}

namespace {

constexpr uint32_t kGlobals = 0x101F0000, kArena = 0x20000000, kSave = kArena;
constexpr uint32_t kCreate = 0x025D7E88, kExec = 0x0254DA38;

void* map(uint32_t at, uint32_t size) {
#ifdef _WIN32
    return VirtualAlloc(mem::ptr(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    return mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#endif
}

// the game: what was called, with which item number
std::vector<std::string> calls;
void original(Cpu* c, uint32_t fn) {
    calls.push_back((fn == kCreate ? "create " : fn == kExec ? "exec " : "other ") + std::to_string(c->r[4 * (fn == kCreate) + 3 * (fn != kCreate)]));
    if (fn == kCreate) c->r[3] = 0x1234;  // the new actor's process id
}
uint32_t game_call(Cpu*, uint32_t, std::initializer_list<uint32_t>) { return 0; }
void next(Cpu* c) { calls.push_back("next " + std::to_string(c->r[3])); }

// part (a)'s answers
uint32_t g_active = 0;

}  // namespace

namespace story_treasure {
uint32_t active_save_no() { return g_active; }
bool chart_for_save_no(uint32_t save_no, std::string& mod, std::string& chart) {
    const int i = mod_chart_index(save_no);
    const std::vector<story_chart::ChartView> all = story_chart::charts();
    if (i < 0 || size_t(i) >= all.size()) return false;
    mod = all[size_t(i)].mod, chart = all[size_t(i)].chart;
    return true;
}
void original(Cpu* c, uint32_t fn) { ::original(c, fn); }
uint32_t call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) { return game_call(c, fn, args); }
}  // namespace story_treasure

namespace {

void clear_save() {
    std::memset(mem::ptr(kArena + 0xE000), 0, story_store::kMemoryBytes);
    for (unsigned i = 0; i < 8; i++) st8(kSave + 0xA6 + i, 0xFF);  // the game's delivery bag: empty
}

Cpu create(uint32_t item) {
    Cpu c{};
    c.r[3] = kArena + 0x100, c.r[4] = item, c.r[5] = 0xFFFFFFFF, c.r[6] = 3;
    story_treasure_chest::hooks::create_item(&c);
    return c;
}
void exec(uint32_t item) {
    Cpu c{};
    c.r[3] = item;
    story_treasure_chest::hooks::exec_item_get(&c);
}

std::vector<std::string> shown;
bool show(const std::string& title, const std::string& text, const char*) {
    shown.push_back(title + "|" + text);
    return true;
}

void test_text() {
    assert(get_text("Old coin", "") == u"You got Old coin!");
    assert(get_text("Old coin", "Worn.\nVery old.") == u"You got Old coin!\nWorn.\nVery old.");
    assert(stand_in_message() == 0x65 + 0x9D);
    // not the notebook's stand-in, the port's own numbers or a pool number
    assert(kStandIn != 0x9C && kStandIn != 0xA0 && kStandIn != 0xA1 && !story_items::pool_number(kStandIn));
    assert(story_items::pool_number(kChestMarker));
}

void test_manifest() {
    // the chart's "item" (story-mod-items' parse) ends up where the chest reads it
    using story_notebook::ManifestChart;
    std::vector<ManifestChart> charts;
    std::string why;
    assert(story_notebook::parse_charts(mods::json::parse(R"({"charts": [
        {"id": "a", "name": "A", "place": {"x": 1000, "z": 2000}, "item": "game:silver_rupee"},
        {"id": "b", "name": "B", "place": {"x": 1000, "z": 2000}, "item": "item:coin"},
        {"id": "c", "name": "C", "place": {"x": 1000, "z": 2000}}],
        "items": [{"id": "coin", "name": "Old coin", "description": "Worn.", "icon": "coin.png"}]})"),
                                        charts, why));
    assert(charts.size() == 3 && charts[0].item_game == 0x0F && charts[1].item_mod == "coin" && charts[2].item_game == 0 &&
           charts[2].item_mod.empty());
    assert(!story_notebook::parse_charts(mods::json::parse(R"({"charts": [{"id": "a", "name": "A", "place": {"x": 0, "z": 0}, "item": "game:green_rupee"}]})"),
                                         charts, why));
    assert(!story_notebook::parse_charts(mods::json::parse(R"({"charts": [{"id": "a", "name": "A", "place": {"x": 0, "z": 0}, "item": "item:none"}]})"),
                                         charts, why));
}

void test_chest() {
    clear_save();
    // no mod chart's chest holds a mod item: nothing to hook
    assert(story_chart::define("mod", "gold", "Gold chart", "", 1000, 2000, "", 0x0F) == story_chart::kResultNone);
    assert(story_chart::define("mod", "plain", "Plain chart", "", 1000, 2000) == story_chart::kResultNone);
    assert(story_treasure::chest_hooks().empty());
    // a game item: its number, no mod item; none declared: an orange rupee; an unknown number: the same
    assert(story_treasure::chest_item_for(129).game_item == 0x0F && story_treasure::chest_item_for(129).mod_item == -1);
    assert(story_treasure::chest_item_for(130).game_item == 0x06 && story_treasure::chest_item_for(130).mod_item == -1);
    assert(story_treasure::chest_item_for(5).game_item == 0x06 && story_treasure::chest_item_for(200).game_item == 0x06);

    // a mod item
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "wwhd_treasure_chest_test";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "coin.png") << "png";
    story_notebook::ManifestItem coin;
    coin.id = "coin", coin.name = "Old coin", coin.description = "Worn.", coin.icon = "coin.png";
    coin.stack = 3;
    std::string why;
    assert(story_items::define("mod", dir.string(), coin, why));
    assert(story_chart::define("mod", "coin", "Coin chart", "", -5000, 3000, "coin") == story_chart::kResultNone);
    assert(story_chart::define("mod", "lost", "Lost chart", "", -5000, 3000, "gone") == story_chart::kResultNone);  // not installed
    const std::vector<story_treasure::Hook> hooks = story_treasure::chest_hooks();
    assert(hooks.size() == 1 && hooks[0].first == kCreate && hooks[0].second == story_treasure_chest::hooks::create_item);
    const story_treasure::ChestItem ci = story_treasure::chest_item_for(131);
    assert(ci.game_item == kChestMarker && ci.mod_item == story_items::item_key("mod", "coin"));
    assert(story_treasure::chest_item_for(132).game_item == 0x06);  // its item isn't installed: an orange rupee

    story_items::set_presenter(show);
    set_item_get_next(next, original);

    // a game item's chest (and any other chest): the game's own create, nothing pending
    calls.clear();
    g_active = 129;
    Cpu c = create(0x0F);
    assert(calls == std::vector<std::string>{"create 15"} && c.r[3] == 0x1234 && !pending());
    exec(0x0F);
    assert(calls.back() == "next 15");

    // the mod item's chest: the stand-in is created and its get gives the mod item (the letter service has
    // no hooks here, so the get message can't be replaced: the light caption names the item)
    calls.clear();
    g_active = 131;
    c = create(kChestMarker);
    assert(calls == std::vector<std::string>{"create " + std::to_string(kStandIn)} && c.r[3] == 0x1234 && pending());
    assert(story_items::count("mod", "coin") == 0);  // given only with the stand-in's get
    exec(0x01);                                       // another item's get on the way: the next handler's
    assert(calls.back() == "next 1" && pending());
    exec(kStandIn);
    assert(!pending() && story_items::count("mod", "coin") == 1);
    assert(calls.back() == "next 1");  // the stand-in's get function didn't run
    assert(story_items::present(nullptr) && shown.size() == 1 && shown[0] == "Old coin|Worn.");
    exec(kStandIn);  // nothing pending: the stand-in's own get (the game's)
    assert(calls.back() == "next " + std::to_string(kStandIn) && story_items::count("mod", "coin") == 1);

    // the marker without a mod chart being salvaged: the default item, nothing pending
    calls.clear();
    g_active = 0;
    create(kChestMarker);
    assert(calls == std::vector<std::string>{"create 6"} && !pending());
    g_active = 132;  // its item isn't installed
    create(kChestMarker);
    assert(calls.back() == "create 6" && !pending());

    // a pending chest is forgotten when a state is loaded, or another chest creates its item
    g_active = 131;
    create(kChestMarker);
    assert(pending());
    story_treasure_chest::state_loaded();
    assert(!pending());
    create(kChestMarker);
    create(0x07);
    assert(!pending());
    exec(kStandIn);
    assert(story_items::count("mod", "coin") == 1);

    // no next handler (no mod items' hook): the game's function
    set_item_get_next(nullptr, original);
    calls.clear();
    exec(0x05);
    assert(calls == std::vector<std::string>{"exec 5"});
}

void test_get_message() {
    // with the letter service's hooks the stand-in's get message shows the item, and no caption
    std::map<uint32_t, void (*)(Cpu*)> hooked;
    story_mail::set_game_calls(original, game_call);
    story_mail::set_text_buffers(kArena + 0x18000, 8192);
    story_mail::note_import();
    story_mail::install([](uint32_t) { return std::string(); }, [&](uint32_t a, void (*fn)(Cpu*)) { hooked[a] = fn; });
    assert(story_mail::installed());
    set_item_get_next(next, original);
    shown.clear();
    g_active = 131;
    create(kChestMarker);
    assert(pending());
    exec(kStandIn);
    assert(story_items::count("mod", "coin") == 2);
    assert(!story_items::present(nullptr) && shown.empty());
    // the slot is the notebook's: free again
    assert(story_mail::item_get_text(stand_in_message(), nullptr));
}

// A placed chest (SDK v3 "chest") holding a mod item: the same marker and stand-in, the chest found by its
// actor (the demo item is created at the chest's position, actor + 0x314)
uint32_t g_placed_actor = 0;
bool placed(uint32_t actor, std::string& mod, std::string& item) {
    if (actor != g_placed_actor) return false;
    mod = "mod", item = "coin";
    return true;
}
void test_placed_chest() {
    assert(!placed_chests());
    note_placed_chests();
    assert(placed_chests() && !story_treasure::chest_hooks().empty());
    set_placed_chest(placed);
    set_item_get_next(next, original);
    g_active = 0;  // no salvage
    const int32_t before = story_items::count("mod", "coin");
    g_placed_actor = kArena + 0x100 - 0x314;
    calls.clear();
    create(kChestMarker);
    assert(calls == std::vector<std::string>{"create " + std::to_string(kStandIn)} && pending());
    exec(kStandIn);
    assert(!pending() && story_items::count("mod", "coin") == before + 1);
    // another chest with the marker (not ours): the default item, nothing pending
    g_placed_actor = 1;
    calls.clear();
    create(kChestMarker);
    assert(calls == std::vector<std::string>{"create 6"} && !pending());
    set_placed_chest(nullptr);
    assert(story_notebook::kChestModItem == kChestMarker);
}

}  // namespace

int main() {
    assert(map(kGlobals, 0x10000) && map(kArena, 0x20000));
    story_store::set_memory(kArena + 0xE000);
    st32(0x101F84DC, kSave);
    test_text();
    test_manifest();
    test_chest();
    test_get_message();
    test_placed_chest();
    std::puts("story_treasure_chest_test: ok");
    return 0;
}
