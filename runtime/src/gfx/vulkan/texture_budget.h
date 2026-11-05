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

// When and how the sweep runs. Desktop: only with WWHD_VK_TEXTURE_BUDGET=1 (budget a quarter of the device's memory
// budget, release by age only past it, pin 3 frames, every 30 frames). Android: on by default (WWHD_VK_TEXTURE_BUDGET=0
// turns it off): every 300 frames, textures unused for 300 frames go least recently used first while the total is
// over max(RAM / 10, 384 MB), any unused for 7200 frames go regardless, and while the room above the system's
// low-memory line is under 768 MB every texture unused for 300 frames goes. From Nycolazs/WindWakerHD-Android-Plus
// e8d20b8 via GreenNaugahyde/ZeldaWWHDRecompAndroid v0.9.9 (db65d60, vk_surfaces.cpp evict_unused_textures), MPL-2.0.
struct Policy {
    bool on = false;
    uint64_t budget = 0, idle = 0, pin = 3;
    unsigned every = 30;  // frames between sweeps
};
constexpr uint64_t kAndroidPin = 300, kAndroidIdle = 7200, kAndroidMinBudget = 384ull << 20;
constexpr int kAndroidTightMb = 768;
// envOn: WWHD_VK_TEXTURE_BUDGET (nullptr unset); deviceBudget: a quarter of it is the desktop budget; ramBytes: the
// device's memory (0 unknown); headroomMb: room above the low-memory line (-1 unknown)
inline Policy policy(bool android, const char* envOn, uint64_t deviceBudget, uint64_t ramBytes, int headroomMb) {
    Policy p;
    const bool forcedOn = envOn && envOn[0] == '1' && !envOn[1];
    const bool forcedOff = envOn && envOn[0] == '0' && !envOn[1];
    if (android && !forcedOff && !forcedOn) {
        p.on = true;
        p.every = 300;
        p.pin = kAndroidPin;
        p.idle = headroomMb >= 0 && headroomMb < kAndroidTightMb ? kAndroidPin : kAndroidIdle;
        p.budget = std::max<uint64_t>(ramBytes / 10, kAndroidMinBudget);
        return p;
    }
    if (!forcedOn) return p;
    p.on = true;
    p.budget = deviceBudget / 4;
    return p;
}

}  // namespace gfxvk::texture_budget
