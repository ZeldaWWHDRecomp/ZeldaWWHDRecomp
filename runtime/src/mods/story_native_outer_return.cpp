// SPDX-License-Identifier: MPL-2.0
#include "story_native_outer_return.h"
#include "guest_addr.h"
namespace story_native_outer_return {
thread_local const OuterReturn*OuterReturn::active_=nullptr;
OuterReturn::OuterReturn(Cpu*c,story_native_event_depth::Tracker&t,story_native_event_depth::FrameWitness w):witness_(std::move(w)),cpu_(c),thread_(std::this_thread::get_id()){
 const auto&call=witness_.root_call();const auto&e=witness_.expected();
 if(!c||active_||!witness_.returned(t)||call.entry!=story_native_event_depth::Entry::CameraExecute||call.function!=GC(0x024FFA3C)||call.authority!=story_native_event_depth::Authority::Owned||call.identity!=e.identity||e.camera.address<0x248||call.receiver!=e.camera.address-0x248||call.subject!=call.receiver||call.pid!=e.camera.pid||call.token||!witness_.claim_outer_return(t))return;
 tracker_=&t;active_=this;activated_=true;
}
OuterReturn::~OuterReturn(){if(active_==this)active_=nullptr;}
bool OuterReturn::current(const Cpu*c,const story_native_event_observation::Expected&e)const{
 return activated_&&active_==this&&c==cpu_&&thread_==std::this_thread::get_id()&&witness_.expected()==e&&witness_.returned_now(true);
}
bool OuterReturn::claim(const Cpu*c,const story_native_event_observation::Expected&e)const{
 if(!current(c,e))return false;bool no=false;return claimed_.compare_exchange_strong(no,true);
}
}
