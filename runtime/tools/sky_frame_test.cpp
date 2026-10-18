// sky_frame.h: the per-frame values of the sky passes (horizon haze grade, toon clouds) and the Horizon
// haze setting's environment parsing.
#include "sky_frame.h"

#include <cassert>
#include <cmath>
#include <cstdio>

static bool close_to(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

int main() {
    // Horizon haze from the environment: default original, clamped, research name = amount removed
    assert(close_to(sky::haze_from_env(nullptr, nullptr), 1.0f));
    assert(close_to(sky::haze_from_env("0.3", nullptr), 0.3f));
    assert(close_to(sky::haze_from_env("-2", nullptr), 0.0f));
    assert(close_to(sky::haze_from_env("7", nullptr), 1.0f));
    assert(close_to(sky::haze_from_env("nan", nullptr), 1.0f));
    assert(close_to(sky::haze_from_env(nullptr, "0.7"), 0.3f));
    assert(close_to(sky::haze_from_env("0.5", "0.9"), 0.5f));  // the setting's own name wins

    // warmth: dusk and dawn by the clock, none at noon or midnight with a high sun
    assert(close_to(sky::warmth(268.0f, 0.6f, 1.0f), 1.0f));
    assert(close_to(sky::warmth(92.0f, 0.6f, 1.0f), 1.0f));
    assert(close_to(sky::warmth(180.0f, 0.8f, 1.0f), 0.0f));
    assert(sky::warmth(180.0f, 0.06f, 1.0f) > 0.9f);  // a low sun is warm whatever the clock says
    assert(close_to(sky::warmth(0.0f, 0.06f, 0.0f), 0.0f));  // ... but not at night

    // frame: view transposed into columns, projection, light direction, daylight
    sky::Inputs in{};
    const float rot[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};  // identity camera
    for (int i = 0; i < 12; i++) in.viewRot[i] = rot[i];
    in.viewRot[1] = 2.0f;  // row 0, column 1
    in.fovy = 90.0f, in.curTime = 180.0f, in.sunVisible = true;
    in.sunPos[1] = 100.0f;  // straight up from the eye at the origin
    in.fog[0] = 0.1f, in.fog[1] = 0.4f, in.fog[2] = 0.7f;
    sky::SkyFrame g;
    sky::frame(in, 2.0f, 0.7f, g);
    assert(close_to(g.view[1 * 4 + 0], 2.0f));  // column 1, row 0
    assert(close_to(g.proj[1], 1.0f) && close_to(g.proj[0], 0.5f) && close_to(g.proj[2], 2.0f));
    assert(close_to(g.sun[0], 0.0f) && close_to(g.sun[1], 1.0f) && close_to(g.sun[3], 1.0f));
    assert(close_to(g.lit[3], 0.0f));
    assert(close_to(g.sky[2], 0.7f) && close_to(g.params[2], 0.7f));
    sky::frame(in, 2.0f, 3.0f, g);
    assert(close_to(g.params[2], 1.0f));  // clamped
    in.sunVisible = false;  // no sun packet: light from above
    sky::frame(in, 2.0f, 0.0f, g);
    assert(close_to(g.sun[1], 1.0f));
    in.sunVisible = true, in.sunPos[1] = -100.0f;  // below the horizon: night
    sky::frame(in, 2.0f, 0.0f, g);
    assert(close_to(g.sun[3], 0.0f));
    std::puts("sky_frame: ok");
    return 0;
}
