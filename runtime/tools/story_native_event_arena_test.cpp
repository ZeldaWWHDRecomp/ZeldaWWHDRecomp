// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_arena.h"
#include <cassert>
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
using namespace story_native_event_arena;
int main(){
 auto l=make(0x6000F000);assert(valid(l)&&l.wait.image.bytes==0x520&&l.turn.image.bytes==0xE60&&l.wait.record.bytes==4096&&l.turn.record.bytes==16384);assert(!valid(make(0x68000000))&&!valid(make(0xFFFFFFF0))&&!valid(make(0x60000001)));
 std::array<Span,10>spans={l.wait.header,l.wait.image,l.wait.stamp,l.wait.depth,l.wait.record,l.turn.header,l.turn.image,l.turn.stamp,l.turn.depth,l.turn.record};for(unsigned n=0;n<spans.size();++n)for(unsigned j=0;j<n;++j)assert(uint64_t(spans[n].address)>=uint64_t(spans[j].address)+spans[j].bytes||uint64_t(spans[j].address)>=uint64_t(spans[n].address)+spans[n].bytes);
 auto changed=l;changed.turn.record.address=l.wait.image.address;assert(!valid(changed));std::vector<mem::AllocRec>allocs={{l.allocation.address,kBytes,123}};auto top=l.allocation.address+kBytes;assert(owns(l,allocs,top));assert(!owns(l,allocs,top-1));allocs[0].size--;assert(!owns(l,allocs,top));allocs[0].size=kBytes;allocs.push_back(allocs[0]);assert(!owns(l,allocs,top));allocs.pop_back();allocs.push_back({l.wait.image.address,16,0});assert(!owns(l,allocs,top));
 // Exercise the ACTUAL sparse fullstate memory serializer/parser, with the
 // dedicated arena crossing a 64KiB chunk boundary and host-only bytes excluded.
 std::vector<uint8_t>memory(0x20000),host(0x10000,0xEE);for(unsigned n=0;n<kBytes;++n)memory[0xF000+n]=uint8_t(n*7+3);
 std::vector<ss::MemoryRegion>regions={{0x60000000,0x20000}};ss::Writer w;ss::capture_regions(w,regions,[&](uint32_t a){return memory.data()+a-0x60000000;},[](auto,size_t){return true;});ss::Reader reader(w.b.data(),w.b.size());std::vector<ss::MemoryRegion>parsed;ss::MemoryChunks chunks;assert(ss::read_regions(reader,parsed,chunks));
 auto section=[&](uint32_t saved_top,std::vector<mem::AllocRec> rows){ss::Writer a;a.u32(saved_top);a.u32(uint32_t(rows.size()));for(const auto&row:rows)a.pod(row);return a;};
 auto good=section(top,{{l.allocation.address,kBytes,123}});auto view=[](const ss::Writer&a){return ss::Reader(a.b.data(),a.b.size());};
 std::vector<uint8_t>restored(kBytes,0);assert(read_saved(l,view(good),parsed,chunks,l.allocation.address,restored));assert(std::equal(restored.begin(),restored.end(),memory.begin()+0xF000));
 std::array<uint8_t,8>out;out.fill(0xAA);auto low=section(top-1,{{l.allocation.address,kBytes,123}});assert(!read_saved(l,view(low),parsed,chunks,l.wait.image.address,out)&&out[0]==0xAA);assert(!read_saved(l,view(good),parsed,chunks,0x68000000,out)&&out[0]==0xAA);assert(!read_saved(l,view(good),{},chunks,l.wait.image.address,out));ss::MemoryChunks sparse;assert(read_saved(l,view(good),parsed,sparse,l.wait.image.address,out)&&out[0]==0);sparse[0x60000000]=nullptr;out.fill(0xAA);assert(!read_saved(l,view(good),parsed,sparse,l.wait.image.address,out)&&out[0]==0xAA);
 auto invalid_regions=parsed;invalid_regions.push_back(parsed[0]);assert(!saved(l,view(good),invalid_regions));auto wrong=l;wrong.version++;assert(!read_saved(wrong,view(good),parsed,chunks,l.wait.image.address,out));
 auto absent=section(top,{}),short_row=section(top,{{l.allocation.address,kBytes-1,123}}),duplicate=section(top,{{l.allocation.address,kBytes,1},{l.allocation.address,kBytes,2}}),late=section(top,{{l.allocation.address+16,kBytes,123}});
 for(const auto* bad:{&absent,&short_row,&duplicate,&late}){out.fill(0xAA);assert(!read_saved(l,view(*bad),parsed,chunks,l.wait.image.address,out)&&out[0]==0xAA);}
 auto truncated=good;truncated.b.pop_back();assert(!saved(l,view(truncated),parsed));auto trailing=good;trailing.u8(0);assert(!saved(l,view(trailing),parsed));
 // Actual sparse restore recovers arena bytes and never touches excluded host.
 std::fill(memory.begin(),memory.end(),0xCC);ss::restore_regions(parsed,chunks,[&](uint32_t a){return memory.data()+a-0x60000000;},[](auto,size_t){return true;});assert(std::equal(restored.begin(),restored.end(),memory.begin()+0xF000)&&host.front()==0xEE);
}
