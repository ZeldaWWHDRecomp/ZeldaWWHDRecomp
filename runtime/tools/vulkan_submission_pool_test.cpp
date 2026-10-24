// The Vulkan submission ring's size switch and slot choice (gfx/vulkan/submission_pool.h).
// Pure functions: no Vulkan device, no environment changes.
#include "gfx/vulkan/submission_pool.h"
#include <cassert>
#include <cstring>
#include <vector>
using namespace gfxvk::subpool;
static void slots(const char* value, const char* alias, unsigned expected, const char* source, bool adjusted) {
    const Choice c = choose(value, alias);
    assert(c.slots == expected);
    assert(!std::strcmp(c.source, source));
    assert(c.adjusted == adjusted);
}
int main() {
    // the default and the count
    slots(nullptr, nullptr, 4, "default", false);
    slots("", "", 4, "default", false);
    slots("8", nullptr, 8, "WWHD_VK_SUBMISSION_POOL", false);
    slots("4", nullptr, 4, "WWHD_VK_SUBMISSION_POOL", false);
    slots("16", nullptr, 16, "WWHD_VK_SUBMISSION_POOL", false);
    slots("6", nullptr, 6, "WWHD_VK_SUBMISSION_POOL", false);
    // out of range: clamped to 4..16
    slots("2", nullptr, 4, "WWHD_VK_SUBMISSION_POOL", true);
    slots("0", nullptr, 4, "WWHD_VK_SUBMISSION_POOL", true);
    slots("-3", nullptr, 4, "WWHD_VK_SUBMISSION_POOL", true);
    slots("64", nullptr, 16, "WWHD_VK_SUBMISSION_POOL", true);
    // not a number: ignored (the alias or the default decides)
    slots("eight", nullptr, 4, "default", true);
    slots("8x", "1", 8, "WWHD_VK_SUBMISSION_POOL8", true);
    // the reported name: 1 = 8 slots, 0 = 4; the count wins when both are set
    slots(nullptr, "1", 8, "WWHD_VK_SUBMISSION_POOL8", false);
    slots(nullptr, "0", 4, "WWHD_VK_SUBMISSION_POOL8", false);
    slots(nullptr, "yes", 4, "default", true);
    slots("6", "1", 6, "WWHD_VK_SUBMISSION_POOL", false);
    slots("12", "0", 12, "WWHD_VK_SUBMISSION_POOL", false);

    // ring steps: the next slot in order, wrapping at the current total
    for (size_t total : {size_t(4), size_t(8), size_t(16)}) {
        size_t active = 0;
        for (size_t i = 0; i < 3 * total; ++i) {
            const Next n = next_slot(active, total, false);
            assert(n.step == Step::Reuse && n.index == (active + 1) % total);
            active = n.index;
        }
    }
    // a captured tail holding the next slot grows the ring (the new slot is appended at index total)
    // until kMaxSlots exist, then the tail is resolved and the slot reused
    {
        size_t total = 4, active = 3;
        std::vector<Step> steps;
        while (true) {
            const Next n = next_slot(active, total, true);
            steps.push_back(n.step);
            if (n.step == Step::ResolveTail) {
                assert(n.index == (active + 1) % total);
                break;
            }
            assert(n.step == Step::Grow && n.index == total);
            active = n.index;
            ++total;
        }
        assert(total == kMaxSlots && steps.size() == kMaxSlots - 4 + 1);
    }
    // a start size of 16 leaves no overflow: a held tail resolves at once
    {
        const Next n = next_slot(15, 16, true);
        assert(n.step == Step::ResolveTail && n.index == 0);
    }
    // 8 at start still grows to 16 when tails hold slots
    {
        const Next n = next_slot(7, 8, true);
        assert(n.step == Step::Grow && n.index == 8);
    }
    return 0;
}
