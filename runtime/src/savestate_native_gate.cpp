// SPDX-License-Identifier: MPL-2.0
#include "savestate_native_gate.h"
#include <algorithm>
namespace ss::native_gate { namespace {
std::atomic<Controller*> active{nullptr};
} // namespace
bool register_controller(Controller* candidate) noexcept {
 if (!candidate) return false;
 Controller* empty=nullptr;
 return active.compare_exchange_strong(empty,candidate,std::memory_order_release,
                                       std::memory_order_relaxed);
}
Controller* controller() noexcept {return active.load(std::memory_order_acquire);}
bool clean_for_save(const LiveBoundary& s) noexcept {
 const auto& j=s.journal;
 const bool defined=std::any_of(j.definition.begin(),j.definition.end(),[](uint8_t b){return b!=0;});
 return s.exact_allocation&&s.journal_readback&&s.fence_held&&
        s.manager_unpublished&&s.consumers_detached&&s.checkpoint_clean&&
        s.callback_depth==0&&j.generation&&j.serial&&j.arena&&defined&&
        j.phase==story_native_wait_journal::Phase::Unpublished&&
        (s.lease==story_native_event_lease::Phase::Empty||
         s.lease==story_native_event_lease::Phase::Retired);
}
bool clean_for_load(const LiveBoundary& live,const SavedBoundary& saved) noexcept {
 return clean_for_save(live)&&saved.snapshot_owned&&saved.exact_allocation&&
        saved.manager_unpublished&&saved.consumers_detached&&saved.depth_zero&&
        saved.journal.phase==story_native_wait_journal::Phase::Unpublished&&
        saved.journal.serial&&
        saved.journal.arena==live.journal.arena&&
        saved.journal.generation==live.journal.generation&&
        saved.journal.definition==live.journal.definition;
}
std::unique_ptr<Hold> try_begin_save(Controller* gate) noexcept {
 if(!gate)return {};
 try{return gate->begin_save();}catch(...){return {};}
}
bool try_finish_save(Controller* gate) noexcept {
 if(!gate)return false;
 try{return gate->finish_save();}catch(...){return false;}
}
std::unique_ptr<Hold> try_begin_load(Controller* gate,const SavedView& view) noexcept {
 if(!gate)return {};
 try{return gate->begin_load(view);}catch(...){return {};}
}
bool try_finish_load(Controller* gate,const SavedView& view) noexcept {
 if(!gate)return false;
 try{return gate->finish_load(view);}catch(...){return false;}
}
std::unique_ptr<Hold> try_begin_progress_load(Controller* gate) noexcept {
 if(!gate)return {};
 try{return gate->begin_progress_load();}catch(...){return {};}
}
} // namespace ss::native_gate
