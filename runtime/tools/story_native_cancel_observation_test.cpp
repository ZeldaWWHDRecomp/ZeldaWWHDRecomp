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
#include <functional>
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

#include "mods/story_native_cancel_observation.h"
#include "mods/story_native_cancel_pose_capture.h"
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
#include <utility>
#include <functional>
namespace R=story_native_people_cancel_refresh;
struct RefreshFixture {
 R::Expected e;std::array<R::P::Snapshot,2>poses{};std::array<R::ModelIdentity,2>models{};std::array<R::Transform,2>transforms{};R::State live;
 Cpu cpu{};std::array<uint8_t,64>frame{};R::I::Config stack{{0x60010000,0x1000},{{0x60000000,0x520}}};
 unsigned calls=0,q=0,reads=0,writes=0,graph_checks=0;unsigned bad=0;bool claimed=true;R::Invoker* active=nullptr;
 RefreshFixture(){e.identity={0x20000000,0x60000000,0x520,1,2,3,4};
  for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x47001000+n*0x1000;a.pid=10+n;a.token=n+1;a.profile=0x175;a.subtype=1;a.fingerprint.fill(n+1);models[n]={0x48000000+n*0x1000,0x49000000+n*0x1000,a.address,0x20010000+n*0x1000,0x20020000+n*0x1000,0x20030000+n*0x1000,0};poses[n].identity={a.address,a.pid,a.token,1,2,3,models[n].morf,models[n].model};poses[n].cut=a.cut;poses[n].status=0x40000;for(unsigned off:{0u,4u,8u})transforms[n].scale[off]=transforms[n].model_scale[off]=0x3F,transforms[n].scale[off+1]=transforms[n].model_scale[off+1]=0x80;for(unsigned off:{0u,20u,40u})transforms[n].base[off]=0x3F,transforms[n].base[off+1]=0x80;}
  for(unsigned n=0;n<2;++n){live.animations[n].address=0x48010000+n*0x1000;live.animations[n].frame_control[4]=0x3F;live.animations[n].frame_control[5]=0x80;}
  live.poses=poses;live.models=models;live.transforms=transforms;live.transforms[0].base[0]=0;live.transforms[1].base[0]=0;
  auto&b=live.boundary;b.identity=e.identity;b.exclusive=b.fenced=b.manager_owned=true;b.control_mode=3;b.serial=77;b.logic_step=100;
  cpu.gqr[2]=0x40004;cpu.gqr[3]=0x50005;cpu.gqr[4]=0x60006;cpu.gqr[5]=0x70007;cpu.r[1]=0x600100C8;cpu.r[9]=0x12345678;cpu.f[7].ps1=9.25;cpu.pc=0x1234;for(unsigned n=0;n<64;++n)frame[n]=uint8_t(n);
 }
 R::Sources source(){R::Sources s;
  s.capture=[&](auto&v){v=live;return true;};s.retained=[&](const auto&x){return claimed&&x==e;};s.boundary=[&](const Cpu*c,const auto&x,const auto&b){return c==&cpu&&x==e&&b.serial==77;};s.model=[&](const auto&x,unsigned n,const auto&m,const auto&t){++graph_checks;return x==e&&m==models[n]&&t==transforms[n]&&bad!=8;};s.stack=[&](const Cpu*c,const auto&){return c==&cpu;};
  s.read=[&](uint32_t at,std::span<uint8_t>out){++reads;if(bad==12)return false;if(bad==5&&calls)return false;if(at!=0x60010088||out.size()!=64)return false;std::copy(frame.begin(),frame.end(),out.begin());if(bad==6&&calls)out[0]^=1;return true;};s.write=[&](uint32_t at,std::span<const uint8_t>b){++writes;if(at!=0x60010088||b.size()!=64)return false;std::copy(b.begin(),b.end(),frame.begin());return bad!=4;};
  s.call=[&](Cpu*c,uint32_t fn,std::initializer_list<uint32_t>a){++calls;if(bad==11){assert(active&&active->run(c)==R::Result::Quarantined);}assert(a.size()==1);unsigned n=(calls-1)/2;assert(n<2);const bool base=(calls&1);assert(fn==(base?GC(0x022BF3A0):GC(0x025E55A0)));assert(*a.begin()==(base?e.actors[n].address:models[n].morf));std::memset(c,0xCD,sizeof *c);frame.fill(0xEE);
   if(bad==3)throw std::runtime_error("native fail");
   if(base){live.transforms[n]=transforms[n];if(bad==7)live.transforms[n].base[0]^=1;}else {live.current_model=bad==9?models[1-n].model:models[n].model;live.root_calc[n]=models[n].morf;live.mtx_calc[n]=models[n].morf;std::copy_n(live.animations[n].frame_control.begin()+4,4,live.animations[n].cached_frame.begin());if(bad==13)++live.animations[n].frame_control[7];if(bad==14)live.animations[n].cached_frame[0]=0;if(bad==15)live.root_calc[n]=models[1-n].morf;if(bad==16)++live.animations[n].morph[11];if(bad==17)live.mtx_calc[n]=models[1-n].morf;}
   if(bad==1)++live.poses[1].identity.pid;if(bad==2)++live.models[1].model_data;if(bad==10)++live.boundary.serial;return 0u;};s.quarantine=[&]{++q;};return s;
 }
 R::Invoker invoker(){return {e,poses,live.boundary,models,transforms,stack,source()};}
};
// Standalone-only issuer primitive. Production defines this friend exclusively
// in threads.cpp after actual scheduler/registry/core parking proof.
namespace threads {class NativeQuiescenceIssuer {public:
 static NativeQuiescence test(bool&live,bool read_only=false){return NativeQuiescence([&](const Cpu*){return live;},[]{},read_only);}
};}
namespace X=story_native_cancel_observation;
namespace guestmods {class NativeOuterReturnIssuer {public:
 static void test(Cpu*c,D::Tracker&t,D::FrameWitness w,const std::function<void(const story_native_outer_return::OuterReturn&)>&body){story_native_outer_return::OuterReturn token(c,t,std::move(w));body(token);}
};}
int main(){
 CameraFixture f;
 const story_native_people_pose_capture::Layout pose_layout{0x25000000,0x25001000};f.mem[pose_layout.profile].resize(0x14);f.mem[pose_layout.profile][8]=1;f.mem[pose_layout.profile][9]=0x75;put(f.mem[pose_layout.profile],0x10,0x920);f.mem[GD(0x101C41D4)+0x18].resize(16);put(f.mem[GD(0x101C41D4)+0x18],4,0x432A0000);put(f.mem[GD(0x101C41D4)+0x18],12,0x43160000);
 for(unsigned n=0;n<2;++n){auto&actor=f.mem[f.e.actors[n].address];put(actor,4,f.e.actors[n].pid);actor[0xE]=1;actor[0xF]=0x75;put(actor,0x10,pose_layout.profile);put(actor,0xB0,0x00FF0000);put(actor,0xB4,pose_layout.vtable);actor[0x2DD]=actor[0x905]=1;actor[0x326]=2;put(actor,0x8A0,GD(0x101C41D4));f.poses[n].angles[0]=1;f.poses[n].angles[1]=2;actor[0x320]=3;actor[0x321]=4;}
 f.e.stage_name[0]='S';f.e.room_number=2;f.b.expected=f.e;
#ifdef STORY_CANCEL_BEFORE_CAMERA
 STORY_CANCEL_BEFORE_CAMERA(f);
#endif
 D::Tracker tracker(f.e,{{11,11,true},{22,22,true},{33,33,true},true});
 D::Tracker impostor(f.e,{{11,11,true},{22,22,true},{33,33,true},true});{auto scope=impostor.enter(f.b.parent);assert(scope.allowed());}assert(impostor.begin_drain());
 D::FrameWitness witness;
 {auto scope=tracker.enter(f.b.parent);assert(scope.allowed());witness=*tracker.current_owned_frame();f.outer=witness.root_serial();f.b.depth=tracker.snapshot();
 f.make_barrier();auto ready=f.barrier->ready(&f.cpu,GC(0x024FE3E8));assert(ready);f.make();assert(f.helper->run(&f.cpu)==Result::Completed);assert(f.barrier->run(&f.cpu,GC(0x024FE3E8),*ready,f.helper->completion(),[&]{f.original();})==B::Result::Returned);}
 X::Result saved_closed,saved_detached;const story_native_outer_return::OuterReturn*active_outer=nullptr;const X::Result*active_result=nullptr;
 int fault=0;unsigned samples=0;bool released=false,parked=true;X::Sources source;source.quiescence=std::make_shared<threads::NativeQuiescence>(threads::NativeQuiescenceIssuer::test(parked));
 source.retained=[&](const auto&e){return e==f.e&&fault!=1;};source.resident=[&](const auto&e,unsigned){return e==f.e&&fault!=2;};source.verify_camera=[&](const auto&e,const auto&){return e==f.e&&fault!=3;};
 source.sample=[&](Cpu*c,X::Sample&out){assert(c==&f.cpu);++samples;assert(f.read(f.e.identity.manager,out.state.manager)&&f.read(f.e.identity.image,out.state.image));auto&o=out.observation;o.identity=f.e.identity;o.room_number=f.e.room_number;o.stage_name=f.e.stage_name;o.resource_generation_matches=true;o.control_owned=!released;o.control_released=released;o.image=out.state.image;o.camera=f.current;
 for(unsigned n=0;n<8;++n)o.manager[n]=uint32_t(out.state.manager[n*4])<<24|uint32_t(out.state.manager[n*4+1])<<16|uint32_t(out.state.manager[n*4+2])<<8|out.state.manager[n*4+3];o.camera_play=uint32_t(out.state.manager[0x23]);o.current_event=uint32_t(out.state.manager[0x53C])<<24|uint32_t(out.state.manager[0x53D])<<16|uint32_t(out.state.manager[0x53E])<<8|out.state.manager[0x53F];std::copy_n(out.state.manager.begin()+0x3C,0x500,o.flags.begin());
 for(unsigned n=0;n<2;++n){assert(f.read(f.e.actors[n].address,out.state.actors[n]));o.actors[n]=f.e.actors[n];std::copy_n(out.state.actors[n].begin()+0x3E0,0x6C,o.actors[n].cut.begin());out.poses[n]=f.poses[n];out.poses[n].cut=o.actors[n].cut;}
 auto pose_read=[&](auto a,auto bytes){return source.quiescence->current(c)&&f.read(a,bytes)&&source.quiescence->current(c);};auto pose_verify=[&](const auto&a,const auto&i){return a==f.e.actors[a.address==f.e.actors[0].address?0:1]&&i==f.e.identity;};
 for(unsigned n=0;n<2;++n){if(released){if(!story_native_people_pose_capture::capture(pose_read,pose_verify,pose_layout,f.e,n,out.poses[n]))return false;}else if(active_result){if(!story_native_cancel_pose_capture::capture(pose_read,pose_verify,pose_layout,f.e,n,*active_result,c,tracker,*active_outer,*source.quiescence,out.poses[n]))return false;}else if(!story_native_cancel_pose_capture::capture_closed(pose_read,pose_verify,pose_layout,f.e,n,f.barrier->completion(),c,tracker,*active_outer,*source.quiescence,out.poses[n]))return false;}
 auto&b=out.boundary;b.identity=f.e.identity;b.exclusive=b.fenced=b.manager_owned=true;b.logic_step=100;b.serial=tracker.snapshot().serial;b.control_mode=released?0:3;
 if(fault==4)out.state.image[0x12F]^=1;if(fault==5)out.state.manager[0x3F]^=1;if(fault==6)out.state.actors[0][0x320]^=1;if(fault==7)o.camera.work37C[0]^=1;if(fault==8)++b.serial;if(fault==9)++b.logic_step;if(fault==10)o.actors[0].fingerprint[0]^=1;if(fault==11)b.fenced=false;if(fault==12)o.control_owned=false;if(fault==13&&samples%2==0)out.poses[0].angles[0]^=1;if(fault==14)o.image=f.e.program->pristine();return true;};
 auto validate=[&]{return X::Validator(f.e,source);};guestmods::NativeOuterReturnIssuer::test(&f.cpu,tracker,witness,[&](const auto&outer){
 active_outer=&outer;assert(!validate().closed(&f.cpu,tracker,outer,f.barrier->completion()).valid());assert(outer.claim(&f.cpu,f.e));assert(!validate().closed(&f.cpu,tracker,outer,f.barrier->completion()).valid());assert(tracker.begin_drain());
 parked=false;assert(!validate().closed(&f.cpu,tracker,outer,f.barrier->completion()).valid());parked=true;
 auto closed=validate().closed(&f.cpu,tracker,outer,f.barrier->completion());assert(closed.valid()&&closed.phase()==X::Phase::Closed&&!closed.released()&&closed.matches(f.e)&&closed.matches_baseline(f.poses)&&closed.logic_step()==100&&closed.serial()==tracker.snapshot().serial);
 assert(impostor.snapshot().serial==tracker.snapshot().serial);assert(!validate().closed(&f.cpu,impostor,outer,f.barrier->completion()).valid());
 saved_closed=closed;active_result=&closed;
 auto pose_read=[&](auto a,auto bytes){return f.read(a,bytes);};auto pose_verify=[&](const auto&a,const auto&i){return (a==f.e.actors[0]||a==f.e.actors[1])&&i==f.e.identity;};P::Snapshot captured;
 assert(story_native_cancel_pose_capture::capture(pose_read,pose_verify,pose_layout,f.e,0,closed,&f.cpu,tracker,outer,*source.quiescence,captured)&&captured.angles[0]==3);
 auto&actor=f.mem[f.e.actors[0].address];actor[0x320]=1;assert(!story_native_cancel_pose_capture::capture(pose_read,pose_verify,pose_layout,f.e,0,closed,&f.cpu,tracker,outer,*source.quiescence,captured)&&!captured.identity.actor);actor[0x321]=2;assert(story_native_cancel_pose_capture::capture(pose_read,pose_verify,pose_layout,f.e,0,closed,&f.cpu,tracker,outer,*source.quiescence,captured)&&captured.angles==f.poses[0].angles);assert(!story_native_cancel_pose_capture::capture_closed(pose_read,pose_verify,pose_layout,f.e,0,f.barrier->completion(),&f.cpu,tracker,outer,*source.quiescence,captured));actor[0x320]=3;actor[0x321]=4;
 actor[0x2EC]^=1;assert(!story_native_cancel_pose_capture::capture(pose_read,pose_verify,pose_layout,f.e,0,closed,&f.cpu,tracker,outer,*source.quiescence,captured));actor[0x2EC]^=1;
 f.mem[GD(0x101C41D4)+0x18][0]=1;assert(!story_native_cancel_pose_capture::capture(pose_read,pose_verify,pose_layout,f.e,0,closed,&f.cpu,tracker,outer,*source.quiescence,captured));f.mem[GD(0x101C41D4)+0x18][0]=0;
 unsigned actor_reads=0;auto changing=[&](uint32_t a,std::span<uint8_t>bytes){if(!f.read(a,bytes))return false;if(a==f.e.actors[0].address&&++actor_reads==3)bytes[4]^=1;return true;};assert(!story_native_cancel_pose_capture::capture(changing,pose_verify,pose_layout,f.e,0,closed,&f.cpu,tracker,outer,*source.quiescence,captured));
 X::Sample partial;assert(source.sample(&f.cpu,partial));assert(!O::validate(O::Operation::Terminal,f.e,partial.observation).valid());
 for(fault=1;fault<=14;++fault){samples=0;assert(!validate().closed(&f.cpu,tracker,outer,f.barrier->completion()).valid());}fault=0;
 assert(!validate().closed(&f.cpu,tracker,outer,B::Completion{}).valid());auto wrong=f.e;++wrong.identity.generation;assert(!closed.matches(wrong));auto poses=f.poses;poses[0].angles[0]^=1;assert(!closed.matches_baseline(poses));
 assert(!validate().detached(&f.cpu,tracker,outer,closed,{}).valid());assert(!validate().released(&f.cpu,tracker,outer,closed).valid());
 std::thread foreign([&]{assert(!validate().closed(&f.cpu,tracker,outer,f.barrier->completion()).valid());});foreign.join();assert(validate().closed(&f.cpu,tracker,outer,f.barrier->completion()).valid());
#ifdef STORY_CANCEL_AFTER_CLOSED
 STORY_CANCEL_AFTER_CLOSED(f,tracker,outer,closed,source);
#endif
#ifdef STORY_CANCEL_WRITER_TEST_HOOK
 STORY_CANCEL_WRITER_TEST_HOOK(f,tracker,outer,*source.quiescence,closed,source);
#endif
 // A genuine refresh helper receipt is required after exact baseline writes.
 for(unsigned n=0;n<2;++n){P::Plan plan;assert(P::make(f.poses[n],f.poses[n].identity,true,true,true,true,plan));for(const auto&p:plan.patches)assert(f.write(p.address,p.bytes));}
 f.current=f.e.camera;
 RefreshFixture refresh;refresh.e=f.e;refresh.poses=f.poses;refresh.live.poses=f.poses;
 for(unsigned n=0;n<2;++n){refresh.models[n].morf=f.poses[n].identity.morf;refresh.models[n].model=f.poses[n].identity.model;refresh.models[n].user_area=f.e.actors[n].address;}refresh.live.models=refresh.models;
 X::Sample detached_sample;assert(source.sample(&f.cpu,detached_sample));refresh.live.boundary=detached_sample.boundary;auto rs=refresh.source();rs.boundary=[&](const Cpu*c,const auto&e,const auto&b){return c==&refresh.cpu&&e==f.e&&b.serial==tracker.snapshot().serial;};
 R::Invoker invoker(refresh.e,refresh.poses,refresh.live.boundary,refresh.models,refresh.transforms,refresh.stack,rs);assert(invoker.run(&refresh.cpu)==R::Result::Complete);
 auto detached=validate().detached(&f.cpu,tracker,outer,closed,invoker.completion());saved_detached=detached;assert(detached.valid()&&detached.phase()==X::Phase::Detached&&!detached.released()&&detached.same_cancellation(closed));
 assert(!validate().released(&f.cpu,tracker,outer,detached).valid());
#ifdef STORY_CANCEL_AFTER_DETACHED
 STORY_CANCEL_AFTER_DETACHED(f,tracker,outer,closed,detached,source);
#endif
 released=true;auto&manager=f.mem[f.e.identity.manager];for(unsigned n=0;n<8;++n)put(manager,n*4,f.e.retail_manager[n]);put(manager,0x20,f.e.retail_camera_play);put(manager,0x53C,f.e.retail_current_event);std::copy(f.e.retail_flags.begin(),f.e.retail_flags.end(),manager.begin()+0x3C);
 auto release=validate().released(&f.cpu,tracker,outer,detached);assert(release.valid()&&release.released()&&release.matches_baseline(f.poses)&&release.same_cancellation(closed)&&release.same_cancellation(detached));
#ifdef STORY_CANCEL_AFTER_RELEASED
 STORY_CANCEL_AFTER_RELEASED(f,tracker,outer,closed,detached,release,source);
#endif
 f.mem[f.e.actors[0].address][0x400]^=1;assert(!validate().released(&f.cpu,tracker,outer,detached).valid());f.mem[f.e.actors[0].address][0x400]^=1;
 manager[0x3F]^=1;assert(!validate().released(&f.cpu,tracker,outer,detached).valid());manager[0x3F]^=1;f.mem[f.e.identity.image][0x12F]^=1;assert(!validate().released(&f.cpu,tracker,outer,detached).valid());
 });assert(!X::Result{}.valid());
 // A new real token/Tracker can have identical Expected/root/serial values;
 // the old cancellation's opaque shared-frame binding still cannot transfer.
 D::Tracker replacement(f.e,{{11,11,true},{22,22,true},{33,33,true},true});D::FrameWitness replacement_frame;{auto scope=replacement.enter(f.b.parent);assert(scope.allowed());replacement_frame=*replacement.current_owned_frame();}
 guestmods::NativeOuterReturnIssuer::test(&f.cpu,replacement,replacement_frame,[&](const auto&fresh){assert(fresh.claim(&f.cpu,f.e)&&replacement.begin_drain());assert(fresh.root_serial()==saved_closed.returned().after().outer_serial&&replacement.snapshot().serial==saved_closed.serial());assert(!saved_closed.current_binding(&f.cpu,replacement,fresh));const auto before=samples;assert(!validate().released(&f.cpu,replacement,fresh,saved_detached).valid()&&samples==before);});return 0;
}
