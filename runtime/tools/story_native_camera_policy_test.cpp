// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_camera_policy.h"
#include <cassert>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <span>
namespace P=story_native_camera_policy;
void put(auto&b,size_t at,uint32_t v){for(unsigned i=0;i<4;++i)b[at+i]=uint8_t(v>>(24-i*8));}
int main(){
 P::Tables t{0x11001000,0x11002000,0x11004000,0x11005000};std::map<uint32_t,std::vector<uint8_t>> mem;mem[t.type_count].resize(4);put(mem[t.type_count],0,63);mem[t.style_count].resize(4);put(mem[t.style_count],0,145);auto&types=mem[t.types];types.resize(63*64);auto&styles=mem[t.styles];styles.resize(145*132);
 for(unsigned i=0;i<63;++i)for(unsigned m=0;m<20;++m){types[i*64+24+m*2]=0xFF;types[i*64+25+m*2]=0xFF;}
 auto slot=[&](unsigned type,unsigned mode,unsigned style){types[type*64+24+mode*2]=uint8_t(style>>8);types[type*64+25+mode*2]=uint8_t(style);};slot(1,12,10);slot(3,0,4);std::memcpy(types.data()+3*64,"Event",6);put(styles,10*132+4,10);put(styles,4*132+4,11);
 unsigned calls=0;P::Read read=[&](uint32_t ea,std::span<uint8_t> out){++calls;auto it=mem.find(ea);if(it==mem.end()||it->second.size()!=out.size())return false;std::copy(it->second.begin(),it->second.end(),out.begin());return true;};
 story_native_event_lease::Identity id{1,0x60010000,0x520,1,2,3,4};story_native_event_observation::Camera c;c.current_type=c.next_type=1;c.event_type=3;c.style=10;c.type_table=t.types;c.type_count=63;c.style_table=t.styles;c.style_count=145;c.param_index=10;c.param_pointer=t.styles+10*132;put(c.mode_phase13C,0,12);put(c.mode_phase13C,4,12);put(c.event_data,12,0xFFFFFFFF);assert(P::verify(t,read,c,id).valid&&calls==4);
 auto entered=c;entered.current_type=3;put(entered.event_data,12,1);assert(P::verify(t,read,entered,id).valid); // NotRun keeps prior cached style/mode12.
 entered.next_type=3;entered.style=entered.param_index=4;entered.param_pointer=t.styles+4*132;put(entered.mode_phase13C,0,0);put(entered.mode_phase13C,4,0);assert(P::verify(t,read,entered,id).valid);
 for(unsigned k=0;k<11;++k){auto bad=c;switch(k){case 0:bad.current_type=63;break;case 1:bad.type_table+=4;break;case 2:bad.type_count=62;break;case 3:bad.style_count=144;break;case 4:bad.param_pointer+=4;break;case 5:bad.param_index=0xFFFFFFFF;break;case 6:put(bad.mode_phase13C,0,20);break;case 7:put(bad.mode_phase13C,4,0);break;case 8:put(bad.event_data,12,0);break;case 9:bad.event_type=2;break;case 10:bad.style=145;break;}assert(!P::verify(t,read,bad,id).valid);}
 auto bad=entered;put(bad.event_data,12,0xFFFFFFFF);assert(!P::verify(t,read,bad,id).valid);
 slot(1,12,0xFFFE);assert(!P::verify(t,read,c,id).valid);slot(1,12,145);assert(!P::verify(t,read,c,id).valid);slot(1,12,10);
 put(styles,4*132+4,1);assert(!P::verify(t,read,c,id).valid);put(styles,4*132+4,11);put(styles,10*132+4,19);assert(!P::verify(t,read,c,id).valid);put(styles,10*132+4,10);
 put(mem[t.type_count],0,64);assert(!P::verify(t,read,c,id).valid);put(mem[t.type_count],0,63);assert(!P::verify(t,[](auto,auto){return false;},c,id).valid);assert(!P::verify(t,[](auto,auto)->bool{throw std::runtime_error("reader");},c,id).valid);
 // E60 must independently prove the actual selected retail Follow1 style;
 // generic WAIT accepts other source-backed native algorithms as before.
 auto turn=id;turn.bytes=0xE60;assert(!P::verify(t,read,c,turn).valid);
 put(styles,10*132+4,1);assert(P::verify(t,read,c,turn).valid);
 auto not_run=c;not_run.current_type=3;put(not_run.event_data,12,1);assert(P::verify(t,read,not_run,turn).valid);
 assert(P::verify(t,read,entered,turn).valid); // actual Event11 remains valid
 for(unsigned algorithm=0;algorithm<=18;++algorithm)if(algorithm!=1){put(styles,10*132+4,algorithm);assert(!P::verify(t,read,c,turn).valid&&!P::verify(t,read,not_run,turn).valid);}
 put(styles,10*132+4,10);assert(P::verify(t,read,c,id).valid);
 auto overflow=t;overflow.styles=0xFFFFFFFC;calls=0;assert(!P::verify(overflow,read,c,id).valid&&calls==0);
}
