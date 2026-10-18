#include "mods/camera_mouse.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <initializer_list>

int main() {
    using namespace mods::camera_mouse;
    for (unsigned proc : {0x86, 0x87, 0x88, 0x89, 0x8E, 0x8F, 0x90, 0x91}) assert(!aiming_item(proc));
    for (unsigned proc : {0x76, 0x7D, 0x80, 0x81, 0x83, 0x84, 0x8A, 0x8B, 0x8C, 0x8D, 0x94, 0x95}) assert(aiming_item(proc));
    assert(!aiming_item(0) && !aiming_item(0xFFFFFFFF));
    // Convert back with each style's live yaw multiplier: equal points mean equal degrees.
    for (double per : {4.0, 6.0, 8.0, 12.0}) {
        for (float points : {-20.f, -5.f, 0.f, 5.f, 20.f}) {
            assert(std::fabs(yaw_speed(points, 0.15f, per) * 0.92f * per + points * 0.15f) < 0.00001);
        }
    }
    assert(yaw_speed(5, 0.15f, 0) == yaw_speed(5, 0.15f, 8));
    assert(yaw_speed(5, 0.15f, std::numeric_limits<double>::quiet_NaN()) == yaw_speed(5, 0.15f, 8));
    assert(fallback_stick(0) == 0);
    assert(std::fabs(fallback_stick(5) - 0.4f) < 0.00001f);
    assert(std::fabs(fallback_stick(1) - 0.08f) < 0.00001f);
    assert(fallback_stick(-5) == -fallback_stick(5));
    assert(fallback_stick(100) == 1 && fallback_stick(-100) == -1);
    // No retained rate after a stop, and an immediate proportional reversal.
    const float movement[] = {5, 10, 0, -5, 0};
    const float rates[] = {0.4f, 0.8f, 0, -0.4f, 0};
    for (int i = 0; i < 5; ++i) assert(std::fabs(fallback_stick(movement[i]) - rates[i]) < 0.00001f);
}
