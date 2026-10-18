// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_run_footprint.h"
#include <algorithm>
namespace story_native_wait_run_footprint {namespace {
bool span(uint32_t at,size_t n){return at&&n&&n<=0x10000&&uint64_t(at)+n<=0x100000000ull;}
bool valid(const Footprint&f){
 if(f.empty()||f.size()>16)return false;
 size_t bytes=0;for(size_t n=0;n<f.size();++n){const auto&a=f[n];if(!span(a.address,a.bytes.size()))return false;bytes+=a.bytes.size();
  for(size_t p=0;p<n;++p)if(uint64_t(a.address)<uint64_t(f[p].address)+f[p].bytes.size()&&uint64_t(f[p].address)<uint64_t(a.address)+a.bytes.size())return false;
 }return bytes<=0x20000;
}
}
bool capture(const story_native_event_memory::Read&read,const story_native_event_arena::Layout&arena,
 const story_native_event_observation::Expected&e,const story_native_people_pose_record::Poses&poses,
 const story_native_camera_policy::Tables&t,Footprint&out){
 out.clear();try{
  if(!read||!story_native_event_arena::valid(arena)||e.identity.manager<0xF4||e.camera.address<0x248||
    e.camera.type_table!=t.types||e.camera.style_table!=t.styles||e.camera.type_count!=63||e.camera.style_count!=145)return false;
  Footprint f;auto add=[&](uint32_t at,size_t n){f.push_back({at,std::vector<uint8_t>(n)});};
  add(arena.allocation.address,arena.allocation.bytes);add(e.identity.manager-0xF4,0x634);
  for(unsigned n=0;n<2;++n){const auto&p=poses[n].identity;
   if(p.actor!=e.actors[n].address||p.pid!=e.actors[n].pid||p.token!=e.actors[n].token||p.generation!=e.identity.generation)return false;
   add(e.actors[n].address,0x920);add(p.morf,0xC8);add(p.model,0x170);
  }
  add(e.camera.address-0x248,0xB28);add(t.type_count,4);add(t.types,63*0x40);add(t.style_count,4);add(t.styles,145*0x84);
  if(!valid(f))return false;for(auto&b:f)if(!read(b.address,b.bytes))return false;
  if(!matches_live(read,f))return false;out=std::move(f);return true;
 }catch(...){out.clear();return false;}
}
bool matches_live(const story_native_event_memory::Read&read,const Footprint&f){
 try{if(!read||!valid(f))return false;for(const auto&b:f){std::vector<uint8_t>actual(b.bytes.size());if(!read(b.address,actual)||actual!=b.bytes)return false;}return true;}catch(...){return false;}
}
bool matches_saved(const story_native_event_arena::Layout&arena,const ss::native_gate::SavedView&view,const Footprint&f){
 try{
  if(!valid(f)||!story_native_event_arena::saved(arena,view.allocations,view.regions)||view.regions.empty()||view.regions.size()>64)return false;
  for(size_t n=0;n<view.regions.size();++n){const auto&r=view.regions[n];
   if(!r.size||(r.base&(ss::kMemoryChunk-1))||(r.size&(ss::kMemoryChunk-1))||uint64_t(r.base)+r.size>0x100000000ull)return false;
   for(size_t p=0;p<n;++p){const auto&o=view.regions[p];if(uint64_t(r.base)<uint64_t(o.base)+o.size&&uint64_t(o.base)<uint64_t(r.base)+r.size)return false;}
  }
  auto covered=[&](uint32_t at,size_t n){if(!span(at,n))return false;return std::any_of(view.regions.begin(),view.regions.end(),[&](const auto&r){return at>=r.base&&uint64_t(at)+n<=uint64_t(r.base)+r.size;});};
  for(const auto&[at,data]:view.chunks)if(!data||(at&(ss::kMemoryChunk-1))||!covered(at,ss::kMemoryChunk))return false;
  for(const auto&b:f){if(!covered(b.address,b.bytes.size()))return false;
   for(size_t offset=0;offset<b.bytes.size();){const uint32_t at=b.address+uint32_t(offset),chunk=at&~(ss::kMemoryChunk-1);const size_t count=std::min(b.bytes.size()-offset,size_t(ss::kMemoryChunk-(at-chunk)));const auto found=view.chunks.find(chunk);
    if(found==view.chunks.end()){if(!std::all_of(b.bytes.begin()+offset,b.bytes.begin()+offset+count,[](uint8_t v){return !v;}))return false;}
    else if(!std::equal(b.bytes.begin()+offset,b.bytes.begin()+offset+count,found->second+(at-chunk)))return false;offset+=count;
   }
  }return true;
 }catch(...){return false;}
}
}
