// SPDX-License-Identifier: MPL-2.0
// Genuine Controller/Session cancellation and reusable-slot proof. Only the
// physical scheduler, native bodies and synchronous outer issuer are synthetic
// trusted primitives; no cancellation/rearm/idle certificate is fabricated.
#include "native_quiescence.h"
#include <cassert>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <cstdio>
#include <tuple>
#define STORY_NATIVE_READINESS_QUERY_PRECONDITION(c) assert(!threads::current_thread_holds_native_quiescence())
#include <tuple>
#include "native_quiescence.h"
#define STORY_NATIVE_READINESS_QUERY_PRECONDITION(c) assert(!threads::current_thread_holds_native_quiescence())
#include <type_traits>
#include "mods/story_native_wait_snapshot_roots.h"
#include "mods/story_native_wait_snapshot_actor_roots.h"
#include "mods/story_native_wait_run_footprint.h"
static unsigned rearm_fault;
static unsigned clean_checks;
template<class...A>static void rearm_before(A&...);
template<class...A>static void rearm_after(A&...);
#define STORY_NATIVE_CANCEL_REARM_BEFORE_CONTROLLER(...) rearm_before(__VA_ARGS__)
#define STORY_NATIVE_CANCEL_REARM_AFTER_OUTER(...) rearm_after(__VA_ARGS__)
#define STORY_NATIVE_CANCEL_REARM_FIXTURE_ONLY
#include "story_native_wait_controller_cancel_test.cpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <cstdio>

// Synthetic trusted frontend adapter exposes only the existing friend's private
// read-only query. Cancellation, origin issuance and lifetime checks stay real;
// actual Context route composition is verified by the headless game run.
namespace story_native_wait_runtime_controller {class Context {public:
 static bool retired(const C::Controller&c,const Cpu*cpu){return c.clean_cancel_retired(cpu);}
};}



