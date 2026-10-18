// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_snapshot_roots.h"
#include <string>
namespace story_native_wait_snapshot_actor_roots {
// Appends exact declared resident roots to an existing data footprint. Actual
// host placement and native list membership are both required; no guest lookup
// or entry authority is issued. Unplaced residents require a coherent all-People
// census with no cut borrowing this arena; saved matching must repeat that proof.
// Production scene callers supply both immutable actor IDs.
bool append(const story_native_event_memory::Read&,
 const story_native_event_arena::Layout&,const std::string&mod,
 std::span<const std::string>ids,story_native_wait_run_footprint::Footprint&);
// These are data checks, never replacements for a genuine checkpoint/park.
bool no_borrow(const story_native_event_memory::Read&,const story_native_event_arena::Layout&);
bool no_borrow_saved(const ss::native_gate::SavedView&,const story_native_event_arena::Layout&);
}
