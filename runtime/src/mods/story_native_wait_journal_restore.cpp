// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_journal_store.h"
namespace story_native_wait_journal_store {
bool Store::adopt_restored(const story_native_wait_journal::Record&saved){
 std::lock_guard lock(mutex_);if(state()!=State::Unused||writing()||!guarded())return false;
 std::array<uint8_t,story_native_wait_journal::kBytes>bytes{};story_native_wait_journal::Record actual;
 if(!read(bytes)||!story_native_wait_journal::decode(bytes,actual)||actual!=saved||saved.arena!=layout_.allocation.address||
  (saved.phase!=story_native_wait_journal::Phase::Published&&saved.phase!=story_native_wait_journal::Phase::Running))return false;
 record_=saved;state_.store(State::Ready,std::memory_order_release);return true;
}
}
