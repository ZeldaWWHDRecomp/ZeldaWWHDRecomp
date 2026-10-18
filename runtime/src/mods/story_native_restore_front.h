// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <atomic>
#include <thread>
#include <utility>
#include <array>
#include <memory>
#include <optional>
#include <string>
namespace story_native_wait_runtime_controller {class Context;}
namespace story_native_event_depth {class Tracker;}
namespace story_native_wait_runtime {class NativeResumeFrontIssuer;struct DeclaredSlot;}
namespace story_native_wait_session {class Session;}
namespace story_native_scene_playback {class Activation;}
namespace story_native_restore_front {
// Metadata is data only. Only the actual runtime issuer can bind these fields
// to a final Front capability and the actual process-wide selection storage.
struct Selection {
 story_native_wait_session::Session*session=nullptr;
 story_native_wait_runtime::DeclaredSlot*declared=nullptr;
 std::shared_ptr<const story_native_scene_playback::Activation>activation;
 bool frontend=false;std::string mod;std::array<std::string,2>ids;
 bool operator==(const Selection&)const=default;
 void swap(Selection&other)noexcept{
  std::swap(session,other.session);std::swap(declared,other.declared);
  activation.swap(other.activation);std::swap(frontend,other.frontend);
  mod.swap(other.mod);for(unsigned i=0;i<2;++i)ids[i].swap(other.ids[i]);
 }
};
// Actual runtime atomic slots only. This host capability cannot issue a lease,
// validate restored bytes, or dispatch native code. It participates only in
// Context's final physical-Q protected, callback-free resume transaction.
class Front final {
 using Context=story_native_wait_runtime_controller::Context;
 using Tracker=story_native_event_depth::Tracker;
 std::atomic<Context*>*context_=nullptr;std::atomic<Tracker*>*depth_=nullptr;
 Context*old_context_=nullptr;Tracker*old_depth_=nullptr;
 Selection*selection_=nullptr;std::optional<Selection>old_selection_,replacement_;
 std::thread::id thread_;bool consumed_=false;
 Front(std::atomic<Context*>&context,std::atomic<Tracker*>&depth):context_(&context),depth_(&depth),old_context_(context.load(std::memory_order_acquire)),old_depth_(depth.load(std::memory_order_acquire)),thread_(std::this_thread::get_id()){}
 Front(std::atomic<Context*>&context,std::atomic<Tracker*>&depth,Selection&selection,Selection replacement)
  :Front(context,depth){selection_=&selection;old_selection_=selection;replacement_=std::move(replacement);}
 bool current()const noexcept{return !consumed_&&thread_==std::this_thread::get_id()&&context_&&depth_&&context_->load(std::memory_order_acquire)==old_context_&&depth_->load(std::memory_order_acquire)==old_depth_&&(!selection_||(old_selection_&&replacement_&&*selection_==*old_selection_));}
 // Caller has already validated every participant and holds physical Q. No
 // callbacks, allocation, guest/native access, or potentially failing checks.
 void commit_unchecked(Context*context,Tracker*depth)noexcept{consumed_=true;if(selection_)selection_->swap(*replacement_);depth_->store(depth,std::memory_order_release);context_->store(context,std::memory_order_release);}
 friend class story_native_wait_runtime::NativeResumeFrontIssuer;
 friend class story_native_wait_runtime_controller::Context;
public:
 Front(const Front&)=delete;Front&operator=(const Front&)=delete;
 Front(Front&&other)noexcept:context_(other.context_),depth_(other.depth_),old_context_(other.old_context_),old_depth_(other.old_depth_),selection_(other.selection_),old_selection_(std::move(other.old_selection_)),replacement_(std::move(other.replacement_)),thread_(other.thread_),consumed_(std::exchange(other.consumed_,true)){}
 Front&operator=(Front&&other)noexcept{if(this!=&other){context_=other.context_;depth_=other.depth_;old_context_=other.old_context_;old_depth_=other.old_depth_;selection_=other.selection_;old_selection_=std::move(other.old_selection_);replacement_=std::move(other.replacement_);thread_=other.thread_;consumed_=std::exchange(other.consumed_,true);}return *this;}
};
}
