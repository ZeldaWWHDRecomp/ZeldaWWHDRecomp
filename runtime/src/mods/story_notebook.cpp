// The story notebook (story_notebook.h, docs/mod-sdk-v2.md "The story notebook"): the services, the
// page driver and the item menu entry.
#include "story_notebook.h"
#include "story_actors.h"
#include "story_scenes.h"
#include "story_notebook_layout.h"
#include "story_notebook_page.h"
#include "story_notebook_caption.h"
#include "story_notebook_bag.h"
#include "story_takeover.h"
#include "story_sail.h"
#include "story_manifest.h"
#include "story_chart.h"
#include "story_items.h"
#include "story_item_buttons.h"
#include "story_mail.h"
#include "story_notebook_game.h"
#include "story_notebook_font.h"
#include "story_notebook_style.h"
#include "story_notebook_art.h"
#include "guest_hud.h"
#include "guest_png.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include "guest_addr.h"
#include "story_store.h"
#include "input.h"
#include "runtime.h"


namespace story_notebook {
namespace {

const uint32_t kSavePtr = GD(0x101F84DC);  // save base pointer (d_com_inf_game)
// the notebook's look, from the Story Notebook package (set_look): its icon's PNG
std::vector<uint8_t> g_icon_png;
const std::string kOwner = "port:story-notebook";  // the HUD list's owner (no mod ID has a colon)
// the notebook's "to continue" line for a step that only works while the quest is tracked

// The item menu (HD decomp MENU-menu_main, hd_screen_menu_main.cpp / hd_screen_menu_item.cpp at
// decomp-readable 02a56a87). The notebook is kept in the Delivery Bag window: its eight places show
// the save's eight delivery-bag entries (keys 0x30..0x37 = dSv_player_bag_item_c::mReserve[0..7], 0xFF
// empty). When the window is built, the first empty place shows the notebook: the save read for that
// key returns kNotebookItem (an item number no game item uses, so no game code knows it), whose icon
// is a letter's. Nothing is written to the save: removing the mods leaves the place empty again.
const uint32_t kInventoryItem = GC(0x02632D94);   // u32 hdMenuItem_inventoryItem(key): the save's item for a menu key
const uint32_t kWindowBuild = GC(0x02692548);     // its call in executeSaveChildren (the bag window opens)
const uint32_t kWindowInit = GC(0x02693C24);      // its call in initializeSaveChildren (the menu opens on a window)
const uint32_t kIconFrame = GC(0x0262F880);       // u32 itemForSlot(item, r4 = item number): the icon frame
const uint32_t kOpenChild = GC(0x0268F060);       // hdMenuMain_c::openSelectedChild(index): A on a place: the item's caption
const uint32_t kTouchSelect = GC(0x02630A64);     // hdMenuItem_c::selectTouchEnter(): a place is touched (an icon is picked up)
const uint32_t kUnselectEnter = GC(0x02630FC4);   // hdMenuItem_c::unselectEnter(): the touch ended without lifting it (a tap)
const uint32_t kSetVisible = GC(0x026333F0);      // hdMenuItem_c::setVisible(visible): shows the item's icon pane
const uint32_t kExecuteWindow = GC(0x0268FF50);   // hdMenuMain_c::executeWindow(): the bag window's state, every frame it is open
const uint32_t kCursorFloat = GC(0x026302B8);     // hdMenuItem_c::beginCursorSelection(point): X/Y/R lifts the item off its place
const uint32_t kSelectedValue = GC(0x0268F420);   // hdMenuMain_c::getSelectedItemValue(receiver): the key X/Y/R would take
const uint32_t kAssignButton = GC(0x02714B0C);    // game system: put a menu key on X/Y/R (controller, button, key)
constexpr uint32_t kFirstDeliveryKey = 0x30, kDeliveryPlaces = 8, kEmpty = 0xFF;
constexpr uint32_t kNotebookItem = 0xA0;          // dItemNo SALVAGE_ITEM_2: unused by the game, no menu mapping
constexpr uint32_t kLetterItem = 0xA1;            // dItemNo SALVAGE_ITEM_3: a quest's letter Link carries (story_takeover.h)
constexpr uint32_t kLetterIcon = 0x25;            // the icon frame of a letter (hidden under the port's envelope)
bool port_item(uint32_t number) { return number == kNotebookItem || number == kLetterItem; }
constexpr uint32_t kNotebookIcon = 0x24;          // the icon frame of the Cabana Deed (a sheet of parchment, the get-item stand-in)
constexpr uint32_t kMenuChildren = 0x48, kItemNumber = 0xEC;  // hdParts_c::mChildren {count, data}; hdMenuItem_c::mSlot
// The notebook's own icon (story_notebook_bag.h): drawn by the port over the bag place, the game's
// sentinel icon pane (hdMenuItem_c::mPaneGroups[0][0], N_ItemIcon_00) kept hidden underneath.
constexpr uint32_t kItemVisible = 0xFC, kItemPaneGroups = 0x8C, kItemLayout = 0x44;  // hdMenuItem_c / hdParts_c
constexpr uint32_t kLayoutLyt = 0x4, kLytRootPane = 0xC;                            // hdPartsLayout_c / hdPartsLyt_c
constexpr uint32_t kPaneParent = 0xC, kPaneTranslate = 0x1C, kPaneScale = 0x34, kPaneFlags = 0x44, kPaneGlobalAlpha = 0x46;
constexpr float kIconSize = 90.0f;  // layout units at scale 1: a little larger than the game's item icons in the bag (about 75)
constexpr float kLayoutWidth = 1280.0f, kLayoutHeight = 720.0f, kLayoutToPicture = 854.0f / 1280.0f;
// the artwork's visual centre (the book, without the float on its left and the bookmark below), as a
// share of the image: drawn on the place's centre
constexpr float kIconAnchorX = 100.4f / 192.0f, kIconAnchorY = 91.9f / 192.0f;
const std::string kIconOwner = "port:story-notebook-icon";
constexpr uint32_t kMenuWindowMode = 0x54, kDeliveryWindow = 2;           // hdMenuMain_c::mWindowMode: 2 the Delivery Bag
constexpr uint32_t kFirstWindowChild = 0x14, kWindowChildren = 8;          // the bag window's eight places (mChildren 0x14..0x1B)
// The notebook's bag place: a child of the menu's open Delivery Bag window, found every frame the
// window runs (executeWindow). Only that place carries the icon: the menu's floating icon (the item
// the cursor or a touch lifts, sMenuIcon) is another item that may show the notebook's number too.
uint32_t g_menu = 0, g_bag_item = 0, g_letter_item = 0;  // game thread
uint32_t g_touch_item = 0;  // the port item a touch picked up, until it ends as a tap or becomes a drag (game thread)
std::atomic<uint64_t> g_step{0};  // the game's logic steps (logic_step)
uint64_t g_window_step = 0;     // the step the bag window last ran
const char* g_icon_reason = "";       // why the icon is (not) shown, for the trace

std::mutex g_mu;
std::vector<Quest> g_quests;  // registered this session, in registration order
// portraits: package images per quest key, created in the HUD store on demand
struct Portrait {
    guestmods::hud::Image pixels;
    guestmods::hud::Handle handle = 0;
    uint64_t generation = 0;
};
std::map<uint16_t, Portrait> g_portraits;
bool g_imported = false, g_installed = false;
std::atomic<bool> g_open{false};
Page g_page;  // game thread
Caption g_caption;  // the sealed letter's caption (game thread)
void (*g_original)(Cpu*, uint32_t) = nullptr;
uint32_t (*g_call)(Cpu*, uint32_t, std::initializer_list<uint32_t>) = nullptr;
const GameUi* g_ui = nullptr;  // the game's font, sounds and window art (the game binary only)

// ---- resident takeovers (story_takeover.h) ----
const uint32_t kPeopleGetMsg = GC(0x022C6C68);    // daNpcPeople_c::getMsg(): which line a Windfall/Outset resident says
const uint32_t kPeopleExecute = GC(0x022C36D0);   // daNpcPeople_Execute(actor): the resident's process step
const uint32_t kPeopleNextMsg = GC(0x022C6108);   // daNpcPeople_c::next_msgStatus(u32* msgNo): what follows a box
const uint32_t kMessageManager = GD(0x101F4B5C);  // the talk's message manager (mSelectNum +0x948: the choice taken)
constexpr uint32_t kSelectNum = 0x948;
constexpr uint32_t kMsgContinues = 0x0F, kMsgEnds = 0x10;  // fopMsgStts
const uint32_t kPlay = GC(0x025200D4);            // the play object
const uint32_t kPresentItem = GC(0x025D7DEC);     // fopAcM_createItemForPresentDemo(pos, item, ...)
const uint32_t kOrderItemEvent = GC(0x025D7B98);  // fopAcM_orderItemEvent(actor): the game's default get-item event
const uint32_t kSearchById = GC(0x025D54C4);      // fopAcM_SearchByID(id, actor**)
const uint32_t kEndCheck = GC(0x0254457C);        // dEvent_manager_c::endCheck(name)
const uint32_t kDefaultGetItem = GD(0x10011EB8);  // "DEFAULT_GETITEM", the game's own string
constexpr uint32_t kPlayEventManager = 0x52C4, kPlayEventFlags = 0x52B8;  // dComIfG_play_c: event manager, mEventFlag
constexpr uint32_t kPeopleMsgList = 0x8A8, kPeopleUbList = 0x8AC, kPeopleNumber = 0x905;  // daNpcPeople_c m734, m738, mNpcNo
constexpr uint32_t kActorPos = 0x314, kEventCommand = 0xF8, kEventCondition = 0xFA;       // fopAc_ac_c current.pos, eventInfo
constexpr uint32_t kActorRoom = 0x326;        // fopAc_ac_c current.roomNo
constexpr uint32_t kPlayStartStage = 0x5134;  // the game info's start stage name (char[8])
constexpr uint16_t kCommandNone = 0, kCommandGetItem = 4, kConditionCanGetItem = 0x0008;
constexpr uint32_t kPlayItemPartner = 0x52A0;     // dComIfGp_event_setItemPartnerId
constexpr uint32_t kPlayPlayer = 0x5B2C;          // dComIfGp_getPlayer(0): Link
const uint32_t kLinkActor = GD(0x1046F0B0) + 0x5B34;  // g_dComIfG_gameInfo.play mpPlayerPtr[0] (as story_chart.cpp)
const Cpu* c_or_zero(const Cpu* c) {
    static const Cpu zero{};
    return c ? c : &zero;
}
// The message our line is shown as: the harbour man's own one-box line (l_msg_ub3_ship_near5), with
// its text replaced for exactly that talk (story_mail::say). Ending after one box (no list).
constexpr uint32_t kCarrierMessage = 0x2CF3;

std::vector<Takeover> g_takeovers;  // registered this session (g_mu)
// quests declared in manifests: the mod and its quest declaration (g_mu)
struct Declared {
    std::string mod;
    uint16_t key;
    ManifestQuest quest;
};
std::vector<Declared> g_declared;
// manifest letters on their own ("letters"): no quest; what they enclose is given once they are read
struct LooseLetter {
    std::string mod;
    ManifestLetter letter;
};
std::vector<LooseLetter> g_letters;
// the talk in progress: talking -> (reward: ordering the get-item event) -> done
struct Talk {
    enum Phase : uint8_t { None, Choosing, Talking, Reward, After } phase = None;
    uint32_t npc = 0, item = 0xFFFFFFFFu;
    Takeover takeover;
    std::vector<Takeover> options;  // Choosing: the quests offered, in box order ("Never mind" last)
    Resident resident = kResidentNone;
    std::u16string text;
    uint64_t since = 0;
} g_talk;
// "Never mind": the resident's next talk is the game's own line (one talk); the one after offers again
Resident g_skip_once = kResidentNone;

// ---- the King of Red Lions' one-time line for the tracked quest (story_sail.h) ----
// HD decomp (wwhd-actors decomp-readable, wwhd_src/d/actor/d_a_ship*.cpp); tww d_a_ship.cpp has the
// same functions. The boat's execute asks checkForceMessage (in every mode but its talk) and orders a
// talk for a forced line while Link rides it (no event running) or stands close in front of it; a
// second chain of forced lines (late game) orders one while Link rides it. A talk command starts
// procTalk_init (riding: after procTalkReady turned the head), whose setInitMessage picks the talk
// hint (riding: keeps mNextMessageNo); procTalk shows mNextMessageNo, and setNextMessage ends the talk
// after the box for the numbers of its end list (no flag set) or chains the next one.
const uint32_t kShipExecute = GC(0x02477A24);       // daShip_c::execute()
const uint32_t kShipForceMessage = GC(0x024731BC);  // daShip_c::checkForceMessage(): one of his forced lines is pending
const uint32_t kShipTalkInit = GC(0x024749DC);      // daShip_c::procTalk_init(): a talk starts (setInitMessage)
const uint32_t kOrderSpeak = GC(0x025D76A8);        // fopAcM_orderSpeakEvent(actor): the game's order for the boat's forced lines
const uint32_t kSeenAngle = GC(0x025D68A0);         // fopAcM_seenActorAngleY(actor, other)
constexpr uint32_t kShipMode = 0x635, kShipState = 0x644, kShipNextMessage = 0x648;  // mCurMode, mStateFlag, mNextMessageNo
constexpr uint32_t kShipTurnAfterTalk = 0x00400000;  // daSFLG_UNK400000_e: the last talk was "too far out": turn back after it
constexpr uint32_t kAttentionPos = 0x390, kAttentionFlags = 0x39C;  // fopAc_ac_c attention_info.position, .flags
constexpr uint32_t kAttnLockonTalk = 0x2, kAttnSpeak = 0x8, kAttnShip = 0x80;
constexpr uint32_t kPlayOrderCount = 0x5290, kPlayEventMode = 0x5292;  // dEvt_control_c (play + 0x51D0) mOrderCount, mMode
constexpr uint32_t kPlayPlayerStatus = 0x5CD8, kPlayMiniGame = 0x5CEA, kRideShip = 0x10000;  // daPyStts0_SHIP_RIDE_e
constexpr uint16_t kCommandTalk = 1, kConditionCanTalk = 1;
// The message the line is shown as: the boat's own one-box line 0x5DF, which setNextMessage ends the
// talk after without setting anything; its text is replaced for exactly that talk (story_mail::say).
constexpr uint32_t kSailCarrier = 0x5DF;
constexpr uint64_t kSailAcceptSteps = 300;  // the talk command came, but no talk started (10 s): given up

std::vector<SailLine> g_sail_lines;  // declared in manifests (g_mu)
bool g_sail_installed = false;
struct Sail {
    enum Phase : uint8_t { Idle, Ordered, Accepted, Talking } phase = Idle;
    uint32_t ship = 0;
    SailLine line;
    std::u16string text;
    uint32_t saved_message = 0;
    bool saved_turn = false;
    uint64_t since = 0, busy = 0;  // busy: the last step the boat talked or an event ran
    bool force_checked = false, force_pending = false;  // this execute
    const char* refusal = nullptr;  // for the trace
} g_sail;

bool trace() {
    static const bool on = [] { const char* e = getenv("WWHD_NOTEBOOK_TRACE"); return e && *e == '1'; }();
    return on;
}

uint32_t save_base() { return ld32(kSavePtr); }
constexpr uint32_t kSaveEventFlags = 0x644, kSaveTime = 0x44;  // dSv_info_c: the event flags; the time of day (degrees)
const uint8_t* event_flags() { return mem::ptr(save_base() + kSaveEventFlags); }
int hour_now() { return int(u32_as_f32(ld32(save_base() + kSaveTime)) / 15.0f); }
bool have_save() { return save_base() != 0; }

// the guest string at addr (up to 8 bytes) is name
bool stage_is(uint32_t addr, const std::string& name) {
    for (size_t i = 0; i < 8; i++) {
        const char ch = char(ld8(addr + uint32_t(i)));
        if (ch != (i < name.size() ? name[i] : '\0')) return false;
        if (!ch) return true;
    }
    return name.size() == 8;
}
// the guest stage name at addr (up to 8 bytes)
std::string stage_name(uint32_t addr) {
    std::string name;
    for (uint32_t i = 0; i < 8; i++) {
        const char ch = char(ld8(addr + i));
        if (!ch) break;
        name += ch;
    }
    return name;
}
story_mail::Book book() { return story_store::book(); }

bool one_line(const std::string& s, size_t max) {
    std::u16string ignored;
    return s.size() <= max && story_mail::append_utf16(s, false, ignored);
}
bool lines(const std::string& s, size_t max) {
    std::u16string ignored;
    return s.size() <= max && story_mail::append_utf16(s, true, ignored);
}

Quest* find(const std::string& mod, const std::string& quest) {
    for (Quest& q : g_quests)
        if (q.mod == mod && q.id == quest) return &q;
    return nullptr;
}
int32_t saved(uint16_t key) {
    if (!have_save()) return kResultNew;
    story_mail::Book b = book();
    return b.usable() ? saved_step(b, key) : kResultNew;
}

// Where the tracked step's resident stands right now (read from the actor while it is loaded, never
// written): the chart's marks follow it. Stale after kLiveSteps logic steps without an update (the
// room was unloaded): the step's catalogue place again.
struct LivePlace {
    uint16_t quest = 0;
    uint8_t step = 0;
    float x = 0, z = 0;
    uint64_t at = 0;  // logic step of the last update
};
LivePlace g_live;
constexpr uint64_t kLiveSteps = 30;

// An interior's chart marker is its declared sea entrance, not the local
// coordinates of a resident visited on the way. Code-owned places keep the
// existing sea-place/following behaviour.
bool interior_place(const Quest&q,uint8_t saved){
    if(q.texts_from_mod)return false;
    const unsigned step=saved?saved:1;
    std::lock_guard lock(g_mu);
    for(const auto&d:g_declared)if(d.key==q.key&&d.mod==q.mod&&d.quest.id==q.id)
        for(const auto&st:d.quest.steps)if(st.n==step)
            return st.has_place&&st.destination.stage!="sea";
    return false;
}

// story_chart's source of the tracked quest's marker (game thread)
bool tracked_marker(float& x, float& z, std::string& label) {
    if (!have_save()) return false;
    std::vector<Quest> storage;
    const std::vector<Row> rows = rows_now(storage);  // with the manifest's place for the current step
    const Quest* q = tracked_place(storage, book());
    static uint16_t no_place_logged = 0;
    if (!q) {
        for (const Row& r : rows)
            if (r.tracked && no_place_logged != r.quest->key) {
                no_place_logged = r.quest->key;
                LOG("[notebook] quest %04X is tracked but its step has no place on the chart%s", r.quest->key,
                    r.quest->hint.empty() ? "" : (" (" + r.quest->hint + ")").c_str());
            }
        return false;
    }
    no_place_logged = 0;
    x = q->x, z = q->z, label = q->title;
    if (!interior_place(*q,saved_step(book(),q->key)) && g_live.quest == q->key && g_live.step == saved_step(book(), q->key) && g_step.load() - g_live.at <= kLiveSteps)
        x = g_live.x, z = g_live.z;
    return true;
}

// the save's delivery-bag entry of a key (the game's own read)
uint32_t reserve(Cpu* c, uint32_t key) {
    Cpu probe = *c;
    probe.r[3] = key;
    g_original(&probe, kInventoryItem);
    return probe.r[3] & 0xFF;
}
// The quest whose letter Link carries (registered this session), 0: none
uint16_t carried_letter() {
    if (!have_save()) return 0;
    story_mail::Book b = book();
    std::lock_guard lock(g_mu);
    for (const Quest& q : g_quests)
        if (carries(b, q.key)) return q.key;
    return 0;
}
// The places the port's items show in now: the first empty delivery places, the notebook first, then
// the carried letter (0: none; if all eight hold game items, they wait for a free place)
struct PortPlaces {
    uint32_t notebook = 0, letter = 0;
};
PortPlaces port_places(Cpu* c) {
    PortPlaces out;
    if (!have_save()) return out;
    const bool notebook = notebook_owned(), letter = carried_letter() != 0;
    for (uint32_t k = kFirstDeliveryKey; k < kFirstDeliveryKey + kDeliveryPlaces && ((notebook && !out.notebook) || (letter && !out.letter)); k++) {
        if (reserve(c, k) != kEmpty || story_items::at_game_place(k - kFirstDeliveryKey)) continue;  // a mod item the player put there
        if (notebook && !out.notebook) out.notebook = k;
        else if (letter && !out.letter) out.letter = k;
    }
    return out;
}
uint32_t notebook_key(Cpu* c) { return port_places(c).notebook; }
bool port_key(Cpu* c, uint32_t key) {
    if (key < kFirstDeliveryKey || key >= kFirstDeliveryKey + kDeliveryPlaces) return false;
    const PortPlaces p = port_places(c);
    return key == p.notebook || key == p.letter;
}
// What a menu key of the Delivery Bag shows, for X/Y/R (story_item_buttons_core.h)
story_item_buttons::MenuKey menu_key(Cpu* c, uint32_t key) {
    story_item_buttons::MenuKey k;
    if (key < kFirstDeliveryKey || key >= kFirstDeliveryKey + kDeliveryPlaces) return k;
    if (const uint16_t item = story_items::at_game_place(key - kFirstDeliveryKey)) {
        const std::optional<story_items::Item> it = story_items::find(item);
        k.item = item, k.usable = it && it->usable();
        return k;
    }
    const PortPlaces p = port_places(c);
    k.notebook = key == p.notebook, k.letter = key == p.letter;
    return k;
}
// An item number the menu may lift towards X/Y/R: not the carried letter, not a mod item without a use action
bool liftable(uint32_t number) {
    if (number == kLetterItem) return false;
    if (story_items::pool_number(number)) {
        const std::optional<story_items::Item> it = story_items::by_number(number);
        return it && it->usable();
    }
    return true;
}

// The sentinel's icon pane of a bag place, or 0
uint32_t icon_pane(uint32_t item) {
    const uint32_t data = ld32(item + kItemPaneGroups + 4);
    return data ? ld32(data) : 0;
}

// The notebook's icon this frame: only while the Delivery Bag window runs (this frame or the one
// before: the window's state runs after the controller read), over the notebook's own bag place.
// Keeps the game's sentinel icon of that place hidden.
IconPlace place_over(uint32_t item, uint32_t number);
IconPlace compute_icon_place() { return place_over(g_bag_item, kNotebookItem); }
// With the Pro Controller the item menu is on the TV only while the game is paused in it: the in-game
// UI's current state (hdIngameUi_c's state machine at +0x10, asked with the game's own
// hdStateMachine current-state function) is ToMenu, Menu or MenuSubWindow. Closing the menu with +
// leaves the bag window's state running unseen. On the GamePad the menu is always shown. Asked while
// the bag window runs (execute_window), kept for the frames drawn until it runs again.
const uint32_t kIngameUi = GD(0x101F8344);
const uint32_t kCurrentState = GC(0x02006478);
const uint32_t kStateToMenu = GD(0x1049EC84), kStateMenu = GD(0x1049ECB0), kStateMenuSubWindow = GD(0x1049ECDC);
bool g_menu_shown = true;
bool menu_shown_now(Cpu* c) {
    if (!input::pro_controller()) return true;
    const uint32_t ui = ld32(kIngameUi);
    if (!ui || !g_call) return true;
    Cpu probe = *c;
    const uint32_t state = g_call(&probe, kCurrentState, {ui + 0x10});
    return state == kStateToMenu || state == kStateMenu || state == kStateMenuSubWindow;
}
bool menu_shown() { return g_menu_shown; }
bool (*g_bag_covered)() = nullptr;  // the Delivery Bag's extra page covers the window (story_bag.h)
IconPlace place_over(uint32_t item, uint32_t number) {
    IconPlace at;
    if (!item || !g_menu) return g_icon_reason = "no bag window yet", at;
    if (g_bag_covered && g_bag_covered()) return g_icon_reason = "the bag's extra page covers the window", at;
    // the window runs once per logic step; frames drawn in between (interpolation, 120 Hz) keep the icon
    if (g_window_step + 1 < g_step.load(std::memory_order_relaxed)) return g_icon_reason = "the bag window is closed", at;
    if (!menu_shown()) return g_icon_reason = "the menu is closed (Pro Controller)", at;
    if (ld32(g_menu + kMenuWindowMode) != kDeliveryWindow) return g_icon_reason = "another bag is open", at;
    if (ld32(item + kItemNumber) != number || !ld8(item + kItemVisible)) return g_icon_reason = "the place shows a game item", at;
    if (number == kNotebookItem && !notebook_owned()) return g_icon_reason = "no notebook in this save file", at;
    const uint32_t icon = icon_pane(item);
    const uint32_t layout = ld32(item + kItemLayout), lyt = layout ? ld32(layout + kLayoutLyt) : 0;
    const uint32_t root = lyt ? ld32(lyt + kLytRootPane) : 0;
    if (!icon || !root) return g_icon_reason = "the place has no panes", at;
    static const bool show_sentinel = [] { const char* e = getenv("WWHD_NOTEBOOK_SHOW_SENTINEL"); return e && *e == '1'; }();
    if (!show_sentinel)  // (test aid: WWHD_NOTEBOOK_SHOW_SENTINEL=1 leaves the stand-in's icon under ours, to check the alignment)
        st8(icon + kPaneFlags, uint8_t(ld8(icon + kPaneFlags) & ~1u));  // the game's sentinel icon stays hidden
    // ... also while the page is open: the page drawn as a game layout fades in over the bag (the HUD's page
    // covered it at once), and the game's selection animation of the tapped place shows the sentinel's
    // stand-in icon (the Cabana Deed's parchment) otherwise
    // the page drawn as a game layout: while it opens or closes the bag place shows the notebook open
    const bool open_book = g_open.load(std::memory_order_relaxed) && number == kNotebookItem && story_notebook_layout::animating() &&
                           !icon_open_png().empty();
    if (g_open.load(std::memory_order_relaxed) && !open_book) {
        hide_floating_notebook_icon();
        return g_icon_reason = "the notebook page is open", at;  // the page covers the menu
    }
    std::vector<PaneNode> chain;
    for (uint32_t pane = icon; pane && chain.size() < 32; pane = ld32(pane + kPaneParent))
        chain.push_back({u32_as_f32(ld32(pane + kPaneTranslate)), u32_as_f32(ld32(pane + kPaneTranslate + 4)),
                         u32_as_f32(ld32(pane + kPaneScale)), u32_as_f32(ld32(pane + kPaneScale + 4)),
                         (ld8(pane + kPaneFlags) & 1) != 0});
    // the GamePad's layout is authored for 1280 x 720 (its screen root pane), shown on the 854 x 480 picture
    at = place_icon(chain, kIconSize, ld8(root + kPaneGlobalAlpha), kLayoutWidth, kLayoutHeight);
    at.x *= kLayoutToPicture, at.y *= kLayoutToPicture, at.size *= kLayoutToPicture;
    if (trace()) {  // the panes from the place's icon up to the screen, once per place
        static uint32_t logged = 0;
        if (logged != item && number == kNotebookItem) {
            logged = item;
            for (uint32_t pane = icon, n = 0; pane && n < 32; pane = ld32(pane + kPaneParent), n++) {
                char words[400];
                int k = 0;
                for (uint32_t o = 0x48; o < 0xA8; o += 4) k += snprintf(words + k, sizeof words - k, " %.1f", u32_as_f32(ld32(pane + o)));
                LOG("[notebook] bag pane %u %08X: t %.1f %.1f s %.2f %.2f size %.0f %.0f flags %02X alpha %u/%u |%s", n, pane,
                    u32_as_f32(ld32(pane + kPaneTranslate)), u32_as_f32(ld32(pane + kPaneTranslate + 4)), u32_as_f32(ld32(pane + kPaneScale)),
                    u32_as_f32(ld32(pane + kPaneScale + 4)), u32_as_f32(ld32(pane + 0x3C)), u32_as_f32(ld32(pane + 0x40)),
                    ld8(pane + kPaneFlags), ld8(pane + 0x45), ld8(pane + kPaneGlobalAlpha), words);
            }
        }
    }
    if (number == kNotebookItem) g_icon_reason = at.shown ? "shown over the notebook's bag place" : "the window is hidden or fading";
    if (trace() && number == kLetterItem && at.shown) {
        static float lx = -1, ly = -1;
        if (std::fabs(at.x - lx) > 1 || std::fabs(at.y - ly) > 1) LOG("[notebook] letter icon at %.1f %.1f (GamePad pixels)", at.x, at.y);
        lx = at.x, ly = at.y;
    }
    return at;
}

// the carried letter's place this frame (an envelope drawn by the port)
IconPlace letter_place() {
    const char* keep = g_icon_reason;
    IconPlace at = place_over(g_letter_item, kLetterItem);
    g_icon_reason = keep;
    return at;
}

// The carried letter's placeholder icon: a cream envelope with its flap and a red wax seal.
std::vector<Command> envelope(float cx, float cy, float size, float alpha) {
    std::vector<Command> out;
    const uint32_t a = uint32_t(alpha * 255.0f + 0.5f);
    const float w = size * 0.80f, h = size * 0.54f, x = cx - w / 2, y = cy - h / 2;
    Command c;
    c.kind = Command::Rect, c.x = x + 2, c.y = y + 3, c.w = w, c.h = h, c.rgba = 0x00000000u | (a * 2 / 5);
    out.push_back(c);
    c.x = x, c.y = y, c.rgba = 0xF4E6C4FFu & (0xFFFFFF00u | a);
    out.push_back(c);
    c.kind = Command::RectOutline, c.rgba = 0x8B6A3E00u | a, c.thickness = std::max(1.0f, size / 40);
    out.push_back(c);
    Command l;
    l.kind = Command::Line, l.rgba = 0x8B6A3E00u | a, l.thickness = std::max(1.0f, size / 45);
    l.x = x, l.y = y, l.w = w / 2, l.h = h * 0.55f;
    out.push_back(l);
    l.x = x + w, l.y = y, l.w = -w / 2, l.h = h * 0.55f;
    out.push_back(l);
    Command seal;
    seal.kind = Command::Circle, seal.x = cx, seal.y = y + h * 0.55f, seal.size = size * 0.09f, seal.rgba = 0xB2392B00u | a;
    out.push_back(seal);
    return out;
}

// Every frame (game thread): the notebook's icon over its bag place, or nothing.
void bag_icon() {
    auto& store = guestmods::hud::store();
    static bool published = false;
    static guestmods::hud::Handle image = 0;
    static uint64_t generation = 0;
    const IconPlace at = compute_icon_place();
    const IconPlace letter = letter_place();
    if (trace()) {  // only when the decision or the place changes
        static const char* last = nullptr;
        static float lx = -1e9f, ly = -1e9f;
        if (g_icon_reason != last || (at.shown && (std::fabs(at.x - lx) > 1 || std::fabs(at.y - ly) > 1))) {
            if (at.shown) LOG("[notebook] bag icon: %s, at %.1f %.1f size %.1f (GamePad pixels)", g_icon_reason, at.x, at.y, at.size);
            else LOG("[notebook] bag icon: %s", g_icon_reason);
        }
        last = g_icon_reason;
        if (at.shown) lx = at.x, ly = at.y;
    }
    // the page drawn as a game layout covers the bag (the HUD's page did by being drawn later)
    const bool open_book = story_notebook_layout::animating() && !icon_open_png().empty();
    if ((story_notebook_layout::drawing() && !open_book) || (!at.shown && !letter.shown)) {
        if (published) store.drop(kIconOwner);
        published = false;
        return;
    }
    static guestmods::hud::Handle open_image = 0;
    if (!image || generation != store.state_generation()) {  // the icon image, again after a state load
        generation = store.state_generation();
        open_image = 0;
        if (!icon_open_png().empty()) try {
            const auto png = guestmods::hud::decode_png({icon_open_png().data(), icon_open_png().size()});
            open_image = store.create_image(kIconOwner, png.width, png.height, png.width * 4, png.rgba.data(), png.rgba.size());
        } catch (const std::exception&) {
            open_image = 0;
        }
        if (g_icon_png.empty()) return;  // no Story Notebook package look: no icon
        try {
            const auto png = guestmods::hud::decode_png({g_icon_png.data(), g_icon_png.size()});
            image = store.create_image(kIconOwner, png.width, png.height, png.width * 4, png.rgba.data(), png.rgba.size());
        } catch (const std::exception&) {
            image = 0;
        }
        if (!image) return;
    }
    // The menu is on the GamePad; with the Pro Controller the game shows it on the TV (the same
    // 854 x 480 layout, scaled to the TV's 1280 x 720).
    const bool tv = input::pro_controller();
    const float k = tv ? 1280.0f / 854.0f : 1.0f;
    const guestmods::hud::Handle list = store.begin(kIconOwner, tv ? 0 : 1);
    if (!list) return;
    if (at.shown) {
        Command c;
        c.x = (at.x - at.size * kIconAnchorX) * k, c.y = (at.y - at.size * kIconAnchorY) * k, c.w = c.h = at.size * k;
        c.rgba = 0xFFFFFF00u | uint32_t(at.alpha * 255.0f + 0.5f);
        store.picture(kIconOwner, list, open_book && open_image ? open_image : image, c);
    }
    if (letter.shown)  // placeholder art: a sealed envelope
        for (Command& c : envelope(letter.x * k, letter.y * k, letter.size * k, letter.alpha)) store.append(kIconOwner, list, std::move(c));
    store.commit(kIconOwner, list);
    published = true;
}

// ---- the page in the game's look (story_notebook_font.h, story_notebook_style.h) ----
// The game's fonts, read glyph by glyph as the page needs them; the line atlas; the menu art and the
// boxes composed from it. All host-side; a state load keeps them (the game's resources don't change),
// only the HUD images are created again.
struct Look {
    font::Face main, fallback, icons;  // CKingMain, CKingMsg (characters CKingMain lacks), the button icons
    bool font_failed = false, icons_probed = false;
    font::Atlas atlas;
    guestmods::hud::Handle atlas_image = 0;
    uint64_t atlas_generation = 0, store_generation = 0;
    style::Art art;
    bool art_tried = false, art_ok = false;
    struct Box {
        guestmods::hud::Handle handle = 0;
        uint64_t store_generation = 0;
        std::shared_ptr<guestmods::hud::Image> image;
    };
    std::map<std::string, Box> boxes;  // "name:WxH" in page units
    font::Prompts prompts;
    uint64_t fonts_key = 0;  // changes when glyphs are added
} g_look;
constexpr float kBoxResolution = 0.75f;  // composed boxes: pixels per page unit (the GamePad shows 0.67)
constexpr float kFontScale = 1.05f;
constexpr float kAtlasScale = 0.62f;     // before the screen's pixels are known: the font's lines at 62 % (46 of its 74 pixels)
float g_close_time = 1.0f;  // the closing animation: 0..1 after the page closed
std::vector<Command> g_closing;  // the last page drawn (resolved), faded out while closing
std::vector<guestmods::hud::Handle> g_closing_images;
uint32_t g_closing_screen = 1;
Sound g_pending_sound = Sound::None;  // the page opened from a hook: its sound with the next controller read

float game_measure(const std::string& text, float size) {
    const int h = g_look.main.cell_height;
    return h ? float(font::Atlas::line_width(text, g_look.main, &g_look.fallback, h)) * size * kFontScale / float(h) : 0.0f;
}

// the glyphs the page's lines need, from the game's fonts (game thread, with a CPU for the game's calls)
void load_glyphs(Cpu* c, const std::vector<std::string>& lines) {
    if (!g_ui || !g_ui->glyphs || !c || g_look.font_failed) return;
    std::vector<uint32_t> want;
    for (const std::string& l : lines)
        for (uint32_t cp : font::codes(l))
            if (!font::Atlas::button(cp) && !font::style_code(cp) && !g_look.main.glyphs.count(cp) && !g_look.main.missing.count(cp) &&
                std::find(want.begin(), want.end(), cp) == want.end())
                want.push_back(cp);
    if (!g_look.main.cell_height) want.push_back('?');  // the stand-in for characters no font has
    bool message_wanted = false;
    for (const std::string& l : lines)
        if (font::has_style(l, font::kMessage))
            for (uint32_t cp : font::codes(l)) message_wanted |= !font::style_code(cp) && !font::Atlas::button(cp) && !g_look.fallback.glyphs.count(cp) &&
                                                                 !g_look.fallback.missing.count(cp);
    if (!want.empty() || message_wanted) {
        const size_t before = g_look.main.glyphs.size();
        if (!g_ui->glyphs(c, 0, want, g_look.main) || !g_look.main.ready()) {
            g_look.font_failed = true;
            LOG("[notebook] the game's menu font can't be read: the page uses the overlay font");
            return;
        }
        std::vector<uint32_t> lacking;
        for (uint32_t cp : want)
            if (g_look.main.missing.count(cp) && !g_look.fallback.glyphs.count(cp) && !g_look.fallback.missing.count(cp)) lacking.push_back(cp);
        // the lines in the message font: all their characters from it
        for (const std::string& l : lines)
            if (font::has_style(l, font::kMessage))
                for (uint32_t cp : font::codes(l))
                    if (!font::Atlas::button(cp) && !font::style_code(cp) && !g_look.fallback.glyphs.count(cp) && !g_look.fallback.missing.count(cp) &&
                        std::find(lacking.begin(), lacking.end(), cp) == lacking.end())
                        lacking.push_back(cp);
        if (!lacking.empty()) g_ui->glyphs(c, 1, lacking, g_look.fallback);
        if (g_look.main.glyphs.size() != before || !lacking.empty()) g_look.fonts_key++;
        if (before == 0) {
            g_text_measure = game_measure;
            LOG("[notebook] the page uses the game's menu font (cell height %d)", g_look.main.cell_height);
        }
    }
    if (!g_look.icons_probed) {  // the button pictures: the font that has the game's A/B icons
        g_look.icons_probed = true;
        for (int slot : {1, 4, 0}) {
            font::Face f;
            if (g_ui->glyphs(c, slot, {font::kButtonA, font::kButtonB, font::kButtonA+2, font::kButtonA+3}, f) && f.glyphs.count(font::kButtonA) && f.glyphs.count(font::kButtonB)) {
                g_look.icons = std::move(f);
                g_look.fonts_key++;
                LOG("[notebook] the button pictures come from font slot %d (%s)", slot,
                    g_look.icons.glyphs[font::kButtonA].colour ? "colour icons" : "coverage");
                break;
            }
        }
        if (!g_look.icons.ready()) LOG("[notebook] no game font has the A/B icons: the page draws its own");
    }
}

// the menu art, once (it is read from the item menu's layout, open under the page)
bool pieces_ready() {
    const style::Art& a = g_look.art;
    return a.sheet.width && a.card.width && a.underline.width && a.uplate.width && a.bands[0].width && a.bands[1].width && a.bands[2].width;
}
// The page's painted art (user artwork, story_notebook_art.h): read once from the folder
// WWHD_NOTEBOOK_ART names (outside the repository): frame.png, banner.png (the list), frame_notabs.png
// (optional: the detail page's frame) and sheet.png,
// card.png, header.png, underline.png, band_cream.png, band_blue.png, band_green.png, plate.png
// (the detail page). WWHD_NOTEBOOK_HEADER=0 leaves the header band out.
bool g_header = true;
void load_user_art() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    const char* dir = getenv("WWHD_NOTEBOOK_ART");
    if (!dir || !*dir) return;
    const char* header = getenv("WWHD_NOTEBOOK_HEADER");
    g_header = !(header && *header == '0');
    auto read = [&](const char* file, game_glyph::Bitmap& out, int height, bool crop = true) {
        const std::string path = std::string(dir) + "/" + file;
        try {
            std::ifstream f(path, std::ios::binary);
            if (!f) return;
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            const auto png = guestmods::hud::decode_png({bytes.data(), bytes.size()});
            game_glyph::Bitmap b;
            b.width = int(png.width), b.height = int(png.height), b.rgba = png.rgba;
            b = art::clean_edges(b, 96, crop);
            if (b.height > height) b = art::resized(b, int(std::lround(b.width * float(height) / b.height)), height);
            out = std::move(b);
        } catch (const std::exception& e) {
            LOG("[notebook] %s not read: %s", path.c_str(), e.what());
        }
    };
    style::Art& a = g_look.art;
    read("frame.png", a.frame, 1044, false);
    read("banner.png", a.banner, 160);
    read("sheet.png", a.sheet, 800);
    read("card.png", a.card, 640);
    read("header_v2.png", a.header, 240);  // the band without the Triforce, when the art has it
    if (!a.header.width) read("header.png", a.header, 240);
    read("underline.png", a.underline, 48);
    read("band_cream.png", a.bands[0], 120);
    read("band_blue.png", a.bands[1], 120);
    read("band_green.png", a.bands[2], 120);
    read("plate.png", a.uplate, 140);
    // the detail page's frame: the frame without its tab plates and close slot when the art has it
    // (frame_notabs.png, the same picture painted without them), else the frame with those painted over
    // by a stretched strip of its green (art::detail_frame, a visible patch)
    game_glyph::Bitmap notabs;
    read("frame_notabs.png", notabs, 1044, false);
    a.detail_frame = notabs.width ? std::move(notabs) : art::detail_frame(a.frame);
    LOG("[notebook] painted art from %s: frame %s, banner %s, detail pieces %s, header band %s", dir, a.frame.width ? "yes" : "no",
        a.banner.width ? "yes" : "no", pieces_ready() ? "yes" : "no", a.header.width && g_header ? "on" : "off");
}

// The sea chart's own Link icon, once the chart menu has been shown this session
void load_chart_icons() {
    static uint32_t read_from = 0;
    const uint32_t chart = story_chart::sea_chart_menu();
    if (!chart || chart == read_from || !g_ui || !g_ui->chart_icons) return;
    read_from = chart;
    if (g_ui->chart_icons(chart, g_look.art)) LOG("[notebook] the mini chart uses the sea chart's Link icon");
}

void load_art() {
    load_user_art();
    load_chart_icons();
    if (g_look.art_tried || !g_ui || !g_ui->art || !g_menu) return;
    g_look.art_tried = true;
    std::string why;
    style::Art keep = g_look.art;  // the painted art and the chart icons stay
    g_look.art_ok = g_ui->art(g_menu, g_look.art, why);
    {  // the painted pieces stay
        style::Art& n = g_look.art;
        n.frame = std::move(keep.frame), n.banner = std::move(keep.banner), n.detail_frame = std::move(keep.detail_frame);
        n.sheet = std::move(keep.sheet), n.card = std::move(keep.card), n.header = std::move(keep.header);
        n.underline = std::move(keep.underline), n.uplate = std::move(keep.uplate);
        for (int i = 0; i < 3; i++) n.bands[i] = std::move(keep.bands[i]);
    }
    g_look.art.link_face = std::move(keep.link_face), g_look.art.ship = std::move(keep.ship);
    if (g_look.art_ok) style::trim_plates(g_look.art);
    if (!g_look.art_ok) LOG("[notebook] the menu's window art can't be read (%s): the page draws plain boxes", why.c_str());
    else LOG("[notebook] the page uses the menu's window art%s%s", why.empty() ? "" : "; ", why.c_str());
}

// The page's commands as HUD commands: boxes and text become pictures of their images (handles
// alongside; 0 for the commands that aren't pictures)
// `screen`: where the page is drawn; its text is composed at that screen's real pixel size.
std::vector<Command> render(std::vector<Command> cmds, std::vector<guestmods::hud::Handle>& images, uint32_t screen) {
    auto& store = guestmods::hud::store();
    const uint64_t generation = store.state_generation();
    cmds = style::resolve(cmds, &g_look.art);  // each box uses its art when that piece was read
    // text: the line atlas, rebuilt when a line is new, the fonts grew or the prompts changed
    const bool font = g_look.main.ready();
    // screen pixels per page unit (the page is laid out in TV units; the GamePad shows 854 / 1280 of them)
    const float pixels = store.pixels(screen) * (screen == 1 ? 854.0f / 1280.0f : 1.0f);
    static float traced = -1;
    if (trace() && std::fabs(pixels - traced) > 0.01f) LOG("[notebook] text composed at %.2f screen pixels per page unit (screen %u)", traced = pixels, screen);
    if (font) {
        const std::vector<std::string> lines = font::lines_of(cmds, kFontScale, pixels);
        if (!g_look.atlas.holds(lines, g_look.prompts, g_look.fonts_key)) {
            g_look.atlas.build(lines, g_look.main, &g_look.fallback, g_look.icons.ready() ? &g_look.icons : nullptr, g_look.prompts, g_look.fonts_key,
                               kAtlasScale);
            if (g_look.atlas_image) store.release(kOwner, g_look.atlas_image);
            g_look.atlas_image = 0;
        }
        if (!g_look.atlas_image || g_look.store_generation != generation) {
            const auto img = g_look.atlas.image();
            g_look.atlas_image = img ? store.create_image(kOwner, img->width, img->height, img->width * 4, img->rgba.data(), img->rgba.size()) : 0;
            g_look.store_generation = generation;
        }
    }
    cmds = font::to_pictures(cmds, font && g_look.atlas_image ? &g_look.atlas : nullptr, kFontScale, pixels);
    images.assign(cmds.size(), 0);
    for (size_t i = 0; i < cmds.size(); i++) {
        Command& c = cmds[i];
        if (c.kind != Command::Picture) continue;
        if (c.text.rfind("art:", 0) != 0) {  // a line of text
            images[i] = g_look.atlas_image;
            c.text.clear();
            continue;
        }
        int bw, bh;  // the size the box was laid out at (the animations scale only the picture)
        style::box_size(c.text, bw, bh);
        const std::string name = "nb:" + style::box_name(c.text).substr(4);
        const float res = name == "nb:window" || name == "nb:panel" || name == "nb:info" || name.rfind("nb:plate", 0) == 0 || name.rfind("nb:tab", 0) == 0
                              ? kBoxResolution : 1.0f;  // the game's art at 75 %, the painted pieces at full size
        const int w = int(std::lround(bw * res)), h = int(std::lround(bh * res));
        const std::string key = name + ":" + std::to_string(w) + "x" + std::to_string(h);
        Look::Box& box = g_look.boxes[key];
        if (!box.image) box.image = style::compose(g_look.art, name, w, h);
        if (box.image && (!box.handle || box.store_generation != generation)) {
            box.handle = store.create_image(kOwner, box.image->width, box.image->height, box.image->width * 4, box.image->rgba.data(),
                                            box.image->rgba.size());
            box.store_generation = generation;
        }
        images[i] = box.handle;
        c.text.clear();
    }
    return cmds;
}

void append_all(guestmods::hud::Handle list, std::vector<Command>& cmds, const std::vector<guestmods::hud::Handle>& images) {
    auto& store = guestmods::hud::store();
    for (size_t i = 0; i < cmds.size(); i++) {
        if (cmds[i].kind == Command::Picture) {
            if (images[i]) store.picture(kOwner, list, images[i], std::move(cmds[i]));
            continue;
        }
        store.append(kOwner, list, std::move(cmds[i]));
    }
}

void publish(const std::vector<Row>& all, const std::vector<Command>* caption = nullptr) {
    auto& store = guestmods::hud::store();
    // the page as a game layout (story_notebook_layout.h): then the HUD draws only its portraits
    const bool by_layout = !caption && story_notebook_layout::update(g_page, all);
    if (!g_page.open() && !caption) {
        store.drop(kOwner);
        return;
    }
    // only on the screen with the game's item menu (the GamePad, or the TV with the Pro Controller)
    const uint32_t screen = menu_screen(input::pro_controller());
    const float k = screen == 1 ? 854.0f / 1280.0f : 1.0f;
    const guestmods::hud::Handle list = store.begin(kOwner, screen);
    if (!list) return;
    std::vector<guestmods::hud::Handle> images;
    std::vector<Command> cmds = by_layout ? std::vector<Command>{} : render(caption ? *caption : g_page.draw(all), images, screen);
    g_closing = cmds, g_closing_images = images, g_closing_screen = screen;  // for the closing animation
    fit_to_screen(cmds, screen);
    append_all(list, cmds, images);
    const std::vector<Row> rows = g_page.shown(all);
    // portraits: in the rows that show them, and the sender's on the detail page (the painted frame)
    std::vector<std::pair<unsigned, const Quest*>> portraits;
    if (!caption && !by_layout && g_page.mode() == Page::Mode::List)  // the layout has its own portraits
        for (unsigned i = 0; i < kRowsShown && g_page.top() + i < rows.size(); i++) portraits.push_back({i, rows[g_page.top() + i].quest});
    if (!caption && g_page.mode() == Page::Mode::Detail && g_page.scene().framed)
        for (const Row& r : rows)
            if (r.quest->key == g_page.selected_key()) portraits.push_back({~0u, r.quest});
    {
        const uint64_t generation = store.state_generation();
        for (const auto& [i, quest] : portraits) {
            const Quest& q = *quest;
            auto it = g_portraits.find(q.key);
            if (q.portrait.empty() || it == g_portraits.end()) continue;
            Portrait& p = it->second;
            if (!p.handle || p.generation != generation) {
                p.handle = store.create_image(kOwner, p.pixels.width, p.pixels.height, p.pixels.width * 4, p.pixels.rgba.data(),
                                              p.pixels.rgba.size());
                p.generation = generation;
            }
            if (!p.handle) continue;
            float cx, cy, r, alpha = 1;
            if (i == ~0u) g_page.portrait_place(cx, cy, r);
            else g_page.emblem_place(i, cx, cy, r);
            float size = 2 * r;
            g_page.place_portrait(cx, cy, size, alpha);
            Command c;
            c.x = (cx - size / 2) * k, c.y = (cy - size / 2) * k, c.w = c.h = size * k;
            c.rgba = 0xFFFFFF00u | uint32_t(std::lround(255.0f * alpha));
            store.picture(kOwner, list, p.handle, c);
        }
    }
    store.commit(kOwner, list);
}

// After the page closed: the last page shrinks a little and fades out (0.15 s), then it is gone.
void publish_closing(float dt) {
    auto& store = guestmods::hud::store();
    story_notebook_layout::update_closed();  // the layout's own closing animation (its page commands aren't kept)
    if (g_close_time >= 1.0f) return;
    g_close_time = std::min(1.0f, g_close_time + dt / 0.15f);
    if (g_close_time >= 1.0f || g_closing.empty()) {
        store.drop(kOwner);
        g_closing.clear();
        return;
    }
    const guestmods::hud::Handle list = store.begin(kOwner, g_closing_screen);
    if (!list) return;
    std::vector<Command> cmds = g_closing;
    const float t = g_close_time;
    transform(cmds, 0, kPageX + kPageW / 2, kPageY + kPageH / 2, 1.0f - 0.05f * t, 0, 0, 1.0f - t);
    fit_to_screen(cmds, g_closing_screen);
    append_all(list, cmds, g_closing_images);
    store.commit(kOwner, list);
}

// The game's menu sound for what the page did. Only while the page has the controller and no game
// event runs (a talk, a cutscene): the game's own sounds are never played over or replaced.
const char* sound_label(Sound s) {
    switch (s) {
    case Sound::Cursor: return "SE_MENU_CURSOR";
    case Sound::Decide: return "SE_MENU_DECIDE";
    case Sound::Cancel: return "SE_MENU_CANCEL";
    case Sound::Tab: return "SE_MENU_TAB_SWITCH";
    case Sound::Open: return "SE_MENU_MINI_WINDOW_OPEN";
    case Sound::Close: return "SE_MENU_MINI_WINDOW_CLOSE";
    case Sound::TrackOn: return "SE_MENU_ITEM_EQUIP_ON";
    case Sound::TrackOff: return "SE_MENU_ITEM_EQUIP_OFF";
    case Sound::None: break;
    }
    return nullptr;
}
constexpr uint32_t kPlayEventRun = 0x5292;  // dComIfGp_event_runCheck(): the event control's run flag
void play(Cpu* c, Sound s) {
    const char* label = sound_label(s);
    if (!label || !c || !g_ui || !g_ui->sound || !g_call || !have_save()) return;
    Cpu probe = *c;
    const uint32_t play_object = g_call(&probe, kPlay, {});
    if (!play_object || ld8(play_object + kPlayEventRun)) {
        if (trace()) LOG("[notebook] sound %s not played: a game event runs", label);
        return;
    }
    if (trace()) LOG("[notebook] sound %s", label);
    g_ui->sound(c, label);
}

}  // namespace

