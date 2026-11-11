#include "gfx/display_modes.h"
#include "gfx/auto_pip.h"
#include "gfx/gameover_pane.h"
#include <cassert>
#include <vector>
#include <cstdlib>

void log_msg(const char*, ...) {}
namespace input { void set_touch(bool, float, float) {} }
namespace aspect { void set_window_aspect(float) {} }
static gfx::PresentPlan frame() {
    return gfx::display_plan(true, 1280, 720, true, 854, 480, 1920, 1080, 1);
}
int main() {
    using namespace gfx;
    AutoPipDecision decision;
    GamePadUiState play{true, false, false, false}, menu{true, true, false, false};
    assert(!decision.shown(play, false, 0));
    decision.update(menu, 1);
    assert(decision.shown(menu, false, 100)); // menu remains visible indefinitely
    decision.update(menu, 100);
    decision.update(play, 100.1);
    assert(decision.shown(play, false, 100.2));
    assert(!decision.shown(play, false, 100.25));
    assert(decision.shown(play, true, 200));
    assert(decision.shown({}, true, 200)); // pin also works before entering a game
    for (GamePadUiState excluded : {GamePadUiState{true, true, true, false},
                                    GamePadUiState{true, true, false, true}}) {
        decision.update(excluded, 201);
        assert(decision.until == 0);
        assert(!decision.shown(excluded, true, 201));
    }
    assert(!decision.shown(play, false, 202)); // no stale hold after Off-TV switch
    decision.update(menu, 203);
    decision.update({}, 203.1);
    assert(!decision.shown({}, false, 203.1)); // scene destroyed/state load
    // Wind Waker: the song list is on the GamePad while Link conducts, gone ~250 ms after.
    GamePadUiState baton{true, false, false, false, true};
    AutoPipDecision conduct;
    conduct.update(play, 300);
    assert(!conduct.shown(play, false, 300));
    conduct.update(baton, 301);
    assert(conduct.shown(baton, false, 340)); // the whole song, however long
    conduct.update(baton, 340);
    conduct.update(play, 340.1);
    assert(conduct.shown(play, false, 340.2));
    assert(!conduct.shown(play, false, 340.3));
    assert(!conduct.shown({true, false, true, false, true}, false, 341)); // Off-TV shows it full size
    assert(!conduct.shown({true, false, false, true, true}, false, 341)); // TV/GamePad switch
    assert(!conduct.shown({false, false, false, false, true}, false, 341)); // no scene yet
    const uint32_t play_state = GD(0x1049EC00), songs = GD(0x1049EDE4), opening = GD(0x1049EDB8);
    assert(gamepad_baton_state(play_state, true, 1)); // flag first, before the song list opens
    assert(gamepad_baton_state(opening, true, 1) && gamepad_baton_state(songs, true, 1));
    assert(gamepad_baton_state(songs, false, 1)); // song list still closing after the flag
    assert(!gamepad_baton_state(play_state, false, 1));
    assert(!gamepad_baton_state(songs, true, 0)); // Pro Controller: no GamePad screen
    assert(!gamepad_baton_state(songs, true, 2)); // Off-TV
    assert(!gamepad_menu_state(songs, 0, 1)); // not a menu: the HUD gate keeps mod HUDs
    for (uint32_t state : {GD(0x1049EC84), GD(0x1049ECB0), GD(0x1049ECDC), GD(0x1049ED34),
                            GD(0x1049EE3C), GD(0x1049EF18), GD(0x1049F204), GD(0x1049F338)}) {
        assert(gamepad_menu_state(state, 0, 1));
        assert(!gamepad_menu_state(state, 0, 0)); // menu is on TV in Pro mode
        assert(!gamepad_menu_state(state, 0, 2)); // Off-TV
    }
    assert(gamepad_menu_state(GD(0x1049EC00), 2, 1));
    assert(gamepad_menu_state(GD(0x1049EC00), 0, 1, true)); // bag may open within Play
    assert(!gamepad_menu_state(GD(0x1049EC00), 0, 0, true));
    for (uint32_t state : {GD(0x1049EC00), GD(0x1049EC2C), GD(0x1049EC58), GD(0x1049F364), GD(0x1049F3BC)})
        assert(!gamepad_menu_state(state, 4, 1)); // ordinary gameplay/cutscenes
    // the save window: on the GamePad from the pause menu (routing 2), on the TV at Game Over (routing 4)
    for (uint32_t state : {GD(0x1049ED08), GD(0x1049ED34)}) {
        assert(gamepad_menu_state(state, 2, 1));
        assert(!gamepad_menu_state(state, 4, 1));
    }
    for (uint32_t state : {GD(0x1049F498), GD(0x1049F4C4), GD(0x1049F4F0)}) assert(gamepad_switch_state(state));
    display_set_mode(kDrcAuto);
    display_gamepad_ui(play);
    assert(!frame().pip_on);
    display_gamepad_ui(menu);
    assert(frame().pip_on && !frame().drc_only);
    assert(!drc_screen_shown(false)); // Cmd+G pins an already visible menu
    display_show_drc(!drc_screen_shown(false));
    assert(g_auto_pin && drc_screen_shown(false));
    display_show_drc(false);
    display_gamepad_ui(play);
    assert(frame().pip_on);
    g_auto_until = 0; // deterministic expiration, no sleeps
    assert(!frame().pip_on);
    display_show_drc(true);
    assert(frame().pip_on);
    display_gamepad_ui({true, false, true, true});
    assert(frame().drc_only); // game has moved the main picture to DRC
    display_gamepad_ui({true, false, false, true});
    assert(!frame().pip_on); // switch to TV, pin retained but temporarily suppressed
    display_gamepad_ui(play);
    assert(frame().pip_on);
    display_show_drc(false);
    assert(!frame().pip_on);
    display_gamepad_ui(baton);
    assert(frame().pip_on && !frame().drc_only);
    display_gamepad_ui(play);
    assert(frame().pip_on); // closing delay
    g_auto_until = 0;
    assert(!frame().pip_on);
    for (int mode : {kDrcWindow, kDrcPip, kDrcOff, kDrcGamePad}) {
        display_set_mode(mode);
        g_has_drc_window = true;
        assert(!display_gamepad_state_needed());
        const auto before = frame();
        display_gamepad_ui({true, true, true, true});
        const auto after = frame();
        assert(before.pip_on == after.pip_on && before.drc_only == after.drc_only && before.drc_window == after.drc_window);
    }
    // Optional pause view preserves the saved view and respects a manual choice.
    if (getenv("WWHD_DRC_PAUSE")) {
        display_set_mode(kDrcOff);
        display_plus_pressed();
        assert(display_gamepad_state_needed());
        display_gamepad_ui(menu);
        assert(g_mode == kDrcGamePad && display_saved_mode() == kDrcOff);
        display_gamepad_ui(play);
        assert(g_mode == kDrcOff);
        display_plus_pressed(); display_gamepad_ui(menu);
        display_set_mode(kDrcPip); display_gamepad_ui(play);
        assert(g_mode == kDrcPip && display_saved_mode() == kDrcPip);
        display_set_mode(kDrcAuto); display_plus_pressed(); display_gamepad_ui(menu);
        assert(g_mode == kDrcAuto && frame().pip_on && !frame().drc_only);
    }
    // the Game Over save window's black pane over the GamePad picture (gameover_pane.h)
    {
        uint32_t res[7 * 2] = {};
        auto tex = [&](int unit, uint32_t w, uint32_t h, uint32_t fmt) {
            res[unit * 7] = (w - 1) << 19;
            res[unit * 7 + 1] = (h - 1) | (fmt << 26);
        };
        tex(0, 8, 8, 0x34);
        tex(1, 64, 64, 0x35);
        const uint8_t one[1] = {0}, two[2] = {0, 1};
        gameover_pane::g_save_prompt = false;
        assert(!gameover_pane::skip(854, 480, 1, one, res));  // not at the prompt
        gameover_pane::g_save_prompt = true;
        assert(gameover_pane::skip(854, 480, 1, one, res));    // the pane
        assert(!gameover_pane::skip(1280, 720, 1, one, res));  // the TV's draws stay
        assert(!gameover_pane::skip(854, 480, 2, two, res));   // the window's own 8x8 panes (with their frame texture)
        tex(0, 30, 30, 0x35);
        assert(!gameover_pane::skip(854, 480, 1, one, res));   // other textured quads
        gameover_pane::g_save_prompt = false;
        assert(gameover_save_window(GD(0x1049ED34), 4) && !gameover_save_window(GD(0x1049ED34), 2) &&
               !gameover_save_window(GD(0x1049EC84), 4));
    }
    // a tap on the GamePad's Cancel / Save: the direction onto it, a gap, then A
    {
        using gameover_pane::Tap;
        // touched at x,y, then (still touching) at upX,upY, then let go
        auto tap = [](Tap& t, float x, float y, float upX, float upY) {
            std::vector<uint32_t> out;
            out.push_back(t.step(true, true, x, y));
            if (t.step(true, true, upX, upY) != 0) out.push_back(~0u);  // nothing while the finger is down
            out.push_back(t.step(true, false, 0, 0));
            for (int i = 0; i < 3 * Tap::kHold + 2; i++) out.push_back(t.step(true, false, 0, 0));
            return out;
        };
        Tap t;
        auto out = tap(t, 0.35f, 0.69f, 0.35f, 0.69f);  // Cancel
        assert(out[0] == 0);
        for (int i = 0; i < Tap::kHold; i++) assert(out[1 + i] == Tap::kLeft);
        for (int i = 0; i < Tap::kHold; i++) assert(out[1 + Tap::kHold + i] == 0);
        for (int i = 0; i < Tap::kHold; i++) assert(out[1 + 2 * Tap::kHold + i] == Tap::kA);
        assert(out.back() == 0);
        out = tap(t, 0.65f, 0.69f, 0.65f, 0.69f);  // Save
        assert(out[1] == Tap::kRight && out[1 + 2 * Tap::kHold] == Tap::kA);
        out = tap(t, 0.35f, 0.69f, 0.65f, 0.69f);  // pressed on Cancel, let go on Save: nothing
        for (uint32_t b : out) assert(b == 0);
        out = tap(t, 0.5f, 0.3f, 0.5f, 0.3f);  // the window's text: nothing
        for (uint32_t b : out) assert(b == 0);
        Tap off;
        assert(off.step(false, true, 0.35f, 0.69f) == 0 && off.step(false, false, 0.35f, 0.69f) == 0 &&
               off.step(false, false, 0, 0) == 0);  // not at the prompt
    }
}
