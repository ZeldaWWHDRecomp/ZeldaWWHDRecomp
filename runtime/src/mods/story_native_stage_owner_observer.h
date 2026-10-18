// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_setinfo_observation.h"
#include "story_native_stage_owner.h"
#include "story_native_stage_owner_hooks.h"
#include <optional>

namespace story_native_stage_owner_observer {
using Candidate = story_native_setinfo_candidate::Candidate;
using Args = story_native_stage_owner_hooks::Args;
using Observer = story_native_stage_owner_hooks::Observer;
using Inspect = story_native_setinfo_observation::Result (*)(
    const Cpu*, const Args&, uint32_t, Candidate&);
using Live = bool (*)(uint32_t, const Candidate&, Candidate&);
using Saved = bool (*)(const ss::native_gate::SavedView&,uint32_t,const Candidate&,Candidate&);

// Bounded passive history only. Reads never acquire Stage lifetime locks and
// records grant no receipt, pin, continuation or mutation authority.
enum class MutationPhase : uint32_t { Begin=1, End=2, Fault=3, Restore=4 };
struct MutationDiagnostic {
 uint64_t sequence{},cookie{};uint32_t canonical{};Args args{};
 MutationPhase phase{};uint32_t before_generation{},after_generation{};
};
struct MutationHistory {std::array<MutationDiagnostic,16>events{};size_t count{};};
MutationHistory mutation_history()noexcept;

struct Snapshot {
    uint32_t control{};
    story_native_stage_owner::Receipt receipt{};
};

// Configure once, before native callbacks start. Production passes
// inspect_detailed and inspect_live. The hook registry has one observer slot;
// this observer and the standalone diagnostic are mutually exclusive.
bool configure(Inspect, Live, Saved = story_native_setinfo_observation::inspect_saved) noexcept;
const Observer* observer() noexcept;
story_native_stage_owner::Tracker& tracker() noexcept;
// Host-captured control and complete candidate identity, copied while holding
// the same fence used by native pre/post callbacks. Empty after any mutation.
std::optional<Snapshot> snapshot() noexcept;
// No new mutex acquisition. Requires this exact global Tracker's actual owning
// Scan on the current thread, suitable after a nonblocking try_scan under Q.
std::optional<Snapshot> snapshot(const story_native_stage_owner::Tracker::Scan&) noexcept;
// SavedView-only comparison against the configured bounded saved inspector.
// No live fallback, owner lookup, lifetime pin, receipt or entry authority.
bool matches_saved(const Snapshot&,const ss::native_gate::SavedView&)noexcept;
// Stage/resource-only continuation. It grants no process/PID/room ownership,
// native event lease, gate admission or permission to thaw. Same host thread.
class RestorePermit {
    friend std::optional<RestorePermit> prepare_restore(const Snapshot&,const ss::native_gate::SavedView&);
    friend std::optional<RestorePermit> prepare_restore_same_resource(const Snapshot&,const ss::native_gate::SavedView&);
    friend std::optional<Snapshot> finish_restore(RestorePermit&&);
    story_native_stage_owner::Tracker::RestorePermit native_;
    uint32_t control_{};Candidate candidate_{};bool consumed_=false;
    RestorePermit(story_native_stage_owner::Tracker::RestorePermit&& p,uint32_t c,Candidate v)
        :native_(std::move(p)),control_(c),candidate_(v){}
public:
    RestorePermit(const RestorePermit&)=delete;RestorePermit&operator=(const RestorePermit&)=delete;
    RestorePermit(RestorePermit&&)=default;RestorePermit&operator=(RestorePermit&&)=default;
};
std::optional<RestorePermit> prepare_restore(const Snapshot&,const ss::native_gate::SavedView&);
// Resource-only continuation across generations: exact full Candidate/control
// on both sides; underlying permit belongs to the actual CURRENT epoch.
// No event claim, process ownership, native entry or thaw authority.
std::optional<RestorePermit> prepare_restore_same_resource(const Snapshot&,const ss::native_gate::SavedView&);
std::optional<Snapshot> finish_restore(RestorePermit&&);
// Call at each full/progress state restore before any owner-backed scan.
void state_loaded() noexcept;
} // namespace story_native_stage_owner_observer
