// SPDX-License-Identifier: MPL-2.0
#define main legacy_scene_fixture_contract_unused
#include "story_scenes_native_test.cpp"
#include <cstdlib>
#include <string>
#include <array>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>
#include <utility>
#include <functional>
#include <cstring>
#include <memory>
#undef main
#include "mods/story_native_scene_playback.h"
#include "mods/story_native_scene_run_owner.h"
#include "mods/story_native_scene_checkpoint.h"
#include "mods/story_native_scene_retire.h"
#include "mods/story_native_restore_resume.h"
#include <map>
#include <cstdlib>
#include <string>
#include <array>
#include <vector>
#include <optional>
#include <cstdint>
#include <utility>
#include <functional>
#include <cstring>
#include <memory>
namespace P=story_native_scene_playback;namespace R=story_native_scene_request;using S=story_scenes::StopReason;
// Actual StoryScenes FIFO/control adapter with a synthetic backend protocol.
// The private token issuers below prove routing/value behavior only; genuine
// native ownership/natural release is checked by the separate Context tests.
namespace story_actors {bool resident_event_declaration(const std::string&mod,const std::string&id,ResidentEventDeclaration&out){if(mod!="mod")return false;unsigned n=id=="first"?0:id=="second"?1:2;if(n==2)return false;out={n+1,0,1,"sea",{}};return true;}}
namespace guestmods {class NativeSceneRequestIssuer {public:template<class F>static void run(F f){R::StartupWindow window;f();}};}
namespace story_native_wait_runtime {class NativeScenePlaybackIssuer {public:
 static auto activation(std::shared_ptr<const R::DeclaredRequest>d){return std::shared_ptr<const P::Activation>(new P::Activation(std::move(d)));}
 static bool install(P::Backend::Start s){return P::Backend::install(std::move(s));}
 static P::Attempt fallback(){return P::Attempt(P::Attempt::Kind::WholeFallback);}
 static P::Attempt denied(){return P::Attempt(P::Attempt::Kind::Denied);}
 static P::Attempt deferred(){return P::Attempt(P::Attempt::Kind::Deferred);}
 static auto cell(std::shared_ptr<const P::Activation>a,std::shared_ptr<const void>life,P::RunOwnerCell::Endpoint e){return std::shared_ptr<P::RunOwnerCell>(new P::RunOwnerCell(std::move(a),std::move(life),std::move(e)));}
 static P::Attempt owned(std::shared_ptr<const P::Activation>a,std::function<P::Progress(Cpu*)>poll,std::function<void(S)>stop,std::shared_ptr<P::RunOwnerCell>cell={}){return P::Attempt(P::Attempt::Kind::Owned,std::unique_ptr<P::Run>(new P::Run(std::move(a),std::move(poll),std::move(stop),std::move(cell))));}
};}
namespace story_native_wait_runtime_controller {class Context {public:
 // Isolated host-protocol proof issuer only. These values do NOT establish
 // native suspension, an actual absent front, or genuine historical resume.
 static auto revoked(std::shared_ptr<const void>life,std::shared_ptr<P::RunOwnerCell>cell={}){return story_native_scene_checkpoint::CurrentRunRevocation(reinterpret_cast<Context*>(uintptr_t(1)),std::move(life),88,std::move(cell));}
 static void open_frontend(story_native_scene_checkpoint::PreparedRestore&prepared)noexcept{prepared.commit_unchecked();}
 static auto revoked_for_retire(std::shared_ptr<const void>life,std::shared_ptr<P::RunOwnerCell>cell){
  auto*context=reinterpret_cast<Context*>(uintptr_t(1));
  auto proof=std::shared_ptr<const story_native_restore_resume::CurrentOwnerRevocation>(new story_native_restore_resume::CurrentOwnerRevocation(context,life,{},88));
  return story_native_scene_checkpoint::CurrentRunRevocation(context,std::move(life),88,std::move(cell),std::move(proof));
 }
 static void retire_frontend(story_native_scene_checkpoint::PreparedRetire&prepared)noexcept{assert(prepared.current());prepared.commit_unchecked();}

 static bool resume(P::RunOwnerCell&cell,std::shared_ptr<const P::Activation>a,std::shared_ptr<const void>life,P::RunOwnerCell::Endpoint endpoint){P::RunOwnerCell::ResumeCertificate certificate(cell,std::move(a),std::move(life),std::move(endpoint));return cell.commit(std::move(certificate));}
 static auto token(std::shared_ptr<const P::Activation>a){story_native_event_observation::Expected e;e.program=a->declaration()->compiled().program();return story_native_scene_completion::Completion(std::move(e),std::move(a),std::make_shared<const unsigned>(8));}};}
