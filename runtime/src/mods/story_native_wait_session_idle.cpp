// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_session.h"
#include "story_native_wait_run_footprint.h"
#include "native_quiescence.h"
#include "story_native_restore_current_owner.h"
#include "runtime.h"
#include "story_native_wait_snapshot_roots.h"
#include "story_native_wait_snapshot_actor_roots.h"
#include "guest_addr.h"
#include <algorithm>
#include <cstring>
#include <thread>
namespace story_native_wait_session {
namespace F=story_native_wait_run_footprint;
struct IdleCheckpoint::Data {
 const Session*owner=nullptr;std::shared_ptr<const uint8_t>host_identity;
 std::shared_ptr<const void>lineage;Session::Layout layout;Session::Record journal;
 Session::Expected expected;F::Footprint footprint;bool committed=false;
};
struct IdleCapture::Pending {
 Session*owner=nullptr;std::shared_ptr<IdleCheckpoint::Data>data;
 std::unique_ptr<Session::Fence>fence;threads::QuiescenceWitness parked;Cpu*cpu=nullptr;
 std::thread::id thread=std::this_thread::get_id();bool consumed=false;
};
struct IdleRestore::Pending {
 Session*owner=nullptr;std::shared_ptr<const IdleCheckpoint::Data>data;
 std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>current_owner;
 std::unique_ptr<Session::Fence>fence;threads::QuiescenceWitness parked;Cpu*cpu=nullptr;
 std::thread::id thread=std::this_thread::get_id();uint64_t epoch=0;bool consumed=false;
};
IdleCheckpoint::IdleCheckpoint(std::shared_ptr<const Data>d):data_(std::move(d)){}
const story_native_wait_journal::Record&IdleCheckpoint::journal()const noexcept{return data_->journal;}
bool IdleCheckpoint::matches_saved(const ss::native_gate::SavedView&view)const noexcept{
 return data_&&data_->committed&&F::matches_saved(data_->layout,view,data_->footprint)&&story_native_wait_snapshot_actor_roots::no_borrow_saved(view,data_->layout);
}
IdleCapture::IdleCapture(std::unique_ptr<Pending>p):pending_(std::move(p)){}
void IdleCapture::release()noexcept{if(pending_&&pending_->fence&&pending_->thread!=std::this_thread::get_id())std::terminate();pending_.reset();}
IdleCapture::~IdleCapture(){release();}
IdleCapture::IdleCapture(IdleCapture&&o)noexcept{if(o.pending_&&o.pending_->fence&&o.pending_->thread!=std::this_thread::get_id())std::terminate();pending_=std::move(o.pending_);}
IdleCapture&IdleCapture::operator=(IdleCapture&&o)noexcept{if(this!=&o){if(o.pending_&&o.pending_->fence&&o.pending_->thread!=std::this_thread::get_id())std::terminate();release();pending_=std::move(o.pending_);}return *this;}
IdleRestore::IdleRestore(std::unique_ptr<Pending>p):pending_(std::move(p)){}
void IdleRestore::release()noexcept{
 if(pending_&&pending_->thread!=std::this_thread::get_id())std::terminate();
 if(pending_&&!pending_->consumed&&pending_->owner){auto&s=*pending_->owner;s.quarantined_=true;s.reusable_=false;s.expected_.reset();s.canonical_image_.clear();}
 pending_.reset();
}
IdleRestore::~IdleRestore(){release();}
IdleRestore::IdleRestore(IdleRestore&&o)noexcept{if(o.pending_&&o.pending_->thread!=std::this_thread::get_id())std::terminate();pending_=std::move(o.pending_);}
IdleRestore&IdleRestore::operator=(IdleRestore&&o)noexcept{if(this!=&o){if(o.pending_&&o.pending_->thread!=std::this_thread::get_id())std::terminate();release();pending_=std::move(o.pending_);}return *this;}
bool Session::idle_shape()const{
 if(!fence_held()||!idle_lineage_||idle_lineage_->owner!=this||idle_lineage_->host_identity!=host_identity_||
    !reusable_||!native_mutation_||unexposed_||publication_sealed_||restore_pending_||quarantined_)return false;
 const auto record=journal();return record&&*record==idle_lineage_->journal&&record->generation==generation_&&
  record->phase==story_native_wait_journal::Phase::Unpublished&&record->definition==readiness_.definition_hash()&&
  record->arena==readiness_.arena().allocation.address&&story_native_event_arena::owns(readiness_.arena(),mem::runtime_alloc_log(),mem::runtime_top());
}
bool Session::idle_read(uint32_t at,std::span<uint8_t>out)const{
 if(!fence_held()||out.empty()||uint64_t(at)+out.size()>0x100000000ull)return false;
 const auto layout=readiness_.arena();const uint64_t begin=layout.allocation.address,end=begin+layout.allocation.bytes;
 // Normalized footprints can join adjacent scratch/arena spans. Split every
 // read at the actual allocation boundaries; no source callback supplies arena bytes.
 size_t done=0;while(done<out.size()){
  const uint64_t address=uint64_t(at)+done;size_t count=out.size()-done;
  if(address>=begin&&address<end){count=std::min(count,size_t(end-address));
   if(!story_native_event_arena::owns(layout,mem::runtime_alloc_log(),mem::runtime_top()))return false;
   std::memcpy(out.data()+done,mem::ptr(uint32_t(address)),count);
  }else{if(address<begin)count=std::min(count,size_t(begin-address));
   if(!idle_sources_.read||!idle_sources_.read(uint32_t(address),out.subspan(done,count)))return false;}
  done+=count;
 }
 return true;
}
bool Session::snapshot_roots(F::Footprint&out)const{
 auto read=[this](uint32_t at,std::span<uint8_t>b){return idle_read(at,b);};
 const auto&request=root_request_;if(request.declared&&!request.declared->current())return false;
 const auto&mod=request.declared?request.declared->mod():request.mod;
 const auto&ids=request.declared?request.declared->compiled().actor_ids():request.residents;
 F::Footprint roots;if(!story_native_wait_snapshot_roots::capture(read,arena(),roots))return false;
 for(auto&b:roots)out.push_back(std::move(b));
 return story_native_wait_snapshot_actor_roots::append(read,arena(),mod,ids,out);
}
bool Session::clean_rearm_roots()const{
 if(!fence_held()||quarantined_||restore_pending_)return false;
 F::Footprint before,after;
 auto read=[this](uint32_t at,std::span<uint8_t>b){return idle_read(at,b);};
 // Both passes resolve real placement/list identity and exclude all native
 // People cut borrowers. Matching bytes alone cannot establish that census.
 return snapshot_roots(before)&&F::matches_live(read,before)&&
  snapshot_roots(after)&&after==before&&F::matches_live(read,after)&&
  fence_held()&&!quarantined_&&!restore_pending_;
}
std::optional<Session::Expected>Session::idle_inspect(Cpu*c,const threads::QuiescenceWitness&parked){
 if(!c||!parked.current(c)||!idle_shape())return {};
 // The immutable Expected was captured by genuine rearm. Current ownership is
 // checked independently through native roots, so another arena may be active.
 const auto e=idle_lineage_->expected;F::Footprint roots;
 if(e.identity.generation!=generation_||(e.identity.image!=arena().wait.image.address&&e.identity.image!=arena().turn.image.address)||
    !snapshot_roots(roots)||!idle_shape()||!parked.current(c))return {};
 return e;
}
std::optional<IdleCapture>Session::begin_idle_capture(Cpu*c,const threads::QuiescenceWitness&parked){try{
 if(!c||!parked.current(c))return {};auto fence=try_fence();if(!fence)return {};
 const auto expected=idle_inspect(c,parked);if(!expected)return {};
 auto p=std::make_unique<IdleCapture::Pending>();p->owner=this;p->cpu=c;p->parked=parked;p->fence=std::move(fence);
 p->data=std::make_shared<IdleCheckpoint::Data>();auto&d=*p->data;d.owner=this;d.host_identity=host_identity_;d.lineage=idle_lineage_;d.layout=readiness_.arena();d.journal=*journal();d.expected=*expected;
 d.footprint.push_back({d.layout.allocation.address,std::vector<uint8_t>(d.layout.allocation.bytes)});
 if(!idle_read(d.layout.allocation.address,d.footprint.back().bytes)||!snapshot_roots(d.footprint))return {};
 auto read=[this](uint32_t at,std::span<uint8_t>b){return idle_read(at,b);};
 const auto after=idle_inspect(c,parked);
 if(!after||*after!=*expected||!F::matches_live(read,d.footprint)||!idle_shape()||!parked.current(c))return {};
 return IdleCapture(std::move(p));
 }catch(...){return {};}}
std::optional<IdleCheckpoint>Session::commit_idle_capture(IdleCapture&&capture,Cpu*c,const threads::QuiescenceWitness&parked,std::unique_ptr<Fence>*retain_fence){
 if(retain_fence&&*retain_fence)return {};auto*p=capture.pending_.get();
 if(!p||p->thread!=std::this_thread::get_id()||p->consumed)return {};p->consumed=true;
 bool valid=false;try{auto read=[this](uint32_t at,std::span<uint8_t>b){return idle_read(at,b);};
  const auto fresh=p->owner==this&&p->cpu==c&&p->fence&&p->parked.current(c)&&parked.current(c)?idle_inspect(c,parked):std::nullopt;
  valid=fresh&&p->data&&p->data->owner==this&&p->data->host_identity==host_identity_&&p->data->lineage==idle_lineage_&&
   p->data->journal==journal()&&*fresh==p->data->expected&&F::matches_live(read,p->data->footprint)&&parked.current(c);
 }catch(...){valid=false;}
 if(retain_fence)*retain_fence=std::move(p->fence);
 if(!valid){capture.release();return {};}
 p->data->committed=true;IdleCheckpoint result(p->data);capture.release();return result;
}
std::optional<IdleRestore>Session::prepare_idle_restore(const IdleCheckpoint&checkpoint,const ss::native_gate::SavedView&view,Cpu*c,const threads::QuiescenceWitness&parked,uint64_t epoch,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>current_owner){try{
 if(!c||!epoch||epoch<=idle_restore_epoch_||!parked.current(c)||!checkpoint.data_||!checkpoint.matches_saved(view)||
    (current_owner&&!current_owner->current(c,parked,view,epoch)))return {};
 const bool borrow=current_owner&&current_owner->holds_session(*this);
 std::unique_ptr<Fence>fence;if(!borrow){fence=try_fence();if(!fence)return {};}
 if(!fence_held())return {};
 const auto&d=*checkpoint.data_;
 if(!d.committed||!d.lineage||d.owner!=this||d.host_identity!=host_identity_||d.layout!=readiness_.arena()||d.journal.definition!=readiness_.definition_hash()||
    d.journal.phase!=story_native_wait_journal::Phase::Unpublished||!checkpoint.matches_saved(view))return {};
 // The actual current owner's independently captured native claim may be
 // RUNNING here. Its opaque proof, never idle journal bytes, permits replacing
 // that same Session. Other Sessions still need their genuine current-clean
 // lineage and bounded roots before any snapshot write.
 if(!borrow&&!idle_inspect(c,parked))return {};
 if(!parked.current(c)||(current_owner&&!current_owner->current(c,parked,view,epoch)))return {};
 auto p=std::make_unique<IdleRestore::Pending>();p->owner=this;p->data=checkpoint.data_;p->fence=std::move(fence);p->parked=parked;p->cpu=c;p->epoch=epoch;p->current_owner=std::move(current_owner);
 idle_restore_epoch_=idle_pending_epoch_=epoch;restore_pending_=true;reusable_=false;expected_.reset();canonical_image_.clear();return IdleRestore(std::move(p));
 }catch(...){return {};}}
bool Session::adopt_idle(IdleRestore&&restore,const ss::native_gate::SavedView&view,Cpu*c,const threads::QuiescenceWitness&parked,std::unique_ptr<Fence>*retain_fence){
 if(retain_fence&&*retain_fence)return false;auto*p=restore.pending_.get();if(!p||p->thread!=std::this_thread::get_id()||p->consumed)return false;p->consumed=true;
 bool valid=false;try{
  const auto d=p->data;auto read=[this](uint32_t at,std::span<uint8_t>b){return idle_read(at,b);};
  if(p->owner==this&&p->cpu==c&&fence_held()&&(p->fence||(p->current_owner&&p->current_owner->holds_session(*this)))&&p->parked.current(c)&&parked.current(c)&&(!p->current_owner||p->current_owner->current(c,parked,view,p->epoch))&&d&&d->owner==this&&d->host_identity==host_identity_&&
     restore_pending_&&idle_pending_epoch_==p->epoch&&d->layout==readiness_.arena()&&d->committed&&
     F::matches_saved(d->layout,view,d->footprint)&&story_native_wait_snapshot_actor_roots::no_borrow_saved(view,d->layout)&&story_native_wait_snapshot_actor_roots::no_borrow(read,d->layout)&&F::matches_live(read,d->footprint)&&story_native_event_arena::owns(d->layout,mem::runtime_alloc_log(),mem::runtime_top())){
   auto store=std::make_unique<story_native_wait_journal_store::Store>(d->layout,journal_backend_);
   if(store->adopt_idle_restored(d->journal)&&parked.current(c)&&F::matches_live(read,d->footprint)&&(!p->current_owner||p->current_owner->current(c,parked,view,p->epoch))){
    journal_store_=std::move(store);generation_=uint32_t(d->journal.generation);idle_lineage_=std::static_pointer_cast<const IdleLineage>(d->lineage);
    staged_expected_.reset();staged_image_.clear();staged_record_.clear();staged_depth_.reset();staged_pose_bytes_.clear();original_poses_.reset();adopted_baseline_.reset();
    expected_.reset();canonical_image_.clear();publication_sealed_=false;native_mutation_=true;unexposed_=false;reusable_=true;restore_pending_=false;quarantined_=false;idle_pending_epoch_=0;
    valid=idle_shape()&&parked.current(c)&&F::matches_live(read,d->footprint)&&(!p->current_owner||p->current_owner->current(c,parked,view,p->epoch));
   }
  }
 }catch(...){valid=false;}
 if(!valid){quarantined_=true;reusable_=false;expected_.reset();canonical_image_.clear();}
 if(retain_fence)*retain_fence=std::move(p->fence);restore.release();return valid;
}
}

namespace story_native_wait_journal_store {
bool Store::adopt_idle_restored(const story_native_wait_journal::Record&saved){
 std::lock_guard lock(mutex_);if(state()!=State::Unused||writing()||!guarded())return false;
 std::array<uint8_t,story_native_wait_journal::kBytes>bytes{};story_native_wait_journal::Record actual;
 if(saved.phase!=story_native_wait_journal::Phase::Unpublished||!read(bytes)||!story_native_wait_journal::decode(bytes,actual)||actual!=saved||saved.arena!=layout_.allocation.address)return false;
 record_=saved;state_.store(State::Ready,std::memory_order_release);return true;
}
}
