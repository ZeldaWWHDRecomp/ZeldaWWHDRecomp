// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_people_pose_record.h"
#include <cassert>
#include <vector>
#include <cstdint>
#include <span>
namespace R=story_native_people_pose_record;
using story_native_event_observation::Expected;
static void put(auto&b,size_t p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-n*8));}
static void checksum(std::vector<uint8_t>&b){uint32_t h=2166136261u;for(size_t p=0;p<b.size();++p)h=(h^(p>=12&&p<16?0:b[p]))*16777619u;put(b,12,h);}
static bool empty(const R::Poses&p){return !p[0].identity.actor&&!p[1].identity.actor&&p[0].status==0&&p[1].status==0;}
int main(){
 Expected e;e.identity={0x20000000,0x60000000,0x520,1,2,3,0x1122334455667788ull};e.stage_name={'s','e','a'};e.room_number=11;
 e.camera.address=0x20003248;e.camera.pid=12;e.camera.event_type=3;e.camera.type_table=0x10049350;e.camera.type_count=63;e.camera.style_table=e.camera.param_pointer=0x1004487C;e.camera.style_count=145;put(e.camera.event_data,12,~0u);
 R::Definition definition{};definition[0]=1;definition[31]=255;R::Poses poses{};
 auto pattern=[](auto&array,unsigned seed){for(size_t k=0;k<array.size();++k)array[k]=uint8_t(k*29+seed);};
 for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x20001000+n*0x1000;a.pid=10+n;a.token=1+n;a.profile=0x175;a.subtype=1;pattern(a.fingerprint,n+1);a.cut[0x60]=a.cut[0x61]=1;auto&s=poses[n];s.identity={a.address,a.pid,a.token,1,2,3,0x30000000+n*0x1000,0x31000000+n*0x1000};s.cut=a.cut;pattern(s.angles,3+n);pattern(s.shape,5+n);pattern(s.joint,7+n);pattern(s.target,9+n);pattern(s.velocity,11+n);s.status=0xA000+s.identity.pid;s.head_only=0xFE;s.noticed=0xAB;s.talk_ready=0x98;s.target_mode=0xFF;}
 std::vector<uint8_t>b,round;assert(R::encode(e,definition,poses,b)&&b.size()==R::kBytes&&b.size()<=1024);
 assert(b[0]=='N'&&b[1]=='P'&&b[2]=='R'&&b[3]=='1'&&b[7]==1&&b[10]==2&&b[11]==92);
 R::Poses decoded{};assert(R::decode(b,e,definition,decoded));assert(R::encode(e,definition,decoded,round)&&round==b);
 assert(decoded[0].identity==poses[0].identity&&decoded[1].joint==poses[1].joint&&decoded[1].head_only==0xFE&&decoded[1].target_mode==255);
 for(size_t p=0;p<b.size();++p){auto bad=b;bad[p]^=1;decoded=poses;assert(!R::decode(bad,e,definition,decoded)&&empty(decoded));}
 for(size_t p=0;p<b.size();++p){decoded=poses;assert(!R::decode(std::span(b).first(p),e,definition,decoded)&&empty(decoded));}
 auto bad=b;bad.push_back(0);assert(!R::decode(bad,e,definition,decoded));
 // Recomputed checksums cannot bypass reserved fields or copied identity.
 for(unsigned p:{16u,20u,24u,56u,88u,152u,180u}){bad=b;bad[p]^=1;checksum(bad);decoded=poses;assert(!R::decode(bad,e,definition,decoded)&&empty(decoded));}
 auto wrongdef=definition;wrongdef[31]^=1;assert(!R::decode(b,e,wrongdef,decoded));
 for(unsigned category=0;category<9;++category){auto changed=e;switch(category){case 0:++changed.identity.generation;break;case 1:++changed.identity.program;break;case 2:changed.identity.image+=16;break;case 3:++changed.actors[0].pid;break;case 4:changed.actors[1].fingerprint[31]^=1;break;case 5:++changed.camera.pid;break;case 6:changed.camera.initial_view010[0]^=1;break;case 7:changed.retail_flags[1279]^=1;break;case 8:++changed.frames;break;}decoded=poses;assert(!R::decode(b,changed,definition,decoded)&&empty(decoded));}
 for(unsigned category=0;category<9;++category){auto changed=poses;switch(category){case 0:++changed[0].identity.generation;break;case 1:++changed[1].identity.pid;break;case 2:++changed[0].identity.token;break;case 3:changed[0].identity.morf=0;break;case 4:changed[0].identity.model=0xFFFFFFFC;break;case 5:++changed[1].identity.model;break;case 6:changed[1].identity.morf=changed[0].identity.morf;break;case 7:changed[0].cut[1]=1;break;case 8:changed[0].identity.model=e.actors[1].address;break;}round={1,2,3};assert(!R::encode(e,definition,changed,round)&&round.empty());}
 assert(!R::encode(e,{},poses,round)&&round.empty());
}
