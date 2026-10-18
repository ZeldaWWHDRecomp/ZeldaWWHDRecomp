// SPDX-License-Identifier: MPL-2.0
#include "story_native_camera_capture.h"
#include "runtime.h"
#include <algorithm>
namespace story_native_camera_capture {namespace {
bool span(uint32_t a,uint32_t bytes){return !(a&3)&&a>=0x10000000u&&bytes&&uint64_t(a)+bytes<=0x50000000u;}
uint32_t word(std::span<const uint8_t>b,unsigned p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
uint16_t half(std::span<const uint8_t>b,unsigned p){return uint16_t(b[p])<<8|b[p+1];}
struct Sample {
 std::array<uint8_t,0xB28> instance{};
 std::array<uint8_t,0x40> profile{};
 std::array<uint8_t,20> methods{};
 uint32_t process{},types{},styles{};
 bool operator==(const Sample&)const=default;
};
bool sample(const Read&read,const Layout&l,const Expected&e,Sample&s){
 std::array<uint8_t,4> scalar{};
 if(!read(l.play+0x5AF8,scalar))return false;s.process=word(scalar);
 if(!span(s.process,0xB28)||s.process!=e.camera.address-0x248||!read(s.process,s.instance))return false;
 const auto&b=s.instance;const uint32_t name=half(b,0xE),profile=word(b,0x10);
 if(word(b,4)!=e.camera.pid||b[0xC]==3||(name!=0x1DC&&name!=0x1DD)||profile!=(name==0x1DC?l.profile0:l.profile1)||word(b,0x228)!=l.methods||word(b,0x248)!=s.process)return false;
 if(!read(profile,s.profile)||half(s.profile,8)!=name||word(s.profile,0x10)!=0xB28||word(s.profile,0x3C)!=l.methods||!read(l.methods,s.methods))return false;
 for(unsigned n=0;n<5;++n)if(word(s.methods,n*4)!=l.functions[n])return false;
 if(!read(l.tables.type_count,scalar))return false;s.types=word(scalar);
 if(!read(l.tables.style_count,scalar))return false;s.styles=word(scalar);
 return s.types==63&&s.styles==145&&s.types==e.camera.type_count&&s.styles==e.camera.style_count&&word(b,0x248+0x8B4)==e.camera.event_type;
}
Camera decode(const Layout&l,const Sample&s){
 Camera c;c.address=s.process+0x248;c.pid=word(s.instance,4);
 auto w=[&](unsigned p){return word(s.instance,0x248+p);};
 auto copy=[&](unsigned p,auto&b){std::copy_n(s.instance.begin()+0x248+p,b.size(),b.begin());};
 c.flags=w(0x510);c.timer=w(0x384);c.current_type=w(0x51C);c.next_type=w(0x520);c.event_type=w(0x8B4);c.style=w(0x514);c.bg=w(0x68);c.trim=w(0x600);c.counter108=w(0x108);c.counter118=w(0x118);c.elapsed11C=w(0x11C);c.type_table=l.tables.types;c.style_table=l.tables.styles;c.type_count=s.types;c.style_count=s.styles;c.param_pointer=w(0x8A8);c.param_index=w(0x8AC);
 copy(0x518,c.type_gate518);copy(0x100,c.engine100);copy(0x13C,c.mode_phase13C);copy(0x10,c.initial_view010);copy(0x100,c.done100);copy(0x37C,c.work37C);copy(0xA4,c.positions0A4);copy(0x3C,c.view03C);copy(0x3FC,c.event_data);return c;
}
}
bool capture(const Read&read,const Layout&l,const Expected&e,Camera&out){
 out={};
 if(!read||!span(l.play,0x5AFC)||!span(l.profile0,0x40)||!span(l.profile1,0x40)||l.profile0==l.profile1||!span(l.methods,20)||!span(l.tables.type_count,4)||!span(l.tables.style_count,4)||!span(l.tables.types,63*0x40)||!span(l.tables.styles,145*0x84)||e.camera.type_table!=l.tables.types||e.camera.style_table!=l.tables.styles||!span(e.camera.address,0x8B8)||e.camera.address<mem::kMem2Start+0x248u||!e.camera.pid||e.camera.pid==~0u||e.camera.event_type>=63)return false;
 for(auto f:l.functions)if(!f||(f&3))return false;
 try{Sample first,second;if(!sample(read,l,e,first)||!sample(read,l,e,second)||first!=second)return false;out=decode(l,second);return true;}catch(...){return false;}
}
}
