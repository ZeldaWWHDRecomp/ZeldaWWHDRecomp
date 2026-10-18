// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_camera_cancel_barrier.h"
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

namespace B=story_native_camera_cancel_barrier;
struct CameraFixture:Fixture {
 O::Camera current;std::unique_ptr<B::Barrier>barrier;uint64_t outer=90;unsigned runs=0;int camera_fault=0;
 CameraFixture(){
 e.camera.bg=0x7F;e.camera.trim=0;e.camera.current_type=e.camera.next_type=41;e.camera.event_type=2;e.camera.style_table=e.camera.param_pointer=0x1004487C;e.camera.style_count=145;e.camera.type_table=0x10049350;e.camera.type_count=63;put(e.camera.event_data,0xC,~0u);put(e.camera.event_data,4,~0u);b.expected=e;
 current=e.camera;current.current_type=current.next_type=2;current.bg=9;current.trim=2;put(current.event_data,0xC,41);put(current.event_data,4,2);put(current.event_data,0x1C,2);put(current.event_data,0x24,1);
 put(mem[e.identity.image],0x1CC,4);auto copy=[&](unsigned dst,unsigned src,unsigned count){std::copy_n(e.program->pristine().begin()+src,count,current.work37C.begin()+dst);};current.work37C[0]=1;copy(4,0xD80,12);copy(0x10,0xD8C,12);copy(0x1C,0xD98,4);current.work37C[0x28]=current.work37C[0x29]='-';put(current.work37C,0x2C,e.program->turn_definition()->camera.hold_timer);current.work37C[0x30]=1;current.timer=0x41200000;cpu.r[3]=e.camera.address;
 }
 void make_barrier(){B::Sources s;s.owned_thread=[&](Cpu*c){return c==&cpu&&std::this_thread::get_id()==thread;};s.frame=[&](Cpu*,B::Frame&out){if(std::this_thread::get_id()!=thread)return false;out={b,outer};return true;};s.read=[&](auto a,auto out){return read(a,out);};s.resident=[&](const auto&expected,unsigned){return expected==e;};s.camera=[&](O::Camera&out){out=current;return true;};s.verify_camera=[&](const auto&expected,const auto&){return expected==e&&camera_fault!=10;};s.original_route=[&](Cpu*,uint32_t fn){return fn==GC(0x024FE3E8)&&camera_fault!=11;};s.quarantine=[&]{++quarantines;};barrier=std::make_unique<B::Barrier>(e,std::move(s));}
 void original(){++runs;if(camera_fault==1)throw 1;if(camera_fault==2)return;
 current.current_type=current.next_type=41;current.bg=e.camera.bg;current.trim=e.camera.trim;put(current.event_data,0xC,~0u);current.work37C.fill(0);put(current.work37C,0,0x464C4C57);put(current.work37C,4,1);current.timer=0;
 if(camera_fault==3)put(current.work37C,0,0xBAD);if(camera_fault==4)mem[e.identity.manager][0x3F]^=1;if(camera_fault==5)put(mem[e.identity.manager],0x53C,0);if(camera_fault==6)mem[e.identity.image][0x12F]^=1;if(camera_fault==7)++outer;if(camera_fault==8)put(current.work37C,8,0x7FC00000);if(camera_fault==9)current.event_data[0x10]^=1;if(camera_fault==12)b.depth.serial+=3;if(camera_fault==13)--b.depth.serial;if(camera_fault==14)current.style++;if(camera_fault==15)put(current.mode_phase13C,0,1);
 }
};

int main(){
 for(int n:{0,12}){CameraFixture f;f.camera_fault=n;f.make_barrier();auto ready=f.barrier->ready(&f.cpu,GC(0x024FE3E8));assert(ready&&ready->valid());f.make();assert(f.helper->run(&f.cpu)==Result::Completed);const auto cancel=f.helper->completion();assert(f.barrier->run(&f.cpu,GC(0x024FE3E8),*ready,cancel,[&]{f.original();})==B::Result::Returned);auto complete=f.barrier->completion();assert(complete.valid()&&complete.matches(f.e)&&complete.cancel().serial()==cancel.serial()&&f.runs==1&&!f.quarantines);assert(f.barrier->run(&f.cpu,GC(0x024FE3E8),*ready,cancel,[&]{f.original();})==B::Result::Refused&&f.runs==1);}
 for(unsigned n=0;n<9;++n){CameraFixture f;if(n==0)f.current=f.e.camera;if(n==1)f.current.next_type=41;if(n==2)f.b.fenced=false;if(n==3)f.b.depth.camera_depth=0;if(n==4)f.camera_fault=10;if(n==5)f.camera_fault=11;if(n==6)f.current.event_data[0x3F]=0x60;if(n==7)f.mem[f.e.identity.image][0x1CF]=5;if(n==8)f.current.work37C[0x28]='r';f.make_barrier();assert(!f.barrier->ready(&f.cpu,GC(0x024FE3E8)));assert(!f.calls&&!f.runs&&!f.quarantines);}
 for(int n:{1,2,3,4,5,6,7,8,9,13,14,15}){CameraFixture f;f.make_barrier();auto ready=f.barrier->ready(&f.cpu,GC(0x024FE3E8));assert(ready);f.make();assert(f.helper->run(&f.cpu)==Result::Completed);f.camera_fault=n;assert(f.barrier->run(&f.cpu,GC(0x024FE3E8),*ready,f.helper->completion(),[&]{f.original();})==B::Result::Quarantined);assert(f.runs==1&&f.quarantines==1&&!f.barrier->completion().valid());assert(f.barrier->run(&f.cpu,GC(0x024FE3E8),*ready,f.helper->completion(),[&]{f.original();})==B::Result::Quarantined&&f.runs==1);}
 {CameraFixture f;f.make_barrier();auto ready=f.barrier->ready(&f.cpu,GC(0x024FE3E8));assert(ready);f.make();assert(f.helper->run(&f.cpu)==Result::Completed);unsigned calls=0;std::thread foreign([&]{assert(f.barrier->run(&f.cpu,GC(0x024FE3E8),*ready,f.helper->completion(),[&]{++calls;})==B::Result::Refused);});foreign.join();assert(!calls&&!f.quarantines);assert(f.barrier->run(&f.cpu,GC(0x024FE3E8),*ready,f.helper->completion(),[&]{f.original();})==B::Result::Returned);}
 {CameraFixture f;f.make_barrier();auto ready=f.barrier->ready(&f.cpu,GC(0x024FE3E8));assert(ready);f.make();assert(f.helper->run(&f.cpu)==Result::Completed);f.b.fenced=false;assert(f.barrier->run(&f.cpu,GC(0x024FE3E8),*ready,f.helper->completion(),[&]{f.original();})==B::Result::Quarantined&&f.runs==0&&f.quarantines==1);}
 {CameraFixture f;f.make_barrier();auto ready=f.barrier->ready(&f.cpu,GC(0x024FE3E8));assert(ready);B::Ready invalid;f.make();assert(f.helper->run(&f.cpu)==Result::Completed);assert(f.barrier->run(&f.cpu,GC(0x024FE3E8),invalid,f.helper->completion(),[&]{f.original();})==B::Result::Quarantined&&f.runs==0);}
}
