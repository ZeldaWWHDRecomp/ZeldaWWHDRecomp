// SPDX-License-Identifier: MPL-2.0
#include "runtime.h"
#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
// Reuse native byte fixtures but supply a genuinely distinct allocation for
// each actual Session. The old one-arena allocator remains an unused fixture.
#define runtime_alloc aggregate_unused_alloc
#define runtime_top aggregate_unused_top
#define runtime_alloc_log aggregate_unused_log
#define STORY_NATIVE_WAIT_SESSION_FIXTURE_ONLY
#include "story_native_wait_session_test.cpp"
#undef STORY_NATIVE_WAIT_SESSION_FIXTURE_ONLY
#undef runtime_alloc
#undef runtime_top
#undef runtime_alloc_log
#include "mods/story_native_wait_save_gate.h"
#include "savestate_native_aggregate.h"
#include "state_memory.h"
#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
namespace mem {
uint32_t runtime_alloc(uint32_t bytes,uint32_t align){assert(bytes==story_native_event_arena::kBytes&&align==16);const uint32_t base=arena_at+uint32_t(allocation_calls++)*0x10000;current_fixture->memory[base].assign(bytes,0);allocations.push_back({base,bytes,123});return base;}
uint32_t runtime_top(){return allocations.empty()?arena_at:allocations.back().addr+allocations.back().size;}
std::vector<AllocRec>runtime_alloc_log(){return allocations;}
}
int main(){
 (void)map_arena;(void)unmap_arena;(void)covered;
 constexpr uint32_t mapped_bytes=0x30000;
#ifdef _WIN32
 auto*guest=static_cast<uint8_t*>(VirtualAlloc(PPC_MEM_BASE+arena_at,mapped_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
 auto*guest=static_cast<uint8_t*>(mmap(PPC_MEM_BASE+arena_at,mapped_bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0));
#endif
 assert(guest==PPC_MEM_BASE+arena_at);std::memset(guest,0,mapped_bytes);
 {
  Readiness fixture;std::array<std::unique_ptr<S::Session>,3>sessions;
  std::array<std::unique_ptr<story_native_wait_save_gate::Controller>,3>gates;
  std::array<ss::native_gate::Controller*,3>children;
  for(unsigned n=0;n<3;++n){sessions[n]=std::make_unique<S::Session>(request(),fixture.source());assert(sessions[n]->startup());gates[n]=std::make_unique<story_native_wait_save_gate::Controller>(*sessions[n]);children[n]=gates[n].get();}
  assert(allocation_calls==3&&allocations.size()==3);std::string why;auto aggregate=ss::native_gate::Aggregate::create(children,why);assert(aggregate);
  {auto save=aggregate->begin_save();assert(save);for(const auto&s:sessions)assert(s->fence_held()&&s->journal()&&s->unexposed()&&!s->try_fence());std::thread foreign([&]{for(const auto&s:sessions)assert(!s->fence_held()&&!s->try_fence());});foreign.join();}
  for(const auto&s:sessions)assert(!s->fence_held()&&s->try_fence());
  ss::Writer memory;const std::vector<ss::MemoryRegion>source_regions{{arena_at,mapped_bytes}};
  ss::capture_regions(memory,source_regions,[](uint32_t at){return mem::ptr(at);},[](const uint8_t*,size_t){return true;});
  ss::Reader encoded(memory.b.data(),memory.b.size());std::vector<ss::MemoryRegion>regions;ss::MemoryChunks chunks;assert(ss::read_regions(encoded,regions,chunks));
  ss::Writer allocation_bytes;allocation_bytes.u32(mem::runtime_top());allocation_bytes.u32(uint32_t(allocations.size()));for(const auto&row:allocations)allocation_bytes.pod(row);
  auto view=[&]{return ss::native_gate::SavedView{ss::Reader(allocation_bytes.b.data(),allocation_bytes.b.size()),regions,chunks};};
  assert(!aggregate->finish_load(view()));
  {auto load=aggregate->begin_load(view());assert(load);for(const auto&s:sessions)assert(s->fence_held()&&!s->unexposed());assert(aggregate->finish_load(view())&&!aggregate->finish_load(view()));for(const auto&s:sessions)assert(s->fence_held()&&s->unexposed());}
  assert(aggregate->begin_save());
  // A denied middle child cannot undo revocation of either successful sibling.
  auto busy=sessions[1]->try_fence();assert(busy);assert(!aggregate->begin_progress_load());busy.reset();
  for(unsigned n:{0u,2u}){auto held=sessions[n]->try_fence();assert(held&&!sessions[n]->unexposed());}
  assert(!aggregate->begin_save());
  // Dropping a real retained load fence without finish stays revoked forever.
  const std::array<ss::native_gate::Controller*,1>middle{gates[1].get()};auto abandoned=ss::native_gate::Aggregate::create(middle,why);assert(abandoned);
  {auto load=abandoned->begin_load(view());assert(load);}
  {auto held=sessions[1]->try_fence();assert(held&&!sessions[1]->unexposed());}
  assert(!abandoned->finish_load(view())&&!abandoned->begin_save());
 }
#ifdef _WIN32
 assert(VirtualFree(guest,0,MEM_RELEASE));
#else
 assert(munmap(guest,mapped_bytes)==0);
#endif
}
