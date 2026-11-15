// Display timing source checks (gfx/vulkan/display_timing_source.h): the two-step past-timing read against a model of
// gamescope's WSI layer up to 3.16.31 (it erases as many timings as the caller's array holds), the refresh duration
// trust rule (gamescope's 16.667 ms placeholder against a 90 Hz screen), and the fallback to CPU-side even pacing
// when timings never come back or come back implausible. Pure logic: runs on every host.
#include "gfx/vulkan/display_timing_source.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace gfxvk::display_timing_source;
namespace {
struct Timing { uint32_t id; int64_t actual; };
// gamescope's layer (3.16.x GetPastPresentationTimingGOOGLE): copies min(count, stored), then erases the caller's
// ORIGINAL count, also when the data pointer is null. Erasing more than is stored is the heap corruption (the real
// layer's std::vector end moves before its start): the model sets CORRUPT instead.
struct OldLayer {
    std::vector<Timing> stored;
    bool fixed = false;  // 3.17.0: erases only what it returned, and nothing on a count query
    int calls = 0;
    bool corrupt = false;
    int operator()(uint32_t* count, Timing* data) {
        ++calls;
        const uint32_t original = *count;
        int result = 0;
        if (!data) *count = uint32_t(stored.size());
        else {
            const uint32_t n = std::min<uint32_t>(*count, uint32_t(stored.size()));
            for (uint32_t i = 0; i < n; ++i) data[i] = stored[i];
            result = n < stored.size() ? 5 : 0;  // (VK_INCOMPLETE)
            *count = n;
        }
        const uint32_t erase = fixed ? (data ? *count : 0) : original;
        if (erase > stored.size()) { corrupt = true; stored.clear(); }
        else stored.erase(stored.begin(), stored.begin() + erase);
        return result;
    }
};
constexpr int64_t kMs = 1'000'000;
}  // namespace

int main() {
    {  // the old read (one call with the whole array) corrupts the old layer as soon as fewer than 16 are stored
        OldLayer layer;
        layer.stored = {{1, 10}};
        Timing out[16];
        uint32_t n = 16;
        layer(&n, out);
        assert(layer.corrupt);
    }
    {  // nothing stored: only the count is asked for (0 in), nothing erased, nothing read
        OldLayer layer;
        Timing out[16];
        uint32_t n = 99;
        assert(fetch(layer, out, 16, n, 0) == 0 && n == 0 && layer.calls == 1 && !layer.corrupt);
    }
    {  // fewer stored than the array holds: exactly those are read and erased (the old single call with 16 asserted)
        OldLayer layer;
        layer.stored = {{1, 10}, {2, 20}, {3, 30}};
        Timing out[16];
        uint32_t n = 0;
        assert(fetch(layer, out, 16, n, 0) == 0 && n == 3 && layer.stored.empty() && !layer.corrupt);
        assert(out[0].id == 1 && out[2].id == 3);
    }
    {  // more stored than the array holds: the first 16, the rest stays for the next poll
        OldLayer layer;
        for (uint32_t i = 0; i < 20; ++i) layer.stored.push_back({i, int64_t(i)});
        Timing out[16];
        uint32_t n = 0;
        assert(fetch(layer, out, 16, n, 0) == 5 && n == 16 && layer.stored.size() == 4 && layer.stored[0].id == 16);
        assert(fetch(layer, out, 16, n, 0) == 0 && n == 4 && layer.stored.empty() && !layer.corrupt);
    }
    {  // the fixed layer (3.17.0) reads the same
        OldLayer layer;
        layer.fixed = true;
        layer.stored = {{7, 70}};
        Timing out[16];
        uint32_t n = 0;
        assert(fetch(layer, out, 16, n, 0) == 0 && n == 1 && out[0].id == 7 && layer.stored.empty());
    }
    {  // a failing count query: its result, nothing read
        Timing out[16];
        uint32_t n = 5;
        auto lost = [](uint32_t*, Timing*) { return -1000000000; };  // (VK_ERROR_SURFACE_LOST_KHR)
        assert(fetch(lost, out, 16, n, 0) == -1000000000 && n == 0);
    }
    {  // gamescope's placeholder refresh on a 90 Hz screen is not used; the compositor's 11.111 ms is
        Source s;
        s.gamescope = true;
        assert(!s.refresh(Source::kLayerPlaceholder, 90) && s.period() == 0);
        assert(s.refresh(11'111'111, 90) && s.period() == 11'111'111);
        assert(!s.refresh(11'111'111, 90));  // (unchanged)
        assert(s.refresh(Source::kLayerPlaceholder, 60) && s.period() == Source::kLayerPlaceholder);  // 60 Hz LCD Deck
        assert(s.refresh(Source::kLayerPlaceholder, 90) && s.period() == 0);  // refresh rate changed: wait again
        // no rate known: gamescope's placeholder is not trusted, any other plausible value is
        assert(!s.trusted(Source::kLayerPlaceholder, 0) && s.trusted(8'333'333, 0));
        assert(!s.trusted(0, 0) && !s.trusted(100 * kMs, 0) && !s.trusted(500'000, 0));
        Source driver;  // a driver's extension (not gamescope): 16.667 ms with no rate known is a 60 Hz screen
        assert(driver.trusted(Source::kLayerPlaceholder, 0) && driver.trusted(16'683'333, 60));
        assert(!driver.trusted(16'666'666, 120));
    }
    {  // Android: no checks; every duration the driver gives is used, the source is never ruled out
        Source s;
        s.checks = false;
        assert(s.refresh(8'300'000, 120) && s.period() == 8'300'000);
        for (int i = 0; i < 2000; ++i) assert(!s.asked_one());
        assert(s.chain() && !s.timing(10 * kMs, 0) && s.chain());
    }
    {  // timings that never come back: ruled out after kProbe presents, present times are no longer asked for
        Source s;
        s.gamescope = true;
        for (uint64_t i = 1; i < Source::kProbe; ++i) assert(!s.asked_one() && s.chain());
        assert(s.asked_one() && s.state == State::Unusable && !s.chain());
    }
    {  // plausible timings confirm it; it stays usable while they keep coming
        Source s;
        int64_t call = 1000 * kMs;
        for (uint64_t i = 1; i <= Source::kConfirm; ++i, call += 16 * kMs) {
            assert(!s.asked_one());
            const bool changed = s.timing(call, call + 20 * kMs);
            assert(changed == (i == Source::kConfirm));
        }
        assert(s.state == State::Usable);
        for (uint64_t i = 0; i < 5000; ++i, call += 16 * kMs) { s.asked_one(); s.timing(call, call + 20 * kMs); }
        assert(s.state == State::Usable);
        // they stop: dropped after kSilent presents
        for (uint64_t i = 1; i < Source::kSilent; ++i) assert(!s.asked_one());
        assert(s.asked_one() && !s.chain());
    }
    {  // implausible times (0, before the call, seconds late) rule it out while probing
        Source s;
        const int64_t call = 1000 * kMs;
        const int64_t bad[] = {0, call - 200 * kMs, call + 2000 * kMs, -5, 0, 0, 0};
        for (int64_t a : bad) assert(!s.timing(call, a));
        assert(s.timing(call, 0) && s.state == State::Unusable);
        // a few implausible ones among good ones do not
        Source t;
        for (int i = 0; i < 7; ++i) t.timing(call, 0);
        for (uint64_t i = 0; i < Source::kConfirm; ++i) t.timing(call, call + 15 * kMs);
        assert(t.state == State::Usable);
    }
    std::printf("display timing source: ok\n");
    return 0;
}
