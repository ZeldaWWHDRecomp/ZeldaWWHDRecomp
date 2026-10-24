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
#include "mods.h"
#include "climb.h"
#include "hud_visibility.h"
#include "guest_hud.h"
#include "../gfx/auto_pip.h"
#include "runtime.h"
#include "guest_addr.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>

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

void hud_service() {
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
