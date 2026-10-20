// The story notebook: the port's quest journal for story mods (docs/mod-sdk-v2.md, "The story
// notebook"). Link gets it with the Story Notebook package's letter (story_mail.h). Mods fill it through the
// wwhd_quest_* services; the player opens it from the item menu and may track one quest, whose next
// step then shows on the sea chart (story_chart.h).
//
// - The quest list, the save record and the tracked quest: story_notebook_list.h.
// - The page (drawing and navigation): story_notebook_page.h.
// - Nothing of this runs unless a loaded guest mod imports a wwhd_quest_* service.
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include "story_notebook_list.h"
#include "story_npc_catalogue.h"
#include "mod_json.h"
#include "story_notebook_bag.h"

struct Cpu;
namespace input { struct PadState; }

namespace story_notebook {

// Results of the guest services (wwhd_guest.h: WWHD_QUEST_*): 0 not started, 1..254 the step
// reached, 255 done, or an error.
inline constexpr int32_t kResultNew = 0, kResultDone = 255, kResultInvalid = -1, kResultNoRoom = -2;

// wwhd_quest_define: registers the quest (or updates its texts) for this session.
int32_t define(const std::string& mod, const std::string& mod_name, const std::string& quest, const std::string& title,
               const std::string& reward, const std::string& condition);
// wwhd_quest_step: step N reached (0: only the texts), with what to do next and where
int32_t step(const std::string& mod, const std::string& quest, uint32_t step, const std::string& next, const std::string& where);
// wwhd_quest_place / wwhd_quest_no_place: the next step's place on the sea chart
int32_t place(const std::string& mod, const std::string& quest, float x, float z);
int32_t no_place(const std::string& mod, const std::string& quest);
// Read the owner's current resolved quest place (XZ only, not stage/room/height).
// Uses existing mod-owned progress; never writes a record. Outputs stay unchanged on failure.
// The actor adapter must constrain navigation to its current room and ground.
struct QuestDestination;
// Current resolved warp destination (or manifest step 1 before starting).
// Interior places preserve stage/room and spawn point or XYZ; sea places and
// code-owned overrides use sea coordinates. Unavailable/completed has no target.
// Keeps outputs unchanged on failure.
bool place_destination(const std::string& mod,const std::string& quest,QuestDestination& out);
bool place_position(const std::string& mod,const std::string& quest,float& x,float& z);
// wwhd_quest_known, wwhd_quest_portrait: what is known so far; the quest's portrait (package image)
int32_t known(const std::string& mod, const std::string& quest, const std::string& text);
int32_t portrait(const std::string& mod, const std::string& quest, const std::string& path, const std::string& package_dir);
int32_t done(const std::string& mod, const std::string& quest);
// Declarative completion also queues the manifest rupee reward, once.
int32_t done_action(const std::string& mod,const std::string& quest);
// The quests a mod declares in its manifest.json ("quests"), when the mod is loaded: false (logged)
// if the declaration is not accepted.
bool declare(const std::string& mod, const std::string& mod_name, const mods::json::Value& manifest);
// World editor (game thread, while story_idle()): the mod's manifest quests, quest talks, sail lines,
// letters and charts again, as the manifest has them now (docs/mod-editor.md, "Quests, letters, charts
// and items while the game runs"). Only this mod's tables change: its declarations, its residents'
// takeovers and sail lines, its letters on their own (and their texts in the postbox), its charts
// (story_chart::redefine); quests its code defined keep their registration. Nothing of the save file is
// written: progress stays as its records have it. All or nothing: false (why) when the manifest's story
// is not accepted or there is no room (a key collision, more quests than the notebook takes).
struct Redeclared {
    unsigned quests = 0, talks = 0, letters = 0, charts = 0;
    std::vector<std::string> charts_kept;  // charts the manifest dropped: declared until a restart
    bool sail_waits = false;               // the manifest has sail lines, but the boat's hooks were not installed at start-up
};
bool redeclare(const std::string& mod, const std::string& mod_name, const mods::json::Value& manifest, Redeclared& out, std::string& why);
// No page, quest talk, sail line or letter of the story system is running (game thread): a calm moment
// for redeclare(). False with what runs.
bool story_idle(std::string& why);
// The notebook's look from the Story Notebook package: its icon (PNG file) and the model archive its
// setup step built on the player's machine (empty: the game's stand-in model). Before install().
void set_look(const std::string& icon_png, const std::string& model_archive);
// The player's NPC catalogue (tools/story_mods/npc_catalogue.py output): names and spots merged into
// the compiled-in recipes; false if the file isn't there or isn't one
bool load_local_npc_catalogue(const std::string& file);
// the compiled-in recipes, for the generator (place -> recipe text)
std::vector<std::pair<std::string, std::string>> npc_recipes();
void declared_tick();  // what pad() runs a few times a second (tests)
// The game's residents the NPC catalogue knows that are in the game's actor list now (Link's stage
// `stage`), with where they stand: for the world editor. Read-only; on the game thread.
struct LiveVillager {
    Resident resident = kResidentNone;  // npc_catalogue().entry(resident)
    uint32_t actor = 0;
    std::string stage;
    int room = -1;
    float x = 0, y = 0, z = 0;
};
std::vector<LiveVillager> scan_villagers(const std::string& stage);
// wwhd_quest_carry: Link carries (or no longer carries) the quest's letter, shown in the Delivery Bag
int32_t carry(const std::string& mod, const std::string& quest, bool carried);
// wwhd_quest_talk: at that step, while the quest is tracked and its letter carried, the resident says
// the lines (one box), takes the letter, gives the reward (a rupee item or 0) and the quest is done
int32_t talk(const std::string& mod, const std::string& quest, uint32_t step, const std::string& resident, const std::string& lines,
             uint32_t reward);
int32_t state(const std::string& mod, const std::string& quest);
// wwhd_quest_set_flag / wwhd_quest_flag: the quest's 8 flags (n 1..8), saved with the story progress.
// set_flag returns the flag's new value (0/1); flag its value; kResultInvalid / kResultNoRoom as above.
int32_t set_flag(const std::string& mod, const std::string& quest, uint32_t n, bool on);
int32_t flag(const std::string& mod, const std::string& quest, uint32_t n);

// The quests of this session (registration order) and the rows of the current save file.
std::vector<Quest> quests();
std::vector<Row> rows_now(std::vector<Quest>& storage);

// The page. open_page() from the item menu (only when the notebook is owned); the page closes
// itself with B. pad() runs once per GamePad read (game thread), on the live sample: while the page
// is open it moves through the page, draws it and leaves the game an idle controller; buttons held
// when it closes reach the game only after they are released.
bool notebook_owned();
bool open_page(const char* how = "by the port");  // how: for the log
// an item's caption (story_items.h: Info, use "show", a get); false while the page or a caption is open
bool show_info(const std::string& title, const std::string& text, const char* how);
// a manifest quest reaches a step without changing its texts (an item's use action); the step, or < 0
int32_t reach(const std::string& mod, const std::string& quest, uint32_t step);
void close_page();
// the item menu while its Delivery Bag window runs (this logic step or the last), else 0 (the bag pages)
uint32_t delivery_window_menu();
// the item menu is on screen (with the Pro Controller, Plus can close it while the bag window's state still runs)
bool item_menu_shown();
// The Delivery Bag's extra pages (story_bag.h): while `covered` says their page lies over the bag window,
// the port's icons over the window's places (the notebook, a letter, mod items) are not drawn
void set_bag_cover(bool (*covered)());
bool page_open();     // the page or the sealed letter's caption has the controller
bool caption_open();  // the sealed letter's caption (A or a touch on the letter in the Delivery Bag)
void pad(input::PadState& p, Cpu* c = nullptr);  // c: the reading thread (the game's fonts and sounds)
void mute(input::PadState& p);  // other controller reads (the Pro Controller): idle while open, held buttons after

void note_import();  // a module imports a quest service
bool imported();
// After all guest modules are loaded (and after story_mail::install): the item menu hooks.
void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
bool installed();
// After install(): the King of Red Lions' one-time line for the tracked quest (story_sail.h), only when
// a declared quest step has a sail_line; nothing of the boat is hooked otherwise or on a conflict.
void install_sail(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
bool sail_installed();
void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>));
void state_loaded();

