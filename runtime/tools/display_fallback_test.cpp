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
    gfx::g_mode = gfx::kDrcWindow;
    gfx::g_shown = true;
    for (unsigned i = 0; i < 20; ++i) {
        gfx::g_has_drc_window = true;
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
