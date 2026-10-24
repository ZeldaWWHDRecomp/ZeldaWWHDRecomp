// Mod treasure charts, part (c): the chest's item (story_treasure.h, story_treasure_chest.h).
//
// The salvage chest (decomp d_a_salvage_tbox.cpp) gets (type << 8) | item number from daSalvage_c, which
// reads the item number from the salvage point's parameters; daSTBox_c::actWait then creates the item Link
// holds up: fopAcM_createItemForTrBoxDemo(pos, itemNo, -1, room, 0, 0) 025D7E88, a daDitem_c (d_a_demo_item.cpp)
// whose Delete runs execItemGet(itemNo) once Link's get message (0x65 + itemNo) is closed.
//   - A game item: the point carries its number; nothing here runs for it.
//   - A mod item: the point carries kChestMarker. create_item() swaps it for kStandIn, arms the stand-in's
//     get message with the mod item's text (story_mail::item_get_text) and keeps the item pending;
//     exec_item_get() then gives Link the mod item instead of running the stand-in's get function.
// Which chart's chest it is: story_treasure::active_save_no(), the point daSalvage_c is working on.
#include "story_treasure_chest.h"

#include <mutex>
#include <optional>

#include "guest_addr.h"
#include "runtime.h"
#include "story_chart.h"
#include "story_items.h"
#include "story_mail.h"
#include "story_treasure.h"
#include "story_world_manifest.h"

