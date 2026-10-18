// SPDX-License-Identifier: MPL-2.0
#include "runtime.h"
#include "mods/story_native_scene_activation.h"
#include <string>
#include <cstdint>
#include <span>
#include <utility>
#include <cstring>
#include <memory>
#define STORY_NATIVE_WAIT_READINESS_FIXTURE_ONLY 1
#include "story_native_wait_readiness_test.cpp"
#include <string>
#include <cstdint>
#include <span>
#include <utility>
#include <cstring>
#include <memory>
namespace guestmods {class NativeSceneRequestIssuer {public:template<class F>static void run(F f){story_native_scene_request::StartupWindow window;f();}};}
// Isolated activation issuer tests the Readiness source handshake. Actual
// StoryScenes native_owner routing is covered by the queue contract.
namespace story_native_wait_runtime {class NativeScenePlaybackIssuer {public:static auto activation(std::shared_ptr<const story_native_scene_request::DeclaredRequest>d){return std::shared_ptr<const story_native_scene_playback::Activation>(new story_native_scene_playback::Activation(std::move(d)));}};}
static void native_definition(){
 story_native_scene_manifest::Definition d;d.stage="sea";d.room=0;d.actors[0].id="first";d.actors[1].id="second";
 d.camera.start_eye={0,10,20};d.camera.end_eye={10,10,20};d.camera.start_target={0,0,0};d.camera.end_target={0,0,0};
 d.camera.hold_ticks=30;d.camera.move_ticks=30;d.camera.fovy=45;d.wait_ticks=30;d.timekeeper_ticks=300;d.watchdog_ticks=600;fixture_scene.native=d;
 // Declared scenes retain their real fallback graph, not the private WAIT fixture.
 fixture_scene.steps[0].then.push_back({story_notebook::StoryAction::Kind::Flag,"fallback",1});
}
int main(){
 (void)covered();(void)request();
 std::shared_ptr<const story_native_scene_request::DeclaredRequest> declared;
 Readiness f;native_definition();std::string why;
 Cpu cpu{};cpu.r[1]=0x600300C0;cpu.r[10]=0x1234;cpu.lr=0x5678;cpu.f[2].ps0=42;const Cpu before=cpu;unsigned restored=0;
 auto sources=f.source();const auto frame_restore=sources.restore_frame;
 using Activation=story_native_scene_playback::Activation;
 using Issuer=story_native_wait_runtime::NativeScenePlaybackIssuer;
 std::shared_ptr<const Activation>owner,swapped_owner;unsigned owner_samples=0;bool swap_owner=false;
 const auto boundary_source=sources.boundary;
 sources.boundary=[&](const Cpu*c,W::Boundary&b){if(!boundary_source(c,b))return false;b.owned_scene=swap_owner&&owner_samples++?swapped_owner:owner;return true;};
 sources.restore_frame=[&](uint32_t at,std::span<const uint8_t>bytes){assert(!std::memcmp(&cpu,&before,sizeof cpu));++restored;return frame_restore(at,bytes);};
 std::unique_ptr<W::Controller> controller;
 guestmods::NativeSceneRequestIssuer::run([&]{
  declared=story_native_scene_request::resolve("test","private-wait",why);assert(declared);
  W::Request r;r.declared=declared;r.mod="spoof";r.scene="other";r.fixture_program="spoof";r.residents={"wrong","wrong"};r.native_wait_timer=0;
  controller=std::make_unique<W::Controller>(r,sources);assert(controller->startup()==W::Status::Allocated&&allocation_calls==1);
  assert(controller->definition_hash()==declared->definition_hash());
 });
 assert(!declared->startup_current());
 f.put(0x1004487C+4,1);f.half(0x10049350+2*0x40+0x18,1);f.put(0x1004487C+0x84+4,11);
 assert(controller->inspect(&cpu)==W::Status::Ready&&controller->candidate()->program==declared->compiled().program()&&restored==1);
 // Exact declared owner allows post-release Readiness while the frontend
 // still holds that Activation. A status flag alone never exempts activity.
 auto exact=Issuer::activation(declared);owner=exact;active_scene=true;
 assert(controller->inspect(&cpu)==W::Status::Ready&&restored==2);
 queries=0;owner.reset();assert(controller->inspect(&cpu)==W::Status::NotReady&&queries==0&&!controller->candidate());
 std::shared_ptr<const story_native_scene_request::DeclaredRequest>foreign;
 guestmods::NativeSceneRequestIssuer::run([&]{foreign=story_native_scene_request::resolve("test","private-wait",why);});assert(foreign&&foreign!=declared);
 owner=Issuer::activation(foreign);assert(controller->inspect(&cpu)==W::Status::NotReady&&queries==0);
 owner=exact;fixture_scene.steps[0].then[0].value=9;
 assert(controller->inspect(&cpu)==W::Status::NotReady&&queries==0);fixture_scene.steps[0].then[0].value=1;
 // Even a second Activation for the SAME immutable declaration cannot replace
 // the owner between the two source samples. Native searches remain read-only.
 swapped_owner=Issuer::activation(declared);swap_owner=true;owner_samples=0;
 const auto before_swap_restore=restored;
 assert(controller->inspect(&cpu)==W::Status::NotReady&&!controller->candidate()&&queries==2&&restored==before_swap_restore+1);
 assert(!std::memcmp(&cpu,&before,sizeof cpu));swap_owner=false;owner_samples=0;
 assert(controller->inspect(&cpu)==W::Status::Ready);active_scene=false;owner.reset();
 queries=0;
 // Source boundary is mandatory before definition freshness or native search.
 f.idle=false;assert(controller->inspect(&cpu)==W::Status::NotReady&&queries==0);f.idle=true;
 fixture_scene.steps[0].then[0].value=2;assert(controller->inspect(&cpu)==W::Status::NotReady&&queries==0);
 const auto before_allocations=allocation_calls;W::Request late;late.declared=declared;W::Controller denied(late,f.source());assert(denied.startup()==W::Status::Definitions&&allocation_calls==before_allocations);
 fixture_scene.steps[0].then[0].value=1;query_throw=true;const auto before_throw_restore=restored;
 assert(controller->inspect(&cpu)==W::Status::Quarantined&&restored==before_throw_restore+1&&!std::memcmp(&cpu,&before,sizeof cpu));query_throw=false;
 Readiness uncommitted;native_definition();uncommitted.committed=false;
 guestmods::NativeSceneRequestIssuer::run([&]{auto d=story_native_scene_request::resolve("test","private-wait",why);assert(d);W::Request r;r.declared=d;W::Controller no(r,uncommitted.source());assert(no.startup()==W::Status::Definitions&&allocation_calls==0);});
}
