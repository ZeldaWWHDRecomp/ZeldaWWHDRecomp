// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_camera_restore.h"
#include <algorithm>
#include <cassert>
#include <array>
#include <cstdint>
#include <span>
using namespace story_native_camera_restore;
static void put(std::span<uint8_t>b,unsigned o,uint32_t v){for(unsigned n=0;n<4;++n)b[o+n]=uint8_t(v>>(24-8*n));}
static uint32_t word(std::span<const uint8_t>b,unsigned o){return uint32_t(b[o])<<24|uint32_t(b[o+1])<<16|uint32_t(b[o+2])<<8|b[o+3];}
struct Fixture {
 Expected e;Camera live;Boundary b;
 Fixture(){e.identity={0x10474374,0x60000000,0x520,77,88,3,4};auto&c=e.camera;c.address=0x20004248;c.pid=12;c.current_type=c.next_type=1;c.event_type=2;c.style=c.param_index=3;c.style_table=0x1004487C;c.style_count=145;c.param_pointer=c.style_table+3*0x84;c.type_table=0x10049350;c.type_count=63;c.bg=9;c.trim=2;
  std::fill(c.initial_view010.begin(),c.initial_view010.end(),0x11);std::fill(c.view03C.begin(),c.view03C.end(),0x22);std::fill(c.positions0A4.begin(),c.positions0A4.end(),0x33);std::fill(c.work37C.begin(),c.work37C.end(),0x44);std::fill(c.event_data.begin(),c.event_data.end(),0x55);put(c.event_data,12,0xFFFFFFFFu);c.timer=30;put(c.work37C,8,30);c.counter108=4;c.counter118=5;c.elapsed11C=6;put(c.engine100,8,4);put(c.engine100,0x18,5);put(c.engine100,0x1C,6);put(c.engine100,0x14,7);put(c.mode_phase13C,0,7);put(c.mode_phase13C,4,7);put(c.mode_phase13C,8,1);put(c.type_gate518,0,2);
  live=c;live.current_type=live.next_type=2;live.style=live.param_index=4;live.param_pointer=live.style_table+4*0x84;put(live.event_data,12,1);live.flags=1;live.bg=8;live.trim=1;
  for(auto*array:{&live.initial_view010})array->fill(0x66);live.view03C.fill(0x77);live.positions0A4.fill(0x88);live.work37C.fill(0x99);live.timer=10;put(live.work37C,8,10);live.engine100.fill(0);live.counter108=live.counter118=live.elapsed11C=0;live.mode_phase13C.fill(0);put(live.mode_phase13C,0,12);put(live.mode_phase13C,4,12);live.type_gate518.fill(0);
  b.identity=e.identity;b.exclusive=b.fenced=b.manager_owned=true;b.control_mode=3;b.event_id=-1;b.serial=9;b.logic_step=100;
 }
 static bool verify(const Camera&c,const Identity&){return c.type_count==63&&c.style_count==145&&c.style<145&&c.param_index==c.style&&c.param_pointer==c.style_table+c.style*0x84;}
};
static Camera decode(const Camera&meta,std::span<const uint8_t>raw){Camera c=meta;auto copy=[&](unsigned off,auto&array){std::copy_n(raw.begin()+off,array.size(),array.begin());};copy(0x10,c.initial_view010);copy(0x3C,c.view03C);copy(0xA4,c.positions0A4);copy(0x100,c.engine100);copy(0x13C,c.mode_phase13C);copy(0x37C,c.work37C);copy(0x3FC,c.event_data);copy(0x518,c.type_gate518);std::copy_n(c.engine100.begin(),3,c.done100.begin());c.bg=word(raw,0x68);c.trim=word(raw,0x600);c.flags=word(raw,0x510);c.style=word(raw,0x514);c.current_type=word(raw,0x51C);c.next_type=word(raw,0x520);c.param_pointer=word(raw,0x8A8);c.param_index=word(raw,0x8AC);c.timer=word(c.work37C,8);c.counter108=word(c.engine100,8);c.counter118=word(c.engine100,0x18);c.elapsed11C=word(c.engine100,0x1C);return c;}
int main(){
 Fixture f;auto p=prepare(f.e,f.live,f.b,Fixture::verify);assert(p.valid()&&p.segments().size()==12);std::array<uint8_t,0x8E0>raw;raw.fill(0xA5);auto before=raw;
 std::array<bool,0x8E0>written{};for(const auto&s:p.segments()){assert(s.offset+s.bytes<=raw.size());for(unsigned j=0;j<s.bytes;++j){assert(!written[s.offset+j]);written[s.offset+j]=true;raw[s.offset+j]=s.data[j];}}
 assert(p.verify_post(decode(f.live,raw),f.b,Fixture::verify));for(unsigned n=0;n<raw.size();++n)if(!written[n])assert(raw[n]==before[n]);
 // Every emitted segment is required by full poststate equality, and unrelated
 // class bytes are excluded from this transaction's write capability.
 for(const auto&s:p.segments()){auto incomplete=raw;incomplete[s.offset]^=1;assert(!p.verify_post(decode(f.live,incomplete),f.b,Fixture::verify));}
 for(unsigned n=0;n<14;++n){auto x=f;if(n==0)x.b.identity.generation++;if(n==1)x.live.pid++;if(n==2)x.b.fenced=false;if(n==3)x.b.callback_depth=1;if(n==4)x.b.foreign_depth=1;if(n==5)x.b.foreign=true;if(n==6)x.b.manager_owned=false;if(n==7)x.b.control_mode=2;if(n==8)x.b.partner1=123;if(n==9)x.b.partner2=123;if(n==10)x.b.event_id=0;if(n==11)x.live.param_pointer+=4;if(n==12)x.b.talk_partner=123;if(n==13)x.b.item_partner=123;assert(!prepare(x.e,x.live,x.b,Fixture::verify).valid());}
 auto bad=f.live;put(bad.event_data,12,0xFFFFFFFFu);assert(!prepare(f.e,bad,f.b,Fixture::verify).valid());bad=f.live;bad.timer++;assert(!prepare(f.e,bad,f.b,Fixture::verify).valid());assert(!prepare(f.e,f.live,f.b,{}).valid());assert(!prepare(f.e,f.live,f.b,[](const auto&,const auto&){return false;}).valid());
 auto end=f.b;++end.serial;assert(!p.verify_post(f.e.camera,end,Fixture::verify));end=f.b;++end.logic_step;assert(!p.verify_post(f.e.camera,end,Fixture::verify));assert(!p.verify_post(f.live,f.b,Fixture::verify));Plan fabricated;assert(!fabricated.valid()&&fabricated.segments().empty()&&!fabricated.verify_post(f.e.camera,f.b,Fixture::verify));
 // These metadata fields are NOT restoration segments. Even otherwise
 // coherent idle cameras must match their immutable baseline before any write.
 for(unsigned fault=0;fault<3;++fault){auto changed=f.e.camera;
  if(fault==0)changed.event_type=3; // valid index, distinct from idle current1
  if(fault==1)changed.type_table+=0x1000;
  if(fault==2){changed.style_table+=0x1000;changed.param_pointer=changed.style_table+changed.style*0x84;}
  assert(Fixture::verify(changed,f.e.identity));
  auto refused=prepare(f.e,changed,f.b,Fixture::verify);
  assert(!refused.valid()&&refused.segments().empty());
 }
 // Native table counts are fixed63/145; differing counts cannot be a valid
 // native fixture, and must fail the unchanged policy before any segments.
 for(unsigned fault=0;fault<2;++fault){auto changed=f.e.camera;
  if(!fault)++changed.type_count;else ++changed.style_count;
  assert(!Fixture::verify(changed,f.e.identity));
  auto refused=prepare(f.e,changed,f.b,Fixture::verify);
  assert(!refused.valid()&&refused.segments().empty());
 }
 // A not-yet-entered published camera can also restore its exact baseline.
 assert(prepare(f.e,f.e.camera,f.b,Fixture::verify).valid());
}
