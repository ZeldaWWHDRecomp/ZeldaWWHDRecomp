// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_people_pose_capture.h"
#include <cassert>
#include <map>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <span>

using story_native_people_pose_plan::Snapshot;
struct PoseFixture {
 static constexpr uint32_t actor=0x47000000,morf=0x48000000,model=0x48001000,profile=0x101C4CD0,vtable=0x100209C4,npc_dat=0x101C41D4;
 std::map<uint32_t,std::vector<uint8_t>> memory{{actor,std::vector<uint8_t>(0x920)},{morf,std::vector<uint8_t>(0xC8)},{model,std::vector<uint8_t>(0xBC)},{profile,std::vector<uint8_t>(0x14)},{npc_dat,std::vector<uint8_t>(0x60)}};
 story_native_event_observation::Expected expected{};
 unsigned verifies{},actor_reads{};bool accessor=true,fail_read=false,mutate=false;
 PoseFixture(){
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
   if(p==actor&&++actor_reads==2&&mutate)bytes[0x321]++;
   std::copy_n(bytes.begin()+p-base,out.size(),out.begin());return true;
  }assert(false&&"unbounded guest read");return false;
 }
 bool capture(Snapshot&s,story_native_people_pose_capture::Mode mode=story_native_people_pose_capture::Mode::Baseline){return story_native_people_pose_capture::capture([&](auto p,auto b){return read(p,b);},[&](const auto&a,const auto&i){++verifies;return accessor&&a==expected.actors[0]&&i==expected.identity;},{profile,vtable},expected,0,s,mode);}
};

// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_camera_capture.h"
#include <algorithm>
#include <cassert>
#include <map>

static void put(std::span<uint8_t>b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
struct CameraFixture {
 story_native_camera_capture::Layout l{0x1046F0B0,0x101D55F0,0x101D5634,0x101D5538,{0x025021A4,0x02502288,0x024FFA3C,0x0250235C,0x024FFC40},{0x10049348,0x10049350,0x10044878,0x1004487C}};
 story_native_camera_capture::Expected e{};uint32_t process=0x20001000;std::map<uint32_t,std::vector<uint8_t>> memory;
 unsigned reads=0,process_reads=0;bool mutate=false,throw_read=false;int change_metadata=0;
 CameraFixture(){
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
 story_native_camera_capture::Read reader(){return [&](uint32_t a,std::span<uint8_t>b){++reads;if(throw_read)throw 4;auto it=memory.find(a);if(it==memory.end()||b.size()!=it->second.size())return false;if(a==process&&++process_reads==2&&mutate)it->second[0x248+0x3C]^=1;if(a==l.tables.style_count&&process_reads==2&&change_metadata)put(it->second,0,144);std::copy(it->second.begin(),it->second.end(),b.begin());return true;};}
 bool run(story_native_camera_capture::Camera&out){return capture(reader(),l,e,out);}
};


#include "mods/story_native_wait_runtime_consumers.h"
#include <vector>
#include <cstdint>
#include <span>
int main(){
 PoseFixture a,b;CameraFixture cam;
 auto e=a.expected;e.camera=cam.e.camera;
 e.actors[1]=e.actors[0];e.actors[1].address+=0x10000;e.actors[1].pid++;e.actors[1].token++;
 auto memory=a.memory;
 auto actor2=b.memory[b.actor];auto morf2=b.memory[b.morf];auto model2=b.memory[b.model];
 auto putword=[](auto&v,unsigned p,uint32_t x){for(unsigned n=0;n<4;++n)v[p+n]=uint8_t(x>>(24-n*8));};
 putword(actor2,4,e.actors[1].pid);putword(actor2,0x44C,b.morf+0x10000);putword(morf2,0x90,b.model+0x10000);putword(model2,0xB8,e.actors[1].address);
 memory[e.actors[1].address]=actor2;memory[b.morf+0x10000]=morf2;memory[b.model+0x10000]=model2;
 memory.insert(cam.memory.begin(),cam.memory.end());
 auto read=[&](uint32_t at,std::span<uint8_t>out){for(auto&[base,bytes]:memory)if(at>=base&&uint64_t(at)+out.size()<=uint64_t(base)+bytes.size()){std::copy_n(bytes.begin()+at-base,out.size(),out.begin());return true;}return false;};
 auto verify=[&](const auto&actor,const auto&id){return id==e.identity&&(actor==e.actors[0]||actor==e.actors[1]);};
 story_native_wait_runtime_consumers::Poses poses;
 for(unsigned n=0;n<2;++n)assert(story_native_people_pose_capture::capture(read,verify,{a.profile,a.vtable},e,n,poses[n]));
 unsigned writes=0;story_native_wait_runtime_consumers::Sources sources;sources.read=read;sources.resident=verify;sources.retained=[&](const auto&x){return x==e;};sources.write=[&](uint32_t at,std::span<const uint8_t>bytes){++writes;for(auto&[base,v]:memory)if(at>=base&&uint64_t(at)+bytes.size()<=uint64_t(base)+v.size()){std::copy(bytes.begin(),bytes.end(),v.begin()+at-base);return true;}return false;};sources.quarantine=[]{};
 sources.boundary=[&](auto&out){out.identity=e.identity;out.exclusive=out.fenced=out.manager_owned=true;out.control_mode=3;out.logic_step=12;out.serial=9;return true;};
 story_native_wait_runtime_consumers::Adapter adapter(e,poses,sources);assert(adapter.configured());story_native_wait_consumer_write::State state;assert(adapter.capture(state));
 // First actor baseline, second actor still exactly owned; raw camera tuple
 // remains deliberately interim/incoherent as in raw camera capture test.
 auto&owned=memory[e.actors[1].address];putword(owned,0x3E0,e.identity.image+0x140);putword(owned,0x3E4,1);putword(owned,0x3E8,e.actors[1].address);
 assert(adapter.capture(state)&&state.people[0].cut==e.actors[0].cut&&state.people[1].cut!=e.actors[1].cut&&state.camera.current_type==2&&state.camera.next_type==1);
 auto source=adapter.source();assert(!source.write(e.actors[0].address+0x3E4,std::span(poses[0].cut).first(4))&&!writes);
 putword(owned,4,12345);assert(!adapter.capture(state));assert(!source.write(e.actors[0].address+0x3E0,poses[0].cut)&&!writes);
 putword(owned,4,e.actors[1].pid);assert(source.write(e.actors[0].address+0x3E0,poses[0].cut)&&writes==1);
}
