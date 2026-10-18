// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_setinfo_observation.h"
#include <cstdint>

namespace story_native_setinfo_diagnostic {
using Inspect = story_native_setinfo_observation::Result (*)(
    const Cpu*, const story_native_stage_owner_hooks::Args&, uint32_t,
    story_native_setinfo_candidate::Candidate&);
using LiveInspect = bool (*)(uint32_t,
    const story_native_setinfo_candidate::Candidate&,
    story_native_setinfo_candidate::Candidate&);
using DeleteMatch = bool (*)(const story_native_stage_owner_hooks::Args&,uint32_t,
    const story_native_setinfo_candidate::Candidate&);
struct Counts {
    uint32_t setinfo{}, setinfo_entered{}, valid{}, candidate_invalid{}, manager_invalid{};
    uint32_t layout_invalid{}, return_invalid{}, failed{}, teardown{}, fault{};
    uint32_t exact_teardown_matches{}, live_at_teardown{};
    uint32_t raw_frees{}, exact_raw_free_matches{}, live_at_raw_free{};
};
// Opt-in passive observer, installed only after all eleven original pass-through
// hooks are installed. It never issues an owner receipt or mutates guest state.
bool enable(Inspect inspect = story_native_setinfo_observation::inspect_detailed,
            LiveInspect live = story_native_setinfo_observation::inspect_live,
            DeleteMatch deletion = story_native_setinfo_observation::matches_delete) noexcept;
Counts counts() noexcept;
} // namespace story_native_setinfo_diagnostic
