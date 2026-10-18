// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "savestate_native_gate.h"
#include <thread>
namespace ss::native_gate {
class Aggregate;
// Synchronous evidence that ALL aggregate children finished this exact load.
// It grants no native entry, restored model, or publication authority. Only
// Aggregate can issue it, while every child Hold and actual owner fence remain.
// The callback must not retain a reference to this token or its SavedView.
class FinishedLoad final {
 std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>owner_;
 SavedView view_;
 std::thread::id thread_=std::this_thread::get_id();bool active_=true;
 FinishedLoad(std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>owner,const SavedView&view)
  :owner_(std::move(owner)),view_(view){}
 void invalidate()noexcept{active_=false;}
 friend class Aggregate;
public:
 FinishedLoad(const FinishedLoad&)=delete;FinishedLoad&operator=(const FinishedLoad&)=delete;
 FinishedLoad(FinishedLoad&&)=delete;FinishedLoad&operator=(FinishedLoad&&)=delete;
 bool current()const noexcept{return thread_==std::this_thread::get_id()&&active_&&owner_&&view_.allocations.ok;}
 const std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>&owner()const noexcept{return owner_;}
 const SavedView&view()const noexcept{return view_;}
};
}
