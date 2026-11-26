// SPDX-License-Identifier: MPL-2.0
// First-person aiming: Right stick (HD) / Left stick (classic) (issue #666): when the sticks swap (aim_stick.h).
// The production filter with the subject-camera hook and Link's live status is in cannon_mouse_test.
#include "mods/aim_stick.h"

#include <cassert>
#include <cstdio>

using namespace mods::playctx;
namespace as = mods::aim_stick;

int main() {
    PlayContext c;
    c.flags = kLink | kInPlay;
    // the views: R3 look, every aim, the telescope, the Picto Box; not walking, swimming, sailing, the cannon's ride
    for (uint32_t s0 : {uint32_t(kSt0FirstPerson), uint32_t(kSt0BowAim), uint32_t(kSt0HookshotAim), uint32_t(kSt0BoomerangAim), uint32_t(kSt0RopeAim), uint32_t(kSt0Telescope)})
        assert(as::aim_view(s0, 0) && as::active(true, true, false, s0, 0, c));
    assert(as::aim_view(0, kSt1PictoAim) && as::active(true, true, false, 0, kSt1PictoAim, c));
    for (uint32_t s0 : {0u, uint32_t(kSt0Swim), uint32_t(kSt0ShipRide), uint32_t(kSt0Hang), uint32_t(kSt0Ladder), uint32_t(kSt0Crawl)}) assert(!as::aim_view(s0, 0));
    for (uint32_t s1 : {uint32_t(kSt1Conduct), uint32_t(kSt1LeafFly), uint32_t(kSt1LeafFan), uint32_t(kSt1Sail)}) assert(!as::aim_view(0, s1));
    // the option, the subject camera (a lock-on aim runs the lock-on camera), the cannon
    assert(!as::active(false, true, false, kSt0BowAim, 0, c));
    assert(!as::active(true, false, false, kSt0BowAim, 0, c));
    assert(!as::active(true, true, true, kSt0ShipRide | kSt0BowAim, 0, c));
    assert(as::active(true, true, false, kSt0ShipRide | kSt0BowAim, 0, c));  // the bow from the boat
    // not in an event, menu, fade, stage change, the album, a choice or a number entry; Link must be the player
    for (uint32_t f : {uint32_t(kEvent), uint32_t(kMenu), uint32_t(kFade), uint32_t(kStageChange), uint32_t(kAlbum), uint32_t(kChoice), uint32_t(kNumberEntry)}) {
        PlayContext d = c;
        d.flags |= f;
        assert(!as::active(true, true, false, kSt0FirstPerson, 0, d));
    }
    PlayContext seagull = c;
    seagull.flags = kInPlay;
    assert(!as::active(true, true, false, kSt0FirstPerson, 0, seagull));
    // a talk's text stops it, except the telescope's own (HD keeps a message status while it is up)
    PlayContext talk = c;
    talk.flags = kLink;
    talk.mesg_status = 11;
    assert(!as::active(true, true, false, kSt0BowAim, 0, talk));
    assert(!as::active(true, true, false, 0, kSt1PictoAim, talk));
    assert(as::active(true, true, false, kSt0Telescope, 0, talk));
    talk.flags |= kEvent;
    assert(!as::active(true, true, false, kSt0Telescope, 0, talk));  // ... not in a cutscene
    // the swap: both axes, exactly, and twice is the identity
    float lx = .25f, ly = -1.f, rx = 0.f, ry = .5f;
    as::swap(lx, ly, rx, ry);
    assert(lx == 0.f && ly == .5f && rx == .25f && ry == -1.f);
    as::swap(lx, ly, rx, ry);
    assert(lx == .25f && ly == -1.f && rx == 0.f && ry == .5f);
    std::puts("aim_stick_test: ok");
}
