// SPDX-License-Identifier: MPL-2.0
#include "story_native_camera_return.h"
#include "guest_addr.h"
#include "runtime.h"
#include <utility>
namespace story_native_camera_return {namespace {
uint32_t word(const std::array<uint8_t,0x114>&b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool in_image(uint32_t p,const Identity&i){return uint64_t(p)>=i.image&&uint64_t(p)<uint64_t(i.image)+i.bytes;}
}
Barrier::Barrier(Identity i,Camera c,Sources s):expected_(i),baseline_(c),sources_(std::move(s)){}
bool Barrier::arm(const story_native_manager_terminal::Completion&completion){
 std::lock_guard lock(arm_mutex_);
 if(terminal_serial_||quarantined_||!completion.valid()||completion.lease()!=expected_||!expected_.manager||!expected_.image||
    !expected_.bytes||!expected_.generation||!expected_.program||!expected_.stage||
    !expected_.room||uint64_t(expected_.image)+expected_.bytes>0x100000000ull||
    !baseline_.address||(baseline_.address&3)||!baseline_.pid||baseline_.pid==~0u||
    baseline_.current_type!=baseline_.next_type||baseline_.current_type==baseline_.event_type||
    baseline_.flags&0x20000000u||
    word(baseline_.event_data,12)!=~0u||!sources_.capture||!sources_.verify_camera||
    !sources_.owned_thread||!sources_.route)return false;
 try{if(!sources_.verify_camera(baseline_,expected_)||!sources_.route())return false;}
 catch(...){return false;}
 completion_=completion;terminal_serial_.store(completion.serial(),std::memory_order_release);return true;
}
bool Barrier::matches_terminal(const story_native_manager_terminal::Terminal&t)const{
 return terminal_serial_.load(std::memory_order_acquire)&&returned()&&completion_==t.completion();
}
bool Barrier::verify_fresh(const Cpu*cpu,const Identity&i,const Camera&camera)const{
 if(!returned()||i!=expected_||camera.current_type!=baseline_.current_type||
    camera.next_type!=baseline_.current_type||word(camera.event_data,12)!=~0u)return false;
 try{
  Capture c;return cpu&&sources_.owned_thread(cpu)&&sources_.route()&&
   sources_.capture(cpu,c)&&c.camera==camera&&check(c);
 }catch(...){return false;}
}
bool Barrier::check(const Capture&c)const{
 if(c.identity!=expected_||!c.retained||!c.exclusive||c.foreign||
    c.terminal_serial!=terminal_serial_||c.event_state!=0||c.camera_play!=0||
    c.camera.address!=baseline_.address||c.camera.pid!=baseline_.pid||
    c.camera.event_type!=baseline_.event_type||c.camera.flags&0x20000000u)return false;
 // WAIT owns no stage/spline pointers. A typed return alone cannot clear a
 // pointer cached by another camera action; such a case stays retained.
 for(unsigned n=0;n<8;++n)if(in_image(word(c.camera.event_data,0x3C+n*0x14),expected_))return false;
 if(in_image(word(c.camera.event_data,0xCC),expected_))return false;
 return sources_.verify_camera(c.camera,expected_);
}
Decision Barrier::run(Cpu*c,uint32_t function,const std::function<void()>&original){
 const uint32_t mapped=GC_OPTIONAL(0x024FE3E8);
 if(!c||!mapped||function!=mapped||c->r[3]!=baseline_.address||!terminal_serial_||returned_)
  return Decision::Delegate;
 if(quarantined_)return Decision::Quarantined;
 auto fail=[&]{quarantined_=true;return Decision::Quarantined;};
 bool idle=false;if(!running_.compare_exchange_strong(idle,true))return fail();
 struct Running {std::atomic<bool>&flag;~Running(){flag=false;}} running{running_};
 bool original_fault=false;
 try{
  if(!original||!sources_.owned_thread(c)||!sources_.route())return fail();
  Capture before;if(!sources_.capture(c,before)||!check(before)||
     before.camera.current_type!=baseline_.event_type||
     before.camera.next_type!=baseline_.event_type||
     word(before.camera.event_data,12)!=baseline_.current_type)return fail();
  // Never hold a host mutex across Run: its owner-thread admission and retained
  // controller prevent publication changes; the outer depth tracker observes it.
  try{original();}catch(...){original_fault=true;throw;}
  Capture after;if(quarantined_||!sources_.capture(c,after)||!check(after))return fail();
  if(after.camera.current_type==baseline_.event_type){
   // A gated or rejected native type transition can legitimately need another
   // frame. Once nextType consumed the return, staying Event is hazardous.
   if(after.camera.next_type!=baseline_.event_type||
      word(after.camera.event_data,12)!=baseline_.current_type)return fail();
   return Decision::Observed;
  }
  if(after.camera.current_type!=baseline_.current_type||
     after.camera.next_type!=baseline_.current_type||word(after.camera.event_data,12)!=~0u)
   return fail();
  returned_=true;return Decision::Observed;
 }catch(...){quarantined_=true;if(original_fault)throw;return Decision::Quarantined;}
}
}
