// SPDX-License-Identifier: MPL-2.0
#include "story_native_turn_camera_observation.h"
#include <algorithm>
namespace story_native_turn_camera_observation {namespace {
uint32_t word(std::span<const uint8_t>b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool identity(const story_native_event_lease::Identity&i){return i.manager&&i.image&&!(i.image&15)&&i.bytes==story_native_turn_camera_image::kImageBytes&&uint64_t(i.image)+i.bytes<=0x100000000ull&&i.stage&&i.room&&i.generation&&i.program;}
bool same(const story_native_event_lease::Identity&a,const story_native_event_lease::Identity&b){return a.manager==b.manager&&a.image==b.image&&a.bytes==b.bytes&&a.stage==b.stage&&a.room==b.room&&a.generation==b.generation&&a.program==b.program;}
}
bool collect(const story_native_event_lease::Identity&i,const Reader&read,Capture&out){out={};if(!identity(i)||!read)return false;Capture next;next.identity=i;try{if(!read(i.image,next.image))return false;}catch(...){return false;}out=next;return true;}
Result validate(Phase phase,const story_native_event_lease::Identity&expected,const story_native_turn_camera_image::Program&p,const Capture&c,uint32_t watchdog){
 Result r;auto fail=[&](Reason why){r.reason=why;return r;};
 if(!identity(expected)||!same(expected,c.identity))return fail(Reason::Identity);
 if(!c.exclusive||c.foreign)return fail(Reason::Ownership);
 if(c.manager_depth||c.camera_depth||c.npc_depth)return fail(Reason::Busy);
 if(!watchdog||watchdog>18000||watchdog<=p.timekeeper_timer||c.elapsed_ticks>=watchdog)return fail(Reason::Watchdog);
 std::vector<uint8_t> canonical;std::string why;if(!story_native_turn_camera_image::make(p,canonical,why))return fail(Reason::Image);
 const auto b=std::span<const uint8_t>(c.image);const auto state=word(b,0xE4);
 const bool terminal=phase==Phase::Terminal||phase==Phase::ExhaustedReleased;
 const bool released=phase==Phase::ExhaustedReleased;
 const bool live=state>=2||terminal;
 if(state!=0&&state!=1&&state!=2&&state!=4)return fail(Reason::State);
 if((terminal&&state!=0)||(phase==Phase::Pristine&&state!=0)||(phase==Phase::Published&&state>1)||(phase==Phase::Running&&state!=2&&state!=4))return fail(Reason::State);
 if(c.completion_flags&~0x1FFu)return fail(Reason::Barrier);
 if(!live&&c.completion_flags)return fail(Reason::Barrier);
 if(terminal&&c.completion_flags!=(released?0u:0x1FFu))return fail(Reason::Barrier);
 for(size_t at=0;at<b.size();++at){bool mutable_byte=at>=0xE4&&at<0xE8;for(unsigned s=0;s<4;++s){size_t start=0xF0+s*80;mutable_byte|=at>=start+0x38&&at<start+0x48;}if(!mutable_byte&&b[at]!=canonical[at])return fail(Reason::Image);}
 for(unsigned s=0;s<4;++s){size_t at=0xF0+s*80;
  if(!live){if(!std::equal(b.begin()+at+0x38,b.begin()+at+0x48,canonical.begin()+at+0x38))return fail(Reason::Staff);continue;}
  const auto cut=word(b,at+0x38),action=word(b,at+0x3C);
  if((s<2&&cut!=s&&cut!=7+s)||(s==2&&cut!=2&&cut!=6)||(s==3&&(cut<3||cut>5)))return fail(Reason::Staff);
  const uint32_t wanted=s<2?(cut==s?0:3):s==2?(cut==2?4:5):(cut==3?0:1);
  if(action!=0xFFFFFFFFu&&action!=wanted)return fail(Reason::Staff);
  if(terminal){const uint32_t last[]={0,1,6,5},actions[]={0,0,5,1};if(cut!=last[s]||action!=actions[s])return fail(Reason::Staff);}
  const uint32_t timer=(uint32_t(b[at+0x42])<<8)|b[at+0x43];
  const uint32_t max_timer=s<2?(cut==s?p.wait_timer:1):s==2?0:p.timekeeper_timer;
  if(b[at+0x40]||b[at+0x41]||timer>max_timer||b[at+0x44]||b[at+0x45]>1||b[at+0x46]>2||b[at+0x47]>1)return fail(Reason::Staff);
  r.cuts[s]=cut;r.actions[s]=wanted;
 }
 if(live&&!released){const auto f=c.completion_flags;
  // Native advance requires old cut completion; successor start barriers add
  // prerequisites. Independent branches may finish in any order.
  for(unsigned s=0;s<2;++s){if(r.cuts[s]==s&&!(f&(1u<<(7+s))))return fail(Reason::Barrier);if((f&(1u<<s))&&r.cuts[s]!=s)return fail(Reason::Barrier);}
  if(r.cuts[2]==6&&!(f&(1u<<2)))return fail(Reason::Barrier);
  if((f&(1u<<6))&&r.cuts[2]!=6)return fail(Reason::Barrier);
  if(r.cuts[3]>=4&&(f&0xBu)!=0xBu)return fail(Reason::Barrier);
  if(r.cuts[3]==5&&(f&0x50u)!=0x50u)return fail(Reason::Barrier);
  if((f&(1u<<5))&&r.cuts[3]!=5)return fail(Reason::Barrier);
  if(state==4&&!(f&(1u<<5)))return fail(Reason::Barrier);
 }
 r.valid=true;r.reason=Reason::None;return r;
}
}
