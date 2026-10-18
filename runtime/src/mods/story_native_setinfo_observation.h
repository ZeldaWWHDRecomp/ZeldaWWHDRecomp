// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_setinfo_candidate.h"
#include "story_native_stage_owner_hooks.h"

struct Cpu;
namespace ss::native_gate {struct SavedView;}
namespace story_native_setinfo_observation {
enum class Result { Valid, ReturnBoundary, Layout, Manager, Candidate };
// A post-original SetInfo callback may call inspect(), but the result is only
// a read-only structural candidate. No observer is registered here; no owner
// receipt or publication is authorized without a complete destruction fence.
bool inspect(const Cpu* cpu, const story_native_stage_owner_hooks::Args& args,
             uint32_t result, story_native_setinfo_candidate::Candidate& out);
Result inspect_detailed(const Cpu* cpu,
                        const story_native_stage_owner_hooks::Args& args,
                        uint32_t result,
                        story_native_setinfo_candidate::Candidate& out);
bool inspect_live(uint32_t control,
                  const story_native_setinfo_candidate::Candidate& expected,
                  story_native_setinfo_candidate::Candidate& out);
// Read-only snapshot graph verification. Every read is supplied by SavedView;
// missing coverage fails and sparse in-region chunks are explicit zero. No
// live EA fallback, process/PID ownership, Stage receipt or entry authority.
bool inspect_saved(const ss::native_gate::SavedView&,uint32_t control,
                   const story_native_setinfo_candidate::Candidate& expected,
                   story_native_setinfo_candidate::Candidate& out);
// Pre-original DeleteInfo r3/r4/r5 are control/name/path. This only matches
// copied names to a prior candidate; it does not revoke or retain the Info.
bool matches_delete(const story_native_stage_owner_hooks::Args& args,
                    uint32_t control,
                    const story_native_setinfo_candidate::Candidate& expected);
} // namespace story_native_setinfo_observation
