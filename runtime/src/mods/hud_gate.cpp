// The gameplay HUDs (the boost's bar, the climb wheel) step aside for the game's own screens. The
// HUDs are drawn by the host into the TV picture and know nothing of the game's menus, so once per
// frame this reads what the game shows and fades both out while a menu, an event or a message is up.
// The rules are hud_visibility.h; mod_hud.mm (Metal) and vulkan/present.cpp apply game_ui_hud_alpha().
//
// Signals (all read on the game thread at frame start, the same ones other host features use):
//   event    dComIfGp_event_runCheck: g_dComIfG_gameInfo.play + 0x5292 (savestate.cpp's "Link is
//            under control" check, the story mods' event guards, the boost's own dust guard)
//   message  dComIfGp_getMesgStatus / the Telescope's: play + 0x5BB2 / 0x5BB3 (savestate.cpp)
//   menu     dMenu_flag (savestate.cpp), the in-game UI's state machine in one of its menu states
//            (the GamePad auto picture-in-picture's table, gfx/auto_pip.h), the menu routed to the
//            GamePad (routing 2) or one of the item menu's bag windows open (gamepad_ui_state.cpp)
// Test aid: WWHD_TEST_HUD_TRACE=1 logs each change of the three signals.
// Test aid: WWHD_TEST_MENU_FOCUS_TRACE=1 logs each change of the menu event manager's focus (issue #336).
// Test aid: WWHD_TEST_MENU_FOCUS_FORCE=<logic step>:<element hex> puts the menu's focus on that element (#336 stop-gap check).
#include "mods.h"
#include "climb.h"
#include "hud_visibility.h"
#include "menu_cursor.h"
#include "guest_hud.h"
#include "../gfx/auto_pip.h"
#include "runtime.h"
#include "guest_addr.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace interp { uint64_t logic_steps(); }
namespace game_settings_page { bool page_open(); bool arrow_pane(uint32_t p); }  // (mods/game_settings_page.h)
namespace game_look { std::string pane_name(uint32_t pane); bool guest_pointer(uint32_t a); }  // (mods/game_look.h)
namespace overlay { bool is_open(); }  // (overlay/overlay.h)

