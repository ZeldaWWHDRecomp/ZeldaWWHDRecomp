// SPDX-License-Identifier: MPL-2.0
// Online co-op's emotes (GAPS C22), the pure part of runtime/src/mods/coop_emotes.h: the catalogue, a pose's joints
// as matrices and back, the blend, the emote's weight and frames, and the clip put on a pose (the body takes it, the
// other models follow the body joint they hang on).
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

#include "mods/coop_emotes.h"

using namespace mods::coop;
using namespace mods::coop::emotes;

static int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                               \
        }                                                           \
    } while (0)

static bool near(float a, float b, float eps = 0.02f) { return std::fabs(a - b) <= eps; }
static Mx turn_y(float deg, float x, float y, float z, float s = 1) {
    const float a = deg * 3.14159265f / 180, c = std::cos(a) * s, n = std::sin(a) * s;
    Mx m;
    const float v[12] = {c, 0, n, x, 0, s, 0, y, -n, 0, c, z};
    for (int i = 0; i < 12; i++) m.m[i] = v[i];
    return m;
}
static bool same(const Mx& a, const Mx& b, float eps = 0.02f) {
    for (int i = 0; i < 12; i++)
        if (!near(a.m[i], b.m[i], i % 4 == 3 ? eps * 10 : eps)) return false;
    return true;
}
static Joint joint(const Mx& m) {
    Joint j{};
    CHECK(to_joint(m, j));
    return j;
}

