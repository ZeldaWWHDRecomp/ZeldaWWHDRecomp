// SPDX-License-Identifier: MPL-2.0
#include "../src/savestate_native_gate.h"
#include <cassert>
#include <array>
#include <stdexcept>
#include <cstdint>
#include <memory>
using namespace ss;
namespace {
struct TestHold final : native_gate::Hold {
 int& count;
 explicit TestHold(int& n):count(n){++count;}
 ~TestHold() override {--count;}
};
struct TestController final : native_gate::Controller {
 int holds=0,saves=0,loads=0,finishes=0,progress_loads=0;
 bool allow=true,finish_allowed=true;
 std::unique_ptr<native_gate::Hold> begin_save() noexcept override {
  ++saves;return allow?std::make_unique<TestHold>(holds):nullptr;
 }
 std::unique_ptr<native_gate::Hold> begin_load(const native_gate::SavedView& view) noexcept override {
  ++loads;
  Reader saved=view.allocations;
  uint32_t word=saved.u32();
  return allow&&word==0x12345678&&view.regions.size()==1&&view.chunks.empty()
   ?std::make_unique<TestHold>(holds):nullptr;
 }
 bool finish_load(const native_gate::SavedView& view) noexcept override {
  ++finishes;
  Reader saved=view.allocations;
  return holds==1&&finish_allowed&&saved.u32()==0x12345678&&view.regions.size()==1;
 }
 std::unique_ptr<native_gate::Hold> begin_progress_load() noexcept override {
  ++progress_loads;return allow?std::make_unique<TestHold>(holds):nullptr;
 }
};
struct ThrowController final : native_gate::Controller {
 int holds=0;
 bool finish_save()override {throw std::runtime_error("capture commit failed");}
 std::unique_ptr<native_gate::Hold> begin_save() override {
  auto hold=std::make_unique<TestHold>(holds);
  throw std::runtime_error("save admission failed");
 }
 std::unique_ptr<native_gate::Hold> begin_load(const native_gate::SavedView&) override {
  auto hold=std::make_unique<TestHold>(holds);
  throw std::runtime_error("load preflight failed");
 }
 bool finish_load(const native_gate::SavedView&) override {
  throw std::runtime_error("rehydrate failed");
 }
 std::unique_ptr<native_gate::Hold> begin_progress_load() override {
  auto hold=std::make_unique<TestHold>(holds);
  throw std::runtime_error("progress load admission failed");
 }
};
struct ThrowFinishController final : native_gate::Controller {
 int holds=0;
 bool quarantined=false;
 std::unique_ptr<native_gate::Hold> begin_save() override {return {};}
 std::unique_ptr<native_gate::Hold> begin_load(const native_gate::SavedView&) override {
  quarantined=true;
  return std::make_unique<TestHold>(holds);
 }
 bool finish_load(const native_gate::SavedView&) override {
  assert(quarantined&&holds==1);
  throw std::runtime_error("restore reconciliation failed");
 }
 std::unique_ptr<native_gate::Hold> begin_progress_load() override {return {};}
};
struct OrderHold final : native_gate::Hold {
 bool& thawed;
 bool& released_while_parked;
 OrderHold(bool& t,bool& released):thawed(t),released_while_parked(released){}
 ~OrderHold() override {released_while_parked=!thawed;}
};
}
int main(){
 assert(!native_gate::controller());
 static TestController c;
 assert(!native_gate::register_controller(nullptr));
 assert(native_gate::register_controller(&c));
 assert(!native_gate::register_controller(&c));
 assert(native_gate::controller()==&c);
 {
  auto hold=c.begin_save();assert(hold&&c.holds==1);
  c.allow=false;assert(!c.begin_save());assert(c.holds==1);
 }assert(c.holds==0);
 c.allow=true;
 std::array<uint32_t,1> alloc{0x12345678};
 std::array<MemoryRegion,1> regions{{{0x10000000,0x10000}}};
 MemoryChunks chunks;
 {
  auto hold=c.begin_load({Reader(alloc.data(),sizeof alloc),regions,chunks});
  assert(hold&&c.holds==1);
  assert(native_gate::try_finish_load(&c,{Reader(alloc.data(),sizeof alloc),regions,chunks}));
  c.finish_allowed=false;
  assert(!native_gate::try_finish_load(&c,{Reader(alloc.data(),sizeof alloc),regions,chunks}));
  c.finish_allowed=true;
 }assert(c.holds==0);
 std::array<uint32_t,1> wrong{0};
 assert(!c.begin_load({Reader(wrong.data(),sizeof wrong),regions,chunks}));
 assert(c.saves==2&&c.loads==2&&c.finishes==2);
 {
  auto hold=native_gate::try_begin_progress_load(&c);
  assert(hold&&c.holds==1&&c.progress_loads==1);
  c.allow=false;
  assert(!native_gate::try_begin_progress_load(&c));
  assert(c.holds==1&&c.progress_loads==2);
 }assert(c.holds==0);
 c.allow=true;
 ThrowController throwing;
 assert(!native_gate::try_begin_save(nullptr));
 assert(!native_gate::try_finish_save(nullptr)&&!native_gate::try_finish_save(&throwing));
 assert(!native_gate::try_begin_save(&throwing)&&throwing.holds==0);
 assert(!native_gate::try_begin_load(&throwing,
         {Reader(alloc.data(),sizeof alloc),regions,chunks})&&throwing.holds==0);
 assert(!native_gate::try_finish_load(nullptr,{Reader(alloc.data(),sizeof alloc),regions,chunks}));
 assert(!native_gate::try_finish_load(&throwing,
         {Reader(alloc.data(),sizeof alloc),regions,chunks}));
 ThrowFinishController finish_throwing;
 {
  auto hold=native_gate::try_begin_load(&finish_throwing,
      {Reader(alloc.data(),sizeof alloc),regions,chunks});
  assert(hold&&finish_throwing.holds==1&&finish_throwing.quarantined);
  assert(!native_gate::try_finish_load(&finish_throwing,
      {Reader(alloc.data(),sizeof alloc),regions,chunks}));
  assert(finish_throwing.holds==1&&finish_throwing.quarantined);
 }
 assert(finish_throwing.holds==0&&finish_throwing.quarantined);
 assert(!native_gate::try_begin_progress_load(nullptr));
 assert(!native_gate::try_begin_progress_load(&throwing)&&throwing.holds==0);
 // The actual savestate caller must reset its Hold before threads::thaw,
 // including the retry path. This fixture checks the required lifetime order.
 bool thawed=false,released_while_parked=false;
 std::unique_ptr<native_gate::Hold> hold=
  std::make_unique<OrderHold>(thawed,released_while_parked);
 hold.reset();
 thawed=true;
 assert(released_while_parked);
 using story_native_wait_journal::Phase;
 native_gate::LiveBoundary live;
 live.journal={.generation=7,.serial=3,.arena=0x60000000,.phase=Phase::Unpublished};
 live.journal.definition[0]=0xA5;
 live.lease=story_native_event_lease::Phase::Retired;
 live.exact_allocation=live.journal_readback=live.fence_held=true;
 live.manager_unpublished=live.consumers_detached=live.checkpoint_clean=true;
 live.callback_depth=0;
 assert(native_gate::clean_for_save(live));
 auto bad_live=live;
 for(auto phase:{Phase::Preparing,Phase::Published,Phase::Running,
                 Phase::Draining,Phase::Detaching,Phase::Quarantined}){
  bad_live=live;bad_live.journal.phase=phase;
  assert(!native_gate::clean_for_save(bad_live));
 }
 for(auto phase:{story_native_event_lease::Phase::Prepared,
                 story_native_event_lease::Phase::Published,
                 story_native_event_lease::Phase::Running,
                 story_native_event_lease::Phase::Draining,
                 story_native_event_lease::Phase::Quarantined}){
  bad_live=live;bad_live.lease=phase;
  assert(!native_gate::clean_for_save(bad_live));
 }
 bad_live=live;bad_live.fence_held=false;assert(!native_gate::clean_for_save(bad_live));
 bad_live=live;bad_live.journal_readback=false;assert(!native_gate::clean_for_save(bad_live));
 bad_live=live;bad_live.exact_allocation=false;assert(!native_gate::clean_for_save(bad_live));
 bad_live=live;bad_live.manager_unpublished=false;assert(!native_gate::clean_for_save(bad_live));
 bad_live=live;bad_live.consumers_detached=false;assert(!native_gate::clean_for_save(bad_live));
 bad_live=live;bad_live.checkpoint_clean=false;assert(!native_gate::clean_for_save(bad_live));
 bad_live=live;bad_live.callback_depth=1;assert(!native_gate::clean_for_save(bad_live));
 native_gate::SavedBoundary saved;
 saved.journal=live.journal;
 saved.snapshot_owned=saved.exact_allocation=saved.manager_unpublished=
  saved.consumers_detached=saved.depth_zero=true;
 assert(native_gate::clean_for_load(live,saved));
 auto bad_saved=saved;
 bad_saved.snapshot_owned=false;assert(!native_gate::clean_for_load(live,bad_saved));
 bad_saved=saved;bad_saved.exact_allocation=false;assert(!native_gate::clean_for_load(live,bad_saved));
 bad_saved=saved;bad_saved.depth_zero=false;assert(!native_gate::clean_for_load(live,bad_saved));
 bad_saved=saved;bad_saved.manager_unpublished=false;assert(!native_gate::clean_for_load(live,bad_saved));
 bad_saved=saved;bad_saved.consumers_detached=false;assert(!native_gate::clean_for_load(live,bad_saved));
 bad_saved=saved;bad_saved.journal.phase=Phase::Running;
 assert(!native_gate::clean_for_load(live,bad_saved));
 bad_saved=saved;bad_saved.journal.serial=0;
 assert(!native_gate::clean_for_load(live,bad_saved));
 bad_saved=saved;++bad_saved.journal.generation;
 assert(!native_gate::clean_for_load(live,bad_saved));
 bad_saved=saved;++bad_saved.journal.arena;
 assert(!native_gate::clean_for_load(live,bad_saved));
 bad_saved=saved;bad_saved.journal.definition[0]^=1;
 assert(!native_gate::clean_for_load(live,bad_saved));
}
