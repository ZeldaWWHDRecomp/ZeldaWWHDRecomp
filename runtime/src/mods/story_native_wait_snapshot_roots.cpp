// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_snapshot_roots.h"
#include "runtime.h"
#include "guest_addr.h"
namespace story_native_wait_snapshot_roots {
namespace {uint32_t word(std::span<const uint8_t>b,unsigned p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}}
bool capture(const story_native_event_memory::Read&read,const story_native_event_arena::Layout&l,story_native_wait_run_footprint::Footprint&out){out.clear();try{
 if(!read||!story_native_event_arena::valid(l))return false;
 const auto inside=[&](uint32_t at){return at>=l.allocation.address&&uint64_t(at)<uint64_t(l.allocation.address)+l.allocation.bytes;};
 const uint32_t play=GD(0x1046F0B0);
 story_native_wait_run_footprint::Footprint f{{play+0x51D0,std::vector<uint8_t>(0x634)},{play+0x5AF8,std::vector<uint8_t>(4)}};
 if(!read(f[0].address,f[0].bytes)||!read(f[1].address,f[1].bytes))return false;
 for(unsigned n=0;n<8;++n)if(inside(word(f[0].bytes,0xF4+n*4)))return false;
 if(inside(word(f[0].bytes,0xF4+0x53C)))return false;
 const auto process=word(f[1].bytes);
 if(process){if((process&3)||process<mem::kMem2Start||uint64_t(process)+0xB28>mem::kMem2End||
  (uint64_t(process)<uint64_t(l.allocation.address)+l.allocation.bytes&&uint64_t(l.allocation.address)<uint64_t(process)+0xB28))return false;
  f.push_back({process,std::vector<uint8_t>(0xB28)});if(!read(process,f.back().bytes)||inside(word(f.back().bytes,0x248+0x8A8)))return false;
 }
 if(!story_native_wait_run_footprint::matches_live(read,f))return false;out=std::move(f);return true;
 }catch(...){out.clear();return false;}}
}