int main() {
    // a pose's character (coop_pose.h char_of/with_char): every one round-trips over every flag set, and never sets or
    // clears a flag (Tetra and Zelda once read as kOnFriend); an older game (bits 4-5) sees Link
    for (int ch = 0; ch < kCharacters; ch++) {
        CHECK(char_of(with_char(0, ch)) == ch);
        CHECK(!(with_char(0, ch) & kAllFlags));
        CHECK(!(with_char(0, ch) & kOnFriend));
        CHECK((with_char(0, ch) >> 4 & 3) == 0);
        for (int f = 0; f < 256; f++) {
            const uint8_t flags = (uint8_t)(f & kAllFlags), w = with_char(flags, ch);
            CHECK(char_of(w) == ch);
            CHECK((w & kAllFlags) == flags);
            CHECK(char_of(with_char(w, 0)) == 0 && (with_char(w, 0) & kAllFlags) == flags);
        }
    }
    CHECK((kOnDeck | kFlying | kHurt | kSailing | kOnFriend) == kAllFlags);
    CHECK(__builtin_popcount(kAllFlags) == 5);
    // the catalogue: ids unique and not 0, each with a clip and a name, played at least once
    std::set<int> ids;
    for (const Emote& e : kEmotes) {
        CHECK(e.id != 0 && ids.insert(e.id).second);
        CHECK(e.clip && *e.clip && e.name && *e.name && e.plays >= 1 && e.hold >= 0);
        CHECK(find(e.id) == &e);
    }
    CHECK(find(0) == nullptr && find(200) == nullptr);

    // joints as matrices and back
    const Mx a = turn_y(30, 12.5f, 80, -40, 1.0f);
    CHECK(same(from_joint(joint(a)), a));
    const Mx b = turn_y(-100, -3, 5, 7, 0.75f);
    CHECK(same(from_joint(joint(b)), b));
    Joint far{};
    CHECK(!to_joint(turn_y(0, 5000, 0, 0), far));  // (a pose holds up to 4096 units)
    // the inverse of a rotation with a scale
    CHECK(same(mul(b, inverse(b)), Mx{}));
    CHECK(same(mul(inverse(a), a), Mx{}));
    // the blend: its ends, and the middle of a turn the short way
    CHECK(same(blend(a, b, 0), a) && same(blend(a, b, 1), b));
    CHECK(same(blend(turn_y(170, 0, 0, 0), turn_y(-170, 0, 0, 0), 0.5f), turn_y(180, 0, 0, 0)));

    // the weight: eased in and out, nothing outside
    CHECK(weight(0, 2) == 0 && weight(-1, 2) == 0 && weight(2, 2) == 0 && weight(3, 2) == 0);
    CHECK(near(weight(1, 2), 1) && weight(0.05f, 2) < weight(0.1f, 2) && weight(1.9f, 2) < weight(1.8f, 2));
    // the frames: played twice, then held at the last
    const Emote twice{9, "x", "x", 2, 0.5f};
    CHECK(near(length(twice, 31), 2.5f));
    CHECK(near(frame_at(twice, 31, 0.5f), 15) && near(frame_at(twice, 31, 1.5f), 15) && near(frame_at(twice, 31, 2.2f), 30));

    // the clip on a pose: body k 2 (3 joints), a sword (k 5) on the body's last joint, a held model on its first
    Pose p;
    Model body, sword, held;
    body.k = 2, sword.k = 5, held.k = kHeld;
    for (int j = 0; j < 3; j++) body.j.push_back(joint(turn_y(0, 0, 40.f * j, 0)));
    sword.j = {joint(turn_y(0, 2, 82, 0)), joint(turn_y(0, 2, 100, 0))};
    held.j = {joint(turn_y(0, 1, 1, 0))};
    p.models = {sword, body, held};
    std::vector<Mx> clip;
    for (int j = 0; j < 3; j++) clip.push_back(turn_y(90, 10, 40.f * j, 0));  // (moved 10 along x and turned 90)
    Pose q = p;
    CHECK(!apply(q, 2, clip, 0));             // (weight 0: unchanged)
    CHECK(!apply(q, 3, clip, 1));             // (no such body)
    CHECK(!apply(q, 2, {clip[0], clip[1]}, 1));  // (not the clip's skeleton)
    CHECK(apply(q, 2, clip, 1));
    const Model& qb = q.models[1];
    for (int j = 0; j < 3; j++) CHECK(same(from_joint(qb.j[size_t(j)]), clip[size_t(j)]));
    // the sword turned with the body's last joint about it: (2, 82, 0) - (0, 80, 0) turned 90 about y is (0, 2, -2)
    const Mx s0 = from_joint(q.models[0].j[0]);
    CHECK(near(s0.m[3], 10, 0.2f) && near(s0.m[7], 82, 0.2f) && near(s0.m[11], -2, 0.2f));
    const Mx s1 = from_joint(q.models[0].j[1]);
    CHECK(near(s1.m[7], 100, 0.2f));
    // the held model on the first joint: (1, 1, 0) turned about (0, 0, 0) is (0, 1, -1), then 10 along x
    const Mx h0 = from_joint(q.models[2].j[0]);
    CHECK(near(h0.m[3], 10, 0.2f) && near(h0.m[7], 1, 0.2f) && near(h0.m[11], -1, 0.2f));
    // half way: half the move
    Pose r = p;
    CHECK(apply(r, 2, clip, 0.5f));
    CHECK(near(from_joint(r.models[1].j[0]).m[3], 5, 0.2f));

    // ended early: faded out over kCancelOut from where it was, then over
    CHECK(near(weight(1, 2, -1), weight(1, 2)));
    CHECK(near(weight(1, 2, 1), 1) && near(weight(1 + kCancelOut / 2, 2, 1), 0.5f) && weight(1 + kCancelOut, 2, 1) == 0);
    CHECK(near(weight(0.1f, 2, 0.1f), weight(0.1f, 2)) && weight(0.1f + kCancelOut / 2, 2, 0.1f) < weight(0.1f, 2));
    CHECK(!over(1, 2, -1) && over(2, 2, -1) && !over(1.05f, 2, 1) && over(1 + kCancelOut, 2, 1));

    // when one may start: Link standing with nothing in his hands and nothing over him
    const Link stand;  // (the defaults: the clips read, the WAIT procedure, no mode flag, no item)
    CHECK(stand.proc == kProcWait && stand.equip == kNoItem);
    CHECK(refused(stand) == nullptr && ends(stand) == nullptr);
    CHECK(refused(Link{.proc = kProcFreeWait}) == nullptr);  // (standing a while: his idle fidgets)
    auto with = [&](auto f) { Link s = stand; f(s); return s; };
    struct Case { Link s; const char* why; };
    const Case refusals[] = {
        {with([](Link& s) { s.clips = false; }), "the emotes aren't loaded yet"},
        {with([](Link& s) { s.character = 1; }), "only as Link"},
        {with([](Link& s) { s.menu = true; }), "a menu is open"},
        {with([](Link& s) { s.overlay = true; }), "a menu is open"},
        {with([](Link& s) { s.event = true; }), "a cutscene or a talk is on"},
        {with([](Link& s) { s.message = true; }), "a text box is open"},
        {with([](Link& s) { s.next_stage = true; }), "on the way to another place"},
        {with([](Link& s) { s.proc = 0x88; }), "on the boat"},  // (a SHIP_* procedure)
        {with([](Link& s) { s.mode = kInShip; }), "on the boat"},
        {with([](Link& s) { s.mode = kSwim, s.proc = 0x37; }), "swimming"},
        {with([](Link& s) { s.mode = kSwim; }), "swimming"},  // (a swimming Link in a procedure that doesn't say so)
        {with([](Link& s) { s.mode = kLadder; }), "climbing"},
        {with([](Link& s) { s.mode = kClimb; }), "climbing"},
        {with([](Link& s) { s.mode = kHang, s.proc = 0x2E; }), "climbing"},
        {with([](Link& s) { s.mode = kRope; }), "climbing"},
        {with([](Link& s) { s.mode = kMidair; }), "in the air"},
        {with([](Link& s) { s.damage = 20; }), "hurt"},
        {with([](Link& s) { s.mode = kDamage; }), "hurt"},
        {with([](Link& s) { s.equip = 0x103; }), "something is in his hands"},  // (his sword drawn)
        {with([](Link& s) { s.mode = kGrab, s.proc = 0x73; }), "something is in his hands"},  // (a pot over his head)
        {with([](Link& s) { s.first_person = true; }), "aiming"},
        {with([](Link& s) { s.mode = kCrouch; }), "crouching"},
        {with([](Link& s) { s.proc = 0x06; }), "Link isn't standing still"},  // (MOVE: walking)
        {with([](Link& s) { s.proc = 0x42; }), "Link isn't standing still"},  // (CUT_F: a sword swing)
    };
    for (const Case& c : refusals) {
        const char* why = refused(c.s);
        CHECK(why && std::string(why) == c.why);
        if (!why || std::string(why) != c.why) std::printf("  (got %s, wanted %s)\n", why ? why : "nothing", c.why);
        CHECK(ends(c.s) != nullptr);  // (and what refuses one ends the one playing)
    }
    // what ends one playing beside them: the stick past its dead zone, any button
    CHECK(ends(with([](Link& s) { s.stick = 0.01f; })) == nullptr);
    CHECK(ends(with([](Link& s) { s.stick = 0.5f; })) != nullptr && refused(with([](Link& s) { s.stick = 0.5f; })) == nullptr);
    CHECK(ends(with([](Link& s) { s.trigger = 1; })) != nullptr);  // (A)
    CHECK(ends(with([](Link& s) { s.trigger = 0x40; })) != nullptr);  // (another button)

    // the emote in a pose header (a cheap puppet's): its id and steps, and back
    for (const Emote& e : kEmotes) {
        uint8_t id = 0, w = 0;
        float t = 0;
        from_header(header_anim(e.id, 1.5f), 200, id, t, w);
        CHECK(id == e.id && near(t, 1.5f) && w == 200);
    }
    {
        uint8_t id = 9, w = 9;
        float t = 9;
        from_header(header_anim(0, 1), 200, id, t, w);
        CHECK(id == 0 && w == 0);
        from_header(uint16_t(7 << 13 | 30), 200, id, t, w);  // (an id this game doesn't have)
        CHECK(id == 0 && w == 0);
        CHECK((header_anim(1, 1000) & 0x1FFF) == 0x1FFF);  // (held at its most)
    }

    std::printf(g_fail ? "coop_emotes: %d failed\n" : "coop_emotes: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
