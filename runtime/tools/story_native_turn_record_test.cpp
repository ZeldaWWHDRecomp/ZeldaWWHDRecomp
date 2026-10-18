// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_turn_record.h"
#include <cassert>
#include <limits>
#include <vector>
#include <cstdint>
namespace T=story_native_turn_record;namespace L=story_native_event_lease;
void put(auto&b,size_t p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-n*8));}
int main(){T::Record v;auto&e=v.expected;e.identity={0x20000000,0x60000000,0xE60,1,2,3,4};e.stage_name={'s','e','a'};e.room_number=11;
 e.camera.address=0x20003248;e.camera.pid=12;e.camera.event_type=3;e.camera.type_table=0x10049350;e.camera.type_count=63;e.camera.style_table=e.camera.param_pointer=0x1004487C;e.camera.style_count=145;put(e.camera.event_data,12,~0u);
 for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x20001000+n*0x1000;a.pid=10+n;a.token=1+n;a.profile=0x175;a.subtype=1;for(unsigned k=0;k<32;++k)a.fingerprint[k]=uint8_t(k*7+n+1);auto&s=v.poses[n];s.identity={a.address,a.pid,a.token,1,2,3,0x30000000+n*0x1000,0x31000000+n*0x1000};s.angles[2]=uint8_t(3+n);s.joint[0]=uint8_t(9+n);s.status=0xA000+s.identity.pid;s.target_mode=1;}

 auto pattern=[](auto&array,unsigned seed){for(size_t k=0;k<array.size();++k)array[k]=uint8_t(k*29+seed);};
 auto&c=e.camera;pattern(c.type_gate518,1);pattern(c.engine100,2);pattern(c.mode_phase13C,3);pattern(c.initial_view010,4);pattern(c.done100,5);pattern(c.work37C,6);pattern(c.positions0A4,7);pattern(c.view03C,8);pattern(c.event_data,9);
 for(unsigned k=0;k<4;++k){c.mode_phase13C[k]=0;c.mode_phase13C[4+k]=0;c.event_data[0x0C+k]=0xFF;}
 pattern(e.retail_flags,11);for(unsigned k=0;k<8;++k)e.retail_manager[k]=0x12000000+k*4;e.retail_camera_play=19;e.retail_current_event=27;
 for(unsigned n=0;n<2;++n){auto&s=v.poses[n];pattern(s.joint,30+n);pattern(s.target,40+n);pattern(s.velocity,50+n);s.head_only=1;s.noticed=1;s.talk_ready=1;}
 v.program.camera.start_eye={0,100,200};v.program.camera.start_center={0,30,0};v.program.camera.end_eye={100,100,200};v.program.camera.end_center={100,30,0};v.program.actor_a_target={100,150,100};v.program.actor_b_target={-100,150,100};v.phase=L::Phase::Running;v.elapsed_ticks=17;v.completion_flags=128;v.watchdog_ticks=900;
 std::vector<uint8_t>b;assert(T::encode(v,b));T::Record d;assert(T::decode(b,d));assert(d.expected==v.expected&&d.elapsed_ticks==17&&d.completion_flags==128&&d.watchdog_ticks==900);
 std::vector<uint8_t>round;assert(T::encode(d,round)&&round==b); // all pose/program bytes survive
 assert(d.poses[1].identity==v.poses[1].identity&&d.poses[1].joint==v.poses[1].joint&&d.poses[1].angles==v.poses[1].angles&&d.poses[1].status==v.poses[1].status);
 assert(d.program.actor_b_target.x==-100&&d.program.camera.end_eye.x==100);
 auto bound=v;bound.expected.program=story_native_event_program::Program::turn(bound.program,bound.watchdog_ticks);assert(bound.expected.program);std::vector<uint8_t>typed;assert(T::encode(bound,typed)&&T::decode(typed,d)&&d.expected==bound.expected&&d.expected.program);assert(T::encode(d,round)&&round==typed);auto mismatch=bound;mismatch.expected.program=story_native_event_program::Program::turn(bound.program,bound.watchdog_ticks+1);assert(!T::encode(mismatch,round));mismatch=bound;mismatch.expected.program=story_native_event_program::Program::wait(30);assert(!T::encode(mismatch,round));
 for(size_t at=0;at<b.size();at+=37){auto corrupt=b;corrupt[at]^=1;assert(!T::decode(corrupt,d));}
 auto shortb=b;shortb.pop_back();assert(!T::decode(shortb,d));auto trailing=b;trailing.push_back(0);assert(!T::decode(trailing,d));
 auto bad=v;bad.expected.actors[0].fingerprint={};assert(!T::encode(bad,round));bad=v;bad.expected.actors[1].fingerprint=bad.expected.actors[0].fingerprint;assert(!T::encode(bad,round));
 bad=v;bad.expected.identity.bytes=0x520;assert(!T::encode(bad,round));bad=v;bad.poses[1].identity.pid++;assert(!T::encode(bad,round));bad=v;bad.poses[0].cut[0]=1;assert(!T::encode(bad,round));
 bad=v;bad.elapsed_ticks=900;assert(!T::encode(bad,round));bad=v;bad.completion_flags=512;assert(!T::encode(bad,round));bad=v;bad.phase=L::Phase::Published;assert(!T::encode(bad,round));bad=v;bad.program.actor_a_target.x=std::numeric_limits<float>::infinity();assert(!T::encode(bad,round));
 bad=v;bad.poses[1].identity.model=bad.poses[0].identity.model;assert(!T::encode(bad,round));
 bad=v;put(bad.poses[0].cut,0,bad.expected.identity.image);bad.expected.actors[0].cut=bad.poses[0].cut;assert(!T::encode(bad,round));
 bad=v;bad.expected.identity.generation++;assert(!T::encode(bad,round));
}