IconPlace icon_place() { return compute_icon_place(); }
const char* icon_reason() { return g_icon_reason; }
void next_frame() { g_step.fetch_add(1, std::memory_order_relaxed); }
void logic_step(uint64_t step) { g_step.store(step, std::memory_order_relaxed); }

int32_t define(const std::string& mod, const std::string& mod_name, const std::string& quest, const std::string& title,
               const std::string& reward, const std::string& condition) {
    if (!story_mail::valid_id(quest) || title.empty() || !one_line(title, kMaxTitleBytes) || !one_line(reward, kMaxRewardBytes) ||
        !one_line(condition, kMaxConditionBytes))
        return kResultInvalid;
    const uint16_t key = quest_key(mod, quest);
    {
        std::lock_guard lock(g_mu);
        if (Quest* known_quest = find(mod, quest)) {
            known_quest->title = title, known_quest->reward = reward, known_quest->condition = condition;
        } else {
            unsigned mine = 0;
            for (const Quest& q : g_quests) {
                if (q.key == key) return kResultInvalid;  // a 16-bit key collision with another quest
                mine += q.mod == mod;
            }
            if (mine >= kMaxQuestsPerMod || g_quests.size() >= kMaxQuests) return kResultNoRoom;
            Quest q;
            q.mod = mod, q.mod_name = mod_name.empty() ? mod : mod_name, q.id = quest, q.key = key;
            q.title = title, q.reward = reward, q.condition = condition;
            g_quests.push_back(std::move(q));
            LOG("[notebook] %s: quest \"%s\" registered", mod.c_str(), quest.c_str());
        }
    }
    return saved(key);
}

