// SPDX-License-Identifier: MPL-2.0
#define STORY_NATIVE_SETINFO_CANDIDATE_FIXTURE_ONLY
#include "story_native_setinfo_candidate_test.cpp"
#include "mods/story_native_setinfo_observation.h"
#include "savestate_native_gate.h"
#include <map>
#include <algorithm>
#include <vector>
#include <cstdint>
namespace mem {uint32_t runtime_top(){return 0x68000000;}}
struct SavedFixture {
 std::map<uint32_t,std::vector<uint8_t>> memory;
 std::vector<ss::MemoryRegion> regions;ss::MemoryChunks chunks;
 explicit SavedFixture(const Fixture&source){
  for(const auto&r:source.regions)for(size_t p=0;p<r.bytes.size();++p){const auto at=r.base+uint32_t(p),base=at&~(ss::kMemoryChunk-1);auto&b=memory[base];if(b.empty())b.resize(ss::kMemoryChunk);b[at-base]=r.bytes[p];}
  auto&global=memory[0x101F0000];global.resize(ss::kMemoryChunk);w32(global,0x4F7C,Mgr);
  for(auto&[base,data]:memory){regions.push_back({base,ss::kMemoryChunk});chunks[base]=data.data();}
 }
 ss::native_gate::SavedView view(){return {{},regions,chunks};}
 void put(uint32_t at,uint32_t value){w32(memory.at(at&~(ss::kMemoryChunk-1)),at&(ss::kMemoryChunk-1),value);}
};
int main(){
 auto live=fixture();Candidate expected;assert(verify(live.read(),L,Ctrl,Args,Args+0x20,Mgr,expected));
 SavedFixture saved(live);Candidate out;assert(story_native_setinfo_observation::inspect_saved(saved.view(),Ctrl,expected,out)&&out==expected);
 // Live data remains valid throughout. Missing/malformed snapshot bytes never
 // consult live EA, including the resource manager global or sparse-zero tail.
 for(unsigned fault=0;fault<10;++fault){SavedFixture bad(live);auto known=expected;auto control=Ctrl;
  if(fault==0)bad.put(0x101F4F7C,0);
  if(fault==1)bad.put(0x101F4F7C,Mgr+4);
  if(fault==2)control+=4;
  if(fault==3)known.stage_name[0]='Q';
  if(fault==4)++known.request_heap;
  if(fault==5)bad.chunks.erase(Sarc);
  if(fault==6){bad.regions.erase(std::remove_if(bad.regions.begin(),bad.regions.end(),[](auto&r){return r.base==0x101F0000;}),bad.regions.end());bad.chunks.erase(0x101F0000);}
  if(fault==7)bad.regions.push_back(bad.regions.front());
  if(fault==8)bad.chunks.begin()->second=nullptr;
  if(fault==9)bad.regions.clear();
  out=expected;assert(!story_native_setinfo_observation::inspect_saved(bad.view(),control,known,out)&&out==Candidate{});
  Candidate current;assert(verify_live(live.read(),L,Ctrl,Mgr,expected,current)&&current==expected);
 }
}
