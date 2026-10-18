// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_journal.h"
#include "savestate_native_gate.h"
#include <memory>
namespace story_native_wait_session {
class Session;
class IdleCapture;
class IdleRestore;
// Immutable SAME-PROCESS provenance from genuine completed release/rearm and
// fresh native-clean observations. Saved Unpublished bytes cannot mint this.
class IdleCheckpoint final {
 struct Data;std::shared_ptr<const Data>data_;
 explicit IdleCheckpoint(std::shared_ptr<const Data>);
 friend class Session;friend class IdleCapture;friend class IdleRestore;
public:
 const story_native_wait_journal::Record&journal()const noexcept;
 bool matches_saved(const ss::native_gate::SavedView&)const noexcept;
};
// Owns the actual Session entry fence through capture/commit. No registry or
// Stage mutex is retained while the real thread serializer runs.
class IdleCapture final {
 struct Pending;std::unique_ptr<Pending>pending_;
 explicit IdleCapture(std::unique_ptr<Pending>);
 void release()noexcept;friend class Session;
public:
 IdleCapture(const IdleCapture&)=delete;IdleCapture&operator=(const IdleCapture&)=delete;
 IdleCapture(IdleCapture&&)noexcept;IdleCapture&operator=(IdleCapture&&)noexcept;
 ~IdleCapture();
};
// A retained current-clean proof, not restored native admission. A different
// active owner requires a separate authenticated current-run transfer path.
class IdleRestore final {
 struct Pending;std::unique_ptr<Pending>pending_;
 explicit IdleRestore(std::unique_ptr<Pending>);
 void release()noexcept;friend class Session;
public:
 IdleRestore(const IdleRestore&)=delete;IdleRestore&operator=(const IdleRestore&)=delete;
 IdleRestore(IdleRestore&&)noexcept;IdleRestore&operator=(IdleRestore&&)noexcept;
 ~IdleRestore();
};
}
