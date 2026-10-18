// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_people_pose_capture.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
using namespace story_native_people_pose_capture;
using story_native_people_pose_plan::Snapshot;
namespace {
struct Fixture {
 static constexpr uint32_t actor=0x47000000,morf=0x48000000,model=0x48001000,profile=0x101C4CD0,vtable=0x100209C4,npc_dat=0x101C41D4;
 std::map<uint32_t,std::vector<uint8_t>> memory{{actor,std::vector<uint8_t>(0x920)},{morf,std::vector<uint8_t>(0xC8)},{model,std::vector<uint8_t>(0xBC)},{profile,std::vector<uint8_t>(0x14)},{npc_dat,std::vector<uint8_t>(0x60)}};
 story_native_event_observation::Expected expected{};
 unsigned verifies{},actor_reads{},image_reads{},dat_reads{};bool accessor=true,fail_read=false,mutate=false,mutate_image=false,mutate_dat=false;
 Fixture(){
  expected.identity={0x10400000,0x60000000,0x520,12,13,2,123};expected.room_number=11;
  auto&a=expected.actors[0];a.address=actor;a.pid=99;a.token=7;a.profile=0x175;a.subtype=1;a.fingerprint[0]=42;
  put(actor,4,99);memory[actor][0xF]=0x75;memory[actor][0xE]=1;put(actor,0x10,profile);put(actor,0xB4,vtable);put(actor,0xB0,0x00FF0000);
  memory[actor][0x2DD]=memory[actor][0x905]=1;memory[actor][0x326]=11;
  put(profile,0x10,0x920);memory[profile][8]=1;memory[profile][9]=0x75;
  put(actor,0x8A0,npc_dat);put(npc_dat,0x1C,0x432A0000);put(npc_dat,0x24,0x43160000);
  put(actor,0x44C,morf);put(morf,0x90,model);put(model,0xB8,actor);
  memory[actor][0x321]=41;memory[actor][0x329]=42;memory[actor][0x3AD]=43;memory[actor][0x885]=44;memory[actor][0x8F9]=45;
  memory[actor][0x8D8]=1;memory[actor][0x8FD]=1;memory[actor][0x8FF]=1;memory[actor][0x90D]=3;put(actor,0x2E0,0x800);
 }
 void put(uint32_t base,unsigned off,uint32_t v){for(unsigned n=0;n<4;++n)memory[base][off+n]=uint8_t(v>>(24-8*n));}
 bool read(uint32_t p,std::span<uint8_t>out){
  if(fail_read)return false;
  for(auto&[base,bytes]:memory)if(p>=base&&uint64_t(p)+out.size()<=uint64_t(base)+bytes.size()){
   if(p==npc_dat+0x18&&++dat_reads==2&&mutate_dat)bytes[0x18]=1;
   if(p==actor&&++actor_reads==2&&mutate)bytes[0x321]++;
   if(p==expected.identity.image&&++image_reads==2&&mutate_image)bytes[0x187]^=1;
   std::copy_n(bytes.begin()+p-base,out.size(),out.begin());return true;
  }assert(false&&"unbounded guest read");return false;
 }
 bool capture(Snapshot&s,Mode mode=Mode::Baseline,unsigned n=0){return story_native_people_pose_capture::capture([&](auto p,auto b){return read(p,b);},[&](const auto&a,const auto&i){++verifies;return accessor&&a==expected.actors[n]&&i==expected.identity;},{profile,vtable},expected,n,s,mode);}
};
}
int main(){
 Snapshot s;
 {Fixture f;const auto before=f.memory;assert(f.capture(s));assert(f.memory==before&&f.verifies==3&&f.actor_reads==2&&f.dat_reads==2);assert(s.identity.actor==f.actor&&s.identity.morf==f.morf&&s.identity.model==f.model&&s.identity.generation==2);assert(s.angles[1]==41&&s.shape[1]==42&&s.joint[1]==43&&s.target[1]==44&&s.velocity[1]==45&&s.status==0x800&&s.target_mode==3);}
 // Native parameter pointers and exact zero horizontal offsets are actual
 // prerequisites in every mode, not snapshot fields or invented pose writes.
 for(unsigned fault=0;fault<6;++fault){Fixture f;if(fault==0)f.put(f.actor,0x8A0,f.npc_dat+4);if(fault==1)f.put(f.npc_dat,0x18,1);if(fault==2)f.put(f.npc_dat,0x1C,0x43290000);if(fault==3)f.put(f.npc_dat,0x20,0x3F800000);if(fault==4)f.put(f.npc_dat,0x24,0x43150000);if(fault==5)f.mutate_dat=true;assert(!f.capture(s)&&s.identity.actor==0);}
 {Fixture f;f.put(f.actor,0x3E0,f.expected.identity.image+0xF0);f.put(f.actor,0x3E8,f.actor);f.put(f.npc_dat,0x20,1);assert(!f.capture(s,Mode::Owned));}
 // Bound native consumer capture is distinct from the immutable idle cut.
 {Fixture f;f.put(f.actor,0x3E0,f.expected.identity.image+0xF0);f.put(f.actor,0x3E8,f.actor);f.put(f.actor,0x3E4,0);assert(!f.capture(s));assert(f.capture(s,Mode::Owned));}
 for(unsigned category=0;category<7;++category){Fixture f;f.put(f.actor,0x3E0,f.expected.identity.image+0xF0);f.put(f.actor,0x3E8,f.actor);f.put(f.actor,0x3E4,0);
 switch(category){case 0:f.put(f.actor,0x3E0,f.expected.identity.image+0x140);break;case 1:f.put(f.actor,0x3E8,f.actor+4);break;case 2:f.put(f.actor,0x3E4,2);break;case 3:f.put(f.actor,0x3F0,3);break;case 4:f.memory[f.actor][0x440]=2;break;case 5:f.put(f.actor,0x3EC,f.actor+4);break;case 6:f.put(f.actor,0x448,f.actor+0x3B0);break;}
 assert(!f.capture(s,Mode::Owned)&&s.identity.actor==0);}
 {Fixture f;f.expected.actors[0].cut[4]=f.expected.actors[0].cut[5]=f.expected.actors[0].cut[6]=f.expected.actors[0].cut[7]=0xFF;f.put(f.actor,0x3E0,f.expected.identity.image+0xF0);f.put(f.actor,0x3E8,f.actor);f.put(f.actor,0x3E4,0xFFFFFFFF);f.put(f.actor,0x3F0,0xFFFFFFFF);assert(f.capture(s,Mode::Owned));}
 // Typed TURN actor1 can inherit baseline staffID0 only before its first
 // own CommonCut lookup. Primed action3 must bind staff1 and currentcut8.
 auto turn=[](Fixture&f){auto a=f.expected.actors[0];f.expected.actors[1]=a;f.expected.actors[0].address+=0x2000;++f.expected.actors[0].pid;++f.expected.actors[0].token;story_native_turn_camera_image::Program p;p.camera.start_eye={0,10,20};p.camera.end_eye={10,10,20};f.expected.program=story_native_event_program::Program::turn(p,301);f.expected.identity.bytes=0xE60;f.memory[f.expected.identity.image]=std::vector<uint8_t>(f.expected.program->pristine().begin(),f.expected.program->pristine().end());f.put(f.expected.identity.image,0xE4,2);f.put(f.expected.identity.image,0x178,8);f.put(f.expected.identity.image,0x17C,3);f.put(f.actor,0x3E0,f.expected.identity.image+0x140);f.put(f.actor,0x3E8,f.actor);f.put(f.actor,0x3E4,1);f.put(f.actor,0x3F0,3);};
 {Fixture f;turn(f);const auto before=f.memory;assert(f.capture(s,Mode::Owned,1)&&f.memory==before&&f.image_reads==2&&s.cut[0x13]==3);f.actor_reads=f.image_reads=0;f.put(f.actor,0x3E4,0);f.put(f.actor,0x3F0,0xFFFFFFFF);f.put(f.expected.identity.image,0x17C,0xFFFFFFFF);assert(f.capture(s,Mode::Owned,1));}
 for(unsigned fault=0;fault<12;++fault){Fixture f;turn(f);switch(fault){case 0:f.put(f.actor,0x3E4,0);break;case 1:f.put(f.expected.identity.image,0x178,7);break;case 2:f.put(f.expected.identity.image,0x17C,0);break;case 3:f.put(f.actor,0x3F0,0);break;case 4:f.memory[f.expected.identity.image][0x500]^=1;break;case 5:f.expected.program.reset();break;case 6:f.put(f.expected.identity.image,0xE4,1);break;case 7:f.put(f.actor,0x3E4,0);f.put(f.actor,0x3F0,0xFFFFFFFF);break;case 8:f.memory[f.expected.identity.image][0x183]=2;break;case 9:f.mutate_image=true;break;case 10:f.memory[f.actor][0xC]=3;break;case 11:f.put(f.model,0xB8,f.actor+4);break;}assert(!f.capture(s,Mode::Owned,1)&&s.identity.actor==0);}
 {Fixture f;turn(f);f.put(f.expected.identity.image,0x178,1);f.put(f.expected.identity.image,0x17C,0);f.put(f.actor,0x3F0,0);assert(f.capture(s,Mode::Owned,1));}
 // NONE is accepted only as a fully exhausted TURN data observation.
 for(unsigned fault=0;fault<11;++fault){Fixture f;turn(f);f.memory[f.expected.identity.manager].resize(0x540);f.put(f.expected.identity.image,0xE4,0);const std::array<uint32_t,4>cuts{0,1,6,5},actions{0,0,5,1};for(unsigned n=0;n<4;++n){f.put(f.expected.identity.image,0x128+n*0x50,cuts[n]);f.put(f.expected.identity.image,0x12C+n*0x50,actions[n]);}f.memory[f.expected.identity.manager][0x3E]=1;f.memory[f.expected.identity.manager][0x3F]=0xFF;f.put(f.actor,0x3F0,0);assert(!f.capture(s,Mode::Owned,1));f.image_reads=0;switch(fault){case 1:f.put(f.expected.identity.image,0xE4,2);break;case 2:f.memory[f.expected.identity.manager][0x3F]=0x7F;break;case 3:f.memory[f.expected.identity.manager][0x40]=1;break;case 4:f.put(f.expected.identity.image,0x128,7);break;case 5:f.put(f.expected.identity.image,0x1CC,4);break;case 6:f.put(f.actor,0x3E4,0);break;case 7:f.put(f.actor,0x3F0,3);break;case 8:f.put(f.actor,0x3F8,1);break;case 9:f.mutate_image=true;break;case 10:f.put(f.npc_dat,0x18,1);break;}const bool result=f.capture(s,Mode::Exhausted,1);if(result!=(fault==0))std::fprintf(stderr,"Exhausted fixture fault=%u result=%u\n",fault,unsigned(result));assert(result==(fault==0));}
 // Native allocation/backpointer and immutable ABI mismatches independently fail.
 for(unsigned category=0;category<13;++category){Fixture f;switch(category){
 case 0:f.put(f.actor,4,100);break;case 1:f.put(f.actor,0x10,0);break;case 2:f.put(f.actor,0xB4,0);break;
 case 3:f.put(f.actor,0xB0,1);break;case 4:f.memory[f.actor][0x905]=2;break;case 5:f.memory[f.actor][0x326]=12;break;
 case 6:f.memory[f.actor][0xC]=3;break;case 7:f.put(f.profile,0x10,0x924);break;
 case 8:f.put(f.actor,0x44C,0xFFFFFFFC);break;case 9:f.put(f.morf,0x90,f.actor);break;
 case 10:f.put(f.model,0xB8,f.actor+4);break;case 11:f.expected.actors[0].fingerprint={};break;case 12:f.expected.actors[0].cut[0]=1;break;
 }assert(!f.capture(s)&&s.identity.actor==0);}
 {Fixture f;f.mutate=true;assert(!f.capture(s)&&s.identity.actor==0);}
 {Fixture f;f.accessor=false;assert(!f.capture(s)&&f.actor_reads==0);}
 {Fixture f;f.fail_read=true;assert(!f.capture(s));}
 {Fixture f;unsigned calls=0;assert(!capture([&](auto p,auto b){return f.read(p,b);},[&](const auto&,const auto&){return ++calls<3;},{f.profile,f.vtable},f.expected,0,s)&&s.identity.actor==0);}
 {Fixture f;assert(!capture([&](auto,auto)->bool{throw std::runtime_error("read fault");},[](const auto&,const auto&){return true;},{f.profile,f.vtable},f.expected,0,s)&&s.identity.actor==0);}
 {Fixture f;assert(!capture([&](auto p,auto b){return f.read(p,b);},[](const auto&,const auto&){return true;},{f.profile,f.vtable},f.expected,2,s));}
}
