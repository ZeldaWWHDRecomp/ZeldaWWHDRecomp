// SPDX-License-Identifier: MPL-2.0
// Calm water (mods/story_world_calm.h): the fade around a zone, several zones together, the turned centre and
// the game's own value passing through untouched.
#include "mods/story_world_calm.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace calm = story_world_calm;
static int failures = 0;
#define CHECK(x) \
    do { \
        if (!(x)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            failures++; \
        } \
    } while (0)
static bool near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e; }
static bool same_bits(float a, float b) { return std::memcmp(&a, &b, 4) == 0; }

int main() {
    // one zone: still inside the radius, a straight line over the falloff (as the game's ramp), full waves beyond
    const calm::Zone z{1000, -2000, 3000, 12800};
    CHECK(calm::factor(z, 1000, -2000) == 0);
    CHECK(calm::factor(z, 1000 + 3000, -2000) == 0);
    CHECK(near(calm::factor(z, 1000 + 3000 + 6400, -2000), 0.5f));
    CHECK(near(calm::factor(z, 1000, -2000 - 3000 - 3200), 0.25f));
    CHECK(calm::factor(z, 1000 + 3000 + 12800, -2000) == 1);
    CHECK(calm::factor(z, 1000 + 50000, -2000) == 1);
    // continuous and never decreasing outwards: no step anywhere along a line through the edge
    float last = 0;
    for (int i = 0; i <= 40000; i++) {
        const float f = calm::factor(z, 1000 + float(i), -2000);
        CHECK(f >= last && f - last <= 1.0f / 12800 + 1e-6f);
        last = f;
    }
    // a bad position never calms the sea
    CHECK(calm::factor(z, std::numeric_limits<float>::quiet_NaN(), 0) == 1);
    CHECK(calm::factor(z, std::numeric_limits<float>::infinity(), 0) == 1);

    // several zones: the calmest wins, the order doesn't matter
    const calm::Zone zs[3] = {{0, 0, 1000, 1000}, {5000, 0, 1000, 4000}, {-90000, 0, 500, 800}};
    const calm::Zone rev[3] = {zs[2], zs[1], zs[0]};
    for (float x = -3000; x <= 12000; x += 250) {
        const float a = calm::combined(zs, 3, x, 0), b = calm::combined(rev, 3, x, 0);
        CHECK(same_bits(a, b));
        CHECK(a <= calm::factor(zs[0], x, 0) && a <= calm::factor(zs[1], x, 0));
    }
    CHECK(calm::combined(zs, 3, 0, 0) == 0);
    CHECK(near(calm::combined(zs, 3, 2500, 0), 0.375f));  // zone 2: 2500 - 1000 - 0 = 1500 out of 4000
    CHECK(calm::combined(zs, 0, 0, 0) == 1);

    // the game's value: only lowered, never raised, and returned bit for bit without zones or when farther out
    for (float game : {0.0f, 0.3f, 1.0f, 0.99999994f}) {
        CHECK(same_bits(calm::apply(game, zs, 0, 0, 0), game));
        CHECK(same_bits(calm::apply(game, zs, 3, 50000, 50000), game));
        CHECK(calm::apply(game, zs, 3, 0, 0) == 0);
        CHECK(calm::apply(game, zs, 3, 2500, 0) == std::fmin(game, calm::combined(zs, 3, 2500, 0)));
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(same_bits(calm::apply(0.5f, zs, 3, nan, 0), 0.5f));

    // the centre: the object's position plus the offset, turned with the object as its collision is
    calm::Settings s;
    s.enabled = true, s.radius = 2000, s.falloff = 3000, s.offset_x = 1000, s.offset_z = 0;
    calm::Zone c = calm::zone_of(s, 100, 200, 0);
    CHECK(near(c.x, 1100) && near(c.z, 200) && c.radius == 2000 && c.falloff == 3000);
    c = calm::zone_of(s, 100, 200, 90);  // +x turned a quarter: x*cos + z*sin, -x*sin + z*cos
    CHECK(near(c.x, 100, 1e-3f) && near(c.z, 200 - 1000, 1e-3f));
    c = calm::zone_of(s, 100, 200, 180);
    CHECK(near(c.x, 100 - 1000, 1e-3f) && near(c.z, 200, 1e-3f));
    s.offset_x = 0, s.offset_z = 0;
    c = calm::zone_of(s, -5, 7, 33);
    CHECK(c.x == -5 && c.z == 7);

    // dormant until a package declares a zone
    CHECK(!calm::declared());
    calm::note_declared();
    CHECK(calm::declared());

    if (failures) {
        std::fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    std::printf("story_world_calm: ok\n");
    return 0;
}
