// The Vulkan renderer's submission ring (backend.cpp): how many submission slots exist at start and
// which slot the next recording uses. Each slot owns a command pool, a fence, a descriptor pool and
// the upload blocks of its submission; a slot is reused only after its fence has signalled, so the
// render thread waits when it comes back round to a slot the GPU has not finished.
//
//   WWHD_VK_SUBMISSION_POOL=N      N slots at start, 4..16 (default 4)
//   WWHD_VK_SUBMISSION_POOL8=1/0   the older name: 1 = 8 slots, 0 = 4; WWHD_VK_SUBMISSION_POOL wins
//
// The ring grows past its start size (up to kMaxSlots in all) only while a captured, not yet
// submitted command tail holds the next slot. Pure functions, tested by submission_pool_test.cpp.
#pragma once
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace gfxvk {
// The running renderer's ring size at start (backend.cpp); 0 before the Vulkan device exists.
unsigned submission_pool_slots();
}

namespace gfxvk::subpool {

constexpr unsigned kMinSlots = 4, kMaxSlots = 16, kDefaultSlots = 4;

struct Choice {
    unsigned slots = kDefaultSlots;
    const char* source = "default";  // the variable that chose it, or "default"
    bool adjusted = false;           // a value was out of range (clamped) or not a number (ignored)
};

// value: WWHD_VK_SUBMISSION_POOL, alias: WWHD_VK_SUBMISSION_POOL8 (either may be null)
inline Choice choose(const char* value, const char* alias) {
    Choice c;
    if (value && *value) {
        char* end = nullptr;
        const long n = std::strtol(value, &end, 10);
        if (end == value || *end) {
            c.adjusted = true;  // not a number: fall back to the alias or the default
        } else {
            c.source = "WWHD_VK_SUBMISSION_POOL";
            c.adjusted = n < long(kMinSlots) || n > long(kMaxSlots);
            c.slots = unsigned(n < long(kMinSlots) ? long(kMinSlots) : n > long(kMaxSlots) ? long(kMaxSlots) : n);
            return c;
        }
    }
    if (alias && *alias) {
        if (!std::strcmp(alias, "1") || !std::strcmp(alias, "0")) {
            c.source = "WWHD_VK_SUBMISSION_POOL8";
            c.slots = *alias == '1' ? 8 : 4;
        } else {
            c.adjusted = true;
        }
    }
    return c;
}

// The ring step after an asynchronous submission: the slot after `active`, or, when that slot still
// holds a captured tail, a new overflow slot while fewer than kMaxSlots exist; otherwise the tail is
// resolved and the slot reused.
enum class Step { Reuse, Grow, ResolveTail };
struct Next {
    size_t index;
    Step step;
};
inline Next next_slot(size_t active, size_t total, bool nextHoldsTail) {
    const size_t next = (active + 1) % total;
    if (!nextHoldsTail) return {next, Step::Reuse};
    if (total < kMaxSlots) return {total, Step::Grow};
    return {next, Step::ResolveTail};
}

}  // namespace gfxvk::subpool
