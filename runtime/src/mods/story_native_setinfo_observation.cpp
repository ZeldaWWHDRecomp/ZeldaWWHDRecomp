// SPDX-License-Identifier: MPL-2.0
#include "story_native_setinfo_observation.h"
#include "guest_addr.h"
#include "runtime.h"
#include "savestate_native_gate.h"
#include <algorithm>
#include <cstring>

namespace story_native_setinfo_observation { namespace {
bool span(uint32_t at,size_t bytes) {
    if(!bytes||uint64_t(at)+bytes>uint64_t(UINT32_MAX)+1)return false;
    if(at>=mem::kMem2Start&&uint64_t(at)+bytes<=mem::kMem2End)return true;
    return at>=mem::kRuntimeStart&&uint64_t(at)+bytes<=mem::runtime_top()&&
           uint64_t(at)+bytes<=mem::kHostStart;
}
bool layout(story_native_setinfo_candidate::Layout& out,uint32_t& manager_global) {
    const uint32_t addresses[]={
        GD(0x100E222C), // ResMgr
        GD(0x100E1DC0), // Info
        GD(0x100E1960), // ResModel wrapper
        GD(0x100E25D0), // named archive holder
        GD(0x1015E184), // SharcArchiveRes
        GD(0x100E1A84), // Info tree node
        GD(0x100E1A9C), // resource tree node
        GD(0x10145B38), // stage solid heap
        GD(0x101F4F7C)  // ResMgr singleton pointer
    };
    out={addresses[0],addresses[1],addresses[2],addresses[3],
         addresses[4],addresses[5],addresses[6],addresses[7]};
    manager_global=addresses[8];
    return true;
}
story_native_setinfo_candidate::Read guest_read() {
    return [](uint32_t at,std::span<uint8_t> dst){
        if(!span(at,dst.size()))return false;
        std::memcpy(dst.data(),mem::ptr(at),dst.size());
        return true;
    };
}
bool manager_address(const story_native_setinfo_candidate::Read& read,
                     uint32_t global,uint32_t& manager) {
    uint8_t raw[4]{};
    if(!read(global,raw))return false;
    manager=uint32_t(raw[0])<<24|uint32_t(raw[1])<<16|
            uint32_t(raw[2])<<8|uint32_t(raw[3]);
    return span(manager,0x1131c);
}
bool safe_text(const story_native_setinfo_candidate::Read& read,uint32_t object,
               std::span<char> out) {
    if(!object||out.size()<2)return false;
    uint8_t p[4]{};
    if(!read(object,p))return false;
    const uint32_t address=uint32_t(p[0])<<24|uint32_t(p[1])<<16|
                           uint32_t(p[2])<<8|uint32_t(p[3]);
    if(!address)return false;
    for(size_t i=0;i<out.size();++i){
        uint8_t c{};
        if(uint64_t(address)+i>UINT32_MAX||
           !read(address+uint32_t(i),std::span<uint8_t>(&c,1)))return false;
        out[i]=char(c);
        if(!c)return i!=0;
        if(c<0x20||c>0x7e)return false;
    }
    return false;
}
}

Result inspect_detailed(const Cpu* cpu,const story_native_stage_owner_hooks::Args& args,
                        uint32_t result,story_native_setinfo_candidate::Candidate& out) {
    out={};
    // invoke_original captures Args before the native body, and calls the
    // observer only after a successful return. This checks that exact boundary.
    if(!cpu||result!=1||cpu->r[3]!=result)return Result::ReturnBoundary;
    story_native_setinfo_candidate::Layout type{};
    uint32_t manager_global{};
    if(!layout(type,manager_global))return Result::Layout;
    const auto read=guest_read();
    uint32_t manager{};
    if(!manager_address(read,manager_global,manager))return Result::Manager;
    try{return story_native_setinfo_candidate::verify(read,type,args.r3_r6[0],
               args.r3_r6[1],args.r3_r6[2],manager,out)?Result::Valid:Result::Candidate;}
    catch(...){out={};return Result::Candidate;}
}
bool inspect(const Cpu* cpu,const story_native_stage_owner_hooks::Args& args,
             uint32_t result,story_native_setinfo_candidate::Candidate& out) {
    return inspect_detailed(cpu,args,result,out)==Result::Valid;
}
bool inspect_live(uint32_t control,
                  const story_native_setinfo_candidate::Candidate& expected,
                  story_native_setinfo_candidate::Candidate& out) {
    out={};
    story_native_setinfo_candidate::Layout type{};
    uint32_t manager_global{};
    if(!layout(type,manager_global))return false;
    const auto read=guest_read();
    uint32_t manager{};
    if(!manager_address(read,manager_global,manager))return false;
    try{return story_native_setinfo_candidate::verify_live(read,type,control,
                                                            manager,expected,out);}
    catch(...){out={};return false;}
}
bool inspect_saved(const ss::native_gate::SavedView&view,uint32_t control,
                   const story_native_setinfo_candidate::Candidate&expected,
                   story_native_setinfo_candidate::Candidate&out){
    out={};
    try{
        if(view.regions.empty()||view.regions.size()>64)return false;
        for(size_t n=0;n<view.regions.size();++n){const auto&r=view.regions[n];
            if(!r.size||(r.base&(ss::kMemoryChunk-1))||(r.size&(ss::kMemoryChunk-1))||uint64_t(r.base)+r.size>uint64_t(UINT32_MAX)+1)return false;
            for(size_t p=0;p<n;++p){const auto&other=view.regions[p];if(uint64_t(r.base)<uint64_t(other.base)+other.size&&uint64_t(other.base)<uint64_t(r.base)+r.size)return false;}
        }
        auto covered=[&](uint32_t at,size_t bytes){if(!bytes||uint64_t(at)+bytes>uint64_t(UINT32_MAX)+1)return false;return std::any_of(view.regions.begin(),view.regions.end(),[&](const auto&r){return at>=r.base&&uint64_t(at)+bytes<=uint64_t(r.base)+r.size;});};
        for(const auto&[at,data]:view.chunks)if(!data||(at&(ss::kMemoryChunk-1))||!covered(at,ss::kMemoryChunk))return false;
        const story_native_setinfo_candidate::Read read=[&](uint32_t at,std::span<uint8_t>dst){
            if(!covered(at,dst.size()))return false;
            size_t offset=0;while(offset<dst.size()){
                const uint32_t address=at+uint32_t(offset),chunk=address&~(ss::kMemoryChunk-1);
                const size_t count=std::min(dst.size()-offset,size_t(ss::kMemoryChunk-(address-chunk)));
                const auto found=view.chunks.find(chunk);
                if(found==view.chunks.end())std::fill_n(dst.begin()+offset,count,0);
                else std::copy_n(found->second+(address-chunk),count,dst.begin()+offset);
                offset+=count;
            }return true;
        };
        story_native_setinfo_candidate::Layout type{};uint32_t global{};
        if(!layout(type,global))return false;
        std::array<uint8_t,4>bytes{};if(!read(global,bytes))return false;
        const uint32_t manager=uint32_t(bytes[0])<<24|uint32_t(bytes[1])<<16|uint32_t(bytes[2])<<8|bytes[3];
        if(!manager||(manager&3)||!story_native_setinfo_candidate::verify_live(read,type,control,manager,expected,out)){out={};return false;}
        if(out!=expected){out={};return false;}return true;
    }catch(...){out={};return false;}
}
bool matches_delete(const story_native_stage_owner_hooks::Args& args,
                    uint32_t control,
                    const story_native_setinfo_candidate::Candidate& expected) {
    if(!control||args.r3_r6[0]!=control||!expected.info)return false;
    const auto read=guest_read();
    char name[64]{},path[64]{};
    if(!safe_text(read,args.r3_r6[1],name)||
       !safe_text(read,args.r3_r6[2],path))return false;
    if(!std::memchr(expected.stage_name.data(),0,expected.stage_name.size()))return false;
    return std::strcmp(name,"Stage")==0&&
           std::strcmp(path,expected.stage_name.data())==0;
}
} // namespace story_native_setinfo_observation
