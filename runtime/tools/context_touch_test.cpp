// SPDX-License-Identifier: MPL-2.0
// context_touch.h: the pack Java (TouchControls.nativeContextButtons) reads.
#include "mods/context_touch.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

using namespace mods;

int main() {
    playctx::PlayContext c;
    c.flags = playctx::kInGame | playctx::kLink | playctx::kInPlay;
    c.a_action = playctx::kActSpeak;
    c.b_action = playctx::kActSword;
    c.y_item = playctx::kItemBow;
    const ctxbtn::Buttons b = ctxbtn::context_buttons(c);
    ctxtouch::Extra e;
    e.enabled = true;
    e.zl_latched = true;
    e.held = 1u << ctxbtn::kZL;
    e.ring[ctxbtn::kA] = 3;
    e.assets_generation = 7;
    int32_t out[48];
    for (auto& v : out) v = -5;
    ctxtouch::pack(b, e, out, 48);
    assert(out[0] == ctxtouch::kPackVersion && ctxtouch::kPackLen == 40);
    assert(out[1] == (ctxtouch::kEnabled | ctxtouch::kActive | ctxtouch::kZlLatched));
    e.item_names_a = true;
    int32_t names[ctxtouch::kPackLen];
    ctxtouch::pack(b, e, names, ctxtouch::kPackLen);
    assert(names[1] == (ctxtouch::kEnabled | ctxtouch::kActive | ctxtouch::kZlLatched | ctxtouch::kItemNamesA));
    e.item_names_a = false;
    assert(out[2] == 7 && out[3] == 0);
    auto st = [&](int i) { return out[ctxtouch::kButtons + 3 * i]; };
    auto lb = [&](int i) { return out[ctxtouch::kButtons + 3 * i + 1]; };
    assert(st(ctxbtn::kA) == (ctxtouch::kShown | 1 << ctxtouch::kKindShift) && lb(ctxbtn::kA) == playctx::kActSpeak);
    assert(out[ctxtouch::kButtons + 3 * ctxbtn::kA + 2] == 3);
    assert(st(ctxbtn::kY) == (ctxtouch::kShown | 2 << ctxtouch::kKindShift) && lb(ctxbtn::kY) == playctx::kItemBow);
    assert(st(ctxbtn::kX) == 0);  // no item on X
    assert(st(ctxbtn::kZL) == (ctxtouch::kShown | ctxtouch::kHeld));
    for (int i = ctxtouch::kPackLen; i < 48; i++) assert(out[i] == 0);
    int32_t small[3];
    ctxtouch::pack(b, e, small, 3);  // a shorter array takes the front
    assert(small[0] == 1 && small[2] == 7);
    puts("context_touch: pack layout, flags, kinds, held, rings PASS");
}
