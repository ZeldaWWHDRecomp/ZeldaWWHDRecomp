// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
struct Cpu;
namespace story_item_models {
inline constexpr uint32_t kMemoryBytes=1024;
using Invoke=uint32_t(*)(Cpu*,uint32_t,std::initializer_list<uint32_t>);
bool declare(uint16_t key,const std::string& archive,unsigned frames,std::string& why,const std::string& asset_digest={});
// a held model is declared (start-up): only then does guestmods allocate the record and hook
bool declared();
size_t checkpoint();
void rollback(size_t) noexcept;
void configure(uint32_t memory,void(*original)(Cpu*,uint32_t),Invoke);
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
bool owns_archive(const std::string& name);
// Return true only when handled; foreign archives leave CPU untouched.
bool resource_table(Cpu*);
bool archive_wrapper(Cpu*);
bool backing_archive_wrapper(Cpu*);
void progress_loaded();
void request(uint16_t key); // successful Use only; no native calls
void tick(Cpu*); // one game logic tick, never Draw
void state_loaded(); // validates guest record on next tick, never replays Use
void reset();

// Item get ("hold_up", docs/mod-sdk-v3.md): when Link gets an item with a held model, the game's own
// get-item event shows it the way it shows the game's items. A demo item of the stand-in game item
// (story_treasure_chest.h kStandIn, argument flag 1: its Delete gives nothing) is created at Link,
// it orders the game's item event (fopAcM_orderItemEvent, as d_a_item does) and becomes the event's
// item partner; Link plays his item-get pose (daPy_lk_c::dProcGetItem) and shows the get message,
// whose text is the mod item's (arm_text). The demo item's own model is never drawn: the held model
// takes its place, with the demo item's matrix (it turns over Link's head, like the game's). When
// DEFAULT_GETITEM ends, the event is reset and the demo item deleted. The demo's state is kept in the
// guest record (kDemo...), so a full save state taken during it finishes the same way.
// hold_up: queue one (game thread or any thread); false: no model for the key (or not installed).
bool hold_up(uint16_t key);
// A chest's own get-item demo (its stand-in) shows the held model instead (on), until its item is given (off).
// Without a declared held model (nothing installed) both do nothing.
void chest_hold_up(uint16_t key, bool on);
// Arms (on) or disarms the stand-in's get message with the item's name and description; false: can't now.
void set_text(bool (*arm)(uint16_t key, bool on));
bool demo_active();  // tests and the caller: a hold-up is queued or running
// the demo's part of the guest record: after the model's (its light settings, a dKy_tevstr_c of 0x1C8
// bytes, end at 512 + 0x1C8 = 968)
inline constexpr uint32_t kDemoOffset = 976;
// daDitem_Draw(item), asked first by the demo item's draw hook (story_item_get_icon.h): the stand-in's demo
// item while a held model is shown in its place draws that model (or nothing until it is ready) and
// returns true; false for every other demo item (the caller draws it). Safe without a declared model.
bool draw_demo_item(Cpu*);
namespace hooks {void hand(Cpu*);void draw(Cpu*);}
}