int32_t step(const std::string& mod, const std::string& quest, uint32_t n, const std::string& next, const std::string& where) {
    if (!story_mail::valid_id(quest) || n > kMaxStep || !one_line(next, kMaxNextBytes) || !one_line(where, kMaxWhereBytes))
        return kResultInvalid;
    uint16_t key;
    {
        std::lock_guard lock(g_mu);
        Quest* q = find(mod, quest);
        if (!q) return kResultInvalid;  // wwhd_quest_define first
        q->next = next, q->where = where;
        q->texts_from_mod = true;
        key = q->key;
    }
    if (!n) return saved(key);
    if (!have_save()) return kResultNoRoom;
    story_mail::Book b = book();
    const int before = saved_step(b, key);
    const int now = reach_step(b, key, n);
    if (now < 0) {
        LOG("[notebook] %s: no room for quest \"%s\" in this save file", mod.c_str(), quest.c_str());
        return kResultNoRoom;
    }
    if (now != before) LOG("[notebook] %s: quest \"%s\" step %d reached", mod.c_str(), quest.c_str(), now);
    if (now != before) story_scenes::trigger("quest_step", mod, quest, unsigned(now));
    return now;
}

int32_t place(const std::string& mod, const std::string& quest, float x, float z) {
    if (!story_mail::valid_id(quest)) return kResultInvalid;
    if (!(x >= -story_chart::kSeaHalf && x <= story_chart::kSeaHalf && z >= -story_chart::kSeaHalf && z <= story_chart::kSeaHalf))
        return kResultInvalid;  // NaN too
    std::lock_guard lock(g_mu);
    Quest* q = find(mod, quest);
    if (!q) return kResultInvalid;
    q->has_place = true, q->x = x, q->z = z;
    return saved(q->key);
}

