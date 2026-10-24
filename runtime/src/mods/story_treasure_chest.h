// Mod treasure charts, part (c): the chest's item (story_treasure.h), unit-tested in
// runtime/tools/story_treasure_chest_test.cpp.
//
// A chart in the manifest says what its chest holds ("charts": [{..., "item": ...}], story_manifest.h,
// kept in story_chart::ChartView item_mod / item_game):
//   "game:<name>"  one of the game's items (story_notebook::chart_game_item). The chest carries its number and the game gives it
//                  the way it gives a treasure chart's item: the get-item demo, its message, execItemGet.
//   "item:<id>"    one of the mod's items (story_items.h). The chest carries kChestMarker. A pool number has
//                  no model or message in the game's item tables (daDitem_c::create would fall back to a
//                  green rupee: its model, its message, and execItemGet gives Link the rupee). So the port
//                  swaps the marker for a stand-in game item (kStandIn) when the chest creates its get-item
//                  item, shows the mod item's name and description in the get message instead of the
//                  stand-in's, and gives Link the mod item when the stand-in's execItemGet comes (the game's
//                  get function of the stand-in is not run).
//   (none)         an orange rupee (story_notebook::kChartDefaultItem).
#pragma once
#include <cstdint>
#include <string>

#include "story_mail_book.h"

struct Cpu;

namespace story_treasure_chest {

// The chest's item number for a mod item: a number of the port's pool (story_items_core.h kPool), which
// no game data uses
inline constexpr uint8_t kChestMarker = 0x1B;
// The game item the get-item demo shows for a mod item: the Complimentary ID (a card); not the notebook's
// stand-in (0x9C), the port's own 0xA0 / 0xA1 or a pool number. Its get message is 0x65 + the number
// (daPy_lk_c::dProcGetItem); its get function never runs while a chest is pending.
inline constexpr uint8_t kStandIn = 0x9D;
inline constexpr uint32_t kMsgForItem = 0x65;
inline constexpr uint32_t stand_in_message() { return kMsgForItem + kStandIn; }

// The get message of a mod item: as the game's ("You got ...!"), then the item's description
inline std::u16string get_text(const std::string& name, const std::string& description) {
    std::u16string out = u"You got ";
    if (!story_mail::append_utf16(name, false, out)) out += u"something";
    out += u"!";
    std::u16string body;
    if (!description.empty() && story_mail::append_utf16(description, true, body)) out += u"\n" + body;
    return out;
}

// ---- the game side (story_treasure_chest.cpp) ----
// story_treasure::chest_item_for and chest_hooks() are declared in story_treasure.h.
// execItemGet comes through the letter service's hook (story_mail::set_item_get_next): this one takes the
// stand-in of a pending mod item, and hands every other number to `next` (the mod items' handler), or the
// game's function (original(c, addr)) when null.
void set_item_get_next(void (*next)(Cpu*), void (*original)(Cpu*, uint32_t));
// Placed chests (SDK v3 "chest" actors, story_actors) with one of the mod's items carry kChestMarker too:
// note_placed_chests() says some do (the hook is needed even without charts); `placed(actor, mod, item)`
// answers for a chest actor (the address of the daTbox whose position the demo item is created at).
void note_placed_chests();
bool placed_chests();
void set_placed_chest(bool (*placed)(uint32_t actor, std::string& mod, std::string& item));
// the chest item hook (fopAcM_createItemForTrBoxDemo) is installed: chests of mod items may appear
void set_item_hook_ready(bool ready);
bool item_hook_ready();
// The get moment shows the mod item's icon in the stand-in's place (story_item_get_icon.h): `icon(key, png,
// true)` when the chest creates its demo item, `icon(0, "", false)` when the item is given or the chest's
// get is cut off. Without it the stand-in's card shows.
void set_icon_presenter(void (*icon)(uint16_t key, const std::string& png, bool on));
void state_loaded();  // a full state was loaded: no chest is pending
bool pending();       // tests: a mod item's chest waits for its execItemGet
namespace hooks {
void create_item(Cpu* c);    // fopAcM_createItemForTrBoxDemo(pos, itemNo, itemBitNo, roomNo, angle, scale)
void exec_item_get(Cpu* c);  // execItemGet(itemNo), after the letter service's hook
}  // namespace hooks

}  // namespace story_treasure_chest
