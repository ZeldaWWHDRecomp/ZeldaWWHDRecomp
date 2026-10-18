// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_record.h"
#include <cassert>
#include <vector>
#include <cstdint>
namespace R=story_native_event_record;
namespace O=story_native_event_observation;
namespace L=story_native_event_lease;
int main(){
 O::Expected e;e.identity={0x10010000,0x60010000,0x520,0x10020000,0x10030000,4,0x1122334455667788ull};
 e.room_number=42;e.frames=300;e.stage_name={'S','D','K','T','E','S','T',0};
 for(unsigned n=0;n<8;++n)e.retail_manager[n]=0x10040000+n*4;
 e.retail_camera_play=0x11;e.retail_current_event=0x22;for(size_t n=0;n<e.retail_flags.size();++n)e.retail_flags[n]=uint8_t(n*37+9);
 for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x10050000+n*0x1000;a.pid=100+n;a.token=200+n;a.profile=0x175;a.subtype=1;for(size_t k=0;k<a.cut.size();++k)a.cut[k]=uint8_t(k*13+n*17);for(size_t k=0;k<a.fingerprint.size();++k)a.fingerprint[k]=uint8_t(k*7+n+1);}
 auto&c=e.camera;c.address=0x10080000;c.pid=500;c.flags=0x1234;c.timer=30;c.current_type=c.next_type=2;c.event_type=7;c.style=5;c.bg=0x44332211;c.trim=2;c.counter108=3;c.counter118=4;c.elapsed11C=5;c.type_table=0x10049350;c.type_count=63;c.style_table=0x1004487C;c.style_count=145;c.param_index=5;c.param_pointer=c.style_table+5*0x84;
 auto pattern=[](auto&array,unsigned seed){for(size_t k=0;k<array.size();++k)array[k]=uint8_t(k*29+seed);};
 pattern(c.type_gate518,1);pattern(c.engine100,2);pattern(c.mode_phase13C,3);pattern(c.initial_view010,4);pattern(c.done100,5);pattern(c.work37C,6);pattern(c.positions0A4,7);pattern(c.view03C,8);pattern(c.event_data,9);
 for(unsigned k=0;k<4;++k){c.mode_phase13C[k]=0;c.mode_phase13C[4+k]=0;c.event_data[0x0C+k]=0xFF;}
 for(auto p:{L::Phase::Prepared,L::Phase::Published,L::Phase::Running,L::Phase::Draining}){
  std::vector<uint8_t>b,roundtrip;assert(R::encode(e,p,b)&&b.size()<R::kMaximumBytes);
  O::Expected d;L::Phase got=L::Phase::Empty;assert(R::decode(b,d,got)&&got==p&&d==e);assert(R::encode(d,got,roundtrip)&&roundtrip==b);
  auto damaged=b;damaged[20]^=1;assert(!R::decode(damaged,d,got));assert(got==L::Phase::Empty);
  damaged=b;damaged.pop_back();assert(!R::decode(damaged,d,got));
  damaged=b;damaged.push_back(0);assert(!R::decode(damaged,d,got));
  damaged=b;damaged[4]=3;assert(!R::decode(damaged,d,got));
  damaged=b;damaged[15]^=1;assert(!R::decode(damaged,d,got));
 }
 std::vector<uint8_t>b;assert(!R::encode(e,L::Phase::Retired,b)&&b.empty());
 e.camera.event_data[0x0C]=0;assert(!R::encode(e,L::Phase::Running,b));
 e.camera.event_data[0x0C]=0xFF;e.actors[1].token=e.actors[0].token;assert(!R::encode(e,L::Phase::Running,b));
 e.actors[1].token=201;e.actors[1].fingerprint=e.actors[0].fingerprint;assert(!R::encode(e,L::Phase::Running,b));
 e.actors[1].fingerprint.fill(0);assert(!R::encode(e,L::Phase::Running,b));
}
