// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_memory.h"
#include "story_native_restore_resume.h"
#include "runtime.h"
namespace story_native_event_memory {
bool Policy::prepare_restore_open(const story_native_event_observation::Result&r,const story_native_restore_resume::Boundary&b,Cpu*c){
 try{Scope s;return phase_==Phase::Restored&&r.restorable()&&r.matches(expected_)&&b.current(c)&&b.completion().expected()==expected_&&scope(s,false)&&s.fenced&&s.logic_step==logic_step_&&s.serial==serial_&&!s.manager_depth&&!s.camera_depth&&!s.npc_depth&&!s.callback_depth&&!s.foreign_depth&&b.current(c);}catch(...){return false;}
}
void Policy::open_restored_unchecked()noexcept{phase_=Phase::Running;error_=Error::None;}
bool Policy::adopt_restored(const story_native_event_observation::Result&r,const story_native_restore_resume::Boundary&b,Cpu*c){
 try{Scope s;const auto saved=b.completion().origin().session_baseline();
  if(phase_!=Phase::Idle||!c||!b.current(c)||!saved||!r.restorable()||!r.matches(expected_)||b.completion().expected()!=expected_||
   !scope(s,false)||!s.fenced||s.manager_depth||s.camera_depth||s.npc_depth||s.callback_depth||s.foreign_depth||
   !b.current(c))return fail(Error::Ownership);
  logic_step_=s.logic_step;serial_=s.serial;phase_=Phase::Restored;error_=Error::None;return true;
 }catch(...){return fail(Error::Ownership);}
}
}
