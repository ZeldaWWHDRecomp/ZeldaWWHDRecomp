// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_snapshot_actor_roots.h"
#include "story_native_manager_cancel_dispatch_audit.h"
#include "story_actors.h"
#include "runtime.h"
#include "guest_addr.h"
#include <algorithm>
namespace story_native_wait_snapshot_actor_roots {namespace {
uint32_t word(std::span<const uint8_t>b,unsigned p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool same(const story_actors::ResidentEventProcess&a,const story_actors::ResidentEventProcess&b){return a.presence==b.presence&&a.pid==b.pid&&a.pid_slot==b.pid_slot&&a.identity_row==b.identity_row&&a.scratch_window==b.scratch_window&&a.pid_bytes==b.pid_bytes&&a.identity_bytes==b.identity_bytes&&a.scratch_bytes==b.scratch_bytes&&a.declaration.token==b.declaration.token&&a.declaration.room==b.declaration.room&&a.declaration.subtype==b.declaration.subtype&&a.declaration.stage==b.declaration.stage&&a.declaration.fingerprint==b.declaration.fingerprint;}
bool normalize(story_native_wait_run_footprint::Footprint&f){
 std::sort(f.begin(),f.end(),[](const auto&a,const auto&b){return a.address<b.address;});
 story_native_wait_run_footprint::Footprint out;size_t total=0;
 for(auto&b:f){if(!b.address||b.bytes.empty()||uint64_t(b.address)+b.bytes.size()>0x100000000ull)return false;
  if(out.empty()||uint64_t(out.back().address)+out.back().bytes.size()<b.address)out.push_back(std::move(b));
  else{auto&a=out.back();const size_t off=b.address-a.address,overlap=std::min(b.bytes.size(),a.bytes.size()-off);
   if(!std::equal(b.bytes.begin(),b.bytes.begin()+overlap,a.bytes.begin()+off))return false;
   a.bytes.insert(a.bytes.end(),b.bytes.begin()+overlap,b.bytes.end());}
 }
 for(const auto&b:out){if(b.bytes.size()>0x10000)return false;total+=b.bytes.size();}
 if(out.size()>16||total>0x20000)return false;f=std::move(out);return true;
}
}
bool append(const story_native_event_memory::Read&read,const story_native_event_arena::Layout&l,const std::string&mod,std::span<const std::string>ids,story_native_wait_run_footprint::Footprint&out){try{
 if(!read||!story_native_event_arena::valid(l)||mod.empty()||ids.empty()||ids.size()>2)return false;
 if(ids.size()==2&&ids[0]==ids[1])return false;
 const auto inside=[&](uint32_t at){return at>=l.allocation.address&&uint64_t(at)<uint64_t(l.allocation.address)+l.allocation.bytes;};
 using namespace story_native_manager_cancel_dispatch;detail::Census before,after;
 if(!detail::census(read,GD(0x101F3328),before))return false;
 auto f=out;
 auto add=[&](uint32_t at,size_t bytes){f.push_back({at,std::vector<uint8_t>(bytes)});return read(at,f.back().bytes);};
 std::vector<story_actors::ResidentEventProcess>processes;std::vector<uint32_t>actors;
 for(const auto&id:ids){story_actors::ResidentEventProcess p;
  if(!story_actors::resident_event_process(mod,id,p))return false;
  if(p.presence==story_actors::ResidentEventProcess::Presence::Absent){
   f.push_back({p.pid_slot,{p.pid_bytes.begin(),p.pid_bytes.end()}});f.push_back({p.identity_row,{p.identity_bytes.begin(),p.identity_bytes.end()}});f.push_back({p.scratch_window,{p.scratch_bytes.begin(),p.scratch_bytes.end()}});
   processes.push_back(std::move(p));actors.push_back(0);continue;
  }
  uint32_t actor=0;
  for(const auto&node:before.nodes){const uint32_t candidate=word(node.second,12);std::array<uint8_t,8>prefix{};
   if(!read(candidate,prefix))return false;if(word(prefix,4)!=p.pid)continue;
   if(actor)return false;actor=candidate;
  }
  if(!actor||std::find(actors.begin(),actors.end(),actor)!=actors.end())return false;
  story_actors::ResidentEventIdentity identity;
  if(!story_actors::resident_event_identity_quiet(mod,id,actor,p.pid,identity)||identity.token!=p.declaration.token||identity.fingerprint!=p.declaration.fingerprint)return false;
  if(!add(actor,0x920))return false;
  const auto&bytes=f.back().bytes;for(unsigned offset:{0u,8u,0xCu,0x1Cu,0x40u,0x68u})if(inside(word(bytes,0x3E0+offset)))return false;
  f.push_back({p.pid_slot,{p.pid_bytes.begin(),p.pid_bytes.end()}});f.push_back({p.identity_row,{p.identity_bytes.begin(),p.identity_bytes.end()}});f.push_back({p.scratch_window,{p.scratch_bytes.begin(),p.scratch_bytes.end()}});
  if(!add(GD(0x1046F0B0)+0x5134,8)||!add(GD(0x1047E6C4),4)||!add(GD(0x1047E8F0)+p.declaration.room*0x22C,4))return false;
  processes.push_back(std::move(p));actors.push_back(actor);
 }
 if(!detail::census(read,GD(0x101F3328),after)||after!=before)return false;
 for(size_t n=0;n<ids.size();++n){story_actors::ResidentEventProcess p;story_actors::ResidentEventIdentity identity;
  if(!story_actors::resident_event_process(mod,ids[n],p)||!same(p,processes[n])||(actors[n]&&!story_actors::resident_event_identity_quiet(mod,ids[n],actors[n],p.pid,identity)))return false;
 }
 if(!no_borrow(read,l)||!normalize(f)||!story_native_wait_run_footprint::matches_live(read,f))return false;
 out=std::move(f);return true;
}catch(...){return false;}}
bool no_borrow(const story_native_event_memory::Read&read,const story_native_event_arena::Layout&l){try{
 if(!read||!story_native_event_arena::valid(l))return false;
 using namespace story_native_manager_cancel_dispatch;detail::Census a,b;
 if(!detail::census(read,GD(0x101F3328),a))return false;
 const auto inside=[&](uint32_t at){return at>=l.allocation.address&&uint64_t(at)<uint64_t(l.allocation.address)+l.allocation.bytes;};
 story_native_wait_run_footprint::Footprint proof;
 for(const auto&node:a.nodes){const uint32_t actor=word(node.second,12);std::array<uint8_t,0x14>prefix{};
  if(!read(actor,prefix))return false;proof.push_back({actor,{prefix.begin(),prefix.end()}});if((uint32_t(prefix[0xE])<<8|prefix[0xF])!=0x175)continue;
  if(word(prefix,0x10)!=GD(0x101C4CD0)||uint64_t(actor)+0x920>mem::kMem2End)return false;
  std::array<uint8_t,4>vt{},size{};if(!read(actor+0xB4,vt)||word(vt)!=GD(0x100209C4)||!read(GD(0x101C4CD0)+0x10,size)||word(size)!=0x920)return false;
  proof.push_back({actor+0xB4,{vt.begin(),vt.end()}});proof.push_back({GD(0x101C4CD0)+0x10,{size.begin(),size.end()}});
  std::array<uint8_t,0x6C>cut{};if(!read(actor+0x3E0,cut))return false;
  for(unsigned off:{0u,8u,0xCu,0x1Cu,0x40u,0x68u})if(inside(word(cut,off)))return false;
  proof.push_back({actor+0x3E0,{cut.begin(),cut.end()}});
 }
 if(!detail::census(read,GD(0x101F3328),b)||a!=b)return false;
 for(const auto&p:proof){std::vector<uint8_t>after(p.bytes.size());if(!read(p.address,after)||after!=p.bytes)return false;}
 return true;
}catch(...){return false;}}
bool no_borrow_saved(const ss::native_gate::SavedView&v,const story_native_event_arena::Layout&l){try{
 // No live-memory fallback and no inferred zero chunk: every census/pointer
 // byte used by this bounded graph must be physically supplied by SavedView.
 if(v.regions.empty()||v.regions.size()>64)return false;
 for(size_t n=0;n<v.regions.size();++n){const auto&r=v.regions[n];
  if(!r.size||(r.base&(ss::kMemoryChunk-1))||(r.size&(ss::kMemoryChunk-1))||uint64_t(r.base)+r.size>0x100000000ull)return false;
  for(size_t p=0;p<n;++p){const auto&o=v.regions[p];if(uint64_t(r.base)<uint64_t(o.base)+o.size&&uint64_t(o.base)<uint64_t(r.base)+r.size)return false;}
 }
 const auto read=[&](uint32_t at,std::span<uint8_t>out){
  if(out.empty()||uint64_t(at)+out.size()>0x100000000ull)return false;
  bool covered=false;for(const auto&r:v.regions)if(at>=r.base&&uint64_t(at)+out.size()<=uint64_t(r.base)+r.size)covered=true;
  if(!covered)return false;
  for(size_t off=0;off<out.size();){const uint32_t address=at+uint32_t(off),base=address&~(ss::kMemoryChunk-1);const auto it=v.chunks.find(base);
   if(it==v.chunks.end()||!it->second)return false;const size_t count=std::min(out.size()-off,size_t(ss::kMemoryChunk-(address-base)));
   std::copy_n(it->second+(address-base),count,out.begin()+off);off+=count;
  }return true;
 };
 return no_borrow(read,l);
}catch(...){return false;}}

}
