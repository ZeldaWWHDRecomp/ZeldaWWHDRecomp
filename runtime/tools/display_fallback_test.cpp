#include "gfx/display_modes.h"
#include <cassert>

// Host effects are irrelevant to the actual shared layout/state exercised here.
void log_msg(const char*, ...) {}
namespace input { void set_touch(bool, float, float) {} }
namespace aspect { void set_window_aspect(float) {} }

static gfx::PresentPlan frame(unsigned i) {
    return gfx::display_plan(true, 1280, 720, true, 854, 480, 1920, 1080, i);
}
int main() {
    // Android start (#222): the saved Off read by Java holds until the native settings load,
    // which then decides, including a missing key; a late seed cannot override it.
    gfx::seed_second_screen_off(true);
    assert(gfx::drc_screen_off());
    gfx::seed_second_screen_off(false);
    assert(!gfx::drc_screen_off());
    gfx::seed_second_screen_off(true);
    gfx::load_second_screen_off(false);
    assert(!gfx::drc_screen_off());
    gfx::seed_second_screen_off(true);
    assert(!gfx::drc_screen_off());
    gfx::load_second_screen_off(true);
    gfx::seed_second_screen_off(false);
    assert(gfx::drc_screen_off());
    gfx::g_second_screen_off = false;
    // Android's default Off mode on a phone is not a request to release a display.
    gfx::g_has_drc_window = false;
    gfx::g_second_display_available = false;
    gfx::display_set_mode(gfx::kDrcOff);
    assert(!gfx::drc_screen_off());
    assert(!gfx::drc_mode_offered(gfx::kDrcWindow));
    // A released surface must not remove the Settings choice or prevent reacquisition.
    gfx::g_second_display_available = true;
    gfx::g_has_drc_window = false;
    assert(gfx::drc_mode_offered(gfx::kDrcWindow));
    gfx::display_set_mode(gfx::kDrcOff);
    assert(gfx::drc_screen_off());
    assert(!frame(0).pip_on && !frame(0).drc_only);
    gfx::display_set_mode(gfx::kDrcWindow);
    assert(!gfx::drc_screen_off());
    gfx::display_show_drc(false);
    assert(gfx::drc_screen_off());
    gfx::display_show_drc(true);
    assert(!gfx::drc_screen_off());
    gfx::display_set_mode(gfx::kDrcAuto);
    gfx::display_show_drc(false);
    assert(gfx::drc_screen_off());
    gfx::display_show_drc(true);
    assert(!gfx::drc_screen_off());
    gfx::display_set_mode(gfx::kDrcGamePad);
    gfx::display_show_drc(false);
    assert(!frame(0).drc_only && !frame(0).pip_on);
    gfx::display_show_drc(true);
    assert(frame(0).drc_only);
    // The saved Off flag survives loss of capability and a single-screen mode change.
    gfx::g_second_screen_off = true;
    gfx::g_second_display_available = false;
    gfx::display_set_mode(gfx::kDrcPip);
    assert(gfx::drc_screen_off());
    assert(!gfx::drc_mode_offered(gfx::kDrcWindow));
    gfx::g_second_display_available = true;
    gfx::display_set_mode(gfx::kDrcWindow);
    assert(!gfx::drc_screen_off());
    gfx::g_second_display_available = false;
    gfx::g_mode = gfx::kDrcWindow;
    gfx::g_shown = true;
    for (unsigned i = 0; i < 20; ++i) {
        gfx::g_has_drc_window = true;
        assert(gfx::drc_mode_offered(gfx::kDrcWindow));
        auto connected = frame(i * 3);
        assert(connected.drc_window && !connected.pip_on);
        gfx::g_has_drc_window = false;
        auto removed = frame(i * 3 + 1);
        assert(!removed.drc_window && removed.pip_on);
        assert(removed.pip.w > 0 && removed.pip.h > 0);
        assert(gfx::g_mode == gfx::kDrcWindow); // restore the user's request on reconnect
        gfx::g_has_drc_window = true;
        auto restored = frame(i * 3 + 2);
        assert(restored.drc_window && !restored.pip_on);
    }
    gfx::g_has_drc_window = false;
    gfx::g_shown = false;
    assert(!frame(61).pip_on);
    gfx::g_shown = true;
    gfx::g_mode = gfx::kDrcOff;
    assert(!frame(62).pip_on);
    gfx::g_mode = gfx::kDrcGamePad;
    assert(frame(63).drc_only);
}