using I=story_native_wait_runtime::NativeScenePlaybackIssuer;using T=story_native_wait_runtime_controller::Context;
static story_native_scene_manifest::Definition recipe(){story_native_scene_manifest::Definition d;d.stage="sea";d.actors[0].id="first";d.actors[1].id="second";d.camera.start_eye={0,10,20};d.camera.end_eye={10,10,20};d.camera.hold_ticks=d.camera.move_ticks=30;d.camera.fovy=45;d.wait_ticks=30;d.timekeeper_ticks=300;d.watchdog_ticks=600;return d;}
int main(int argc,char**argv){
 assert(map(kArena,0x50000)==mem::ptr(kArena));assert(map(0x101F0000,0x10000)==mem::ptr(0x101F0000));clear_guest();Cpu cpu{};
 story_scenes::set_game_calls(game_call);story_scenes::set_scratch_memory(kScratch);story_scenes::set_callbacks([](Cpu*,const std::string&,const std::string&){return kActor;},[](const std::string&,const std::vector<story_notebook::StoryAction>&actions){++finished;for(const auto&a:actions)reward_flags.push_back(a.id);return true;});
 std::vector<story_notebook::ManifestScene>scenes;for(const char*id:{"a","b","c"}){story_notebook::ManifestScene s;s.id=id;s.native=recipe();story_notebook::SceneStep step,end;step.kind=story_notebook::SceneStep::Kind::Wait;step.frames=1;end.kind=story_notebook::SceneStep::Kind::End;end.then.push_back({story_notebook::StoryAction::Kind::Flag,id,1});if(s.id=="b"){step.kind=story_notebook::SceneStep::Kind::Sound;step.sound="SE_MENU_CANCEL";}s.steps={step,end};scenes.push_back(s);}std::string why;assert(story_scenes::declare("mod",scenes,why));
 story_scenes::install([](Cpu*,uint32_t){},[](uint32_t){return std::string();},[](uint32_t address,void(*hook)(Cpu*)){assert(address==0x024FF8A0);trim_hook=hook;});
 std::map<std::string,std::shared_ptr<const R::DeclaredRequest>>declarations;std::vector<std::string>starts;unsigned polls=0,stops=0;bool released=false,stopped=false,force_fallback=false,deny=false,abort_during_start=false,defer=false,throw_start=false;std::optional<S>cause;std::shared_ptr<const P::Activation>activation;bool cell_backend=false;std::shared_ptr<P::RunOwnerCell>cell;auto run_life=std::make_shared<const unsigned>(44);P::RunOwnerCell::Endpoint endpoint;
 guestmods::NativeSceneRequestIssuer::run([&]{for(const auto&s:scenes){declarations[s.id]=R::resolve("mod",s.id,why);assert(declarations[s.id]);}
 assert(I::install([&](Cpu*entered,const std::string&mod,const story_notebook::ManifestScene&s){assert(mod=="mod"&&!story_scenes::active()&&!ld8(kPlay+0x5292));starts.push_back(s.id);assert(!story_scenes::request(mod,s.id));story_scenes::tick(entered);if(force_fallback)return I::fallback();if(deny)return I::denied();if(throw_start)throw 1;if(defer)return I::deferred();activation=I::activation(declarations.at(s.id));released=stopped=false;cause.reset();if(abort_during_start)story_scenes::abort();auto owner=activation;
  endpoint={[&,owner](Cpu*){++polls;assert(story_scenes::native_owner(*owner)&&!story_scenes::fallback_active());P::Progress p;p.phase=released?P::Progress::Phase::Retired:P::Progress::Phase::Running;p.reason=cause;if(released)p.natural.emplace(T::token(owner));return p;},[&](S){++stops;stopped=true;}};
  if(cell_backend){cell=I::cell(owner,run_life,endpoint);return I::owned(owner,[current=cell](Cpu*c){return current->poll(c);},[current=cell](S reason){current->queue_stop(reason);},cell);}
  return I::owned(owner,endpoint.poll,endpoint.queue_stop);}));});
 assert(story_scenes::request("mod","a")&&story_scenes::request("mod","b")&&story_scenes::request("mod","c"));test_tick(&cpu);assert(story_scenes::active()&&!story_scenes::fallback_active()&&!story_scenes::request("mod","a"));const auto native_before=native_calls;test_tick(&cpu);assert(native_calls==native_before&&!ld8(kPlay+0x5292)&&end_calls==0&&reward_flags.empty());
 released=true;test_tick(&cpu);assert(!story_scenes::active()&&(reward_flags==std::vector<std::string>{"a"}));test_tick(&cpu);assert(story_scenes::fallback_active()&&ld8(kPlay+0x5292)==3);for(unsigned n=0;n<6&&story_scenes::active();++n)test_tick(&cpu);assert(!story_scenes::active()&&end_calls==1&&sounds==1&&(reward_flags==std::vector<std::string>{"a","b"}));test_tick(&cpu);assert((starts==std::vector<std::string>{"a","c"})&&story_scenes::active());
 // Abort retains cleanup ownership and removes queued entries; even an authentic
 // synthetic natural token arriving afterward cannot apply C's END rewards.
 assert(story_scenes::request("mod","a"));story_scenes::abort();assert(stopped&&story_scenes::active());test_tick(&cpu);assert(story_scenes::active());released=true;test_tick(&cpu);assert(!story_scenes::active()&&reward_flags.size()==2);test_tick(&cpu);assert(starts.size()==2);
 // An abort during the backend's synchronous start is retained even though
 // native active() must remain false until that start returns.
 abort_during_start=true;assert(story_scenes::request("mod","a"));test_tick(&cpu);abort_during_start=false;assert(stopped&&story_scenes::active());released=true;test_tick(&cpu);assert(!story_scenes::active()&&reward_flags.size()==2);
 // Skip input is swallowed through the genuine retirement boundary.
 assert(story_scenes::request("mod","a"));test_tick(&cpu);pad_buttons=input::kPlus;test_tick(&cpu);assert(stopped&&story_scenes::active());input::PadState pad{};pad.buttons=input::kPlus;pad.lx=1;story_scenes::mute(pad);assert(!(pad.buttons&input::kPlus)&&pad.lx==0);released=true;test_tick(&cpu);assert(!story_scenes::active()&&reward_flags.size()==2);pad_buttons=0;test_tick(&cpu);
 // Load/interruption clears the old FIFO and never falls back after ownership.
 for(unsigned mode=0;mode<2;++mode){assert(story_scenes::request("mod","c"));test_tick(&cpu);assert(story_scenes::request("mod","a"));const auto count=starts.size();if(mode==0)story_scenes::state_loaded();else cause=S::Interrupted;test_tick(&cpu);assert(stopped&&story_scenes::active());released=true;test_tick(&cpu);assert(!story_scenes::active()&&reward_flags.size()==2);test_tick(&cpu);assert(starts.size()==count);}
 // Explicit fallback is allowed only before the synthetic backend touched any
 // guest state. It executes the complete ordinary timeline and its END once.
 force_fallback=true;assert(story_scenes::request("mod","a"));test_tick(&cpu);assert(story_scenes::fallback_active());for(unsigned n=0;n<6&&story_scenes::active();++n)test_tick(&cpu);assert(!story_scenes::active()&&reward_flags.size()==3&&reward_flags.back()=="a");force_fallback=false;
 // Actual queue/Player consumes only the private final-commit notice. The
 // same-room load drops old FIFO, but newly requested work after resume stays.
 cell_backend=true;assert(story_scenes::request("mod","c"));test_tick(&cpu);assert(story_scenes::request("mod","a"));
 auto checkpoint=story_scenes::capture_native_checkpoint(*activation,cell);assert(checkpoint&&checkpoint->definition_current()&&checkpoint->cell()==cell&&checkpoint->activation()==activation&&checkpoint->original_run_life()==run_life&&checkpoint->definition().id=="c");
 auto foreign_activation=I::activation(declarations.at("c"));assert(!story_scenes::capture_native_checkpoint(*foreign_activation,cell));
 auto foreign_cell=I::cell(activation,run_life,endpoint);assert(!story_scenes::capture_native_checkpoint(*activation,foreign_cell));
 std::weak_ptr<P::RunOwnerCell>historical_cell=cell;const auto historical_endpoint=endpoint;
 story_scenes::state_loaded();assert(stopped&&!story_scenes::capture_native_checkpoint(*activation,cell));
 auto wrong_revocation=T::revoked(run_life,foreign_cell);assert(!story_scenes::prepare_native_restore(*checkpoint,std::move(wrong_revocation)));
 assert(T::resume(*cell,activation,run_life,endpoint));stopped=false;
 test_tick(&cpu);assert(story_scenes::active()&&!stopped&&reward_flags.size()==3);
 assert(story_scenes::request("mod","a"));test_tick(&cpu);released=true;test_tick(&cpu);assert(!story_scenes::active()&&reward_flags.size()==4&&reward_flags.back()=="c");
 assert(!story_scenes::capture_native_checkpoint(*activation,cell));
 const auto before_postload=starts.size();test_tick(&cpu);assert(story_scenes::active()&&starts.size()==before_postload+1&&starts.back()=="a");
 story_scenes::abort();released=true;test_tick(&cpu);assert(!story_scenes::active()&&reward_flags.size()==4);cell_backend=false;cell.reset();assert(!historical_cell.expired()&&checkpoint->definition_current()&&checkpoint->definition().id=="c");
 // Retired frontend reattachment protocol, including repeated same immutable
 // checkpoint. Native proof is deliberately isolated in the fixture issuer;
 // production Context may issue only after actual current-owner reconciliation.
 for(unsigned repeat=0;repeat<3;++repeat){
  story_scenes::state_loaded();activation=checkpoint->activation();released=stopped=false;cause.reset();
  auto revocation=T::revoked(run_life);auto prepared=story_scenes::prepare_native_restore(*checkpoint,std::move(revocation));assert(prepared);
  assert(!story_scenes::prepare_native_restore(*checkpoint,std::move(revocation)));assert(!story_scenes::request("mod","a"));
  const auto before_reserved=polls;test_tick(&cpu);assert(polls==before_reserved);
  if(repeat==2)story_scenes::abort();
  auto restored_cell=checkpoint->cell();assert(T::resume(*restored_cell,activation,checkpoint->original_run_life(),historical_endpoint));
  T::open_frontend(*prepared);assert(story_scenes::active()&&story_scenes::native_owner(*activation));prepared.reset();
  test_tick(&cpu);assert(story_scenes::active()&&(repeat==2?stopped:!stopped));
  const auto rewards_before=reward_flags.size();released=true;test_tick(&cpu);assert(!story_scenes::active()&&reward_flags.size()==rewards_before+(repeat==2?0:1));
 }
 checkpoint.reset();assert(historical_cell.expired());
 // Isolated frontend retirement protocol: actual Context/all-child proof
 // issuance is tested separately; these fixture tokens grant no native entry.
 cell_backend=true;assert(story_scenes::request("mod","a"));test_tick(&cpu);assert(story_scenes::active());
 const auto retire_calls=native_calls;const auto retire_ends=end_calls;const auto retire_rewards=reward_flags.size();
 story_scenes::state_loaded();
 auto wrong_retire=T::revoked_for_retire(run_life,foreign_cell);assert(!story_scenes::prepare_native_retire(std::move(wrong_retire)));
 auto retire_proof=T::revoked_for_retire(run_life,cell);
 std::optional<story_native_scene_checkpoint::PreparedRetire>foreign_retire;
 std::thread([&]{foreign_retire=story_scenes::prepare_native_retire(std::move(retire_proof));}).join();assert(!foreign_retire);
 auto retirement=story_scenes::prepare_native_retire(std::move(retire_proof));assert(retirement);
 assert(!story_scenes::prepare_native_retire(std::move(retire_proof))&&!story_scenes::request("mod","c"));
 const auto retire_polls=polls;test_tick(&cpu);assert(polls==retire_polls&&story_scenes::active());
 T::retire_frontend(*retirement);assert(!story_scenes::active());
 input::PadState free_input{};free_input.lx=1;story_scenes::mute(free_input);assert(free_input.lx==1);
 retirement.reset();assert(native_calls==retire_calls&&end_calls==retire_ends&&reward_flags.size()==retire_rewards);
 test_tick(&cpu);assert(!story_scenes::active());
 assert(story_scenes::request("mod","a"));test_tick(&cpu);assert(story_scenes::active());
 story_scenes::abort();released=true;test_tick(&cpu);assert(!story_scenes::active()&&reward_flags.size()==retire_rewards);
 cell_backend=false;
 // Temporary eligibility retains the exact FIFO front without a Player,
 // input/control lease or native body/callback/reward. Each idle tick performs
 // only the existing Play accessor; no native event/camera mutator runs.
 const auto deferred_rewards=reward_flags.size();const auto deferred_ends=end_calls;
 defer=true;const auto first_attempt=starts.size();const auto reads_before=native_calls;
 const auto deferred_polls=polls,deferred_stops=stops;
 std::array<uint8_t,0x50000>before_defer{};std::memcpy(before_defer.data(),mem::ptr(kArena),before_defer.size());
 assert(story_scenes::request("mod","a")&&story_scenes::request("mod","c"));
 for(unsigned n=0;n<3;++n){test_tick(&cpu);assert(starts.size()==first_attempt+n+1&&starts.back()=="a"&&!story_scenes::active()&&!story_scenes::fallback_active()&&!story_scenes::request("mod","a"));}
 assert(native_calls==reads_before+3&&polls==deferred_polls&&stops==deferred_stops&&end_calls==deferred_ends&&reward_flags.size()==deferred_rewards&&!std::memcmp(before_defer.data(),mem::ptr(kArena),before_defer.size()));
 input::PadState pending_input{};pending_input.lx=1;story_scenes::mute(pending_input);assert(pending_input.lx==1);
 defer=false;test_tick(&cpu);assert(starts.back()=="a"&&story_scenes::active());story_scenes::abort();released=true;test_tick(&cpu);assert(!story_scenes::active()&&reward_flags.size()==deferred_rewards);const auto aborted_attempts=starts.size();test_tick(&cpu);assert(starts.size()==aborted_attempts); // queued C discarded by abort
 // A skip of the FIRST deferred decision drops only that front, never starts
 // its fallback, and leaves the next FIFO item available.
 defer=true;assert(story_scenes::request("mod","a")&&story_scenes::request("mod","c"));test_tick(&cpu,true);assert(!story_scenes::active()&&reward_flags.size()==deferred_rewards);defer=false;test_tick(&cpu);assert(starts.back()=="c"&&story_scenes::active());story_scenes::abort();released=true;test_tick(&cpu);
 // A later skip is processed even while an ordinary retail event is busy;
 // it cannot issue another backend attempt or retain input/control ownership.
 defer=true;assert(story_scenes::request("mod","a"));test_tick(&cpu);const auto skipped_attempts=starts.size();st8(kPlay+0x5292,1);test_tick(&cpu,true);st8(kPlay+0x5292,0);assert(!story_scenes::active()&&starts.size()==skipped_attempts);defer=false;test_tick(&cpu);assert(starts.size()==skipped_attempts&&reward_flags.size()==deferred_rewards);
 // Abort and load drop deferred metadata and duplicate guards. A later request
 // starts at attempt1, rather than inheriting an old near-expired deadline.
 for(unsigned interruption=0;interruption<2;++interruption){defer=true;assert(story_scenes::request("mod","a"));test_tick(&cpu);const auto count=starts.size();if(interruption)story_scenes::state_loaded();else story_scenes::abort();test_tick(&cpu);defer=false;test_tick(&cpu);assert(!story_scenes::active()&&starts.size()==count&&reward_flags.size()==deferred_rewards);}
 defer=true;assert(story_scenes::request("mod","a"));const auto bounded_attempts=starts.size();
 for(unsigned n=1;n<P::kNativeDeferredAttempts;++n){test_tick(&cpu);assert(!story_scenes::active()&&!story_scenes::fallback_active()&&starts.size()==bounded_attempts+n);}
 test_tick(&cpu);assert(story_scenes::fallback_active()&&starts.size()==bounded_attempts+P::kNativeDeferredAttempts);const auto expired_attempts=starts.size();
 for(unsigned n=0;n<6&&story_scenes::active();++n)test_tick(&cpu);assert(!story_scenes::active()&&starts.size()==expired_attempts&&reward_flags.size()==deferred_rewards+1&&reward_flags.back()=="a");defer=false;
 // Denied/exception both latch attempted ownership. A separate --throw
 // invocation tests the exception case without resetting an owned Player.
 const bool throwing=argc>1&&std::string(argv[1])=="--throw";throw_start=throwing;deny=!throwing;
 assert(story_scenes::request("mod","a")&&story_scenes::request("mod","b"));test_tick(&cpu);assert(story_scenes::active()&&!story_scenes::fallback_active());
 throw_start=deny=false;defer=true;const auto failed_attempts=starts.size();const auto failed_calls=native_calls;test_tick(&cpu);
 assert(starts.size()==failed_attempts&&native_calls==failed_calls&&reward_flags.size()==deferred_rewards+1&&story_scenes::active()&&!story_scenes::fallback_active());
 assert(end_calls==deferred_ends+1);
 static_assert(story_native_scene_manifest::public_selection_enabled());
}
