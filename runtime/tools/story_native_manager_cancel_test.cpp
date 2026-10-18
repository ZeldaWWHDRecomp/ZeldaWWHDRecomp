// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_manager_cancel.h"
#include "guest_addr.h"
#include "ppc.h"
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
#include <utility>
using namespace story_native_manager_cancel;
static void put(auto&b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
struct Fixture {
 O::Expected e;std::array<P::Snapshot,2>poses{};Boundary b;Cpu cpu{};std::map<uint32_t,std::vector<uint8_t>>mem;
 const std::thread::id thread=std::this_thread::get_id();std::unique_ptr<Cancel> helper;unsigned calls=0,originals=0,quarantines=0;int fault=0;bool restore_fail=false;I::Range arena{0x60000000,0x10000};
 bool read(uint32_t a,std::span<uint8_t>out){for(auto&[base,v]:mem)if(a>=base&&uint64_t(a)+out.size()<=uint64_t(base)+v.size()){std::copy_n(v.begin()+a-base,out.size(),out.begin());return true;}return false;}
 bool write(uint32_t a,std::span<const uint8_t>in){if(restore_fail)return false;for(auto&[base,v]:mem)if(a>=base&&uint64_t(a)+in.size()<=uint64_t(base)+v.size()){std::copy(in.begin(),in.end(),v.begin()+a-base);return true;}return false;}
 Fixture(){
 story_native_turn_camera_image::Program def;def.camera.start_eye={0,10,20};def.camera.end_eye={10,10,20};e.program=story_native_event_program::Program::turn(def,301);assert(e.program);
 e.identity={0x20000000,0x60000000,0xE60,1,2,3,4};e.frames=def.wait_timer;e.camera.address=0x21000248;e.camera.pid=20;
 mem[e.identity.image]={e.program->pristine().begin(),e.program->pristine().end()};put(mem[e.identity.image],0xE4,2);mem[e.identity.manager].resize(0x540);put(mem[e.identity.manager],0x20,1);put(mem[e.identity.manager],0x53C,e.identity.image+0x40);
 const auto pools=e.program->pool_offsets();for(unsigned n=0;n<8;++n)put(mem[e.identity.manager],n*4,e.identity.image+pools[n]);
 for(unsigned n=0;n<4;++n){put(mem[e.identity.image],0xF0+n*0x50+0x38,e.program->initial_cut(n));put(mem[e.identity.image],0xF0+n*0x50+0x3C,~0u);}
 for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x22000000+n*0x10000;a.pid=10+n;a.token=100+n;a.profile=0x175;a.subtype=1;a.fingerprint[0]=1+n;auto&p=poses[n];p.identity={a.address,a.pid,a.token,1,2,3,0x23000000+n*0x10000,0x24000000+n*0x10000};p.status=0x40180;
 auto&actor=mem[a.address];actor.resize(0x920);put(actor,0x2E0,p.status|0x8000);put(actor,0x44C,p.identity.morf);put(actor,0x3E0,e.identity.image+0xF0+n*0x50);put(actor,0x3E4,n);put(actor,0x3E8,a.address);put(actor,0x3F0,~0u);
 mem[p.identity.morf].resize(0xC8);put(mem[p.identity.morf],0x90,p.identity.model);mem[p.identity.model].resize(0xBC);put(mem[p.identity.model],0xB8,a.address);
 }
 b.expected=e;b.logic_step=100;b.depth.serial=99;b.depth.coverage=true;b.depth.camera_depth=1;b.retained=b.fenced=b.stage=b.exclusive=true;b.parent={D::Entry::CameraExecute,GC(0x024FFA3C),e.camera.address-0x248,e.camera.address-0x248,e.camera.pid,0,e.identity,D::Authority::Owned};
 cpu.r[1]=0x30001008;cpu.r[3]=0x1234;cpu.lr=0x5678;mem[0x30000000].resize(0x2000,0xA5);
 }
 void make(){Sources s;s.read=[&](auto a,auto out){return read(a,out);};s.write=[&](auto a,auto in){return write(a,in);};s.boundary=[&](Cpu*,Boundary&out){if(std::this_thread::get_id()!=thread)return false;out=b;return true;};s.resident=[&](const auto&expected,unsigned){return expected==e;};s.routes=[] {return true;};s.arena=[&](const auto&expected,I::Range&out){out=arena;return expected==e;};s.stack=[](Cpu*,I::Config&out){out.stack={0x30000000,0x2000};out.protected_ranges={{0x61000000,0x1000}};return true;};s.quarantine=[&]{++quarantines;};
 s.call=[&](Cpu*c,uint32_t fn,std::initializer_list<uint32_t>args){++calls;assert(helper->native_cancel_scope());assert(fn==GC(0x0254465C));assert(std::vector<uint32_t>(args)==std::vector<uint32_t>({e.identity.manager,0,1}));c->r[3]=0xDEAD;c->lr=0;mem[0x30000000][0x1000]=0;
  if(fault==1)throw 1;
  if(fault==8){b.fenced=false;assert(helper->run(c)==Result::Refused);b.fenced=true;}
  if(fault==9)assert(helper->run(c)==Result::Refused);
  if(fault==10){std::thread foreign([&]{assert(helper->run(c)==Result::Refused);});foreign.join();}
  for(unsigned n=0;n<2;++n){auto off=[&,n]{++originals;put(mem[e.actors[n].address],0x2E0,poses[n].status);return 0u;};helper->route_all_off(e.actors[n].address,off);if(fault==2&&n==0)helper->route_all_off(e.actors[n].address,off);}
  unsigned foreign=0;helper->route_all_off(0xDEAD,[&]{++foreign;return 0u;});assert(foreign==0);
  put(mem[e.identity.image],0xE4,0);put(mem[e.identity.manager],0x20,0);
  if(fault==3)mem[e.identity.manager][0x3F]^=1;if(fault==4)put(mem[e.identity.manager],0x53C,0);if(fault==5)++b.depth.serial;if(fault==6)mem[e.identity.image][0x12F]^=1;
  return 0u;};helper=std::make_unique<Cancel>(e,poses,std::move(s));}
};
int main(){
 {Fixture f;f.make();const Cpu before=f.cpu;const auto stack=f.mem[0x30000000];assert(f.helper->run(&f.cpu)==Result::Completed);assert(!std::memcmp(&before,&f.cpu,sizeof(Cpu)));assert(stack==f.mem[0x30000000]);assert(f.calls==1&&f.originals==2&&!f.quarantines);auto c=f.helper->completion();assert(c.valid()&&c.matches(f.e)&&c.before().depth.camera_depth==1&&c.after().depth.serial==99);assert(!f.helper->native_cancel_scope());assert(f.helper->run(&f.cpu)==Result::Refused&&f.calls==1);unsigned ordinary=0;assert(f.helper->route_all_off(0xDEAD,[&]{++ordinary;return 7u;})==7&&ordinary==1);auto wrong=f.e;wrong.identity.generation++;assert(!c.matches(wrong));}
 for(unsigned n=0;n<18;++n){Fixture f;if(n==0)f.b.fenced=false;if(n==1)f.b.depth.camera_depth=0;if(n==2)f.b.depth.state=D::State::Draining;if(n==3)f.b.parent.authority=D::Authority::Foreign;if(n==4)f.b.depth.callback_depth=1;if(n==5)f.b.elapsed=301;if(n==6)put(f.mem[f.e.identity.image],0xE4,4);if(n==7)f.mem[f.e.identity.manager][0x40]=1;if(n==8)put(f.mem[f.e.identity.image],0x128,0);if(n==9)put(f.mem[f.e.actors[0].address],0x2E0,f.poses[0].status);if(n==10)put(f.mem[f.e.actors[1].address],0x3E0,f.e.identity.image+0xF0);if(n==11)put(f.mem[f.poses[0].identity.model],0xB8,0);if(n==12)f.cpu.r[1]=f.e.actors[0].address+0x100;if(n==13)f.cpu.r[1]++;if(n==14)f.b.stage=false;if(n==15){f.poses[1].identity.model=f.poses[0].identity.model;put(f.mem[f.poses[1].identity.morf],0x90,f.poses[0].identity.model);put(f.mem[f.poses[0].identity.model],0xB8,f.e.actors[1].address);}if(n==16)f.arena={0x20000000,0x50010000};if(n==17)f.arena.bytes=0x100;f.make();assert(f.helper->run(&f.cpu)==Result::Refused);assert(f.calls==0&&!f.helper->completion().valid());}
 {Fixture f;put(f.mem[f.e.identity.manager],0x53C,0);f.mem[f.e.identity.manager][0x3F]=0x80;put(f.mem[f.e.identity.image],0x128,0);put(f.mem[f.e.identity.image],0x12C,0);put(f.mem[f.e.actors[0].address],0x3F0,0);f.make();assert(f.helper->run(&f.cpu)==Result::Completed);assert(f.helper->completion().poststate().manager[0x3F]==0x80);}
 {Fixture f;f.e.camera.address=f.poses[0].identity.model+0x248;f.b.expected=f.e;f.b.parent.receiver=f.b.parent.subject=f.poses[0].identity.model;f.make();assert(f.helper->run(&f.cpu)==Result::Refused&&f.calls==0);}
 for(int n=8;n<=10;++n){Fixture f;f.fault=n;f.make();assert(f.helper->run(&f.cpu)==Result::Completed&&!f.quarantines);}
 for(int n=1;n<=7;++n){Fixture f;f.fault=n;f.restore_fail=n==7;f.make();const Cpu before=f.cpu;assert(f.helper->run(&f.cpu)==Result::Quarantined);assert(!std::memcmp(&before,&f.cpu,sizeof(Cpu)));assert(f.quarantines==1&&!f.helper->completion().valid());assert(f.helper->run(&f.cpu)==Result::Quarantined&&f.calls==1);}
}
