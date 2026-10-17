// Mod items (docs/mod-sdk-v2.md, "Items"): a story mod declares items in its manifest ("items"); Link
// gets them from the mod's quests (a letter encloses one, a talk gives one, a quest's reward) or from
// the mod's code (wwhd_item_give). They live in the Delivery Bag: on its extra pages (the bag page,
// a layout of the port), or in one of the game's own eight places when the player puts one there.
//
// What a save file keeps of them is in its story block (story_items_core.h); nothing is written to
// the game's item bytes or bits, so removing the mods leaves a normal save. An item the game has to
// draw or use (one of its own bag places, X/Y/R) gets a game item number from the port's pool, bound
// to it in the story block; the port answers the game's tables for those numbers (install()).
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "story_items_core.h"
#include "story_prop_rewards.h"
#include "story_manifest.h"

struct Cpu;

namespace story_items {

// Results of the guest services (wwhd_guest.h: WWHD_ITEM_*): the new count, or < 0
inline constexpr int32_t kResultInvalid = -1, kResultNoRoom = -2, kResultNotEnough = -3;

struct Item {
    uint16_t key = 0;  // item_key(mod, id)
    std::string mod, id, name, description;
    std::string icon_path;  // the PNG in the package (absolute)
    uint64_t icon_hash = 0;
    uint8_t stack = 1;
    story_notebook::ManifestItem::Use use = story_notebook::ManifestItem::Use::None;
    std::string use_quest;
    unsigned use_step = 0;
    std::string held_model;
    unsigned held_frames = 90;
    bool usable() const { return use != story_notebook::ManifestItem::Use::None; }
};

// The registry: the items of the enabled mods' manifests (story_notebook::declare). False (and why)
// when the item can't be used: a bad icon, or its key is another item's.
bool define(const std::string& mod, const std::string& package_dir, const story_notebook::ManifestItem& m, std::string& why);
void set_model_presenter(std::function<void(uint16_t)> presenter);
std::vector<Item> installed();  // in declaration order (mod by mod)
std::optional<Item> find(uint16_t key);
std::optional<Item> find(const std::string& mod, const std::string& id);

// Startup-only world batch checkpoints. No native instances/hooks may consume registrations
// until the batch succeeds. Rollback never recycles opaque handles or changes loaded Book bytes.
PropMarkerCheckpoint prop_marker_checkpoint();
bool rollback_prop_markers(PropMarkerCheckpoint checkpoint); // includes enemy markers in the same startup batch
// Internal native-prop claims; callbacks run serially on the game logic thread.
PropMarkerHandle register_prop_marker(const std::string& mod, const std::string& actor_id,
                                     const std::string& broken_flag, std::string& why);
bool prop_broken(PropMarkerHandle handle);
PropClaimStatus claim_prop_drops(PropMarkerHandle handle, const std::string& mod,
                                const std::vector<PropDrop>& drops, bool caption = true);

// Enemy terminal adapters alone decide when to claim; Delete/unload/Create failure never imply defeat.
EnemyMarkerHandle register_enemy_marker(const std::string& mod, const std::string& actor_id,
                                       const std::string& defeat_flag, std::string& why);
bool enemy_defeated(EnemyMarkerHandle handle);
EnemyClaimStatus claim_enemy_drops(EnemyMarkerHandle handle, const std::string& mod,
                                  const std::vector<PropDrop>& drops, bool caption = true);

// The loaded save file (none: 0 / nullopt)
uint8_t count(uint16_t key);
uint8_t place(uint16_t key);
uint32_t number(uint16_t key);                // its bound pool number, 0 if none
std::optional<Item> by_number(uint32_t number);  // the installed item bound to a pool number
uint16_t at_game_place(unsigned place);       // the item the player put in one of the game's places (0..7), 0 if none
uint16_t on_button(unsigned button);          // the installed item on an item button (0..4), 0 if none
std::vector<uint16_t> owned();                // the installed items Link has, in the save's record order
std::vector<uint16_t> unplaced();             // ... of them the ones without a place (the pages were full)
uint32_t number_at_game_place(unsigned place); // the pool number of the item in a game place (0..7), bound now if needed; 0 if none
// The bag's soft cap: how many pages new items fill (default kPages, 1..kMaxPages). Places beyond it
// stay valid in a save.
void set_page_limit(unsigned pages);
unsigned page_limit();
// The icon frame of an item in the game's icon layout (the port's extended BtnItemIcon_00), -1 without it
void set_frame_source(int (*frame)(uint16_t key));
int frame(uint16_t key);
bool notebook_on_button(unsigned button);     // the story notebook is on that button (kRecordNotebookButtons)
bool assign_notebook(int button);             // puts the notebook on a button (0..4; takes any item off it), -1 takes it off all

// wwhd_item_give / take / count / used, and the manifest's gives: the new count, or < 0. A give
// shows the item's caption with the game's small get jingle (present()).
// caption false: no get caption (a get the game's own message already names, e.g. a treasure chest)
int32_t give(const std::string& mod, const std::string& id, uint32_t n, const char* how, bool caption = true);
int32_t take(const std::string& mod, const std::string& id, uint32_t n);
int32_t count(const std::string& mod, const std::string& id);
int32_t used(const std::string& mod, const std::string& id);  // times used (use action "event") since the last call

// What the bag page (and the game's places) can do with an item. False and reason() when refused.
enum class Action { Info, Use, Assign, MoveToGamePlace, MoveToPage };
bool act(uint16_t key, Action action, int arg = 0);  // Assign: arg = button (0..4), -1 takes it off; Move*: the place
std::string reason();

// The game thread's turn (the notebook's controller read, while nothing of the port has the
// controller): a pending get caption is shown and its jingle played. True when it showed one.
using ShowInfo = bool (*)(const std::string& title, const std::string& text, const char* how);
void set_presenter(ShowInfo show);
bool present(Cpu* c);
// What the use actions call (the notebook): open its page, make a quest reach a step
void set_use_calls(bool (*open_notebook)(const char* how), int32_t (*reach)(const std::string& mod, const std::string& quest, uint32_t step));

// The game's item tables for the pool numbers (execItemGet / checkItemGet): installed with the other
// story hooks once a mod declares items.
void note_import();
bool imported();
void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
bool installed_hooks();
void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>));
void reset();  // tests: forget the registry and the session state

namespace hooks {
void exec_item_get(Cpu* c);   // execItemGet(itemNo)
void check_item_get(Cpu* c);  // checkItemGet(itemNo, default)
}  // namespace hooks

}  // namespace story_items
