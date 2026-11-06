#include "../src/gfx/gamepad_backdrop.h"
#include "../src/gfx/vulkan/gamepad_backdrop_present.h"
#include <vector>
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
    struct Quad { void* image; Rect box; bool solid = false; };
    int tv, drc;
    std::vector<Quad> quads{{&tv, {0, 0, 0, 0}}, {nullptr, {0, 4, 31, 18}, true},
                            {&drc, content}};
    // Actual production selection helper: the retained zero-size TV placeholder
    // must not hide the later full-output GamePad. Its exact fit rect is retained.
    assert(gfxvk::gamepad_backdrop_quad(quads, &drc, true) == 2);
    assert(gfxvk::gamepad_backdrop_quad(quads, &drc, false) == quads.size()); // PiP
    assert(quads[2].box.y == content.y && quads[2].box.h == content.h);
    {
        // Off-TV plan: tv placeholder, 0.6-alpha PiP frame, picture. The bars go
        // after the frame and before the picture (Metal compose_tv order), so the
        // frame's border never lies over the bars.
        auto ordered = quads;
        const std::vector<Quad> barQuads{{&drc, {0, 0, 31, 4.78f}}, {&drc, {0, 22.2f, 31, 4.8f}}};
        const auto picture = gfxvk::gamepad_backdrop_quad(ordered, &drc, true);
        gfxvk::insert_backdrop_bars(ordered, picture, barQuads);
        assert(ordered.size() == 5 && ordered[1].solid);
        assert(ordered[2].box.y == 0 && ordered[3].box.y == 22.2f);
        assert(ordered[4].image == &drc && ordered[4].box.h == content.h);
        // The frame precedes every bar: nothing solid is drawn after the first bar.
        for (std::size_t i = 2; i < ordered.size(); ++i) assert(!ordered[i].solid);
        // DRC window: the picture is the only quad; bars come first.
        std::vector<Quad> window{{&drc, content}};
        gfxvk::insert_backdrop_bars(window, gfxvk::gamepad_backdrop_quad(window, &drc, false), barQuads);
        assert(window.size() == 3 && window[2].box.h == content.h);
    }
    quads = {{&drc, content}};
    assert(gfxvk::gamepad_backdrop_quad(quads, &drc, false) == 0); // DRC window
    quads[0].box.w = 0;
    assert(gfxvk::gamepad_backdrop_quad(quads, &drc, true) == quads.size());
    quads.clear();
    assert(gfxvk::gamepad_backdrop_quad(quads, &drc, true) == 0);
    // Shared video-memory policy (both backends).
    assert(!release_idle(true, 0, 0) && !release_idle(true, kIdleFrames - 1, 0));
    assert(release_idle(true, kIdleFrames, 0) && release_idle(true, 1000 + kIdleFrames, 1000));
    assert(!release_idle(true, 1000 + kIdleFrames - 1, 1000));
    assert(release_idle(false, 0, 0) && release_idle(false, 5, 5)); // option off: at once
    assert(!release_idle(true, 10, 20)); // frame counter behind the last capture: not idle
    const uint64_t rgba16 = 1280ull * 720 * 8;
    assert(fits_pool(0, 0, rgba16) && fits_pool(kPoolBytes - rgba16, 0, rgba16));
    assert(!fits_pool(kPoolBytes - rgba16 + 1, 0, rgba16));
    assert(fits_pool(kPoolBytes, rgba16, rgba16)); // replacing an image of the same size
    assert(!fits_pool(0, 0, kPoolBytes + 1) && !fits_pool(0, 0, 0));
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
