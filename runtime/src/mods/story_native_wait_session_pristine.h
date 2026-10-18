// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_journal.h"
#include "savestate_native_gate.h"
#include <memory>
namespace story_native_wait_session {
class Session;
// Immutable SAME-PROCESS save provenance for an arena genuinely never exposed
// at capture. Matching saved bootstrap bytes alone cannot mint this receipt.
class PristineCheckpoint final {
 struct Data;std::shared_ptr<const Data>data_;
 explicit PristineCheckpoint(std::shared_ptr<const Data>);friend class Session;friend class PristineCapture;friend class PristineRestore;
public:
 const story_native_wait_journal::Record&journal()const noexcept;
 bool matches_saved(const ss::native_gate::SavedView&)const noexcept;
};
// Borrows the caller's actual bootstrap Hold fence. Commit requires that exact
// unbroken Fence identity and original freeze; releasing/reacquiring denies.
class PristineCapture final {
 struct Pending;std::unique_ptr<Pending>pending_;
 explicit PristineCapture(std::unique_ptr<Pending>);friend class Session;
public:
 PristineCapture(const PristineCapture&)=delete;PristineCapture&operator=(const PristineCapture&)=delete;
 PristineCapture(PristineCapture&&)noexcept;PristineCapture&operator=(PristineCapture&&)noexcept;
 ~PristineCapture();
};
// Retains either an actual own Session fence or the authenticated coordinator's
// original current-owner fence. Admission stays revoked until exact adoption.
class PristineRestore final {
 struct Pending;std::unique_ptr<Pending>pending_;
 explicit PristineRestore(std::unique_ptr<Pending>);void release()noexcept;friend class Session;
public:
 PristineRestore(const PristineRestore&)=delete;PristineRestore&operator=(const PristineRestore&)=delete;
 PristineRestore(PristineRestore&&)noexcept;PristineRestore&operator=(PristineRestore&&)noexcept;
 ~PristineRestore();
};

}