int32_t no_place(const std::string& mod, const std::string& quest) {
    if (!story_mail::valid_id(quest)) return kResultInvalid;
    std::lock_guard lock(g_mu);
    Quest* q = find(mod, quest);
    if (!q) return kResultInvalid;
    q->has_place = false;
    return saved(q->key);
}

bool place_destination(const std::string&mod,const std::string&quest,QuestDestination&out){
    if(mod.empty()||!story_mail::valid_id(quest)||!have_save()||!book().usable())return false;
    std::vector<Quest> storage;
    const auto current=rows_now(storage);
    for(const auto&row:current){const auto&q=*row.quest;
        if(q.mod!=mod||q.id!=quest)continue;
        if((row.status!=Status::New&&row.status!=Status::InProgress)||!q.has_place)return false;
        if(!q.texts_from_mod){
            const auto saved=saved_step(book(),q.key);const unsigned step=saved?saved:1;
            std::lock_guard lock(g_mu);
            for(const auto&d:g_declared)if(d.key==q.key&&d.mod==mod&&d.quest.id==quest)
                for(const auto&st:d.quest.steps)if(st.n==step&&st.has_place&&st.destination.stage!="sea"){
                    out=st.destination;return true;
                }
        }
        // The resolved sea place also covers code-only quests, code overrides
        // and catalogue-derived places that have no explicit destination DTO.
        QuestDestination sea;std::string why;mods::json::Value v;
        v["x"]=double(q.x);v["z"]=double(q.z);
        if(!parse_quest_destination(v,sea,why))return false;
        out=std::move(sea);return true;
    }
    return false;
}

bool place_position(const std::string& mod,const std::string& quest,float& x,float& z) {
    if(mod.empty()||!story_mail::valid_id(quest)||!have_save()||!book().usable())return false;
    std::vector<Quest> storage;
    const auto current=rows_now(storage); // resolves the manifest step from this save's story record
    for(const auto& row:current){const Quest& q=*row.quest;
        if(q.mod!=mod||q.id!=quest)continue;
        if((row.status!=Status::New&&row.status!=Status::InProgress)||!q.has_place||!std::isfinite(q.x)||!std::isfinite(q.z)||std::fabs(q.x)>350000||std::fabs(q.z)>350000)return false;
        x=q.x;z=q.z;return true;
    }
    return false;
}

int32_t known(const std::string& mod, const std::string& quest, const std::string& text) {
    if (!story_mail::valid_id(quest) || !lines(text, kMaxKnownBytes)) return kResultInvalid;
    std::lock_guard lock(g_mu);
    Quest* q = find(mod, quest);
    if (!q) return kResultInvalid;
    q->known = text;
    return saved(q->key);
}

int32_t portrait(const std::string& mod, const std::string& quest, const std::string& path, const std::string& package_dir) {
    if (!story_mail::valid_id(quest) || path.size() > kMaxPortraitBytes || (!path.starts_with("assets/") && !path.starts_with("textures/")) ||
        path.find("..") != std::string::npos)
        return kResultInvalid;
    guestmods::hud::Image pixels;
    try {
        auto png = guestmods::hud::load_png(package_dir, path);
        if (png.width > 256 || png.height > 256) return kResultInvalid;
        pixels.width = png.width, pixels.height = png.height, pixels.rgba = std::move(png.rgba);
    } catch (const std::exception&) {
        return kResultInvalid;
    }
    std::lock_guard lock(g_mu);
    Quest* q = find(mod, quest);
    if (!q) return kResultInvalid;
    if (q->portrait != path) {
        q->portrait = path;
        g_portraits[q->key] = {std::move(pixels), 0, 0};
    }
    return saved(q->key);
}

int32_t done(const std::string& mod, const std::string& quest) {
    if (!story_mail::valid_id(quest)) return kResultInvalid;
    uint16_t key;
    {
        std::lock_guard lock(g_mu);
        Quest* q = find(mod, quest);
        if (!q) return kResultInvalid;
        key = q->key;
    }
    if (!have_save()) return kResultNoRoom;
    story_mail::Book b = book();
    if (saved_step(b, key) == kStepDone) return kResultDone;
    if (finish(b, key) < 0) return kResultNoRoom;
    LOG("[notebook] %s: quest \"%s\" done", mod.c_str(), quest.c_str());
    return kResultDone;
}

int32_t done_action(const std::string&mod,const std::string&quest){
    uint16_t key=0;uint8_t reward=0;
    {std::lock_guard lock(g_mu);for(const auto&d:g_declared)if(d.mod==mod&&d.quest.id==quest){key=d.key;reward=d.quest.reward_item;break;}}
    if(!key)return kResultInvalid;
    if(!have_save())return kResultNoRoom;
    auto b=book();if(saved_step(b,key)==kStepDone)return kResultDone;
    if(reward&&!b.set(story_mail::kRecordQuestActionReward,key,reward))return kResultNoRoom;
    const auto result=done(mod,quest);
    if(result<0&&reward)b.erase(story_mail::kRecordQuestActionReward,key);
    return result;
}

// Rupee-only rewards run on the game thread through the game's own item getter.
// The pending record is snapshot-owned and removed before handing over the item.
void action_rewards(Cpu*c){
    if(!have_save()||!c||!g_call)return;
    auto b=book();
    for(unsigned r=0;r<b.records();++r)if(b.record_kind(r)==story_mail::kRecordQuestActionReward){
        const auto key=b.record_key(r);const auto item=b.record_value(r);
        if(saved_step(b,key)!=kStepDone||item<1||item>6)continue;
        bool enabled=false;{std::lock_guard lock(g_mu);for(const auto&d:g_declared)if(d.key==key&&d.quest.reward_item==item){enabled=true;break;}}
        if(!enabled)continue;
        b.erase(story_mail::kRecordQuestActionReward,key);Cpu probe=*c;g_call(&probe,GC(0x0254DA38),{item});
        LOG("[notebook] quest action reward invoked key=%04X item=%u",unsigned(key),unsigned(item));
    }
}

int32_t state(const std::string& mod, const std::string& quest) {
    if (!story_mail::valid_id(quest)) return kResultInvalid;
    return saved(quest_key(mod, quest));
}

