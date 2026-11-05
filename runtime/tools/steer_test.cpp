// SPDX-License-Identifier: MPL-2.0
// steer.h: walking direction, camera swing, boat speed, pinch field of view (pure angle maths).
#include "mods/steer.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace mods::steer;

int main() {
    const int16_t up = int16_t(0x8000);  // the stick's raw angle for "up" (measured in the fork's notes; any value works)
    // ---- off: nothing kept
    {
        Direction d;
        Options o;
        assert(d.step(true, 0, up, true, false, 1, o) == 0);
    }
    // ---- player turns: the camera turned by the player is kept off, its own following is not
    {
        Direction d;
        Options o;
        o.mode = kPlayerTurns;
        uint64_t t = 100;
        assert(d.step(true, 1000, up, false, false, t++, o) == 0);  // the push begins
        for (int i = 1; i <= 10; i++) d.step(true, int16_t(1000 + 500 * i), up, true, false, t++, o);
        assert(d.offset() == 5000);  // 10 player turns of 500 units: Link walks on as before
        d.step(true, 6500 + 300, up, false, false, t++, o);  // the camera follows by itself: not kept
        assert(d.offset() == 5000);
        // the stick moves 30 degrees: steering again, relative to the camera
        d.step(true, 6800, int16_t(up + units(30)), false, false, t++, o);
        assert(d.offset() == 0);
        // letting go ends it
        d.step(true, 7000, up, true, false, t++, o);
        assert(d.offset() == 200);
        d.step(false, 7000, up, false, false, t++, o);
        assert(d.offset() == 0 && !d.holding());
    }
    // ---- stick held: every camera turn kept; the anchor follows the stick while the player turns
    {
        Direction d;
        Options o;
        o.mode = kStickHeld;
        uint64_t t = 0;
        d.step(true, 0, up, false, false, t++, o);
        d.step(true, 400, up, false, false, t++, o);
        assert(d.offset() == 400);
        // a small slide of the stick (5 degrees) keeps it
        d.step(true, 400, int16_t(up + units(5)), false, false, t++, o);
        assert(d.offset() == 400);
    }
    // ---- the kept angle eases away half a second after the camera stopped
    {
        Direction d;
        Options o;
        o.mode = kPlayerTurns;
        uint64_t t = 0;
        d.step(true, 0, up, false, false, t++, o);
        d.step(true, units(40), up, true, false, t++, o);
        assert(d.offset() == units(40));
        for (int i = 0; i < int(kEaseAfter); i++) d.step(true, units(40), up, false, false, t++, o);
        assert(d.offset() == units(40));
        int16_t last = d.offset();
        for (int i = 0; i < 40; i++) {
            d.step(true, units(40), up, false, false, t++, o);
            assert(std::abs(d.offset()) <= std::abs(last));
            last = d.offset();
        }
        assert(d.offset() == 0);  // gone within about a second
        // setStickData twice per logic step (a repeated call) eases once per step
        Direction e;
        e.step(true, 0, up, false, false, 500, o);
        e.step(true, units(40), up, true, false, 501, o);
        e.step(true, units(40), up, false, false, 501 + kEaseAfter + 1, o);
        const int16_t once = e.offset();
        e.step(true, units(40), up, false, false, 501 + kEaseAfter + 1, o);
        assert(e.offset() == once && once < units(40));
    }
    // ---- wrap-around: turns across +-180 degrees add up as short turns
    {
        Direction d;
        Options o;
        o.mode = kPlayerTurns;
        d.step(true, int16_t(32000), up, false, false, 0, o);
        d.step(true, int16_t(-32000), up, true, false, 1, o);  // +1536 units across the wrap
        assert(d.offset() == 1536);
    }
    // ---- the swing: waits for a run and the delay, turns towards Link's back, eases
    {
        Swing s;
        Options o;
        o.mode = kPlayerTurns, o.swing = true, o.swing_delay = 30;
        uint64_t t = 1000;
        const int16_t face = units(90), cam = 0;
        float deg = 0;
        for (int i = 0; i < 40; i++) deg = s.step(true, face, cam, 0.9f, false, t++, o);
        assert(deg > 0 && deg <= 4.0f);  // 90 degrees off: the full 4 degrees per step... clamped
        assert(std::fabs(deg - 4.0f) < 1e-4f);
        assert(std::fabs(s.step(true, face, int16_t(face - units(10)), 0.9f, false, t++, o) - 0.8f) < 1e-3f);  // 10 off: 8 %
        assert(s.step(true, face, int16_t(face - units(5)), 0.9f, false, t++, o) == 0);  // near: done
        assert(s.step(true, face, cam, 0.3f, false, t++, o) == 0);  // walking slowly: no swing
        // the player touched the camera: wait the delay again
        Swing u;
        for (int i = 0; i < 40; i++) u.step(true, face, cam, 0.9f, false, t++, o);
        assert(u.step(true, face, cam, 0.9f, true, t++, o) == 0);
        for (int i = 0; i < 25; i++) assert(u.step(true, face, cam, 0.9f, false, t++, o) == 0);
        for (int i = 0; i < 10; i++) deg = u.step(true, face, cam, 0.9f, false, t++, o);
        assert(deg > 0);
        // running towards the camera: waits, then keeps one way near 180
        Swing w;
        const int16_t back = units(170);
        for (int i = 0; i < 35; i++) assert(w.step(true, back, 0, 0.9f, false, t++, o) == 0);
        for (int i = 0; i < 10; i++) deg = w.step(true, back, 0, 0.9f, false, t++, o);
        assert(deg > 0);
        assert(w.step(true, int16_t(-units(170)), 0, 0.9f, false, t++, o) > 0);  // across 180: same way
        // the option off, or the walking direction off
        Swing x;
        Options off = o;
        off.swing = false;
        for (int i = 0; i < 40; i++) assert(x.step(true, face, cam, 0.9f, false, t++, off) == 0);
    }
    // ---- boat speed and pinch
    assert(std::fabs(boat_speed_factor(8.0f) - 1.0f) < 1e-6f && std::fabs(boat_speed_factor(4.0f) - 2.0f) < 1e-6f);
    assert(boat_speed_factor(0) == 1.0f && boat_speed_factor(NAN) == 1.0f && boat_speed_factor(0.1f) == 8.0f);
    assert(pinch_fov(1.0f, 100, 200) == kFovMin && pinch_fov(1.0f, 200, 100) == 2.0f && pinch_fov(1.0f, 100, 100) == 1.0f);
    assert(pinch_fov(1.2f, 100, 120) == 1.0f && pinch_fov(1.0f, 0, 50) == 1.0f && pinch_fov(2.0f, 50, 10) == kFovMax);
    printf("steer_test: ok\n");
    return 0;
}
