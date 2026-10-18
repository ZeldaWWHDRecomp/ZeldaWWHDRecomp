// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_table_scan.h"
#include "story_native_stage_owner.h"

namespace story_native_stage_table_owner {
using Context = story_native_event_table_scan::Context;
using Result = story_native_event_table_scan::Result;
using Read = story_native_event_table_scan::Read;
using Current = std::function<bool(Context&)>;
using Live = std::function<bool(const story_native_setinfo_candidate::Candidate&)>;

// Hold the Stage owner scan guard across both live identity rechecks and the
// complete event-table census. Current supplies measured game-thread context;
// its resource field is ignored. Live must re-read and match the recorded
// Info/holder/heap/extent graph. This remains read-only and issues no lease.
Result inspect(story_native_stage_owner::Tracker&, const Read&, const Current&,
               const Live&);
} // namespace story_native_stage_table_owner
