// camera_swipe.h (swipe camera scaling) and touch_style.h (the touch options' rules).
#include "mods/camera_mouse.h"
#include "mods/camera_swipe.h"
#include "platform/touch_style.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

static bool near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) < eps; }

int main() {
    using namespace mods::camera_swipe;
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    // one inch at speed 1 is kDegreesPerInch, on any display density
    for (float dpi : {160.f, 320.f, 400.f, 560.f}) {
        assert(near(degrees(dpi, dpi, 1.0f), kDegreesPerInch));
        assert(near(degrees(-dpi / 2, dpi, 1.0f), -kDegreesPerInch / 2));
        assert(near(degrees(dpi, dpi, 2.0f), 2 * kDegreesPerInch));
    }
    // linear in distance and speed, no dead zone, sign kept
    assert(degrees(0, 400, 1) == 0);
    assert(near(degrees(1, 400, 1) * 37, degrees(37, 400, 1)));
    assert(near(degrees(100, 400, 0.5f) * 2, degrees(100, 400, 1)));
    // speed is clamped to its range; non-finite falls back to the default
    assert(near(degrees(400, 400, 10), kSpeedMax * kDegreesPerInch));
    assert(near(degrees(400, 400, 0.01f), kSpeedMin * kDegreesPerInch));
    assert(near(degrees(400, 400, nan), kSpeedDefault * kDegreesPerInch));
    // a display DPI that is missing or absurd counts as kFallbackDpi
    for (float dpi : {0.f, -1.f, 10.f, 5000.f, nan, inf}) assert(near(degrees(kFallbackDpi, dpi, 1), kDegreesPerInch));
    assert(degrees(nan, 400, 1) == 0 && degrees(inf, 400, 1) == 0);
    // the boat factor: only while sailing and not looking through first person / aiming
    assert(step_factor(false, false, 3) == 1);
    assert(step_factor(true, true, 3) == 1);
    assert(step_factor(true, false, 3) == 3);
    assert(step_factor(true, false, 0.2f) == kBoatMin && step_factor(true, false, 99) == kBoatMax);
    assert(step_factor(true, false, nan) == kBoatDefault);
    // through the manualCamera site (camera.cpp: yaw_speed(degrees, 1, f25)): the camera turns by the
    // swipe's degrees whatever this camera style's turning factor; a swipe to the right turns like the
    // stick to the right (negative speed)
    for (double f25 : {4.0, 8.0, 12.0}) {
        const float deg = degrees(200, 400, 1.5f) * step_factor(true, false, 2);
        const float speed = mods::camera_mouse::yaw_speed(deg, 1.0f, f25);
        assert(speed < 0);
        assert(near(-speed * 0.92 * f25, 0.5 * kDegreesPerInch * 1.5 * 2, 1e-3));
    }
    // a whole phone half (2.8 in at speed 1) turns about 200 degrees
    assert(degrees(2.8f * 420, 420, 1) > 190 && degrees(2.8f * 420, 420, 1) < 215);
    // the fallback stick (first person, other camera modes) sees the swipe as nominal mouse points
    assert(near(mods::camera_mouse::fallback_stick(degrees(2, 400, 1) / kNominalSensitivity), 0.08 * 0.36 / 0.15, 1e-4));

    // ---- touch_style.h
    using namespace touch_style;
    Options o;
    // the defaults: Flexible with the swipe camera, visible and following sticks, 100 % / 1x
    assert(o.style == Flexible && o.show_sticks && o.follow && o.right == RightSwipe);
    assert(o.swipe_speed == 1.0f && o.swipe_boat == 1.0f && sanitize(o) == o);
    // Classic (a player's explicit choice): every free finger goes to the game
    Options classic;
    classic.style = Classic;
    assert(sanitize(classic).style == Classic);
    for (float x : {0.f, 0.3f, 0.7f, 1.f}) assert(free_touch(classic, x, false) == FreeGame);
    assert(free_touch(o, 0.1f, false) == FreeLeftStick);
    assert(free_touch(o, 0.49f, false) == FreeLeftStick);
    assert(free_touch(o, 0.5f, false) == FreeSwipe);
    assert(free_touch(o, 0.9f, false) == FreeSwipe);
    assert(free_touch(o, 0.1f, true) == FreeGame && free_touch(o, 0.9f, true) == FreeGame);  // view button, GamePad picture
    assert(free_touch(o, nan, false) == FreeGame);
    o.right = RightStick;
    assert(free_touch(o, 0.9f, false) == FreeRightStick && free_touch(o, 0.2f, false) == FreeLeftStick);
    // the game's own touch areas, as the renderer routes a primary-screen finger (game_touch); the CI
    // display smoke's case: two screens with roles swapped, a tap on the GamePad picture the phone shows
    // must reach the game, not become a floating stick or a swipe in the default Flexible style
    {
        GameTouch g;
        assert(!game_touch(g));  // one screen, off the view button and the GamePad picture
        g.view_button = true;
        assert(game_touch(g));
        g = {};
        g.pip = true;
        assert(game_touch(g));
        GameTouch swapped;
        swapped.dual = swapped.swapped = true;
        assert(!game_touch(swapped));
        swapped.primary_gamepad = true;
        assert(game_touch(swapped));
        assert(free_touch(Options{}, 0.5f, game_touch(swapped)) == FreeGame);  // the CI tap (screen centre)
        GameTouch tv;  // two screens, the phone shows the TV: the one-screen areas do not count there
        tv.dual = true, tv.view_button = true, tv.pip = true, tv.primary_gamepad = true;
        assert(!game_touch(tv));
        assert(free_touch(Options{}, 0.5f, game_touch(tv)) == FreeSwipe);
    }
    // sanitize: unknown enum values and out-of-range speeds
    Options bad;
    bad.style = 7, bad.right = -3, bad.swipe_speed = 40, bad.swipe_boat = nan;
    Options s = sanitize(bad);
    assert(s.style == Flexible && s.right == RightSwipe && s.swipe_speed == kSpeedMax && s.swipe_boat == kBoatDefault);
    // pack: the layout TouchControls.java reads
    Options p;
    p.style = Flexible, p.show_sticks = false, p.follow = true, p.right = RightStick, p.swipe_speed = 1.25f, p.swipe_boat = 2.5f;
    int32_t v[kPackLen + 2] = {0};
    v[kPackLen] = v[kPackLen + 1] = -9;
    pack(p, v, kPackLen + 2);
    const int32_t want[kPackLen] = {kPackVersion, 1, 0, 1, 1, 125, 250};
    for (int i = 0; i < kPackLen; i++) assert(v[i] == want[i]);
    assert(v[kPackLen] == -9);  // never past its length
    int32_t short_out[3] = {0, 0, 0};
    pack(p, short_out, 3);
    assert(short_out[0] == kPackVersion && short_out[2] == 0);
    std::puts("camera_swipe: scaling, DPI fallback, boat factor, manualCamera speed, touch style rules PASS");
}
