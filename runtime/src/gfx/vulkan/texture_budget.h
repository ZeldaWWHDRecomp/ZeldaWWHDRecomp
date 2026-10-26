// SPDX-License-Identifier: MPL-2.0
// The texture budget's choice (surfaces.cpp texture_budget_step, WWHD_VK_TEXTURE_BUDGET): which released-able
// textures go, least recently looked up first. Pure, so it is tested without a GPU (runtime/tools/artistic_mips_test.cpp).
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace gfxvk::texture_budget {

struct Candidate {
    uint64_t lastUsed = 0;  // frame of the last lookup
    uint64_t bytes = 0;     // its image's allocation
    size_t index = 0;       // the caller's
};

// Of all images that may be released (total: their bytes in all), those to release now: none looked up within the
// last `pin` frames; least recently used first while the total is over `budget` (down to 90 % of it, so a total at
// the edge does not release on every sweep); and, with idle != 0, every one unused for `idle` frames. budget 0:
// no budget (idle release only).
inline std::vector<size_t> select(std::vector<Candidate> all, uint64_t total, uint64_t frame, uint64_t budget,
                                  uint64_t idle, uint64_t pin) {
    std::vector<size_t> out;
    std::erase_if(all, [&](const Candidate& c) { return c.lastUsed + pin > frame; });
    std::sort(all.begin(), all.end(), [](const Candidate& a, const Candidate& b) {
        return a.lastUsed != b.lastUsed ? a.lastUsed < b.lastUsed : a.index < b.index;
    });
    const uint64_t target = budget - budget / 10;
    bool over = budget && total > budget;
    for (const auto& c : all) {
        const bool idleOut = idle && c.lastUsed + idle <= frame;
        if (!idleOut && !(over && total > target)) {
            if (!idle) break;  // sorted by age: with no idle release nothing newer goes either
            continue;
        }
        out.push_back(c.index);
        total -= std::min(total, c.bytes);
    }
    return out;
}

}  // namespace gfxvk::texture_budget
