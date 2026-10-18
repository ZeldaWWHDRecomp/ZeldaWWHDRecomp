// SPDX-License-Identifier: MPL-2.0
#include "story_native_stage_table_owner.h"
#include "guest_addr.h"
#include <algorithm>

namespace story_native_stage_table_owner {
Result inspect(story_native_stage_owner::Tracker& tracker, const Read& read,
               const Current& current, const Live& live) {
    using namespace story_native_event_table_scan;
    auto scan = tracker.scan();
    if (!scan.valid() || !read || !current || !live)
        return {false, Reason::Provenance};
    const auto receipt = scan.receipt();
    const auto& owner = receipt.owner;
    const auto end = std::find(owner.stage_name.begin(), owner.stage_name.end(), '\0');
    const size_t name_bytes = size_t(end - owner.stage_name.begin());
    if (!name_bytes || name_bytes > 8 || !receipt.generation)
        return {false, Reason::Provenance};
    std::array<uint8_t, 8> name{};
    std::copy_n(owner.stage_name.begin(), name_bytes, name.begin());
    const Query query = [&](Context& out) {
        if (!scan.current() || !live(owner) || !current(out) ||
            out.stage_name != name) return false;
        out.resource = {owner.resource, owner.resource_bytes,
                        receipt.generation, GD(0x1004DF68),
                        GD(0x1004DF70), true};
        return scan.current();
    };
    return story_native_event_table_scan::inspect(read, query);
}
} // namespace story_native_stage_table_owner
