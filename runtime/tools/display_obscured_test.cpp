// Issue #349: system bars drawn over the TV surface (Android's navigation bar over the bottom of a 4:3
// screen) are left out of the TV layout and of "Match window".
#include "gfx/display_modes.h"
#include <cassert>
#include <cmath>

void log_msg(const char*, ...) {}
namespace input { void set_touch(bool, float, float) {} }
static float g_window_aspect = 0;
namespace aspect { void set_window_aspect(float a) { g_window_aspect = a; } }
namespace tv_pointer { bool wanted(unsigned) { return false; } }
namespace mods { bool guest_touch_claimed_at(float, float) { return false; } }

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main() {
    gfx::g_has_drc_window = false;
    gfx::display_set_mode(gfx::kDrcOff);
    // nothing covered: the whole surface, as before
    const float none[4] = {0, 0, 0, 0};
    auto v = gfx::visible_area(1280, 960, none, 1280, 960);
    assert(v.x == 0 && v.y == 0 && v.w == 1280 && v.h == 960);
    // a view without a size yet: the whole surface
    const float bar[4] = {0, 0, 0, 48};
    v = gfx::visible_area(1280, 960, bar, 0, 0);
    assert(v.w == 1280 && v.h == 960);
    // the navigation bar over the bottom 48 view pixels of a 1280x960 surface
    v = gfx::visible_area(1280, 960, bar, 1280, 960);
    assert(v.x == 0 && v.y == 0 && v.w == 1280 && v.h == 912);
    // the view at another size than the surface (scaled surface): the bar in surface pixels
    v = gfx::visible_area(1280, 960, bar, 2560, 1920);
    assert(v.w == 1280 && v.h == 936);
    // bars on the left and top (rotated screens, status bar)
    const float lt[4] = {40, 24, 0, 0};
    v = gfx::visible_area(1920, 1080, lt, 1920, 1080);
    assert(v.x == 40 && v.y == 24 && v.w == 1880 && v.h == 1056);
    // a report covering most of the surface (keyboard, stale size) is no bar: ignored
    const float huge[4] = {0, 0, 0, 700};
    v = gfx::visible_area(1280, 960, huge, 1280, 960);
    assert(v.w == 1280 && v.h == 960);

    // the plan: nothing reported -> the full 4:3 surface, the TV picture fills it
    auto p = gfx::display_plan(true, 1280, 960, false, 0, 0, 1280, 960, 1);
    assert(near(g_window_aspect, 1280.0f / 960.0f));
    assert(p.dw == 1280 && p.dh == 960 && p.tv.x == 0 && p.tv.y == 0 && p.tv.w == 1280 && p.tv.h == 960);
    // the bar reported: "Match window" follows the visible 1280x912, and the picture (made for it) sits above the bar
    gfx::display_set_obscured(0, 0, 0, 48, 1280, 960);
    p = gfx::display_plan(true, 1280, 912, false, 0, 0, 1280, 960, 2);
    assert(near(g_window_aspect, 1280.0f / 912.0f));
    assert(p.dw == 1280 && p.dh == 960);
    assert(p.tv.x == 0 && p.tv.y == 0 && p.tv.w == 1280 && p.tv.h == 912);
    // a picture of the previous shape (the frame before the aspect change): fitted into the visible part
    p = gfx::display_plan(true, 1280, 960, false, 0, 0, 1280, 960, 3);
    assert(p.tv.y >= 0 && p.tv.y + p.tv.h <= 912 && p.tv.h == 912);
    // the same layout for another target size (display_plan_for): the bar scaled to it
    auto q = gfx::display_plan_for(p, 2560, 1920, 1280, 912, 0, 0);
    assert(q.dw == 2560 && q.dh == 1920 && q.tv.y == 0 && q.tv.h == 1824 && q.tv.w == 2560);
    // a status bar at the top: the picture starts below it; touches map through the shifted box
    gfx::display_set_obscured(0, 30, 0, 0, 1920, 1080);
    p = gfx::display_plan(true, 1920, 1050, false, 0, 0, 1920, 1080, 4);
    assert(p.tv.y == 30 && p.tv.h == 1050 && p.tv.w == 1920);
    float x, y, w, h;
    assert(gfx::main_picture(&x, &y, &w, &h));
    assert(near(y, 30.0f / 1080.0f) && near(h, 1050.0f / 1080.0f));
    // the bars hidden again: the full surface
    gfx::display_set_obscured(0, 0, 0, 0, 1920, 1080);
    p = gfx::display_plan(true, 1920, 1080, false, 0, 0, 1920, 1080, 5);
    assert(near(g_window_aspect, 16.0f / 9.0f));
    assert(p.tv.x == 0 && p.tv.y == 0 && p.tv.w == 1920 && p.tv.h == 1080);
    return 0;
}
