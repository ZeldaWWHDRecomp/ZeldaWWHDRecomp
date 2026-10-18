// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_run_footprint.h"
namespace story_native_wait_snapshot_roots {
// Bounded data-only capture of native manager/control and actual camera root.
// It proves no listed native root borrows this arena, not general ownership,
// a retired lineage, physical park or permission to invoke/publish anything.
bool capture(const story_native_event_memory::Read&,const story_native_event_arena::Layout&,
 story_native_wait_run_footprint::Footprint&out);
}
