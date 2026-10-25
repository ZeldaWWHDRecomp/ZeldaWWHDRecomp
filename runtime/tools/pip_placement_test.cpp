#include "gfx/display_modes.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <limits>
void log_msg(const char *, ...) {}
namespace input {
void set_touch(bool, float, float) {}
} // namespace input
namespace aspect {
void set_window_aspect(float) {}
} // namespace aspect
// value nullptr removes the variable
static void test_env(const char *name, const char *value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}
static bool near(float a, float b) { return std::fabs(a - b) < 0.002f; }
int main() {
    using namespace gfx;
    const char *old[] = {"tl", "tr", "bl", "br"};
    const int migrated[] = {kPipTL, kPipTR, kPipBL, kPipBR};
    for (int i = 0; i < 4; ++i)
        assert(pip_load_anchor(nullptr, old[i]) == migrated[i]);
    assert(pip_load_anchor("cc", "br") == kPipCC);
    assert(pip_load_anchor("invalid", "br") == kPipBR);
    assert(pip_load_anchor(nullptr, nullptr) == kPipBR);
    assert(pip_clamp(NAN, 0.1f, 0.5f, 0.25f) == 0.25f);
    assert(pip_clamp(-1, 0.2f, 1, 1) == 0.2f);
    assert(pip_clamp(2, 0.2f, 1, 1) == 1);
    // #221 limits: size 5..100%, opacity 10..100%; values saved under the old limits stay as they were.
    assert(kPipSizeMin == 0.05f && kPipSizeMax == 1.0f && kPipOpacityMin == 0.1f && kPipOpacityMax == 1.0f);
    assert(pip_clamp_size(0.0f) == 0.05f && pip_clamp_size(-1) == 0.05f && pip_clamp_size(0.04f) == 0.05f);
    assert(pip_clamp_size(1.0f) == 1.0f && pip_clamp_size(1.5f) == 1.0f && pip_clamp_size(NAN) == 0.25f);
    for (float old : {0.1f, 0.2f, 0.25f, 0.33f, 0.4f, 0.5f}) assert(pip_clamp_size(old) == old);
    assert(pip_clamp_size(0.8f) == 0.8f && pip_clamp_size(0.05f) == 0.05f);
    assert(pip_clamp_opacity(0.0f) == 0.1f && pip_clamp_opacity(0.05f) == 0.1f && pip_clamp_opacity(2) == 1.0f);
    assert(pip_clamp_opacity(NAN) == 1.0f && pip_clamp_opacity(0.1f) == 0.1f);
    for (float old : {0.2f, 0.5f, 0.7f, 0.85f, 1.0f}) assert(pip_clamp_opacity(old) == old);
    // the test variable uses the same limits
    test_env("WWHD_DRC_PIP", "cc:0.03:0.02");
    display_env_overrides();
    assert(g_pip_anchor == kPipCC && g_pip_size == 0.05f && g_pip_opacity == 0.1f);
    test_env("WWHD_DRC_PIP", "br:0.8:0.5");
    display_env_overrides();
    assert(g_pip_anchor == kPipBR && g_pip_size == 0.8f && g_pip_opacity == 0.5f);
    test_env("WWHD_DRC_PIP", nullptr);
    g_pip_size = 0.25f;
    g_pip_opacity = 1.0f;
    assert(pip_place({}, 1, 0.25f, 0, 0).anchor == kPipCustom);
    for (float aspect : {16.0f / 9, 4.0f / 3, 32.0f / 9}) {
        const Box tv{31, 47, 1440, 1440 / aspect};
        for (float size : {-10.0f, 0.05f, 0.1f, 0.25f, 0.5f, 0.8f, 1.0f, 10.0f, NAN})
            for (int a = 0; a <= kPipCustom; ++a) {
                auto b = pip_box(tv, 854.0f / 480, size, {a, 0.73f, 0.62f});
                assert(b.x >= tv.x && b.y >= tv.y);
                assert(b.x + b.w <= tv.x + tv.w + 0.002f && b.y + b.h <= tv.y + tv.h + 0.002f);
                if (a == kPipBR)
                    assert(b.y + b.h <= tv.y + tv.h * kPipRupeeBottom + 0.002f);
                if (a == kPipTL) assert(b.y >= tv.y + tv.h * kPipLeftHudBottom - 0.002f);
                if (a == kPipTR) assert(b.y >= tv.y + tv.h * kPipRightHudBottom - 0.002f);
                const float x = (b.x - tv.x + b.w / 2) / tv.w, y = (b.y - tv.y + b.h / 2) / tv.h;
                auto mapped = pip_place(tv, 854.0f / 480, size, x, y);
                auto back = pip_box(tv, 854.0f / 480, size, mapped);
                assert(near(mapped.x, (back.x - tv.x + back.w / 2) / tv.w));
                assert(near(mapped.y, (back.y - tv.y + back.h / 2) / tv.h));
                if (a == kPipCustom) assert(near(b.x, back.x) && near(b.y, back.y));
                if (a < kPipCustom) {
                    // Dragging exactly onto a preset's spot keeps the free (Custom) position...
                    assert(mapped.anchor == kPipCustom);
                    assert(pip_place(tv, 854.0f / 480, size, x + 0.001f, y + 0.001f).anchor == kPipCustom);
                    // ...while a click near its target picks the preset, at every size.
                    float cx, cy;
                    pip_target_centre(tv, a, &cx, &cy);
                    assert(pip_target_at(tv, cx + 5, cy - 5, 12) == a);
                }
            }
        // An exact 50% Custom width fits even at 32:9; another anchor's HUD
        // restrictions must not silently reduce it.
        auto exact = pip_box(tv, 854.0f / 480, 0.5f, {kPipCustom, 0.5f, 0.5f});
        assert(near(exact.w, tv.w * 0.5f));
        for (float x : {-10.0f, 0.0f, 0.5f, 1.0f, 10.0f, NAN}) {
            auto b = pip_box(tv, 854.0f / 480, 0.5f, {kPipCustom, x, x});
            assert(b.x >= tv.x && b.y >= tv.y && b.x + b.w <= tv.x + tv.w + 0.002f &&
                   b.y + b.h <= tv.y + tv.h + 0.002f);
        }
        // 100% (Custom) is the TV picture's full width wherever the GamePad picture fits.
        if (aspect == 16.0f / 9) {
            auto full = pip_box(tv, 854.0f / 480, 1.0f, {kPipCustom, 0.3f, 0.7f});
            assert(near(full.w, tv.w) && near(full.x, tv.x));
        }
        // A drag beyond the TV edge is clamped to the edge but stays free (no snapping).
        auto edge = pip_place(tv, 854.0f / 480, 0.25f, -1, 0.5f);
        assert(edge.anchor == kPipCustom && near(edge.y, 0.5f));
        auto p = pip_place(tv, 854.0f / 480, 0.25f, 0.41f, 0.37f);
        assert(p.anchor == kPipCustom && near(p.x, 0.41f) && near(p.y, 0.37f));
        // The targets form a fixed 3x3 grid: in reading order, inside the picture, well apart.
        for (int a = 0; a < kPipCustom; ++a) {
            float x, y;
            pip_target_centre(tv, a, &x, &y);
            assert(x > tv.x && x < tv.x + tv.w && y > tv.y && y < tv.y + tv.h);
            for (int other = 0; other < a; ++other) {
                float ox, oy;
                pip_target_centre(tv, other, &ox, &oy);
                assert(std::hypot(x - ox, y - oy) >= 0.3f * tv.h);
                assert(a / 3 == other / 3 ? ox < x : oy < y);
            }
        }
        // Clicks away from every target, and invalid input, pick no preset.
        assert(pip_target_at(tv, tv.x + tv.w * 0.3f, tv.y + tv.h * 0.3f, 12) == -1);
        assert(pip_target_at(tv, NAN, tv.y, 12) == -1);
        assert(pip_target_at(tv, tv.x + tv.w * 0.5f, tv.y + tv.h * 0.5f, 0) == -1);
        assert(pip_target_at({}, 0, 0, 12) == -1);
        g_pip_anchor = kPipBR;
        auto l = layout(1440, 1440 / aspect, 1440, 1440 / aspect, 854, 480, true);
        assert(l.pip_on && l.pip.y + l.pip.h <= l.tv.y + l.tv.h * kPipRupeeBottom + 0.002f);
    }
    g_mode = kDrcOff;
    g_shown = false;
    g_pip_preview = true;
    auto p = display_plan(true, 1280, 720, true, 854, 480, 1920, 1080, 1);
    assert(p.pip_on && !p.drc_only);
    g_pip_preview = false;
    assert(!display_plan(true, 1280, 720, true, 854, 480, 1920, 1080, 2).pip_on);

    // Moving and resizing the picture with the pointer (macOS TV window: right-button or Option drag,
    // scroll wheel). The last published layout is a 1280x720 TV picture with a 320x180 GamePad at (900, 500).
    {
        PresentPlan plan;
        plan.dw = 1280, plan.dh = 720;
        plan.tv = {0, 0, 1280, 720};
        plan.pip = {900, 500, 320, 180};
        plan.pip_on = true;
        display_input_plan(plan);
        g_pip_anchor = kPipBR;
        g_pip_size = 0.25f;
        assert(!pip_drag_begin(0.1f, 0.1f) && !pip_dragging());  // off the picture: not taken
        assert(!pip_drag_end());
        // a drag from inside moves the picture by the pointer's motion and makes the placement Custom
        assert(pip_drag_begin(1000 / 1280.0f, 550 / 720.0f) && pip_dragging());
        pip_drag_move(600 / 1280.0f, 350 / 720.0f);
        assert(g_pip_anchor == kPipCustom && near(g_pip_size, 0.25f));
        assert(near(g_pip_x, (1060 - 400) / 1280.0f) && near(g_pip_y, (590 - 200) / 720.0f));
        // the picture stays inside the TV picture
        pip_drag_move(-1, -1);
        Box b = pip_box(plan.tv, 320.0f / 180, g_pip_size, pip_position());
        assert(near(b.x, 0) && near(b.y, 0));
        assert(pip_drag_end() && !pip_dragging());
        // a drag from the bottom-right corner resizes it; the top-left corner stays
        assert(pip_drag_begin(1215 / 1280.0f, 675 / 720.0f));
        pip_drag_move(1220 / 1280.0f, 700 / 720.0f);
        const float w = std::max(1220.0f - 900, (700.0f - 500) * (320.0f / 180));
        assert(near(g_pip_size, w / 1280));
        b = pip_box(plan.tv, 320.0f / 180, g_pip_size, pip_position());
        assert(std::fabs(b.x - 900) < 0.5f && std::fabs(b.y - 500) < 0.5f && std::fabs(b.w - w) < 0.5f);
        assert(pip_drag_end());
        // the wheel resizes the picture under the pointer by 10% a step, within the size limits
        g_pip_size = 0.25f;
        assert(pip_wheel(1000 / 1280.0f, 550 / 720.0f, 1) && near(g_pip_size, 0.275f));
        assert(pip_wheel(1000 / 1280.0f, 550 / 720.0f, -100) && near(g_pip_size, kPipSizeMin));
        assert(!pip_wheel(0.1f, 0.1f, 1) && !pip_wheel(1000 / 1280.0f, 550 / 720.0f, 0));
        // GamePad only: the picture fills the window and is not moved
        plan.drc_only = true;
        display_input_plan(plan);
        assert(!pip_drag_begin(1000 / 1280.0f, 550 / 720.0f));
    }
}
