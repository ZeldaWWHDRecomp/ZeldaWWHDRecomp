// SPDX-License-Identifier: MPL-2.0
#include "runtime.h"
#include <string>
#include <vector>
#include <optional>
#include <utility>
#include <functional>
#include <memory>
#define main request_fixture_contract_unused
#include "story_native_scene_request_test.cpp"
#undef main
#include "mods/story_native_scene_playback.h"
#include <limits>
#include <string>
#include <vector>
#include <optional>
#include <utility>
#include <functional>
#include <memory>
namespace P=story_native_scene_playback;
// Isolated value/protocol issuers only. This test asserts frontend behavior,
// not native ownership; production Context alone verifies natural release.
namespace story_native_wait_runtime {class NativeScenePlaybackIssuer {public:
 static auto activation(std::shared_ptr<const R::DeclaredRequest>d){return std::shared_ptr<const P::Activation>(new P::Activation(std::move(d)));}
 static bool install(P::Backend::Start s){return P::Backend::install(std::move(s));}
 static P::Attempt fallback(){return P::Attempt(P::Attempt::Kind::WholeFallback);}
 static P::Attempt denied(){return P::Attempt(P::Attempt::Kind::Denied);}
 static P::Attempt deferred(){return P::Attempt(P::Attempt::Kind::Deferred);}
 static P::Attempt malformed_deferred(std::shared_ptr<const P::Activation>a){return P::Attempt(P::Attempt::Kind::Deferred,std::unique_ptr<P::Run>(new P::Run(std::move(a),[](Cpu*){return P::Progress{};},[](story_scenes::StopReason){ })));}
 static P::Attempt owned(std::shared_ptr<const P::Activation>a,std::function<P::Progress(Cpu*)>poll,std::function<void(story_scenes::StopReason)>stop){return P::Attempt(P::Attempt::Kind::Owned,std::unique_ptr<P::Run>(new P::Run(std::move(a),std::move(poll),std::move(stop))));}
};}
namespace story_native_wait_runtime_controller {class Context {public:static auto token(std::shared_ptr<const P::Activation>a){story_native_event_observation::Expected e;e.program=a->declaration()->compiled().program();return story_native_scene_completion::Completion(std::move(e),std::move(a),std::make_shared<const unsigned>(7));}};}
using I=story_native_wait_runtime::NativeScenePlaybackIssuer;using T=story_native_wait_runtime_controller::Context;using S=story_scenes::StopReason;
struct State {unsigned mode=0,starts=0,polls=0,stops=0,rewards=0;std::shared_ptr<const P::Activation>a,other;bool retire=false,natural=true,poll_throws=false,stop_throws=false;std::optional<S>cause;};
int main(){
 std::vector<M::ManifestScene>scenes;std::string why;assert(M::parse_scenes(manifest(),scenes,why));definition=scenes[0];for(unsigned n=0;n<2;++n)actors[n]={n+1,11,1,"sea",{}};
 std::shared_ptr<const R::DeclaredRequest>declaration;guestmods::NativeSceneRequestIssuer::run([&]{declaration=R::resolve("demo","native_intro",why);});assert(declaration);
 Cpu cpu{};P::Player missing;assert(missing.start(&cpu,"demo",definition,1,{})==P::StartResult::WholeFallback&&!missing.active());
 State state;assert(!I::install({}));assert(I::install([&](Cpu*,const std::string&,const M::ManifestScene&){++state.starts;if(state.mode==1)return I::fallback();if(state.mode==2)return I::denied();if(state.mode==3)throw 1;if(state.mode==13)return I::deferred();if(state.mode==14)return I::malformed_deferred(state.a);
  return I::owned(state.a,[&](Cpu*){++state.polls;if(state.poll_throws)throw 1;P::Progress p;p.phase=state.retire?P::Progress::Phase::Retired:state.mode==7?P::Progress::Phase::Quarantined:P::Progress::Phase::Running;p.reason=state.cause;if(state.natural&&(state.retire||state.mode==8))p.natural.emplace(T::token(state.mode==6?state.other:state.a));return p;},[&](S){++state.stops;if(state.stop_throws)throw 1;});}));assert(!I::install([](Cpu*,const std::string&,const M::ManifestScene&){return I::fallback();}));
 for(unsigned mode=0;mode<15;++mode){state={};state.mode=mode;state.a=I::activation(declaration);state.other=I::activation(declaration);P::Player player;
  auto finish=[&](const std::string&m,const auto&rewards){assert(m=="demo"&&!rewards.empty());++state.rewards;if(mode==9)throw 1;return true;};
  auto scene=definition;scene.steps.back().then.push_back({M::StoryAction::Kind::Flag,"reward",1});
  // The activation binds the entire exact declaration, including rewards.
  definition=scene;guestmods::NativeSceneRequestIssuer::run([&]{declaration=R::resolve("demo","native_intro",why);});state.a=I::activation(declaration);state.other=I::activation(declaration);
  auto result=player.start(&cpu,"demo",scene,1,finish);
  if(mode==13){
   assert(result==P::StartResult::Deferred&&!player.active()&&!player.owns(*state.a)&&state.starts==1&&!state.polls&&!state.stops&&!state.rewards);
   player.request_stop(S::Skipped);player.state_loaded(99);assert(!player.active()&&!state.stops&&player.tick(&cpu,99)==P::TickResult::Failed);
   assert(player.start(&cpu,"demo",scene,1,finish)==P::StartResult::Deferred&&!player.active()&&state.starts==2&&!state.rewards);
   state.mode=0;assert(player.start(&cpu,"demo",scene,1,finish)==P::StartResult::Owned&&state.starts==3);
   player.request_stop(S::Aborted);state.retire=true;assert(player.tick(&cpu,1)==P::TickResult::Stopped&&!state.rewards);continue;
  }
  if(mode==1){
   assert(result==P::StartResult::WholeFallback&&!player.active()&&state.starts==1&&!state.polls&&!state.stops&&!state.rewards);
   // A genuine pre-attempt fallback did not claim the Player: a later normal
   // request can succeed, then abort without applying completion rewards.
   state.mode=0;assert(player.start(&cpu,"demo",scene,1,finish)==P::StartResult::Owned&&state.starts==2);
   player.request_stop(S::Aborted);state.retire=true;assert(player.tick(&cpu,1)==P::TickResult::Stopped&&!state.rewards);continue;
  }
  if(mode==2||mode==3||mode==14){
   assert(result==P::StartResult::Denied&&player.active());assert(player.tick(&cpu,1)==P::TickResult::Active&&state.polls==0);
   // Recovery of backend eligibility cannot replay fallback after an attempted
   // start/refusal/exception. No second backend or reward call is permitted.
   const auto attempts=state.starts;state.mode=1;assert(player.start(&cpu,"demo",scene,1,finish)==P::StartResult::Denied&&state.starts==attempts);
   assert(player.tick(&cpu,1)==P::TickResult::Active&&!state.polls&&!state.rewards);continue;
  }
  assert(result==P::StartResult::Owned&&player.owns(*state.a));
  if(mode==4)player.request_stop(S::Skipped);if(mode==5)state.natural=false;if(mode==10)state.poll_throws=true;if(mode==11){state.stop_throws=true;player.request_stop(S::Aborted);}
  if(mode==7){assert(player.tick(&cpu,1)==P::TickResult::Active&&player.active());assert(player.start(&cpu,"demo",scene,1,finish)==P::StartResult::Denied);continue;}
  if(mode==8){assert(player.tick(&cpu,1)==P::TickResult::Active&&player.active()&&!state.rewards);const auto polled=state.polls;player.tick(&cpu,1);assert(state.polls==polled);continue;}
  if(mode==10||mode==11){player.tick(&cpu,1);const auto polled=state.polls;player.tick(&cpu,1);assert(player.active()&&state.polls==polled&&!state.rewards);continue;}
  state.retire=true;const auto tick=player.tick(&cpu,mode==12?2:1);assert(!player.active());
  if(mode==4||mode==12)assert(tick==P::TickResult::Stopped&&state.stops==1&&!state.rewards);
  else if(mode==5||mode==6)assert(tick==P::TickResult::Failed&&!state.rewards);
  else if(mode==9)assert(tick==P::TickResult::Failed&&state.rewards==1);
  else assert(tick==P::TickResult::Finished&&state.rewards==1);
  const auto count=state.rewards;player.tick(&cpu,1);assert(state.rewards==count);
 }
 // Same textual room after a load is still a new host generation. Escalation
 // to StateLoaded clears any weaker interruption/skip cause and never rewards.
 state={};state.a=I::activation(declaration);P::Player loaded;
 assert(loaded.start(&cpu,"demo",definition,41,[&](const auto&,const auto&){++state.rewards;return true;})==P::StartResult::Owned);
 loaded.request_stop(S::Skipped);assert(state.stops==1);
 loaded.request_stop(S::Interrupted);assert(state.stops==2);
 loaded.request_stop(S::Aborted);assert(state.stops==2&&loaded.stop_reason()==S::Interrupted);
 assert(loaded.tick(&cpu,42)==P::TickResult::Active&&state.stops==3&&loaded.stop_reason()==S::StateLoaded);
 loaded.request_stop(S::Interrupted);assert(state.stops==3);
 state.retire=true;assert(loaded.tick(&cpu,42)==P::TickResult::Stopped&&!loaded.active()&&!state.rewards);
 // An actual backend interruption also stops a same-generation owner. The
 // authentic natural token arriving at retirement cannot override that cause.
 state={};state.a=I::activation(declaration);P::Player interrupted;
 assert(interrupted.start(&cpu,"demo",definition,42,[&](const auto&,const auto&){++state.rewards;return true;})==P::StartResult::Owned);
 state.cause=S::Interrupted;assert(interrupted.tick(&cpu,42)==P::TickResult::Active&&state.stops==1);
 state.retire=true;assert(interrupted.tick(&cpu,42)==P::TickResult::Stopped&&!state.rewards);
 // Installing a backend never bypasses whole-scene coverage. Unsupported
 // families and graphs must return BEFORE any backend admission/start attempt.
 using K=M::SceneStep::Kind;
 for(const auto kind:{K::Say,K::Move,K::Sound,K::Fade,K::CameraPath,K::CameraFollow,K::Animation,K::Walk,K::Choice,K::Music,K::Particle}){
  state={};P::Player unsupported;auto scene=definition;scene.steps[0].kind=kind;
  assert(unsupported.start(&cpu,"demo",scene,1,{})==P::StartResult::WholeFallback&&!unsupported.active()&&!state.starts&&!state.polls&&!state.stops&&!state.rewards);
 }
 for(unsigned fault=0;fault<11;++fault){
  state={};P::Player unsupported;auto scene=definition;
  if(fault==0)scene.native.reset();
  if(fault==1)scene.steps[0].then.push_back({M::StoryAction::Kind::Flag,"mid-scene",1});
  if(fault==2){scene.steps[0].kind=K::Face;scene.steps[0].actor="not-a-native-role";}
  if(fault==3){scene.steps[0].kind=K::Face;scene.steps[0].actor=scene.native->actors[0].id;scene.steps[0].target_actor="link";}
  if(fault==4){scene.steps[0].kind=K::Camera;scene.steps[0].target_actor="link";}
  if(fault==5)scene.steps[0].successors={0,1};
  if(fault==6)scene.steps[0].next="wrong-terminal";
  if(fault==7)scene.steps.back().kind=K::Wait;
  if(fault==8)scene.steps[0].frames=0;
  if(fault==9){scene.steps[0].kind=K::Camera;scene.steps[0].eye.x=std::numeric_limits<float>::quiet_NaN();}
  if(fault==10){scene.steps[0].kind=K::Face;scene.steps[0].actor=scene.native->actors[0].id;scene.steps[0].position.x=350001;}
  assert(unsupported.start(&cpu,"demo",scene,1,{})==P::StartResult::WholeFallback&&!unsupported.active()&&!state.starts&&!state.polls&&!state.stops&&!state.rewards);
 }

 static_assert(story_native_scene_manifest::public_selection_enabled());
}
