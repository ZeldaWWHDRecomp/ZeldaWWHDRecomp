// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_wait_actor_cast.h"
#include "mods/story_native_event_image.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <map>
#include <thread>
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
namespace C=story_native_wait_actor_cast;
namespace D=story_native_event_depth;
using Expected=story_native_event_observation::Expected;
void put(auto&b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-n*8));}
uint32_t word(const auto&b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
struct Fixture {
 Expected e;Cpu cpu{};std::map<uint32_t,std::vector<uint8_t>>mem;
 std::thread::id owner=std::this_thread::get_id();bool claimed=true,owned=true;unsigned cancelled=0,bodies=0,status_writes=0;
 Fixture(){e.identity={0x20000000,0x60000000,0x520,1,2,3,4};e.room_number=11;e.camera.address=0x20003248;e.camera.pid=12;
  for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x47001000+n*0x1000;a.pid=10+n;a.token=n+1;a.profile=0x175;a.subtype=1;a.fingerprint.fill(n+1);mem[a.address].resize(0x920);put(mem[a.address],4,a.pid);put(mem[a.address],0x2E0,0x40000);}
  mem[e.identity.image]=story_native_event_image::make_wait_fixture(e.frames);put(mem[e.identity.image],0xE4,1);auto&manager=mem[e.identity.manager];manager.resize(0x540);manager[0x3C+0x4FF]=0x40;const uint32_t image=e.identity.image;const std::array<uint32_t,8>p{image,image+0x40,image+0xF0,image+0x230,image+0x410,0,image+0x510,0};for(unsigned n=0;n<8;++n)put(manager,n*4,p[n]);mem[0x60010000].resize(0x1000,0xAA);mem[0x10003000].resize(32);std::memcpy(mem[0x10003000].data(),"Uo2",4);
  cpu.r[1]=0x600100C8;cpu.r[3]=e.identity.manager;cpu.r[4]=image+0x40;
 }
 bool read(uint32_t at,std::span<uint8_t>out){for(auto&[base,b]:mem)if(at>=base&&uint64_t(at)+out.size()<=uint64_t(base)+b.size()){std::copy_n(b.begin()+at-base,out.size(),out.begin());return true;}return false;}
 void w(uint32_t at,uint32_t value){for(auto&[base,b]:mem)if(at>=base&&uint64_t(at)+4<=uint64_t(base)+b.size()){put(b,at-base,value);return;}assert(false);}
 C::Sources source(){C::Sources s;s.read=[&](auto at,auto b){return read(at,b);};s.retained=[&](const auto&x){return claimed&&x==e;};s.owned_thread=[&](const Cpu*c){return owned&&c==&cpu&&owner==std::this_thread::get_id()&&c->r[1]>=0x60010040&&c->r[1]<=0x60011000;};s.resident=[&](const auto&a,const auto&i){for(const auto&want:e.actors)if(a==want)return i==e.identity&&word(mem[a.address],4)==a.pid;return false;};s.queue_cancel=[&]{++cancelled;};return s;}
 D::Call call(D::Entry entry){return {entry,D::entry_address(entry),e.identity.manager,e.identity.manager,0,0,e.identity,D::Authority::Owned};}
 void native(C::Cast&cast,unsigned bad=0){++bodies;const auto entry_sp=cpu.r[1];cpu.r[1]-=0x40;const uint32_t param=cpu.r[1]+8;
  for(unsigned n=0;n<2;++n){w(param,e.identity.image+0xF0+n*0x50);w(param+4,bad==1?1:0);w(param+8,0);w(param+12,0x0000AABBu);
   if(bad==2&&n==0)w(e.actors[0].address+4,999);
   uint32_t found=0;
   for(uint32_t candidate:{0x47100000u,e.actors[0].address,e.actors[1].address}){cpu.r[3]=candidate;cpu.r[4]=param+(bad==3?4:0);assert(cast.candidate(&cpu)==C::Decision::Handled);if(cast.fault()){cpu.r[1]=entry_sp;return;}if(cpu.r[3]){found=cpu.r[3];break;}}
   assert(found==e.actors[n].address);
   if(bad==4){cpu.r[3]=found;cpu.r[4]=param;assert(cast.candidate(&cpu)==C::Decision::Handled&&cpu.r[3]==0);cpu.r[1]=entry_sp;return;}
   // These stores stand for the ORIGINAL native startProc, not the adapter.
   if(bad!=5){w(found+0x2E0,word(mem[found],0x2E0)|0x8000);++status_writes;}
   w(e.identity.image+0xF0+n*0x50+0x38,e.program?e.program->initial_cut(n):n);mem[e.identity.image][0xF0+n*0x50+0x46]=2;
  }
  if(e.program){for(unsigned n=2;n<4;++n)w(e.identity.image+0xF0+n*0x50+0x38,e.program->initial_cut(n));}
  if(bad!=6)w(e.identity.image+0xE4,2);
  // Native flags.init tail clears 0x140 words at manager+3C.
  if(bad!=7)std::fill_n(mem[e.identity.manager].begin()+0x3C,0x500,0);
  cpu.r[1]=entry_sp;
 }
};
D::Coverage coverage(){return {{1,1,true},{2,2,true},{3,3,true},true};}
int main(){
 {Fixture f;D::Tracker d(f.e,coverage());C::Cast cast(f.e,d,f.source());assert(cast.configured()&&!cast.publication_enabled()&&!cast.initialized(f.e));auto run=d.enter(f.call(D::Entry::ManagerRun));auto main=d.enter(f.call(D::Entry::ManagerMain));assert(run.allowed()&&main.allowed());assert(cast.start(&f.cpu,[&]{f.native(cast);assert(!cast.initialized(f.e));})==C::Decision::Handled&&!cast.fault()&&f.bodies==1&&f.status_writes==2&&!f.cancelled);assert(cast.initialized(f.e));auto other=f.e;++other.identity.generation;assert(!cast.initialized(other));for(const auto&a:f.e.actors)assert(word(f.mem[a.address],0x2E0)==0x48000);}
 // Typed E60 uses its canonical pools and native initial TURN cuts7/8;
 // only the genuine synthetic original performs FORCEMOVE/flag init stores.
 auto turn=[](Fixture&f){story_native_turn_camera_image::Program p;p.camera.start_eye={0,10,20};p.camera.end_eye={10,10,20};f.e.program=story_native_event_program::Program::turn(p,301);f.e.identity.bytes=f.e.program->bytes();f.mem[f.e.identity.image]=std::vector<uint8_t>(f.e.program->pristine().begin(),f.e.program->pristine().end());f.w(f.e.identity.image+0xE4,1);const auto pools=f.e.program->pool_offsets();for(unsigned n=0;n<8;++n)f.w(f.e.identity.manager+n*4,f.e.identity.image+pools[n]);};
 {Fixture f;turn(f);D::Tracker d(f.e,coverage());C::Cast cast(f.e,d,f.source());assert(cast.configured());auto run=d.enter(f.call(D::Entry::ManagerRun));auto main=d.enter(f.call(D::Entry::ManagerMain));assert(cast.start(&f.cpu,[&]{f.native(cast);assert(!cast.initialized(f.e));})==C::Decision::Handled&&!cast.fault()&&cast.initialized(f.e)&&f.bodies==1&&f.status_writes==2);assert(word(f.mem[f.e.identity.image],0x128)==7&&word(f.mem[f.e.identity.image],0x178)==8);auto other=f.e;other.program=story_native_event_program::Program::wait(30);assert(!cast.initialized(other));auto changed=*f.e.program->turn_definition();changed.actor_a_target.x=1;other=f.e;other.program=story_native_event_program::Program::turn(changed,301);assert(!cast.initialized(other));}
 for(unsigned fault=0;fault<5;++fault){Fixture f;turn(f);if(fault==0)f.w(f.e.identity.manager+0x10,f.e.identity.image+0x410);if(fault==1)f.mem[f.e.identity.image]=story_native_event_image::make_wait_fixture();if(fault==2)f.w(f.e.identity.image+0x120,0);if(fault==3)f.w(f.e.identity.image+0x128,7);D::Tracker d(f.e,coverage());C::Cast cast(f.e,d,f.source());auto run=d.enter(f.call(D::Entry::ManagerRun));auto main=d.enter(f.call(D::Entry::ManagerMain));assert(cast.start(&f.cpu,[&]{f.native(cast);if(fault==4)f.w(f.e.identity.image+0x128,8);})==C::Decision::Handled&&cast.fault()&&!cast.initialized(f.e));if(fault<4)assert(!f.bodies&&!f.status_writes);}
 for(unsigned bad=1;bad<=7;++bad){Fixture f;D::Tracker d(f.e,coverage());C::Cast cast(f.e,d,f.source());auto run=d.enter(f.call(D::Entry::ManagerRun));auto main=d.enter(f.call(D::Entry::ManagerMain));assert(cast.start(&f.cpu,[&]{f.native(cast,bad);})==C::Decision::Handled&&cast.fault()&&f.bodies==1&&f.cancelled==1&&!cast.initialized(f.e));if(bad<=4)assert(!f.status_writes);}
 {Fixture f;D::Tracker d(f.e,coverage());C::Cast cast(f.e,d,f.source());assert(cast.start(&f.cpu,[&]{++f.bodies;})==C::Decision::Handled&&cast.fault()&&!f.bodies);}
 {Fixture f;D::Tracker d(f.e,coverage());C::Cast cast(f.e,d,f.source());f.claimed=false;assert(cast.start(&f.cpu,[&]{++f.bodies;})==C::Decision::Delegate&&!f.bodies);assert(cast.candidate(&f.cpu)==C::Decision::Delegate&&!cast.fault());}
 {Fixture f;D::Tracker d(f.e,coverage());C::Cast cast(f.e,d,f.source());const uint32_t param=0x60010090;f.w(param,0x10003000);f.cpu.r[3]=f.e.actors[0].address;f.cpu.r[4]=param;const Cpu before=f.cpu;assert(cast.candidate(&f.cpu)==C::Decision::Delegate&&!std::memcmp(&before,&f.cpu,sizeof before)&&!cast.fault());f.w(param,f.e.identity.image+0xF0);assert(cast.candidate(&f.cpu)==C::Decision::Handled&&f.cpu.r[3]==0&&cast.fault());}
 {Fixture f;D::Tracker d(f.e,coverage());C::Cast cast(f.e,d,f.source());std::thread worker([&]{assert(cast.start(&f.cpu,[&]{++f.bodies;})==C::Decision::Handled);});worker.join();assert(cast.fault()&&!f.bodies&&f.cancelled==1);}
}