// The item menu hooks, exposed for runtime/tools/story_notebook_test.cpp
namespace hooks {
void inventory_item(Cpu* c);  // hdMenuItem_inventoryItem(key)
void icon_frame(Cpu* c);      // hdMenuItem itemForSlot(this, item)
void open_child(Cpu* c);      // hdMenuMain_c::openSelectedChild(index)
void touch_select(Cpu* c);    // hdMenuItem_c::selectTouchEnter()
void unselect_enter(Cpu* c);  // hdMenuItem_c::unselectEnter(): a tap ended
void set_visible(Cpu* c);     // hdMenuItem_c::setVisible(visible)
void execute_window(Cpu* c);  // hdMenuMain_c::executeWindow()
void cursor_float(Cpu* c);    // hdMenuItem_c::beginCursorSelection(point)
void people_message(Cpu* c);  // daNpcPeople_c::getMsg()
void people_next_message(Cpu* c);  // daNpcPeople_c::next_msgStatus()
void people_execute(Cpu* c);  // daNpcPeople_Execute(actor)
}  // namespace hooks
// The notebook's icon in the bag window this frame (story_notebook_bag.h), and why; for tests
IconPlace icon_place();
const char* icon_reason();
void next_frame();  // one logic step on (tests)
void logic_step(uint64_t step);  // the game's logic step (guestmods::frame)
namespace hooks {
void selected_value(Cpu* c);  // hdMenuMain_c::getSelectedItemValue(receiver)
void assign_button(Cpu* c);   // game system: X/Y/R assignment(button, key)
void ship_execute(Cpu* c);        // daShip_c::execute()
void ship_force_message(Cpu* c);  // daShip_c::checkForceMessage()
void ship_talk_init(Cpu* c);      // daShip_c::procTalk_init()
}  // namespace hooks

}  // namespace story_notebook
