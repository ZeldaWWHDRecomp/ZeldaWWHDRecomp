// SPDX-License-Identifier: MPL-2.0
#include "savestate_native_aggregate.h"
#include "savestate_native_finished_load.h"
#include <mutex>
#include <stdexcept>
#include <set>
#include <thread>
#include <vector>
namespace ss::native_gate {
struct Aggregate::Impl {
 const std::vector<Controller*>children;
 LoadGroupFactory group_factory;
 std::mutex mutex;bool acquiring=false,poisoned=false;
 struct Transaction {
  Mode mode;std::thread::id thread=std::this_thread::get_id();
  std::vector<std::unique_ptr<Hold>>holds;
  LoadGroup group;
  Reader allocations;const MemoryRegion*regions=nullptr;size_t region_count=0;const MemoryChunks*chunks=nullptr;
  bool attempted=false,finishing=false,release_requested=false,released=false;
  Transaction(Mode m,size_t count,const SavedView*v):mode(m){holds.reserve(count);if(v){allocations=v->allocations;regions=v->regions.data();region_count=v->regions.size();chunks=&v->chunks;}}
  bool same(const SavedView&v)const{return allocations.p==v.allocations.p&&allocations.e==v.allocations.e&&allocations.ok==v.allocations.ok&&regions==v.regions.data()&&region_count==v.regions.size()&&chunks==&v.chunks;}
  void clear()noexcept{if((!holds.empty()||group.stage||group.owner)&&thread!=std::this_thread::get_id())std::terminate();if(group.stage){if(group.close)group.close(*group.stage);group.stage.reset();}while(!holds.empty())holds.pop_back();if(group.owner){if(group.close_owner)group.close_owner(*group.owner);group.owner.reset();}}
  ~Transaction(){clear();}
 };
 std::shared_ptr<Transaction>active;
 explicit Impl(std::vector<Controller*>list,LoadGroupFactory factory):children(std::move(list)),group_factory(std::move(factory)){}
 void release(const std::shared_ptr<Transaction>&t)noexcept{
  if(t->thread!=std::this_thread::get_id())std::terminate();
  {std::lock_guard lock(mutex);if(t->released)return;if(t->finishing){t->release_requested=true;return;}t->released=true;if(t->mode==Mode::Load&&!t->attempted)poisoned=true;acquiring=true;if(active==t)active.reset();}
  // Native Session fences form a real stack; never rely on vector teardown
  // order or release an earlier child while a later child still retains it.
  t->clear();
  {std::lock_guard lock(mutex);acquiring=false;}
 }
 struct Lease final:Hold {
  std::shared_ptr<Impl>owner;std::shared_ptr<Transaction>transaction;
  Lease(std::shared_ptr<Impl>o,std::shared_ptr<Transaction>t):owner(std::move(o)),transaction(std::move(t)){}
  ~Lease()override{owner->release(transaction);}
 };
};
Aggregate::Aggregate(std::shared_ptr<Impl>impl):impl_(std::move(impl)){}
Aggregate::~Aggregate()=default;
size_t Aggregate::child_count()const noexcept{return impl_->children.size();}
std::unique_ptr<Aggregate>Aggregate::create(std::span<Controller*const>children,std::string&why,LoadGroupFactory factory){why.clear();try{
 if(children.empty()||children.size()>kMaxChildren){why="native aggregate requires 1..129 persistent children";return {};}
 std::set<Controller*>seen;for(auto*child:children)if(!child||!seen.insert(child).second){why="native aggregate rejects null or duplicate children";return {};}
 return std::unique_ptr<Aggregate>(new Aggregate(std::make_shared<Impl>(std::vector<Controller*>(children.begin(),children.end()),std::move(factory))));
}catch(...){why="native aggregate construction failed";return {};}}
std::unique_ptr<Hold>Aggregate::begin(Mode mode,const SavedView*view){
 auto state=impl_;
 {std::unique_lock lock(state->mutex,std::try_to_lock);if(!lock.owns_lock()||state->acquiring||state->active||state->poisoned)return {};state->acquiring=true;}
 std::shared_ptr<Impl::Transaction>transaction;
 try{
  transaction=std::make_shared<Impl::Transaction>(mode,state->children.size(),view);bool complete=true;
  if(mode==Mode::Load&&state->group_factory){
   transaction->group=state->group_factory(*view);
   if(!transaction->group.stage||!transaction->group.close||(transaction->group.owner&&!transaction->group.close_owner)||(transaction->group.finish&&!transaction->group.owner))throw std::runtime_error("native restore group unavailable");
  }
  for(auto*child:state->children){std::unique_ptr<Hold>hold;try{
   if(mode==Mode::Save)hold=child->begin_save();else if(mode==Mode::Load)hold=child->begin_group_load(*view,transaction->group.stage,transaction->group.owner);else hold=child->begin_progress_load();
  }catch(...){complete=false;}
   if(hold)transaction->holds.push_back(std::move(hold));else{complete=false;if(mode!=Mode::Progress)break;}
  }
  if(complete){auto lease=std::make_unique<Impl::Lease>(state,transaction);{std::lock_guard lock(state->mutex);state->active=transaction;state->acquiring=false;}return lease;}
 }catch(...){/* No partially acquired hold may escape. */}
 if(transaction)transaction->clear();
 {std::lock_guard lock(state->mutex);state->acquiring=false;if(mode!=Mode::Save)state->poisoned=true;}
 return {};
}
std::unique_ptr<Hold>Aggregate::begin_save(){return begin(Mode::Save,nullptr);}
bool Aggregate::finish_save(){
 auto state=impl_;std::shared_ptr<Impl::Transaction>transaction;
 {std::unique_lock lock(state->mutex,std::try_to_lock);if(!lock.owns_lock()||state->acquiring||state->poisoned||!state->active)return false;
  transaction=state->active;if(transaction->thread!=std::this_thread::get_id()||transaction->mode!=Mode::Save||transaction->attempted||transaction->released)return false;
  transaction->attempted=true;transaction->finishing=true;
 }
 bool success=true;
 for(auto*child:state->children){try{if(!child->finish_save())success=false;}catch(...){success=false;}}
 bool release=false;{std::lock_guard lock(state->mutex);transaction->finishing=false;release=transaction->release_requested;}
 if(release)state->release(transaction);
 return success;
}
std::unique_ptr<Hold>Aggregate::begin_load(const SavedView&view){return begin(Mode::Load,&view);}
std::unique_ptr<Hold>Aggregate::begin_progress_load(){return begin(Mode::Progress,nullptr);}
bool Aggregate::finish_load(const SavedView&view){
 auto state=impl_;std::shared_ptr<Impl::Transaction>transaction;
 {std::unique_lock lock(state->mutex,std::try_to_lock);if(!lock.owns_lock()||state->acquiring||state->poisoned||!state->active)return false;
  transaction=state->active;if(transaction->thread!=std::this_thread::get_id()||transaction->mode!=Mode::Load||transaction->attempted||transaction->released)return false;
  transaction->attempted=true;if(!transaction->same(view)){state->poisoned=true;return false;}transaction->finishing=true;
 }
 bool success=true;
 // Each child is checked even after an earlier failure. Their own contract
 // forbids publication on finish; aggregate failure is permanently denied.
 for(auto*child:state->children){
  bool child_ok=false;try{child_ok=child->finish_load(view);}catch(...){child_ok=false;}
  if(!child_ok){
   success=false;
   // Revoke the common proof immediately, including failures in children
   // which have no Stage participant of their own. Later checks still run.
   if(transaction->group.stage&&transaction->group.close)transaction->group.close(*transaction->group.stage);
  }
 }
 // A source-backed finalizer may only remove the previous runtime front.
 // Issue its token after every child passed, before releasing any real Hold.
 if(success&&transaction->group.finish){
  FinishedLoad finished(transaction->group.owner,view);
  success=finished.current()&&transaction->group.finish(finished);
  finished.invalidate();
  if(!success&&transaction->group.stage&&transaction->group.close)transaction->group.close(*transaction->group.stage);
 }
 bool release=false;{std::lock_guard lock(state->mutex);transaction->finishing=false;if(!success)state->poisoned=true;release=transaction->release_requested;}
 if(release)state->release(transaction);
 return success;
}
}
