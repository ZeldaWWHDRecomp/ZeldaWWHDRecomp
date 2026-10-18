// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_people_cancel_refresh.h"
#include "mods/story_native_refresh_stack.h"
#include "guest_addr.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
namespace R=story_native_people_cancel_refresh;
struct Fixture {
 R::Expected e;std::array<R::P::Snapshot,2>poses{};std::array<R::ModelIdentity,2>models{};std::array<R::Transform,2>transforms{};R::State live;
 Cpu cpu{};std::array<uint8_t,64>frame{};R::I::Config stack{{0x60010000,0x1000},{{0x60000000,0x520}}};
 unsigned calls=0,q=0,reads=0,writes=0,graph_checks=0;unsigned bad=0;bool claimed=true;R::Invoker* active=nullptr;
 Fixture(){e.identity={0x20000000,0x60000000,0x520,1,2,3,4};
  for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x47001000+n*0x1000;a.pid=10+n;a.token=n+1;a.profile=0x175;a.subtype=1;a.fingerprint.fill(n+1);models[n]={0x48000000+n*0x1000,0x49000000+n*0x1000,a.address,0x20010000+n*0x1000,0x20020000+n*0x1000,0x20030000+n*0x1000,0};poses[n].identity={a.address,a.pid,a.token,1,2,3,models[n].morf,models[n].model};poses[n].cut=a.cut;poses[n].status=0x40000;for(unsigned off:{0u,4u,8u})transforms[n].scale[off]=transforms[n].model_scale[off]=0x3F,transforms[n].scale[off+1]=transforms[n].model_scale[off+1]=0x80;for(unsigned off:{0u,20u,40u})transforms[n].base[off]=0x3F,transforms[n].base[off+1]=0x80;}
  for(unsigned n=0;n<2;++n){live.animations[n].address=0x48010000+n*0x1000;live.animations[n].frame_control[4]=0x3F;live.animations[n].frame_control[5]=0x80;}
  live.poses=poses;live.models=models;live.transforms=transforms;live.transforms[0].base[0]=0;live.transforms[1].base[0]=0;
  auto&b=live.boundary;b.identity=e.identity;b.exclusive=b.fenced=b.manager_owned=true;b.control_mode=3;b.serial=77;b.logic_step=100;
  cpu.gqr[2]=0x40004;cpu.gqr[3]=0x50005;cpu.gqr[4]=0x60006;cpu.gqr[5]=0x70007;cpu.r[1]=0x600100C8;cpu.r[9]=0x12345678;cpu.f[7].ps1=9.25;cpu.pc=0x1234;for(unsigned n=0;n<64;++n)frame[n]=uint8_t(n);
 }
 R::Sources source(){R::Sources s;const Cpu caller=cpu;
  s.capture=[&](auto&v){v=live;return true;};s.retained=[&](const auto&x){return claimed&&x==e;};s.boundary=[&](const Cpu*c,const auto&x,const auto&b){return c==&cpu&&x==e&&b.serial==77;};s.model=[&](const auto&x,unsigned n,const auto&m,const auto&t){++graph_checks;return x==e&&m==models[n]&&t==transforms[n]&&bad!=8;};s.stack=[&](const Cpu*c,const auto&){return c==&cpu;};
  s.read=[&](uint32_t at,std::span<uint8_t>out){++reads;if(bad==12)return false;if(bad==5&&calls)return false;if(at!=0x60010088||out.size()!=64)return false;std::copy(frame.begin(),frame.end(),out.begin());if(bad==6&&calls)out[0]^=1;return true;};s.write=[&,caller](uint32_t at,std::span<const uint8_t>b){assert(!std::memcmp(&caller,&cpu,sizeof cpu));++writes;if(at!=0x60010088||b.size()!=64)return false;std::copy(b.begin(),b.end(),frame.begin());return bad!=4;};
  s.call=[&](Cpu*c,uint32_t fn,std::initializer_list<uint32_t>a){++calls;if(bad==11){assert(active&&active->run(c)==R::Result::Quarantined);}assert(a.size()==1);unsigned n=(calls-1)/2;assert(n<2);const bool base=(calls&1);assert(fn==(base?GC(0x022BF3A0):GC(0x025E55A0)));assert(*a.begin()==(base?e.actors[n].address:models[n].morf));std::memset(c,0xCD,sizeof *c);frame.fill(0xEE);
   if(bad==3)throw std::runtime_error("native fail");
   if(base){live.transforms[n]=transforms[n];if(bad==7)live.transforms[n].base[0]^=1;}else {live.current_model=bad==9?models[1-n].model:models[n].model;live.root_calc[n]=models[n].morf;live.mtx_calc[n]=models[n].morf;std::copy_n(live.animations[n].frame_control.begin()+4,4,live.animations[n].cached_frame.begin());if(bad==13)++live.animations[n].frame_control[7];if(bad==14)live.animations[n].cached_frame[0]=0;if(bad==15)live.root_calc[n]=models[1-n].morf;if(bad==16)++live.animations[n].morph[11];if(bad==17)live.mtx_calc[n]=models[1-n].morf;}
   if(bad==1)++live.poses[1].identity.pid;if(bad==2)++live.models[1].model_data;if(bad==10)++live.boundary.serial;return 0u;};s.quarantine=[&]{++q;};return s;
 }
 R::Invoker invoker(){return {e,poses,live.boundary,models,transforms,stack,source()};}
};
int main(){
 static_assert(story_native_refresh_stack::fits(0x60000000,0x60010000,0x60002000));
 static_assert(!story_native_refresh_stack::fits(0x60000000,0x60010000,0x60001FF8));
 static_assert(!story_native_refresh_stack::fits(0x60000000,0x60010000,0x60002004));
 static_assert(!story_native_refresh_stack::fits(0xFFFFF000,0xFFFFFFFF,0xFFFFFFF8));

 {Fixture f;auto saved=f.cpu;auto frame=f.frame;auto r=f.invoker();assert(!r.publication_enabled()&&!r.completion().valid());assert(r.run(&f.cpu)==R::Result::Complete&&f.calls==4&&!f.q&&f.frame==frame&&!std::memcmp(&saved,&f.cpu,sizeof saved));auto done=r.completion();assert(done.matches(f.e,f.poses,f.live.boundary));auto wrong=f.e;++wrong.identity.generation;assert(!done.matches(wrong,f.poses,f.live.boundary));auto poses=f.poses;poses[1].joint[0]=1;assert(!done.matches(f.e,poses,f.live.boundary));auto b=f.live.boundary;++b.serial;assert(!done.matches(f.e,f.poses,b));assert(r.run(&f.cpu)==R::Result::Refused&&f.calls==4);assert(f.live.current_model==f.models[1].model);}
 {Fixture f;f.cpu.gqr[5]=0;auto saved=f.cpu;auto r=f.invoker();assert(r.run(&f.cpu)==R::Result::Quarantined&&!f.calls&&f.q==1&&!std::memcmp(&saved,&f.cpu,sizeof saved));}
 for(unsigned bad=1;bad<=17;++bad){Fixture f;f.bad=bad;auto saved=f.cpu;auto frame=f.frame;auto r=f.invoker();f.active=&r;const auto result=r.run(&f.cpu);if(bad==8)assert(result==R::Result::Refused&&!f.calls&&!f.q);else assert(result==R::Result::Quarantined&&f.q==1&&(f.calls||bad==12)&&f.calls<=2);assert(!r.completion().valid()&&!std::memcmp(&saved,&f.cpu,sizeof saved));assert(f.frame==frame);if(bad!=8)assert(r.run(&f.cpu)==R::Result::Quarantined&&f.q==1);}
 {Fixture f;auto r=f.invoker();++f.live.poses[1].identity.pid;assert(r.run(&f.cpu)==R::Result::Refused&&!f.calls&&!f.writes&&!f.q);}
 {Fixture f;auto r=f.invoker();f.live.transforms[1].position[0]=1;assert(r.run(&f.cpu)==R::Result::Refused&&!f.calls);}
 {Fixture f;auto r=f.invoker();f.claimed=false;assert(r.run(&f.cpu)==R::Result::Refused&&!f.calls);}
}
