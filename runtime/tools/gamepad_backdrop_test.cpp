#include "../src/gfx/gamepad_backdrop.h"
#include <cassert>
#include <cmath>
#include <limits>
using namespace gfx::gamepad_backdrop;
int main() {
    assert(enabled.load());
    assert(!capture_requested.load() && !capture_enabled());
    set_capture_requested(true);
    assert(capture_enabled());
    enabled.store(false);
    assert(!capture_enabled());
    enabled.store(true);
    assert(capture_enabled());
    set_capture_requested(false); // no output/reset/shutdown
    assert(!capture_enabled());
    // Entry/exit pairing is producer scope-local, not re-tested from this flag
    // at exit: disabling between the two must still close an emitted begin.
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
    const auto check_bars = [](float w, float h, Rect c) {
        const auto output = bars(w, h, c);
        for (const auto& b : output) {
            assert(b.x >= 0 && b.y >= 0 && b.w >= 0 && b.h >= 0);
            assert(b.x + b.w <= w && b.y + b.h <= h);
            if (b.w && b.h) assert(b.x + b.w <= c.x || b.x >= c.x + c.w ||
                                      b.y + b.h <= c.y || b.y >= c.y + c.h);
        }
        // No two nonempty bars overlap.
        for (unsigned i = 0; i < 4; ++i) for (unsigned j = i + 1; j < 4; ++j) {
            const auto& a = output[i]; const auto& b = output[j];
            if (a.w && a.h && b.w && b.h)
                assert(a.x + a.w <= b.x || b.x + b.w <= a.x ||
                       a.y + a.h <= b.y || b.y + b.h <= a.y);
        }
        return output;
    };
    auto b = check_bars(31, 27, content);
    assert(b[unsigned(Edge::Top)].h == content.y);
    assert(b[unsigned(Edge::Bottom)].h == content.y);
    assert(b[unsigned(Edge::Left)].w == 0 && b[unsigned(Edge::Right)].w == 0);
    b = check_bars(40, 9, {12, 0, 16, 9});
    assert(b[unsigned(Edge::Left)].w == 12 && b[unsigned(Edge::Right)].w == 12);
    b = check_bars(16, 9, {0, 0, 16, 9});
    for (const auto& r : b) assert(r.w * r.h == 0);
    b = check_bars(16, 9, {-3, -2, 24, 14});
    for (const auto& r : b) assert(r.w * r.h == 0);
    check_bars(16, 9, {-3, 2, 10, 14});
    check_bars(16, 9, {20, 20, 2, 2});
    for (const auto& r : bars(16, 9, {0, 0, -1, 9})) assert(r.w * r.h == 0);
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
