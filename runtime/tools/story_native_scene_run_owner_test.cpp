// SPDX-License-Identifier: MPL-2.0
#include "runtime.h"
#include <string>
#include <vector>
#include <utility>
#include <memory>
#define main request_fixture_contract_unused
#include "story_native_scene_request_test.cpp"
#undef main
#include "mods/story_native_scene_run_owner.h"
#include <thread>
#include <future>
#include <string>
#include <vector>
#include <utility>
#include <memory>
namespace P=story_native_scene_playback;
// Isolated host-protocol issuers only; no actual native resume is exercised.
namespace story_native_wait_runtime {class NativeScenePlaybackIssuer {public:
 static auto activation(std::shared_ptr<const R::DeclaredRequest>d){return std::shared_ptr<const P::Activation>(new P::Activation(std::move(d)));}
 static bool install(P::Backend::Start start){return P::Backend::install(std::move(start));}
 static P::Attempt owned(std::shared_ptr<P::RunOwnerCell>cell){return P::Attempt(P::Attempt::Kind::Owned,std::unique_ptr<P::Run>(new P::Run(cell->activation(),[cell](Cpu*c){return cell->poll(c);},[cell](auto reason){cell->queue_stop(reason);},cell)));}
 static auto cell(std::shared_ptr<const P::Activation>a,std::shared_ptr<const void>life,P::RunOwnerCell::Endpoint e){return std::unique_ptr<P::RunOwnerCell>(new P::RunOwnerCell(std::move(a),std::move(life),std::move(e)));}
};}
namespace story_native_wait_runtime_controller {class Context {public:
 static auto natural(std::shared_ptr<const P::Activation>a){story_native_event_observation::Expected e;e.program=a->declaration()->compiled().program();return story_native_scene_completion::Completion(std::move(e),std::move(a),std::make_shared<const unsigned>(81));}
 static auto certificate(P::RunOwnerCell&c,std::shared_ptr<const P::Activation>a,std::shared_ptr<const void>life,P::RunOwnerCell::Endpoint e){return P::RunOwnerCell::ResumeCertificate(c,std::move(a),std::move(life),std::move(e));}
 static bool commit(P::RunOwnerCell&c,P::RunOwnerCell::ResumeCertificate&&r){return c.commit(std::move(r));}
 static auto prepare(P::RunOwnerCell&c,P::RunOwnerCell::ResumeCertificate&&r){return c.prepare_resume(std::move(r));}
 static void finish(P::RunOwnerCell::PreparedResume&r)noexcept{r.commit_unchecked();}
};}
using I=story_native_wait_runtime::NativeScenePlaybackIssuer;using C=story_native_wait_runtime_controller::Context;
int main(){
 std::vector<M::ManifestScene>scenes;std::string why;assert(M::parse_scenes(manifest(),scenes,why));definition=scenes[0];for(unsigned n=0;n<2;++n)actors[n]={n+1,11,1,"sea",{}};
 std::shared_ptr<const R::DeclaredRequest>d;guestmods::NativeSceneRequestIssuer::run([&]{d=R::resolve("demo","native_intro",why);});assert(d);
 auto life=std::make_shared<const unsigned>(17);auto a=I::activation(d),other=I::activation(d);Cpu cpu{};unsigned old_polls=0,new_polls=0,stops=0;
 auto endpoint=[&](bool old){return P::RunOwnerCell::Endpoint{[&,old](Cpu*){old?++old_polls:++new_polls;return P::Progress{};},[&](auto){++stops;}};};
 auto cell=I::cell(a,life,endpoint(true)),foreign=I::cell(a,life,endpoint(true));
 cell->poll(&cpu);cell->queue_stop(story_scenes::StopReason::Aborted);assert(old_polls==1&&stops==1);
 auto wrong_cell=C::certificate(*cell,a,life,endpoint(false));assert(!C::commit(*foreign,std::move(wrong_cell)));assert(!C::commit(*cell,std::move(wrong_cell)));
 auto wrong_activation=C::certificate(*cell,other,life,endpoint(false));assert(!C::commit(*cell,std::move(wrong_activation)));
 auto stale=C::certificate(*cell,a,life,endpoint(false));auto valid=C::certificate(*cell,a,life,endpoint(false));assert(C::commit(*cell,std::move(valid)));assert(!C::commit(*cell,std::move(valid)));assert(!C::commit(*cell,std::move(stale)));
 cell->poll(&cpu);assert(old_polls==1&&new_polls==1&&cell->activation()==a);
 auto wrong_life=C::certificate(*cell,a,std::make_shared<const unsigned>(17),endpoint(false));assert(!C::commit(*cell,std::move(wrong_life)));
 auto empty=C::certificate(*cell,a,life,{});assert(!C::commit(*cell,std::move(empty)));
 auto moved=C::certificate(*cell,a,life,endpoint(true));auto destination=std::move(moved);assert(!C::commit(*cell,std::move(moved)));assert(C::commit(*cell,std::move(destination)));
 // A certificate may not replace callbacks while one endpoint is on-stack.
 bool denied=false;auto nested=C::certificate(*cell,a,life,endpoint(false));
 auto entering=C::certificate(*cell,a,life,{[&](Cpu*){denied=!C::commit(*cell,std::move(nested));return P::Progress{};},[&](auto){}});
 assert(C::commit(*cell,std::move(entering)));cell->poll(&cpu);assert(denied);
 auto thread_cert=C::certificate(*cell,a,life,endpoint(false));bool rejected=false;
 std::thread thread([&]{rejected=!C::commit(*cell,std::move(thread_cert));try{cell->poll(&cpu);assert(false);}catch(const std::logic_error&){} });thread.join();assert(rejected);
 // Wrong-thread attempts consume their certificate; no late replay on owner.
 assert(!C::commit(*cell,std::move(thread_cert)));
 auto dying=I::cell(a,life,endpoint(true));auto dead=C::certificate(*dying,a,life,endpoint(false));dying.reset();assert(!C::commit(*cell,std::move(dead)));
 // Exceptions restore the callback busy guard so a later authentic replacement
 // remains possible; no native resources are inferred from these callbacks.
 auto throwing=C::certificate(*cell,a,life,{[](Cpu*)->P::Progress{throw 1;},[](auto){throw 1;}});assert(C::commit(*cell,std::move(throwing)));
 try{cell->poll(&cpu);assert(false);}catch(int){}try{cell->queue_stop(story_scenes::StopReason::Aborted);assert(false);}catch(int){}
 auto after_throw=C::certificate(*cell,a,life,endpoint(false));assert(C::commit(*cell,std::move(after_throw)));cell->poll(&cpu);assert(new_polls==2);
 // Preparation retains the callback gate even after its no-fail host swap.
 // Abandoning a reservation leaves the old endpoint and version unchanged.
 unsigned reserved_polls=0;
 auto abandoned=C::certificate(*cell,a,life,{[&](Cpu*){++reserved_polls;return P::Progress{};},[](auto){}});
 {auto reservation=C::prepare(*cell,std::move(abandoned));assert(reservation);try{cell->poll(&cpu);assert(false);}catch(const std::logic_error&){} }
 cell->poll(&cpu);assert(reserved_polls==0);
 auto reserved=C::certificate(*cell,a,life,{[&](Cpu*){++reserved_polls;return P::Progress{};},[](auto){}});
 {auto reservation=C::prepare(*cell,std::move(reserved));assert(reservation);auto moved_reservation=std::move(*reservation);C::finish(moved_reservation);try{cell->poll(&cpu);assert(false);}catch(const std::logic_error&){} }
 cell->poll(&cpu);assert(reserved_polls==1);
 // A real foreign host stop callback excludes commit without blocking the
 // owner thread. An overlapping stop remains pending across a later swap.
 std::promise<void>entered,release;auto released=release.get_future().share();unsigned foreign_stops=0,replacement_stops=0;
 auto blocking=C::certificate(*cell,a,life,{[](Cpu*){return P::Progress{};},[&](auto){++foreign_stops;entered.set_value();released.wait();}});
 assert(C::commit(*cell,std::move(blocking)));
 std::thread stopping([&]{cell->queue_stop(story_scenes::StopReason::Aborted);});entered.get_future().wait();
 auto while_stopping=C::certificate(*cell,a,life,endpoint(false));assert(!C::commit(*cell,std::move(while_stopping)));
 cell->queue_stop(story_scenes::StopReason::StateLoaded);release.set_value();stopping.join();assert(foreign_stops==1);
 auto retained_stop=C::certificate(*cell,a,life,{[](Cpu*){return P::Progress{};},[&](auto reason){assert(reason==story_scenes::StopReason::StateLoaded);++replacement_stops;}});
 assert(C::commit(*cell,std::move(retained_stop)));cell->poll(&cpu);assert(replacement_stops==1);

 // Full Player/cell protocol: only the private final-commit notice reconciles
 // a new generation. Copied status, wrong lineage, and later loads cannot.
 std::shared_ptr<P::RunOwnerCell>current_owner;
 assert(I::install([&](Cpu*,const auto&,const auto&){return I::owned(current_owner);}));
 definition.steps.back().then.push_back({M::StoryAction::Kind::Flag,"resume_reward",1});
 guestmods::NativeSceneRequestIssuer::run([&]{d=R::resolve("demo","native_intro",why);});assert(d);
 for(unsigned mode=0;mode<9;++mode){
  auto activation=I::activation(d);auto run_life=std::make_shared<const unsigned>(90+mode);
  unsigned old_stops=0,new_stops=0,rewards=0;bool running=false;
  auto progress=[&]{P::Progress p;p.phase=running?P::Progress::Phase::Running:P::Progress::Phase::Retired;if(!running)p.natural.emplace(C::natural(activation));return p;};
  current_owner=I::cell(activation,run_life,{[&](Cpu*){return progress();},[&](auto){++old_stops;}});
  P::Player player;assert(player.start(&cpu,"demo",definition,100,[&](const auto&,const auto&){++rewards;return true;})==P::StartResult::Owned);
  if(mode==7){auto early=C::certificate(*current_owner,activation,run_life,{[&](Cpu*){return progress();},[&](auto){++new_stops;}});assert(C::commit(*current_owner,std::move(early)));}
  player.state_loaded(101);assert(mode==7?new_stops==1:old_stops==1);
  if(mode==3)player.request_stop(story_scenes::StopReason::Skipped);
  if(mode==4)player.request_stop(story_scenes::StopReason::Aborted);
  if(mode==6)player.request_stop(story_scenes::StopReason::Interrupted);
  if(mode&&mode!=7){auto certificate=C::certificate(*current_owner,activation,mode==2?std::make_shared<const unsigned>(90+mode):run_life,{[&](Cpu*){return progress();},[&](auto){++new_stops;}});assert(C::commit(*current_owner,std::move(certificate))==(mode!=2));}
  if(mode==8)player.state_loaded(102);
  if(mode==5){running=true;assert(player.tick(&cpu,101)==P::TickResult::Active&&!player.stop_reason());player.state_loaded(102);running=false;assert(player.tick(&cpu,102)==P::TickResult::Stopped&&!rewards&&new_stops==1);}
  else{const auto result=player.tick(&cpu,mode==8?102:101);if(mode==1)assert(result==P::TickResult::Finished&&rewards==1&&!new_stops);else assert(result==P::TickResult::Stopped&&!rewards);if(mode==3||mode==4||mode==6)assert(new_stops==1);}
  assert(!player.active());current_owner.reset();
 }

}
