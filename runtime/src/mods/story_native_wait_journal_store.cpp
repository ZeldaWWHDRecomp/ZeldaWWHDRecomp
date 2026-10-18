// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_journal_store.h"
#include <algorithm>
#include <cstring>

namespace story_native_wait_journal_store {
namespace {
bool header(const story_native_event_arena::Layout& layout,uint32_t at,size_t bytes){
 return story_native_event_arena::valid(layout)&&at==layout.wait.header.address&&
        bytes==story_native_wait_journal::kBytes;
}
}
Backend guest_backend(const story_native_event_arena::Layout& layout,Verify fence){
 Backend b;
 b.allocation=[layout]{return story_native_event_arena::owns(
  layout,mem::runtime_alloc_log(),mem::runtime_top());};
 b.fence=std::move(fence);
 auto allowed=[allocation=b.allocation,fence=b.fence]{
  try{return allocation&&fence&&allocation()&&fence();}catch(...){return false;}
 };
 b.read=[layout,allowed](uint32_t at,std::span<uint8_t> out){
  if(!header(layout,at,out.size())||!allowed())return false;
  std::memcpy(out.data(),mem::ptr(at),out.size());return true;
 };
 b.write=[layout,allowed](uint32_t at,std::span<const uint8_t> in){
  if(!header(layout,at,in.size())||!allowed())return false;
  std::memcpy(mem::ptr(at),in.data(),in.size());return true;
 };
 return b;
}
Store::Store(story_native_event_arena::Layout layout,Backend backend)
 :layout_(layout),backend_(std::move(backend)){}
bool Store::guarded()const{
 try{return story_native_event_arena::valid(layout_)&&backend_.allocation&&backend_.fence&&
        backend_.read&&backend_.write&&backend_.allocation()&&backend_.fence();}
 catch(...){return false;}
}
bool Store::quarantine(){state_.store(State::Quarantined,std::memory_order_release);return false;}
bool Store::read(std::array<uint8_t,story_native_wait_journal::kBytes>& out){
 return guarded()&&backend_.read(layout_.wait.header.address,out);
}
bool Store::persist(const story_native_wait_journal::Record& next){
 std::array<uint8_t,story_native_wait_journal::kBytes> encoded{},actual{};
 if(!story_native_wait_journal::encode(next,encoded)||!guarded())return quarantine();
 // The caller's entry fence must remain held through this write and readback.
 writing_.store(true,std::memory_order_release);
 bool ok=false;
 try{ok=backend_.write(layout_.wait.header.address,encoded)&&
        read(actual)&&actual==encoded;}
 catch(...){ok=false;}
 if(!ok){quarantine();writing_.store(false,std::memory_order_release);return false;}
 record_=next;state_.store(State::Ready,std::memory_order_release);
 writing_.store(false,std::memory_order_release);return true;
}
bool Store::persist_initial(const story_native_wait_journal::Record& initial){
 std::lock_guard lock(mutex_);
 if(state()!=State::Unused||initial.phase!=story_native_wait_journal::Phase::Unpublished||
    initial.arena!=layout_.allocation.address)
  return quarantine();
 try{
  std::array<uint8_t,story_native_wait_journal::kBytes> old{};
  if(!read(old)||!std::all_of(old.begin(),old.end(),[](uint8_t v){return v==0;}))
   return quarantine();
  return persist(initial);
 }catch(...){return quarantine();}
}
bool Store::persist_transition(story_native_wait_journal::Phase phase){
 std::lock_guard lock(mutex_);
 if(state()!=State::Ready)return false;
 try{
  std::array<uint8_t,story_native_wait_journal::kBytes> expected{},actual{};
  if(!story_native_wait_journal::encode(record_,expected)||!read(actual)||actual!=expected)
   return quarantine();
  story_native_wait_journal::Record next;
  if(!story_native_wait_journal::transition(record_,phase,next))return quarantine();
  return persist(next);
 }catch(...){return quarantine();}
}
bool Store::persist_rearm(const story_native_wait_journal::Record& previous,uint32_t generation){
 std::lock_guard lock(mutex_);
 if(state()!=State::Ready)return false;
 try{
  std::array<uint8_t,story_native_wait_journal::kBytes> expected{},actual{};
  if(previous!=record_||previous.phase!=story_native_wait_journal::Phase::Unpublished||
     !generation||generation<=previous.generation||previous.serial==UINT64_MAX||
     !story_native_wait_journal::encode(previous,expected)||!read(actual)||actual!=expected)
   return quarantine();
  auto next=previous;next.generation=generation;++next.serial;
  return persist(next);
 }catch(...){return quarantine();}
}

}
