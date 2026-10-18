// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_outer_return.h"
#include "ppc.h"
#include <cassert>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
namespace D=story_native_event_depth;
namespace R=story_native_outer_return;
// Standalone-only trusted issuer seam. Production defines this friend solely
// inside guest_mods.cpp and must additionally prove the actual native pipeline
// and exactly one completed original. These tests exercise token primitives;
// synthetic Tracker scopes are never production native-return authority.
namespace guestmods {class NativeOuterReturnIssuer {
public:
 static void test(Cpu*c,D::Tracker&t,D::FrameWitness w,const std::function<void(const R::OuterReturn&)>&body){R::OuterReturn token(c,t,std::move(w));body(token);}
};}
static D::Expected expected(){D::Expected e;e.identity={0x20000000,0x60000000,0x520,1,2,3,4};e.room_number=11;for(unsigned n=0;n<2;++n)e.actors[n]={0x20001000+n*0x1000,10+n,1+n,0x175,1,{}};e.camera.address=0x20003248;e.camera.pid=12;return e;}
static D::Coverage coverage(){return {{11,11,true},{22,22,true},{33,33,true},true};}
static D::Call call(const D::Expected&e,D::Entry entry=D::Entry::CameraExecute){D::Call c;c.entry=entry;c.function=D::entry_address(entry);c.identity=e.identity;c.authority=D::Authority::Owned;c.pid=e.camera.pid;c.receiver=c.subject=e.camera.address-0x248;if(entry==D::Entry::CameraEvent)c.receiver=e.camera.address;if(entry==D::Entry::PeopleExecute){c.receiver=c.subject=e.actors[0].address;c.pid=e.actors[0].pid;c.token=e.actors[0].token;}return c;}
int main(){
 static_assert(!std::is_default_constructible_v<R::OuterReturn>);static_assert(!std::is_copy_constructible_v<R::OuterReturn>);static_assert(!std::is_move_constructible_v<R::OuterReturn>);
 auto e=expected();Cpu cpu{},wrong_cpu{};D::Tracker tracker(e,coverage()),other(e,coverage());D::FrameWitness witness,child;
 assert(!witness.valid()&&!witness.current(tracker)&&!witness.returned(tracker)&&!witness.root_serial());
 {auto outer=tracker.enter(call(e));assert(outer.allowed());witness=*tracker.current_owned_frame();assert(witness.valid()&&witness.current(tracker)&&!witness.returned(tracker)&&witness.root_serial()==witness.current_serial()&&witness.expected()==e&&!witness.current(other));
  const auto root=witness.root_serial();{auto inner=tracker.enter(call(e,D::Entry::CameraEvent));assert(inner.allowed());child=*tracker.current_owned_frame();assert(child.current(tracker)&&child.root_serial()==root&&child.current_serial()>root&&!witness.current(tracker));}
  assert(witness.current(tracker)&&tracker.snapshot().serial>root&&!child.current(tracker)&&!child.returned(tracker));
  std::thread foreign([&]{assert(!tracker.current_owned_frame()&&!witness.current(tracker)&&!witness.returned(tracker));});foreign.join();
 }
 assert(witness.returned(tracker)&&!witness.current(tracker)&&!witness.returned(other));
 guestmods::NativeOuterReturnIssuer::test(&cpu,tracker,witness,[&](const auto&token){
  assert(token.current(&cpu,e)&&!token.current(&wrong_cpu,e)&&!token.claimed_current(&cpu,e));auto wrong=e;++wrong.identity.generation;assert(!token.current(&cpu,wrong));assert(token.claim(&cpu,e)&&!token.claim(&cpu,e)&&token.claimed_current(&cpu,e));
  std::thread foreign([&]{assert(!token.current(&cpu,e)&&!token.claim(&cpu,e));});foreign.join();
  guestmods::NativeOuterReturnIssuer::test(&cpu,tracker,witness,[&](const auto&nested){assert(!nested.current(&cpu,e)&&!nested.claim(&cpu,e));});assert(token.current(&cpu,e));
  assert(tracker.begin_drain()&&token.claimed_current(&cpu,e));assert(tracker.cancel_drain()&&token.current(&cpu,e));
 });
 // Same returned frame cannot issue another token even after callback expiry.
 guestmods::NativeOuterReturnIssuer::test(&cpu,tracker,witness,[&](const auto&again){assert(!again.current(&cpu,e)&&!again.claim(&cpu,e));});
 D::FrameWitness fresh;{auto scope=tracker.enter(call(e));fresh=*tracker.current_owned_frame();}assert(!witness.returned(tracker)&&fresh.returned(tracker));
 guestmods::NativeOuterReturnIssuer::test(&cpu,tracker,fresh,[&](const auto&token){assert(token.current(&cpu,e));{auto entry=other.enter(call(e));assert(entry.allowed()&&!token.current(&cpu,e));}assert(!token.current(&cpu,e));});
 // Intervening entries invalidate returned proof, including another Tracker.
 assert(!fresh.returned(tracker));
 D::Tracker people(e,coverage());D::FrameWitness person;{auto scope=people.enter(call(e,D::Entry::PeopleExecute));person=*people.current_owned_frame();}assert(person.returned(people));guestmods::NativeOuterReturnIssuer::test(&cpu,people,person,[&](const auto&t){assert(!t.current(&cpu,e));});
 D::Tracker fault(e,coverage());D::FrameWitness failed;try{auto scope=fault.enter(call(e));failed=*fault.current_owned_frame();throw std::runtime_error("native tail");}catch(const std::runtime_error&){}assert(!failed.valid()&&!failed.returned(fault));guestmods::NativeOuterReturnIssuer::test(&cpu,fault,failed,[&](const auto&t){assert(!t.current(&cpu,e));});
 // The opaque owner lifetime prevents stale-pointer use after Tracker teardown.
 auto dying=std::make_unique<D::Tracker>(e,coverage());D::FrameWitness last;{auto scope=dying->enter(call(e));last=*dying->current_owned_frame();}assert(last.returned(*dying));guestmods::NativeOuterReturnIssuer::test(&cpu,*dying,last,[&](const auto&t){assert(t.current(&cpu,e));dying.reset();assert(!last.valid()&&!t.current(&cpu,e)&&!t.claim(&cpu,e));});assert(last.expected()==e);
}
