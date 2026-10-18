// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_install.h"
#include "guest_addr.h"
#include "runtime.h"
#include <utility>
namespace story_native_event_install {namespace {
const uint32_t OrderControl=GC(0x0253EC0C),CheckStart=GC(0x0253FD1C),EndControl=GC(0x0253EFA8),ClearBase=GC(0x0254273C),SetData=GC(0x025443B4),OrderEvent=GC(0x02544534),CutInfo=GC(0x0259F7D4);
uint32_t word(std::span<const uint8_t>b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool owned_control(std::span<const uint8_t>b){return b[0xC0]==0&&b[0xC2]==3&&word(b,0xC4)==0xFFFFFFFFu&&word(b,0xC8)==0xFFFFFFFFu&&word(b,0xCC)==0xFFFFFFFFu&&word(b,0xD0)==0xFFFFFFFFu&&b[0xD6]==0&&b[0xD7]==0&&b[0xD8]==0xFF&&b[0xD9]==0xFF&&b[0xDE]==0xFF&&b[0xDF]==0xFF&&b[0xE8]==0&&b[0xE9]==0;}
struct CpuGuard {Cpu*c;Cpu saved;explicit CpuGuard(Cpu*p):c(p),saved(*p){}~CpuGuard(){*c=saved;}};
bool identity(const Expected&e,const Captured&c){const auto&a=e.identity;const auto&b=c.identity;return a.manager==b.manager&&a.image==b.image&&a.bytes==b.bytes&&a.stage==b.stage&&a.room==b.room&&a.generation==b.generation&&a.program==b.program&&e.stage_name==c.stage_name&&e.room_number==c.room_number&&c.resource_generation_matches&&!c.foreign&&!c.manager_depth&&!c.camera_depth&&!c.npc_depth;}
}
Install::Install(Backend b,Expected e):backend_(std::move(b)),expected_(std::move(e)){}
bool Install::write_word(uint32_t p,uint32_t value){std::array<uint8_t,4>b{};for(unsigned n=0;n<4;++n)b[n]=uint8_t(value>>(24-8*n));return backend_.write(p,b);}
bool Install::capture(Cpu*c,Captured&o,bool idle){
 if(!backend_.scope(expected_)||!backend_.capture(c,o))return false;
 std::array<uint8_t,0xF4> bytes{};if(!backend_.read(control(),bytes))return false;
 // Actual native control scalar ABI, independently checked rather than trusting
 // collector booleans. Mode3 has null-partner PID sentinels and no event ID.
 o.control_owned=owned_control(bytes);
 o.control_released=bytes[0xC0]==0&&bytes[0xC2]==0;
 return idle?o.control_released:o.control_owned;
}
bool Install::restore_manager(){
 for(unsigned n=0;n<8;++n)if(!write_word(expected_.identity.manager+n*4,expected_.retail_manager[n]))return false;
 return write_word(expected_.identity.manager+0x20,expected_.retail_camera_play)&&write_word(expected_.identity.manager+0x53C,expected_.retail_current_event)&&backend_.write(expected_.identity.manager+0x3C,expected_.retail_flags);
}
bool Install::rollback(Cpu*c,const std::array<uint8_t,0xF4>&before,bool bound)noexcept{
 try{
  Captured o;if(!backend_.scope(expected_)||!backend_.capture(c,o)||!identity(expected_,o))return false;
  // Before engine resume only known clear/set-data pointer writes are allowed.
  std::array<uint32_t,8> published={expected_.identity.image,expected_.identity.image+0x40,expected_.identity.image+0xF0,expected_.identity.image+0x230,expected_.identity.image+0x410,0,expected_.identity.image+0x510,0};if(expected_.program){const auto offsets=expected_.program->pool_offsets();for(unsigned n=0;n<8;++n)published[n]=offsets[n]?expected_.identity.image+offsets[n]:n?0:expected_.identity.image;}
  for(unsigned n=0;n<8;++n)if(o.manager[n]&&o.manager[n]!=published[n]&&o.manager[n]!=expected_.retail_manager[n])return false;
  std::array<uint8_t,0xF4> current{};if(!backend_.read(control(),current))return false;
  // Never restore a control takeover even if a faulty collector omitted foreign.
  const bool owned=owned_control(current);
  const bool queued=current[0xC0]==1&&current[0xC1]==0&&current[0xC2]==0&&current[0]==0&&current[1]==3&&current[2]==0&&current[3]==0&&current[4]==0&&current[5]==0&&word(current,8)==0&&word(current,12)==0&&current[16]==0xFF&&current[17]==0xFF&&current[18]==0&&current[19]==1&&current[20]==0xFF&&current[21]==0xFF;
  if(current!=before&&!owned&&!queued)return false;
  if(bound&&!backend_.detach(c,expected_))return false;
  // Synchronous pre-resume rollback: restore only our just-modified control
  // allocation and manager fields. No calls to end_proc/ALL/global event reset.
  if(!backend_.write(control(),before)||!restore_manager())return false;
  if(!capture(c,o,true)||!validate(Operation::Release,expected_,o).released())return false;
  return true;
 }catch(...){return false;}
}
Status Install::start(Cpu*c){
 if(status_!=Status::Empty)return status_;
 if(!c||!backend_.invoke||!backend_.read||!backend_.write||!backend_.capture||!backend_.scope||!backend_.detach)return status_=Status::Refused;
 CpuGuard guard(c);std::array<uint8_t,0xF4> before{};bool touched=false,bound=false;
 try{
  const auto&i=expected_.identity;
  // Only the audited People/Uo2 initial fixture layout is admitted here.
  if(i.manager<mem::kMem2Start+0xF4||uint64_t(i.manager)+0x540>mem::kMem2End||i.image<mem::kRuntimeStart||uint64_t(i.image)+i.bytes>mem::kHostStart||!backend_.scope(expected_))return status_=Status::Refused;
  for(const auto&a:expected_.actors)if(a.address<mem::kMem2Start||uint64_t(a.address)+0xA24>mem::kMem2End||a.profile!=0x175||a.subtype!=1)return status_=Status::Refused;
  // The caller must have persisted Preparing and installed/read back the
  // canonical image under its entry fence. This helper may not write image
  // bytes before it has an independently validated Prepare observation.
  Captured o;if(!capture(c,o,true))return status_=Status::Refused;const auto prepared=validate(Operation::Prepare,expected_,o);if(!prepared.valid()||!backend_.read(control(),before)||!lease_.prepare(prepared))return status_=Status::Refused;
  touched=true;
  // Ninth native argument is u8 infoFF in the low byte of stack outgoing+8.
  if(!backend_.invoke(c,OrderControl,{control(),3,1,0,0,0,0,0xFFFFFFFFu},{0xFF})||!backend_.invoke(c,CheckStart,{control()},{}))throw false;
  if(!capture(c,o,false)||!identity(expected_,o))throw false;
  backend_.invoke(c,ClearBase,{i.manager},{});backend_.invoke(c,SetData,{i.manager,i.image},{});
  if(!capture(c,o,false)||!validate(Operation::Publish,expected_,o).valid()||!lease_.publish({i,false,false}))throw false;
  bound=true;
  for(unsigned n=0;n<2;++n)backend_.invoke(c,CutInfo,{expected_.actors[n].address+0x3E0,i.image+0xF0+n*0x50,expected_.actors[n].address},{});
  if(!backend_.invoke(c,OrderEvent,{i.manager,0},{}))throw false;
  if(!capture(c,o,false)||!validate(Operation::Publish,expected_,o).valid())throw false;
  return status_=Status::Started;
 }catch(...){const bool clean=touched&&rollback(c,before,bound);lease_.invalidate();return status_=clean?Status::RolledBack:Status::Quarantined;}
}
Status Install::rehydrate(const Result&r,story_native_event_lease::Phase phase){
 try{
 using story_native_event_lease::Phase;
 const bool callbacks=backend_.invoke&&backend_.read&&backend_.write&&backend_.capture&&backend_.scope&&backend_.detach;
 const uint32_t state=r.event_state();
 const bool coherent=(phase==Phase::Published&&state==1)||(phase==Phase::Running&&(state==2||state==4))||(phase==Phase::Draining&&(state==1||state==2||state==4));
 if(status_!=Status::Empty||!callbacks||!r.restorable()||!r.matches(expected_)||!coherent||!backend_.scope(expected_)||!lease_.prepare_restore(r)||!lease_.restore(expected_.identity,phase,{expected_.identity,false,false})||!backend_.scope(expected_)){
  lease_.invalidate();return status_=Status::Quarantined;
 }
 return status_=phase==Phase::Published?Status::Started:phase==Phase::Running?Status::Running:Status::Draining;
 }catch(...){lease_.invalidate();return status_=Status::Quarantined;}
}
Status Install::observe(Cpu*c){
 if(!c||(status_!=Status::Started&&status_!=Status::Running))return status_;
 CpuGuard guard(c);try{Captured o;if(!capture(c,o,false))throw false;
  const uint32_t state=o.image.size()==expected_.identity.bytes?word(o.image,0xE4):0xFFFFFFFFu;
  if(!validate(state==1?Operation::Publish:Operation::Running,expected_,o).valid())throw false;
  if(state>=2&&status_==Status::Started){if(!lease_.started({expected_.identity,false,false}))throw false;status_=Status::Running;}
  return status_;
 }catch(...){lease_.invalidate();return status_=Status::Quarantined;}
}
Status Install::stop(Cpu*c,bool completed){
 if(!c||(status_!=Status::Started&&status_!=Status::Running&&status_!=Status::Draining))return status_;
 CpuGuard guard(c);try{Captured o;if(!capture(c,o,false))throw false;
  const uint32_t state=o.image.size()==expected_.identity.bytes?word(o.image,0xE4):0xFFFFFFFFu;
  if(state>=2)throw false; // native consumers require stop_terminal receipts
  if(!validate(state==1?Operation::Publish:Operation::Running,expected_,o).valid()||(completed&&(state!=4||!(o.flags[3]&0x20))))throw false;
  if((status_!=Status::Draining&&!lease_.finish({expected_.identity,false,false}))||!backend_.detach(c,expected_))throw false;
  if(!capture(c,o,false)||!validate(Operation::Drain,expected_,o).valid())throw false;
  // Only verified owned mode3: native end does not invoke demoEnd/ALL here.
  if(!backend_.invoke(c,EndControl,{control()},{}))throw false;
  if(!capture(c,o,true)||!identity(expected_,o))throw false;
  backend_.invoke(c,ClearBase,{expected_.identity.manager},{});
  if(!restore_manager()||!capture(c,o,true))throw false;
  auto released=validate(Operation::Release,expected_,o);
  if(!released.released()||!lease_.retire({expected_.identity,false,true})||!lease_.can_free(released)||!lease_.can_restore_retail(released))throw false;
  return status_=Status::Released;
 }catch(...){lease_.invalidate();return status_=Status::Quarantined;}
}
Status Install::stop_terminal(Cpu*c,const story_native_manager_terminal::Terminal&terminal,
                             const story_native_camera_return::Barrier&camera){
 if(!c||(status_!=Status::Started&&status_!=Status::Running&&status_!=Status::Draining))return status_;
 CpuGuard guard(c);try{
  if(!terminal.completed()||!terminal.matches(expected_)||!camera.matches_terminal(terminal)||!camera.matches(expected_))throw false;
  Captured o;if(!capture(c,o,false)||!validate(Operation::Terminal,expected_,o).valid()||
     !camera.verify_fresh(c,o.identity,o.camera))throw false;
  if(status_!=Status::Draining&&!lease_.finish({expected_.identity,false,false}))throw false;
  status_=Status::Draining;
  if(!backend_.detach(c,expected_)||!capture(c,o,false)||
     !validate(Operation::TerminalDrain,expected_,o).valid())throw false;
  // Manager endProc and real camera Run already completed; mode3 control end
  // now clears only our control state, without demoEnd or a second ALL callback.
  if(!backend_.invoke(c,EndControl,{control()},{}))throw false;
  if(!capture(c,o,true)||!identity(expected_,o))throw false;
  backend_.invoke(c,ClearBase,{expected_.identity.manager},{});
  if(!restore_manager()||!capture(c,o,true))throw false;
  const auto released=validate(Operation::ReleaseTerminal,expected_,o);
  if(!released.released()||!lease_.retire({expected_.identity,false,true})||
     !lease_.can_free(released)||!lease_.can_restore_retail(released))throw false;
  return status_=Status::Released;
 }catch(...){lease_.invalidate();return status_=Status::Quarantined;}
}
}