template<class...A>static void rearm_before(A&...args){
 auto tuple=std::tie(args...);auto&[f,session,depth,cpu,e,layout,cfg,sources,source_fault,live_step]=tuple;
 sources.cancel_rearm=[&,layout](Cpu*c,const auto&expected,const auto&scope){
  ++clean_checks;if(c!=&cpu||expected!=e||source_fault||!scope.fenced)return false;
  auto q=threads::NativeQuiescenceIssuer::test();if(!q.current(c))return false;
  auto read=[&](uint32_t at,std::span<uint8_t>b){if(at>=layout.allocation.address&&uint64_t(at)+b.size()<=uint64_t(layout.allocation.address)+layout.allocation.bytes){std::copy_n(mem::ptr(at),b.size(),b.begin());return true;}auto*p=f.pointer(at,b.size());if(!p)return false;std::copy_n(p,b.size(),b.begin());return true;};
  namespace F=story_native_wait_run_footprint;
  auto capture=[&](F::Footprint&out){auto req=request();bool roots=story_native_wait_snapshot_roots::capture(read,layout,out);bool actors=roots&&story_native_wait_snapshot_actor_roots::append(read,layout,req.mod,req.residents,out);return roots&&actors;};
  F::Footprint before,after;if(!capture(before))return false;
  if(rearm_fault==8&&clean_checks==2)++live_step;
  if(rearm_fault==9&&clean_checks==2)f.put(e.identity.manager,e.identity.image);
  return F::matches_live(read,before)&&capture(after)&&before==after&&F::matches_live(read,after)&&q.current(c);
 };
}
template<class...A>static void rearm_after(A&...args){
 auto tuple=std::tie(args...);auto&[f,session,controller,depth,cpu,e,layout,cfg,sources,poses,calls,claims,arms,claim,live_step,read,write,w,source_fault,hold,controller_owner]=tuple;
 assert(controller.state()==C::State::Released&&!claim&&!threads::current_thread_holds_native_quiescence());
 using Front=story_native_wait_runtime_controller::Context;
 assert(Front::retired(controller,&cpu));Cpu foreign_cpu=cpu;assert(!Front::retired(controller,&foreign_cpu));
 hold.reset();assert(!Front::retired(controller,&cpu));hold=session.try_fence();assert(hold&&Front::retired(controller,&cpu));
 static_assert(!std::is_default_constructible_v<S::Session::RearmCertificate>);
 static_assert(!std::is_copy_constructible_v<S::Session::RearmCertificate>);
 const auto original=e;const auto generation=session.journal()->generation;const auto native_calls=calls;
 // Cancellation preserved genuine early TURN cursors, never final/all-flags.
 std::array<uint8_t,4>cut{};assert(read(e.identity.image+0xF0+0x38,cut));assert(cut[0]==0&&cut[1]==0&&cut[2]==0&&cut[3]==7);
 ++live_step; // fresh real-source boundary, after the OuterReturn has expired
 if(rearm_fault!=10)cpu.r[1]=0x600300C0; // simulate next logic-service parent frame; case10 retains B8 and must refuse
 auto certificate=controller.take_rearm_certificate();assert(certificate);
 if(rearm_fault==1)w(e.identity.manager,e.identity.image);
 if(rearm_fault==2)w(e.actors[0].address+0x3E0,e.identity.image);
 if(rearm_fault==3)w(e.camera.address+0x8A8,e.identity.image);
 if(rearm_fault==4)source_fault=true;
 if(rearm_fault==7){controller_owner.reset();assert(!session.rearm(std::move(*certificate),&cpu));assert(session.journal()->generation==generation&&calls==native_calls);return;}
 if(rearm_fault==5){Cpu wrong=cpu;assert(!session.rearm(std::move(*certificate),&wrong));}
 else if(rearm_fault==6){S::Session foreign(request(),f.source());auto fence=foreign.try_fence();assert(fence&&!foreign.rearm(std::move(*certificate),&cpu));}
 else {const bool okay=session.rearm(std::move(*certificate),&cpu);if(okay!=(rearm_fault==0))std::fprintf(stderr,"cancel rearm fault=%u okay=%d clean_checks=%u\n",rearm_fault,int(okay),clean_checks);assert(okay==(rearm_fault==0));}
 assert(calls==native_calls);assert(!session.rearm(std::move(*certificate),&cpu));assert(!controller.take_rearm_certificate());
 if(rearm_fault){assert(session.journal()->generation==generation);return;}
 assert(session.journal()->generation!=generation&&!session.unexposed());
 assert(!Front::retired(controller,&cpu)); // old origin cannot detach a new generation
 assert(clean_checks>=2);hold.reset();bool parked=true;auto witness=threads::NativeQuiescenceIssuer::witness(&cpu,parked);
 auto idle=session.begin_idle_capture(&cpu,witness);assert(idle);auto checkpoint=session.commit_idle_capture(std::move(*idle),&cpu,witness);assert(checkpoint&&calls==native_calls);
 assert(session.inspect(&cpu));hold=session.try_fence();assert(hold);e=*session.expected();assert(e.identity.generation!=original.identity.generation);
 for(auto&p:poses)p.identity.generation=e.identity.generation;
 assert(session.stage_image(poses));D::Tracker next_depth(e,{{1,1,true},{2,2,true},{3,3,true},true});
 auto next_sources=sources;next_sources.validation.session=[&](story_native_event_memory::Scope&s){const auto d=next_depth.snapshot();s={};s.identity=e.identity;s.link=link_actor;s.link_pid=55;s.exclusive=s.bindings_verified=true;s.fenced=d.state==D::State::Draining;s.logic_step=live_step;s.serial=d.serial;return true;};
 next_sources.arm_clock=[](const auto&published){return published.valid();};auto next_config=cfg;next_config.collector.clock=[&](story_native_event_collect::Clock&out){out={e.identity,live_step,next_depth.snapshot().serial,0};return true;};
 const auto before=calls;C::Controller next(session,next_depth,e,layout,{},next_config,next_sources);assert(next.start(&cpu)==C::State::Started&&calls==before+7&&claim);
}
int main(){for(rearm_fault=0;rearm_fault<11;++rearm_fault){clean_checks=0;run(0);}}
