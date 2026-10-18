// SPDX-License-Identifier: MPL-2.0
#include "story_native_people_pose_capture.h"
#include "guest_addr.h"
#include <algorithm>

namespace story_native_people_pose_capture { namespace {
uint32_t word(std::span<const uint8_t>b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
uint16_t half(std::span<const uint8_t>b,unsigned p){return uint16_t(b[p])<<8|b[p+1];}
bool span(uint32_t p,uint32_t bytes){return !(p&3)&&p>=0x10000000&&uint64_t(p)+bytes<=0x50000000;}
bool overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn){return uint64_t(a)<uint64_t(b)+bn&&uint64_t(b)<uint64_t(a)+an;}
struct Sample {std::array<uint8_t,story_native_turn_camera_image::kImageBytes> image{};std::array<uint8_t,0x500> flags{};std::array<uint8_t,0x920> actor{};uint32_t morf{},model{};story_native_people_pose_plan::Snapshot pose{};};
bool sample(const Read&read,const Layout&l,const story_native_event_observation::Expected&e,unsigned n,Mode mode,Sample&out){
 const auto&a=e.actors[n];auto&b=out.actor;
 if(!read(a.address,b)||word(b,4)!=a.pid||b[0xC]==3||half(b,0xE)!=0x175||word(b,0x10)!=l.profile||word(b,0xB4)!=l.vtable||word(b,0xB0)!=0x00FF0000||b[0x2DD]!=1||b[0x905]!=1||b[0x326]!=e.room_number)return false;
 // Uo2 day/night l_npc_dat slots101C4ADC/4AE0 both point here. Its
 // horizontal attention offsets18/20 are zero, height1C170, eyeheight24150.
 // Native Execute's derived attention/eye positions therefore do not depend
 // on restored yaw for this stationary subtype. Check actual pointer/data on
 // every sample, not a declaration or a prior capture.
 if(word(b,0x8A0)!=GD(0x101C41D4))return false;
 std::array<uint8_t,16>npc_dat{};
 if(!read(GD(0x101C41D4)+0x18,npc_dat)||word(npc_dat,0)||word(npc_dat,4)!=0x432A0000u||word(npc_dat,8)||word(npc_dat,12)!=0x43160000u)return false;
 std::array<uint8_t,0x14>profile{};
 if(!read(l.profile,profile)||half(profile,8)!=0x175||word(profile,0x10)!=0x920)return false;
 // d_npc.h: mpMorf +44C. m_Do_ext.h: native Morf allocation C8,
 // mpModel +90. People createHeap sets model userArea +B8 to this;
 // native 022BE034 node callback uses that backpointer for joint rotations.
 out.morf=word(b,0x44C);
 if(!span(out.morf,0xC8)||overlap(a.address,0x920,out.morf,0xC8))return false;
 std::array<uint8_t,0xC8>morf{};if(!read(out.morf,morf))return false;
 out.model=word(morf,0x90);
 if(!span(out.model,0xBC)||overlap(a.address,0x920,out.model,0xBC)||overlap(out.morf,0xC8,out.model,0xBC))return false;
 std::array<uint8_t,0xBC>model{};if(!read(out.model,model)||word(model,0xB8)!=a.address)return false;
 auto&s=out.pose;s.identity={a.address,a.pid,a.token,e.identity.stage,e.identity.room,e.identity.generation,out.morf,out.model};
 auto copy=[&](unsigned p,auto&dest){std::copy_n(b.begin()+p,dest.size(),dest.begin());};
 copy(0x320,s.angles);copy(0x328,s.shape);copy(0x3AC,s.joint);copy(0x3E0,s.cut);copy(0x884,s.target);copy(0x8F8,s.velocity);
 s.head_only=b[0x8D8];s.noticed=b[0x8FD];s.talk_ready=b[0x8FF];s.target_mode=b[0x90D];s.status=word(b,0x2E0);
 if(s.cut[0x60]>1||s.cut[0x61]>1)return false;
 if(mode==Mode::Baseline)return s.cut==a.cut;
 const auto&cut=s.cut;const auto&i=e.identity;
 if((mode!=Mode::Owned&&mode!=Mode::Exhausted)||(i.image&15)||i.image<0x60000000||i.bytes<0x190||uint64_t(i.image)+i.bytes>0x68000000)return false;
 const uint32_t staff=word(cut,4),action=word(cut,0x10);
 if(word(cut,0)!=i.image+0xF0+n*0x50||word(cut,8)!=a.address||
    (word(cut,0xC)!=0&&word(cut,0xC)!=a.address)||
    (word(cut,0x68)!=0&&word(cut,0x68)!=a.address+0x3AC))return false;
 if(e.program&&(i.bytes!=e.program->bytes()||e.frames!=e.program->frames()))return false;
 const bool turn=e.program&&e.program->kind()==story_native_event_program::Kind::TurnCamera;
 if(!turn)return mode==Mode::Owned&&(staff==n||staff==word(a.cut,4))&&(action==0||action==0xFFFFFFFFu);
 if(i.bytes!=e.program->bytes()||e.frames!=e.program->frames()||!read(i.image,out.image)||!e.program->immutable_matches(out.image))return false;
 const auto image=std::span<const uint8_t>(out.image);const uint32_t state=word(image,0xE4);
 if(mode==Mode::Exhausted){
  if(state!=0||!read(i.manager+0x3C,out.flags))return false;
  for(unsigned k=0;k<out.flags.size();++k)if(out.flags[k]!=(k==2?1:k==3?0xFF:0))return false;
  const std::array<uint32_t,4>cuts{0,1,6,5},actions{0,0,5,1};
  for(unsigned k=0;k<4;++k){const unsigned at=0xF0+k*0x50;const auto final=e.program->cut(k,cuts[k]);if(!final||half(image,at+0x42)>final->maximum_timer||word(image,at+0x38)!=cuts[k]||word(image,at+0x3C)!=actions[k]||word(image,at+0x40)>>16||image[at+0x44]||image[at+0x45]>1||image[at+0x46]>2||image[at+0x47]>1)return false;}
  return staff==n&&action==0&&word(cut,0x18)==0;
 }
 if(state!=2&&state!=4)return false;
 const unsigned row=0xF0+n*0x50;const auto selected=e.program->cut(n,word(image,row+0x38));const uint32_t cache=word(image,row+0x3C);
 if(!selected||(cache!=selected->action&&cache!=0xFFFFFFFFu)||(action!=selected->action&&action!=0xFFFFFFFFu)||
    word(image,row+0x40)>>16||half(image,row+0x42)>selected->maximum_timer||image[row+0x44]||image[row+0x45]>1||image[row+0x46]>2||image[row+0x47]>1)return false;
 // An old baseline staff ID is accepted only before this native CommonCut
 // has bound its own staff/action. A primed TURN always belongs to staff n.
 return staff==n||(staff==word(a.cut,4)&&action==0xFFFFFFFFu&&cache==0xFFFFFFFFu);
}
bool equal(const story_native_people_pose_plan::Snapshot&a,const story_native_people_pose_plan::Snapshot&b){return a.identity==b.identity&&a.angles==b.angles&&a.shape==b.shape&&a.joint==b.joint&&a.cut==b.cut&&a.target==b.target&&a.velocity==b.velocity&&a.head_only==b.head_only&&a.noticed==b.noticed&&a.talk_ready==b.talk_ready&&a.target_mode==b.target_mode&&a.status==b.status;}
}
bool capture(const Read&read,const VerifyActor&verify,const Layout&l,const story_native_event_observation::Expected&e,unsigned n,story_native_people_pose_plan::Snapshot&out,Mode mode){
 out={};if(!read||!verify||n>=e.actors.size()||!span(l.profile,0x14)||!span(l.vtable,4)||!e.identity.stage||!e.identity.room||!e.identity.generation||e.room_number>63)return false;
 const auto&a=e.actors[n];if(!span(a.address,0x920)||!a.pid||a.pid==~0u||!a.token||a.profile!=0x175||a.subtype!=1||std::all_of(a.fingerprint.begin(),a.fingerprint.end(),[](uint8_t b){return !b;}))return false;
 try{Sample first,second;if(!verify(a,e.identity)||!sample(read,l,e,n,mode,first)||!verify(a,e.identity)||!sample(read,l,e,n,mode,second)||!equal(first.pose,second.pose)||first.image!=second.image||first.flags!=second.flags||!verify(a,e.identity))return false;out=first.pose;return true;}catch(...){return false;}
}
}
