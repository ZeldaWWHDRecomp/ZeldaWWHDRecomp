// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_preflight.h"
#include <algorithm>

namespace story_native_wait_preflight {
Result extract(const story_native_event_arena::Layout& layout,
               ss::Reader allocations,
               std::span<const ss::MemoryRegion> regions,
               const ss::MemoryChunks& chunks) {
 if (!story_native_event_arena::valid(layout)) return {};
 std::array<uint8_t, story_native_wait_journal::kBytes> bytes{};
 if (!story_native_event_arena::read_saved(layout, allocations, regions, chunks,
                                            layout.wait.header.address, bytes)) return {};
 if (std::all_of(bytes.begin(), bytes.end(), [](uint8_t byte) { return byte == 0; }))
  return {Status::EmptyHeader, {}};
 story_native_wait_journal::Record journal;
 if (!story_native_wait_journal::decode(bytes, journal) || journal.arena != layout.allocation.address)
  return {Status::CorruptHeader, {}};
 return {Status::Extracted, journal};
}
}
