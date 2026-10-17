// SPDX-License-Identifier: MPL-2.0
#include "story_particles.h"
#include "guest_addr.h"
#include "runtime.h"
#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
namespace story_particles {namespace {
const uint32_t GetPlay=GC(0x025200D4),SetParticle=GC(0x025A847C);
const uint32_t ManagerGlobal=GD(0x1047B2D4);
Invoke invoke=nullptr;uint32_t scratch=0,mask=0;bool running=false;
bool mem2(uint32_t p,uint32_t n){return !(p&3)&&n&&n<=mem::kMem2End-mem::kMem2Start&&p>=mem::kMem2Start&&p<=mem::kMem2End-n;}
bool runtime_span(uint32_t p,uint32_t n){return !(p&3)&&n&&n<=mem::kHostStart-mem::kRuntimeStart&&p>=mem::kRuntimeStart&&p<=mem::kHostStart-n;}
bool guest_stack(uint32_t p,uint32_t n){return mem2(p,n)||runtime_span(p,n);}
uint32_t bit(Effect e){switch(e){case Effect::Dust:return 0;case Effect::DoorDebris:return kDoorDebrisMask;case Effect::AuctionSparkle:return kAuctionSparkleMask;case Effect::BeaconSparkle:return kBeaconSparkleMask;}return 0;}
uint16_t id(Effect e){switch(e){case Effect::Dust:return 0x2022;case Effect::DoorDebris:return 0x81B2;case Effect::AuctionSparkle:return 0x8153;case Effect::BeaconSparkle:return 0x8152;}return 0;}
struct KeepCpu {Cpu*c;Cpu saved;explicit KeepCpu(Cpu*p):c(p),saved(*p){}~KeepCpu(){*c=saved;}};
struct Busy {Busy(){running=true;}~Busy(){running=false;}};
struct KeepBytes {uint32_t at;std::array<uint8_t,kScratchBytes> bytes;explicit KeepBytes(uint32_t p):at(p){std::memcpy(bytes.data(),mem::ptr(p),bytes.size());}~KeepBytes(){std::memcpy(mem::ptr(at),bytes.data(),bytes.size());}};
// 02821448 dereferences bank wrapper before checking the requested resource.
// Validate its complete bounded ID table first; 02826FC4 is only an existence
// test and would otherwise follow unchecked pointers itself.
bool resource_available(uint32_t manager,uint16_t effect){
 const uint32_t bank=(effect>>15)&1;uint32_t wrapper=0,table=0,count=0,entries=0;unsigned matches=0;
 auto finish=[&](const char*reason,bool result){
  static unsigned traces=0;
  if(std::getenv("WWHD_PARTICLE_TRACE")&&traces++<16)
   log_msg("[particles] preflight id=%04X bank=%u manager=%08X wrapper=%08X table=%08X count=%u matches=%u reason=%s\n",effect,bank,manager,wrapper,table,count,matches,reason);
  return result;
 };
 if(!mem2(manager,0x40))return finish("manager",false);
 wrapper=ld32(manager+0x30+4*bank);if(!mem2(wrapper,12))return finish("bank-wrapper",false);
 table=ld32(wrapper+4);if(!mem2(table,12))return finish("resource-table",false);
 count=ld32(table);entries=ld32(table+8);
 if(!count||count>4096)return finish("resource-count",false);
 if(!mem2(entries,count*4))return finish("resource-entries",false);
 for(uint32_t i=0;i<count;++i){const uint32_t entry=ld32(entries+i*4);if(!mem2(entry,8))return finish("resource-entry",false);if(ld16(entry+6)==effect)++matches;}
 return finish(matches==1?"present":matches?"duplicate-id":"missing-id",matches==1);
}
}
bool configure(Invoke i,uint32_t s,uint32_t bytes,uint32_t admission){
 if(!i||bytes<kScratchBytes||!runtime_span(s,kScratchBytes)||(admission&~kAllEffectsMask))return false;
 if(invoke&&(invoke!=i||scratch!=s||mask!=admission))return false;
 invoke=i;scratch=s;mask=admission;return true;
}
bool from_name(std::string_view text,Effect&out){if(text=="door-debris"){out=Effect::DoorDebris;return true;}if(text=="auction-sparkle"){out=Effect::AuctionSparkle;return true;}if(text=="beacon-sparkle"){out=Effect::BeaconSparkle;return true;}return false;}
std::string_view name(Effect e){switch(e){case Effect::Dust:return {};case Effect::DoorDebris:return "door-debris";case Effect::AuctionSparkle:return "auction-sparkle";case Effect::BeaconSparkle:return "beacon-sparkle";}return {};}
bool admitted(Effect e){return invoke&&(mask&bit(e))!=0;}
bool valid(const Burst&b){return bit(b.effect)&&std::isfinite(b.position.x)&&std::isfinite(b.position.y)&&std::isfinite(b.position.z)&&std::fabs(b.position.x)<=1000000&&std::fabs(b.position.y)<=1000000&&std::fabs(b.position.z)<=1000000&&std::isfinite(b.scale)&&b.scale>=0.1f&&b.scale<=4&&b.alpha<=255&&b.room>=-1&&b.room<=63;}
bool burst(Cpu*c,const Burst&b){
 if(!c||!invoke||running||!admitted(b.effect)||!valid(b)||(c->r[1]&15)||c->r[1]<0x40||!guest_stack(c->r[1]-0x40,0x40))return false;
 const uint32_t frame=c->r[1]-0x40;
 if(scratch<frame+0x40&&frame<scratch+kScratchBytes)return false;
 KeepCpu cpu(c);Busy busy;KeepBytes saved_scratch(scratch);
 // Restore all outgoing frame bytes, including native guest_call's backchain.
 std::array<uint8_t,0x40> old_frame{};std::memcpy(old_frame.data(),mem::ptr(frame),old_frame.size());
 struct RestoreFrame{uint32_t at;std::array<uint8_t,0x40>&bytes;~RestoreFrame(){std::memcpy(mem::ptr(at),bytes.data(),bytes.size());}} restore_frame{frame,old_frame};
 try{
  const uint32_t play=invoke(c,GetPlay,{});if(!mem2(play,0x5AB4))return false;
  const uint32_t control=ld32(play+0x5AB0),manager=ld32(ManagerGlobal);
  if(!mem2(control,0x41A4)||!resource_available(manager,id(b.effect)))return false;
  const float values[]={b.position.x,b.position.y,b.position.z,b.scale,b.scale,b.scale};
  for(unsigned i=0;i<6;++i)st32(scratch+i*4,f32_as_u32(values[i]));
  // guest_call preserves its CPU caller. Use the originally validated SP even
  // if the bridge's GetPlay implementation left temporary returned registers.
  *c=cpu.saved;
  st32(frame+8,uint32_t(b.room));st32(frame+12,0);st32(frame+16,0);st32(frame+20,0);
  const uint32_t emitter=invoke(c,SetParticle,{control,0,id(b.effect),scratch,0,scratch+12,b.alpha,0});
  const bool span=mem2(emitter,0x264);
  const uint32_t actual_id=span?ld16(emitter+0x260):0,group=span?ld8(emitter+0x262):0,bank=span?ld8(emitter+0x263):0,callback=span?ld32(emitter+0x1E4):0;
  const bool accepted=span&&actual_id==id(b.effect)&&group==0&&bank==((id(b.effect)>>15)&1)&&callback==0;
  static unsigned result_traces=0;
  if(std::getenv("WWHD_PARTICLE_TRACE")&&result_traces++<16)
   log_msg("[particles] result emitter=%08X id=%04X bank=%u group=%u callback=%08X reason=%s\n",emitter,actual_id,bank,group,callback,!span?(emitter?"invalid-span":"null"):accepted?"accepted":"metadata");
  return accepted;
 }catch(...){return false;}
}
bool burst_named(Cpu*c,std::string_view effect,const Placement&placement,Options options){
 Effect parsed=Effect::Dust;
 if(!from_name(effect,parsed)||!admitted(parsed))return false;
 return burst(c,{parsed,placement.position,options.scale,options.alpha,placement.room});
}
}
