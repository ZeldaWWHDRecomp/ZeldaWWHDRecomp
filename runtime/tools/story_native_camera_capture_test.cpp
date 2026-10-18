// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_camera_capture.h"
#include <algorithm>
#include <cassert>
#include <map>
#include <vector>
#include <cstdint>
#include <span>
using namespace story_native_camera_capture;
static void put(std::span<uint8_t>b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
struct Fixture {
 Layout l{0x1046F0B0,0x101D55F0,0x101D5634,0x101D5538,{0x025021A4,0x02502288,0x024FFA3C,0x0250235C,0x024FFC40},{0x10049348,0x10049350,0x10044878,0x1004487C}};
 Expected e{};uint32_t process=0x20001000;std::map<uint32_t,std::vector<uint8_t>> memory;
 unsigned reads=0,process_reads=0;bool mutate=false,throw_read=false;int change_metadata=0;
 Fixture(){
  e.camera.address=process+0x248;e.camera.pid=17;e.camera.event_type=2;e.camera.type_table=l.tables.types;e.camera.style_table=l.tables.styles;e.camera.type_count=63;e.camera.style_count=145;
  memory[l.play+0x5AF8].resize(4);put(memory[l.play+0x5AF8],0,process);
  auto&b=memory[process];b.resize(0xB28);put(b,4,e.camera.pid);b[0xE]=1;b[0xF]=0xDC;put(b,0x10,l.profile0);put(b,0x228,l.methods);put(b,0x248,process);
  auto&p=memory[l.profile0];p.resize(0x40);p[8]=1;p[9]=0xDC;put(p,0x10,0xB28);put(p,0x3C,l.methods);
  auto&m=memory[l.methods];m.resize(20);for(unsigned n=0;n<5;++n)put(m,n*4,l.functions[n]);
  memory[l.tables.type_count].resize(4);put(memory[l.tables.type_count],0,63);memory[l.tables.style_count].resize(4);put(memory[l.tables.style_count],0,145);
  auto w=[&](unsigned p,uint32_t v){put(b,0x248+p,v);};w(0x8B4,2);w(0x51C,2);w(0x520,1);w(0x514,4);w(0x8A8,l.tables.styles+3*0x84);w(0x8AC,3);w(0x384,29);w(0x108,5);w(0x118,6);w(0x11C,7);w(0x510,1);w(0x68,9);w(0x600,2);
  // Deliberately incoherent partial cache/type tuple: raw capture must not
  // reject or reinterpret intermediate detach state as a valid native policy.
  std::fill_n(b.begin()+0x248+0x10,0x2C,0x11);std::fill_n(b.begin()+0x248+0x3C,0x28,0x22);std::fill_n(b.begin()+0x248+0xA4,0x40,0x33);std::fill_n(b.begin()+0x248+0x3FC,0x114,0x44);
 }
 Read reader(){return [&](uint32_t a,std::span<uint8_t>b){++reads;if(throw_read)throw 4;auto it=memory.find(a);if(it==memory.end()||b.size()!=it->second.size())return false;if(a==process&&++process_reads==2&&mutate)it->second[0x248+0x3C]^=1;if(a==l.tables.style_count&&process_reads==2&&change_metadata)put(it->second,0,144);std::copy(it->second.begin(),it->second.end(),b.begin());return true;};}
 bool run(Camera&out){return capture(reader(),l,e,out);}
};
static void denied(Fixture&f){Camera out;out.pid=42;assert(!f.run(out)&&out==Camera{});}
int main(){
 {Fixture f;Camera c;assert(f.run(c));assert(c.address==f.e.camera.address&&c.pid==17&&c.current_type==2&&c.next_type==1&&c.style==4&&c.param_index==3&&c.param_pointer==f.l.tables.styles+3*0x84);assert(c.timer==29&&c.counter108==5&&c.counter118==6&&c.elapsed11C==7&&c.bg==9&&c.trim==2&&c.initial_view010[0]==0x11&&c.view03C[0]==0x22&&c.positions0A4[0]==0x33&&c.event_data[0]==0x44);assert(c.type_count==63&&c.style_count==145&&f.process_reads==2);}
 {Fixture f;put(f.memory[f.process],4,18);denied(f);}
 {Fixture f;put(f.memory[f.process],0x10,f.l.profile1);denied(f);}
 {Fixture f;f.memory[f.process][0xF]=0xDE;denied(f);}
 {Fixture f;f.memory[f.process][0xC]=3;denied(f);}
 {Fixture f;put(f.memory[f.l.profile0],0x10,0xB24);denied(f);}
 {Fixture f;put(f.memory[f.l.profile0],0x3C,f.l.methods+4);denied(f);}
 {Fixture f;put(f.memory[f.process],0x228,f.l.methods+4);denied(f);}
 {Fixture f;put(f.memory[f.process],0x248,f.process+4);denied(f);}
 {Fixture f;put(f.memory[f.l.play+0x5AF8],0,f.process+4);denied(f);}
 for(unsigned n=0;n<5;++n){Fixture f;put(f.memory[f.l.methods],n*4,f.l.functions[n]+4);denied(f);}
 {Fixture f;put(f.memory[f.l.tables.type_count],0,62);denied(f);}
 {Fixture f;put(f.memory[f.l.tables.style_count],0,144);denied(f);}
 {Fixture f;f.e.camera.type_table+=4;denied(f);}
 {Fixture f;f.e.camera.style_table+=4;denied(f);}
 {Fixture f;put(f.memory[f.process],0x248+0x8B4,3);denied(f);}
 {Fixture f;f.mutate=true;denied(f);}
 {Fixture f;f.change_metadata=1;denied(f);}
 {Fixture f;f.throw_read=true;denied(f);}
 {Fixture f;f.l.tables.styles=0x4FFFFFF0;denied(f);assert(!f.reads);}
 {Fixture f;f.l.play=0xFFFFFFFC;denied(f);assert(!f.reads);}
 {Fixture f;f.e.camera.address=0x10000000;denied(f);assert(!f.reads);}
 {Fixture f;f.l.methods|=1;denied(f);assert(!f.reads);}
 {Fixture f;put(f.memory[f.l.play+0x5AF8],0,0x4FFFFFF0);denied(f);}
 {Fixture f;auto&p=f.memory[f.l.profile0];auto&p1=f.memory[f.l.profile1];p1=p;p1[9]=0xDD;f.memory[f.process][0xF]=0xDD;put(f.memory[f.process],0x10,f.l.profile1);Camera c;assert(f.run(c));}
}
