// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "savestate.h"
#include "state_memory.h"
#include "mods/story_native_event_lease.h"
#include "mods/story_native_wait_journal.h"
#include <atomic>
#include <memory>
#include <span>

namespace story_native_stage_restore_group {class StageRestoreGroup;}
namespace story_native_restore_resume {class CurrentLoadOwner;}
namespace ss::native_gate {
// The controller owns this object and must retain its real entry fence until
// destruction. A Hold is a lifetime mechanism, not an ownership certificate.
struct Hold { virtual ~Hold() = default; };
struct SavedView {
 Reader allocations;
 std::span<const MemoryRegion> regions;
 const MemoryChunks& chunks;
};
// Pure fail-closed policy for a future controller's source-backed receipts.
// These fields are NOT populated by savestate or inferred from a journal.
// A controller must acquire its real fence and verify every field before use.
struct LiveBoundary {
 story_native_wait_journal::Record journal{};
 story_native_event_lease::Phase lease=story_native_event_lease::Phase::Quarantined;
 bool exact_allocation=false,journal_readback=false,fence_held=false;
 bool manager_unpublished=false,consumers_detached=false,checkpoint_clean=false;
 uint32_t callback_depth=UINT32_MAX;
};
struct SavedBoundary {
 story_native_wait_journal::Record journal{};
 bool snapshot_owned=false,exact_allocation=false;
 bool manager_unpublished=false,consumers_detached=false,depth_zero=false;
};
// Conservative first gate: active, draining, partial or quarantined scenes
// cannot be captured/restored. Supporting active snapshots requires a separate
// saved-native rehydrate proof and must not weaken these functions implicitly.
bool clean_for_save(const LiveBoundary&) noexcept;
bool clean_for_load(const LiveBoundary&,const SavedBoundary&) noexcept;
struct Controller {
 virtual ~Controller() = default;
 // Called with all guest threads quiescent, before any save capture. A nonnull
 // Hold must exclude native journal/image mutation through the entire capture.
 // Failure or contention must deny promptly; a throwing implementation is
 // caught by the savestate caller and treated as denial.
 virtual std::unique_ptr<Hold> begin_save() = 0;
 // After complete memory capture, under the SAME Hold and global park, before
 // any file write or thaw. Active owners commit their host capture receipt
 // here. Refusal discards the captured payload; it must never be written.
 // Bootstrap-only controllers need no additional capture commit.
 virtual bool finish_save() { return true; }
 // Called after ordinary host/thread layout checks but before memory restore.
 // Validate only snapshot-owned bytes and reconcile the live controller. The
 // Hold must exclude competing native transitions through restore and be
 // released while workers are still parked, immediately before thaw. It must
 // leave native admission quarantined until finish_load succeeds. Success
 // alone does not grant publication or restored game-thread authority.
 virtual std::unique_ptr<Hold> begin_load(const SavedView&) = 0;
 // Explicit per-load rendezvous; default children need no Stage participant.
 virtual std::unique_ptr<Hold> begin_group_load(const SavedView&view,
  std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>,
  std::shared_ptr<story_native_restore_resume::CurrentLoadOwner> = {}){return begin_load(view);}
 // Called under the same Hold after guest memory and mod host state are
 // restored, before saved threads resume. Revalidate/rebuild from the saved
 // view and restored guest state. Failure or exception must leave admission
 // quarantined, including after the Hold is destroyed; never publish partway
 // through a sequence that can still fail. A successful return only means the
 // controller has established a safe restored/deferred state; fresh game-thread
 // and native ownership proofs are still required before publication. The
 // full restore cannot roll back.
 virtual bool finish_load(const SavedView&) = 0;
 // Called on the game thread after portable-load readiness succeeds, before
 // its first guest write. There is no snapshot journal or global quiesce here.
 // Admission must first revoke native publication and quarantine any old
 // image/receipt through the later stage transition, even if applying the
 // portable data fails. The Hold excludes competing transitions only while
 // apply_portable runs; destroying it must not undo that quarantine.
 virtual std::unique_ptr<Hold> begin_progress_load() = 0;
};
// Persistent controller registration is optional while native publication is
// disabled. A future publisher must require successful registration itself.
bool register_controller(Controller*) noexcept;
Controller* controller() noexcept;
std::unique_ptr<Hold> try_begin_save(Controller*) noexcept;
bool try_finish_save(Controller*) noexcept;
std::unique_ptr<Hold> try_begin_load(Controller*, const SavedView&) noexcept;
bool try_finish_load(Controller*, const SavedView&) noexcept;
std::unique_ptr<Hold> try_begin_progress_load(Controller*) noexcept;
} // namespace ss::native_gate
