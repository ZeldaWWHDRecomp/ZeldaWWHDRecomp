// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_arena.h"
#include <algorithm>
namespace story_native_event_arena {namespace {
bool contains(Span s,uint32_t a,size_t bytes){return bytes&&a>=s.address&&uint64_t(a)+bytes<=uint64_t(s.address)+s.bytes;}
bool runtime(uint32_t a,uint32_t bytes){return !(a&15)&&a>=mem::kRuntimeStart&&bytes&&uint64_t(a)+bytes<=mem::kHostStart;}
}
Layout make(uint32_t base){
 if(!runtime(base,kBytes))return {};
 auto s=[&](uint32_t offset,uint32_t bytes){return Span{base+offset,bytes};};
 return {kVersion,s(0,kBytes),{s(0,0x100),s(0x100,0x520),s(0x640,0x40),s(0x680,0x60),s(0x700,0x1000)},
 {s(0x1700,0x100),s(0x1800,0xE60),s(0x2680,0x40),s(0x26C0,0x60),s(0x2800,0x4000)}};
}
bool valid(const Layout&l){return l.version==kVersion&&runtime(l.allocation.address,kBytes)&&l==make(l.allocation.address);}
bool owns(const Layout&l,std::span<const mem::AllocRec>records,uint32_t top){
 if(!valid(l)||top<mem::kRuntimeStart||top>mem::kHostStart||uint64_t(l.allocation.address)+kBytes>top)return false;
 unsigned matches=0;for(const auto&r:records){if(r.addr==l.allocation.address&&r.size==kBytes)++matches;else if(uint64_t(r.addr)<uint64_t(l.allocation.address)+kBytes&&uint64_t(l.allocation.address)<uint64_t(r.addr)+r.size)return false;}return matches==1;
}
bool saved(const Layout&l,ss::Reader allocations,std::span<const ss::MemoryRegion>regions){
 if(!valid(l)||!allocations.p||!allocations.e)return false;
 const uint32_t top=allocations.u32(),count=allocations.u32();
 if(!allocations.ok||top<mem::kRuntimeStart||top>mem::kHostStart||
    uint64_t(l.allocation.address)+kBytes>top||
    uint64_t(count)*sizeof(mem::AllocRec)!=size_t(allocations.e-allocations.p))return false;
 uint64_t previous_end=mem::kRuntimeStart;unsigned owned=0;
 for(uint32_t n=0;n<count;++n){
  const auto record=allocations.pod<mem::AllocRec>();
  const uint64_t end=uint64_t(record.addr)+record.size;
  if(!allocations.ok||!record.size||record.addr<previous_end||end>top)return false;
  previous_end=end;
  if(record.addr==l.allocation.address&&record.size==kBytes)++owned;
  else if(uint64_t(record.addr)<uint64_t(l.allocation.address)+kBytes&&
          uint64_t(l.allocation.address)<end)return false;
 }
 if(owned!=1||!allocations.at_end())return false;
 unsigned matches=0;for(const auto&r:regions){if((r.base&(ss::kMemoryChunk-1))||(r.size&(ss::kMemoryChunk-1))||uint64_t(r.base)+r.size>0x100000000ull)return false;if(contains({r.base,r.size},l.allocation.address,kBytes))++matches;}
 return matches==1;
}
bool read_saved(const Layout&l,ss::Reader allocations,std::span<const ss::MemoryRegion>regions,const ss::MemoryChunks&chunks,uint32_t a,std::span<uint8_t>out){
 if(!saved(l,allocations,regions)||!contains(l.allocation,a,out.size()))return false;
 // Validate all needed chunk pointers before modifying caller output.
 for(size_t offset=0;offset<out.size();){uint32_t current=a+uint32_t(offset),chunk=current&~(ss::kMemoryChunk-1);auto it=chunks.find(chunk);if(it!=chunks.end()&&!it->second)return false;offset+=std::min(out.size()-offset,size_t(ss::kMemoryChunk-(current-chunk)));}
 for(size_t offset=0;offset<out.size();){uint32_t current=a+uint32_t(offset),chunk=current&~(ss::kMemoryChunk-1);size_t n=std::min(out.size()-offset,size_t(ss::kMemoryChunk-(current-chunk)));auto it=chunks.find(chunk);if(it==chunks.end())std::fill_n(out.begin()+offset,n,0);else std::copy_n(it->second+(current-chunk),n,out.begin()+offset);offset+=n;}return true;
}
}
