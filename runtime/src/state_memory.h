// Full-state sparse memory serialization, shared with synthetic mod-region tests.
#pragma once
#include "savestate.h"
#include <unordered_map>
namespace ss {
inline constexpr uint32_t kMemoryChunk=0x10000;
struct MemoryRegion {uint32_t base,size;};
using MemoryChunks=std::unordered_map<uint32_t,const uint8_t*>;
inline bool memory_chunk_zero(const uint8_t* p) {
    for(uint32_t i=0;i<kMemoryChunk;i+=8){uint64_t word;memcpy(&word,p+i,sizeof word);if(word)return false;}
    return true;
}
template<class Pointer,class Touched>
void capture_regions(Writer& w,const std::vector<MemoryRegion>& regions,Pointer ptr,Touched touched) {
    w.u32(uint32_t(regions.size()));
    for(const auto& g:regions) {
        w.u32(g.base);w.u32(g.size);size_t count_at=w.b.size();w.u32(0);uint32_t present=0;
        for(uint32_t off=0;off<g.size;off+=kMemoryChunk){
            const uint8_t* p=ptr(g.base+off);if(!touched(p,kMemoryChunk))continue;
            if(memory_chunk_zero(p))continue;w.u32(off/kMemoryChunk);w.bytes(p,kMemoryChunk);++present;
        }
        memcpy(w.b.data()+count_at,&present,sizeof present);
    }
}
inline bool read_regions(Reader& r,std::vector<MemoryRegion>& regions,MemoryChunks& chunks) {
    uint32_t count=r.u32();if(count>64)return false;
    for(uint32_t i=0;i<count;++i){
        MemoryRegion g{r.u32(),r.u32()};
        if(!r.ok||(g.base&(kMemoryChunk-1))||(g.size&(kMemoryChunk-1))||uint64_t(g.base)+g.size>0x100000000ull)return false;
        for(const auto& previous:regions)if(uint64_t(g.base)<uint64_t(previous.base)+previous.size&&uint64_t(previous.base)<uint64_t(g.base)+g.size)return false;
        regions.push_back(g);uint32_t present=r.u32();if(present>g.size/kMemoryChunk)return false;
        for(uint32_t j=0;j<present;++j){uint32_t index=r.u32();
            if(index>=g.size/kMemoryChunk||!chunks.emplace(g.base+index*kMemoryChunk,r.p).second||!r.bytes(nullptr,kMemoryChunk))return false;
        }
    }
    return r.ok&&r.at_end();
}
template<class Pointer,class Touched>
void restore_regions(const std::vector<MemoryRegion>& regions,const MemoryChunks& chunks,Pointer ptr,Touched touched) {
    for(const auto& g:regions)for(uint32_t off=0;off<g.size;off+=kMemoryChunk){
        uint32_t address=g.base+off;uint8_t* p=ptr(address);auto found=chunks.find(address);
        if(found!=chunks.end()){memcpy(p,found->second,kMemoryChunk);continue;}
        if(touched(p,kMemoryChunk)&&!memory_chunk_zero(p))memset(p,0,kMemoryChunk);
    }
}
} // namespace ss
