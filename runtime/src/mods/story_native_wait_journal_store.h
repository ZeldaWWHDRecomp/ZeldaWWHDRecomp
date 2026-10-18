// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_arena.h"
#include "story_native_wait_journal.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>

namespace story_native_wait_session {class Session;}
namespace story_native_wait_journal_store {
using Verify=std::function<bool()>;
using Read=std::function<bool(uint32_t,std::span<uint8_t>)>;
using Write=std::function<bool(uint32_t,std::span<const uint8_t>)>;
struct Backend {Verify allocation,fence;Read read;Write write;};
// Actual guest-memory I/O and allocator receipt. There is intentionally no
// default fence: a future controller must supply a real retained entry fence
// covering this entire transaction and fullstate capture. No current caller
// installs one, so this primitive cannot enable native publication.
Backend guest_backend(const story_native_event_arena::Layout&,Verify fence);

enum class State {Unused,Ready,Quarantined};
class Store {
 story_native_event_arena::Layout layout_;
 Backend backend_;
 story_native_wait_journal::Record record_{};
 std::atomic<State> state_{State::Unused};
 std::atomic<bool> writing_{false};
 mutable std::mutex mutex_;
 friend class story_native_wait_session::Session;
 bool adopt_restored(const story_native_wait_journal::Record&);
 bool adopt_idle_restored(const story_native_wait_journal::Record&);
 bool adopt_pristine_restored(const story_native_wait_journal::Record&);
 bool guarded()const;
 bool read(std::array<uint8_t,story_native_wait_journal::kBytes>&);
 bool persist(const story_native_wait_journal::Record&);
 bool quarantine();
public:
 Store(story_native_event_arena::Layout,Backend);
 Store(const Store&)=delete;Store& operator=(const Store&)=delete;
 State state()const{return state_.load(std::memory_order_acquire);}
 // Host diagnostic only. It does not fence ss::do_save until that routine
 // consults it while holding the same real admission fence.
 bool writing()const{return writing_.load(std::memory_order_acquire);}
 // Initial record is supplied by the real startup generation issuer, never
 // minted here. The 128-byte header must be all zero and the record Unpublished.
 bool persist_initial(const story_native_wait_journal::Record&);
 // Exact previous bytes must still be present. Any attempted write or failed
 // readback latches host quarantine and retains the guest allocation.
 bool persist_transition(story_native_wait_journal::Phase);
 // Storage only, not detached ownership authority. Session requires an opaque
 // Controller certificate before calling this exact-previous-header operation.
 bool persist_rearm(const story_native_wait_journal::Record& previous,uint32_t generation);
 // This record is only a storage receipt, not a retained claim or restore token.
 std::optional<story_native_wait_journal::Record> receipt()const{
  std::lock_guard lock(mutex_);
  return state()==State::Ready?std::optional(record_):std::nullopt;
 }
};
}
