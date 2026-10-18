// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_turn_camera_observation.h"
#include <cassert>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdint>
namespace O=story_native_turn_camera_observation;
void put(auto&b,size_t p,uint32_t v){for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(v>>(24-i*8));}
void staff(O::Capture&c,unsigned s,uint32_t cut,uint32_t action){size_t at=0xF0+s*80;put(c.image,at+0x38,cut);put(c.image,at+0x3C,action);c.image[at+0x46]=1;}
int main(){
 story_native_turn_camera_image::Program p;p.camera.start_eye={0,100,200};p.camera.end_eye={100,100,200};p.camera.start_center={0,0,0};p.camera.end_center={100,0,0};p.actor_a_target={100,150,100};p.actor_b_target={-100,150,100};
 std::vector<uint8_t>b;std::string why;assert(story_native_turn_camera_image::make(p,b,why));story_native_event_lease::Identity id{0x10010000,0x60010000,0xE60,1,2,3,4};O::Capture c;unsigned reads=0;assert(O::collect(id,[&](auto at,auto bytes){++reads;assert(at==id.image&&bytes.size()==0xE60);std::copy(b.begin(),b.end(),bytes.begin());return true;},c));assert(reads==1);c.exclusive=true;auto check=[&](const O::Capture&v){return O::validate(O::Phase::Running,id,p,v,600);};assert(O::validate(O::Phase::Pristine,id,p,c,600).valid);
 put(c.image,0xE4,2);staff(c,0,7,3);staff(c,1,8,3);staff(c,2,2,4);staff(c,3,3,0);assert(check(c).valid);
 // Either actor may complete first. No TIMEKEEPER timer can bypass TURN.
 auto first=c;staff(first,0,0,0);first.completion_flags=1u<<7;assert(check(first).valid);auto second=c;staff(second,1,1,0);second.completion_flags=1u<<8;assert(check(second).valid);
 auto final=first;staff(final,1,1,0);staff(final,2,6,5);staff(final,3,5,1);final.completion_flags=0x1FF;put(final.image,0xE4,4);assert(check(final).valid);
 for(unsigned flag:{0u,1u,2u,3u,4u,5u,6u,7u,8u}){auto bad=final;bad.completion_flags&=~(1u<<flag); // flags5 itself required by event terminal; allothers causal prerequisites
  assert(!check(bad).valid);}
 auto ended=final;put(ended.image,0xE4,0);assert(O::validate(O::Phase::Terminal,id,p,ended,600).valid);assert(!check(ended).valid);
 for(unsigned bit=0;bit<9;++bit){auto x=ended;x.completion_flags^=1u<<bit;assert(!O::validate(O::Phase::Terminal,id,p,x,600).valid);}
 for(unsigned s=0;s<4;++s){auto x=ended;put(x.image,0xF0+s*80+0x3C,0xFFFFFFFFu);assert(!O::validate(O::Phase::Terminal,id,p,x,600).valid);}
 auto released=ended;released.completion_flags=0;assert(O::validate(O::Phase::ExhaustedReleased,id,p,released,600).valid);assert(!O::validate(O::Phase::Terminal,id,p,released,600).valid);released.completion_flags=0x1FF;assert(!O::validate(O::Phase::ExhaustedReleased,id,p,released,600).valid);
 auto stalled=c;stalled.elapsed_ticks=599;assert(check(stalled).valid);stalled.elapsed_ticks=600;assert(!check(stalled).valid);assert(!O::validate(O::Phase::Running,id,p,c,300).valid);assert(!O::validate(O::Phase::Running,id,p,c,18001).valid);
 for(unsigned k=0;k<10;++k){auto bad=c;size_t a=0xF0;switch(k){case 0:staff(bad,0,8,3);break;case 1:staff(bad,0,7,0);break;case 2:bad.image[a+0x43]=2;break;case 3:bad.image[a+0x40]=1;break;case 4:bad.image[a+0x44]=1;break;case 5:bad.image[a+0x46]=3;break;case 6:bad.image[0x500+28*64+0x2C]^=1;break;case 7:bad.completion_flags=1u<<20;break;case 8:staff(bad,3,4,1);break;case 9:staff(bad,2,6,5);break;}assert(!check(bad).valid);}
 auto bad=c;bad.foreign=true;assert(!check(bad).valid);bad=c;bad.identity.generation++;assert(!check(bad).valid);bad=c;bad.npc_depth=1;assert(!check(bad).valid);
 O::Capture failed;assert(!O::collect(id,[](auto,auto){return false;},failed)&&failed.identity.image==0);auto wrong=id;wrong.bytes=0xC20;assert(!O::collect(wrong,[](auto,auto){assert(false);return true;},failed));
}
