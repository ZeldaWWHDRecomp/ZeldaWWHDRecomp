// SPDX-License-Identifier: MPL-2.0
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>
#include "story_native_wait_session.h"
#include "story_native_wait_run_footprint.h"
#include "native_quiescence.h"
#include "runtime.h"
#include "story_native_restore_current_owner.h"
#include "story_native_wait_snapshot_roots.h"
#include "story_native_wait_snapshot_actor_roots.h"
namespace story_native_wait_session {
namespace F=story_native_wait_run_footprint;
struct PristineCheckpoint::Data {
 const Session*owner=nullptr;std::shared_ptr<const uint8_t>host_identity;
 Session::Layout layout;Session::Record journal;F::Footprint footprint;bool committed=false;
};
struct PristineCapture::Pending {
 Session*owner=nullptr;Cpu*cpu=nullptr;std::thread::id thread=std::this_thread::get_id();
 threads::QuiescenceWitness parked;std::shared_ptr<const uint8_t>fence;
 std::shared_ptr<PristineCheckpoint::Data>data;bool consumed=false;
};
PristineCheckpoint::PristineCheckpoint(std::shared_ptr<const Data>d):data_(std::move(d)){}
const story_native_wait_journal::Record&PristineCheckpoint::journal()const noexcept{return data_->journal;}
bool PristineCheckpoint::matches_saved(const ss::native_gate::SavedView&v)const noexcept{return data_&&data_->committed&&F::matches_saved(data_->layout,v,data_->footprint)&&story_native_wait_snapshot_actor_roots::no_borrow_saved(v,data_->layout);}
PristineCapture::PristineCapture(std::unique_ptr<Pending>p):pending_(std::move(p)){}
PristineCapture::PristineCapture(PristineCapture&&)noexcept=default;
PristineCapture&PristineCapture::operator=(PristineCapture&&)noexcept=default;
PristineCapture::~PristineCapture()=default;
std::shared_ptr<const uint8_t>Session::current_fence_identity()const noexcept{
 for(auto*f=active_fence_;f;f=f->prior_)if(f->owner_==this&&f->lock_.owns_lock())return f->identity_;
 return {};
}
bool Session::pristine_shape()const{
 if(!fence_held()||publication_sealed_||restore_pending_||quarantined_)return false;
 if(!native_mutation_&&!reusable_&&unexposed_)return unexposed();
 if(!native_mutation_||!reusable_||unexposed_||!pristine_lineage_)return false;
 const auto d=std::static_pointer_cast<const PristineCheckpoint::Data>(pristine_lineage_);const auto record=journal();const auto l=arena();
 const auto block=d?std::find_if(d->footprint.begin(),d->footprint.end(),[&](const auto&b){return b.address<=l.allocation.address&&uint64_t(b.address)+b.bytes.size()>=uint64_t(l.allocation.address)+l.allocation.bytes;}):F::Footprint::const_iterator{};
 return d&&d->committed&&d->owner==this&&d->host_identity==host_identity_&&d->layout==l&&record&&*record==d->journal&&generation_==record->generation&&record->phase==story_native_wait_journal::Phase::Unpublished&&record->definition==readiness_.definition_hash()&&story_native_event_arena::owns(l,mem::runtime_alloc_log(),mem::runtime_top())&&block!=d->footprint.end()&&!std::memcmp(mem::ptr(l.allocation.address),block->bytes.data()+(l.allocation.address-block->address),l.allocation.bytes);
}
bool Session::preserve_pristine_progress_load(Cpu*c,const threads::QuiescenceWitness&w){
 if(!c||!fence_held()||!w.current(c)||!pristine_shape()||!clean_progress_roots()||!w.current(c))return false;
 expected_.reset();canonical_image_.clear();return true;
}
std::optional<PristineCapture>Session::begin_pristine_capture(Cpu*c,const threads::QuiescenceWitness&w){try{
 if(!c||!w.current(c)||!pristine_shape())return {};
 const auto journal=Session::journal();const auto layout=arena();if(!journal||journal->phase!=story_native_wait_journal::Phase::Unpublished||journal->generation!=generation_||journal->serial!=1||journal->definition!=readiness_.definition_hash())return {};
 auto p=std::make_unique<PristineCapture::Pending>();p->owner=this;p->cpu=c;p->parked=w;p->fence=current_fence_identity();p->data=std::make_shared<PristineCheckpoint::Data>();auto&d=*p->data;
 d.owner=this;d.host_identity=host_identity_;d.layout=layout;d.journal=*journal;
 d.footprint.push_back({layout.allocation.address,std::vector<uint8_t>(layout.allocation.bytes)});
 std::memcpy(d.footprint[0].bytes.data(),mem::ptr(layout.allocation.address),layout.allocation.bytes);
 if(!snapshot_roots(d.footprint))return {};
 auto read=[this](uint32_t at,std::span<uint8_t>b){return pristine_shape()&&idle_read(at,b);};
 if(!F::matches_live(read,d.footprint)||!w.current(c)||!pristine_shape()||p->fence!=current_fence_identity())return {};
 return PristineCapture(std::move(p));
 }catch(...){return {};}}
std::optional<PristineCheckpoint>Session::commit_pristine_capture(PristineCapture&&capture,Cpu*c,const threads::QuiescenceWitness&w){
 auto*p=capture.pending_.get();if(!p||p->consumed||p->thread!=std::this_thread::get_id())return {};p->consumed=true;
 try{if(p->owner!=this||p->cpu!=c||!p->parked.current(c)||!w.current(c)||!p->fence||p->fence!=current_fence_identity()||!pristine_shape()||!p->data)return {};
 auto&d=*p->data;if(d.owner!=this||d.host_identity!=host_identity_||d.layout!=arena()||d.journal!=journal())return {};
 auto read=[this](uint32_t at,std::span<uint8_t>b){return pristine_shape()&&idle_read(at,b);};
 if(!F::matches_live(read,d.footprint)||!story_native_wait_snapshot_actor_roots::no_borrow(read,d.layout)||!w.current(c)||p->fence!=current_fence_identity())return {};
 d.committed=true;return PristineCheckpoint(p->data);
 }catch(...){return {};}
}
struct PristineRestore::Pending {
 Session*owner=nullptr;Cpu*cpu=nullptr;std::thread::id thread=std::this_thread::get_id();
 threads::QuiescenceWitness parked;std::shared_ptr<const uint8_t>fence_identity;
 std::shared_ptr<const PristineCheckpoint::Data>data;
 std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>current_owner;
 std::unique_ptr<Session::Fence>fence;uint64_t epoch=0;bool consumed=false;
};
PristineRestore::PristineRestore(std::unique_ptr<Pending>p):pending_(std::move(p)){}
void PristineRestore::release()noexcept{if(pending_){if(pending_->thread!=std::this_thread::get_id())std::terminate();if(!pending_->consumed&&pending_->owner->fence_held()){auto&s=*pending_->owner;s.quarantined_=true;s.reusable_=false;s.expected_.reset();s.canonical_image_.clear();}}pending_.reset();}
PristineRestore::~PristineRestore(){release();}
PristineRestore::PristineRestore(PristineRestore&&p)noexcept{if(p.pending_&&p.pending_->thread!=std::this_thread::get_id())std::terminate();pending_=std::move(p.pending_);}
PristineRestore&PristineRestore::operator=(PristineRestore&&p)noexcept{if(this!=&p){if(p.pending_&&p.pending_->thread!=std::this_thread::get_id())std::terminate();release();pending_=std::move(p.pending_);}return *this;}
std::optional<PristineRestore>Session::prepare_pristine_restore(const PristineCheckpoint&checkpoint,const ss::native_gate::SavedView&v,Cpu*c,const threads::QuiescenceWitness&w,uint64_t epoch,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>current){try{
 if(!c||!epoch||epoch<=pristine_restore_epoch_||!w.current(c)||!checkpoint.data_||!checkpoint.matches_saved(v)||(current&&!current->current(c,w,v,epoch)))return {};
 auto p=std::make_unique<PristineRestore::Pending>();p->owner=this;p->cpu=c;p->parked=w;p->current_owner=std::move(current);p->data=checkpoint.data_;p->epoch=epoch;
 if(p->current_owner&&p->current_owner->holds_session(*this)){if(!fence_held())return {};}
 else{p->fence=try_fence();if(!p->fence)return {};
 // A genuine completed rearm is a separate current-clean lineage. The saved
 // pristine checkpoint remains immutable; an Unpublished journal alone cannot
 // authorize rewinding a previously exposed Session.
 if(!pristine_shape()){if(!idle_inspect(c,w))return {};}
 else{F::Footprint roots;if(!snapshot_roots(roots))return {};}}
 const auto&d=*p->data;if(!d.committed||d.owner!=this||d.host_identity!=host_identity_||d.layout!=arena()||d.journal.definition!=readiness_.definition_hash()||!d.journal.generation||d.journal.generation>UINT32_MAX||!w.current(c)||(p->current_owner&&!p->current_owner->current(c,w,v,epoch)))return {};
 p->fence_identity=current_fence_identity();if(!p->fence_identity)return {};
 pristine_restore_epoch_=pristine_pending_epoch_=epoch;restore_pending_=true;reusable_=false;expected_.reset();canonical_image_.clear();return PristineRestore(std::move(p));
 }catch(...){return {};}}
bool Session::adopt_pristine(PristineRestore&&restore,const ss::native_gate::SavedView&v,Cpu*c,const threads::QuiescenceWitness&w,std::unique_ptr<Fence>*retain){
 if(retain&&*retain)return false;auto*p=restore.pending_.get();if(!p||p->consumed||p->thread!=std::this_thread::get_id())return false;p->consumed=true;bool ok=false;
 try{const auto d=p->data;const auto current=[&]{return p->owner==this&&p->cpu==c&&p->parked.current(c)&&w.current(c)&&p->fence_identity&&p->fence_identity==current_fence_identity()&&restore_pending_&&pristine_pending_epoch_==p->epoch&&(!p->current_owner||p->current_owner->current(c,w,v,p->epoch));};
 auto read=[this](uint32_t at,std::span<uint8_t>b){return idle_read(at,b);};
 if(d&&current()&&d->committed&&d->owner==this&&d->host_identity==host_identity_&&d->layout==arena()&&F::matches_saved(d->layout,v,d->footprint)&&story_native_wait_snapshot_actor_roots::no_borrow_saved(v,d->layout)&&story_native_wait_snapshot_actor_roots::no_borrow(read,d->layout)&&F::matches_live(read,d->footprint)){
 auto store=std::make_unique<story_native_wait_journal_store::Store>(d->layout,journal_backend_);
 if(store->adopt_pristine_restored(d->journal)&&current()&&F::matches_live(read,d->footprint)){
 journal_store_=std::move(store);generation_=uint32_t(d->journal.generation);pristine_lineage_=d;idle_lineage_.reset();
 staged_expected_.reset();staged_image_.clear();staged_record_.clear();staged_depth_.reset();staged_pose_bytes_.clear();original_poses_.reset();adopted_baseline_.reset();expected_.reset();canonical_image_.clear();
 publication_sealed_=false;native_mutation_=true;unexposed_=false;reusable_=true;restore_pending_=false;quarantined_=false;pristine_pending_epoch_=0;
 ok=pristine_shape()&&w.current(c)&&(!p->current_owner||p->current_owner->current(c,w,v,p->epoch));
 }}
 }catch(...){ok=false;}
 if(!ok&&fence_held()){quarantined_=true;reusable_=false;expected_.reset();canonical_image_.clear();}
 if(retain)*retain=std::move(p->fence);restore.release();return ok;
}

}

namespace story_native_wait_journal_store {
bool Store::adopt_pristine_restored(const story_native_wait_journal::Record&saved){
 std::lock_guard lock(mutex_);if(state()!=State::Unused||writing()||!guarded())return false;
 std::array<uint8_t,story_native_wait_journal::kBytes>bytes{};story_native_wait_journal::Record actual;
 if(saved.phase!=story_native_wait_journal::Phase::Unpublished||saved.serial!=1||!read(bytes)||!story_native_wait_journal::decode(bytes,actual)||actual!=saved||saved.arena!=layout_.allocation.address)return false;
 record_=saved;state_.store(State::Ready,std::memory_order_release);return true;
}
}