namespace story_treasure_chest {
namespace {

const uint32_t kCreateItemForTrBoxDemo = GC(0x025D7E88);  // fopAcM_createItemForTrBoxDemo
const uint32_t kExecItemGet = GC(0x0254DA38);             // execItemGet(itemNo)

struct Pending {
    bool armed = false;
    std::string mod, item;
    uint32_t save_no = 0;
    bool caption = false;  // the get message couldn't be armed: the light caption names the item
};
std::mutex g_mu;
Pending g_pending;
std::u16string g_text;  // the armed get message (kept alive while armed)
void (*g_next)(Cpu*) = nullptr;
bool g_placed_chests = false;
bool (*g_placed)(uint32_t, std::string&, std::string&) = nullptr;
constexpr uint32_t kActorPos = 0x314;  // fopAc_ac_c::current.pos: the chest passes its address
static_assert(story_notebook::kChestModItem == kChestMarker, "the placed chest's mod item marker is the chart chest's");
void (*g_original)(Cpu*, uint32_t) = nullptr;

constexpr uint8_t kDefaultGameItem = 0x06;  // story_notebook::kChartDefaultItem (an orange rupee)

// what the chest of a reserved save number holds: its chart's declaration (story_chart::charts())
struct Chest {
    std::string mod, chart, item_mod;
    uint8_t item_game = kDefaultGameItem;
};
std::optional<Chest> chest_of(uint32_t save_no) {
    Chest out;
    if (!story_treasure::chart_for_save_no(save_no, out.mod, out.chart)) return std::nullopt;
    for (const story_chart::ChartView& v : story_chart::charts())
        if (v.mod == out.mod && v.chart == out.chart) {
            out.item_mod = v.item_mod;
            if (v.item_game) out.item_game = v.item_game;
            return out;
        }
    return std::nullopt;
}

void (*g_icon)(uint16_t, const std::string&, bool) = nullptr;
void disarm_locked() {
    if (g_pending.armed) {
        story_mail::item_get_text(0, nullptr);
        if (g_icon) g_icon(0, {}, false);
    }
    g_pending = {};
}

}  // namespace

void set_item_get_next(void (*next)(Cpu*), void (*original)(Cpu*, uint32_t)) { g_next = next, g_original = original; }
void set_icon_presenter(void (*icon)(uint16_t, const std::string&, bool)) { g_icon = icon; }
void note_placed_chests() { g_placed_chests = true; }
bool placed_chests() { return g_placed_chests; }
void set_placed_chest(bool (*placed)(uint32_t, std::string&, std::string&)) { g_placed = placed; }
namespace { bool g_hook_ready = false; }
void set_item_hook_ready(bool ready) { g_hook_ready = ready; }
bool item_hook_ready() { return g_hook_ready; }

void state_loaded() {
    std::lock_guard lock(g_mu);
    disarm_locked();
}

bool pending() {
    std::lock_guard lock(g_mu);
    return g_pending.armed;
}

namespace hooks {

// fopAcM_createItemForTrBoxDemo(pos, itemNo, ...): the marker of a mod chart's chest becomes the stand-in
void create_item(Cpu* c) {
    if (c->r[4] != kChestMarker) {
        {
            std::lock_guard lock(g_mu);
            disarm_locked();  // another chest: a pending one was cut off (its event ended without the get)
        }
        return story_treasure::original(c, kCreateItemForTrBoxDemo);
    }
    const uint32_t save_no = story_treasure::active_save_no();
    std::optional<Chest> chest = story_treasure::is_mod_save_no(save_no) ? chest_of(save_no) : std::nullopt;
    if (!chest && g_placed && c->r[3] >= kActorPos) {  // a placed chest of the mod's item
        Chest placed;
        if (g_placed(c->r[3] - kActorPos, placed.mod, placed.item_mod)) placed.chart = "(placed chest)", chest = placed;
    }
    const std::optional<story_items::Item> item =
        chest && !chest->item_mod.empty() ? story_items::find(chest->mod, chest->item_mod) : std::nullopt;
    if (!item) {
        // (can't happen with a declared chart: the marker is only in our points) the default game item
        LOG("[treasure] a chest with the mod item marker, but no mod chart's item (save number %u): %02X instead", save_no,
            kDefaultGameItem);
        c->r[4] = kDefaultGameItem;
        return story_treasure::original(c, kCreateItemForTrBoxDemo);
    }
    bool caption;
    {
        std::lock_guard lock(g_mu);
        disarm_locked();
        g_text = get_text(item->name, item->description);
        g_pending.armed = true;
        g_pending.mod = chest->mod, g_pending.item = item->id, g_pending.save_no = save_no;
        caption = g_pending.caption = !story_mail::item_get_text(stand_in_message(), &g_text);
    }
    LOG("[treasure] %s: chart \"%s\"'s chest holds item \"%s\"%s", chest->mod.c_str(), chest->chart.c_str(), item->id.c_str(),
        caption ? " (the get message can't be replaced now: the item's caption names it)" : "");
    story_items::chest_hold_up(item->key, true);  // "hold_up": the stand-in shows the item's own model
    if (g_icon) g_icon(item->key, item->icon_path, true);  // otherwise (or until the model is ready): its icon
    c->r[4] = kStandIn;
    story_treasure::original(c, kCreateItemForTrBoxDemo);
}

// execItemGet(itemNo): the stand-in of a pending chest gives Link the mod item
void exec_item_get(Cpu* c) {
    Pending p;
    {
        std::lock_guard lock(g_mu);
        if (g_pending.armed && (c->r[3] & 0xFF) == kStandIn) {
            p = g_pending;
            disarm_locked();
        }
    }
    if (p.armed) {
        if (const std::optional<story_items::Item> it = story_items::find(p.mod, p.item)) story_items::chest_hold_up(it->key, false);
        const int32_t n = story_items::give(p.mod, p.item, 1, "a treasure chart's chest", p.caption);
        if (n < 0) LOG("[treasure] %s: item \"%s\" from the chest could not be given (%d)", p.mod.c_str(), p.item.c_str(), n);
        return;
    }
    if (g_next) return g_next(c);
    g_original(c, kExecItemGet);
}

}  // namespace hooks
}  // namespace story_treasure_chest

namespace story_treasure {

ChestItem chest_item_for(uint32_t save_no) {
    using namespace story_treasure_chest;
    ChestItem out;
    out.game_item = kDefaultGameItem;
    const std::optional<Chest> chest = chest_of(save_no);
    if (!chest) return out;
    if (chest->item_mod.empty()) {
        out.game_item = chest->item_game;
        return out;
    }
    const std::optional<story_items::Item> item = story_items::find(chest->mod, chest->item_mod);
    if (!item) {
        LOG("[treasure] %s: chart \"%s\"'s chest item \"%s\" is not installed: the chest holds %02X", chest->mod.c_str(),
            chest->chart.c_str(), chest->item_mod.c_str(), kDefaultGameItem);
        return out;
    }
    out.game_item = kChestMarker;
    out.mod_item = int32_t(item->key);  // the item's story key (the chest binds no pool number)
    return out;
}

std::vector<Hook> chest_hooks() {
    // only when a chart's chest holds a mod item: game items take the game's own path
    bool mod_item = false;
    for (const story_chart::ChartView& v : story_chart::charts()) mod_item |= !v.item_mod.empty();
    if (!mod_item && !story_treasure_chest::placed_chests()) return {};
    return {{story_treasure_chest::kCreateItemForTrBoxDemo, story_treasure_chest::hooks::create_item}};
}

}  // namespace story_treasure
