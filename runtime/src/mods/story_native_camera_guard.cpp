// SPDX-License-Identifier: MPL-2.0
#include "story_native_camera_guard.h"
namespace story_native_camera_guard {namespace {
bool identity(const story_native_event_lease::Identity&a,const story_native_event_lease::Identity&b){return a.manager==b.manager&&a.image==b.image&&a.bytes==b.bytes&&a.stage==b.stage&&a.room==b.room&&a.generation==b.generation&&a.program==b.program;}
bool defined(const Snapshot&s){const auto&i=s.identity;return i.manager&&i.image&&i.bytes&&i.stage&&i.room&&i.generation&&i.program&&s.camera&&!(s.camera&3)&&s.pid&&s.pid!=0xFFFFFFFFu;}
bool cache(const Snapshot&s){
 if(!s.style_table||(s.style_table&3)||!s.style_count||s.style_count>4096||s.tuple.style<0||uint32_t(s.tuple.style)>=s.style_count||s.param_index!=s.tuple.style)return false;
 const uint64_t expected=uint64_t(s.style_table)+uint64_t(s.param_index)*0x84;
 return expected<=0xFFFFFFFFu&&s.param_pointer==expected;
}
bool entered(const Tuple&b,const Tuple&t){return t.event_type==b.event_type&&t.current_type==b.event_type&&(t.next_type==b.event_type||t.next_type==b.next_type)&&t.saved_return==b.current_type;}
}
Result evaluate(Phase phase,const Snapshot&b,const Snapshot&c,const Guards&g,VerifyTuple verify){
 Result result;auto reject=[&](Reason r){result.reason_=r;return result;};
 if(!defined(b)||!defined(c)||!identity(b.identity,c.identity)||b.camera!=c.camera||b.pid!=c.pid)return reject(Reason::Identity);
 if(!g.exclusive||g.foreign)return reject(Reason::Ownership);
 if(g.manager_depth||g.camera_depth||g.npc_depth)return reject(Reason::Busy);
 if(!verify||b.style_table!=c.style_table||b.style_count!=c.style_count||!cache(b)||!cache(c))return reject(Reason::NativeTuple);
 try{if(!verify(b.tuple)||!verify(c.tuple))return reject(Reason::NativeTuple);}catch(...){return reject(Reason::NativeTuple);}
 if((b.flags|c.flags)&0x20000000u)return reject(Reason::Ownership); // explicit/debug camera route is not this lease
 const auto&original=b.tuple;const auto&live=c.tuple;
 if(original.current_type!=original.next_type||original.current_type==original.event_type||original.saved_return!=-1||original.current_mode!=original.next_mode)return reject(Reason::Baseline);
 if(live.event_type!=original.event_type)return reject(Reason::Transition);
 const bool untouched=live==original,active=entered(original,live);
 // Returning -1 while current remains Event is the concrete nextType408 hazard.
 if(live.current_type==live.event_type&&live.saved_return<0)return reject(Reason::ReturnHazard);
 switch(phase){
 case Phase::BeforePublish:case Phase::VerifyDetached:
  if(g.manager_owned||!g.manager_detached)return reject(Reason::Manager);
  if(!untouched)return reject(Reason::Transition);break;
 case Phase::Published:
  if(!g.manager_owned||g.manager_detached)return reject(Reason::Manager);
  if(!untouched&&!active)return reject(Reason::Transition);break;
 case Phase::Running:
  if(!g.manager_owned||g.manager_detached)return reject(Reason::Manager);
  if(!active)return reject(Reason::Transition);break;
 case Phase::PlanDetach:
  if(!g.manager_owned||g.manager_detached)return reject(Reason::Manager);
  if(!untouched&&!active)return reject(Reason::Transition);
  // Source: CurType51C/NextType520, saved return408; style514;
  // actual modes13C/140 and gate144. 518 is a run gate, not CurMode.
  result.writes_={{{0x51C,uint32_t(original.current_type)},{0x520,uint32_t(original.next_type)},{0x408,uint32_t(original.saved_return)},
   {0x514,uint32_t(original.style)},{0x13C,uint32_t(original.current_mode)},{0x140,uint32_t(original.next_mode)},
   {0x144,original.mode_gate},{0x518,original.run_gate},{0x114,original.type_counter},
   {0x8A8,b.param_pointer},{0x8AC,uint32_t(b.param_index)}}};
  result.restore_=true;break;
 }
 result.valid_=true;return result;
}
}
