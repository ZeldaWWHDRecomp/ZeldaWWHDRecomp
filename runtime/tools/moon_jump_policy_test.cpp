#include "mods/moon_jump_policy.h"
#include <cassert>
#include <limits>

using mods::moon_jump_policy::steer;
using mods::moon_jump_policy::leaves_alone;
int main() {
    // Camera-relative forward/right and a released stick.
    auto forward = steer(0, 0, 1, 0, 1, 1);
    assert(forward.facing == 0 && std::abs(forward.x) < .001f && forward.z == 40.f);
    auto aligned = steer(-16384, 1, 0, 0, 1, 1);
    assert(aligned.facing == -16384 && std::abs(aligned.x + 40.f) < .001f);
    auto idle = steer(1234, 0, 0, 0, 1, 1);
    assert(idle.facing == 1234 && idle.x == 0 && idle.z == 0);
    // Two half turns cover the same angle as one full turn; velocity stays in
    // 30 Hz units so the movement hook, rather than this policy, scales travel.
    auto full = steer(0, 0, -1, 0, 1, 1);
    auto half = steer(0, 0, -1, 0, 1, .5f);
    auto second = steer(half.facing, 0, -1, 0, 1, .5f);
    assert(full.facing == second.facing);
    assert(std::abs(std::hypot(half.x, half.z) - 40.f) < .001f);
    // Turn through the signed-angle seam by the short route.
    auto seam = steer(32760, 0, -1, 0, 1, 1);
    assert(seam.facing == -32768);
    for (float bad : {std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity()}) {
        auto rejected = steer(42, bad, 1, 0, 1, 1);
        assert(rejected.facing == 42 && rejected.x == 0 && rejected.z == 0);
    }
    auto no_camera = steer(42, 1, 1, 0, 0, 1);
    assert(no_camera.x == 0 && no_camera.z == 0);
    // #616: no moon jump in damage or demo procedures or while an event runs
    for (uint32_t proc : {0x04u, 0x07u, 0x27u, 0x24u, 0x65u, 0x6Du, 0x93u, 0xA8u})
        assert(!leaves_alone(proc, 0));
    for (uint32_t proc : {0x66u, 0x6Bu, 0x6Cu, 0xA9u, 0xAAu, 0xB0u, 0xB1u, 0xB2u, 0xDAu})
        assert(leaves_alone(proc, 0));
    for (uint8_t mode : {1, 2, 0xFF}) assert(leaves_alone(0x27, mode) && leaves_alone(0x04, mode));
}
