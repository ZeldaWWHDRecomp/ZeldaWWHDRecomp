// SPDX-License-Identifier: MPL-2.0
#include "story_native_manager_cancel.h"
#include "guest_addr.h"
#include "ppc.h"
#include <algorithm>
#include <cstring>
namespace story_native_manager_cancel {namespace {
uint32_t w(std::span<const uint8_t>b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
void put(std::span<uint8_t>b,unsigned p,uint32_t v){for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(v>>(24-8*i));}
bool range(I::Range r){return r.address&&r.bytes&&uint64_t(r.address)+r.bytes<=0x100000000ull;}
bool overlap(I::Range a,I::Range b){return uint64_t(a.address)<uint64_t(b.address)+b.bytes&&uint64_t(b.address)<uint64_t(a.address)+a.bytes;}
bool same(const Boundary&a,const Boundary&b){return a.expected==b.expected&&a.logic_step==b.logic_step&&a.elapsed==b.elapsed&&a.depth.serial==b.depth.serial&&a.parent.entry==b.parent.entry&&a.parent.function==b.parent.function&&a.parent.receiver==b.parent.receiver&&a.parent.subject==b.parent.subject&&a.parent.pid==b.parent.pid&&a.parent.token==b.parent.token&&a.parent.identity==b.parent.identity&&a.parent.authority==b.parent.authority;}
std::atomic<uint64_t> serial{0};
uint64_t next_serial(){uint64_t value=serial.load();for(;;){if(value==UINT64_MAX)return 0;if(serial.compare_exchange_weak(value,value+1))return value+1;}}
}
thread_local Cancel* Cancel::scope_=nullptr;
Cancel::Cancel(O::Expected e,std::array<P::Snapshot,2>p,Sources s):expected_(std::move(e)),poses_(std::move(p)),sources_(std::move(s)){}
void Cancel::fail()noexcept{if(fault_.exchange(true))return;try{if(sources_.quarantine)sources_.quarantine();}catch(...){} }
bool Cancel::native_cancel_scope()const noexcept{return scope_==this&&busy_&&!fault_&&attempted_&&!completion_.valid();}
bool Cancel::boundary(Cpu*c,Boundary&b){
 if(!sources_.boundary(c,b)||b.expected!=expected_||!b.retained||!b.fenced||!b.stage||!b.exclusive||!b.logic_step||!b.depth.serial||!b.depth.coverage||b.depth.fault||b.depth.state!=D::State::Active||b.depth.camera_depth!=1||b.depth.npc_depth||b.depth.manager_depth||b.depth.callback_depth||b.depth.foreign_depth)return false;
 const auto&p=b.parent;return p.authority==D::Authority::Owned&&p.entry==D::Entry::CameraExecute&&p.function==GC(0x024FFA3C)&&p.identity==expected_.identity&&expected_.camera.address>=0x248&&p.receiver==expected_.camera.address-0x248&&p.subject==p.receiver&&p.pid==expected_.camera.pid&&!p.token&&b.elapsed<expected_.program->watchdog();
}
bool Cancel::capture(State&s){
 if(!sources_.routes()||!sources_.read(expected_.identity.manager,s.manager)||!sources_.read(expected_.identity.image,s.image))return false;
 for(unsigned n=0;n<2;++n){if(!sources_.resident(expected_,n)||!sources_.read(expected_.actors[n].address,s.actors[n])||!sources_.resident(expected_,n))return false;
  std::array<uint8_t,0xC8>morf{};std::array<uint8_t,0xBC>model{};const auto&i=poses_[n].identity;
  if(w(s.actors[n],0x44C)!=i.morf||!sources_.read(i.morf,morf)||w(morf,0x90)!=i.model||!sources_.read(i.model,model)||w(model,0xB8)!=expected_.actors[n].address)return false;
 }
 return true;
}
bool Cancel::initial(const State&s)const{
 const auto&e=expected_;const auto&g=*e.program;
 if(!g.immutable_matches(s.image)||w(s.image,0xE4)!=2||w(s.manager,0x20)!=1||(w(s.manager,0x53C)!=0&&w(s.manager,0x53C)!=e.identity.image+0x40))return false;
 const auto pools=g.pool_offsets();for(unsigned n=0;n<8;++n)if(w(s.manager,n*4)!=e.identity.image+pools[n])return false;
 uint32_t mask=0;for(unsigned p=0;p<0x500;++p){const uint8_t allowed=p==3?0xFF:p==2?1:0;if(s.manager[0x3C+p]&~allowed)return false;}mask=w(s.manager,0x3C);
 std::array<uint32_t,4>cuts{};
 for(unsigned n=0;n<4;++n){unsigned p=0xF0+n*0x50;cuts[n]=w(s.image,p+0x38);const auto cut=g.cut(n,cuts[n]);const auto cache=w(s.image,p+0x3C);
  if(!cut||(cache!=~0u&&cache!=cut->action)||w(s.image,p+0x40)>cut->maximum_timer||s.image[p+0x44]||s.image[p+0x45]>1||s.image[p+0x46]>2||s.image[p+0x47]>1)return false;
 }
 auto flag=[&](unsigned n){return (mask&(1u<<n))!=0;};
 for(unsigned n=0;n<2;++n){if((cuts[n]==n&&!flag(7+n))||(flag(n)&&cuts[n]!=n))return false;const auto&a=e.actors[n];const auto&p=poses_[n];const auto&i=p.identity;
  if(i.actor!=a.address||i.pid!=a.pid||i.token!=a.token||i.stage!=e.identity.stage||i.room!=e.identity.room||i.generation!=e.identity.generation||p.cut!=a.cut||p.status&0x9800||!range({i.morf,0xC8})||!range({i.model,0xBC})||(i.morf&3)||(i.model&3)||w(s.actors[n],0x2E0)!=(p.status|0x8000))return false;
  const auto cut=std::span<const uint8_t>(s.actors[n]).subspan(0x3E0,0x6C);const auto chosen=g.cut(n,cuts[n]);
  const uint32_t action=w(cut,0x10),staff=w(cut,4),cache=w(s.image,0xF0+n*0x50+0x3C);
  if(w(cut,0)!=e.identity.image+0xF0+n*0x50||w(cut,8)!=a.address||(w(cut,0xC)&&w(cut,0xC)!=a.address)||(w(cut,0x68)&&w(cut,0x68)!=a.address+0x3AC)||cut[0x60]>1||cut[0x61]>1||!chosen||(action!=~0u&&action!=chosen->action)||(staff!=n&&!(staff==w(a.cut,4)&&action==~0u&&cache==~0u)))return false;
 }
 return !(cuts[2]==6&&!flag(2))&&!(flag(6)&&cuts[2]!=6)&&!(cuts[3]>=4&&(mask&0xB)!=0xB)&&!(cuts[3]==5&&(mask&0x50)!=0x50)&&!(flag(5)&&cuts[3]!=5);
}
bool Cancel::invoke(Cpu*c){
 I::Range arena;if(!sources_.arena(expected_,arena)||!range(arena)||arena.address>expected_.identity.image||uint64_t(arena.address)+arena.bytes<uint64_t(expected_.identity.image)+expected_.identity.bytes)return false;
 std::vector<I::Range> objects{{expected_.identity.manager-0xF4,0x634},{expected_.camera.address-0x248,0xB28}};
 for(unsigned n=0;n<2;++n){objects.push_back({expected_.actors[n].address,0x920});objects.push_back({poses_[n].identity.morf,0xC8});objects.push_back({poses_[n].identity.model,0x170});}
 for(unsigned n=0;n<objects.size();++n){if(!range(objects[n])||overlap(arena,objects[n]))return false;for(unsigned k=0;k<n;++k)if(overlap(objects[n],objects[k]))return false;}
 I::Config config;if(!sources_.stack(c,config)||!range(config.stack)||(c->r[1]&7)||c->r[1]<I::kFrameBytes||config.protected_ranges.empty())return false;
 const I::Range frame{c->r[1]-I::kFrameBytes,I::kFrameBytes};if(frame.address<config.stack.address||uint64_t(frame.address)+frame.bytes>uint64_t(config.stack.address)+config.stack.bytes)return false;
 std::vector<I::Range> protected_ranges=config.protected_ranges;protected_ranges.push_back(arena);protected_ranges.insert(protected_ranges.end(),objects.begin(),objects.end());
 for(auto p:protected_ranges)if(!range(p)||overlap(frame,p))return false;
 const Cpu saved=*c;std::array<uint8_t,I::kFrameBytes> bytes{};try{if(!sources_.read(frame.address,bytes)){*c=saved;return false;}}catch(...){*c=saved;return false;}
 bool called=false,restored=false;attempted_=true;active_cpu_=c;scope_=this;
 try{sources_.call(c,GC(0x0254465C),{expected_.identity.manager,0,1});called=true;}catch(...){}
 scope_=nullptr;active_cpu_=nullptr;
 try{restored=sources_.write(frame.address,bytes);std::array<uint8_t,I::kFrameBytes>check{};restored=restored&&sources_.read(frame.address,check)&&check==bytes;}catch(...){restored=false;}
 *c=saved;return called&&restored&&!fault_;
}
uint32_t Cancel::route_all_off(uint32_t actor,const std::function<uint32_t()>&original){
 if(scope_!=this)return original();
 if(!native_cancel_scope())return 0;
 unsigned n=0;for(;n<2;++n)if(expected_.actors[n].address==actor)break;if(n==2)return 0;
 try{Boundary current;if(!boundary(active_cpu_,current)||!same(current,invoking_boundary_)||seen_[n]||!original||!sources_.resident(expected_,n)){fail();return 0;}seen_[n]=true;
  std::array<uint8_t,0x920>before{},after{};if(!sources_.read(actor,before)||before!=pre_.actors[n]){fail();return 0;}
  const uint32_t result=original();auto desired=before;put(desired,0x2E0,w(before,0x2E0)&~0x9800u);
  if(result||!sources_.read(actor,after)||after!=desired||!sources_.resident(expected_,n))fail();
 }catch(...){fail();}return 0;
}
Result Cancel::run(Cpu*c){
 if(fault_)return Result::Quarantined;
 if(!c||!expected_.program||expected_.program->kind()!=story_native_event_program::Kind::TurnCamera||expected_.identity.bytes!=0xE60||!sources_.boundary||!sources_.resident||!sources_.routes||!sources_.arena||!sources_.stack||!sources_.read||!sources_.write||!sources_.call||!sources_.quarantine)return Result::Refused;
 const auto&i=expected_.identity;
 if(!i.stage||!i.room||!i.generation||!i.program||i.manager<0xF4||!range({i.manager-0xF4,0x634})||!range({i.image,i.bytes})||(i.image&15)||expected_.frames!=expected_.program->frames()||expected_.camera.address<0x248||!range({expected_.camera.address-0x248,0xB28})||expected_.actors[0].address==expected_.actors[1].address)return Result::Refused;
 for(const auto&a:expected_.actors)if(!range({a.address,0x920})||!a.pid||!a.token||a.profile!=0x175||a.subtype!=1||std::all_of(a.fingerprint.begin(),a.fingerprint.end(),[](uint8_t v){return !v;}))return Result::Refused;
 // Authenticate the actual designated thread before touching mutable state.
 try{Boundary admission;I::Config stack;if(scope_||!boundary(c,admission)||!sources_.stack(c,stack))return Result::Refused;}catch(...){return Result::Refused;}
 if(busy_.exchange(true)){fail();return Result::Quarantined;}struct Reset{std::atomic<bool>&b;~Reset(){b=false;}}reset{busy_};
 if(attempted_||completion_.valid())return Result::Refused;
 try{Boundary before,after;State check,post;if(!boundary(c,before)||!capture(pre_)||!initial(pre_)||!capture(check)||pre_.manager!=check.manager||pre_.image!=check.image||pre_.actors!=check.actors||!boundary(c,after)||!same(before,after))return Result::Refused;
  const uint64_t receipt_serial=next_serial();if(!receipt_serial)return Result::Refused;
  invoking_boundary_=before;
  if(!invoke(c)){if(attempted_)fail();return fault_?Result::Quarantined:Result::Refused;}
  if(!seen_[0]||!seen_[1]||!boundary(c,after)||!same(before,after)||!capture(post)){fail();return Result::Quarantined;}
  State desired=pre_;put(desired.manager,0x20,0);put(desired.image,0xE4,0);for(unsigned n=0;n<2;++n)put(desired.actors[n],0x2E0,w(desired.actors[n],0x2E0)&~0x9800u);
  if(post.manager!=desired.manager||post.image!=desired.image||post.actors!=desired.actors||!capture(check)||post.manager!=check.manager||post.image!=check.image||post.actors!=check.actors||!boundary(c,after)||!same(before,after)){fail();return Result::Quarantined;}
  completion_.expected_=expected_;completion_.baseline_=poses_;completion_.before_=before;completion_.after_=after;completion_.pre_=pre_;completion_.post_=post;completion_.serial_=receipt_serial;return Result::Completed;
 }catch(...){scope_=nullptr;active_cpu_=nullptr;if(attempted_){fail();return Result::Quarantined;}return Result::Refused;}
}
}