namespace mods {
namespace {
const uint32_t kPlay = GD(0x1046F0B0);             // g_dComIfG_gameInfo.play
constexpr uint32_t kEventRun = 0x5292;             // dComIfGp_event_runCheck (dEvt_control_c mode)
constexpr uint32_t kMesgStatus = 0x5BB2, kScopeMesgStatus = 0x5BB3;  // dComIfGp_getMesgStatus (025F795C)
const uint32_t kMenuFlag = GD(0x101EA069);         // dMenu_flag (025986BC)
const uint32_t kIngameUi = GD(0x101F8344);         // SCENE-ingame_ui (hdIngameUi_c*)
const uint32_t kMenuRouting = GD(0x101F8348);      // 2: the pause/menu overlay is routed to the GamePad
bool object(uint32_t p, uint32_t size) { return (p & 3) == 0 && p >= mem::kMem2Start && p <= mem::kMem2End - size; }

std::atomic<float> g_alpha{1.f};
std::chrono::steady_clock::time_point g_last{};

GameUi read_game_ui() {
    GameUi ui;
    ui.event = ld8(kPlay + kEventRun) != 0;
    ui.message = ld8(kPlay + kMesgStatus) != 0 || ld8(kPlay + kScopeMesgStatus) != 0;
    bool menu = ld8(kMenuFlag) != 0 || ld32(kMenuRouting) == 2;
    const uint32_t scene = ld32(kIngameUi);
    if (!menu && object(scene, 0x24c)) {
        // hdStateMachine +8 is the current state object, its +8 the descriptor; with no current
        // object +4 is the pending descriptor (gamepad_ui_state.cpp)
        const uint32_t current = ld32(scene + 0x18);
        const uint32_t descriptor = object(current, 12) ? ld32(current + 8) : ld32(scene + 0x14);
        const uint32_t items = ld32(scene + 0x17c);
        menu = gfx::gamepad_scene_kind(descriptor) == gfx::GamePadSceneKind::Menu ||
               (object(items, 0x58) && ld32(items + 0x54) < 3);
    }
    ui.menu = menu;
    return ui;
}
}  // namespace

// the same reading for other host features (play_context.cpp), so the signals are read in one place
GameUi game_ui_now() { return read_game_ui(); }

// Issue #336 (the pause menu's selection cursor gone until the title screen): while the item menu is up, the
// game's own conditions for drawing its selection cursor (menu_cursor.h) are read every logic step; when the
// cursor has been missing for a second, one line says so, which condition is missing and which element holds
// the menu's focus; when it is drawn again (in that opening or a later one), one line says it is back. A
// player's log then tells a lost game state (the line) from a cursor the game drew but the picture lost (no
// line), and names the element the focus was left on. Reads only; at most 20 lines a session.
//   sInputOptions (*101F8378) +0x14   directional input on (the cursor shown, the menu moved by buttons)
//   sEventMgr (*101F4FF0) +0x70       the focused receiver (+0x74 the next one); its +0xC must be that
//                                     event manager (the element is linked into it)
//   sCursorMgrInstance (*101F5A44)    +0x18 the active cursor (0 focus, 1 fixed), +0x10 its cursors; the
//                                     cursor's +0x14 its state id, whose +4 is the name (::StateID_LoopAll)
//   sIngameUi +0x23D                  mMenuRequested (set by Game Over; the TV cursor is drawn only at 0)
// A receiver (cking::ui::input::Receiver, the menu's elements) as the line names it: its address, the list it
// is linked into (+0xC: "the menu's" when it is the event manager, "unlinked" at 0), its id (+0x10), its
// receiver vtable (+0x14), its pane's name with two parents (+0x18), and "cursor on the settings arrow" when its
// cursor pane (+0x5C) is the settings entry arrow's (game_settings_page.cpp).
static std::string describe_receiver(uint32_t r, uint32_t events) {
    char buf[96];
    if (!r) return "none";
    if (!object(r, 0x60)) return snprintf(buf, sizeof buf, "%08X (not a receiver)", r), buf;
    const uint32_t owner = ld32(r + 0xC);
    if (owner == events || !owner) snprintf(buf, sizeof buf, "%08X (%s", r, owner ? "the menu's" : "unlinked");
    else snprintf(buf, sizeof buf, "%08X (linked to %08X", r, owner);
    std::string s = buf;
    snprintf(buf, sizeof buf, ", id %u, vtable %08X", ld32(r + 0x10), ld32(r + 0x14));
    s += buf;
    std::string panes;
    for (uint32_t pane = ld32(r + 0x18), depth = 0; pane && game_look::guest_pointer(pane) && depth < 3; pane = ld32(pane + 0xC), ++depth) {
        const std::string name = game_look::pane_name(pane);
        if (name.empty()) break;
        panes += (depth ? "<" : "") + name;
    }
    if (!panes.empty()) s += ", pane " + panes;
    if (game_settings_page::arrow_pane(ld32(r + 0x5C))) s += ", cursor on the settings arrow";
    return s + ")";
}

static std::string guest_text(uint32_t a) {
    std::string s;
    for (uint32_t i = 0; i < 40 && a >= mem::kMem2Start && a + i < mem::kMem2End; i++) {
        const char ch = char(ld8(a + i));
        if (!ch) break;
        s += ch;
    }
    return s;
}

// Issue #336 stop-gap, until the trigger is found (menu_cursor.h Restore): the item menu up for a second with
// its focus on an element that isn't linked into the menu's event manager gets the focus back on the menu's
// default element, through the game's own setter cking::ui::input::EventMgr::setModeDefault (02615154:
// (manager, element) sets the focus and the next focus, as the menu's screens do when a window slides in).
// The [menu-cursor] lines before it name the element the focus was on: that is what is to find the trigger.
//   hdMenuMain_c (sIngameUi +0x17C): +0x50 mSelectionMode (0 the item window, 1 the other one), +0x48 its
//   children (count, data); a child (hdButtonPart_c) +0x50 its element
static uint32_t menu_default_focus(uint32_t scene) {
    const uint32_t items = ld32(scene + 0x17C);
    if (!object(items, 0x60)) return 0;
    const uint32_t mode = ld32(items + 0x50), count = ld32(items + 0x48), data = ld32(items + 0x4C);
    const uint32_t index = mode == 0 ? 0x11 : mode == 1 ? 0x28 : ~0u;
    if (index >= count || !object(data, 4 * (index + 1))) return 0;
    const uint32_t child = ld32(data + 4 * index);
    return object(child, 0x54) ? ld32(child + 0x50) : 0;
}

static void menu_cursor_watch(Cpu* c) {
    static int lines = 0;
    static menu_cursor::Watch watch;
    static menu_cursor::Restore restore;
    static uint32_t last_good = 0;  // the element the focus was last on in this opening, linked
    static const bool trace = getenv("WWHD_TEST_MENU_FOCUS_TRACE") != nullptr;  // test aid: every focus change
    static std::string traced;
    const uint32_t scene = ld32(kIngameUi);
    bool menu = false;
    if (object(scene, 0x24c)) {
        const uint32_t current = ld32(scene + 0x18);
        const uint32_t descriptor = object(current, 12) ? ld32(current + 8) : ld32(scene + 0x14);
        menu = descriptor == GD(0x1049ECB0);  // l_ingameMenuState
    }
    const uint32_t options = ld32(GD(0x101F8378)), events = ld32(GD(0x101F4FF0)), mgr = ld32(GD(0x101F5A44));
    // test aid: WWHD_TEST_MENU_FOCUS_FORCE=<logic step>:<element hex> puts the focus on that element (an unlinked
    // chart-grid cell gives the #336 state) through the game's setter, once, at that step with the item menu up
    static uint64_t force_step = 0;
    static uint32_t force_element = 0;
    static const bool force = [] {
        const char* e = getenv("WWHD_TEST_MENU_FOCUS_FORCE");
        return e && sscanf(e, "%llu:%x", (unsigned long long*)&force_step, &force_element) == 2;
    }();
    if (force && menu && c && force_step && interp::logic_steps() >= force_step && object(events, 0xE4)) {
        force_step = 0;
        const Cpu saved = *c;
        guest_call(c, GC(0x02615154), {events, force_element});
        *c = saved;
        LOG("[menu-focus] test: focus forced to %s", describe_receiver(force_element, events).c_str());
    }
    const uint32_t focus = object(events, 0xE4) ? ld32(events + 0x70) : 0;
    menu_cursor::Reading r;
    r.focus_linked = object(focus, 0x60) && ld32(focus + 0xC) == events;
    r.active = object(mgr, 0x20) ? int(ld32(mgr + 0x18)) : -1;
    r.focus = focus;
    r.focus_on_arrow = object(focus, 0x60) && game_settings_page::arrow_pane(ld32(focus + 0x5C));
    if (trace && object(events, 0xE4)) {
        const std::string now = describe_receiver(focus, events) + ", next " + describe_receiver(ld32(events + 0x74), events);
        if (now + (menu ? "+" : "") != traced) {
            traced = now + (menu ? "+" : "");
            LOG("[menu-focus] logic step %llu%s: focus %s; cursor %d", (unsigned long long)interp::logic_steps(),
                menu ? "" : " (item menu not up)", now.c_str(), r.active);
        }
    }
    if (!menu) { watch.reset(), restore.reset(), last_good = 0; return; }
    if (r.focus_linked && r.active == 0) last_good = focus;
    r.directional = object(options, 0x18) ? ld8(options + 0x14) : -1;
    const uint32_t cursor = r.active == 0 || r.active == 1 ? ld32(mgr + 0x10 + 4 * r.active) : 0;
    const uint32_t id = object(cursor, 0x54) ? ld32(cursor + 0x14) : 0;
    r.state = object(id, 8) ? guest_text(ld32(id + 4)) : "none";
    r.requested = ld8(scene + 0x23D);
    const uint64_t step = interp::logic_steps();
    const auto change = watch.update(menu_cursor::shown(r), step);
    const auto lost_line = [&] {
        return "[menu-cursor] the item menu is up without its selection cursor since logic step " +
               std::to_string(watch.since()) + " (issue #336): directional input " + std::to_string(r.directional) +
               ", cursor " + std::to_string(r.active) + " " + r.state + ", menu requested " + std::to_string(r.requested) +
               ", settings arrow or page " + std::to_string(int(game_settings_page::page_open())) + "; focus " +
               describe_receiver(focus, events) + ", next " +
               describe_receiver(object(events, 0xE4) ? ld32(events + 0x74) : 0, events);
    };
    bool logged = false;
    if (change != menu_cursor::Watch::Change::None && lines < 20) {
        lines++;
        if (change == menu_cursor::Watch::Change::Back) LOG("[menu-cursor] the item menu's selection cursor is back");
        else LOG("%s", lost_line().c_str()), logged = true;
    }
    const menu_cursor::Context context{overlay::is_open(), game_settings_page::page_open()};
    if (!c || !restore.update(r, context, step)) return;
    const uint32_t fallback = menu_default_focus(scene);
    const uint32_t target = menu_cursor::restore_target(fallback, object(fallback, 0x60) && ld32(fallback + 0xC) == events,
                                                        last_good, object(last_good, 0x60) && ld32(last_good + 0xC) == events);
    if (!logged) LOG("%s", lost_line().c_str());  // the line that names the element, before every restore
    if (!target) {
        LOG("[menu-cursor] focus not restored (issue #336 stop-gap): no element of the menu to give it to");
        return;
    }
    const Cpu saved = *c;
    guest_call(c, GC(0x02615154), {events, target});  // cking::ui::input::EventMgr::setModeDefault
    *c = saved;
    LOG("[menu-cursor] focus restored to %s (issue #336 stop-gap, %s)", describe_receiver(target, events).c_str(),
        target == fallback ? "the menu's default element" : "the element it was last on");
}

void hud_service(Cpu* c) {
    menu_cursor_watch(c);
    const auto now = std::chrono::steady_clock::now();
    const float seconds = g_last.time_since_epoch().count() ? std::min(0.25f, std::chrono::duration<float>(now - g_last).count()) : 0.f;
    g_last = now;
    // nothing to fade unless one of the HUD mods is on: no guest reads otherwise
    if (!move_speed() && !climb_enabled()) { g_alpha.store(1.f, std::memory_order_relaxed); return; }
    const GameUi ui = read_game_ui();
    static const bool trace = getenv("WWHD_TEST_HUD_TRACE") != nullptr;
    if (trace) {
        static int previous = -1;
        const int bits = int(ui.event) | int(ui.message) << 1 | int(ui.menu) << 2;
        if (bits != previous) {
            LOG("[mods] game UI: event=%d message=%d menu=%d -> HUD %s", ui.event, ui.message, ui.menu,
                game_ui_hides_hud(ui) ? "hidden" : "shown");
            previous = bits;
        }
    }
    g_alpha.store(game_ui_fade(g_alpha.load(std::memory_order_relaxed), game_ui_hides_hud(ui), seconds),
                  std::memory_order_relaxed);
}

float game_ui_hud_alpha() { return g_alpha.load(std::memory_order_relaxed); }

// Frame interpolation: the wheel's and the bar's stamina and fade advance once per logic step; the
// frames between two steps show them between the step before and this one (hud_drawn, called by
// interp.cpp after each pass is drawn, with its fraction). The guest HUD lists blend their elements
// the same way (guest_hud.h blend_lists). A value that changed since that pass (or jumped by more than
// half) is shown as it is. debug: WWHD_INTERP_MOD_HUD=0
namespace {
struct HudBlend {
    uint64_t step = ~0ull;
    float before = 0, after = 0;
    std::atomic<float> shown{0}, exact{0};
    void update(float v, float t, uint64_t step_now, bool blend) {
        if (step != step_now) {
            before = step + 1 == step_now ? after : v;
            after = v;
            step = step_now;
        }
        const float x = blend && after == v && std::fabs(v - before) <= 0.5f ? before + (v - before) * t : v;
        shown.store(x, std::memory_order_relaxed);
        exact.store(v, std::memory_order_relaxed);
    }
    float get(float v) const {
        return exact.load(std::memory_order_relaxed) == v ? shown.load(std::memory_order_relaxed) : v;
    }
};
HudBlend g_climb_stamina, g_climb_alpha, g_move_stamina, g_move_alpha;
}  // namespace

void hud_drawn(float t, uint64_t step, bool interpolating) {
    static const bool off = getenv("WWHD_INTERP_MOD_HUD") && !atoi(getenv("WWHD_INTERP_MOD_HUD"));
    const bool blend = interpolating && !off && t < 1.f;
    guestmods::hud::store().set_fraction(blend ? t : 1.f);
    const ClimbHud c = climb_hud();
    const MoveHud m = move_hud();
    g_climb_stamina.update(c.stamina, t, step, blend);
    g_climb_alpha.update(c.alpha, t, step, blend);
    g_move_stamina.update(m.stamina, t, step, blend);
    g_move_alpha.update(m.alpha, t, step, blend);
}
ClimbHud climb_hud_drawn() {
    ClimbHud c = climb_hud();
    c.stamina = g_climb_stamina.get(c.stamina);
    c.alpha = g_climb_alpha.get(c.alpha);
    return c;
}
MoveHud move_hud_drawn() {
    MoveHud m = move_hud();
    m.stamina = g_move_stamina.get(m.stamina);
    m.alpha = g_move_alpha.get(m.alpha);
    return m;
}
}  // namespace mods
