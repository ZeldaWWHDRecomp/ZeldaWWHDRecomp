// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_wait_detach_plan.h"
#include <cassert>
#include <array>
#include <cstdint>
#include <span>
using namespace story_native_wait_detach_plan;
using story_native_event_observation::Camera;
using story_native_event_lease::Identity;
static void put(std::span<uint8_t>b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
static bool camera_policy(const Camera&c,const Identity&){return c.type_count==63&&c.style_count==145&&c.style<145&&c.param_index==c.style&&c.param_pointer==c.style_table+c.style*0x84;}
struct Fixture {
 story_native_event_observation::Expected e{};std::array<P::Snapshot,2> actors{},live{};
 C::Boundary boundary{};
 Fixture(){
  e.identity={0x10474374,0x60000000,0x520,77,88,3,4};
  for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x20001000+n*0x2000;a.pid=10+n;a.token=20+n;a.profile=0x175;a.subtype=1;a.cut[0x60]=1;
   auto&s=actors[n];s.identity={a.address,a.pid,a.token,77,88,3,0x30001000+n*0x2000,0x30002000+n*0x2000};s.cut=a.cut;live[n]=s;put(live[n].cut,0,e.identity.image+0xF0+n*0x50);put(live[n].cut,4,n);put(live[n].cut,8,a.address);put(live[n].cut,0x10,0);}
  auto&c=e.camera;c.address=0x20008248;c.pid=12;c.current_type=c.next_type=1;c.event_type=2;c.style=c.param_index=3;c.style_table=0x1004487C;c.style_count=145;c.param_pointer=c.style_table+3*0x84;c.type_table=0x10049350;c.type_count=63;c.timer=30;put(c.work37C,8,30);put(c.event_data,12,0xFFFFFFFFu);put(c.engine100,0x14,7);put(c.mode_phase13C,0,7);put(c.mode_phase13C,4,7);
  boundary.identity=e.identity;boundary.exclusive=boundary.fenced=boundary.manager_owned=true;boundary.control_mode=3;boundary.serial=9;boundary.logic_step=100;
 }
 Plan plan(bool cancel=false){return prepare(e,actors,live,e.camera,boundary,camera_policy,true,true,true,cancel);}
};
int main(){
 Fixture f;auto p=f.plan();assert(p.valid()&&p.camera().segments().size()==12);
 for(unsigned n=0;n<2;++n){assert(p.people()[n].patches.size()==1&&p.people()[n].patches[0].address==f.e.actors[n].address+0x3E0);}
 assert(p.verify_post(f.actors,f.e.camera,f.boundary,camera_policy));
 auto stale=f.actors;stale[1].cut[0]^=1;assert(!p.verify_post(stale,f.e.camera,f.boundary,camera_policy));
 auto changed=f;changed.live[1].identity.pid++;assert(!changed.plan().valid()); // second actor must validate before any writer exists
 changed=f;changed.live[1].cut[0]^=1;assert(!changed.plan().valid());
 changed=f;put(changed.live[1].cut,4,0);assert(!changed.plan().valid());
 changed=f;put(changed.live[1].cut,8,changed.e.actors[0].address);assert(!changed.plan().valid());
 changed=f;put(changed.live[1].cut,0x10,2);assert(!changed.plan().valid());
 changed=f;changed.e.camera.address=changed.e.actors[1].address;assert(!changed.plan().valid());
 changed=f;changed.e.actors[1].address=changed.e.actors[0].address+0x100;assert(!changed.plan().valid());
 changed=f;changed.e.actors[1].cut[0]^=1;assert(!changed.plan().valid());
 changed=f;changed.boundary.serial++;assert(changed.plan().valid()); // a new preflight, never a reused post-boundary
 assert(!p.verify_post(f.actors,f.e.camera,changed.boundary,camera_policy));
 auto cancel=f.plan(true);assert(cancel.valid()&&cancel.people()[0].patches.size()==10);
 stale=f.actors;stale[0].angles[0]^=1;assert(!cancel.verify_post(stale,f.e.camera,f.boundary,camera_policy));
}
