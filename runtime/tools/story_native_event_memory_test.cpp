// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_memory.h"
#include "mods/story_native_event_image.h"
#include <algorithm>
#include <cassert>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
using namespace story_native_event_memory;
struct Fake {
 Expected e;Config c;Scope s;unsigned reads=0,writes=0;bool allocation=true;std::array<uint8_t,0xF4>control{};
 Fake(){e.identity={0x10474374,0x60000000,0x520,77,88,3,4};e.frames=30;e.actors[0]={0x20001000,10,1,0x175,1};e.actors[1]={0x20002000,11,2,0x175,1};e.camera.address=0x20004248;e.camera.pid=12;c={0x60001000,0x20006000,55,{0x60002000,4096}};s.identity=e.identity;s.link=c.link;s.link_pid=c.link_pid;s.exclusive=s.bindings_verified=s.fenced=true;s.logic_step=100;s.serial=9;}
 Policy policy(){return Policy(e,c,[&](Scope&o){o=s;return true;},[&](const auto&,const auto&){return allocation;},[&](uint32_t a,std::span<uint8_t>b){++reads;if(a==e.identity.manager-0xF4&&b.size()==control.size())std::copy(control.begin(),control.end(),b.begin());else std::fill(b.begin(),b.end(),0);return true;},[&](uint32_t,std::span<const uint8_t>){++writes;return true;});}
};
static std::array<uint8_t,4> be(uint32_t v){return {uint8_t(v>>24),uint8_t(v>>16),uint8_t(v>>8),uint8_t(v)};}
int main(){
 Fake f;auto p=f.policy();std::array<uint8_t,4>b{};assert(p.read(f.e.actors[0].address+4,b));assert(p.transition(Phase::Preparing));auto image=story_native_event_image::make_wait_fixture();assert(p.write(f.e.identity.image,image));assert(p.transition(Phase::Publishing));assert(p.write(f.e.identity.manager,be(f.e.identity.image)));f.s.fenced=false;assert(p.transition(Phase::Running));f.s.fenced=true;f.s.logic_step=200;f.s.serial=20;assert(p.transition(Phase::Draining));assert(p.transition(Phase::Restoring));assert(p.write(f.e.identity.manager,be(0)));assert(p.write(f.e.identity.manager+0x3C,f.e.retail_flags));assert(p.transition(Phase::Closed));assert(!p.write(f.e.identity.manager,be(0)));
 // TURN uses its authenticated full extent and exact pools; the larger record
 // does not grant arbitrary image bytes or WAIT-layout pointer writes.
 for(unsigned fault=0;fault<6;++fault){Fake x;story_native_turn_camera_image::Program def;def.camera.start_eye={0,10,20};def.camera.end_eye={10,10,20};x.e.program=story_native_event_program::Program::turn(def,301);x.e.identity.bytes=0xE60;x.s.identity=x.e.identity;x.c.record.bytes=16384;std::vector<uint8_t>canonical(x.e.program->pristine().begin(),x.e.program->pristine().end());const auto offsets=x.e.program->pool_offsets();if(fault==3)x.e.program.reset();if(fault==4)x.c.record.bytes++;if(fault==5)x.e.frames++;auto typed=x.policy();if(fault>=3){assert(!typed.transition(Phase::Preparing)&&x.writes==0);continue;}assert(typed.transition(Phase::Preparing));if(fault==1){canonical.back()^=1;assert(!typed.write(x.e.identity.image,canonical)&&x.writes==0);continue;}assert(typed.write(x.e.identity.image,canonical));assert(typed.transition(Phase::Publishing));if(fault==2){assert(!typed.write(x.e.identity.manager+16,be(x.e.identity.image+0x410))&&x.writes==1);continue;}for(unsigned n=0;n<8;++n)assert(typed.write(x.e.identity.manager+n*4,be(x.e.identity.image+offsets[n])));assert(x.writes==9);}
 // Full synchronous rollback is exact, and cannot survive an intervening native entry.
 Fake r;auto rollback=r.policy();assert(rollback.transition(Phase::Preparing));assert(rollback.transition(Phase::Rollback));assert(rollback.write(r.e.identity.manager-0xF4,r.control));assert(rollback.transition(Phase::Closed));
 Fake stale;auto late=stale.policy();assert(late.transition(Phase::Preparing));++stale.s.serial;assert(!late.transition(Phase::Rollback)&&late.phase()==Phase::Quarantined&&stale.writes==0);
 // Arbitrary addresses, stack, camera/NPC mutations, and noncanonical image bytes are denied before the backend.
 for(unsigned n=0;n<6;++n){Fake x;auto k=x.policy();assert(k.transition(Phase::Preparing));bool ok=false;if(n==0)ok=k.read(0x20009900,b);if(n==1)ok=k.write(x.e.camera.address+0x408,be(0));if(n==2)ok=k.write(x.e.actors[0].address+0x3E0,be(0));if(n==3){auto bad=image;bad.back()^=1;ok=k.write(x.e.identity.image,bad);}if(n==4)ok=k.read(0xFFFFFFFC,b);if(n==5)ok=k.write(x.c.stamp,be(0));assert(!ok&&k.phase()==Phase::Quarantined&&x.writes==0);}
 for(unsigned n=0;n<5;++n){Fake x;auto k=x.policy();if(n==0)x.s.identity.generation++;if(n==1)x.s.foreign=true;if(n==2)x.s.callback_depth=1;if(n==3)x.allocation=false;if(n==4)x.s.fenced=false;assert(!k.transition(Phase::Preparing)&&x.writes==0);}
 Fake payload;auto wrong=payload.policy();assert(wrong.transition(Phase::Preparing));assert(wrong.transition(Phase::Publishing));assert(!wrong.write(payload.e.identity.manager,be(0x60000004))&&payload.writes==0);
 Fake invalid;invalid.c.record.address=invalid.e.identity.image;auto overlap=invalid.policy();assert(!overlap.read(invalid.e.identity.image,b)&&invalid.reads==0);
 Fake busy;busy.control[0xC2]=1;auto foreign=busy.policy();assert(!foreign.transition(Phase::Preparing)&&busy.writes==0);
}
