// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_arena.h"
#include "story_native_wait_journal.h"

namespace story_native_wait_preflight {
enum class Status { NoSavedArena, EmptyHeader, CorruptHeader, Extracted };
struct Result {
 Status status=Status::NoSavedArena;
 story_native_wait_journal::Record journal{};
};

// Call while the game is quiescent, before ss::restore_memory, with ONLY
// Snapshot::section(kSecAllocs), Snapshot::regs, and Snapshot::chunks. The
// layout must come from an independently authenticated live allocation owner.
// Extraction does not authorize load, restore, hook admission, or thaw: the
// issuer must still match generation/definition, saved native state and depth,
// and live resource/actor ownership. Never use ss::snap_ld32 here: it can
// fall back to live memory when an address is absent from the snapshot.
Result extract(const story_native_event_arena::Layout&,
               ss::Reader saved_allocations,
               std::span<const ss::MemoryRegion> saved_regions,
               const ss::MemoryChunks& saved_chunks);
}
