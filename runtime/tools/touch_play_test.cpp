// SPDX-License-Identifier: MPL-2.0
// touch_play.h: items by tap (latch, shoot, put away, letting go), aim-only buttons, auto put-away,
// the ZL latch (let go, next target, plain tap) and the golden rings.
#include "mods/touch_play.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

using namespace mods;
using namespace mods::playctx;
using namespace mods::touchplay;

namespace {
PlayContext play() {
    PlayContext c;
    c.flags = kInGame | kLink | kInPlay;
    c.item_in_hand = kHandNone;
    c.y_item = 0x27;  // the bow on Y
    c.x_item = 0x31;  // bombs on X
    c.r_item = kItemNone;
    c.proc = 0;
    return c;
}
struct Run {
    Machine m;
    Options o;
    uint64_t step = 100;
    bool touch = true;
    float lx = 0, ly = 0;
    uint32_t read(uint32_t buttons, const PlayContext& c) {
        In in;
        in.buttons = buttons, in.lx = lx, in.ly = ly, in.touch = touch, in.step = step++;
        return m.filter(in, c, o);
    }
};

void item_tap() {
    Run r;
    PlayContext c = play();
    assert(r.read(0, c) == 0);
    // a tap on Y: Y stays held after the finger lifts
    assert(r.read(kBtnY, c) & kBtnY);
    assert(r.m.latched() == 1);
    c.player_status0 = kSt0BowAim;  // the game aims
    for (int i = 0; i < 10; i++) assert(r.read(0, c) == kBtnY);
    // a tap shoots: Y lets go for kShootSteps, then is held again (the finger still down or not)
    assert(r.read(kBtnY, c) == 0);
    assert(r.read(kBtnY, c) == 0);
    assert(r.read(0, c) == 0);
    assert(r.read(0, c) == kBtnY);
    assert(r.m.aim_latched());
    // B puts it away: B goes through, Y stays held while the game still aims (letting go would shoot), then lets go
    assert(r.read(kBtnB, c) == (kBtnB | kBtnY));
    assert(!r.m.aim_latched());
    assert(r.read(0, c) == kBtnY);
    c.player_status0 = 0;
    assert(r.read(0, c) == 0);

    // A "Back" (the grappling hook's put-away while aimed) also lets go
    r.read(kBtnY, c);
    c.player_status0 = kSt0RopeAim;
    c.a_action = kActReturn;
    assert(r.read(0, c) == kBtnY && r.m.aim_latched());
    assert(r.read(kBtnA, c) == (kBtnA | kBtnY) && !r.m.aim_latched());
    for (uint64_t i = 1; i < kPutAwayHoldSteps; i++) assert(r.read(0, c) == kBtnY);  // the game keeps aiming: held at most this long
    assert(r.read(0, c) == 0);
    c.player_status0 = 0;
    c.a_action = kActNone;
    assert(r.read(0, c) == 0);
    // A with another word (not a put-away) keeps the latch
    r.read(kBtnY, c);
    c.a_action = kActSpeak;
    assert(r.read(kBtnA, c) == (kBtnA | kBtnY) && r.m.aim_latched());
    assert(r.read(kBtnB, c) == kBtnB && !r.m.aim_latched());  // (not aimed: nothing to hold)
    c.a_action = kActNone;
    r.read(0, c);

    // bombs (no aim): untouched
    assert(r.read(kBtnX, c) == kBtnX && r.read(0, c) == 0 && !r.m.aim_latched());

    // never aimed (Link couldn't): lets go after kAimStartSteps
    r.read(kBtnY, c);
    for (uint64_t i = 0; i < kAimStartSteps; i++) assert(r.read(0, c) == kBtnY);
    r.read(0, c);
    assert(!r.m.aim_latched() && r.read(0, c) == 0);

    // the aim ended (the game put the bow away): lets go after kAimLostSteps
    r.read(kBtnY, c);
    c.player_status0 = kSt0BowAim;
    r.read(0, c);
    c.player_status0 = 0;
    for (uint64_t i = 0; i < kAimLostSteps; i++) assert(r.read(0, c) == kBtnY);
    r.read(0, c);
    assert(!r.m.aim_latched());

    // an aim procedure counts as aiming too (camera_mouse::aiming_item)
    r.read(kBtnY, c);
    c.proc = 0x80;
    for (int i = 0; i < 60; i++) assert(r.read(0, c) == kBtnY);
    // a menu opens: let go; the finger still on Y keeps Y up until it lifts
    c.flags &= ~kInPlay;
    c.flags |= kMenu;
    assert(r.read(kBtnY, c) == 0 && !r.m.aim_latched());
    c.flags = kInGame | kLink | kInPlay;
    assert(r.read(kBtnY, c) == 0);  // still the same finger: suppressed
    assert(r.read(0, c) == 0);
    c.proc = 0;

    // the item on the button changes (the ring put another on): let go
    r.read(kBtnY, c);
    assert(r.m.aim_latched());
    c.y_item = 0x31;
    assert(r.read(0, c) == 0 && !r.m.aim_latched());
    c.y_item = 0x27;

    // a controller: nothing latches, nothing changes
    r.touch = false;
    assert(r.read(kBtnY, c) == kBtnY && r.read(0, c) == 0 && !r.m.aim_latched());
    r.touch = true;

    // the option off: a plain button
    r.o.item_tap = false;
    assert(r.read(kBtnY, c) == kBtnY && r.read(0, c) == 0);
    r.o.item_tap = true;

    // not usable (swimming): no latch
    c.player_status0 = kSt0Swim;
    assert(r.read(kBtnY, c) == kBtnY && !r.m.aim_latched());
    r.read(0, c);
    c.player_status0 = 0;
    puts("touch_play: items by tap (latch, shoot, put away, never aimed, aim lost, menu, item change, controller, option, unusable) PASS");
}

void aim_only_buttons() {
    PlayContext c = play();
    c.a_action = kActSpeak;
    c.b_action = kActSword;
    c.zr_action = 0x22;
    c.flags |= kHasWindWaker;
    ctxbtn::Buttons b = ctxbtn::context_buttons(c);
    assert(b[ctxbtn::kA].shown && b[ctxbtn::kX].shown && b[ctxbtn::kPlus].shown);
    aim_only(b, c, 1);
    assert(!b[ctxbtn::kA].shown && !b[ctxbtn::kX].shown && !b[ctxbtn::kR].shown && !b[ctxbtn::kPlus].shown);
    assert(!b[ctxbtn::kDpadUp].shown && !b[ctxbtn::kStickR].shown);
    assert(b[ctxbtn::kY].shown && b[ctxbtn::kY].kind == ctxbtn::Kind::kItem && b[ctxbtn::kY].label == 0x27);
    assert(b[ctxbtn::kB].shown && b[ctxbtn::kB].label == kActPutAway);  // not "sword"
    assert(b[ctxbtn::kZL].shown && b[ctxbtn::kZR].shown);
    c.a_action = kActReturn;  // A puts the item away: it stays
    ctxbtn::Buttons back = ctxbtn::context_buttons(c);
    aim_only(back, c, 1);
    assert(back[ctxbtn::kA].shown && back[ctxbtn::kA].label == kActReturn);
    ctxbtn::Buttons all;  // the full pad (rules inactive): untouched
    aim_only(all, c, 1);
    assert(!all.active && !all[ctxbtn::kY].shown);
    puts("touch_play: aim-only buttons PASS");
}

void put_away() {
    Run r;
    PlayContext c = play();
    c.item_in_hand = kHandSword;
    c.a_action = kActPutAway;
    r.read(0, c);
    // swinging and walking keep it out
    for (uint64_t i = 0; i < kSwordIdleSteps; i++) assert(r.read(i % 40 == 0 ? kBtnB : 0, c) == (i % 40 == 0 ? kBtnB : 0u));
    r.lx = 0.8f;
    for (uint64_t i = 0; i < kSwordIdleSteps; i++) assert(r.read(0, c) == 0);
    r.lx = 0;
    // a lock-on too
    c.flags |= kLocked;
    for (uint64_t i = 0; i < kSwordIdleSteps; i++) assert(r.read(0, c) == 0);
    c.flags &= ~kLocked;
    // 5 s idle: A for kPutPressSteps
    uint64_t i = 0;
    while (!(r.read(0, c) & kBtnA)) assert(++i < kSwordIdleSteps + 2);
    assert(i + 1 >= kSwordIdleSteps);
    assert(r.read(0, c) & kBtnA);
    assert(!(r.read(0, c) & kBtnA));
    assert(r.m.put_aways() == 1);
    // only while the game offers "Put away" on A
    c.a_action = kActSpeak;
    for (uint64_t k = 0; k < 2 * kSwordIdleSteps; k++) assert(r.read(0, c) == 0);
    // with a controller: never
    Run k;
    k.touch = false;
    c.a_action = kActPutAway;
    for (uint64_t s = 0; s < 3 * kSwordIdleSteps; s++) assert(k.read(0, c) == 0);
    // the option off: never
    Run off;
    off.o.auto_put_away = false;
    for (uint64_t s = 0; s < 3 * kSwordIdleSteps; s++) assert(off.read(0, c) == 0);
    // the Deku Leaf: once Link stands still on the ground; not while gliding
    Run l;
    c.item_in_hand = kItemDekuLeaf;
    c.player_status1 = kSt1LeafFly;
    for (uint64_t s = 0; s < 3 * kLeafStillSteps; s++) assert(l.read(0, c) == 0);
    c.player_status1 = 0;
    c.mode_flags = kModeMidair;  // falling
    for (uint64_t s = 0; s < 3 * kLeafStillSteps; s++) assert(l.read(0, c) == 0);
    c.mode_flags = 0;
    i = 0;
    while (!(l.read(0, c) & kBtnA)) assert(++i <= kLeafStillSteps + 1);
    puts("touch_play: auto put-away (sword idle, activity, lock-on, only Put away, controller, option, Deku Leaf) PASS");
}

void zl_latch() {
    Run r;
    PlayContext c = play();
    // no target in reach: the plain ZL
    assert(r.read(kBtnZL, c) == kBtnZL && r.read(0, c) == 0 && !r.m.zl_latched());
    // a target in reach: the tap latches
    c.lock_reach = 2;
    assert(r.read(kBtnZL, c) == kBtnZL);
    c.lock_reach = 0;
    c.flags |= kLocked;
    for (int i = 0; i < 20; i++) assert(r.read(0, c) == kBtnZL);
    // the next tap lets go, also while the finger is still down
    assert(r.read(kBtnZL, c) == 0 && !r.m.zl_latched());
    assert(r.read(kBtnZL, c) == 0);
    assert(r.read(0, c) == 0);
    // a defeat: let go (default)
    assert(r.read(kBtnZL, c) == kBtnZL && r.m.zl_latched());
    r.read(0, c);
    c.flags &= ~kLocked;
    c.defeats++;
    c.lock_reach = 1;
    assert(r.read(0, c) == 0 && !r.m.zl_latched());
    // next target: ZL lets go for kRetargetSteps, then holds again
    r.o.zl_latch = kLatchNext;
    c.flags |= kLocked;
    c.lock_reach = 0;
    r.read(kBtnZL, c);
    assert(r.m.zl_latched());
    c.flags &= ~kLocked;
    c.defeats++;
    c.lock_reach = 1;
    for (uint64_t i = 0; i < kRetargetSteps; i++) assert(r.read(0, c) == 0);
    assert(r.read(0, c) == kBtnZL && r.m.zl_latched());
    // next target with none in reach: lets go
    c.defeats++;
    c.lock_reach = 0;
    assert(r.read(0, c) == 0 && !r.m.zl_latched());
    // an event: lets go
    c.lock_reach = 1;
    r.read(kBtnZL, c);
    assert(r.m.zl_latched());
    c.flags = kInGame | kLink | kEvent;
    assert(r.read(0, c) == 0 && !r.m.zl_latched());
    // off: the plain ZL
    c = play();
    c.lock_reach = 1;
    r.o.zl_latch = kLatchOff;
    assert(r.read(kBtnZL, c) == kBtnZL && r.read(0, c) == 0);
    puts("touch_play: ZL latch (plain tap, latch, tap off, let go, next target, none in reach, event, off) PASS");
}

void rings() {
    RingTracker t;
    PlayContext c = play();
    uint64_t s = 1;
    auto at = [&](uint8_t a) {
        c.a_action = a;
        t.step(ctxbtn::context_buttons(c), c, s++);
        return t.count();
    };
    assert(at(0) == 0);
    assert(at(kActSpeak) == 1);   // a new action
    assert(at(kActSpeak) == 1);   // the same, still
    assert(at(0) == 1);
    assert(at(kActSpeak) == 1);   // flickered off and on: no new ring
    assert(at(kActLift) == 2);    // another action
    assert(at(kActPutAway) == 2); // never for "Put away"
    assert(at(kActReturn) == 2);  // nor "Back" (an aimed item's)
    assert(at(kActAttack) == 2);  // nor "Roll" (0x0C: a lock-on's)
    for (int i = 0; i < 20; i++) at(0);
    assert(at(kActLift) == 3);    // back after a while: new again
    // talks, shops, minigames: none
    c.flags = kInGame | kLink | kEvent;
    c.mesg_status = 6;
    assert(at(kActNext) == 3);
    c = play();
    c.minigame = 2;
    at(0);
    assert(at(kActOpen) == 3);
    puts("touch_play: golden rings (new, flicker, another, put away, again later, talks, minigames) PASS");
}
}  // namespace

int main() {
    item_tap();
    aim_only_buttons();
    put_away();
    zl_latch();
    rings();
}
