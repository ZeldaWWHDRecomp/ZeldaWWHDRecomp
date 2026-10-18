// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_people_pose_capture.h"
#include <cassert>
#include <map>
#include <stdexcept>
#include <algorithm>
#include <array>
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

#include "mods/story_native_wait_runtime_route_capture.h"
#include "mods/story_native_event_image.h"
#include "runtime.h"
#include <array>
#include <vector>
#include <cstdint>
#include <span>
int main(){
 PoseFixture actor;CameraFixture cam;auto e=actor.expected;e.camera=cam.e.camera;e.identity.manager=0x1046F0B0+0x52C4;e.frames=30;e.actors[1]=e.actors[0];e.actors[1].address+=0x20000;e.actors[1].pid+=2;e.actors[1].token++;
 auto memory=actor.memory;memory.insert(cam.memory.begin(),cam.memory.end());
 auto putword=[](auto&v,unsigned p,uint32_t x){for(unsigned n=0;n<4;++n)v[p+n]=uint8_t(x>>(24-n*8));};
 auto scalar=[&](uint32_t at,uint32_t v){memory[at].resize(4);putword(memory[at],0,v);};
 scalar(0x104753A8,1);scalar(0x1047E6C4,e.identity.stage);scalar(0x1047E8F0+e.room_number*0x22C,e.identity.room);memory[0x1046F0B0+0x5134]=std::vector<uint8_t>(8);
 memory[e.identity.image]=story_native_event_image::make_wait_fixture(e.frames);putword(memory[e.identity.image],0xE4,4);
 auto&m=memory[e.identity.manager];m.resize(0x540);const std::array<uint32_t,8> pools{e.identity.image,e.identity.image+0x40,e.identity.image+0xF0,e.identity.image+0x230,e.identity.image+0x410,0,e.identity.image+0x510,0};for(unsigned n=0;n<8;++n)putword(m,n*4,pools[n]);putword(m,0x20,1);putword(m,0x53C,0);
 auto&control=memory[e.identity.manager-0xF4];control.resize(0xF4);control[0xC2]=3;for(unsigned p:{0xC4,0xC8,0xCC,0xD0})putword(control,p,~0u);control[0xD8]=control[0xD9]=0xFF;
 uint32_t foreign=0x47010000,name=0x10010000;auto&fb=memory[foreign];fb=memory[actor.actor];putword(fb,4,100);putword(fb,0x3E0,name);putword(fb,0x3E8,foreign);memory[0x101C3E7C].resize(19*4);for(unsigned n=0;n<19;++n)putword(memory[0x101C3E7C],n*4,name);memory[name].resize(32);memory[name][0]='U';memory[name][1]='O';memory[name][2]='2';
 unsigned name_reads=0,ship_reads=0,layer_reads=0;bool mutate_name=false,mutate_ship=false,mutate_layer=false;uint32_t proof_ship=0,proof_layer=0;
 auto read=[&](uint32_t at,std::span<uint8_t>out){if(at==name&&mutate_name&&++name_reads==2)memory[name][0]^=1;if(at==proof_ship&&mutate_ship&&++ship_reads==2)memory[proof_ship][0x2E0]^=1;if(at==proof_layer&&mutate_layer&&++layer_reads==2)memory[proof_layer][0xC]^=1;for(auto&[base,bytes]:memory)if(at>=base&&uint64_t(at)+out.size()<=uint64_t(base)+bytes.size()){std::copy_n(bytes.begin()+at-base,out.size(),out.begin());return true;}return false;};
 bool resident=true,pending_order=false;unsigned identities=0;story_native_wait_runtime_route_capture::Sources sources;sources.read=read;sources.retained=[&](const auto&x){return x==e;};sources.resident=[&](const auto&,const auto&){++identities;return resident;};sources.camera=[&](auto&out){return story_native_camera_capture::capture(read,cam.l,e,out);};sources.pending_order=[&](const auto&x){return pending_order&&x==e;};
 story_native_wait_runtime_route_capture::Adapter adapter(e,sources);Cpu c{};c.r[3]=foreign;
 assert(adapter.retail_safe(&c,0x022C36D0)&&adapter.retail_safe(&c,0x022C2958));c.r[3]=foreign+0x3E0;assert(adapter.retail_safe(&c,0x0259F858));
 c.r[3]=e.identity.manager;c.r[4]=name;assert(adapter.retail_safe(&c,0x02542D88));c.r[5]=foreign;assert(!adapter.retail_safe(&c,0x02542D88));c.r[5]=0;
 // General negative lookup names need not occur in the People table.
 const uint32_t other_name=0x10011000;memory[other_name].resize(32);const char*link_name="Link";std::copy_n(link_name,4,memory[other_name].begin());c.r[3]=e.identity.manager;c.r[4]=other_name;c.r[5]=c.r[6]=0;
 assert(adapter.retail_safe(&c,0x02542D88));
 for(const char*reserved:{"SDK_ACTOR_A","SDK_ACTOR","SDK_ACTOR_A_EXTRA","CAMERA","TIME"}){std::fill(memory[other_name].begin(),memory[other_name].end(),0);std::copy_n(reserved,std::char_traits<char>::length(reserved),memory[other_name].begin());assert(!adapter.retail_safe(&c,0x02542D88));}
 std::fill(memory[other_name].begin(),memory[other_name].end(),'X');assert(!adapter.retail_safe(&c,0x02542D88));
 c.r[4]=0xFFFFFFF0;assert(!adapter.retail_safe(&c,0x02542D88));c.r[4]=e.identity.image+0xF0;assert(!adapter.retail_safe(&c,0x02542D88));
 c.r[4]=name;name_reads=0;mutate_name=true;assert(!adapter.retail_safe(&c,0x02542D88));mutate_name=false;memory[name][0]='U';
 c.r[4]=other_name;std::fill(memory[other_name].begin(),memory[other_name].end(),0);std::copy_n(link_name,4,memory[other_name].begin());c.r[6]=1;assert(!adapter.retail_safe(&c,0x02542D88));c.r[6]=0;
 // Actual native Ship identity is rooted in a preclaim pointer/PID snapshot,
 // its live global, canonical RPX profile/table/methods, and stable class prefix.
 memory[0x1048A9BC].resize(0x2C);proof_ship=0x47030000;auto&ship_bytes=memory[proof_ship];ship_bytes.resize(0x328);putword(ship_bytes,4,144);ship_bytes[0xE]=0;ship_bytes[0xF]=0xA5;putword(ship_bytes,0x10,0x101D04A8);putword(ship_bytes,0xB4,0x1003A2EC);ship_bytes[0x326]=5;ship_bytes[0x5D]=0xAA;putword(ship_bytes,0x2C,0x1048A9BC);putword(ship_bytes,0x2E0,0x8000);
 scalar(0x1046F0B0+0x5B3C,proof_ship);scalar(0x101F3EE0+0xA5*4,0x101D04A8);auto&ship_profile=memory[0x101D04A8];ship_profile.resize(0x28);ship_profile[9]=0xA5;putword(ship_profile,0x10,0xD198);putword(ship_profile,0x24,0x101D0394);
 sources.ship=story_native_wait_runtime_route_capture::Ship{proof_ship,144};story_native_wait_runtime_route_capture::Adapter ship_adapter(e,sources);
 c.r[3]=e.identity.manager;c.r[4]=other_name;c.r[5]=proof_ship;c.r[6]=0;std::fill(memory[other_name].begin(),memory[other_name].end(),0);std::copy_n("Ship",4,memory[other_name].begin());assert(ship_adapter.retail_safe(&c,0x02542D88));
 const auto original_ship=ship_bytes,original_profile=ship_profile;
 for(unsigned bad=0;bad<9;++bad){ship_bytes=original_ship;ship_profile=original_profile;scalar(0x1046F0B0+0x5B3C,proof_ship);scalar(0x101F3EE0+0xA5*4,0x101D04A8);switch(bad){case 0:putword(ship_bytes,4,145);break;case 1:ship_bytes[0xC]=3;break;case 2:ship_bytes[0x326]=64;break;case 3:putword(ship_bytes,0x10,0x101C4CD0);break;case 4:putword(ship_bytes,0xB4,0);break;case 5:putword(ship_profile,0x10,0xD19C);break;case 6:putword(ship_profile,0x24,0);break;case 7:scalar(0x1046F0B0+0x5B3C,proof_ship+4);break;case 8:scalar(0x101F3EE0+0xA5*4,0x101C4CD0);break;}assert(!ship_adapter.retail_safe(&c,0x02542D88));}
 ship_bytes=original_ship;ship_profile=original_profile;scalar(0x1046F0B0+0x5B3C,proof_ship);scalar(0x101F3EE0+0xA5*4,0x101D04A8);
 ship_reads=0;mutate_ship=true;assert(!ship_adapter.retail_safe(&c,0x02542D88));mutate_ship=false;ship_bytes=original_ship;
 // Mobile FF unknown-room is legitimate only with independently live
 // ancestry. DeleteTag is the one-byte in-use flag; adjacent bytes may differ.
 proof_layer=0x48030000;const uint32_t scene=0x48031000;auto&layer_bytes=memory[proof_layer];layer_bytes.resize(0x2C);putword(layer_bytes,0x18,scene);auto&scene_bytes=memory[scene];scene_bytes.resize(0x60);scene_bytes[0x5D]=0xBB;putword(scene_bytes,0x2C,0x1048A9BC);ship_bytes[0x326]=0xFF;putword(ship_bytes,0x2C,proof_layer);assert(ship_adapter.retail_safe(&c,0x02542D88));
 const auto live_ship=ship_bytes,live_layer=layer_bytes,live_scene=scene_bytes;
 for(unsigned bad=0;bad<8;++bad){ship_bytes=live_ship;layer_bytes=live_layer;scene_bytes=live_scene;memory[0x1048A9BC].assign(0x2C,0);switch(bad){case 0:ship_bytes[0x5C]=1;break;case 1:scene_bytes[0x5C]=1;break;case 2:scene_bytes[0xC]=3;break;case 3:putword(scene_bytes,0x2C,proof_layer);break;case 4:putword(layer_bytes,0x18,0);break;case 5:putword(memory[0x1048A9BC],0xC,1);break;case 6:putword(memory[0x1048A9BC],0x18,scene);break;case 7:putword(ship_bytes,0x2C,0);break;}assert(!ship_adapter.retail_safe(&c,0x02542D88));}
 ship_bytes=live_ship;layer_bytes=live_layer;scene_bytes=live_scene;memory[0x1048A9BC].assign(0x2C,0);
 layer_reads=0;mutate_layer=true;assert(!ship_adapter.retail_safe(&c,0x02542D88));mutate_layer=false;layer_bytes=live_layer;
 // Parent stage deletion also denies an otherwise live first scene.
 const uint32_t parent_layer=0x48032000,parent_scene=0x48033000;memory[parent_layer].resize(0x2C);putword(memory[parent_layer],0x18,parent_scene);memory[parent_scene].resize(0x60);putword(memory[parent_scene],0x2C,0x1048A9BC);memory[parent_scene][0x5C]=1;putword(scene_bytes,0x2C,parent_layer);assert(!ship_adapter.retail_safe(&c,0x02542D88));memory[parent_scene][0x5C]=0;assert(ship_adapter.retail_safe(&c,0x02542D88));
 // Seventeen live noncyclic layers still exceed the bounded ancestry proof.
 for(unsigned n=0;n<17;++n){const uint32_t deep_layer=0x49000000+n*0x1000,deep_scene=deep_layer+0x100;memory[deep_layer].resize(0x2C);putword(memory[deep_layer],0x18,deep_scene);memory[deep_scene].resize(0x60);putword(memory[deep_scene],0x2C,n==16?0x1048A9BC:deep_layer+0x1000);}
 putword(ship_bytes,0x2C,0x49000000);assert(!ship_adapter.retail_safe(&c,0x02542D88));
 ship_bytes=original_ship;layer_bytes=live_layer;scene_bytes=live_scene;
 assert(!adapter.retail_safe(&c,0x02542D88)); // current global/PID without preclaim snapshot is insufficient
 c.r[4]=e.identity.image+0xF0;assert(!ship_adapter.retail_safe(&c,0x02542D88));c.r[4]=other_name;c.r[6]=1;assert(!ship_adapter.retail_safe(&c,0x02542D88));c.r[6]=0;c.r[5]=0;
 // Native19 membership remains mandatory for People, even on a no-match name.
 c.r[3]=foreign;putword(fb,0x3E0,other_name);assert(!adapter.retail_safe(&c,0x022C36D0));putword(fb,0x3E0,name);
 c.r[3]=e.actors[0].address;putword(memory[e.actors[0].address],4,101);assert(!adapter.retail_safe(&c,0x022C36D0));
 c.r[3]=foreign;const auto original_name=memory[name];std::fill(memory[name].begin(),memory[name].end(),0);const char* sdk="SDK_ACTOR_A";std::copy_n(sdk,11,memory[name].begin());assert(!adapter.retail_safe(&c,0x022C36D0));memory[name]=original_name;
 c.r[3]=e.camera.address-0x248;assert(!adapter.retail_safe(&c,0x024FFA3C));c.r[3]=e.camera.address;assert(!adapter.retail_safe(&c,0x024FF164));
 c.r[3]=foreign;putword(fb,0x3E0,e.identity.image+0xF0);assert(!adapter.retail_safe(&c,0x022C36D0));putword(fb,0x3E0,name);
 memory[e.identity.image][0xF0]='X';assert(!adapter.retail_safe(&c,0x022C36D0));memory[e.identity.image][0xF0]='S';
 c.r[3]=e.identity.manager;c.r[4]=0;story_native_event_end_guard::Observation observation;assert(adapter.end_observation(&c,observation)&&observation.event==e.identity.image+0x40&&observation.retained_claim&&identities==4);
 putword(memory[e.identity.image],0xE4,0);putword(m,0x20,0);assert(adapter.end_observation(&c,observation));
 putword(memory[e.identity.image],0xE4,2);assert(!adapter.end_observation(&c,observation));putword(m,0x53C,e.identity.image+0x40);assert(adapter.end_observation(&c,observation));
 // ORDER is pending before first manager mainProc, but guest ORDER alone
 // cannot certify the successful native order or durable publication.
 putword(memory[e.identity.image],0xE4,1);putword(m,0x53C,0);assert(!adapter.end_observation(&c,observation));
 pending_order=true;assert(adapter.end_observation(&c,observation));
 putword(m,0x20,1);assert(!adapter.end_observation(&c,observation));putword(m,0x20,0);
 memory[e.identity.image][0xF0+0x46]=1;assert(!adapter.end_observation(&c,observation));memory[e.identity.image][0xF0+0x46]=0;
 control[0xC0]=1;assert(!adapter.end_observation(&c,observation));control[0xC0]=0;
 pending_order=false;assert(!adapter.end_observation(&c,observation));
 putword(memory[e.identity.image],0xE4,2);putword(m,0x53C,e.identity.image+0x40);
 control[0xC2]=2;assert(!adapter.end_observation(&c,observation)&&!observation.retained_claim);control[0xC2]=3;
 resident=false;assert(!adapter.end_observation(&c,observation));
}
