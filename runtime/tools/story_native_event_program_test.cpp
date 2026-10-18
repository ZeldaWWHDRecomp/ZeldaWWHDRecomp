// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_program.h"
#include "mods/story_native_event_image.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <type_traits>
#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
using namespace story_native_event_program;
int main(){
 static_assert(!std::is_default_constructible_v<Program>);
 assert(!Program::wait(0)&&!Program::wait(32768));
 for(uint32_t frames:{1u,30u,32767u}){auto p=Program::wait(frames);assert(p&&p->kind()==Kind::Wait&&p->bytes()==0x520&&p->frames()==frames&&!p->watchdog()&&!p->turn_definition());auto b=story_native_event_image::make_wait_fixture(frames);assert(std::equal(b.begin(),b.end(),p->pristine().begin())&&p->immutable_matches(b));assert(p->completion_mask()==0x3F&&p->pool_offsets()[4]==0x410);for(unsigned s=0;s<4;++s)assert(p->initial_cut(s)==s&&p->cut(s,s));assert(!p->cut(0,7)&&!p->cut(2,6)&&!p->cut(4,0));b[0xE7]=2;b[0x128]=0;assert(p->immutable_matches(b));b[0x410]^=1;assert(!p->immutable_matches(b));}
 story_native_turn_camera_image::Program d;d.camera.start_eye={0,10,20};d.camera.end_eye={10,10,20};d.actor_a_target={20,0,0};d.actor_b_target={-20,0,0};
 auto p=Program::turn(d,301);assert(p&&p->bytes()==0xE60&&p->kind()==Kind::TurnCamera&&p->turn_definition()&&p->watchdog()==301&&p->completion_mask()==0x1FF);assert(p->initial_cut(0)==7&&p->initial_cut(1)==8&&p->initial_cut(2)==2&&p->initial_cut(4)==0xFFFFFFFFu);assert(p->pool_offsets()==(std::array<uint32_t,8>{0,0x40,0xF0,0x230,0x500,0xD80,0xE00,0xE40}));
 assert(p->cut(0,7)->action==3&&p->cut(0,7)->maximum_timer==1&&p->cut(0,0)->action==0&&p->cut(1,8)->action==3&&!p->cut(0,8)&&!p->cut(1,7));assert(p->cut(2,2)->action==4&&p->cut(2,6)->action==5&&!p->cut(2,3));assert(p->cut(3,3)->action==0&&p->cut(3,4)->action==1&&p->cut(3,5)->action==1&&!p->cut(3,6));
 std::vector<uint8_t>b;std::string why;assert(story_native_turn_camera_image::make(d,b,why)&&std::equal(b.begin(),b.end(),p->pristine().begin()));for(size_t i=0;i<b.size();++i){auto changed=b;changed[i]^=1;bool mutable_byte=i>=0xE4&&i<0xE8;for(unsigned s=0;s<4;++s)mutable_byte|=i>=0xF0+s*0x50+0x38&&i<0xF0+s*0x50+0x48;assert(p->immutable_matches(changed)==mutable_byte);}assert(!p->immutable_matches(std::span(b).first(b.size()-1)));
 assert(*p==*Program::turn(d,301)&&*p!=*Program::turn(d,302));auto distinct=d;distinct.actor_b_target.z=1;assert(*p!=*Program::turn(distinct,301));assert(*Program::wait(30)==*Program::wait(30)&&*Program::wait(30)!=*Program::wait(31)&&*p!=*Program::wait(30));
 assert(!Program::turn(d,300)&&!Program::turn(d,18001));auto bad=d;bad.actor_a_target.x=std::numeric_limits<float>::quiet_NaN();assert(!Program::turn(bad,301));d.actor_a_target.x=99;assert(p->turn_definition()->actor_a_target.x==20);assert(!Program::wait(30)->immutable_matches(b));
}
