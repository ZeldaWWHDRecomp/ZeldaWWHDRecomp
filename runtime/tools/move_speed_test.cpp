#include "mods/move_speed.h"
#include <cassert>
#include <cmath>
#include <limits>
int main() {
    for (auto proc : {0x06u, 0x37u}) for (float factor : {1.25f, 1.5f, 2.f, 4.f}) {
        for (int rate : {30, 60, 120, 240}) {
            // One second: interpolation draws no extra logic; true60 uses two dt=.5 steps.
            for (bool true60 : {false, true}) {
                float distance = 0;
                const int logic = true60 ? 60 : 30;
                for (int frame = 0; frame < rate; ++frame)
                    for (int n = frame * logic / rate; n < (frame + 1) * logic / rate; ++n)
                        distance += 10.f * (true60 ? .5f : 1.f) * mods::move_factor(true, proc, 4, 4, factor);
                assert(std::abs(distance / 300.f - factor) < 1e-6f);
            }
        }
        assert(mods::move_factor(false, proc, 4, 4, factor) == 1);
        assert(mods::move_factor(true, proc, 0, 4, factor) == 1);
        assert(mods::move_factor(true, proc, 4, 8, factor) == 1);
    }
    for (uint32_t proc = 0; proc < 256; ++proc)
        if (proc != 6 && proc != 0x37) assert(mods::move_factor(true, proc, 4, 4, 4) == 1);
    assert(mods::clamp_move_speed(std::numeric_limits<float>::quiet_NaN()) == 1.5f);
    assert(mods::clamp_move_speed(100) == 4);
}
