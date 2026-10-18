// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "native_quiescence.h"
#include <memory>
#include <cstdint>
namespace ss::native_gate {struct SavedView;}
namespace story_native_wait_session {class Session;}
namespace story_native_wait_runtime_controller {class Context;}
namespace story_native_restore_resume {
class CurrentOwnerRevocation;
// One real aggregate LOAD attempt. Only actual Context can issue this after
// authenticating the real process front and revoking its current owner. Saved
// Origin/checkpoints remain independent. The owner Session fence is retained
// until the coordinator drops ALL child Holds, then explicitly closes this.
class CurrentLoadOwner final {
 std::shared_ptr<void>state_;
 bool(*current_)(const void*,Cpu*,const threads::QuiescenceWitness&,const ss::native_gate::SavedView&,uint64_t)noexcept;
 bool(*holds_)(const void*,const story_native_wait_session::Session&)noexcept;
 void(*close_)(void*)noexcept;
 bool(*mark_active_)(void*)noexcept;bool(*has_active_)(const void*)noexcept;
 uint64_t epoch_;
 std::shared_ptr<const CurrentOwnerRevocation>revocation_;
 CurrentLoadOwner(std::shared_ptr<void>state,decltype(current_)current,decltype(holds_)holds,decltype(close_)close,decltype(mark_active_)mark_active,decltype(has_active_)has_active,uint64_t epoch,std::shared_ptr<const CurrentOwnerRevocation>revocation)
  :state_(std::move(state)),current_(current),holds_(holds),close_(close),mark_active_(mark_active),has_active_(has_active),epoch_(epoch),revocation_(std::move(revocation)){}
 friend class story_native_wait_runtime_controller::Context;
public:
 CurrentLoadOwner(const CurrentLoadOwner&)=delete;CurrentLoadOwner&operator=(const CurrentLoadOwner&)=delete;
 bool current(Cpu*c,const threads::QuiescenceWitness&w,const ss::native_gate::SavedView&v,uint64_t epoch)const noexcept{return state_&&current_&&current_(state_.get(),c,w,v,epoch);}
 bool holds_session(const story_native_wait_session::Session&s)const noexcept{return state_&&holds_&&holds_(state_.get(),s);}
 uint64_t epoch()const noexcept{return epoch_;}
 // No restored entry authority. Closure only releases the actual owner fence.
 void close()noexcept{if(state_&&close_)close_(state_.get());}
 ~CurrentLoadOwner(){close();}
};
}
