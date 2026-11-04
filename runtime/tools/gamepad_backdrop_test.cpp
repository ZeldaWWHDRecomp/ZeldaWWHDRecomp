#include "../src/gfx/gamepad_backdrop.h"
#include <cassert>
#include <cmath>
#include <limits>
using namespace gfx::gamepad_backdrop;
int main() {
    assert(enabled.load());
    assert(is_background("P_BgPattern_00", 1280, 720));
    assert(!is_background("RootPane", 1280, 720));
    assert(!is_background("P_BgPattern_00-child", 1280, 720));
    assert(!is_background("P_BgPattern_00", 1200, 700));
    const Rect original{0, 0, 1280, 720};
    assert(!has_bars(1280, 720, original));
    // 31:27 window: centered native 16:9 content remains exactly 31x17.4375.
    const Rect content{0, (27.f - 17.4375f) * .5f, 31, 17.4375f};
    assert(has_bars(31, 27, content));
    const auto background = cover(31, 27, 1280, 720);
    assert(std::fabs(background.y) < .00001f && std::fabs(background.h - 27) < .00001f);
    assert(background.x < 0 && background.w > 31);
    assert(content.y == 4.78125f && content.h == 17.4375f);
    assert(!has_bars(0, 27, content));
    assert(cover(31, 0, 1280, 720).w == 0);
    assert(cover(std::numeric_limits<float>::infinity(), 27, 1280, 720).w == 0);
    ScanCache cache;
    cache.capture(1, 10); cache.capture(2, 20); // TV and DRC distinct surfaces.
    auto selected = cache.scan(2);
    assert(selected.generation == 1 && selected.ticket == 20);
    for (int i = 0; i != 100; ++i) assert(cache.committed().ticket == 20); // re-present
    selected = cache.scan(2); // genuinely new DRC scan lacking background
    assert(selected.generation == 2 && selected.ticket == 0);
    cache.capture(2, 30); cache.capture(2, 31);
    assert(cache.scan(2).ticket == 31); // latest tagged replay wins
    assert(cache.scan(1).ticket == 0); // old TV capture cannot leak across DRC generations
    assert(cache.scan(0).ticket == 0);
    cache.capture(2, 40); cache.reset();
    assert(cache.committed().generation == 0 && cache.scan(2).ticket == 0);
    for (uint64_t i = 1; i <= 17; ++i) cache.capture(i, i + 100);
    assert(cache.scan(17).ticket == 117); // overflow keeps only fresh capture
    assert(cache.scan(1).ticket == 0); // bounded overflow fails closed
}
