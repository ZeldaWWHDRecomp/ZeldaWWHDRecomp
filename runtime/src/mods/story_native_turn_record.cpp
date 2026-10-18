// SPDX-License-Identifier: MPL-2.0
#include "story_native_turn_record.h"
#include <cstring>
#include <algorithm>
#include <array>
#include <utility>
namespace story_native_turn_record {namespace {
using story_native_event_observation::Expected;
using story_native_event_lease::Phase;
constexpr uint32_t Magic=0x4E545231,Version=kVersion;
uint32_t word(std::span<const uint8_t>b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
void put(std::vector<uint8_t>&b,size_t p,uint32_t v){for(unsigned k=0;k<4;++k)b[p+k]=uint8_t(v>>(24-8*k));}
uint32_t digest(std::span<const uint8_t>b){uint32_t h=2166136261u;for(size_t p=0;p<b.size();++p)h=(h^(p>=12&&p<16?0:b[p]))*16777619u;return h;}
bool phase_ok(Phase p){return p>=Phase::Prepared&&p<=Phase::Draining;}
bool plausible(const Expected&e,Phase p){
 const auto&i=e.identity;
 if(!phase_ok(p)||!i.manager||i.manager&3||!i.image||i.image&15||i.bytes!=0xE60||!i.stage||!i.room||!i.generation||!i.program||!e.stage_name[0]||e.room_number>63||!e.frames||e.frames>32767||!e.camera.address||!e.camera.pid||e.camera.pid==0xFFFFFFFFu)return false;
 for(const auto&a:e.actors)if(!a.address||a.address&3||!a.pid||a.pid==0xFFFFFFFFu||!a.token||a.profile!=0x175||a.subtype!=1||std::all_of(a.fingerprint.begin(),a.fingerprint.end(),[](uint8_t b){return b==0;}))return false;
 if(e.actors[0].fingerprint==e.actors[1].fingerprint)return false;
 return e.actors[0].address!=e.actors[1].address&&e.actors[0].pid!=e.actors[1].pid&&e.actors[0].token!=e.actors[1].token&&story_native_event_observation::valid_camera_transition(e.camera,e.camera);
}
struct Writer {
 std::vector<uint8_t>&b;
 void u(uint32_t v){size_t p=b.size();b.resize(p+4);put(b,p,v);}
 template<size_t N>void a(const std::array<uint8_t,N>&v){b.insert(b.end(),v.begin(),v.end());}
};
struct Reader {
 std::span<const uint8_t>b;size_t p=0;bool ok=true;
 uint32_t u(){if(!ok||p+4>b.size()){ok=false;return 0;}auto v=word(b,p);p+=4;return v;}
 template<size_t N>void a(std::array<uint8_t,N>&v){if(!ok||p+N>b.size()){ok=false;return;}std::copy_n(b.begin()+p,N,v.begin());p+=N;}
};
void camera(Writer&w,const story_native_event_observation::Camera&c){
 w.u(c.address);w.u(c.pid);w.u(c.flags);w.u(c.timer);w.u(c.current_type);w.u(c.next_type);w.u(c.event_type);w.u(c.style);w.u(c.bg);w.u(c.trim);w.u(c.counter108);w.u(c.counter118);w.u(c.elapsed11C);w.u(c.style_table);w.u(c.style_count);w.u(c.param_pointer);w.u(c.param_index);w.u(c.type_table);w.u(c.type_count);
 w.a(c.type_gate518);w.a(c.engine100);w.a(c.mode_phase13C);w.a(c.initial_view010);w.a(c.done100);w.a(c.work37C);w.a(c.positions0A4);w.a(c.view03C);w.a(c.event_data);
}
void camera(Reader&r,story_native_event_observation::Camera&c){
 c.address=r.u();c.pid=r.u();c.flags=r.u();c.timer=r.u();c.current_type=r.u();c.next_type=r.u();c.event_type=r.u();c.style=r.u();c.bg=r.u();c.trim=r.u();c.counter108=r.u();c.counter118=r.u();c.elapsed11C=r.u();c.style_table=r.u();c.style_count=r.u();c.param_pointer=r.u();c.param_index=r.u();c.type_table=r.u();c.type_count=r.u();
 r.a(c.type_gate518);r.a(c.engine100);r.a(c.mode_phase13C);r.a(c.initial_view010);r.a(c.done100);r.a(c.work37C);r.a(c.positions0A4);r.a(c.view03C);r.a(c.event_data);
}
void expected(Writer&w,const Expected&e){
 const auto&i=e.identity;w.u(i.manager);w.u(i.image);w.u(i.bytes);w.u(i.stage);w.u(i.room);w.u(i.generation);w.u(uint32_t(i.program>>32));w.u(uint32_t(i.program));w.u(e.room_number);w.u(e.frames);w.a(e.stage_name);
 for(auto v:e.retail_manager)w.u(v);w.u(e.retail_camera_play);w.u(e.retail_current_event);w.a(e.retail_flags);
 for(const auto&a:e.actors){w.u(a.address);w.u(a.pid);w.u(a.token);w.u(a.profile);w.u(a.subtype);w.a(a.cut);w.a(a.fingerprint);}camera(w,e.camera);
}
void expected(Reader&r,Expected&e){
 auto&i=e.identity;i.manager=r.u();i.image=r.u();i.bytes=r.u();i.stage=r.u();i.room=r.u();i.generation=r.u();i.program=uint64_t(r.u())<<32;i.program|=r.u();e.room_number=r.u();e.frames=r.u();r.a(e.stage_name);
 for(auto&v:e.retail_manager)v=r.u();e.retail_camera_play=r.u();e.retail_current_event=r.u();r.a(e.retail_flags);
 for(auto&a:e.actors){a.address=r.u();a.pid=r.u();a.token=r.u();a.profile=r.u();a.subtype=r.u();r.a(a.cut);r.a(a.fingerprint);}camera(r,e.camera);
}
void pose(Writer&w,const story_native_people_pose_plan::Snapshot&s){
 const auto&i=s.identity;for(auto v:{i.actor,i.pid,i.token,i.stage,i.room,i.generation,i.morf,i.model})w.u(v);
 w.a(s.angles);w.a(s.shape);w.a(s.joint);w.a(s.cut);w.a(s.target);w.a(s.velocity);
 for(auto v:{uint32_t(s.head_only),uint32_t(s.noticed),uint32_t(s.talk_ready),uint32_t(s.target_mode),s.status})w.u(v);
}
void pose(Reader&r,story_native_people_pose_plan::Snapshot&s){
 auto&i=s.identity;i.actor=r.u();i.pid=r.u();i.token=r.u();i.stage=r.u();i.room=r.u();i.generation=r.u();i.morf=r.u();i.model=r.u();
 r.a(s.angles);r.a(s.shape);r.a(s.joint);r.a(s.cut);r.a(s.target);r.a(s.velocity);
 auto byte=[&](){auto v=r.u();if(v>255)r.ok=false;return uint8_t(v);};
 s.head_only=byte();s.noticed=byte();s.talk_ready=byte();s.target_mode=byte();s.status=r.u();
}
bool valid(const Record&v,std::vector<uint8_t>&image){
 if(v.expected.program){const auto bound=story_native_event_program::Program::turn(v.program,v.watchdog_ticks);if(!bound||v.expected.program!=bound||v.expected.frames!=bound->frames())return false;}
 if(!plausible(v.expected,v.phase)||!v.watchdog_ticks||v.watchdog_ticks>18000||v.watchdog_ticks<=v.program.timekeeper_timer||v.elapsed_ticks>=v.watchdog_ticks||(v.completion_flags&~0x1FFu))return false;
 if((v.phase==Phase::Prepared||v.phase==Phase::Published)&&v.completion_flags)return false;
 std::string why;if(!story_native_turn_camera_image::make(v.program,image,why))return false;
 if(v.poses[0].identity.morf==v.poses[1].identity.morf||v.poses[0].identity.model==v.poses[1].identity.model)return false;
 const auto&a0=v.expected.actors[0];const auto&a1=v.expected.actors[1];
 if(uint64_t(a0.address)<uint64_t(a1.address)+0x920&&uint64_t(a1.address)<uint64_t(a0.address)+0x920)return false;
 for(unsigned n=0;n<2;++n){const auto&s=v.poses[n];const auto&a=v.expected.actors[n];const auto&i=v.expected.identity;story_native_people_pose_plan::Plan plan;
 if((s.identity.morf&3)||(s.identity.model&3))return false;
 const auto name=word(s.cut,0);const auto actor=word(s.cut,8),talk=word(s.cut,12),joint=word(s.cut,0x68);
 if((uint64_t(name)>=i.image&&uint64_t(name)<uint64_t(i.image)+i.bytes)||(actor&&actor!=a.address)||(talk&&talk!=a.address)||(joint&&joint!=a.address+0x3AC))return false;
 if(s.identity.actor!=a.address||s.identity.pid!=a.pid||s.identity.token!=a.token||s.identity.stage!=i.stage||s.identity.room!=i.room||s.identity.generation!=i.generation||s.cut!=a.cut||!story_native_people_pose_plan::make(s,s.identity,true,true,true,true,plan))return false;}
 return true;
}
}
bool encode(const Record&v,std::vector<uint8_t>&out){
 out.clear();std::vector<uint8_t> image;if(!valid(v,image))return false;
 Writer w{out};w.u(Magic);w.u(Version);w.u(0);w.u(0);w.u(uint32_t(v.phase));expected(w,v.expected);
 w.u(v.elapsed_ticks);w.u(v.completion_flags);w.u(v.watchdog_ticks);w.u(v.expected.program?1:0);
 for(const auto&p:v.poses)pose(w,p);
 w.u(uint32_t(image.size()));out.insert(out.end(),image.begin(),image.end());
 if(out.size()>kMaximumBytes){out.clear();return false;}put(out,8,uint32_t(out.size()));put(out,12,digest(out));return true;
}
bool decode(std::span<const uint8_t>in,Record&out){
 out={};if(in.size()<20||in.size()>kMaximumBytes||word(in,0)!=Magic||word(in,4)!=Version||word(in,8)!=in.size()||word(in,12)!=digest(in))return false;
 Reader r{in};r.p=16;Record v;v.phase=Phase(r.u());expected(r,v.expected);v.elapsed_ticks=r.u();v.completion_flags=r.u();v.watchdog_ticks=r.u();const auto descriptor=r.u();if(descriptor>1)return false;for(auto&p:v.poses)pose(r,p);
 const auto bytes=r.u();if(!r.ok||bytes!=0xE60||r.p+bytes!=in.size())return false;
 std::vector<uint8_t> image(in.begin()+r.p,in.end());std::string why;
 if(!story_native_turn_camera_image::validate(image,v.program,why))return false;
 if(descriptor){v.expected.program=story_native_event_program::Program::turn(v.program,v.watchdog_ticks);if(!v.expected.program)return false;}
 std::vector<uint8_t> canonical;if(!valid(v,canonical)||canonical!=image)return false;
 out=std::move(v);return true;
}
}
