#include "gfx/display_modes.h"
#include "gfx/auto_pip.h"
#include "gfx/auto_pip_options.h"
#include <cassert>
#include <cstdlib>
#include <map>
#include <string>

void log_msg(const char*, ...) {}
namespace input { void set_touch(bool, float, float) {} }
namespace aspect { void set_window_aspect(float) {} }
static void set_env(const char* name, const char* value) {  // "" clears it
#ifdef _WIN32
    _putenv_s(name, value);
#else
    if (*value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}
// the saved settings (display.plist / settings.ini) for auto_pip_options.h
static std::map<std::string, std::string> g_saved;
namespace hostui {
bool get(const char* key, std::string& value) {
    auto found = g_saved.find(key);
    if (found == g_saved.end()) return false;
    value = found->second;
    return true;
}
void set(const char* key, const std::string& value) { g_saved[key] = value; }
}
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
    // the game's menus on the GamePad's touch screen, also in Off-TV Play (One screen mode's touches and clicks)
    assert(gamepad_touch_menu_state(GD(0x1049EC00), 0, 2, true) && gamepad_touch_menu_state(GD(0x1049EC00), 0, 1, true));
    assert(!gamepad_touch_menu_state(GD(0x1049EC00), 0, 2) && !gamepad_touch_menu_state(GD(0x1049EC00), 0, 0, true));
    assert(gamepad_touch_menu_state(GD(0x1049EC84), 0, 2) && !gamepad_touch_menu_state(GD(0x1049EC84), 0, 0));
    assert(!gamepad_touch_menu_state(GD(0x1049ED08), 4, 2));  // Game Over's save window: on the TV
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
    // #694 Song list while conducting: Hide. The policy: conducting alone opens nothing, menus still do.
    {
        AutoPipDecision hide;
        hide.update(baton, 400, false);
        assert(!hide.shown(baton, false, 400.1, false) && hide.until == 0);
        hide.update(menu, 401, false);
        assert(hide.shown(menu, false, 401, false));
        GamePadUiState menu_while_conducting{true, true, false, false, true};
        hide.update(menu_while_conducting, 402, false);
        assert(hide.shown(menu_while_conducting, false, 402, false));
        hide.update(baton, 402.1, false); // the menu closed, Link still conducts: only the closing delay
        assert(hide.shown(baton, false, 402.2, false) && !hide.shown(baton, false, 402.4, false));
        assert(hide.shown(baton, false, 402.4, false, true)); // a peek shows it while Link conducts
        assert(!hide.shown(play, false, 402.4, false, true));  // and not after
        assert(!hide.shown({true, false, true, false, true}, false, 403, false, true)); // Off-TV: full size anyway
        assert(hide.shown(baton, true, 403, false)); // a pin still shows everything
        // the shortcut: a press while conducting peeks (no pin), a press while pinned or peeking clears both
        auto t = auto_pip_show(true, false, baton, false);
        assert(!t.pin && t.peek);
        t = auto_pip_show(true, false, play, false);
        assert(t.pin && !t.peek); // not conducting: an ordinary pin
        t = auto_pip_show(true, true, baton, false);
        assert(t.pin && !t.peek); // already pinned (the Settings box): stays a pin
        t = auto_pip_show(true, false, baton, true);
        assert(t.pin && !t.peek); // Show: the pin as before
        t = auto_pip_show(false, true, baton, false);
        assert(!t.pin && !t.peek);
    }
    display_set_mode(kDrcAuto);
    g_auto_song_list = false;
    display_gamepad_ui(play);
    assert(!frame().pip_on);
    display_gamepad_ui(baton);
    assert(!frame().pip_on && g_auto_until == 0); // conducting: the song list stays off the TV
    display_gamepad_ui(menu);
    assert(frame().pip_on); // the menus still open it
    display_gamepad_ui(play);
    g_auto_until = 0;
    display_gamepad_ui(baton);
    assert(!frame().pip_on && !drc_screen_shown(false));
    display_show_drc(!drc_screen_shown(false)); // the PiP shortcut while conducting: shows the song list
    assert(frame().pip_on && drc_screen_shown(false) && g_auto_peek && !g_auto_pin);
    display_gamepad_ui(baton);
    assert(frame().pip_on); // stays up for the whole song
    display_show_drc(!drc_screen_shown(false)); // again: hidden
    assert(!frame().pip_on && !drc_screen_shown(false) && !g_auto_peek && !g_auto_pin);
    display_show_drc(!drc_screen_shown(false)); // and up again
    assert(frame().pip_on);
    display_gamepad_ui(play); // the song ends: the peek ends with it, nothing stuck open
    assert(!frame().pip_on && !g_auto_peek && !g_auto_pin && !drc_screen_shown(false));
    display_gamepad_ui(menu);
    display_gamepad_ui(play);
    g_auto_until = 0;
    assert(!frame().pip_on);
    display_show_drc(true); // a pin made in play survives conducting and stays after it
    display_gamepad_ui(baton);
    assert(frame().pip_on && g_auto_pin && !g_auto_peek);
    display_gamepad_ui(play);
    assert(frame().pip_on && drc_screen_shown(false));
    display_show_drc(false);
    assert(!frame().pip_on);
    display_gamepad_ui(baton);
    display_show_drc(true);
    display_gamepad_ui({true, false, true, false, true}); // Off-TV mid-song: full size, the peek ends
    assert(frame().drc_only && !g_auto_peek);
    display_gamepad_ui(play);
    assert(!frame().pip_on);
    g_auto_song_list = true; // Show again: conducting opens it as before
    display_gamepad_ui(baton);
    assert(frame().pip_on);
    display_gamepad_ui(play);
    g_auto_until = 0;
    display_gamepad_ui(baton);
    display_show_drc(true); // Show: the shortcut pins, as before
    assert(g_auto_pin && !g_auto_peek);
    display_gamepad_ui(play);
    g_auto_until = 0;
    assert(frame().pip_on);
    display_show_drc(false);
    display_gamepad_ui(baton);
    display_set_mode(kDrcPip); // Hide does not touch manual picture-in-picture
    g_auto_song_list = false;
    assert(frame().pip_on);
    display_set_mode(kDrcAuto);
    g_auto_song_list = true;
    // the option is saved; Show is the default; WWHD_AUTO_PIP_BATON=0|1 overrides it for a start
    {
        namespace o = auto_pip_options;
        set_env("WWHD_AUTO_PIP_BATON", "");
        g_auto_song_list = false;
        o::load_options();
        assert(g_auto_song_list && g_saved.empty());
        o::set_option(false);
        assert(!g_auto_song_list && g_saved.at(o::kSettingKey) == "0");
        g_auto_song_list = true;
        o::load_options();
        assert(!g_auto_song_list);
        set_env("WWHD_AUTO_PIP_BATON", "1");
        o::load_options();
        assert(g_auto_song_list && g_saved.at(o::kSettingKey) == "0"); // not saved
        set_env("WWHD_AUTO_PIP_BATON", "0");
        g_saved.clear();
        o::load_options();
        assert(!g_auto_song_list && g_saved.empty());
        set_env("WWHD_AUTO_PIP_BATON", "");
        g_auto_peek = true;
        o::set_option(true);
        assert(g_auto_song_list && !g_auto_peek && g_saved.at(o::kSettingKey) == "1");
    }
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
    // the Game Over save window (routing 4) is not a GamePad menu; from the pause menu (routing 2) it is
    assert(gameover_save_window(GD(0x1049ED34), 4) && gameover_save_window(GD(0x1049ED08), 4));
    assert(!gameover_save_window(GD(0x1049ED34), 2) && !gameover_save_window(GD(0x1049EC84), 4));
}
