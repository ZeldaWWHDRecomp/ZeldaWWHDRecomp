// See controller_reapply.h. Game addresses from wwhd_src (d/hd_scene_ingame_ui.cpp,
// d/hd_scene_ingame_ui_part2.cpp, d/hd_screen_sequence_option.cpp, d/hd_input_ctrl.cpp).
//
// The game's in-game controller change runs from its pause menu: Options -> controller, then
// StateID_ExchangeProMode. Of its steps only two concern play (ingameExchangeProModeExecute): the
// controller mode itself, hdInputManager::setMode (the controller the game reads, the GamePad
// output), and for the GamePad its touch pointer (mPointerEnabled, the exchange's
// mModeChangeRequested). The rest is the switch's own screens (the "look at the TV" curtain, the
// mode hint), and where a menu opens is decided again each time one opens (prepareMapCoordinator,
// from the mode). So after a load, once the player is in play (StateID_Play) or in an event or dialog
// (StateID_PauseDrc), the switch is those two steps, made directly: no menu, no screens, no input.
//
// Only when setMode refuses (the controller it needs is not there) do the menu's steps run as the
// player would: the menu opens (StateID_ToMenu), the exchange runs with the Options category, and
// the menu it returns to is closed by the menu's own close (hdIngameUi_c::menuExecute with its cancel
// input held for that one call). Starting the exchange straight from play leaves the TV-mode switch
// screen waiting forever (its screens are still on).
#include "controller_reapply.h"
#include "guest_addr.h"
#include "input.h"
#include "runtime.h"
#include <atomic>
#include <cstdlib>

