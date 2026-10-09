#include "gfx/capture_schedule.h"
#include <cassert>
#include <limits>
using namespace gfx::capture_schedule;
int main() {
    assert(parse(nullptr).empty());
    assert(parse("").empty());
    assert((parse("0,180,182,,184,") == std::vector<uint64_t>{0,180,182,184}));
    assert((parse("180,invalid,184") == std::vector<uint64_t>{180}));
    assert(parse("invalid").empty());
    assert(parse("18446744073709551616").empty());
    assert((parse("18446744073709551615") == std::vector<uint64_t>{UINT64_MAX}));
    const auto offsets = parse("0,180,182,184");
    assert(!relative_due(offsets, 180, 0)); // No completed load.
    assert(!relative_due(offsets, 2999, 3000)); // No unsigned underflow.
    assert(relative_due(offsets, 3000, 3000));
    assert(!relative_due(offsets, 3179, 3000));
    assert(relative_due(offsets, 3180, 3000));
    assert(relative_due(offsets, 3193, 3013)); // Async completion shifted; same offset.
    assert(!relative_due(offsets, 3180, 3013)); // Absolute alignment is insufficient.
    assert(!relative_due(offsets, 3181, 3000));
    assert(relative_due(offsets, UINT64_MAX, UINT64_MAX-180));
    const auto absolute = parse("3400,3402,3404");
    assert(contains(absolute, 3402));
    assert(!contains(absolute, 3403));
    assert(stem(3402, false) == "frame_3402");
    assert(stem(182, true) == "load_frame_182");
    assert(relative_due(offsets, 4182, 4000)); // New load uses new origin.
    assert(!relative_due(offsets, 4182, 3000));
}
