// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_collect.h"
#include "mods/story_native_event_image.h"
#include <cassert>
#include <cstring>
#include <map>
#include <vector>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
using namespace story_native_event_collect;
static constexpr uint32_t play=0x1046F0B0,cam_process=0x20004000,link_actor=0x20006000,stamp=0x60001000;
struct Fake {
 Expected e;Config config;std::map<uint32_t,std::vector<uint8_t>> memory;std::array<std::array<uint8_t,32>,2> actual_fingerprints{};bool owned=true,allocated=true,exclusive=true,camera_valid=true;unsigned reads=0;uint32_t allocation_extent=0x520;
 void add(uint32_t a,size_t n){memory[a].resize(n);}
 uint8_t* pointer(uint32_t a,size_t n){for(auto&[base,b]:memory)if(a>=base&&uint64_t(a)+n<=uint64_t(base)+b.size())return b.data()+a-base;return nullptr;}
 void put(uint32_t a,uint32_t v){auto*p=pointer(a,4);assert(p);for(unsigned n=0;n<4;++n)p[n]=uint8_t(v>>(24-8*n));}
 void half(uint32_t a,uint16_t v){auto*p=pointer(a,2);assert(p);p[0]=v>>8;p[1]=v;}
 void byte(uint32_t a,uint8_t v){auto*p=pointer(a,1);assert(p);*p=v;}
 void copy(uint32_t a,std::span<const uint8_t>b){auto*p=pointer(a,b.size());assert(p);std::copy(b.begin(),b.end(),p);}
 Fake(){
  e.identity={play+0x52C4,0x60000000,0x520,77,1000,3,4};e.stage_name={'s','e','a'};config.stamp=stamp;
  add(0x10049348,8+3*0x40);put(0x10049348,3);add(0x10044878,4+4*0x84);put(0x10044878,4);add(play,0x5B38);add(0x10475000,0x1000);add(0x1047E000,0x1000);add(0x101C4CD0,0x30);add(0x101D5500,0x200);add(cam_process,0xB28);add(link_actor,0x328);add(e.identity.image,0x520);add(stamp,64);
  put(0x104753A8,1);put(0x1047E6C4,77);put(0x1047E8F0,1000);copy(play+0x5134,e.stage_name);put(play+0x5B34,link_actor);put(link_actor+4,55);half(link_actor+14,0xA8);
  put(0x101C4CD0+0x10,0x920);half(0x101C4CD0+8,0x175);
  for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x20001000+n*0x1000;a.pid=10+n;a.token=1+n;a.profile=0x175;a.subtype=1;for(unsigned k=0;k<32;++k)a.fingerprint[k]=uint8_t(k+n+1);actual_fingerprints[n]=a.fingerprint;add(a.address,0x920);put(a.address+4,a.pid);half(a.address+14,0x175);put(a.address+0x10,0x101C4CD0);put(a.address+0xB0,0x00FF0000);byte(a.address+0x2DD,1);byte(a.address+0x905,1);}
  e.camera.address=cam_process+0x248;e.camera.pid=12;e.camera.event_type=2;e.camera.style_table=e.camera.param_pointer=0x1004487C;e.camera.style_count=4;e.camera.type_table=0x10049350;e.camera.type_count=3;put(cam_process+0x248+0x8A8,0x1004487C);put(cam_process+0x248+0x8B4,2);put(cam_process+0x248+0x408,0xFFFFFFFFu);e.camera.event_data[12]=e.camera.event_data[13]=e.camera.event_data[14]=e.camera.event_data[15]=0xFF;put(play+0x5AF8,cam_process);put(cam_process+4,12);half(cam_process+14,0x1DC);put(cam_process+0x10,0x101D55F0);put(cam_process+0x228,0x101D5538);put(cam_process+0x248,cam_process);
  half(0x101D55F0+8,0x1DC);put(0x101D55F0+0x10,0xB28);put(0x101D55F0+0x3C,0x101D5538);
  const uint32_t functions[]={0x025021A4,0x02502288,0x024FFA3C,0x0250235C,0x024FFC40};for(unsigned n=0;n<5;++n)put(0x101D5538+n*4,functions[n]);
  copy(e.identity.image,story_native_event_image::make_wait_fixture());copy(stamp,make_stamp(e));
 }
 Collector collector(){return Collector(e,config,[this](uint32_t a,std::span<uint8_t>b){++reads;auto*p=pointer(a,b.size());if(!p)return false;std::copy_n(p,b.size(),b.begin());return true;},[this](const Actor&a,const auto&){const auto n=(a.address-e.actors[0].address)/0x1000;return owned&&n<2&&a.token==e.actors[n].token&&a.fingerprint==actual_fingerprints[n];},[this](uint32_t a,uint32_t n,uint32_t s,uint32_t g){return allocated&&a==e.identity.image&&n==allocation_extent&&s==stamp&&g==3;},[this](const Camera&c,const auto&){return camera_valid&&c.event_type==2&&c.current_type<=2&&c.next_type<=2;},[this](Guards&g){g.exclusive=exclusive;return true;});}
};
int main(){
 Fake f;auto collector=f.collector();Captured c;assert(collector.collect(c)&&!c.foreign&&c.control_released);assert(validate(Operation::Prepare,f.e,c).valid());
 assert(c.camera.address==cam_process+0x248&&c.camera.pid==12&&c.actors[0].pid==10&&c.actors[1].pid==11);
 for(unsigned n=0;n<3;++n){Fake x;if(n==0)x.put(cam_process+4,99);if(n==1)x.put(cam_process+0x248,cam_process+4);if(n==2)x.put(cam_process+0x228,0x101D5540);auto k=x.collector();assert(!k.collect(c)&&k.error()==Error::Camera&&c.image.empty());}
 for(unsigned n=0;n<4;++n){Fake x;if(n==0)x.byte(x.e.actors[0].address+0x2DD,2);if(n==1)x.put(x.e.actors[0].address+4,99);if(n==2)x.owned=false;if(n==3)x.put(x.e.actors[0].address+0xB0,0);auto k=x.collector();assert(!k.collect(c)&&k.error()==Error::Actor);}
 // Reject stale/partial unwind tuples, and capture actual elapsed separately
 // from the WAIT Timer scalar stored in work+8.
 for(unsigned n=0;n<3;++n){Fake x;if(n==0)x.put(cam_process+0x248+0x51C,2);if(n==1)x.put(cam_process+0x248+0x520,2);if(n==2)x.put(cam_process+0x248+0x8B4,3);auto k=x.collector();assert(!k.collect(c)&&k.error()==Error::Camera);}
 Fake active;active.put(cam_process+0x248+0x51C,2);active.put(cam_process+0x248+0x520,2);active.put(cam_process+0x248+0x408,0);active.put(cam_process+0x248+0x11C,7);active.put(cam_process+0x248+0x384,30);auto entered=active.collector();assert(entered.collect(c)&&c.camera.elapsed11C==7&&c.camera.timer==30&&c.camera.current_type==2);
 Fake camera_denied;camera_denied.camera_valid=false;auto invalid_types=camera_denied.collector();assert(!invalid_types.collect(c)&&invalid_types.error()==Error::Camera);
 for(unsigned n=0;n<3;++n){Fake x;if(n==0)x.put(cam_process+0x248+0x8A8,0x10044880);if(n==1)x.put(cam_process+0x248+0x8AC,0xFFFFFFFFu);if(n==2)x.put(0x10044878,0);auto k=x.collector();assert(!k.collect(c)&&k.error()==Error::Camera);}
 Fake token;token.put(stamp+0x34,999);auto bad_stamp=token.collector();assert(!bad_stamp.collect(c)&&bad_stamp.error()==Error::Stamp);
 Fake fingerprint;fingerprint.e.actors[0].fingerprint[0]^=1;fingerprint.copy(stamp,make_stamp(fingerprint.e));auto wrong_config=fingerprint.collector();assert(!wrong_config.collect(c)&&wrong_config.error()==Error::Actor);
 Fake generation;generation.put(0x1047E6C4,78);auto stale=generation.collector();assert(!stale.collect(c)&&stale.error()==Error::Stage);
 Fake room;room.put(0x1047E8F0,1001);auto room_stale=room.collector();assert(!room_stale.collect(c)&&room_stale.error()==Error::Stage);
 Fake truncated;truncated.memory[cam_process].resize(0xB27);auto short_span=truncated.collector();assert(!short_span.collect(c)&&short_span.error()==Error::Read);
 Fake bounds;bounds.put(play+0x5AF8,0x4FFFFFFC);auto no_deref=bounds.collector();assert(!no_deref.collect(c)&&no_deref.error()==Error::Camera);
 Fake image_bounds;image_bounds.e.identity.image=0x67FFFFF0;auto no_image=image_bounds.collector();assert(!no_image.collect(c)&&no_image.error()==Error::Configuration&&image_bounds.reads==0);
 Fake denied;denied.exclusive=false;auto no_owner=denied.collector();assert(!no_owner.collect(c)&&no_owner.error()==Error::Ownership&&denied.reads==0);
 Fake foreign;foreign.byte(play+0x51D0+0xC2,1);auto foreign_control=foreign.collector();assert(foreign_control.collect(c)&&c.foreign&&!validate(Operation::Prepare,foreign.e,c).valid());
 // E60 is read through the same bounded actual memory source, only with a
 // typed definition and independently sampled host lease clock.
 auto setup=[](Fake&x){story_native_turn_camera_image::Program d;d.camera.start_eye={0,10,20};d.camera.end_eye={10,10,20};x.e.program=story_native_event_program::Program::turn(d,301);x.e.identity.bytes=0xE60;x.allocation_extent=0xE60;x.memory[x.e.identity.image]=std::vector<uint8_t>(x.e.program->pristine().begin(),x.e.program->pristine().end());x.copy(stamp,make_stamp(x.e));};
 Fake turn;setup(turn);Clock clock{turn.e.identity,10,2,7};turn.config.clock=[&](Clock&v){v=clock;return true;};auto tk=turn.collector();assert(tk.collect(c)&&c.image.size()==0xE60&&c.elapsed_ticks==7&&c.image.back()==turn.memory[turn.e.identity.image].back());assert(validate(Operation::Prepare,turn.e,c).valid());
 clock.logic_step=9;assert(!tk.collect(c)&&tk.error()==Error::Clock&&c.image.empty());clock.logic_step=10;clock.serial=1;assert(!tk.collect(c)&&tk.error()==Error::Clock);clock.serial=2;clock.elapsed_ticks=6;assert(!tk.collect(c)&&tk.error()==Error::Clock);
 Fake pending;setup(pending);Clock pending_clock{pending.e.identity,100,0,0};pending.config.clock=[&](Clock&v){v=pending_clock;return true;};auto pk=pending.collector();assert(pk.collect(c)&&c.elapsed_ticks==0);pending.put(pending.e.identity.image+0xE4,1);assert(pk.collect(c));pending_clock.logic_step++;assert(!pk.collect(c)&&pk.error()==Error::Clock&&c.image.empty());pending_clock.logic_step=100;pending.put(pending.e.identity.image+0xE4,2);assert(!pk.collect(c)&&pk.error()==Error::Clock);pending.put(pending.e.identity.image+0xE4,4);assert(!pk.collect(c)&&pk.error()==Error::Clock);pending.put(pending.e.identity.image+0xE4,0);pending_clock.elapsed_ticks=1;assert(!pk.collect(c)&&pk.error()==Error::Clock);pending_clock.elapsed_ticks=0;pending.put(pending.e.identity.image+0x128,0);assert(!pk.collect(c)&&pk.error()==Error::Clock);
 for(unsigned fault=0;fault<10;++fault){Fake x;setup(x);Clock sample{x.e.identity,10,2,7};unsigned calls=0;x.config.clock=[&](Clock&v){v=sample;if(fault==5&&++calls==2)++v.serial;return fault!=6;};if(fault==0)x.config.clock={};if(fault==1)x.e.program.reset();if(fault==2)x.allocation_extent=0x520;if(fault==3)x.memory[x.e.identity.image].resize(0xE5F);if(fault==4)sample.identity.generation++;if(fault==7)sample.logic_step=0;if(fault==8){sample.serial=0;x.put(x.e.identity.image+0xE4,2);}if(fault==9)sample.elapsed_ticks=301;auto k=x.collector();c.image=turn.e.program->pristine();assert(!k.collect(c)&&c.image.empty());if(fault==3)assert(k.error()==Error::Read);if(fault==2)assert(k.error()==Error::Ownership);if(fault==0||fault==1)assert(k.error()==Error::Configuration);}
 return 0;
}
