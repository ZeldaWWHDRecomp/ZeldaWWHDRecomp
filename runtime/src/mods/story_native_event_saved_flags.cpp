// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_saved_flags.h"
#include <algorithm>
#include <vector>
namespace story_native_event_saved_flags {
bool read(uint32_t manager,std::span<const ss::MemoryRegion>regions,const ss::MemoryChunks&chunks,Flags&out){
 if((manager&3)||manager<mem::kMem2Start||uint64_t(manager)+0x540>mem::kMem2End||regions.empty()||regions.size()>64)return false;
 std::vector<ss::MemoryRegion>sorted(regions.begin(),regions.end());std::sort(sorted.begin(),sorted.end(),[](auto&a,auto&b){return a.base<b.base;});
 uint64_t end=0;for(const auto&r:sorted){if(!r.size||(r.base&(ss::kMemoryChunk-1))||(r.size&(ss::kMemoryChunk-1))||uint64_t(r.base)+r.size>0x100000000ULL||r.base<end)return false;end=uint64_t(r.base)+r.size;}
 const uint32_t start=manager+0x3C;const uint64_t stop=uint64_t(start)+out.size();uint64_t at=start;
 for(const auto&r:sorted){auto finish=uint64_t(r.base)+r.size;if(finish<=at)continue;if(r.base>at)return false;at=std::min(stop,finish);if(at==stop)break;}if(at!=stop)return false;
 Flags next{};for(size_t offset=0;offset<next.size();){uint32_t current=start+uint32_t(offset),chunk=current&~(ss::kMemoryChunk-1);size_t n=std::min(next.size()-offset,size_t(ss::kMemoryChunk-(current-chunk)));auto it=chunks.find(chunk);if(it!=chunks.end()){if(!it->second)return false;std::copy_n(it->second+(current-chunk),n,next.begin()+offset);}offset+=n;}
 out=next;return true;
}
}
