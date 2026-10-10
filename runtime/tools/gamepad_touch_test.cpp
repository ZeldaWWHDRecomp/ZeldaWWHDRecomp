#include "platform/gamepad_touch.h"
#include "platform/dual_display.h"
#include <cassert>
#include <limits>

int main() {
    for (bool swapped : {false, true}) {
        assert(dual_display::source_index(0, false, swapped) == 0);
        assert(dual_display::source_index(1, false, swapped) == 1);
        assert(dual_display::source_index(0, true, swapped) == (swapped ? 1 : 0));
        assert(dual_display::source_index(1, true, swapped) == (swapped ? 0 : 1));
    }
    using namespace gamepad_touch;
    float x, y;
    Viewport letterbox{0, .25f, 1, .5f};
    assert(map(letterbox, 0, .25f, false, x, y) && x == 0 && y == 0);
    assert(map(letterbox, 1, .75f, false, x, y) && x == 1 && y == 1);
    assert(map(letterbox, .5f, .5f, false, x, y) && x == .5f && y == .5f);
    assert(!map(letterbox, .5f, .1f, false, x, y));
    assert(map(letterbox, 2, -.5f, true, x, y) && x == 1 && y == 0);
    Viewport pillarbox{.25f, 0, .5f, 1};
    assert(!map(pillarbox, .1f, .5f, false, x, y));
    assert(map(pillarbox, .5f, .5f, false, x, y) && x == .5f && y == .5f);
    assert(!map({}, .5f, .5f, true, x, y));
    assert(!map(letterbox, std::numeric_limits<float>::quiet_NaN(), 0, true, x, y));
    assert(!map({0, 0, std::numeric_limits<float>::infinity(), 1}, 0, 0, true, x, y));
}
