// SPDX-License-Identifier: MPL-2.0
#include "story_native_camera_policy.h"
#include <array>
#include <cstring>
namespace story_native_camera_policy {namespace {
uint32_t word(std::span<const uint8_t>b,size_t p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool span(uint32_t p,size_t n){return p&&!(p&3)&&uint64_t(p)+n<=0x100000000ull;}
}
Result verify(const Tables&t,const Read&read,const story_native_event_observation::Camera&c,const story_native_event_lease::Identity&i){
 Result r;auto fail=[&](Reason reason){r.reason=reason;return r;};
 if(!read||!span(t.type_count,4)||!span(t.style_count,4)||!span(t.types,63*0x40)||!span(t.styles,145*0x84)||!i.manager||!i.image||!i.bytes||!i.stage||!i.room||!i.generation||!i.program)return fail(Reason::Configuration);
 std::array<uint8_t,4> count{};std::array<uint8_t,63*0x40> types{};std::array<uint8_t,145*0x84> styles{};
 try{
  if(!read(t.type_count,count))return fail(Reason::Read);if(word(count)!=63)return fail(Reason::Count);
  if(!read(t.style_count,count))return fail(Reason::Read);if(word(count)!=145)return fail(Reason::Count);
  if(!read(t.types,types)||!read(t.styles,styles))return fail(Reason::Read);
 }catch(...){return fail(Reason::Read);}
 if(c.type_table!=t.types||c.type_count!=63)return fail(Reason::Type);
 if(c.current_type>=63||c.next_type>=63||c.event_type>=63)return fail(Reason::Type);
 const uint32_t current_mode=word(c.mode_phase13C),next_mode=word(c.mode_phase13C,4);
 if(current_mode>=20||next_mode>=20)return fail(Reason::Mode);
 if(c.style>=145)return fail(Reason::Style);
 if(c.style_table!=t.styles||c.style_count!=145||c.param_index!=c.style||uint64_t(t.styles)+uint64_t(c.style)*0x84!=c.param_pointer)return fail(Reason::Cache);
 auto slot=[&](uint32_t type,uint32_t mode)->int32_t{size_t at=type*0x40+0x18+mode*2;uint32_t value=uint32_t(types[at])<<8|types[at+1];return value&0x8000?int32_t(value)-65536:int32_t(value);};
 // Validate all table references before trusting any selected entry. -1 is
 // native unsupported mode; every other negative/index beyond145 is invalid.
 for(unsigned type=0;type<63;++type)for(unsigned mode=0;mode<20;++mode){int32_t s=slot(type,mode);if(s < -1||s>=145)return fail(Reason::Style);}
 const size_t event=c.event_type*0x40;const char expected[]="Event";
 if(std::memcmp(types.data()+event,expected,sizeof(expected))||slot(c.event_type,0)<0||word(styles,size_t(slot(c.event_type,0))*0x84+4)!=11)return fail(Reason::Type);
 const uint32_t saved=word(c.event_data,0x0C);uint32_t selected_type=c.current_type;
 if(c.current_type==c.event_type){
  if(saved>=63||saved==c.event_type||(c.next_type!=c.event_type&&c.next_type!=saved))return fail(Reason::Transition);
  // Exact NotRun path changes CurType only; its cached style remains that of
  // saved prior type. Run path changes next/current/type/style together.
  if(c.next_type!=c.event_type)selected_type=saved;
 }else if(c.current_type!=c.next_type||saved!=0xFFFFFFFFu)return fail(Reason::Transition);
 if(current_mode!=next_mode)return fail(Reason::Transition);
 const auto selected=slot(selected_type,current_mode);
 if(selected<0||uint32_t(selected)!=c.style)return fail(Reason::Style);
 // Native style algorithms dispatch0..18; Event Run must use algorithm11.
 const auto algorithm=word(styles,c.style*0x84+4);
 if(algorithm>18||(c.current_type==c.event_type&&c.next_type==c.event_type&&algorithm!=11))return fail(Reason::Style);
 // The private E60 grammar returns through native Follow (algorithm1),
 // including NotRun's cached saved style. Prove it from actual bounded tables
 // on every production verification path; a stale Work tag is not evidence.
 if(i.bytes==0xE60&&selected_type!=c.event_type&&algorithm!=1)return fail(Reason::Style);
 r.valid=true;r.reason=Reason::None;return r;
}
}
