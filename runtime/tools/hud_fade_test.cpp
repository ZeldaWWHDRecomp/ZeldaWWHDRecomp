// SPDX-License-Identifier: MPL-2.0
// hud_fade.h: when the hearts, the rupees and the magic bar show, and the fade itself.
#include "mods/hud_fade.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

using namespace mods::hudfade;

int main() {
    Rules r;
    bool v[kCount];
    In in;
    in.play = in.save = true;
    in.life = in.max_life = 40;  // 10 hearts, full
    in.rupees = 100;
    in.magic = 16;
    uint64_t s = 1000;
    r.step(in, s++, v);  // the first reading: no change
    assert(!v[kHearts] && !v[kRupees] && !v[kMagic]);
    // the sword out, a lock-on: hearts
    in.sword = true;
    r.step(in, s++, v);
    assert(v[kHearts] && !v[kRupees]);
    in.sword = false, in.locked = true;
    r.step(in, s++, v);
    assert(v[kHearts]);
    in.locked = false;
    r.step(in, s++, v);
    assert(!v[kHearts]);
    // a hit: hearts for kHeartsAfterChange
    in.life = 36;
    for (uint64_t i = 0; i < kHeartsAfterChange; i++) r.step(in, s++, v), assert(v[kHearts]);
    r.step(in, s++, v);
    assert(!v[kHearts]);
    // low life: always
    in.life = 10;
    r.step(in, s, v);
    s += 1000;
    r.step(in, s++, v);
    assert(v[kHearts]);
    in.max_life = 80, in.life = 20;  // a quarter of 20 hearts
    r.step(in, s, v);
    s += 1000;
    r.step(in, s++, v);
    assert(v[kHearts]);
    in.life = 21;
    r.step(in, s, v);
    s += 1000;
    r.step(in, s++, v);
    assert(!v[kHearts]);
    // rupees: on change, in shops
    in.rupees = 120;
    for (uint64_t i = 0; i < kRupeesAfterChange; i++) r.step(in, s++, v), assert(v[kRupees] && !v[kMagic]);
    r.step(in, s++, v);
    assert(!v[kRupees]);
    in.shop = true;
    r.step(in, s++, v);
    assert(v[kRupees]);
    in.shop = false;
    // magic: on change
    in.magic = 15;
    for (uint64_t i = 0; i < kMagicAfterChange; i++) r.step(in, s++, v), assert(v[kMagic]);
    r.step(in, s++, v);
    assert(!v[kMagic]);
    // not in play (an event, a menu): all as the game shows them
    in.play = false;
    r.step(in, s++, v);
    assert(v[kHearts] && v[kRupees] && v[kMagic]);
    in.play = true;
    // a new file (no save for a while): its first reading is no change
    in.save = false;
    r.step(in, s++, v);
    assert(v[kRupees]);
    in.save = true, in.rupees = 5, in.magic = 0, in.life = in.max_life = 12 * 4;
    r.step(in, s++, v);
    assert(!v[kRupees] && !v[kMagic] && !v[kHearts]);
    // the fade: in quickly, out slower, clamped
    float a = 1;
    a = fade(a, false, 0.5f);
    assert(a > 0.2f && a < 0.3f);
    a = fade(a, false, 1);
    assert(a == 0);
    a = fade(a, true, 0.1f);
    assert(a > 0.39f && a < 0.41f);
    a = fade(a, true, 1);
    assert(a == 1);
    assert(fade(0.5f, true, 0) == 0.5f);
    puts("hud_fade: hearts (sword, lock-on, change, low life), rupees (change, shop), magic, not in play, new file, fade PASS");
}
