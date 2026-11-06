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

    std::printf(g_fail ? "coop_emotes: %d failed\n" : "coop_emotes: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
