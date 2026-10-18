// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_session.h"
#include "story_native_wait_snapshot_roots.h"
#include "story_native_wait_snapshot_actor_roots.h"
#include "native_quiescence.h"
#include <algorithm>
namespace story_native_wait_session {
bool Session::initial_import_roots(const ss::native_gate::SavedView&view,Cpu*cpu,const threads::QuiescenceWitness&parked)const{try{
 const auto shape=[&]{return fence_held()&&cpu&&parked.current(cpu)&&!native_mutation_&&!reusable_&&!quarantined_&&!publication_sealed_&&
  !idle_lineage_&&!pristine_lineage_&&!staged_expected_&&staged_image_.empty()&&staged_record_.empty()&&
  ((restore_pending_&&!unexposed_)||(!restore_pending_&&unexposed()));};
 if(!shape())return false;
 const auto l=arena();const auto current=journal();if(!current||current->phase!=story_native_wait_journal::Phase::Unpublished||
  current->serial!=1||current->generation!=generation_||current->definition!=readiness_.definition_hash()||current->arena!=l.allocation.address)return false;
 std::array<uint8_t,story_native_event_arena::kBytes>bytes{};story_native_wait_journal::Record saved;
 if(!story_native_event_arena::read_saved(l,view.allocations,view.regions,view.chunks,l.allocation.address,bytes)||
  !story_native_wait_journal::decode(std::span<const uint8_t>(bytes).first(story_native_wait_journal::kBytes),saved)||saved!=*current||
  !std::all_of(bytes.begin()+story_native_wait_journal::kBytes,bytes.end(),[](uint8_t b){return b==0;}))return false;
 // This validates every region before either saved graph reader is used. No
 // omitted chunk is interpreted as zero and no live-memory fallback exists.
 if(!story_native_wait_snapshot_actor_roots::no_borrow_saved(view,l))return false;
 const auto read_saved=[&](uint32_t at,std::span<uint8_t>out){
  if(out.empty()||uint64_t(at)+out.size()>0x100000000ull)return false;
  bool covered=false;for(const auto&r:view.regions)if(at>=r.base&&uint64_t(at)+out.size()<=uint64_t(r.base)+r.size)covered=true;
  if(!covered)return false;
  for(size_t off=0;off<out.size();){const uint32_t address=at+uint32_t(off),base=address&~(ss::kMemoryChunk-1);const auto i=view.chunks.find(base);
   if(i==view.chunks.end()||!i->second)return false;const auto count=std::min(out.size()-off,size_t(ss::kMemoryChunk-(address-base)));
   std::copy_n(i->second+address-base,count,out.begin()+off);off+=count;
  }return true;
 };
 story_native_wait_run_footprint::Footprint saved_roots,live_roots;
 return story_native_wait_snapshot_roots::capture(read_saved,l,saved_roots)&&snapshot_roots(live_roots)&&
  story_native_wait_snapshot_actor_roots::no_borrow_saved(view,l)&&shape()&&journal()==current;
}catch(...){return false;}}
}
