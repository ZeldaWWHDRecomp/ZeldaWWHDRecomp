// SPDX-License-Identifier: MPL-2.0
#include "mods/startup_touch.h"
#include <cassert>
#include <cstdio>
using namespace mods::startup_touch;
int main() {
    assert(scene(false, "") == kBoot);
    assert(scene(false, "sea_T") == kTitle);
    assert(scene(false, "Name") == kFileSelect);
    assert(scene(true, "sea") == kGameplay);
    assert(pack(kUnknown) == 0 && pack(kFileSelect) == (kPackMagic | kFileSelect));
    TitlePress p;
    assert(p.filter(0, kTitle, true, 100) == kConfirm);
    assert(p.filter(0, kTitle, false, 101) == kConfirm);
    assert(p.filter(0, kTitle, false, 102) == 0);
    assert(p.filter(0x4000, kFileSelect, true, 103) == 0x4000);
    assert(p.filter(0, kTitle, false, 104) == 0);
    assert(p.filter(0, kTitle, true, 105) == kConfirm);
    assert(p.filter(0, kGameplay, false, 106) == 0);
    assert(p.filter(0, kTitle, false, 107) == 0);
    puts("startup_touch: stage state, title pulse, stale taps and menu/gameplay isolation PASS");
}
