// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_wait_journal_store.h"
#include <algorithm>
#include <cassert>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <array>
#include <vector>
#include <cstdint>
#include <span>

namespace A=story_native_event_arena;
namespace J=story_native_wait_journal;
namespace S=story_native_wait_journal_store;

namespace mem {
static std::vector<AllocRec> test_allocations;
static uint32_t test_top=0;
std::vector<AllocRec> runtime_alloc_log(){return test_allocations;}
uint32_t runtime_top(){return test_top;}
}

int main(){
 const auto layout=A::make(0x60010000);
 mem::test_top=layout.allocation.address+A::kBytes;
 mem::test_allocations={{layout.allocation.address,A::kBytes,123}};
 bool fenced=true,owned=true,fail_write=false;
 unsigned writes=0,reads=0,fail_read_at=0;
 std::array<uint8_t,J::kBytes> header{};
 auto backend=S::Backend{
  [&]{return owned;},[&]{return fenced;},
  [&](uint32_t at,std::span<uint8_t> out){
   assert(at==layout.wait.header.address&&out.size()==header.size());++reads;
   if(fail_read_at==reads)return false;
   std::copy(header.begin(),header.end(),out.begin());return true;
  },
  [&](uint32_t at,std::span<const uint8_t> in){
   assert(at==layout.wait.header.address&&in.size()==header.size());++writes;
   if(fail_write){std::copy_n(in.begin(),19,header.begin());return false;}
   std::copy(in.begin(),in.end(),header.begin());return true;
  }
 };
 J::Record initial{7,1,layout.allocation.address,J::Phase::Unpublished};
 initial.definition[0]=0x42;
 auto real=S::guest_backend(layout,[&]{return fenced;});
 assert(real.allocation());
 mem::test_allocations[0].size--;
 assert(!real.allocation());
 std::array<uint8_t,J::kBytes> denied_read{};
 assert(!real.read(layout.wait.header.address,denied_read));
 mem::test_allocations[0].size++;
 S::Store store(layout,backend);
 assert(store.persist_initial(initial)&&store.state()==S::State::Ready);
 assert(!store.writing()&&store.receipt()&&*store.receipt()==initial);
 assert(store.persist_transition(J::Phase::Preparing));
 assert(store.receipt()->phase==J::Phase::Preparing&&store.receipt()->serial==2);
 // A partially written Published record is permanently quarantined. No
 // retry can overwrite its uncertain bytes or present a storage receipt.
 fail_write=true;
 assert(!store.persist_transition(J::Phase::Published));
 const auto after_failure=header;
 const unsigned write_count=writes;
 fail_write=false;
 assert(store.state()==S::State::Quarantined&&!store.writing()&&!store.receipt());
 assert(!store.persist_transition(J::Phase::Published)&&writes==write_count&&header==after_failure);

 header.fill(0);fail_read_at=reads+2; // initial read succeeds; post-write readback fails
 S::Store readback(layout,backend);
 assert(!readback.persist_initial(initial)&&readback.state()==S::State::Quarantined);
 assert(writes==write_count+1&&!readback.receipt());
 fail_read_at=0;
 header.fill(0);fenced=false;
 assert(!real.read(layout.wait.header.address,denied_read));
 assert(!real.write(layout.wait.header.address,denied_read));
 S::Store unfenced(layout,backend);
 const auto before=writes;
 assert(!unfenced.persist_initial(initial)&&writes==before);
 fenced=true;owned=false;
 S::Store unowned(layout,backend);
 assert(!unowned.persist_initial(initial)&&writes==before);
 owned=true;header.fill(0);
 S::Store tampered(layout,backend);
 assert(tampered.persist_initial(initial));
 header[5]^=1;
 assert(!tampered.persist_transition(J::Phase::Preparing)&&tampered.state()==S::State::Quarantined);
 header.fill(0);
 S::Store missing_fence(layout,{backend.allocation,{},backend.read,backend.write});
 assert(!missing_fence.persist_initial(initial));
 J::Record wrong_arena=initial;wrong_arena.arena+=0x10000;
 S::Store mismatched(layout,backend);
 const auto no_extra_writes=writes;
 assert(!mismatched.persist_initial(wrong_arena)&&writes==no_extra_writes);
 // Concurrent initial callers cannot both observe a virgin header. A second
 // attempted initialization fails closed, without a second guest write.
 header.fill(0);
 S::Store simultaneous(layout,backend);
 std::atomic<unsigned> winners{0};
 const unsigned prior_writes=writes;
 std::thread first([&]{if(simultaneous.persist_initial(initial))++winners;});
 std::thread second([&]{if(simultaneous.persist_initial(initial))++winners;});
 first.join();second.join();
 assert(winners==1&&writes==prior_writes+1);
 assert(simultaneous.state()==S::State::Quarantined);
 // Receipt copies and transition are serialized; a reader sees a complete
 // old or new serial, never a partially assigned Record.
 header.fill(0);
 S::Store observed(layout,backend);
 assert(observed.persist_initial(initial));
 std::atomic<bool> running{true};
 std::thread observer([&]{while(running.load()){auto copy=observed.receipt();assert(copy&&
  (copy->serial==1||copy->serial==2)&&copy->generation==7&&copy->definition==initial.definition);}});
 assert(observed.persist_transition(J::Phase::Preparing));
 running=false;observer.join();
 // Rearm is storage-only: it preserves immutable definition/arena, checks the
 // exact previous bytes and commits one monotonically newer generation.
 header.fill(0);S::Store rearmed(layout,backend);assert(rearmed.persist_initial(initial));
 assert(rearmed.persist_rearm(initial,20));auto second_generation=*rearmed.receipt();
 assert(second_generation.generation==20&&second_generation.serial==2&&second_generation.definition==initial.definition);
 const auto count_after_rearm=writes;
 assert(!rearmed.persist_rearm(initial,21)&&writes==count_after_rearm&&rearmed.state()==S::State::Quarantined);
 header.fill(0);S::Store partial_rearm(layout,backend);assert(partial_rearm.persist_initial(initial));
 fail_write=true;assert(!partial_rearm.persist_rearm(initial,21));fail_write=false;
 const auto partial_bytes=header;const auto partial_count=writes;
 assert(!partial_rearm.persist_rearm(initial,22)&&writes==partial_count&&header==partial_bytes&&!partial_rearm.receipt());
 header.fill(0);S::Store active_rearm(layout,backend);assert(active_rearm.persist_initial(initial));
 assert(active_rearm.persist_transition(J::Phase::Preparing));const auto active_record=*active_rearm.receipt();
 const auto active_count=writes;assert(!active_rearm.persist_rearm(active_record,30)&&writes==active_count);
 header.fill(0);S::Store backward_rearm(layout,backend);assert(backward_rearm.persist_initial(initial));
 assert(!backward_rearm.persist_rearm(initial,7));

}
