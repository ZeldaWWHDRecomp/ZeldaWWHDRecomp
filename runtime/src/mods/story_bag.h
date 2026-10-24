// The Delivery Bag's extra pages for mod items (docs/mod-sdk-v2.md "Items"): the driver of the page
// (story_bag_page.h) over the game's Delivery Bag window, drawn as our layout StoryBag_00 with the item
// screen (story_lyt_screen.h; the description: runtime/assets/story_bag/StoryBag_00.json, made by
// tools/story_mods/make_bag_page_layout.py, built into an archive on the player's machine).
//
// While the game's Delivery Bag window is open and Link has mod items, R turns to the first extra page;
// the page then has the controller (the game sees an idle one, as with the notebook's page) until it goes
// back to the game's window. Items move between the extra pages and the game's eight places only through
// story_items::act (record 11 of the save's story block); nothing is written to the game's own save bytes.
// The soft cap (how many pages new items fill): story_items::set_page_limit, from WWHD_BAG_PAGES (1..16,
// default 4).
#pragma once
#include <cstdint>
#include <filesystem>
#include <initializer_list>

#include "../input.h"

struct Cpu;

namespace story_bag {

// Builds the layout's archive into cache_dir and adds it as a story_lyt_screen client (before
// story_lyt_screen::install). False (logged) if there are no mod items or the layout can't be built.
bool configure(const std::filesystem::path& cache_dir);
void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>));
// The GamePad's read (VPADRead, game thread): the page's input; R in the Delivery Bag window opens it.
// While the page is open the game gets an idle controller.
void pad(input::PadState& p, Cpu* c);
// The Pro Controller's read (KPADRead): idle while the page is open
void mute(input::PadState& p);
bool page_open();
// Called (game thread) after X / Y on the page put an item on a button: the item buttons' strip refresh
// (story_item_buttons::refresh_strip, wired where both are built)
void set_after_assign(void (*after)(Cpu*));
void state_loaded();
// The item's icon changed (the world editor): read it again the next time a page shows it
void forget_icon(uint16_t key);

}  // namespace story_bag