int32_t set_flag(const std::string& mod, const std::string& quest, uint32_t n, bool on) {
    if (!story_mail::valid_id(quest) || n < 1 || n > 8) return kResultInvalid;
    {
        std::lock_guard lock(g_mu);
        if (!find(mod, quest)) return kResultInvalid;
    }
    if (!have_save()) return kResultNoRoom;
    story_mail::Book b = book();
    const uint16_t key = quest_key(mod, quest);
    const uint8_t was = b.get(story_mail::kRecordQuestFlags, key);
    const uint8_t now = on ? uint8_t(was | 1u << (n - 1)) : uint8_t(was & ~(1u << (n - 1)));
    if (now != was) {
        if (now ? !b.set(story_mail::kRecordQuestFlags, key, now) : !b.erase(story_mail::kRecordQuestFlags, key)) return kResultNoRoom;
        LOG("[notebook] %s: quest \"%s\" flag %u %s", mod.c_str(), quest.c_str(), n, on ? "on" : "off");
    }
    return on ? 1 : 0;
}

int32_t flag(const std::string& mod, const std::string& quest, uint32_t n) {
    if (!story_mail::valid_id(quest) || n < 1 || n > 8) return kResultInvalid;
    if (!have_save()) return kResultNoRoom;
    return (book().get(story_mail::kRecordQuestFlags, quest_key(mod, quest)) >> (n - 1)) & 1;
}

std::vector<Quest> quests() {
    std::lock_guard lock(g_mu);
    return g_quests;
}

std::vector<Row> rows_now(std::vector<Quest>& storage) {
    storage = quests();
    if (!have_save()) return {};
    story_mail::Book b = book();
    {
        std::lock_guard lock(g_mu);
        for (Quest& q : storage) {  // the manifest's texts and place for the current step
            if (q.texts_from_mod) continue;
            for (const Declared& d : g_declared) {
                if (d.key != q.key) continue;
                q.steps.clear();
                for (const ManifestStep& st : d.quest.steps) {  // by step number
                    if (q.steps.size() < st.n) q.steps.resize(st.n);
                    q.steps[st.n - 1] = st.text;
                }
                const uint8_t n = saved_step(b, q.key);
                if (n == 0) {  // not started: the start text, and step 1's place ("Not started yet" in the detail)
                    q.next = d.quest.start_text, q.where.clear(), q.has_place = false;
                    for (const ManifestStep& st : d.quest.steps)
                        if (st.n == 1) q.where = st.where, q.has_place = st.has_place, q.x = st.x, q.z = st.z;
                }
                for (const ManifestStep& st : d.quest.steps)
                    if (st.n == n) {
                        q.next = st.text, q.where = st.where, q.has_place = st.has_place, q.x = st.x, q.z = st.z;
                        if (!st.find_chart.empty() && story_chart::state(d.mod, st.find_chart) == story_chart::kResultOwned)
                            q.chart_mod = d.mod, q.chart = st.find_chart;
                        if (st.when.any() && !available(st.when, event_flags(), hour_now(),b)) {
                            q.hint = "Not yet possible: " + st.when.text;  // no ring, no talk until it holds
                            q.has_place = false;
                        }
                    }
            }
        }
    }
    return rows(storage, b);
}

void set_look(const std::string& icon_png, const std::string& model_archive) {
    if (!icon_png.empty()) {
        std::ifstream f(icon_png, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (bytes.size() > 8 && bytes.size() <= 4u << 20) g_icon_png = std::move(bytes);
        LOG("[notebook] the notebook's icon: %s (%s)", icon_png.c_str(), g_icon_png.empty() ? "not read" : "read");
    }
    if (!model_archive.empty()) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(model_archive, ec)) story_mail::set_notebook_model(model_archive);
        LOG("[notebook] the notebook's model: %s (%s)", model_archive.c_str(), ec || !std::filesystem::is_regular_file(model_archive, ec) ? "not built yet: the game's stand-in" : "built");
    }
}

bool load_local_npc_catalogue(const std::string& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const unsigned n = npc_catalogue_mutable().add_local(text);
    LOG("[notebook] NPC catalogue: %u residents named from the player's game (%s)", n, file.c_str());
    return n > 0;
}

std::vector<std::pair<std::string, std::string>> npc_recipes() {
    std::vector<std::pair<std::string, std::string>> out;
    for (unsigned i = 0; i < wwhd_npc_recipe_count; i++) out.push_back({std::to_string(i), wwhd_npc_recipes[i]});
    return out;
}

namespace {
// The tables of a mod's manifest quests (declare, redeclare): its registrations, declarations, resident
// takeovers and sail lines.
void add_quests(const std::string& mod, const std::string& mod_name, const std::vector<ManifestQuest>& quests) {
    for (const ManifestQuest& q : quests) {
        if (define(mod, mod_name, q.id, q.title, q.reward_text, "") < 0) {
            LOG("[notebook] %s: quest \"%s\" not registered", mod.c_str(), q.id.c_str());
            continue;
        }
        std::lock_guard lock(g_mu);
        Quest* reg = find(mod, q.id);
        reg->hidden = q.journal != "on_enable";
        auto owned=q;for(auto&st:owned.steps)st.when.flag_mod=mod;
        g_declared.push_back({mod, reg->key, owned});
        if (q.letter_encloses_notebook && q.has_letter) {
            if (mod == kNotebookPackage) story_mail::set_notebook_letter(mod, q.letter_id);
            else LOG("[notebook] %s: only the Story Notebook package's letter brings the notebook", mod.c_str());
        }
        for (const ManifestStep& st : owned.steps) {
            if (st.has_talk && !st.needs_letter && !st.when.any())
                LOG("[notebook] %s: warning: quest \"%s\" step %u takes over %s with no condition (no letter, no \"available\"): it "
                    "speaks for the quest whenever the step is current", mod.c_str(), q.id.c_str(), st.n,
                    npc_catalogue().entry(st.resident)->name.c_str());
            if (st.has_talk)
                g_takeovers.push_back({reg->key, uint8_t(st.n), st.resident, st.needs_letter, st.lines, st.reward, st.when, st.follow,
                                       st.give_chart.empty() && st.give_item.empty() ? std::string() : mod, st.give_chart, st.give_item});
            if (!st.sail_line.empty()) g_sail_lines.push_back({reg->key, uint8_t(st.n), st.sail_line});
        }
    }
}
}  // namespace

bool declare(const std::string& mod, const std::string& mod_name, const mods::json::Value& manifest) {
    std::vector<ManifestQuest> quests;
    std::vector<ManifestChart> charts;
    std::string why;
    std::vector<std::string> warnings;
    std::vector<ManifestLetter> letters;
    if (!parse_quests(manifest, quests, why, npc_catalogue(), &warnings) || !parse_charts(manifest, charts, why) ||
        !parse_letters(manifest, letters, why)) {
        LOG("[notebook] %s: the manifest's quests are not used: %s", mod.c_str(), why.c_str());
        return false;
    }
    if (!letters.empty()) {
        {
            std::lock_guard lock(g_mu);
            for (ManifestLetter& l : letters) g_letters.push_back({mod, std::move(l)});
        }
        note_import();
        story_mail::note_import();
        if (!charts.empty()) story_chart::note_import();
        LOG("[notebook] %s: %zu letter(s) from the manifest", mod.c_str(), letters.size());
    }
    // the mod's charts: the MAP screen's chart list shows them while Link owns them
    for (const ManifestChart& ch : charts)
        if (story_chart::define(mod, ch.id, ch.name, ch.description, ch.x, ch.z, ch.item_mod, ch.item_game) < 0)
            LOG("[notebook] %s: chart \"%s\" not registered", mod.c_str(), ch.id.c_str());
    if (!charts.empty()) {
        story_chart::note_import();
        story_mail::note_import();
        LOG("[notebook] %s: %zu chart(s) from the manifest", mod.c_str(), charts.size());
    }
    for (const std::string& w : warnings) LOG("[notebook] %s: warning: %s", mod.c_str(), w.c_str());
    if (quests.empty()) return true;
    add_quests(mod, mod_name, quests);
    // the notebook, its marker on the sea chart and the letters are needed
    note_import();
    story_chart::note_import();
    story_mail::note_import();
    LOG("[notebook] %s: %zu quest(s) from the manifest", mod.c_str(), quests.size());
    return true;
}

// the residents' takeovers as they are now (runtime/tools/story_notebook_test.cpp)
std::vector<Takeover> takeovers() {
    std::lock_guard lock(g_mu);
    return g_takeovers;
}

bool story_idle(std::string& why) {
    if (g_open) return why = "the story notebook is open", false;
    if (g_talk.phase != Talk::None) return why = "a quest talk is running", false;
    if (g_sail.phase != Sail::Idle) return why = "the King of Red Lions is speaking", false;
    if (story_mail::busy()) return why = "a letter or a quest line is on the screen", false;
    return true;
}

bool redeclare(const std::string& mod, const std::string& mod_name, const mods::json::Value& manifest, Redeclared& out, std::string& why) {
    out = {};
    std::vector<ManifestQuest> quests;
    std::vector<ManifestChart> charts;
    std::vector<ManifestLetter> letters;
    std::vector<std::string> warnings;
    if (!parse_quests(manifest, quests, why, npc_catalogue(), &warnings) || !parse_charts(manifest, charts, why) ||
        !parse_letters(manifest, letters, why))
        return false;
    // what the mod's manifest declared so far, and room for the new list: nothing changes on a refusal
    std::set<uint16_t> old_keys;
    std::vector<std::string> old_letters;
    {
        std::lock_guard lock(g_mu);
        for (const Declared& d : g_declared)
            if (d.mod == mod) {
                old_keys.insert(d.key);
                if (d.quest.has_letter) old_letters.push_back(d.quest.letter_id);
            }
        for (const LooseLetter& l : g_letters)
            if (l.mod == mod) old_letters.push_back(l.letter.id);
        auto dropped = [&](const Quest& q) { return q.mod == mod && old_keys.count(q.key) && !q.texts_from_mod; };
        unsigned all = 0, mine = 0;
        for (const Quest& q : g_quests)
            if (!dropped(q)) all++, mine += q.mod == mod;
        for (const ManifestQuest& nq : quests) {
            const uint16_t key = quest_key(mod, nq.id);
            const Quest* had = find(mod, nq.id);
            if (!had || dropped(*had)) all++, mine++;
            for (const Quest& q : g_quests)
                if (q.key == key && !(q.mod == mod && q.id == nq.id))
                    return why = "quest " + nq.id + " can't be told apart from " + q.mod + "'s quest " + q.id + " in the save file (same key); rename one", false;
        }
        if (all > kMaxQuests || mine > kMaxQuestsPerMod)
            return why = "no room in the notebook (" + std::to_string(kMaxQuestsPerMod) + " quests per mod, " + std::to_string(kMaxQuests) + " in all)", false;
    }
    std::vector<story_chart::Declared> defs;
    for (const ManifestChart& ch : charts) defs.push_back({ch.id, ch.name, ch.description, ch.x, ch.z, ch.item_mod, ch.item_game});
    if (!story_chart::redefine(mod, defs, out.charts_kept, why)) return false;
    // the tables: this mod's entries out, the manifest's in
    {
        std::lock_guard lock(g_mu);
        std::erase_if(g_declared, [&](const Declared& d) { return d.mod == mod; });
        std::erase_if(g_takeovers, [&](const Takeover& t) { return old_keys.count(t.quest) != 0; });
        std::erase_if(g_sail_lines, [&](const SailLine& l) { return old_keys.count(l.quest) != 0; });
        std::erase_if(g_letters, [&](const LooseLetter& l) { return l.mod == mod; });
        std::erase_if(g_quests, [&](const Quest& q) { return q.mod == mod && old_keys.count(q.key) && !q.texts_from_mod; });
    }
    add_quests(mod, mod_name, quests);
    std::vector<story_mail::LetterText> texts;
    std::vector<std::string> now_letters;
    for (const ManifestQuest& q : quests)
        if (q.has_letter) texts.push_back({q.letter_id, q.letter_sender, q.letter_text}), now_letters.push_back(q.letter_id);
    {
        std::lock_guard lock(g_mu);
        for (const ManifestLetter& l : letters) {
            texts.push_back({l.id, l.sender, l.text}), now_letters.push_back(l.id);
            g_letters.push_back({mod, l});
        }
        for (const Takeover& t : g_takeovers)
            for (const ManifestQuest& q : quests) out.talks += t.quest == quest_key(mod, q.id);
        for (const ManifestQuest& q : quests)
            for (const ManifestStep& st : q.steps) out.sail_waits |= !st.sail_line.empty() && !g_sail_installed;
    }
    std::vector<std::string> forget;
    for (const std::string& id : old_letters)
        if (std::find(now_letters.begin(), now_letters.end(), id) == now_letters.end()) forget.push_back(id);
    story_mail::reload(mod, texts, forget);
    out.quests = unsigned(quests.size()), out.letters = unsigned(letters.size()), out.charts = unsigned(charts.size());
    for (const std::string& w : warnings) LOG("[notebook] %s: warning: %s", mod.c_str(), w.c_str());
    LOG("[notebook] %s: the story tables again (world editor): %u quest(s), %u talk(s), %u letter(s), %u chart(s)", mod.c_str(), out.quests,
        out.talks, out.letters, out.charts);
    return true;
}

