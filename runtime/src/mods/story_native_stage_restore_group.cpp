// SPDX-License-Identifier: MPL-2.0
#include "story_native_stage_restore_group.h"
#include "savestate_native_gate.h"
#include <thread>
#include <utility>
namespace story_native_stage_restore_group {
namespace O=story_native_stage_owner_observer;
struct StageRestoreGroup::State {
 std::thread::id thread=std::this_thread::get_id();Cpu*cpu=nullptr;
 threads::QuiescenceWitness parked;O::Snapshot original{},target{};
 const uint8_t*alloc_begin=nullptr,*alloc_end=nullptr;
 const ss::MemoryRegion*regions=nullptr;size_t region_count=0;
 const ss::MemoryChunks*chunks=nullptr;
 std::optional<O::RestorePermit>permit;
 std::optional<story_native_stage_owner::Tracker::Scan>pin;
 std::optional<O::Snapshot>finished;bool failed=false;
 bool same_view(const ss::native_gate::SavedView&v)const{return v.allocations.ok&&alloc_begin==v.allocations.p&&alloc_end==v.allocations.e&&regions==v.regions.data()&&region_count==v.regions.size()&&chunks==&v.chunks;}
 bool live(Cpu*c,const threads::QuiescenceWitness&w)const{return !failed&&thread==std::this_thread::get_id()&&c&&c==cpu&&parked.current(c)&&w.current(c);}
 void close()noexcept{if(thread!=std::this_thread::get_id())std::terminate();failed=true;pin.reset();permit.reset();}
 ~State(){if((pin||permit)&&thread!=std::this_thread::get_id())std::terminate();}
};
StageRestoreGroup::StageRestoreGroup():state_(std::make_shared<State>()){close_=[](StageRestoreGroup&g)noexcept{g.close_impl();};}
std::shared_ptr<StageRestoreGroup>StageRestoreGroup::create(){return std::shared_ptr<StageRestoreGroup>(new StageRestoreGroup);}
StageRestoreGroup::~StageRestoreGroup(){close();}
void StageRestoreGroup::close_impl()noexcept{if(state_)state_->close();}
StageRestoreGroup::Participant::Participant(std::shared_ptr<State>s):state_(std::move(s)){}
StageRestoreGroup::Participant::Participant(Participant&&o)noexcept:state_(std::move(o.state_)),consumed_(o.consumed_){o.consumed_=true;}
StageRestoreGroup::Participant&StageRestoreGroup::Participant::operator=(Participant&&o)noexcept{if(this!=&o){if(state_&&!consumed_)invalidate();state_=std::move(o.state_);consumed_=o.consumed_;o.consumed_=true;}return *this;}
StageRestoreGroup::Participant::~Participant(){if(state_&&!consumed_)invalidate();}
void StageRestoreGroup::Participant::invalidate()noexcept{if(state_)state_->close();consumed_=true;}
std::optional<StageRestoreGroup::Participant>StageRestoreGroup::join(const O::Snapshot&saved,const ss::native_gate::SavedView&v,Cpu*c,const threads::QuiescenceWitness&w){try{
 auto&s=*state_;if(s.thread!=std::this_thread::get_id())return {};
 struct Deny {State&s;bool success=false;~Deny(){if(!success)s.close();}}deny{s};
 if(s.failed||s.finished||!c||!w.current(c)||!v.allocations.ok)return {};
 if(s.cpu&&(!s.live(c,w)||!s.same_view(v)||saved.control!=s.target.control||saved.receipt.owner!=s.target.receipt.owner))return {};
 auto pin=O::tracker().try_scan();if(!pin.current())return {};const auto current=O::snapshot(pin);if(!current)return {};
 if(s.cpu&&current->receipt!=s.original.receipt)return {};
 // Every participant independently validates its actual saved resource graph.
 // Extra permits only validate and unlock; they never issue a new receipt.
 auto permit=O::prepare_restore_same_resource(saved,v);if(!permit||!w.current(c))return {};
 if(!s.cpu){s.cpu=c;s.parked=w;s.original=*current;s.target=saved;s.alloc_begin=v.allocations.p;s.alloc_end=v.allocations.e;s.regions=v.regions.data();s.region_count=v.regions.size();s.chunks=&v.chunks;s.permit.emplace(std::move(*permit));}
 deny.success=true;return Participant(state_);
 }catch(...){return {};}}
std::optional<O::Snapshot>StageRestoreGroup::Participant::finish(const ss::native_gate::SavedView&v,Cpu*c,const threads::QuiescenceWitness&w){
 if(!state_||consumed_||state_->thread!=std::this_thread::get_id())return {};consumed_=true;
 try{auto&s=*state_;if(!s.live(c,w)||!s.same_view(v)){invalidate();return {};}
  if(!s.finished){if(!s.permit){invalidate();return {};}
   auto restored=O::finish_restore(std::move(*s.permit));s.permit.reset();
   if(!restored||restored->control!=s.target.control||restored->receipt.owner!=s.target.receipt.owner||!s.live(c,w)){invalidate();return {};}
   auto pin=O::tracker().try_scan();if(!pin.current()){invalidate();return {};}
   const auto current=O::snapshot(pin);if(!current||current->control!=restored->control||current->receipt!=restored->receipt){invalidate();return {};}
   s.pin.emplace(std::move(pin));s.finished=*restored;
  }
  const auto current=s.pin?O::snapshot(*s.pin):std::nullopt;
  if(!current||current->control!=s.finished->control||current->receipt!=s.finished->receipt||!s.live(c,w)){invalidate();return {};}
  return s.finished;
 }catch(...){invalidate();return {};}
}
}
