// SPDX-License-Identifier: MPL-2.0
// Hand-drawn mip level detection (gfx/artistic_mips.h) and the texture budget's choice (gfx/vulkan/texture_budget.h).
#include "gfx/artistic_mips.h"
#include "gfx/vulkan/texture_budget.h"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace gfx::artistic_mips;

int main() {
    // RGBA8 64x64 chain: level 0 random-ish, the rest box downscales -> downscaled
    const uint32_t W = 64;
    std::vector<std::vector<uint8_t>> levels;
    std::vector<uint8_t> base(W * W * 4);
    uint32_t seed = 7;
    for (auto& b : base) { seed = seed * 1103515245u + 12345u; b = uint8_t(seed >> 23); }
    levels.push_back(base);
    for (uint32_t w = W; w > 1; w /= 2) levels.push_back(downscale(levels.back(), w, w, w / 2, w / 2));
    auto source = [&](uint32_t n, const uint8_t*& p, size_t& size) {
        if (n >= levels.size()) return false;
        p = levels[n].data(); size = levels[n].size(); return true;
    };
    Verdict v = check(0x1A, W, W, uint32_t(levels.size()), source);
    assert(!v.artistic && v.compared >= 3 && v.firstLevel == 0);
    for (uint32_t n = 1; n <= v.compared; ++n) assert(v.diff[n] < 0.5f);
    // the same chain with every level from 2 on replaced by a different picture (a distance effect) -> hand-drawn
    for (size_t n = 2; n < levels.size(); ++n)
        for (size_t i = 0; i < levels[n].size(); ++i) levels[n][i] = uint8_t(i % 8 < 4 ? 0 : 255);
    v = check(0x1A, W, W, uint32_t(levels.size()), source);
    assert(v.artistic);
    // a large texture starts at the first level of at most 64x64 pixels
    assert(check(0x1A, 1024, 1024, 11, [&](uint32_t n, const uint8_t*& p, size_t& size) {
        static std::vector<uint8_t> big; big.assign(size_t(std::max(1024u >> n, 1u)) * std::max(1024u >> n, 1u) * 4, 128);
        p = big.data(); size = big.size(); return true; }).firstLevel == 4);
    // BC5: a decoded constant block, and formats the check skips
    assert(bc_type(0x35) == 5 && bc_type(0x235) == 5 && bc_type(0x431) == 1 && bc_type(0x1A) == 0);
    assert(supported(0x35) && supported(0x1A) && !supported(0x19) && !supported(0x816));
    std::vector<uint8_t> block(16, 0), rgba;
    block[0] = block[1] = 200; block[8] = block[9] = 50;
    assert(to_rgba8(0x35, block.data(), block.size(), 4, 4, rgba));
    for (size_t i = 0; i < 16; ++i) assert(rgba[i * 4] == 200 && rgba[i * 4 + 1] == 50);
    assert(!check(0x19, 64, 64, 7, source).compared);   // unsupported format: no verdict
    assert(!check(0x1A, 64, 64, 1, source).compared);   // one level
    // the bias at an internal resolution scale
    assert(resolution_bias(1, 1) == 0 && resolution_bias(2, 2) == 1 && resolution_bias(4, 2) == 1);

    // texture budget: pinned, least recently used first, down to 90 %, idle release
    using gfxvk::texture_budget::Candidate;
    using gfxvk::texture_budget::select;
    std::vector<Candidate> all{{100, 40, 0}, {10, 30, 1}, {50, 30, 2}, {99, 20, 3}};
    // total 120, budget 100: release oldest until <= 90 -> index 1 (120 -> 90)
    assert((select(all, 120, 100, 100, 0, 3) == std::vector<size_t>{1}));
    // budget 50: target 45 -> 1, 2 (120 -> 60 -> 30); 3 and 0 are pinned (used in the last 3 frames)
    assert((select(all, 120, 100, 50, 0, 3) == std::vector<size_t>{1, 2}));
    // within budget: nothing
    assert(select(all, 120, 100, 200, 0, 3).empty());
    // idle 60 frames, no budget: only 1 (unused for 90 frames; 2 for 50 stays)
    assert((select(all, 120, 100, 0, 60, 3) == std::vector<size_t>{1}));
    // pin larger than everything: nothing goes even far over budget
    assert(select(all, 120, 100, 10, 0, 1000).empty());
    puts("artistic_mips_test: ok");
    return 0;
}
