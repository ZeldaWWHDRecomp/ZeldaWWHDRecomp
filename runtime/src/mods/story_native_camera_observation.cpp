// SPDX-License-Identifier: MPL-2.0
#include "story_native_camera_observation.h"
#include <algorithm>
namespace story_native_camera_observation {namespace {
uint32_t word(std::span<const uint8_t>b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool identity(const story_native_event_lease::Identity&i){return i.manager&&i.image&&!(i.image&15)&&i.bytes==story_native_camera_image::kImageBytes&&uint64_t(i.image)+i.bytes<=0x100000000ull&&i.stage&&i.room&&i.generation&&i.program;}
bool same(const story_native_event_lease::Identity&a,const story_native_event_lease::Identity&b){return a.manager==b.manager&&a.image==b.image&&a.bytes==b.bytes&&a.stage==b.stage&&a.room==b.room&&a.generation==b.generation&&a.program==b.program;}
}
bool collect(const story_native_event_lease::Identity&i,const Reader&read,Capture&out){out={};if(!identity(i)||!read)return false;Capture copy;copy.identity=i;try{if(!read(i.image,copy.image))return false;}catch(...){return false;}out=copy;return true;}
Result validate(Phase phase,const story_native_event_lease::Identity&expected,const story_native_camera_image::Program&p,const Capture&c){
 Result r;auto fail=[&](Reason why){r.reason=why;return r;};
 if(!identity(expected)||!same(expected,c.identity))return fail(Reason::Identity);
 if(!c.exclusive||c.foreign)return fail(Reason::Ownership);
 if(c.manager_depth||c.camera_depth||c.npc_depth)return fail(Reason::Busy);
 std::vector<uint8_t> canonical;std::string why;if(!story_native_camera_image::make(p,canonical,why))return fail(Reason::Image);
 const auto b=std::span<const uint8_t>(c.image);const uint32_t state=word(b,0xE4);r.event_state=state;
 if(state!=0&&state!=1&&state!=2&&state!=4)return fail(Reason::State);
 if((phase==Phase::Pristine&&state!=0)||(phase==Phase::Published&&state>1)||(phase==Phase::Running&&state!=2&&state!=4))return fail(Reason::State);
 for(size_t at=0;at<b.size();++at){bool mutable_byte=at>=0xE4&&at<0xE8;for(unsigned s=0;s<4;++s){size_t start=0xF0+s*0x50;mutable_byte|=at>=start+0x38&&at<start+0x48;}if(!mutable_byte&&b[at]!=canonical[at])return fail(Reason::Image);}
 const uint32_t total=p.hold_timer+p.move_timer;
 for(unsigned s=0;s<4;++s){size_t at=0xF0+s*0x50;
  if(state<2){if(!std::equal(b.begin()+at+0x38,b.begin()+at+0x48,canonical.begin()+at+0x38))return fail(Reason::Staff);continue;}
  const uint32_t cut=word(b,at+0x38),action=word(b,at+0x3C);
  if((s<2&&cut!=s)||(s==2&&cut!=2&&cut!=6)||(s==3&&(cut<3||cut>5)))return fail(Reason::Staff);
  const uint32_t wanted=s<2?0:s==2?(cut==2?4:5):(cut==3?0:1);
  if(action!=0xFFFFFFFFu&&action!=wanted)return fail(Reason::Staff);
  // dEvDtStaff::init/advance/reset: zero u16+40, countdown u16+42,
  // byte44 zero, advance45/cache47 bool, state46 in0..2.
  const uint32_t timer=(uint32_t(b[at+0x42])<<8)|b[at+0x43];
  if(b[at+0x40]||b[at+0x41]||timer>total||b[at+0x44]||b[at+0x45]>1||b[at+0x46]>2||b[at+0x47]>1)return fail(Reason::Staff);
  if(s==2){r.camera_cut=cut;r.camera_action=wanted;}
 }
 r.valid=true;r.reason=Reason::None;return r;
}
}