namespace controller_reapply {
namespace {
bool object(uint32_t p) { return p >= mem::kMem2Start && p < mem::kMem2End - 0x400; }

const uint32_t kInputManager = GD(0x101F5088);  // hdInputManager* (the in-game UI's sController)
constexpr uint32_t kHold = 0x18;                // its pointer controller's held buttons (mPointer.mHold)
constexpr uint32_t kCancel = 2;                 // the menu's close button (hdIngameUi_c::menuExecute)
constexpr uint32_t kMode = 0x1D0;               // hdInputManager::mMode
constexpr uint32_t kPointerEnabled = 0x26C;     // hdInputManager::mPointerEnabled (the GamePad's touch)
constexpr uint32_t kTrigger = 0x1C;             // its pointer controller's new buttons (mPointer.mTrigger)
const uint32_t kIngameUi = GD(0x101F8344);      // sIngameUi (hdIngameUi_c*, 0 outside gameplay)
constexpr uint32_t kUiState = 0x10;             // hdIngameUi_c::mState (hdStateMachine)
constexpr uint32_t kUiOptionScreen = 0x1C0;     // hdIngameUi_c::mpOptionScreen (hdSequenceOption_c*)
constexpr uint32_t kOptionCategory = 0x11C;     // hdSequenceOption_c::mPreviousCategory
const uint32_t kPlayState = GD(0x1049EC00);     // l_ingamePlayState
const uint32_t kPauseDrcState = GD(0x1049EC58); // l_ingamePauseDrcState (events, dialogs)
const uint32_t kToMenuState = GD(0x1049EC84);   // l_ingameToMenuState
const uint32_t kMenuState = GD(0x1049ECB0);     // l_ingameMenuState
const uint32_t kExchangeState = GD(0x1049F4F0); // l_ingameExchangeProModeState
const uint32_t fn_currentState = GC(0x02006478);  // the state machine's current state id
const uint32_t fn_changeState = GC(0x020063C0);   // hdIngameChangeState(machine, id)
const uint32_t fn_menuExecute = GC(0x0270E4E4);   // hdIngameUi_c::menuExecute
const uint32_t fn_setMode = GC(0x02618094);       // hdInputManager::setMode(this, mode): 1 if switched
const uint32_t kPlay = GD(0x1046F0B0);            // g_dComIfG_gameInfo.play
const uint32_t kLinkPtr = kPlay + 0x5B34;         // its mpPlayerPtr[0]: daPy_lk_c
constexpr uint32_t kActorPos = 0x314, kShapeAngleY = 0x32A, kLinkProc = 0x65F0;

constexpr int kSettleFrames = 20;    // the in-game UI has been in this state this long (fades, arrival)
constexpr int kGiveUpFrames = 3600;  // ~1 min without an in-game UI (title screen): nothing to switch
constexpr int kMenuFrames = 600;     // the exchange is back in its menu within this many frames

enum Phase { kIdle, kWaitPlay, kWaitMenuOpen, kWaitMenuBack };
Phase g_phase = kIdle;
const char* g_kind = "";
int g_frames = 0, g_stable = 0, g_trace = 0, g_category = -1;
bool g_direct_refused = false;  // setMode refused the mode: the menu path, once in play
std::atomic<bool> g_hold_pro{false};  // see holds_pro_controller
std::atomic<bool> g_hold_gamepad{false};  // see holds_gamepad
std::atomic<bool> g_live_pro{false};  // see pro_controller_live

int game_mode() {
    uint32_t m = ld32(kInputManager);
    return object(m) ? (int)ld32(m + kMode) : -1;
}

uint32_t state_key(Cpu* c, uint32_t id) {
    uint32_t vt = ld32(id + 8);
    return object(vt) ? guest_call(c, ld32(vt + 0x14), {id}) : 0;
}

// the in-game UI's current StateID key (0: no in-game UI)
uint32_t ui_state(Cpu* c, uint32_t ui) {
    if (!object(ui)) return 0;
    uint32_t current = guest_call(c, fn_currentState, {ui + kUiState});
    return object(current) ? state_key(c, current) : 0;
}

void restore(Cpu* c, const Cpu& saved) {
    uint32_t core = c->core;
    void* th = c->thread;
    *c = saved;
    c->core = core;
    c->thread = th;
}

// debug: WWHD_TEST_CONTROLLER_TRACE logs the in-game UI state (StateID key) for 20 s after a load
void trace_ui(Cpu* c) {
    static const bool on = getenv("WWHD_TEST_CONTROLLER_TRACE") != nullptr;
    static uint32_t last = ~0u;
    if (!on || g_trace <= 0) return;
    --g_trace;
    Cpu saved = *c;
    uint32_t key = ui_state(c, ld32(kIngameUi));
    restore(c, saved);
    const uint32_t ui = ld32(kIngameUi), phase = object(ui) ? ld32(ui + 0x234) : ~0u;  // mModeSwitchPhase
    static uint32_t last_phase = ~0u;
    if (key != last || phase != last_phase)
        LOG("[controller] in-game UI state %u (mode switch phase %d), game mode %d", key, (int)phase, game_mode());
    last = key, last_phase = phase;
    // what the game reads from its controller (its pointer controller: the buttons every scene reads),
    // and Link: position, facing, procedure, every 30 frames and whenever a button is held
    const uint32_t mgr = ld32(kInputManager), link = ld32(kLinkPtr);
    const uint32_t hold = object(mgr) ? ld32(mgr + kHold) : 0, trig = object(mgr) ? ld32(mgr + kTrigger) : 0;
    if (hold || trig || g_trace % 30 == 0) {
        if (object(link))
            LOG("[controller] game input hold %08X trigger %08X; Link %.1f %.1f %.1f facing %d procedure 0x%X", hold, trig,
                ldf32(link + kActorPos), ldf32(link + kActorPos + 4), ldf32(link + kActorPos + 8), (int16_t)ld16(link + kShapeAngleY),
                ld32(link + kLinkProc));
        else
            LOG("[controller] game input hold %08X trigger %08X; no Link", hold, trig);
    }
}

void wait_play(Cpu* c, uint32_t ui) {
    const int mode = game_mode();
    const bool pro = input::pro_controller();
    const int category = needed_category(mode, pro);
    if (category < 0) {
        LOG("[controller] after the %s: the game plays with the %s (mode %d), as the player", g_kind,
            pro ? "Pro Controller" : "GamePad", mode);
        g_phase = kIdle;
        return;
    }
    // play, or an event or dialog (StateID_PauseDrc: the GamePad's item screen paused; a state saved in
    // a dialog must not wait for an input the game can't read), for a moment (fades, arrival)
    const uint32_t state = ui_state(c, ui);
    const bool play = state == state_key(c, kPlayState);
    g_stable = play || state == state_key(c, kPauseDrcState) ? g_stable + 1 : 0;
    if (g_stable < kSettleFrames) return;
    // the exchange's own steps for play, directly (see the top of this file)
    const uint32_t mgr = ld32(kInputManager);
    static const bool menu_path = getenv("WWHD_TEST_CONTROLLER_MENU") != nullptr;  // test aid: the fallback
    if (!menu_path && !g_direct_refused) {
        if (guest_call(c, fn_setMode, {mgr, (uint32_t)category}) && game_mode() == category) {
            if (category == 1) st8(mgr + kPointerEnabled, 1);  // the GamePad's touch, as the exchange does
            LOG("[controller] after the %s: the game was in mode %d; it plays with the %s now (mode %d)", g_kind, mode,
                category == 0 ? "Pro Controller" : "GamePad", game_mode());
            g_phase = kIdle;
            return;
        }
        g_direct_refused = true;
        LOG("[controller] after the %s: the game did not take mode %d directly; through the menu once in play", g_kind, category);
    }
    if (!play || !object(ld32(ui + kUiOptionScreen))) return;  // the menu path starts from play
    // the player opens the pause menu (StateID_ToMenu: screens off, game paused)
    guest_call(c, fn_changeState, {ui + kUiState, kToMenuState});
    LOG("[controller] after the %s: the game is in mode %d; switching it to the %s", g_kind, mode,
        category == 0 ? "Pro Controller" : "GamePad");
    g_category = category;
    g_phase = kWaitMenuOpen;
    g_frames = g_stable = 0;
}

// waits until the in-game UI has been in its menu state for a moment; false after a timeout
bool menu_settled(Cpu* c, uint32_t ui, const char* what) {
    if (++g_frames > kMenuFrames) {
        LOG("[controller] after the %s: %s (mode %d)", g_kind, what, game_mode());
        g_phase = kIdle;
        return false;
    }
    g_stable = ui_state(c, ui) == state_key(c, kMenuState) ? g_stable + 1 : 0;
    return g_stable >= kSettleFrames;
}

void wait_menu_open(Cpu* c, uint32_t ui) {
    if (!menu_settled(c, ui, "the menu did not open; not switched")) return;
    // Options -> controller: the category (hdSequenceOption_c::beginConnection), then the exchange
    // state the Options screen's close enters (hdIngameUi_c::optionExecute)
    st32(ld32(ui + kUiOptionScreen) + kOptionCategory, (uint32_t)g_category);
    guest_call(c, fn_changeState, {ui + kUiState, kExchangeState});
    g_phase = kWaitMenuBack;
    g_frames = g_stable = 0;
}

void wait_menu_back(Cpu* c, uint32_t ui) {
    if (!menu_settled(c, ui, "the controller switch did not return to its menu")) return;
    // the menu's own close, as when the player presses its close button
    const uint32_t mgr = ld32(kInputManager);
    const uint32_t hold = ld32(mgr + kHold);
    st32(mgr + kHold, hold | kCancel);
    guest_call(c, fn_menuExecute, {ui});
    st32(mgr + kHold, hold);
    LOG("[controller] after the %s: the game plays with the %s (mode %d)", g_kind,
        game_mode() == 0 ? "Pro Controller" : "GamePad", game_mode());
    g_phase = kIdle;
}

std::atomic<const char*> g_request{nullptr};

// Which controller is kept for the game while it follows the host's choice. Both holds end as soon as
// the game's mode has followed, the host switches back, or the re-apply ends (done, given up, or a
// load / reset that leaves the game in the host's mode).
void update_gamepad_hold() {
    // the controller the game still reads stays live: the switch waits for play, and a menu (pause
    // menu, file select) is left only by a press the game can hear (issue #330)
    const bool switching = g_phase != kIdle, pro = input::pro_controller();
    const int mode = game_mode();
    g_hold_gamepad = keeps_gamepad(switching, mode, pro, g_category);
    g_live_pro = switching && !pro && mode == 0 && g_category != 1;
}
void update_holds() {
    // the Pro Controller stays connected while the game still plays with it or is being switched off it
    g_hold_pro = g_phase != kIdle && !input::pro_controller() && (game_mode() == 0 || g_category == 1);
    update_gamepad_hold();
}

// the host's choice changed (Input menu, Controls, shortcut): the game follows it as after a load
void host_changed(bool pro) {
    if (!pro) g_hold_pro = true;  // no reconnect prompt before the game thread has looked at it
    g_live_pro = !pro && game_mode() == 0;  // no deaf Pro Controller read before then either
    // no deaf GamePad read before the game thread has looked at it (a GamePad game; no-op otherwise)
    g_hold_gamepad = pro && reads_gamepad(game_mode());
    request("controller change");
}
const bool g_listening = (input::set_controller_listener(host_changed), true);
}  // namespace

void request(const char* kind) { g_request = kind; }

bool holds_pro_controller() { return g_hold_pro.load(std::memory_order_relaxed); }
bool holds_gamepad() { return g_hold_gamepad.load(std::memory_order_relaxed); }
bool pro_controller_live() { return g_live_pro.load(std::memory_order_relaxed); }

void service(Cpu* c) {
    (void)g_listening;
    if (const char* kind = g_request.exchange(nullptr)) {
        g_phase = kWaitPlay;
        g_kind = kind;
        g_frames = g_stable = 0;
        g_category = -1;
        g_direct_refused = false;
        g_trace = 600;
    }
    trace_ui(c);
    update_holds();
    if (g_phase == kIdle) return;
    const uint32_t ui = ld32(kIngameUi);
    if (!object(ui) || !object(ld32(kInputManager))) {
        // title screen or a stage change: no in-game UI
        if (may_give_up(holds_gamepad(), ++g_frames, kGiveUpFrames)) g_phase = kIdle, update_gamepad_hold();
        return;
    }
    Cpu saved = *c;
    if (g_phase == kWaitPlay) wait_play(c, ui);
    else if (g_phase == kWaitMenuOpen) wait_menu_open(c, ui);
    else wait_menu_back(c, ui);
    restore(c, saved);
    update_gamepad_hold();  // a switch that just finished hands over before the game's next read
}
}  // namespace controller_reapply
