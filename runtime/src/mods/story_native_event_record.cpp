// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_record.h"
#include <algorithm>
#include <array>
#include <utility>
namespace story_native_event_record {namespace {
using story_native_event_observation::Expected;
using story_native_event_lease::Phase;
constexpr uint32_t Magic=0x4E455231,Version=2;
uint32_t word(std::span<const uint8_t>b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
void put(std::vector<uint8_t>&b,size_t p,uint32_t v){for(unsigned k=0;k<4;++k)b[p+k]=uint8_t(v>>(24-8*k));}
uint32_t digest(std::span<const uint8_t>b){uint32_t h=2166136261u;for(size_t p=0;p<b.size();++p)h=(h^(p>=12&&p<16?0:b[p]))*16777619u;return h;}
bool phase_ok(Phase p){return p>=Phase::Prepared&&p<=Phase::Draining;}
bool plausible(const Expected&e,Phase p){
 const auto&i=e.identity;
 if(!phase_ok(p)||!i.manager||i.manager&3||!i.image||i.image&15||i.bytes!=0x520||!i.stage||!i.room||!i.generation||!i.program||!e.stage_name[0]||e.room_number>63||!e.frames||e.frames>32767||!e.camera.address||!e.camera.pid||e.camera.pid==0xFFFFFFFFu)return false;
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
}
bool encode(const Expected&e,Phase phase,std::vector<uint8_t>&out){
 out.clear();if(!plausible(e,phase))return false;
 Writer w{out};w.u(Magic);w.u(Version);w.u(0);w.u(0);w.u(uint32_t(phase));expected(w,e);
 if(out.size()>kMaximumBytes){out.clear();return false;}put(out,8,uint32_t(out.size()));put(out,12,digest(out));return true;
}
bool decode(std::span<const uint8_t>in,Expected&out,Phase&phase){
 out={};phase=Phase::Empty;
 if(in.size()<20||in.size()>kMaximumBytes||word(in,0)!=Magic||word(in,4)!=Version||word(in,8)!=in.size()||word(in,12)!=digest(in))return false;
 Reader r{in};r.p=16;Phase p=Phase(r.u());Expected e;expected(r,e);
 if(!r.ok||r.p!=in.size()||!plausible(e,p))return false;
 out=std::move(e);phase=p;return true;
}
}