// Manifest quests that need the port to act (game thread, a few times a second): the quest's letter
// is sent (the postbox hands it out once Link has the Delivery Bag); once it is read the quest reaches
// step 1 and Link carries the letter it encloses.
void declared_tick() {
    if (!have_save()) return;
    std::vector<Declared> declared;
    {
        std::lock_guard lock(g_mu);
        declared = g_declared;
    }
    // letters on their own: sent while the mod is enabled; once read, what they enclose, once
    std::vector<LooseLetter> loose;
    {
        std::lock_guard lock(g_mu);
        loose = g_letters;
    }
    for (const LooseLetter& l : loose) {
        const uint16_t key = story_mail::letter_key(l.mod, l.letter.id);
        // a timed letter waits for its condition and its days, until it is sent
        if (l.letter.timed() && story_mail::state(l.mod, l.letter.id) == story_mail::kResultNone) {
            story_mail::Book b = book();
            const bool holds = letter_condition(l.letter.when, [&](const std::string& q) { return int32_t(saved_step(b, quest_key(l.mod, q))); },
                                                [&](const std::string& f) { return story_actors::flag(b, l.mod, f); });
            const uint16_t today = ld16(save_base() + kSaveDay);
            const bool counting = b.has(story_mail::kRecordLetterDayLow, key);
            const bool due = holds && letter_due(b, key, l.letter.after_days, today);
            if (holds && !counting && b.has(story_mail::kRecordLetterDayLow, key))
                LOG("[notebook] %s: letter \"%s\" waits %u day(s) from day %u", l.mod.c_str(), l.letter.id.c_str(), l.letter.after_days, today);
            if (!due) continue;
            LOG("[notebook] %s: letter \"%s\" is due: it waits in the postbox", l.mod.c_str(), l.letter.id.c_str());
        }
        const int32_t sent = story_mail::send(l.mod, l.letter.id, l.letter.sender, l.letter.text);
        if (sent < story_mail::kResultWaiting) continue;
        story_mail::Book b = book();
        if (l.letter.after_days && b.has(story_mail::kRecordLetterDayLow, key)) erase_letter_day(b, key);
        if (sent != story_mail::kResultRead) continue;
        if (b.get(story_mail::kRecordLetterEnclosures, key)) continue;
        if (!b.set(story_mail::kRecordLetterEnclosures, key, 1)) continue;  // no room: next time
        for (const std::string& chart : l.letter.charts)
            if (story_chart::give(l.mod, chart) == story_chart::kResultOwned)
                LOG("[notebook] %s: letter \"%s\" gives Link chart \"%s\"", l.mod.c_str(), l.letter.id.c_str(), chart.c_str());
        for (const std::string& item : l.letter.items) story_items::give(l.mod, item, 1, "its letter");
    }
    for (const Declared& d : declared) {
        if (!d.quest.has_letter) continue;
        const int32_t letter = story_mail::send(d.mod, d.quest.letter_id, d.quest.letter_sender, d.quest.letter_text);
        if (letter != story_mail::kResultRead) continue;
        story_mail::Book b = book();
        if (saved_step(b, d.key) != 0) continue;
        if (reach_step(b, d.key, 1) == 1) {
            if (d.quest.letter_encloses_letter) b.set(kRecordCarried, d.key, 1);
            LOG("[notebook] %s: quest \"%s\" started by its letter%s", d.mod.c_str(), d.quest.id.c_str(),
                d.quest.letter_encloses_letter ? "; Link carries the enclosed letter" : "");
            for (const std::string& item : d.quest.letter_items)  // the items in the letter
                story_items::give(d.mod, item, 1, "the letter of its quest");
            for (const std::string& chart : d.quest.letter_charts)  // the charts in the letter
                if (story_chart::give(d.mod, chart) == story_chart::kResultOwned)
                    LOG("[notebook] %s: the letter of quest \"%s\" gives Link chart \"%s\"", d.mod.c_str(), d.quest.id.c_str(), chart.c_str());
        }
    }
    // charts: a step's treasure found (the quest is past the step) finishes its chart; a done quest
    // gives its reward chart
    for (const Declared& d : declared) {
        story_mail::Book b = book();
        const uint8_t now = saved_step(b, d.key);
        if (!now) continue;
        for (const ManifestStep& st : d.quest.steps)
            if (!st.find_chart.empty() && now > st.n && story_chart::state(d.mod, st.find_chart) == story_chart::kResultOwned) {
                story_chart::finish(d.mod, st.find_chart);
                LOG("[notebook] %s: quest \"%s\" is past step %u: chart \"%s\" is finished", d.mod.c_str(), d.quest.id.c_str(), st.n,
                    st.find_chart.c_str());
            }
        if (now == kStepDone && !d.quest.reward_chart.empty() && story_chart::state(d.mod, d.quest.reward_chart) == story_chart::kResultNone &&
            story_chart::give(d.mod, d.quest.reward_chart) == story_chart::kResultOwned)
            LOG("[notebook] %s: quest \"%s\" is done: Link gets chart \"%s\"", d.mod.c_str(), d.quest.id.c_str(), d.quest.reward_chart.c_str());
        if (now == kStepDone && !d.quest.reward_mod_item.empty() && !b.get(story_items::kRecordQuestRewardItem, d.key) &&
            b.set(story_items::kRecordQuestRewardItem,d.key,0) &&
            story_items::give(d.mod, d.quest.reward_mod_item, 1, "its quest's reward") >= 0)
            b.set(story_items::kRecordQuestRewardItem, d.key, 1);
    }
}

int32_t carry(const std::string& mod, const std::string& quest, bool carried) {
    if (!story_mail::valid_id(quest)) return kResultInvalid;
    uint16_t key;
    {
        std::lock_guard lock(g_mu);
        Quest* q = find(mod, quest);
        if (!q) return kResultInvalid;
        key = q->key;
    }
    if (!have_save()) return kResultNoRoom;
    story_mail::Book b = book();
    const uint8_t now = saved_step(b, key);
    if (carried && now != kStepDone && !carries(b, key)) {
        if (!b.set(kRecordCarried, key, 1)) return kResultNoRoom;
        LOG("[notebook] %s: Link carries the letter of quest \"%s\"", mod.c_str(), quest.c_str());
    } else if (!carried && carries(b, key)) {
        b.erase(kRecordCarried, key);
    }
    return now;
}

int32_t talk(const std::string& mod, const std::string& quest, uint32_t step, const std::string& resident, const std::string& lines,
             uint32_t reward) {
    const Resident who = npc_catalogue().find(resident);
    if (!story_mail::valid_id(quest) || !step || step > kMaxStep || who == kResidentNone || !npc_catalogue().entry(who)->talks() || lines.empty() || lines.size() > kMaxLinesBytes ||
        reward > 0x06)
        return kResultInvalid;
    std::u16string probe;
    if (!story_mail::append_utf16(lines, true, probe)) return kResultInvalid;
    size_t breaks = 0;
    for (char ch : lines) breaks += ch == '\n';
    if (breaks > 2) return kResultInvalid;  // one message box: three lines
    std::lock_guard lock(g_mu);
    Quest* q = find(mod, quest);
    if (!q) return kResultInvalid;
    for (Takeover& t : g_takeovers)
        if (t.quest == q->key && t.step == step) {
            t.resident = who, t.lines = lines, t.reward = uint8_t(reward);
            return saved(q->key);
        }
    g_takeovers.push_back({q->key, uint8_t(step), who, true, lines, uint8_t(reward)});
    LOG("[notebook] %s: quest \"%s\" step %u talks to %s", mod.c_str(), quest.c_str(), step, npc_catalogue().entry(who)->name.c_str());
    return saved(q->key);
}

void hide_floating_notebook_icon() {
    // the menu's floating icon (hdMenuItem sMenuIcon, HD decomp hd_screen_menu_item.cpp): a tap on a place
    // lifts its item into it; for the notebook's place it would show the stand-in's icon (the Cabana Deed's
    // parchment) while the page opens over the bag
    static const uint32_t kMenuIcon = GD(0x101F5578);
    const uint32_t icon = ld32(kMenuIcon);
    if (!icon || ld32(icon + kItemNumber) != kNotebookItem) return;
    if (const uint32_t pane = icon_pane(icon)) st8(pane + kPaneFlags, uint8_t(ld8(pane + kPaneFlags) & ~1u));
}

bool portrait_pixels(uint16_t key, guestmods::hud::Image& out) {
    std::lock_guard lock(g_mu);
    auto it = g_portraits.find(key);
    if (it == g_portraits.end() || !it->second.pixels.width) return false;
    out = it->second.pixels;
    return true;
}

bool notebook_owned() { return story_mail::notebook_owned(); }

bool open_page(const char* how) {
    const char* refused = !g_installed ? "the notebook is not installed" : !have_save() ? "no save file" :
                          !notebook_owned() ? "no notebook in this save file" : g_open ? "the page is already open" : nullptr;
    if (refused) {
        LOG("[notebook] page not opened (%s): %s", how, refused);
        return false;
    }
    g_page.show();
    g_open = true;
    g_close_time = 1.0f, g_closing.clear();
    g_pending_sound = Sound::Open;  // with the page's first controller read (the game's menu sound)
    LOG("[notebook] page opened (%s)", how);
    return true;
}
// The sealed letter's caption (A or a touch on it in the Delivery Bag): its name and description
bool open_letter(const char* how) {
    const uint16_t key = carried_letter();
    if (!g_installed || !key || g_open) return false;
    std::string title = "Sealed Letter", text;
    {
        std::lock_guard lock(g_mu);
        for (const Declared& d : g_declared)
            if (d.key == key) title = enclosure_name(d.quest), text = enclosure_description(d.quest);
        if (text.empty())  // a quest a mod's code defines: its mod as the sender
            for (const Quest& q : g_quests)
                if (q.key == key) text = "A sealed letter (" + q.mod_name + "). It isn't addressed to you.";
    }
    g_caption.show(title, text);
    g_open = true;
    g_close_time = 1.0f, g_closing.clear();
    g_pending_sound = Sound::Open;
    LOG("[notebook] the sealed letter's caption (%s)", how);
    return true;
}
// An item's caption (story_items.h): its Info, its use "show", a get. False while the page or a caption is open.
bool show_info(const std::string& title, const std::string& text, const char* how) {
    if (!g_installed || g_open || !have_save()) return false;
    g_caption.show(title, text);
    g_open = true;
    g_close_time = 1.0f, g_closing.clear();
    g_pending_sound = Sound::Open;
    LOG("[notebook] caption \"%s\" (%s)", title.c_str(), how);
    return true;
}
// A manifest quest reaches a step (an item's use action "quest_step"); its texts stay the manifest's
int32_t reach(const std::string& mod, const std::string& quest, uint32_t n) {
    uint16_t key;
    {
        std::lock_guard lock(g_mu);
        Quest* q = find(mod, quest);
        if (!q || !n || n > kMaxStep) return kResultInvalid;
        key = q->key;
    }
    if (!have_save()) return kResultNoRoom;
    story_mail::Book b = book();
    const int before = saved_step(b, key);
    const int now = reach_step(b, key, n);
    if (now < 0) return kResultNoRoom;
    if (now != before) story_scenes::trigger("quest_step", mod, quest, unsigned(now));
    return now;
}
void close_page() {
    if (g_open) LOG("[notebook] page closed (state load or shutdown)");
    g_page.hide();
    g_caption.hide();
    g_open = false;
    g_close_time = 1.0f, g_closing.clear(), g_closing_images.clear();
    g_pending_sound = Sound::None;
    guestmods::hud::store().drop(kOwner);
}
bool page_open() { return g_open.load(std::memory_order_relaxed); }
void set_bag_cover(bool (*covered)()) { g_bag_covered = covered; }
bool item_menu_shown() { return menu_shown(); }
uint32_t delivery_window_menu() {
    return g_menu && g_window_step + 1 >= g_step.load(std::memory_order_relaxed) && ld32(g_menu + kMenuWindowMode) == kDeliveryWindow ? g_menu : 0;
}
bool caption_open() { return g_caption.open(); }

// buttons held when the page closed: the game gets them only once they are released
std::atomic<uint32_t> g_swallow{0};

void mute(input::PadState& p) {
    if (g_open.load(std::memory_order_relaxed)) {
        p = input::PadState{};
        return;
    }
    if (const uint32_t s = g_swallow.load(std::memory_order_relaxed)) p.buttons &= ~s;
}

// seconds since the last controller read (the animations), at most a tenth of a second
float frame_seconds() {
    using clock = std::chrono::steady_clock;
    static clock::time_point last{};
    const clock::time_point now = clock::now();
    const float dt = last == clock::time_point{} ? 0.0f : std::chrono::duration<float>(now - last).count();
    last = now;
    return std::min(dt, 0.1f);
}

// research aid: WWHD_CHART_TEST_OPEN=mod:chart:read gives the chart (if Link doesn't own it) and opens it
// in the MAP screen's chart list (as "Open chart") at that controller read, once Link is in play
void chart_test_open(uint32_t read) {
    static bool done = false;
    const char* e = getenv("WWHD_CHART_TEST_OPEN");
    if (done || !e || !*e || !ld32(kLinkActor)) return;
    char mod[64] = {}, chart[64] = {};
    unsigned at = 0;
    if (sscanf(e, "%63[^:]:%63[^:]:%u", mod, chart, &at) < 2 || read < at) return;
    done = true;
    const int32_t owned = story_chart::state(mod, chart) > story_chart::kResultNone ? story_chart::kResultOwned : story_chart::give(mod, chart);
    const bool ok = story_chart::open_in_list(mod, chart);
    LOG("[chart] WWHD_CHART_TEST_OPEN at read %u: \"%s\" state %d, %s", read, chart, owned, ok ? "opened in the chart list" : "not opened");
}

void pad(input::PadState& p, Cpu* c) {
    if (!g_installed) return;  // no quest mod: nothing of the notebook runs
    static uint32_t ticks = 0;
    if (++ticks % 15 == 0) declared_tick();
    action_rewards(c);
    chart_test_open(ticks);
    bag_icon();
    story_item_buttons::test_assign();
    story_item_buttons::tick();  // ours on X/Y/R: the item number the game reads this frame
    const float dt = frame_seconds();
    if (!g_open.load(std::memory_order_relaxed)) {
        publish_closing(dt);
        if (const uint32_t s = g_swallow.load(std::memory_order_relaxed)) {
            g_swallow.store(s & p.buttons, std::memory_order_relaxed);
            p.buttons &= ~s;
        } else if (g_close_time >= 1.0f && story_items::present(c)) {  // an item Link got: its caption, from the next read
            g_pending_sound = Sound::None;  // present() played the get jingle
        }
        return;
    }
    if (g_ui && g_ui->prompts) g_look.prompts = g_ui->prompts();
    load_art();
    std::vector<Quest> storage;
    if (g_caption.open()) {  // the sealed letter's caption
        PadInput in;
        in.buttons = p.buttons, in.touch = p.touch, in.x = p.tx * 1280.0f, in.y = p.ty * 720.0f;
        const Sound sound = g_caption.input(in);
        g_caption.tick(dt);
        if (g_pending_sound != Sound::None) play(c, g_pending_sound), g_pending_sound = Sound::None;
        play(c, sound);
        if (g_caption.open()) {
            const std::vector<Command> cmds = g_caption.draw();
            load_glyphs(c, font::texts_of(cmds));
            publish({}, &cmds);
        } else {
            g_open = false;
            g_close_time = 0.0f;
            g_swallow.store(p.buttons, std::memory_order_relaxed);
            LOG("[notebook] the sealed letter's caption closed");
            publish_closing(0.0f);
        }
        p = input::PadState{};
        return;
    }
    std::vector<Row> rows = rows_now(storage);
    {  // what the page shows besides the quests: its frame, Link's place on the sea (the mini chart)
        Scene scene;
        scene.framed = g_look.art.framed();
        scene.pieces = scene.framed && pieces_ready();
        scene.header = g_look.art.header.width > 0 && g_header;
        scene.close_in_detail = story_notebook_layout::drawing();
        Cpu probe = *c_or_zero(c);
        const uint32_t play_object = c && g_call ? g_call(&probe, kPlay, {}) : 0;  // the game only (not in tests)
        const uint32_t link = play_object ? ld32(kLinkActor) : 0;
        if (link && stage_is(play_object + kPlayStartStage, "sea")) {
            scene.link_known = true;
            scene.link_x = u32_as_f32(ld32(link + kActorPos)), scene.link_z = u32_as_f32(ld32(link + kActorPos + 8));
        }
        g_page.set_scene(scene);
    }
    PadInput in;
    in.buttons = p.buttons, in.ly = p.ly, in.touch = p.touch;
    in.x = p.tx * 1280.0f, in.y = p.ty * 720.0f;  // the GamePad picture (854 x 480) as the page's 1280 x 720
    const Page::Action act = g_page.input(in, rows);
    g_page.tick(dt);
    if (act.kind == Page::Action::OpenChart) {  // the MAP screen's chart list, the step's chart selected
        for (const Quest& q : storage)
            if (q.key == act.key && !q.chart.empty()) {
                const bool ok = story_chart::open_in_list(q.chart_mod, q.chart);
                LOG("[notebook] quest %04X: open chart \"%s\" %s", act.key, q.chart.c_str(), ok ? "in the MAP screen's chart list" : "failed (no map screen)");
            }
    }
    if (act.kind == Page::Action::Toggle && have_save()) {
        story_mail::Book b = book();
        const bool on = toggle_tracked(b, act.key);
        LOG("[notebook] quest %04X %s", act.key, on ? "tracked: shown on the sea chart" : "no longer tracked");
        rows = rows_now(storage);
    }
    // the game's menu sounds (the page's own input only; the game sees an idle controller)
    if (g_pending_sound != Sound::None) play(c, g_pending_sound), g_pending_sound = Sound::None;
    play(c, act.sound);
    if (trace()) {
        static int last_mode = -1;
        static uint16_t last_key = 0;
        if (int(g_page.mode()) != last_mode || g_page.selected_key() != last_key)
            LOG("[notebook] page mode %d, quest %04X selected", int(g_page.mode()), g_page.selected_key());
        last_mode = int(g_page.mode()), last_key = g_page.selected_key();
    }
    if (g_page.open()) load_glyphs(c, font::texts_of(g_page.draw(rows)));
    publish(rows);
    if (!g_page.open()) {
        g_open = false;
        g_close_time = 0.0f;  // the page fades out over the next frames (publish_closing)
        g_swallow.store(p.buttons, std::memory_order_relaxed);  // the B that closed it doesn't reach the game
        LOG("[notebook] page closed (B or the close mark)");
        publish_closing(0.0f);
    }
    p = input::PadState{};  // the game sees an idle controller while the page is open
}

