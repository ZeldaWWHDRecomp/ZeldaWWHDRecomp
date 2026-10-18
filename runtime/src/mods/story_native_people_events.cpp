// SPDX-License-Identifier: MPL-2.0
#include "story_native_people_events.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
#include <utility>
namespace story_native_people_events {namespace {
const uint32_t CommonCut=GC(0x0259F858),PeopleProfile=GD(0x101C4CD0),PeopleVtable=GD(0x100209C4);
uint32_t word(std::span<const uint8_t>b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
uint16_t half(std::span<const uint8_t>b,size_t p){return uint16_t(b[p])<<8|b[p+1];}
bool span(uint32_t p,size_t n,bool runtime=false){return !(p&3)&&n&&n<=(runtime?mem::kHostStart-mem::kRuntimeStart:mem::kMem2End-mem::kMem2Start)&&p>=(runtime?mem::kRuntimeStart:mem::kMem2Start)&&uint64_t(p)+n<=(runtime?mem::kHostStart:mem::kMem2End);}
struct Keep {Cpu*c;Cpu saved;explicit Keep(Cpu*p):c(p),saved(*p){}~Keep(){*c=saved;}};
thread_local Router* scoped_router=nullptr;
thread_local int scoped_index=-1;
constexpr const char*Names[]={"SDK_ACTOR_A","SDK_ACTOR_B"};
}
Router::Router(Backend b,Expected e):backend_(std::move(b)),expected_(std::move(e)){
 const auto&i=expected_.identity;
 if(!backend_.read||!backend_.write||!backend_.invoke||!backend_.actor||!backend_.lease||!backend_.action||!backend_.capabilities.staff_lookup||!backend_.capabilities.common_event_move||!i.generation||!i.stage||!i.room||!i.program||i.bytes<0x190||(i.image&15)||!span(i.manager,0x540)||!span(i.image,i.bytes,true)||expected_.room_number>63)return;
 for(unsigned n=0;n<2;++n){const auto&a=expected_.actors[n];if(!span(a.address,0x920)||!a.pid||a.pid==0xFFFFFFFFu||!a.token||a.profile!=0x175||(a.subtype!=1&&a.subtype!=9))return;}
 const auto&a=expected_.actors[0];const auto&z=expected_.actors[1];if(a.pid==z.pid||a.token==z.token||(uint64_t(a.address)<uint64_t(z.address)+0x920&&uint64_t(z.address)<uint64_t(a.address)+0x920))return;
 for(const auto&v:expected_.actors){const auto name=word(v.cut,0);if((uint64_t(name)>=i.image&&uint64_t(name)<uint64_t(i.image)+i.bytes)||(word(v.cut,8)&&word(v.cut,8)!=v.address)||(word(v.cut,0xC)&&word(v.cut,0xC)!=v.address)||(word(v.cut,0x68)&&word(v.cut,0x68)!=v.address+0x3AC))return;}
 configured_=true;
}
bool Router::read(uint32_t p,std::span<uint8_t>b){return span(p,b.size(),p>=mem::kRuntimeStart)&&backend_.read(p,b);}
bool Router::scope(){return configured_&&!quarantined_&&!detached_&&backend_.lease(expected_.identity);}
Binding Router::binding(unsigned n,const Actor&a)const{return {expected_.identity,a,expected_.identity.image+0xF0+n*0x50,int32_t(n)};}
bool Router::capture(unsigned n,Actor&out){
 if(n>=2||!backend_.lease(expected_.identity))return false;
 std::array<uint8_t,0x30>profile{};std::array<uint8_t,0x920>bytes{};
 if(!read(PeopleProfile,profile)||half(profile,8)!=0x175||word(profile,0x10)!=0x920||!read(expected_.actors[n].address,bytes))return false;
 const auto&w=expected_.actors[n];out=w;out.pid=word(bytes,4);out.profile=half(bytes,14);out.subtype=bytes[0x2DD];std::copy_n(bytes.begin()+0x3E0,out.cut.size(),out.cut.begin());
 return out.pid==w.pid&&out.profile==0x175&&word(bytes,0x10)==PeopleProfile&&word(bytes,0xB4)==PeopleVtable&&word(bytes,0xB0)==0x00FF0000&&out.subtype==w.subtype&&bytes[0x905]==w.subtype&&bytes[0x326]==expected_.room_number&&backend_.actor(out,expected_.identity);
}
bool Router::own_cut(unsigned n,const Actor&a)const{
 const auto&b=a.cut;const uint32_t actor=a.address,name=expected_.identity.image+0xF0+n*0x50;
 if(!(b[0x60]<=1&&b[0x61]<=1&&word(b,0)==name&&word(b,8)==actor&&(word(b,0xC)==0||word(b,0xC)==actor)&&(word(b,0x68)==0||word(b,0x68)==actor+0x3AC)))return false;
 if(word(b,4)==0xFFFFFFFFu||word(b,4)==n)return true;
 // Native casting retains the saved cut's staff ID until the first CommonCut.
 // Admit that exact saved value only while this staff has no cached action.
 if(word(b,4)!=word(expected_.actors[n].cut,4)||word(b,0x10)!=0xFFFFFFFFu)return false;
 std::array<uint8_t,4>state{};std::array<uint8_t,8>staff{};
 const uint32_t initial=expected_.program?expected_.program->initial_cut(n):n;
 if(expected_.program&&(expected_.identity.bytes!=expected_.program->bytes()||expected_.frames!=expected_.program->frames()))return false;
 return backend_.read(expected_.identity.image+0xE4,state)&&(word(state,0)==2||word(state,0)==4)&&
   backend_.read(name+0x38,staff)&&word(staff,0)==initial&&word(staff,4)==0xFFFFFFFFu;
}
Dispatch Router::event_move(Cpu*c,uint32_t actor){
 if(!c)return Dispatch::Refused;
 int index=-1;for(unsigned n=0;n<2;++n)if(expected_.actors[n].address==actor)index=int(n);
 if(index<0)return Dispatch::Foreign;
 Keep cpu(c);
 if(!configured_||quarantined_||detached_)return Dispatch::Refused;
 if(depth_||scoped_router){quarantined_=true;return Dispatch::Quarantined;}
 try{
  Actor a;if(!scope()||!capture(index,a)||!own_cut(index,a)){quarantined_=true;return Dispatch::Quarantined;}
  uint32_t action=0xFFFFFFFFu;const auto b=binding(index,a);
  if(!backend_.action(b,action)||(action!=0&&action!=3&&action!=4)){quarantined_=true;return Dispatch::Quarantined;}
  if(action==4&&(!backend_.capabilities.move_process||!backend_.capabilities.cut_end||!backend_.capabilities.ride_callback))return Dispatch::Refused;
  std::array<uint8_t,32>name{};if(!read(b.name,name)||!std::equal(Names[index],Names[index]+12,name.begin())){quarantined_=true;return Dispatch::Quarantined;}
  active_=index;++depth_;scoped_router=this;scoped_index=index;
  struct Clear{Router&r;~Clear(){scoped_router=nullptr;scoped_index=-1;r.active_=-1;--r.depth_;}}clear{*this};
  const uint32_t handled=backend_.invoke(c,CommonCut,{actor+0x3E0});
  Actor after;uint32_t fresh=0xFFFFFFFFu;
  if(!handled||quarantined_||!scope()||!capture(index,after)||!own_cut(index,after)||word(after.cut,4)!=uint32_t(index)||word(after.cut,0x10)!=action||!backend_.action(binding(index,after),fresh)||fresh!=action){quarantined_=true;return Dispatch::Quarantined;}
  // Native People.eventMove preserves attention over common WAIT transitions.
  // Match that one-byte behavior only after exact post-native identity checks;
  // never restore a stale whole cut object over native progress.
  if(a.cut[0x60]&&!after.cut[0x60]){
   Actor fresh;if(!capture(index,fresh)||fresh.cut!=after.cut){quarantined_=true;return Dispatch::Quarantined;}
   const std::array<uint8_t,1>attention={a.cut[0x60]};
   if(!backend_.write(actor+0x440,attention)){quarantined_=true;return Dispatch::Quarantined;}
   after.cut[0x60]=attention[0];
   if(!capture(index,fresh)||fresh.cut!=after.cut||!own_cut(index,fresh)){quarantined_=true;return Dispatch::Quarantined;}
  }
  return Dispatch::Processed;
 }catch(...){quarantined_=true;return Dispatch::Quarantined;}
}
bool Router::staff_id(Cpu*c){
 if(!c||!configured_)return false;
 try{
  std::array<uint8_t,12>name{};bool owned_name=false,owned_pointer=false;
  const uint32_t p=c->r[4];for(unsigned n=0;n<2;++n)owned_pointer|=p==expected_.identity.image+0xF0+n*0x50;
  const bool inside=(p>=mem::kMem2Start&&uint64_t(p)+name.size()<=mem::kMem2End)||(p>=mem::kRuntimeStart&&uint64_t(p)+name.size()<=mem::kHostStart);
  if(!inside||!backend_.read(p,name)){
   if(owned_pointer||scoped_router==this){quarantined_=true;c->r[3]=0xFFFFFFFFu;return true;}return false;
  }
  for(const auto*n:Names)owned_name|=std::equal(n,n+12,name.begin());
  if(!owned_name){if(owned_pointer||scoped_router==this){quarantined_=true;c->r[3]=0xFFFFFFFFu;return true;}return false;}
  uint32_t result=0xFFFFFFFFu;
  if(scoped_router==this&&scoped_index==active_&&active_>=0&&depth_==1&&!quarantined_&&!detached_&&c->r[3]==expected_.identity.manager&&c->r[4]==expected_.identity.image+0xF0+uint32_t(active_)*0x50&&c->r[5]==0&&c->r[6]==0){Actor actor;if(scope()&&capture(unsigned(active_),actor)&&own_cut(unsigned(active_),actor))result=uint32_t(active_);else quarantined_=true;}
  else if(scoped_router==this)quarantined_=true;
  c->r[3]=result;return true;
 }catch(...){quarantined_=true;c->r[3]=0xFFFFFFFFu;return true;}
}
bool Router::detach_people(Cpu*c){
 if(!c||!configured_||depth_||detached_)return detached_;
 Keep cpu(c);
 try{
  if(!backend_.lease(expected_.identity))return false;
  std::array<Actor,2>actors;for(unsigned n=0;n<2;++n)if(!capture(n,actors[n])||!own_cut(n,actors[n])){quarantined_=true;return false;}
  for(unsigned n=0;n<2;++n){Actor fresh;if(!capture(n,fresh)||fresh.cut!=actors[n].cut||!backend_.write(fresh.address+0x3E0,expected_.actors[n].cut)){quarantined_=true;return false;}if(!capture(n,fresh)||fresh.cut!=expected_.actors[n].cut){quarantined_=true;return false;}}
  detached_=true;return true;
 }catch(...){quarantined_=true;return false;}
}
}
