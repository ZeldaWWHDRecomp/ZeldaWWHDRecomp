// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_stage_table_owner.h"
#include <cassert>
#include <map>
#include <vector>
#include <algorithm>
#include <optional>
#include <cstdint>
#include <span>
using namespace story_native_stage_table_owner;
namespace {
constexpr uint32_t Play=0x1046F0B0, Base=0x20000000;
struct Fixture {
 std::map<uint32_t,std::vector<uint8_t>> memory;
 story_native_stage_owner::Tracker tracker;
 bool live=true, drift=false;
 unsigned live_calls=0;
 Fixture(){
  memory[Play].resize(0x6000);memory[0x1047E6C4].resize(4);
  memory[0x1047E8F0].resize(64*0x22C);memory[0x104753A8].resize(4);
  memory[Base].resize(0x1000);
  put(0x1047E6C4,77);put(0x1047E8F0+11*0x22C,88);put(0x104753A8,1);
  auto* stage=ptr(Play+0x5134,8);stage[0]='s';stage[1]='e';stage[2]='a';
  put(Play+0x52C4,Base);put(Play+0x52C8,Base+0x40);
  put(Base,0x40);put(Base+4,2);
 }
 uint8_t* ptr(uint32_t at,size_t n){for(auto&[base,bytes]:memory)
  if(at>=base&&uint64_t(at)+n<=uint64_t(base)+bytes.size())return bytes.data()+at-base;
  return nullptr;}
 void put(uint32_t at,uint32_t value){auto*p=ptr(at,4);assert(p);
  for(unsigned n=0;n<4;++n)p[n]=uint8_t(value>>(24-8*n));}
 bool loaded(){story_native_stage_owner::Receipt r;auto& o=r.owner;
  o.info=0x60810000;o.archive_wrapper=0x60811000;o.archive_holder=0x60812000;
  o.archive_resource=0x60818000;o.archive_base=Base-0x1000;
  o.archive_bytes=0x3000;o.outer_base=Base-0x80;o.outer_bytes=0x1100;
  o.resource=Base;o.resource_bytes=0x1000;o.entry=Base+0x118;
  o.archive_key=0x1004DF68;o.resource_key=0x1004DF70;
  o.request_heap=o.pack_heap=0x60830000;o.data_heap=0x60831000;
  o.data_heap_parent=0x60832000;
  o.stage_name[0]='s';o.stage_name[1]='e';o.stage_name[2]='a';
  return tracker.finish_setinfo(tracker.begin_native(),[&]{return std::optional<story_native_stage_owner::Receipt>(r);});}
 Result scan(){return inspect(tracker,
  [&](uint32_t at,std::span<uint8_t> bytes){auto*p=ptr(at,bytes.size());
   if(!p)return false;std::copy_n(p,bytes.size(),bytes.begin());return true;},
  [&](Context& out){out={77,88,11,{'s','e','a'},
    {0x60000000,0x38,99,0,0,true},100,12,true};return true;},
  [&](const story_native_setinfo_candidate::Candidate& owner){
    ++live_calls;return live&&owner.info==0x60810000&&(!drift||live_calls<2);});}
};
}
int main(){
 Fixture f;assert(f.scan().reason==story_native_event_table_scan::Reason::Provenance);
 assert(f.loaded());auto good=f.scan();assert(good.idle&&good.events==2&&f.live_calls>=2);
 f.live=false;assert(!f.scan().idle);
 f.live=true;f.drift=true;f.live_calls=0;
 assert(f.scan().reason==story_native_event_table_scan::Reason::Changed);
 f.tracker.finish_native(f.tracker.begin_native());assert(!f.scan().idle);
}