void set_game_ui(const GameUi* ui) { g_ui = ui; }
void note_import() { g_imported = true; }
bool imported() { return g_imported; }
bool installed() { return g_installed; }
void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>)) {
    g_original = original;
    g_call = call;
}

namespace hooks {

// inventoryItem(key): the notebook in the first empty delivery place, for the bag window only
void inventory_item(Cpu* c) {
    const uint32_t key = c->r[3], lr = c->lr;
    g_original(c, kInventoryItem);
    if ((lr != kWindowBuild && lr != kWindowInit) || key < kFirstDeliveryKey || key >= kFirstDeliveryKey + kDeliveryPlaces) return;
    if ((c->r[3] & 0xFF) != kEmpty) return;
    // a mod item the player put in this place (story_items.h): its pool number (nothing is in the save)
    if (const uint32_t n = story_items::number_at_game_place(key - kFirstDeliveryKey)) {
        c->r[3] = n;
        return;
    }
    const PortPlaces places = port_places(c);
    if (key != places.notebook && key != places.letter) return;
    c->r[3] = key == places.notebook ? kNotebookItem : kLetterItem;
    static uint32_t last[2] = {0, 0};
    const int which = key == places.notebook ? 0 : 1;
    if (trace() && key != last[which])
        LOG("[notebook] the delivery bag shows the %s in place %u", which ? "carried letter" : "notebook", key - kFirstDeliveryKey);
    last[which] = key;
}

// itemForSlot(this, item): the notebook's icon
void icon_frame(Cpu* c) {
    if (port_item(c->r[4])) {
        // the notebook: its own frame in the extended icon archive (item_icons.h, key 0) when served, so the
        // icon a touch lifts (the menu's floating icon) is the notebook and not the stand-in's
        const int own = c->r[4] == kNotebookItem ? story_items::frame(0) : -1;
        c->r[3] = own >= 0 ? uint32_t(own) : c->r[4] == kNotebookItem ? kNotebookIcon : kLetterIcon;
        return;
    }
    // a mod item's number: its frame in the extended icon layout, else a stand-in (the letter's)
    if (story_items::pool_number(c->r[4])) {
        const std::optional<story_items::Item> it = story_items::by_number(c->r[4]);
        const int frame = it ? story_items::frame(it->key) : -1;
        c->r[3] = frame >= 0 ? uint32_t(frame) : kLetterIcon;
        return;
    }
    g_original(c, kIconFrame);
}

// openSelectedChild(this, index): A on the notebook opens its page, A on the sealed letter its Info
// box (story_notebook_caption.h), instead of the game's Info for an item
void open_child(Cpu* c) {
    const uint32_t menu = c->r[3], index = c->r[4];
    const uint32_t count = ld32(menu + kMenuChildren), data = ld32(menu + kMenuChildren + 4);
    const uint32_t child = data ? ld32(data + 4 * (index < count ? index : 0)) : 0;
    if (trace()) LOG("[notebook] item menu: A on place %u (item %02X)", index, child ? ld32(child + kItemNumber) : 0xFFu);
    if (child && ld32(child + kItemNumber) == kNotebookItem && open_page("A on its bag place")) return;
    if (child && ld32(child + kItemNumber) == kLetterItem && open_letter("A on its bag place")) return;
    if (child && story_items::pool_number(ld32(child + kItemNumber)))  // a mod item: its Info (the game has none for it)
        if (const std::optional<story_items::Item> it = story_items::by_number(ld32(child + kItemNumber))) {
            story_items::act(it->key, story_items::Action::Info);
            return;
        }
    g_original(c, kOpenChild);
}

// setVisible(this, visible): the game shows a place's icon pane; the notebook's own icon replaces it
void set_visible(Cpu* c) {
    const uint32_t item = c->r[3];
    g_original(c, kSetVisible);
    if (!port_item(ld32(item + kItemNumber))) return;
    static const bool show_sentinel = [] { const char* e = getenv("WWHD_NOTEBOOK_SHOW_SENTINEL"); return e && *e == '1'; }();
    if (show_sentinel) return;
    if (const uint32_t icon = icon_pane(item)) st8(icon + kPaneFlags, uint8_t(ld8(icon + kPaneFlags) & ~1u));
}

// executeWindow(this): the bag window runs this frame; find the notebook's place among its children
void execute_window(Cpu* c) {
    const uint32_t menu = c->r[3];
    g_original(c, kExecuteWindow);
    g_menu_shown = menu_shown_now(c);
    g_menu = menu;
    g_window_step = g_step.load(std::memory_order_relaxed);
    g_bag_item = g_letter_item = 0;
    if (ld32(menu + kMenuWindowMode) != kDeliveryWindow) return;
    static const char* panes = getenv("WWHD_NOTEBOOK_PANES");  // research aid: the menu layout's panes, once
    static bool dumped = false;
    static unsigned frames = 0;
    if (panes && *panes && g_ui && g_ui->dump && !dumped && ++frames > 40) {
        dumped = true;
        g_ui->dump(c, menu, panes);
    }
    const uint32_t count = ld32(menu + kMenuChildren), data = ld32(menu + kMenuChildren + 4);
    for (uint32_t i = kFirstWindowChild; i < kFirstWindowChild + kWindowChildren && i < count && data; i++) {
        const uint32_t child = ld32(data + 4 * i);
        if (child && ld32(child + kItemNumber) == kNotebookItem) g_bag_item = child;
        if (child && ld32(child + kItemNumber) == kLetterItem) g_letter_item = child;
    }
}

// beginCursorSelection(this, point): X/Y/R lifts an item off its place towards the button. The notebook
// and a mod item with a use action go (story_item_buttons.h); the carried letter and a mod item
// without one stay where they are
void cursor_float(Cpu* c) {
    const uint32_t number = ld32(c->r[3] + kItemNumber);
    if (c->r[3] == g_touch_item && liftable(number)) g_touch_item = 0;  // a drag, not a tap
    if (!liftable(number)) {
        if (trace()) LOG("[notebook] item menu: item %02X can't go on X, Y or R (the letter, or a mod item without a use action)", number);
        return;
    }
    g_original(c, kCursorFloat);
}

// selectTouchEnter(this): a touch on the notebook, the sealed letter or a mod item (the GamePad has no
// A on its items). The game picks it up; what it opens waits for the touch to end as a tap
// (unselect_enter): a drag lifts it towards X/Y/R instead (cursor_float)
void touch_select(Cpu* c) {
    const uint32_t item = c->r[3];
    g_original(c, kTouchSelect);
    const uint32_t number = ld32(item + kItemNumber);
    if (trace()) LOG("[notebook] item menu: touch on item %02X", number);
    g_touch_item = number == kNotebookItem || number == kLetterItem || story_items::pool_number(number) ? item : 0;
}

// unselectEnter(this): the touch ended without lifting the item (a tap): the notebook's page, the
// sealed letter's caption, a mod item's Info
void unselect_enter(Cpu* c) {
    const uint32_t item = c->r[3];
    g_original(c, kUnselectEnter);
    if (!item || item != g_touch_item) return;
    g_touch_item = 0;
    const uint32_t number = ld32(item + kItemNumber);
    if (number == kNotebookItem) open_page("a tap on its bag place");
    if (number == kLetterItem) open_letter("a tap on its bag place");
    if (story_items::pool_number(number))
        if (const std::optional<story_items::Item> it = story_items::by_number(number)) story_items::act(it->key, story_items::Action::Info);
}

// getSelectedItemValue(this, receiver): the key X/Y/R takes; none for the carried letter and a mod item without a use action
void selected_value(Cpu* c) {
    g_original(c, kSelectedValue);
    if (story_item_buttons::route(menu_key(c, c->r[3])) == story_item_buttons::Route::Blocked) c->r[3] = kEmpty;
}

// the game system's X/Y/R assignment (this, button 0..2, key): the notebook and a mod item with a use
// action go on the button in the story block (story_item_buttons.h), never in the save's key; a game
// item is the game's, and takes the button back from ours
void assign_button(Cpu* c) {
    const uint32_t slot = c->r[4], key = c->r[5];
    const story_item_buttons::MenuKey k = menu_key(c, key);
    switch (story_item_buttons::route(k)) {
    case story_item_buttons::Route::Blocked: return;
    case story_item_buttons::Route::Notebook:
        if (story_item_buttons::assign_notebook(slot)) story_item_buttons::refresh_strip(c);
        return;
    case story_item_buttons::Route::Item:
        if (story_item_buttons::assign_item(k.item, slot)) story_item_buttons::refresh_strip(c);
        return;
    case story_item_buttons::Route::Game: break;
    }
    uint8_t before[story_item_buttons::kAssignable];
    story_item_buttons::save_keys(before);
    g_original(c, kAssignButton);
    story_item_buttons::game_assigned(before, slot);
}

// the tracked step's resident (a following one): note where it stands (read-only)
void note_live_place(Cpu* c, uint32_t npc) {
    if (!have_save()) return;
    story_mail::Book b = book();
    const uint16_t tracked = tracked_key(b);
    if (!tracked) return;
    const uint8_t step = saved_step(b, tracked);
    Resident who = kResidentNone;
    {
        std::lock_guard lock(g_mu);
        for (const Takeover& t : g_takeovers)
            if (t.quest == tracked && t.step == step && t.follow) who = t.resident;
    }
    const NpcEntry* e = npc_catalogue().entry(who);
    if (!e) return;
    Cpu probe = *c;
    const uint32_t stage = g_call(&probe, kPlay, {}) + kPlayStartStage;
    const uint8_t number = ld8(npc + kPeopleNumber);
    const int room = int8_t(ld8(npc + kActorRoom));
    const bool here = e->people_at(stage_name(stage), room, number);
    if (!here || !stage_is(stage, "sea")) return;  // the chart's coordinates are the sea's
    g_live = {tracked, step, u32_as_f32(ld32(npc + kActorPos)), u32_as_f32(ld32(npc + kActorPos + 8)), g_step.load()};
}

// getMsg(this): a resident's line. While the tracked quest's step hands this resident a takeover
// (story_takeover.h), the resident says the quest's line instead: the game's own selection does not
// run for this talk (it would set its first-talk flag; nothing is set), the list is cleared so the
// talk ends after the one box, and our text shows in the carrier message for exactly this talk.
void people_message(Cpu* c) {
    if (story_actors::people_message(c)) return;
    const uint32_t npc = c->r[3];
    const Takeover* live = nullptr;
    Takeover copy;
    Resident who = kResidentNone;
    std::vector<Takeover> options;  // several quests want the resident: a choice first
    if (have_save() && g_talk.phase == Talk::None) {
        story_mail::Book b = book();
        std::lock_guard lock(g_mu);
        // the catalogue's character for this resident where it stands (stage and room)
        Cpu probe = *c;  // r3 stays the resident for the game's getMsg
        const uint32_t stage = g_call(&probe, kPlay, {}) + kPlayStartStage;
        const uint8_t number = ld8(npc + kPeopleNumber);
        const int room = int8_t(ld8(npc + kActorRoom));
        const NpcCatalogue& catalogue = npc_catalogue();
        const std::string stage_text = stage_name(stage);
        for (size_t i = 0; i < catalogue.entries.size() && !live; i++) {
            // one of this character's people actors, where the table says it stands
            if (!catalogue.entries[i].people_at(stage_text, room, number)) continue;
            if (g_skip_once == Resident(i + 1) && live_takeover(g_takeovers, b, Resident(i + 1), event_flags(), hour_now())) {
                g_skip_once = kResidentNone;  // after "Never mind": this talk is the game's
                LOG("[notebook] %s: after \"Never mind\", this talk is the game's", catalogue.entries[i].name.c_str());
                break;
            }
            if ((live = live_takeover(g_takeovers, b, Resident(i + 1), event_flags(), hour_now()))) {
                copy = *live;
                who = Resident(i + 1);
                if (wanting(g_takeovers, b, who, event_flags(), hour_now()) > 1) options = wanted(g_takeovers, b, who, event_flags(), hour_now(), 2);
            }
        }
    }
    if (!live) {
        g_original(c, kPeopleGetMsg);
        if (trace()) LOG("[notebook] resident %u says the game's line %X", ld8(npc + kPeopleNumber), c->r[3]);
        return;
    }
    g_talk = {};
    g_talk.phase = Talk::Talking, g_talk.npc = npc, g_talk.takeover = copy, g_talk.since = g_step.load(), g_talk.resident = who;
    if (options.size() > 1) {
        // the game's own choice box: the quests (their titles) and "Never mind"
        g_talk.phase = Talk::Choosing, g_talk.options = options;
        std::string prompt = "Oh, hello there. What brings you here?\n";
        story_mail::append_utf16(prompt, true, g_talk.text);
        for (char16_t u : {u'\x0E', u'\x01', char16_t(options.size() + 1 == 3 ? 8 : 7), u'\x00'}) g_talk.text.push_back(u);
        std::string labels;
        for (const Takeover& t : options) {
            std::string title;
            for (const Quest& q : g_quests)
                if (q.key == t.quest) title = q.title;
            labels += choice_label(title.empty() ? "A quest" : title) + "\n";
        }
        labels += "Never mind";
        story_mail::append_utf16(labels, true, g_talk.text);
        LOG("[notebook] %zu quests want %s: the choice box", options.size(), npc_catalogue().entry(who)->name.c_str());
    } else {
        story_mail::append_utf16(copy.lines, true, g_talk.text);
        LOG("[notebook] quest %04X: the resident says the quest's line (step %u)", copy.quest, copy.step);
    }
    story_mail::say(kCarrierMessage, &g_talk.text);
    st32(npc + kPeopleMsgList, 0);
    st32(npc + kPeopleUbList, 0);
    c->r[3] = kCarrierMessage;
}

// next_msgStatus(this, msgNo): after our choice box, the chosen quest's lines continue the same talk; "Never
// mind" ends it (and the resident's next talk is the game's). Any other box: the game's.
void people_next_message(Cpu* c) {
    if (story_actors::people_next_message(c)) return;
    const uint32_t npc = c->r[3], msg_no = c->r[4];
    if (g_talk.phase != Talk::Choosing || npc != g_talk.npc) return g_original(c, kPeopleNextMsg);
    const uint32_t manager = ld32(kMessageManager);
    const uint32_t chosen = manager ? ld32(manager + kSelectNum) : 0xFFFFFFFFu;
    if (chosen < g_talk.options.size()) {
        g_talk.takeover = g_talk.options[chosen];
        g_talk.options.clear();
        g_talk.text.clear();
        story_mail::append_utf16(g_talk.takeover.lines, true, g_talk.text);
        story_mail::say(kCarrierMessage, &g_talk.text);
        st32(msg_no, kCarrierMessage);
        g_talk.phase = Talk::Talking;
        LOG("[notebook] quest %04X chosen: the resident says the quest's line (step %u)", g_talk.takeover.quest, g_talk.takeover.step);
        c->r[3] = kMsgContinues;
        return;
    }
    g_skip_once = g_talk.resident;
    LOG("[notebook] \"Never mind\": the talk ends; the next one is the game's");
    story_mail::unsay();
    g_talk = {};
    c->r[3] = kMsgEnds;
}

// the resident's process step: after our talk, hand over the letter, the reward through the game's
// own get-item event (the way the game's items order it), and finish the quest
void people_execute(Cpu* c) {
    const uint32_t npc = c->r[3];
    const bool mod_actor = story_actors::people_execute(c, true);
    g_original(c, kPeopleExecute);
    if (mod_actor) {
        const Cpu keep = *c; c->r[3] = npc;
        story_actors::people_execute(c, false); *c = keep; return;
    }
    note_live_place(c, npc);
    if (g_talk.phase == Talk::None || npc != g_talk.npc) return;
    const uint16_t command = ld16(npc + kEventCommand);  // the resident's: its talk
    if (g_talk.phase == Talk::Choosing) {
        if (command == kCommandNone) story_mail::unsay(), g_talk = {};  // the box went away without a choice
        return;
    }
    if (g_talk.phase == Talk::Talking) {
        if (command != kCommandNone) return;  // the talk is still on
        story_mail::unsay();
        if (have_save()) {
            story_mail::Book b = book();
            complete(b, g_talk.takeover);
            if (!g_talk.takeover.chart.empty() && story_chart::give(g_talk.takeover.chart_mod, g_talk.takeover.chart) == story_chart::kResultOwned)
                LOG("[notebook] quest %04X: Link gets chart \"%s\"", g_talk.takeover.quest, g_talk.takeover.chart.c_str());
            if (!g_talk.takeover.item.empty()) story_items::give(g_talk.takeover.chart_mod, g_talk.takeover.item, 1, "a talk of its quest");
        }
        LOG("[notebook] quest %04X: delivered, the quest is done", g_talk.takeover.quest);
        if (!g_talk.takeover.reward) {
            g_talk = {};
            return;
        }
        g_talk.phase = Talk::Reward;
    }
    if (g_talk.phase == Talk::Reward) {
        // the game's default get-item event, ordered by the resident who hands the reward over (as the
        // game's items do: fopAcM_orderItemEvent + CANGETITEM until the event's command arrives)
        if (command == kCommandGetItem) {  // the get-item event started: its item is ours
            if (g_talk.item != 0xFFFFFFFFu) st32(g_call(c, kPlay, {}) + kPlayItemPartner, g_talk.item);
            LOG("[notebook] quest %04X: reward %02X handed over", g_talk.takeover.quest, g_talk.takeover.reward);
            g_talk.phase = Talk::After;
            g_talk.since = g_step.load();
            return;
        }
        if (g_talk.item == 0xFFFFFFFFu) {
            Cpu probe = *c;
            g_talk.item = g_call(&probe, kPresentItem, {npc + kActorPos, g_talk.takeover.reward, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0});
        }
        Cpu probe = *c;
        g_call(&probe, kOrderItemEvent, {npc});
        st16(npc + kEventCondition, uint16_t(ld16(npc + kEventCondition) | kConditionCanGetItem));
        return;
    }
    if (g_talk.phase == Talk::After) {
        // The actor that ordered the get-item event ends it (as the game's items do, d_a_item): once
        // the event manager says DEFAULT_GETITEM is over, reset the event (dComIfGp_event_reset);
        // otherwise the resident keeps the event's command and nobody can talk to him.
        const uint32_t p = g_call(c, kPlay, {});
        if (g_call(c, kEndCheck, {p + kPlayEventManager, kDefaultGetItem})) {
            st16(p + kPlayEventFlags, uint16_t(ld16(p + kPlayEventFlags) | 8));
            if (trace()) LOG("[notebook] quest %04X: the get-item event ended", g_talk.takeover.quest);
            g_talk = {};
        } else if (g_step.load() - g_talk.since > 1800) {
            LOG("[notebook] quest %04X: the get-item event did not end in a minute", g_talk.takeover.quest);
            g_talk = {};
        }
    }
}

// checkForceMessage(this): the game's answer, noted for this execute of the boat (never changed)
void ship_force_message(Cpu* c) {
    g_original(c, kShipForceMessage);
    g_sail.force_checked = true;
    g_sail.force_pending = g_sail.force_pending || c->r[3] != 0;
}

namespace {
void sail_finish(uint32_t ship, uint64_t now, const char* how) {
    const bool shown = story_mail::said();
    story_mail::unsay();
    if (ship) {  // the boat as it was: its next message and its turn-back flag
        if (ld32(ship + kShipNextMessage) == kSailCarrier) st32(ship + kShipNextMessage, g_sail.saved_message);
        if (g_sail.saved_turn) st32(ship + kShipState, ld32(ship + kShipState) | kShipTurnAfterTalk);
    }
    bool marked = false;
    if (shown && have_save()) {
        story_mail::Book b = book();
        marked = mark_sail_line_said(b, g_sail.line);
    }
    LOG("[notebook] quest %04X: the King of Red Lions' line for step %u %s (%s)", g_sail.line.quest, g_sail.line.step,
        marked ? "was said: remembered in the story record" : shown ? "was said, but the record is full" : "was not shown", how);
    const uint64_t busy = now;
    g_sail = {};
    g_sail.busy = busy;
}

// After the boat's execute: the talk we started ends, or the due line is ordered in a free moment.
void sail_after(Cpu* c, uint32_t ship, uint64_t now) {
    const uint8_t mode = ld8(ship + kShipMode);
    Cpu probe = *c;
    const uint32_t play = g_call(&probe, kPlay, {});
    const bool event = ld8(play + kPlayEventMode) != 0;
    if (g_sail.phase == Sail::Talking) {
        if (ship != g_sail.ship) return sail_finish(0, now, "another boat");
        if (mode == kShipModeTalk) return;
        sail_finish(ship, now, "the talk ended");
    }
    if (event || mode == kShipModeTalk) g_sail.busy = now;
    if (g_sail.phase == Sail::Accepted) {
        if (ship == g_sail.ship && now - g_sail.since < kSailAcceptSteps) return;
        LOG("[notebook] quest %04X: the King of Red Lions' talk did not start", g_sail.line.quest);
        g_sail.phase = Sail::Idle;
    }
    if (g_sail.phase != Sail::Idle || !have_save() || g_talk.phase != Talk::None) return;
    SailLine line;
    {
        story_mail::Book b = book();
        std::lock_guard lock(g_mu);
        const SailLine* due = due_sail_line(g_sail_lines, b);
        if (!due) return;
        line = *due;
    }
    ShipMoment m;
    m.can_say = story_mail::can_say();
    m.force_checked = g_sail.force_checked, m.force_pending = g_sail.force_pending;
    const int8_t orders = int8_t(ld8(play + kPlayOrderCount));
    m.mode = mode, m.event_running = event, m.orders = orders > 0 ? unsigned(orders) : 0;
    m.mini_game = ld8(play + kPlayMiniGame) != 0;
    m.riding = (ld32(play + kPlayPlayerStatus) & kRideShip) != 0;
    m.steps_since_busy = now - g_sail.busy;
    const uint32_t link = ld32(play + kPlayPlayer);
    if (!m.riding && link) {  // the game's walk-up test for its forced lines (daShip_c::execute)
        const float dy = u32_as_f32(ld32(ship + kActorPos + 4)) - u32_as_f32(ld32(link + kActorPos + 4));
        const float dx = u32_as_f32(ld32(ship + kAttentionPos)) - u32_as_f32(ld32(link + kActorPos));
        const float dz = u32_as_f32(ld32(ship + kAttentionPos + 8)) - u32_as_f32(ld32(link + kActorPos + 8));
        if (std::fabs(dy) < 50.0f && dx * dx + dz * dz < 62500.0f) {
            Cpu angle = *c;
            m.close_in_front = int32_t(g_call(&angle, kSeenAngle, {ship, link})) < 0x6000;
        }
    }
    const char* refusal = sail_refusal(m);
    if (refusal) {
        if (trace() && refusal != g_sail.refusal)
            LOG("[notebook] quest %04X: the King of Red Lions' line waits: %s (step %llu)", line.quest, refusal, (unsigned long long)now);
        g_sail.refusal = refusal;
        return;
    }
    // the game's own order for the boat's forced lines, and what its execute sets with it (attention
    // flags and the talk condition are set anew by every execute)
    g_call(&probe, kOrderSpeak, {ship});
    st32(ship + kAttentionFlags, (ld32(ship + kAttentionFlags) & ~kAttnShip) | kAttnLockonTalk | kAttnSpeak);
    st16(ship + kEventCondition, uint16_t(ld16(ship + kEventCondition) | kConditionCanTalk));
    if (g_sail.refusal || trace())
        LOG("[notebook] quest %04X: the King of Red Lions' line for step %u is ordered (%s, step %llu)", line.quest, line.step,
            m.riding ? "Link rides the boat" : "Link is close in front of the boat", (unsigned long long)now);
    g_sail.refusal = nullptr;
    g_sail.phase = Sail::Ordered, g_sail.ship = ship, g_sail.line = line, g_sail.since = now;
}
}  // namespace

// execute(this): the boat's step. Our talk is the one whose command answers our order at the next
// execute; afterwards, our line is ordered in a free moment or our talk ends.
void ship_execute(Cpu* c) {
    const uint32_t ship = c->r[3];
    const uint64_t now = g_step.load(std::memory_order_relaxed);
    g_sail.force_checked = g_sail.force_pending = false;
    if (g_sail.phase == Sail::Ordered) {
        const bool ours = ship == g_sail.ship && ld16(ship + kEventCommand) == kCommandTalk && ld8(ship + kShipMode) != kShipModeTalk;
        g_sail.phase = ours ? Sail::Accepted : Sail::Idle;  // not taken: ordered again in a free moment
        if (ours) g_sail.since = now;
        if (trace()) LOG("[notebook] quest %04X: the King of Red Lions' talk %s", g_sail.line.quest, ours ? "starts" : "was not taken");
    }
    g_original(c, kShipExecute);
    const uint32_t result = c->r[3];
    sail_after(c, ship, now);
    c->r[3] = result;
}

// procTalk_init(this): a talk of the boat starts; the game picks its message first. For our talk the
// message is the carrier with the quest's text, and the boat doesn't turn back after it; both are put
// back when the talk ends.
void ship_talk_init(Cpu* c) {
    const uint32_t ship = c->r[3];
    const uint32_t before = ld32(ship + kShipNextMessage);
    g_original(c, kShipTalkInit);
    if (g_sail.phase != Sail::Accepted || ship != g_sail.ship) return;
    g_sail.saved_message = before;  // the boat's message before this talk (riding, setInitMessage keeps it)
    const uint32_t state = ld32(ship + kShipState);
    g_sail.saved_turn = (state & kShipTurnAfterTalk) != 0;
    st32(ship + kShipState, state & ~kShipTurnAfterTalk);
    st32(ship + kShipNextMessage, kSailCarrier);
    g_sail.text.clear();
    story_mail::append_utf16(g_sail.line.text, true, g_sail.text);
    story_mail::say(kSailCarrier, &g_sail.text);
    g_sail.phase = Sail::Talking;
    LOG("[notebook] quest %04X: the King of Red Lions says the quest's line (step %u; his message was %X; logic step %llu)", g_sail.line.quest,
        g_sail.line.step, g_sail.saved_message, (unsigned long long)g_step.load(std::memory_order_relaxed));
}

}  // namespace hooks

void install_sail(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (!g_installed || g_sail_installed) return;
    {
        std::lock_guard lock(g_mu);
        if (g_sail_lines.empty()) return;  // no quest has a line: the boat stays untouched
    }
    const std::pair<uint32_t, void (*)(Cpu*)> list[] = {
        {kShipExecute, hooks::ship_execute}, {kShipForceMessage, hooks::ship_force_message}, {kShipTalkInit, hooks::ship_talk_init}};
    for (const auto& [addr, fn] : list) {
        std::string other = replaced_by(addr);
        if (!other.empty()) {
            LOG("[notebook] %s replaces %08X: the King of Red Lions says no quest lines this session", other.c_str(), addr);
            return;
        }
    }
    for (const auto& [addr, fn] : list) replace(addr, fn);
    g_sail_installed = true;
    LOG("[notebook] the King of Red Lions can say the tracked quest's line");
}
bool sail_installed() { return g_sail_installed; }

void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (!g_imported || g_installed || !g_original || !g_call) return;
    const std::pair<uint32_t, void (*)(Cpu*)> list[] = {
        {kInventoryItem, hooks::inventory_item}, {kIconFrame, hooks::icon_frame},     {kOpenChild, hooks::open_child},
        {kTouchSelect, hooks::touch_select},     {kUnselectEnter, hooks::unselect_enter}, {kSelectedValue, hooks::selected_value},
        {kAssignButton, hooks::assign_button},
        {kSetVisible, hooks::set_visible},       {kExecuteWindow, hooks::execute_window}, {kCursorFloat, hooks::cursor_float},
        {kPeopleGetMsg, hooks::people_message},  {kPeopleExecute, hooks::people_execute},
        {kPeopleNextMsg, hooks::people_next_message}};
    // all or nothing: a notebook that shows but can't be opened, or can be put on a button, is worse than none
    for (const auto& [addr, fn] : list) {
        std::string other = replaced_by(addr);
        if (!other.empty()) {
            LOG("[notebook] %s replaces %08X: the story notebook is not in the item menu this session", other.c_str(), addr);
            return;
        }
    }
    for (const auto& [addr, fn] : list) replace(addr, fn);
    story_chart::set_tracked_source(tracked_marker);
    g_installed = true;
    LOG("[notebook] the story notebook is in the Delivery Bag");
}

// the game's actor list (fopAcIt_Executor's queue): its "people" residents the catalogue knows, where they
// stand. Read-only; on the game thread (the list changes while the game runs).
std::vector<LiveVillager> scan_villagers(const std::string& stage) {
    const uint32_t kActorQueue = GD(0x101F3328);  // g_fopAcTg_Queue (turbo.cpp)
    constexpr uint32_t kSubMethod = 0xF0;             // fopAc_ac_c::sub_method: its execute at +8
    std::vector<LiveVillager> out;
    const NpcCatalogue& catalogue = npc_catalogue();
    uint32_t n = ld32(kActorQueue);
    for (int k = 0; n && k < 2048; k++, n = ld32(n + 8)) {
        if (n < 0x10000000 || n >= 0x50000000) break;
        const uint32_t a = ld32(n + 0xC);
        if (a < 0x10000000 || a >= 0x50000000) continue;
        const uint32_t sub = ld32(a + kSubMethod);
        if (sub < 0x10000000 || sub >= 0x50000000 || ld32(sub + 8) != kPeopleExecute) continue;
        const int room = int8_t(ld8(a + kActorRoom));
        const Resident who = catalogue.people_at(stage, room, ld8(a + kPeopleNumber));
        if (who == kResidentNone || story_actors::owns_resident(a)) continue;  // the mods' own residents are not the game's
        out.push_back({who, a, stage, room, u32_as_f32(ld32(a + kActorPos)), u32_as_f32(ld32(a + kActorPos + 4)), u32_as_f32(ld32(a + kActorPos + 8))});
    }
    return out;
}

void state_loaded() {
    close_page();
    g_bag_item = 0, g_letter_item = 0, g_menu = 0, g_window_step = 0, g_menu_shown = true, g_touch_item = 0;
    g_talk = {};
    if (g_sail.phase == Sail::Talking) story_mail::unsay();
    g_sail = {};  // the state has its own boat
    std::lock_guard lock(g_mu);
    for (auto& [key, p] : g_portraits) p.handle = 0;
}

}  // namespace story_notebook
