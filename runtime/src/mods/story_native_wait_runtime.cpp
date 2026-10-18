// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_runtime.h"
#include "story_native_wait_session.h"
#include "story_native_wait_save_gate.h"
#include "story_native_wait_active_save_gate.h"
#include "story_native_event_game_thread.h"
#include "story_native_event_depth.h"
#include "story_native_event_backend.h"
#include "story_native_people_pose_capture.h"
#include "story_native_people_model_graph.h"
#include "story_native_wait_runtime_model_graph.h"
#include "story_native_camera_capture.h"
#include "story_native_wait_runtime_backend.h"
#include "story_native_wait_runtime_controller.h"
#include "story_native_restore_front.h"
#include "story_native_restore_resume.h"
#include "story_native_restore_current_owner.h"
#include "story_native_stage_owner_observer.h"
#include "story_native_stage_restore_group.h"
#include "story_native_stage_table_owner.h"
#include "story_scenes.h"
#include "story_native_scene_catalog.h"
#include "story_native_scene_manifest.h"
#include "story_native_scene_coverage.h"
#include "story_native_scene_activation.h"
#include "story_native_scene_playback.h"
#include "story_native_scene_run_owner.h"
#include "story_native_scene_checkpoint.h"
#include "story_native_scene_retire.h"
#include "savestate_native_finished_load.h"
#include "savestate_native_aggregate.h"
#include "runtime.h"
#include "guest_addr.h"
#include "guest_mods.h"
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
namespace story_native_wait_runtime {
class NativeResumeFrontIssuer {
public:
 static story_native_restore_front::Front capture_load_front();
 static story_native_restore_front::Front capture(story_native_wait_session::Session*,
  const std::string&,const std::array<std::string,2>&,
  std::shared_ptr<const story_native_scene_playback::Activation>);
};
class NativeScenePlaybackIssuer {
public:
 static bool install();
 static story_native_scene_playback::Attempt start(Cpu*,const std::string&,const story_notebook::ManifestScene&);
 static std::shared_ptr<const story_native_scene_playback::Activation>activation(
  const std::shared_ptr<const story_native_scene_request::DeclaredRequest>&request){
  if(!request||!request->current())return {};
  return std::shared_ptr<const story_native_scene_playback::Activation>(
      new story_native_scene_playback::Activation(request));
 }
};
struct DeclaredSlot {
 std::shared_ptr<const story_native_scene_request::DeclaredRequest> request;
 story_native_scene_coverage::Coverage coverage;
 std::unique_ptr<story_native_wait_session::Session> owner;
 std::unique_ptr<story_native_wait_active_save_gate::Controller> gate;
 story_native_wait_runtime_controller::Context*current=nullptr;
 bool allocated=false;
 bool reusable=true;
};
namespace {
// The selected Session is borrowed from process-lifetime storage. Native
// callbacks retain exact Session addresses, so selection must never destroy or
// move an earlier scene's owner, even after its native lease is released.
std::unique_ptr<story_native_wait_session::Session> private_session;
story_native_restore_front::Selection runtime_selection;
auto*&session=runtime_selection.session;
// Retain immutable declarations while the authentic startup window is open.
// This catalog carries definition freshness only; each run still needs its own
// live readiness, ownership and completion proofs.
std::optional<story_native_scene_catalog::StartupCatalog> declared_catalog;
std::unique_ptr<story_native_wait_save_gate::Controller> save_gate;

std::vector<std::unique_ptr<DeclaredSlot>> declared_slots;
auto*&selected_declared=runtime_selection.declared;
auto&selected_activation=runtime_selection.activation;
auto&selected_frontend=runtime_selection.frontend;
bool frontend_installed=false;
unsigned frontend_probe_requests=0;
std::unique_ptr<ss::native_gate::Aggregate> aggregate_save_gate;
namespace D=story_native_event_depth;
// A resolver returns a raw Tracker to the surrounding native body. Retired
// trackers stay alive until process shutdown, including across parked stacks
// during restore; revocation only removes admission to new observations.
std::vector<std::unique_ptr<D::Tracker>> depth_storage;
// Factory callbacks retain only copied context and process-lifetime objects.
std::vector<story_native_wait_runtime_backend::Bundle> preparation_backends;
namespace RC=story_native_wait_runtime_controller;
std::vector<std::unique_ptr<RC::Context>> native_contexts;
std::atomic<RC::Context*> active_context{nullptr};
RC::Context* reported_completion=nullptr;
std::atomic<D::Tracker*> active_depth{nullptr};
bool outer_observer_registered=false;
uint64_t first_depth_serial=0;
bool depth_reported=false;
auto&resident_mod=runtime_selection.mod;
auto&resident_ids=runtime_selection.ids;
LogicStep clock_step=nullptr;
std::atomic<bool> enabled{false};
std::atomic<bool> owner_registered{false};
std::atomic<bool> reported{false};
bool attempted=false,committed=false;
unsigned completed_pilot_runs=0;
thread_local const Cpu* frame_cpu=nullptr;
thread_local uint32_t frame_address=0;
thread_local story_native_event_game_thread::Ticket frame_ticket{};
bool span(uint32_t at,size_t bytes){
 if(!bytes||uint64_t(at)+bytes>0x100000000ull)return false;
 return (at>=mem::kMem2Start&&uint64_t(at)+bytes<=mem::kMem2End)||
  (at>=mem::kRuntimeStart&&uint64_t(at)+bytes<=mem::runtime_top()&&uint64_t(at)+bytes<=mem::kHostStart);
}
bool read(uint32_t at,std::span<uint8_t>out){
 if(!span(at,out.size()))return false;
 std::memcpy(out.data(),mem::ptr(at),out.size());return true;
}
bool scalar(uint32_t at,uint32_t&out){
 std::array<uint8_t,4>b{};if(!read(at,b))return false;
 out=uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];return true;
}
bool boundary(const Cpu*c,story_native_wait_readiness::Boundary&out){
 out={};const auto ticket=story_native_event_game_thread::ticket(c);
 if(!clock_step||!ticket.epoch||!story_native_event_game_thread::verify(c,ticket)||
    (story_scenes::active()&&!(selected_frontend&&selected_activation&&story_scenes::native_owner(*selected_activation))))return false;
 // OSCreateThread writes these native OSThread fields in threads.cpp. Read the
 // current attested thread, never a caller-supplied stack range.
 uint32_t top=0,bottom=0,host_top=0,host_bottom=0;
 if(!threads::current_stack_bounds(c,host_bottom,host_top)||
    !span(ticket.guest,0x39C)||!scalar(ticket.guest+0x394,top)||!scalar(ticket.guest+0x398,bottom)||
    bottom!=host_bottom||top!=host_top||
    // Native Kamome eventProc reserves 0x18 bytes before staff lookup. Existing
    // read-only callbacks therefore legitimately arrive at 8-byte alignment.
    bottom>=top||!span(bottom,uint64_t(top)-bottom)||(c->r[1]&7)||
    c->r[1]<bottom+0x40||c->r[1]>top)return false;
 out={ticket.guest,bottom,top,clock_step(),true};
 if(selected_frontend&&selected_activation&&story_scenes::native_owner(*selected_activation))out.owned_scene=selected_activation;
 frame_cpu=c;frame_address=c->r[1]-0x40;frame_ticket=ticket;
 return true;
}
bool restore_frame(uint32_t at,std::span<const uint8_t>bytes){
 if(bytes.size()!=0x40||at!=frame_address||!frame_cpu||threads::current()!=frame_cpu||
    !story_native_event_game_thread::verify(frame_cpu,frame_ticket)||!span(at,bytes.size()))return false;
 std::memcpy(mem::ptr(at),bytes.data(),bytes.size());return true;
}
std::string env(const char*name){const char*value=std::getenv(name);return value?value:"";}
bool observe_outer(const Cpu*c,uint32_t function,D::Tracker*&tracker,D::Call&call) noexcept{
 tracker=active_depth.load(std::memory_order_acquire);if(!tracker||!c)return false;
 if(auto*context=active_context.load(std::memory_order_acquire)){
  // No owned Tracker scope exists yet. The cheap probe releases its Stage
  // scan before renew_stage acquires physical parking and fresh lifetime pins.
  try{if(threads::current()==c&&context->stage_renewal_needed()){
   const bool renewed=context->renew_stage(threads::current());
   LOG("[native_wait] outer Stage renewal=%u fn=%08X",unsigned(renewed),function);
  }}catch(...){LOG("[native_wait] outer Stage renewal inspection failed fn=%08X",function);}
  // Renewal never authorizes delegation: ordinary exact outer admission must
  // independently pass against the shared binding, including on refusal.
  return context->outer(c,function,call);
 }
 for(auto entry:{D::Entry::PeopleExecute,D::Entry::CameraExecute,D::Entry::ManagerRun})
  if(function==D::entry_address(entry)){
   call={entry,function,c->r[3],c->r[3],0,0,{},D::Authority::Foreign};return true;
  }
 tracker=nullptr;return false;
}
// guestmods issues this only after the actual original outer entry and all
// dispatcher return hooks have completed and its depth scope has unwound.
void observe_outer_return(Cpu*c,const story_native_outer_return::OuterReturn&returned)noexcept{
 if(auto*context=active_context.load(std::memory_order_acquire))context->outer_return(c,returned);
}
guestmods::NativeWaitDecision bootstrap_route(
 Cpu*c,uint32_t function,const guestmods::NativeWaitNext&next){
 if(auto*context=active_context.load(std::memory_order_acquire))
  return context->route(c,function,next)==story_native_wait_owned_routes::Decision::Handled?
      guestmods::NativeWaitDecision::Handled:guestmods::NativeWaitDecision::Delegate;
 // No retained native event claim exists during discovery. The route runner
 // preserves the prior world-actor owner or calls the original exactly once.
 // Inner foreign scopes must span the actual delegated body. Outer scopes are
 // supplied by observed_native, which also merges call_original delegation.
 if(auto*tracker=active_depth.load(std::memory_order_acquire)){
  for(auto entry:{D::Entry::PeopleEventMove,D::Entry::CommonCut,D::Entry::CameraEvent,
      D::Entry::ManagerMain,D::Entry::Timekeeper,D::Entry::StaffLookup,D::Entry::CutEnd,
      D::Entry::WaitStart,D::Entry::WaitProcess}){
   if(function!=D::entry_address(entry))continue;
   D::Call call{entry,function,c->r[3],c->r[3],0,0,{},D::Authority::Foreign};
   auto scope=tracker->enter(call);
   next();return guestmods::NativeWaitDecision::Handled;
  }
 }
 return guestmods::NativeWaitDecision::Delegate;
}
// Discovery evidence only: the Stage guard protects the resource census, but
// this does not issue a consumer lease or authorize publication.
story_native_stage_table_owner::Result stage_census(
 Cpu*c,const story_native_event_observation::Expected&expected){
 const auto owner=story_native_stage_owner_observer::snapshot();
 if(!owner)return {};
 story_native_wait_readiness::Boundary entry;
 if(!boundary(c,entry))return {};
 const auto current=[&](story_native_stage_table_owner::Context&out){
  story_native_wait_readiness::Boundary now;
  uint32_t initialized=0,stage=0,room=0;
  std::array<uint8_t,8>name{};
  std::array<uint8_t,0xF4>control{};
  uint32_t camera_play=0,event=0;
  if(!boundary(c,now)||now!=entry||
     !scalar(GD(0x104753A8),initialized)||!initialized||
     !scalar(GD(0x1047E6C4),stage)||stage!=expected.identity.stage||
     !scalar(GD(0x1047E8F0)+expected.room_number*0x22C,room)||room!=expected.identity.room||
     !read(GD(0x1046F0B0)+0x5134,name)||name!=expected.stage_name||
     !read(expected.identity.manager-0xF4,control)||control[0xC0]||control[0xC2]||
     !scalar(expected.identity.manager+0x20,camera_play)||camera_play||
     !scalar(expected.identity.manager+0x53C,event)||event)return false;
  out={};out.stage_pid=stage;out.room_pid=room;out.room_number=expected.room_number;
  out.stage_name=name;out.logic_step=now.logic_step;out.serial=now.logic_step;
  out.exclusive_idle=true;return true;
 };
 const auto live=[&](const story_native_setinfo_candidate::Candidate&candidate){
  story_native_setinfo_candidate::Candidate observed;
  return candidate==owner->receipt.owner&&
   story_native_setinfo_observation::inspect_live(owner->control,candidate,observed);
 };
 const auto result=story_native_stage_table_owner::inspect(
  story_native_stage_owner_observer::tracker(),read,current,live);
 if(result.reason==story_native_event_table_scan::Reason::Layout){
  // Bounded private diagnosis of the rejected native layout; never repair it.
  LOG("[native_wait] Stage layout base=%08X bytes=%u",owner->receipt.owner.resource,owner->receipt.owner.resource_bytes);
  for(unsigned n=0;n<7;++n){
   uint32_t offset=0,count=0,pointer=0;
   if(!scalar(owner->receipt.owner.resource+n*8,offset)||
      !scalar(owner->receipt.owner.resource+n*8+4,count)||
      !scalar(expected.identity.manager+(n+1)*4,pointer))break;
   LOG("[native_wait] Stage pool=%u offset=%u count=%u pointer=%08X",n,offset,count,pointer);
  }
 }
 return result;
}
bool resident_binding_for(const std::string&mod,const std::array<std::string,2>&ids,
 const story_native_event_observation::Actor&actor,
 const story_native_event_lease::Identity&identity,
 const story_native_event_observation::Expected&expected){
 if(identity!=expected.identity||actor.profile!=0x175||actor.subtype!=1)return false;
 for(unsigned n=0;n<2;++n){const auto&want=expected.actors[n];
  if(actor.address!=want.address||actor.pid!=want.pid||actor.token!=want.token||actor.fingerprint!=want.fingerprint)continue;
  story_actors::ResidentEventIdentity live;
  return story_actors::resident_event_identity(mod,ids[n],actor.address,actor.pid,live)&&
   live.token==actor.token&&live.fingerprint==actor.fingerprint&&live.stage_pid==identity.stage&&
   live.room_pid==identity.room&&live.room==expected.room_number&&live.subtype==1;
 }
 return false;
}
bool resident_binding(const story_native_event_observation::Actor&actor,
 const story_native_event_lease::Identity&identity,
 const story_native_event_observation::Expected&expected){
 return resident_binding_for(resident_mod,resident_ids,actor,identity,expected);
}
// Synchronous pending-source bridge. Its authentic Boundary never escapes
// the post-load observer; clearing it permanently removes the pending path.
// Ordinary native entry still requires the exact published runtime fronts.
struct RestorePending {
 const story_native_restore_resume::Boundary*boundary=nullptr;
 Cpu*cpu=nullptr;RC::Context*old_context=nullptr;D::Tracker*old_depth=nullptr;
 D::Tracker*tracker=nullptr;story_native_wait_session::Session*owner=nullptr;
 std::string mod;std::array<std::string,2>ids;
 bool current()const noexcept{
  try{return boundary&&cpu&&tracker&&owner&&boundary->current(cpu)&&
   active_context.load(std::memory_order_acquire)==old_context&&
   active_depth.load(std::memory_order_acquire)==old_depth&&
   RC::Context::restore_front_current(*boundary,old_context)&&tracker->restoring(*boundary);
  }catch(...){return false;}
 }
};
std::optional<story_native_wait_runtime_backend::Bundle> runtime_backend(
 Cpu*c,const story_native_event_observation::Expected&expected,D::Tracker&depth,
 story_native_stage_owner_observer::Snapshot owner,story_native_event_game_thread::Ticket ticket,
 const story_native_event_arena::Layout&layout,std::shared_ptr<RestorePending>pending={}){
 namespace B=story_native_event_backend;
 if(pending&&(!pending->current()||pending->tracker!=&depth||
    pending->boundary->completion().expected()!=expected))return {};
 const bool turn=expected.program&&expected.program->kind()==story_native_event_program::Kind::TurnCamera;
 const auto&instance=turn?layout.turn:layout.wait;
 B::Config config;config.memory.stamp=config.collector.stamp=instance.stamp.address;
 config.memory.record={instance.record.address,instance.record.bytes};
 if(!scalar(GD(0x1046F0B0)+0x5B34,config.memory.link)||
    !scalar(config.memory.link+4,config.memory.link_pid))return {};
 std::array<uint8_t,24> manager_context{};
 if(!read(expected.identity.manager+0x24,manager_context))return {};
 std::copy_n(manager_context.begin(),12,config.collector.manager_exception.begin());
 std::copy_n(manager_context.begin()+12,12,config.collector.manager_goal.begin());
 story_native_wait_runtime_backend::Callbacks callbacks;
 auto*const owned_session=pending?pending->owner:session;
 callbacks.session=owned_session;
 callbacks.read=read;
 callbacks.resident=[expected,mod=pending?pending->mod:resident_mod,ids=pending?pending->ids:resident_ids](const auto&actor,const auto&identity){
  return resident_binding_for(mod,ids,actor,identity,expected);
 };
 callbacks.fence=[owned_session]{return owned_session&&owned_session->fence_held();};
 callbacks.retained=[expected,tracker=&depth,pending](const auto&e){auto*context=active_context.load(std::memory_order_acquire);return e==expected&&((active_depth.load(std::memory_order_acquire)==tracker&&context&&context->retained())||(pending&&pending->current()&&pending->tracker==tracker));};
 callbacks.routes=[tracker=&depth,pending]{
  return outer_observer_registered&&guestmods::routes_owned()&&
   (active_depth.load(std::memory_order_acquire)==tracker||(pending&&pending->tracker==tracker&&pending->current()));
 };
 callbacks.baseline=[owned_session](const auto&expected){
  if(!owned_session||!owned_session->fence_held())return false;
  auto stored=owned_session->baseline();
  if(!stored&&!owned_session->quarantined())stored=owned_session->staged_expected();
  return stored&&*stored==expected;
 };
 callbacks.boundary=boundary;
 callbacks.allocation=[expected,layout](const auto&identity,const auto&){
  return identity==expected.identity&&story_native_event_arena::owns(
      layout,mem::runtime_alloc_log(),mem::runtime_top());
 };
 return story_native_wait_runtime_backend::make(
     expected,layout,config,owner,ticket,c,depth,std::move(callbacks));
}
story_native_event_backend::Report preparing_preflight(
 Cpu*c,const story_native_event_observation::Expected&expected,D::Tracker&depth){
 namespace B=story_native_event_backend;
 B::Report refused;refused.reason=B::Reason::Session;
 if(!session->fence_held()||active_depth.load(std::memory_order_acquire)!=&depth||
    !outer_observer_registered||!guestmods::routes_owned())return refused;
 const auto owner=story_native_stage_owner_observer::snapshot();
 const auto stored=session->staged_expected();
 story_native_wait_readiness::Boundary entry;
 if(!owner||!stored||*stored!=expected||!boundary(c,entry))return refused;
 auto bundle=runtime_backend(c,expected,depth,*owner,story_native_event_game_thread::ticket(c),session->arena());
 if(!bundle)return refused;
 preparation_backends.push_back(std::move(*bundle));
 auto&retained=preparation_backends.back();
 B::Scaffold backend(expected,retained.config,retained.sources,depth);
 return backend.preflight(story_native_event_observation::Operation::Prepare);
}
}
static bool setup_routes(){
 if(!story_native_stage_owner_observer::configure(story_native_setinfo_observation::inspect_detailed,
     story_native_setinfo_observation::inspect_live)||
    !story_native_stage_owner_hooks::register_observer(story_native_stage_owner_observer::observer())){
  LOG("[native_wait] bootstrap refused: owner observer conflict");return false;
 }
 owner_registered.store(true,std::memory_order_release);
 if(!guestmods::install_native_wait_routes(bootstrap_route)){
  // The installer is transactional. Read-only discovery can still diagnose
  // Stage ownership when native route admission fails; publication stays off.
  LOG("[native_wait] native routes unavailable: read-only discovery only");
 }
 const auto routes=guestmods::native_wait_route_coverage();
 if(routes.certified){
  const bool returned=guestmods::register_native_outer_return_callback(observe_outer_return);
  outer_observer_registered=returned&&guestmods::register_native_event_observer(observe_outer);
  LOG("[native_wait] outer return callback registered=%u publication=disabled",unsigned(returned));
 }
 LOG("[native_wait] outer depth observer registered=%u publication=disabled",unsigned(outer_observer_registered));
 LOG("[native_wait] discovery routes=%u/%u composed_world_actors=%u composed_notebook=%u certified=%u publication=disabled",
     routes.installed,routes.requested,routes.world_actors_composed,routes.notebook_composed,unsigned(routes.certified));
 return routes.certified&&outer_observer_registered;
}
static void startup_private(bool stage_hooks,LogicStep step){
 if(env("WWHD_TEST_NATIVE_WAIT")!="1")return;
 if(!stage_hooks||!step||env("WWHD_TEST_NATIVE_SETINFO_PROBE")=="1"){
  LOG("[native_wait] bootstrap refused: Stage observer unavailable");return;
 }
 if(env("WWHD_TEST_NATIVE_DECLARED_SCENE")=="1"){
  const auto mod=env("WWHD_TEST_NATIVE_WAIT_MOD"),scene=env("WWHD_TEST_NATIVE_WAIT_SCENE");
  for(const auto&slot:declared_slots)
   if(slot->request->mod()==mod&&slot->request->scene()==scene){selected_declared=slot.get();break;}
  if(!selected_declared||!selected_declared->allocated||!selected_declared->request->current()){
   selected_declared=nullptr;LOG("[native_wait] declared pilot refused: no eligible allocated declaration");return;
  }
  session=selected_declared->owner.get();
  resident_mod=selected_declared->request->mod();
  resident_ids=selected_declared->request->compiled().actor_ids();
 }else{
 story_native_wait_readiness::Request request;
 request.private_opt_in=true;request.fixture_program="sdk-private-wait-v1";
 request.mod=env("WWHD_TEST_NATIVE_WAIT_MOD");request.scene=env("WWHD_TEST_NATIVE_WAIT_SCENE");
 request.residents={env("WWHD_TEST_NATIVE_WAIT_ACTOR_A"),env("WWHD_TEST_NATIVE_WAIT_ACTOR_B")};
 resident_mod=request.mod;resident_ids=request.residents;
 const auto frames=env("WWHD_TEST_NATIVE_WAIT_FRAMES");
 if(!frames.empty()){
  char*end=nullptr;const auto value=std::strtoul(frames.c_str(),&end,10);
  if(!end||*end||!value||value>32767){LOG("[native_wait] bootstrap refused: invalid timer");return;}
  request.native_wait_timer=uint32_t(value);
 }
 if(env("WWHD_TEST_NATIVE_TURN")=="1"){
  story_native_turn_camera_image::Program program;
  program.wait_timer=request.native_wait_timer;program.timekeeper_timer=300;
  program.actor_a_target={400,1040,-201500};program.actor_b_target={100,1040,-201500};
  program.camera.start_eye={-500,1350,-201100};program.camera.start_center={250,1100,-201500};
  program.camera.end_eye={1000,1350,-201100};program.camera.end_center={250,1100,-201500};
  program.camera.hold_timer=30;program.camera.move_timer=120;program.camera.fovy=45;
  request.program=story_native_event_program::Program::turn(program,600);
  if(!request.program){LOG("[native_wait] bootstrap refused: invalid private TURN program");return;}
  request.fixture_program="sdk-private-turn-v1";
 }
 clock_step=step;committed=true;
 story_native_wait_readiness::Sources sources;
 sources.startup_complete=[]{return committed;};sources.boundary=boundary;
 sources.read=read;sources.restore_frame=restore_frame;
 private_session=std::make_unique<story_native_wait_session::Session>(std::move(request),std::move(sources));
 session=private_session.get();
 save_gate=std::make_unique<story_native_wait_save_gate::Controller>(*session);
 if(!session->startup()) {LOG("[native_wait] bootstrap refused: declarations or arena");return;}
 }
 if(!setup_routes())return;
 enabled.store(true,std::memory_order_release);
 auto hold=session->try_fence();if(!hold){enabled=false;LOG("[native_wait] bootstrap fence unavailable");return;}
 const auto arena=session->arena();
 LOG("[native_wait] bootstrap ready arena=%08X generation=%u publication=disabled",
     arena.allocation.address,session->generation());
}
void startup(bool stage_hooks,LogicStep step){
 if(attempted)return;attempted=true;
 clock_step=step;committed=true;
 std::string why;
 declared_catalog=story_native_scene_catalog::collect(why);
 if(!declared_catalog){
  LOG("[native_wait] declaration catalog unavailable: %s",why.c_str());
 }else if(stage_hooks&&step&&env("WWHD_TEST_NATIVE_SETINFO_PROBE")!="1"){
  // Allocate while each request's genuine startup lineage is still current.
  // Every object survives even a later refusal: its guest arena and any future
  // callback must never outlive the host Session that owns it.
  for(const auto&request:declared_catalog->requests()){
   auto coverage=story_native_scene_coverage::inspect(request->definition());
   if(coverage.selection!=story_native_scene_coverage::Selection::NativeAlternative)continue;
   auto slot=std::make_unique<DeclaredSlot>();
   slot->request=request;slot->coverage=std::move(coverage);
   story_native_wait_readiness::Request selected;selected.declared=request;
   story_native_wait_readiness::Sources sources;
   sources.startup_complete=[]{return committed;};sources.boundary=boundary;
   sources.read=read;sources.restore_frame=restore_frame;
   slot->owner=std::make_unique<story_native_wait_session::Session>(std::move(selected),std::move(sources));
   // Retain before allocation so a refusal cannot erase an allocated arena's
   // owning Session. A failed child remains fail-closed in the aggregate gate.
   auto*live=slot.get();declared_slots.push_back(std::move(slot));
   live->allocated=live->owner->startup();
   live->gate=std::make_unique<story_native_wait_active_save_gate::Controller>(*live->owner,[live]{return live->current;},[]{return NativeResumeFrontIssuer::capture_load_front();});
   LOG("[native_wait] declared scene %s:%s startup=%u public_selection=%u",
       request->mod().c_str(),request->scene().c_str(),unsigned(live->allocated),unsigned(story_native_scene_manifest::public_selection_enabled()));
  }
 }
 startup_private(stage_hooks,step);
 if(env("WWHD_TEST_NATIVE_WAIT")!="1"&&!declared_slots.empty()&&stage_hooks&&step&&
    env("WWHD_TEST_NATIVE_SETINFO_PROBE")!="1")
  enabled.store(setup_routes(),std::memory_order_release);
 std::vector<ss::native_gate::Controller*>children;
 for(const auto&slot:declared_slots)children.push_back(slot->gate.get());
 if(save_gate)children.push_back(save_gate.get());
 if(children.empty())return;
 aggregate_save_gate=ss::native_gate::Aggregate::create(children,why,[](const ss::native_gate::SavedView&view){
  ss::native_gate::Aggregate::LoadGroup group;
  group.stage=story_native_stage_restore_group::StageRestoreGroup::create();
  group.close=[](auto&stage)noexcept{stage.close();};
  static std::atomic<uint64_t>epochs{0};auto previous=epochs.load(std::memory_order_relaxed);
  while(previous!=UINT64_MAX&&!epochs.compare_exchange_weak(previous,previous+1,std::memory_order_relaxed)){}
  if(previous==UINT64_MAX)return ss::native_gate::Aggregate::LoadGroup{};
  group.owner=RC::Context::prepare_current_load(NativeResumeFrontIssuer::capture_load_front(),view,previous+1);
  if(!group.owner)return ss::native_gate::Aggregate::LoadGroup{};
  group.close_owner=[](auto&owner)noexcept{owner.close();};
  group.finish=[](const ss::native_gate::FinishedLoad&finished)noexcept{
   try{return RC::Context::finish_empty_load(finished,NativeResumeFrontIssuer::capture_load_front(),
    [](story_native_scene_checkpoint::CurrentRunRevocation&&revoked){
     auto prepared=story_scenes::prepare_native_retire(std::move(revoked));
     return prepared?std::make_shared<story_native_scene_checkpoint::PreparedRetire>(std::move(*prepared)):
      std::shared_ptr<story_native_scene_checkpoint::PreparedRetire>{};
    });}catch(...){return false;}
  };return group;
 });
 if(!aggregate_save_gate||!ss::native_gate::register_controller(aggregate_save_gate.get())){
  enabled.store(false,std::memory_order_release);
  LOG("[native_wait] aggregate save controller refused: %s",why.c_str());
 }else if(enabled&&(story_native_scene_manifest::public_selection_enabled()||
                  env("WWHD_TEST_NATIVE_SCENE_FRONTEND")=="1")){
  frontend_installed=NativeScenePlaybackIssuer::install();
  LOG("[native_wait] frontend backend installed=%u public_selection=%u",
      unsigned(frontend_installed),unsigned(story_native_scene_manifest::public_selection_enabled()));
  if(!frontend_installed)enabled=false;
 }
}
static RC::Sources native_sources(story_native_wait_session::Session*,const std::string&,
 const std::array<std::string,2>&,std::shared_ptr<const story_native_scene_playback::Activation>);
static void restored_scene_boundary(Cpu*c,const story_native_restore_resume::Boundary&restored,
 story_native_wait_session::Session*owner,const std::string&mod,const std::array<std::string,2>&ids,
 std::shared_ptr<const story_native_scene_playback::Activation>activation){
 auto*old=active_context.load(std::memory_order_acquire);
 LOG("[native_wait] actual restored boundary entered epoch=%llu",
     static_cast<unsigned long long>(restored.load_epoch()));
 if(!owner||!restored.current(c)||!RC::Context::restore_front_current(restored,old)){
  LOG("[native_wait] actual restored boundary refused original front");return;
 }
 // Access::issue already minted the first ticket at this authentic boundary.
 // Reissuing twice is correctly forbidden: consume its fresh ticket instead.
 const auto ticket=story_native_event_game_thread::ticket(c);
 if(ticket.epoch!=restored.load_epoch()||!story_native_event_game_thread::verify(c,ticket)){
  LOG("[native_wait] actual restored boundary refused fresh ticket");return;
 }
 const auto*certified=restored.completion().stage();if(!certified){
  LOG("[native_wait] actual restored boundary refused Stage certificate");return;
 }
 {
  std::string why;auto parked=threads::try_native_quiescence(c,ticket,100,why);if(!parked){LOG("[native_wait] actual restored boundary refused parking: %s",why.c_str());return;}
  auto scan=story_native_stage_owner_observer::tracker().try_scan();
  const auto live=story_native_stage_owner_observer::snapshot(scan);
  if(!live||live->control!=certified->control||live->receipt!=certified->receipt){LOG("[native_wait] actual restored boundary refused current Stage");return;}
 }
 auto fresh=guestmods::acquire_native_restore_tracker(restored,c);if(!fresh){LOG("[native_wait] actual restored boundary refused Tracker");return;}
 auto*tracker=fresh.get();depth_storage.push_back(std::move(fresh));
 auto pending=std::make_shared<RestorePending>();
 pending->boundary=&restored;pending->cpu=c;pending->old_context=old;
 pending->old_depth=active_depth.load(std::memory_order_acquire);
 pending->tracker=tracker;pending->owner=owner;pending->mod=mod;pending->ids=ids;
 struct ClearBoundary {std::shared_ptr<RestorePending>pending;~ClearBoundary(){pending->boundary=nullptr;}}clear{pending};
 std::array<story_native_people_admission::Reference,2>refs;
 for(unsigned n=0;n<2;++n)refs[n]={mod,ids[n]};
 auto factory=[c,tracker,stage=*certified,ticket,pending](const auto&expected,const auto&layout){
  return runtime_backend(c,expected,*tracker,stage,ticket,layout,pending);
 };
 auto context=RC::Context::prepare_restored(restored,*owner,*tracker,*certified,ticket,c,
     std::move(factory),std::move(refs),native_sources(owner,mod,ids,activation));
 if(!context){LOG("[native_wait] actual restored context refused");return;}
 // Allocate persistent ownership BEFORE the final no-fail host transaction.
 // A failed final proof still retains its quarantined Context and callbacks.
 auto*live=context.get();native_contexts.push_back(std::move(context));
 const bool resumed=live->resume_restored(restored,NativeResumeFrontIssuer::capture(owner,mod,ids,activation));
 if(resumed){
  for(const auto&slot:declared_slots)if(slot->owner.get()==owner)slot->current=live;
  reported_completion=nullptr;reported=false;depth_reported=false;
  first_depth_serial=tracker->snapshot().serial;
 }
 LOG("[native_wait] actual restored context resumed=%u retained=%u",
     unsigned(resumed),unsigned(live->retained()));
}
static RC::Sources native_sources(story_native_wait_session::Session*owner,const std::string&mod,
 const std::array<std::string,2>&ids,std::shared_ptr<const story_native_scene_playback::Activation>activation){
 RC::Sources native;native.activation=activation;
 native.restore_read=read; // data-only; opaque suspended permit gates restored-byte inspection
 native.write=[](uint32_t at,std::span<const uint8_t>bytes){
  if(!span(at,bytes.size()))return false;
  std::memcpy(mem::ptr(at),bytes.data(),bytes.size());
  return !std::memcmp(mem::ptr(at),bytes.data(),bytes.size());
 };
 native.call=[](Cpu*cpu,uint32_t function,std::initializer_list<uint32_t>args){
  const bool lifecycle=function!=GC(0x0259F858);
  if(lifecycle)LOG("[native_wait] native call=%08X",function);
  const auto value=guest_call(cpu,function,args);
  if(lifecycle)LOG("[native_wait] native returned=%08X value=%08X",function,value);
  return value;
 };
 native.stack=[](Cpu*cpu,story_native_event_invoke::Config&config){
  config={};story_native_wait_readiness::Boundary now;
  if(!boundary(cpu,now))return false;
  config.stack={now.stack_begin,now.stack_end-now.stack_begin};return true;
 };
 native.routes=[]{return outer_observer_registered&&guestmods::routes_owned();};
 native.prepare_frontend_restore=[](const story_native_scene_checkpoint::Checkpoint&saved,
  story_native_scene_checkpoint::CurrentRunRevocation&&revoked){
  auto prepared=story_scenes::prepare_native_restore(saved,std::move(revoked));
  return prepared?std::make_shared<story_native_scene_checkpoint::PreparedRestore>(std::move(*prepared)):
   std::shared_ptr<story_native_scene_checkpoint::PreparedRestore>{};
 };
 native.restored_boundary=[owner,mod,ids,activation=std::move(activation)](Cpu*c,const auto&restored){
  restored_scene_boundary(c,restored,owner,mod,ids,activation);
 };
 return native;
}
static void service_selected(Cpu*c,bool force){
 if(!enabled.load(std::memory_order_acquire)||!clock_step)return;
 const auto step=clock_step();auto*current=active_context.load(std::memory_order_acquire);
 // The queued-cancel probe must sample actual logic boundaries, rather than
 // rounding its requested elapsed time to the discovery diagnostic cadence.
 if(!force&&step%30&&!(current&&((selected_frontend&&selected_activation&&story_scenes::native_owner(*selected_activation))||
    !env("WWHD_TEST_NATIVE_QUEUE_CANCEL_AFTER_TICKS").empty())))return;
 if(auto*context=current){
  const auto before=context->state();const auto after=context->tick(c);
  if(before!=after)LOG("[native_wait] native state=%u retained=%u public_selection=%u",
      unsigned(after),unsigned(context->retained()),unsigned(story_native_scene_manifest::public_selection_enabled()));
  if(after==RC::State::Released&&reported_completion!=context){
   reported_completion=context;
   if(!selected_frontend&&selected_declared&&selected_activation){
    auto completion=context->take_completion();
    const bool natural=completion&&completion->consume(*selected_activation);
    const bool replay_denied=completion&&!completion->consume(*selected_activation);
    LOG("[native_wait] declared pilot completion natural=%u replay_denied=%u",
        unsigned(natural),unsigned(replay_denied));
   }
   if(selected_frontend){
    const bool rearmed=context->rearm(c);
    if(selected_declared&&!rearmed)selected_declared->reusable=false;
    active_context.store(nullptr,std::memory_order_release);
    active_depth.store(nullptr,std::memory_order_release);
    reported=false;depth_reported=false;first_depth_serial=0;
    session=nullptr;selected_declared=nullptr;selected_activation.reset();selected_frontend=false;
    LOG("[native_wait] frontend retired rearm=%u retained=%u",unsigned(rearmed),unsigned(context->retained()));
    return;
   }
   ++completed_pilot_runs;
   const auto repeat=env("WWHD_TEST_NATIVE_REPEAT_RUNS");
   if(!repeat.empty()){
    char*end=nullptr;const auto limit=std::strtoul(repeat.c_str(),&end,10);
    if(end&&!*end&&limit>=2&&limit<=8&&completed_pilot_runs<limit){
     // Consume the genuine release certificate synchronously: its exact
     // release clock/serial must still match this boundary. Retain the old
     // Context/Tracker storage; the next run constructs fresh helpers and a
     // fresh preclaim model baseline against the newly minted generation.
     const bool rearmed=context->rearm(c);
     LOG("[native_wait] pilot repeat rearm=%u completed=%u requested=%lu",
         unsigned(rearmed),completed_pilot_runs,limit);
     if(rearmed){
      active_context.store(nullptr,std::memory_order_release);
      active_depth.store(nullptr,std::memory_order_release);
      reported=false;depth_reported=false;first_depth_serial=0;
     }else enabled.store(false,std::memory_order_release);
    }
   }
  }
  return;
 }
 if(!session){
  unsigned long requested=1;
  const auto repeat=env("WWHD_TEST_NATIVE_REPEAT_RUNS");
  if(!repeat.empty()){
   char*end=nullptr;const auto count=std::strtoul(repeat.c_str(),&end,10);
   if(end&&!*end&&count>=2&&count<=8)requested=count;
  }
  // The FIFO deliberately rejects duplicate pending/active scene IDs. Queue
  // each repeat only after the previous frontend owner has fully retired.
  if(frontend_installed&&frontend_probe_requests<requested&&!story_scenes::active()&&
     env("WWHD_TEST_NATIVE_SCENE_FRONTEND")=="1"){
   const auto after=env("WWHD_TEST_NATIVE_FRONTEND_AFTER_STEP");
   if(!after.empty()){
    char*end=nullptr;const auto first=std::strtoull(after.c_str(),&end,10);
    if(!end||*end||first>10000000||step<first)return;
   }
   const auto mod=env("WWHD_TEST_NATIVE_WAIT_MOD");
   auto scene=env("WWHD_TEST_NATIVE_WAIT_SCENE");
   const auto second=env("WWHD_TEST_NATIVE_SECOND_SCENE");
   if(frontend_probe_requests&&!second.empty())scene=second;
   for(const auto&slot:declared_slots)if(slot->allocated&&slot->request->mod()==mod&&slot->request->scene()==scene&&slot->owner->inspect(c)){
    const bool queued=story_scenes::request(mod,scene);
    if(queued)++frontend_probe_requests;
    LOG("[native_wait] frontend probe queued=%u index=%u requested=%lu scene=%s:%s",
        unsigned(queued),frontend_probe_requests,requested,mod.c_str(),scene.c_str());break;
   }
  }
  return;
 }
 if(auto*tracker=active_depth.load(std::memory_order_acquire)){
  const auto depth=tracker->snapshot();
  if(!depth_reported&&depth.serial>first_depth_serial&&depth.idle()&&guestmods::routes_owned()){
   auto hold=session->try_fence();if(!hold)return;
   story_native_wait_readiness::Boundary current;if(!boundary(c,current))return;
   const auto record=tracker->record();
   if(!tracker->begin_drain())return;
   const auto fenced=tracker->snapshot();
   const bool stored=fenced.idle()&&fenced.serial==depth.serial&&session->stage_depth(record);
   const auto after=tracker->snapshot();
   const bool stable=after.idle()&&after.serial==fenced.serial;
   const bool resumed=tracker->cancel_drain();
   LOG("[native_wait] persistent native depth serial=%llu idle=%u fault=%u coverage=%u stored=%u fence_restored=%u publication=disabled",
       static_cast<unsigned long long>(fenced.serial),unsigned(after.idle()),unsigned(after.fault),
       unsigned(after.coverage),unsigned(stored&&stable),unsigned(resumed));
   if(!stored||!stable||!resumed){
    session->revoke_unexposed();active_depth.store(nullptr,std::memory_order_release);
    enabled.store(false,std::memory_order_release);
   }
   depth_reported=true;
  }
  return; // private staged image remains unexposed to all native consumers
 }
 const bool ready=session->inspect(c);
 auto hold=session->try_fence();if(!hold)return;
 const auto expected=session->expected();
 if(!ready&&step%600==0)
  LOG("[native_wait] discovery pending check=%u generation=%u quarantined=%u",
      unsigned(session->readiness_check()),session->generation(),unsigned(session->quarantined()));
 if(ready&&expected&&!reported.exchange(true)){
  const auto stage=story_native_stage_owner_observer::snapshot();
  const auto census=stage_census(c,*expected);
  LOG("[native_wait] discovered actors=%08X/%08X camera=%08X manager=%08X generation=%u stage_owner=%u publication=disabled",
      expected->actors[0].address,expected->actors[1].address,expected->camera.address,
      expected->identity.manager,expected->identity.generation,unsigned(bool(stage)));
  LOG("[native_wait] Stage census idle=%u reason=%u events=%u blocking=%u state=%u publication=disabled",
      unsigned(census.idle),unsigned(census.reason),census.events,census.blocking_index,census.blocking_state);
  if(selected_declared||env("WWHD_TEST_NATIVE_WAIT_STAGE")=="1"){
   const auto routes=guestmods::native_wait_route_coverage();
   const bool turn=expected->program&&expected->program->kind()==story_native_event_program::Kind::TurnCamera;
   story_native_people_pose_record::Poses prepared_poses;unsigned prepared_count=0;
   if(turn&&census.idle&&routes.certified&&outer_observer_registered){
    const story_native_people_pose_capture::Layout pose_layout{GD(0x101C4CD0),GD(0x100209C4)};
    const auto verify=[&](const auto&a,const auto&i){return resident_binding(a,i,*expected);};
    for(unsigned n=0;n<2;++n){if(!story_native_people_pose_capture::capture(read,verify,pose_layout,*expected,n,prepared_poses[n]))break;++prepared_count;}
   }
   const bool staged=census.idle&&routes.certified&&outer_observer_registered&&(turn?(prepared_count==2&&session->stage_image(prepared_poses)):session->stage_image());
   const auto record=session->journal();
   LOG("[native_wait] private image staged=%u journal_phase=%u journal_serial=%llu quarantined=%u publication=disabled",
       unsigned(staged),record?unsigned(record->phase):~0u,
       static_cast<unsigned long long>(record?record->serial:0),unsigned(session->quarantined()));
   if(staged){
    D::Coverage coverage;
    coverage.people={routes.outer_owner[0],routes.outer_owner[0],true};
    coverage.camera={routes.outer_owner[1],routes.outer_owner[1],true};
    coverage.manager={routes.outer_owner[2],routes.outer_owner[2],true};
    coverage.inner_callbacks=true;
    auto tracker=std::make_unique<D::Tracker>(*expected,coverage);
    if(tracker->ready()){
     auto*retained=tracker.get();depth_storage.push_back(std::move(tracker));
     first_depth_serial=retained->snapshot().serial;depth_reported=false;
     active_depth.store(retained,std::memory_order_release);
     // Persist the actual initial idle receipt in the same fenced boundary as
     // staging. A later observed serial cannot substitute for this baseline.
     const bool initial_depth=session->stage_depth(retained->record());
     const auto preflight=preparing_preflight(c,*expected,*retained);
     LOG("[native_wait] bounded preparation preflight=%u reason=%u collector=%u observation=%u serial=%llu publication=disabled",
         unsigned(preflight.preflight),unsigned(preflight.reason),unsigned(preflight.collector_error),
         unsigned(preflight.observation.reason()),static_cast<unsigned long long>(preflight.serial));
     story_native_people_pose_record::Poses poses=prepared_poses;
     unsigned captured=turn?prepared_count:0;
     if(preflight.preflight&&!turn){
      const story_native_people_pose_capture::Layout pose_layout{GD(0x101C4CD0),GD(0x100209C4)};
      const auto verify=[&](const auto&actor,const auto&identity){return resident_binding(actor,identity,*expected);};
      for(unsigned n=0;n<2;++n){
       if(!story_native_people_pose_capture::capture(read,verify,pose_layout,*expected,n,poses[n]))break;
       ++captured;
      }
     }
     const bool pose_stored=captured==2&&(turn?bool(session->staged_poses()):(session->stage_poses(poses)&&bool(session->staged_poses())));
     LOG("[native_wait] People pose baseline captured=%u stored=%u publication=disabled",captured,unsigned(pose_stored));
     const story_native_camera_capture::Layout camera_layout{
      GD(0x1046F0B0),GD(0x101D55F0),GD(0x101D5634),GD(0x101D5538),
      {GC(0x025021A4),GC(0x02502288),GC(0x024FFA3C),GC(0x0250235C),GC(0x024FFC40)},
      {GD(0x10049348),GD(0x10049350),GD(0x10044878),GD(0x1004487C)}};
     story_native_event_observation::Camera raw_camera;
     const bool camera_captured=story_native_camera_capture::capture(read,camera_layout,*expected,raw_camera)&&
      raw_camera==expected->camera;
     LOG("[native_wait] initial depth stored=%u raw camera baseline=%u publication=disabled",
         unsigned(initial_depth),unsigned(camera_captured));
     if(!initial_depth||!pose_stored||!camera_captured){active_depth.store(nullptr,std::memory_order_release);enabled.store(false,std::memory_order_release);}
     else if((selected_declared||env("WWHD_TEST_NATIVE_WAIT_RUN")=="1")&&!preparation_backends.empty()){
      std::shared_ptr<story_native_wait_runtime_model_graph::Context>prepared_model;

      if (env("WWHD_TEST_NATIVE_MODEL_GRAPH_DIAGNOSTIC") == "1" &&
          session && session->fence_held() && !preparation_backends.empty()) {
       namespace MG = story_native_people_model_graph;
       const auto want = *expected;
       const auto owner = story_native_stage_owner_observer::snapshot();
       auto stage_pin = story_native_stage_owner_observer::tracker().scan();
       const auto source = preparation_backends.back().sources;
       if (owner && stage_pin.valid() && stage_pin.receipt() == owner->receipt &&
           stage_pin.current()) {
        MG::Sources diagnostic_source;
        diagnostic_source.read = read;
        diagnostic_source.resident = source.actor;
        // Synchronous ephemeral Adapter: stage_pin and this lambda cannot escape.
        diagnostic_source.scope = [&, want, source, owner, tracker = retained](MG::Scope& out) {
         out = {};
         if (!session || !session->fence_held() ||
             active_context.load(std::memory_order_acquire) ||
             active_depth.load(std::memory_order_acquire) != tracker ||
             !outer_observer_registered || !guestmods::routes_owned() ||
             !stage_pin.valid() || stage_pin.receipt() != owner->receipt ||
             !stage_pin.current()) return false;
         const auto staged = session->staged_expected();
         const auto journal = session->journal();
         if (!staged || *staged != want || !session->staged_poses() || !journal ||
             journal->phase != story_native_wait_journal::Phase::Preparing || !source.session)
          return false;
         story_native_event_memory::Scope live;
         if (!source.session(live) || live.identity != want.identity ||
             !live.exclusive || !live.bindings_verified || live.foreign || live.fault ||
             live.npc_depth || live.camera_depth || live.manager_depth ||
             live.callback_depth || live.foreign_depth) return false;
         const auto depth = tracker->snapshot();
         if (!depth.coverage || depth.fault || depth.state != D::State::Active ||
             !depth.idle() || depth.serial != live.serial || !stage_pin.current())
          return false;
         out.expected = want;
         out.fenced = session->fence_held();
         out.stage_live = live.bindings_verified;
         out.resources_pinned = stage_pin.current();
         out.native_dispatch.reset(); // explicit diagnostic has no invocation authority
         out.preclaim = true; // all live preclaim checks above; no record-only substitute
         return true;
        };
        const auto dispatch_guard=guestmods::acquire_native_people_refresh_dispatch();
        LOG("[native_wait] refresh dispatch diagnostic acquired=%u current=%u functions=%u admission=disabled",
            unsigned(bool(dispatch_guard)),unsigned(dispatch_guard&&dispatch_guard->current()),
            unsigned(story_native_refresh_dispatch::required_functions().size()));
        MG::Adapter diagnostic(want, std::move(diagnostic_source));
        for (unsigned n = 0; n < 2; ++n) {
         const auto d = diagnostic.diagnose(n); // NEVER prepare(), capture(), or verify_model()
         LOG("[native_wait] graph diagnostic actor=%u parsed=%u refusal=%u actorptr=%08X morf=%08X model=%08X data=%08X fmdl=%08X skeleton=%08X animation=%08X bck=%08X public_publication=disabled",
             n, unsigned(d.parsed), unsigned(d.refusal), d.actor, d.morf, d.model,
             d.model_data, d.fmdl, d.skeleton, d.animation, d.bck);
         LOG("[native_wait] graph refusal actor=%u check=%u address=%08X observed=%08X required=%08X readBytes=%X samplerFlags=%X samplerKeys=%u animationEnd=%08X", n,unsigned(d.check),d.checked_address,d.observed,d.required,d.read_bytes,d.sampler_flags,d.sampler_keys,d.animation_end);
         LOG("[native_wait] graph resources actor=%u info=%08X archive=%08X fres=%08X metadataHeap=%08X span=%08X+%X parentHeap=%08X cacheHolder=%08X cacheResource=%08X payloadSpan=%08X+%X actualBuffer=%08X+%X logical=%X membership=%u public_publication=disabled",
             n, d.archive_info, d.archive, d.fres, d.data_heap, d.metadata.address,
             d.metadata.bytes, d.parent_heap, d.cache_holder, d.cache_resource,
             d.payload_heap.address, d.payload_heap.bytes, d.cache_buffer.address,
             d.cache_buffer.bytes, d.cache_size, unsigned(d.cache_membership));
        }
        // Diagnostic failure has no effect on normal private WAIT/TURN eligibility.
       }
      }

      if(selected_declared||env("WWHD_TEST_NATIVE_MODEL_GRAPH_PREPARE")=="1"){
       std::string why;
       const auto model_stage=story_native_stage_owner_observer::snapshot();
       if(model_stage)prepared_model=story_native_wait_runtime_model_graph::Context::prepare(
        *session,*retained,*expected,*model_stage,story_native_event_game_thread::ticket(c),
        c,preparation_backends.back(),why);
       else why="model Stage unavailable";
       LOG("[native_wait] model preclaim actual_prepared=%u reason=%s native_calls=0 refresh_admission=%u public_publication=disabled",
        unsigned(prepared_model&&prepared_model->prepared()),why.c_str(),
        unsigned(story_native_people_model_graph::Adapter::refresh_admission_enabled()));
      }
      const auto owner=story_native_stage_owner_observer::snapshot();
      if(selected_declared){
       selected_activation=NativeScenePlaybackIssuer::activation(selected_declared->request);
       if(!selected_activation){enabled=false;return;}
      }
      auto native=native_sources(session,resident_mod,resident_ids,selected_activation);
      native.model_graph=std::move(prepared_model);

      std::array<story_native_people_admission::Reference,2>refs;
      for(unsigned n=0;n<2;++n)refs[n]={resident_mod,resident_ids[n]};
      auto saved_model_probe=native.model_graph;
      auto context=owner?RC::Context::prepare(*session,*retained,*expected,session->arena(),*owner,
          story_native_event_game_thread::ticket(c),c,preparation_backends.back(),std::move(refs),std::move(native)):nullptr;
      if(context){
       auto*live=context.get();native_contexts.push_back(std::move(context));
       if(selected_declared)selected_declared->current=live;
       active_context.store(live,std::memory_order_release);
       // Prepare requires the exact camera baseline: start before this native
       // boundary returns, while the original staging fence is still held.
       const auto state=live->tick(c,true);
       LOG("[native_wait] native start state=%u retained=%u public_selection=%u",
           unsigned(state),unsigned(live->retained()),unsigned(story_native_scene_manifest::public_selection_enabled()));
       if(saved_model_probe&&state==RC::State::Started&&env("WWHD_TEST_NATIVE_MODEL_SAVED_CAPTURE")=="1"){
        std::string why;auto binding=saved_model_probe->bind(why);
        auto receipt=binding?saved_model_probe->capture_saved(*binding):std::nullopt;
        const bool matched=receipt&&saved_model_probe->matches_live(*binding,*receipt);
        LOG("[native_wait] saved model capture actual=%u live=%u spans=%zu bytes=%zu reason=%s native_calls=0",
         unsigned(bool(receipt)),unsigned(matched),receipt?receipt->span_count():0,
         receipt?receipt->byte_count():0,why.c_str());
       }
      }else LOG("[native_wait] native context refused before publication");
     }
    }else enabled.store(false,std::memory_order_release);
   }else enabled.store(false,std::memory_order_release);
  }
 }
}
story_native_restore_front::Front NativeResumeFrontIssuer::capture_load_front(){
 // Capture the actual global owner, including authenticated absence. The
 // per-slot capture resolver may still retain a retired historical Context.
 return story_native_restore_front::Front(active_context,active_depth,runtime_selection,runtime_selection);
}
story_native_restore_front::Front NativeResumeFrontIssuer::capture(
 story_native_wait_session::Session*owner,const std::string&mod,
 const std::array<std::string,2>&ids,std::shared_ptr<const story_native_scene_playback::Activation>activation){
 story_native_restore_front::Selection replacement;
 replacement.session=owner;replacement.mod=mod;replacement.ids=ids;
 replacement.frontend=bool(activation);replacement.activation=std::move(activation);
 for(const auto&slot:declared_slots)if(slot->owner.get()==owner)replacement.declared=slot.get();
 // All allocation/copies precede Context's final Tracker lock. The private
 // Front commits these exact actual globals with no callbacks or allocation.
 return story_native_restore_front::Front(active_context,active_depth,runtime_selection,std::move(replacement));
}
void logic(Cpu*c){service_selected(c,false);}
bool NativeScenePlaybackIssuer::install(){
 return story_native_scene_playback::Backend::install([](Cpu*c,const std::string&mod,const story_notebook::ManifestScene&scene){return start(c,mod,scene);});
}
story_native_scene_playback::Attempt NativeScenePlaybackIssuer::start(
 Cpu*c,const std::string&mod,const story_notebook::ManifestScene&scene){
 namespace P=story_native_scene_playback;
 auto fallback=[] {return P::Attempt(P::Attempt::Kind::WholeFallback);};
 auto deferred=[] {return P::Attempt(P::Attempt::Kind::Deferred);};
 auto denied=[] {return P::Attempt(P::Attempt::Kind::Denied);};
 if(!c||!frontend_installed||!enabled||!aggregate_save_gate||
    ss::native_gate::controller()!=aggregate_save_gate.get())return fallback();
 if(active_context.load(std::memory_order_acquire)||active_depth.load(std::memory_order_acquire))return denied();
 DeclaredSlot*slot=nullptr;
 for(const auto&candidate:declared_slots)
  if(candidate->request->mod()==mod&&candidate->request->scene()==scene.id){slot=candidate.get();break;}
 if(!slot||!slot->allocated||!slot->reusable||!slot->request->current()||slot->request->definition()!=scene){
  LOG("[native_wait] frontend preflight fallback declaration=%u scene=%s:%s",unsigned(bool(slot)),mod.c_str(),scene.id.c_str());return fallback();
 }
 // Readiness and census are read-only, before the private image or native
 // consumers can change. A refused selection still permits the whole fallback.
 if(!slot->owner->inspect(c)){
  auto hold=slot->owner->try_fence();
  if(hold){
   const auto check=slot->owner->readiness_check();
   const bool waiting=!slot->owner->quarantined()&&(check==story_native_wait_session::Session::Check::Control||
       check==story_native_wait_session::Session::Check::Manager||check==story_native_wait_session::Session::Check::Camera);
   LOG("[native_wait] frontend preflight readiness=%u quarantined=%u deferred=%u scene=%s:%s",
       unsigned(check),unsigned(slot->owner->quarantined()),unsigned(waiting),mod.c_str(),scene.id.c_str());
   if(check==story_native_wait_session::Session::Check::Control){
    const uint32_t at=GD(0x1046F0B0)+0x51D0;uint32_t a=0,b=0,d=0,e=0;
    std::array<uint8_t,2>flags{},partner{};
    if(read(at+0xC0,flags)&&read(at+0xD8,partner)&&scalar(at+0xC4,a)&&scalar(at+0xC8,b)&&scalar(at+0xCC,d)&&scalar(at+0xD0,e))
     LOG("[native_wait] pre-entry control mode=%u partners=%08X/%08X/%08X/%08X staff=%04X",
         unsigned(flags[0]),a,b,d,e,unsigned(uint16_t(partner[0])<<8|partner[1]));
   }
   if(waiting)return deferred();
  }
  return fallback();
 }
 {
  auto hold=slot->owner->try_fence();if(!hold)return fallback();
  const auto expected=slot->owner->expected();
  if(!expected)return fallback();
  const auto census=stage_census(c,*expected);
  if(!census.idle){LOG("[native_wait] frontend preflight fallback census=%u events=%u blocking=%u state=%u scene=%s:%s",
      unsigned(census.reason),census.events,census.blocking_index,census.blocking_state,mod.c_str(),scene.id.c_str());
   return census.reason==story_native_event_table_scan::Reason::Busy?deferred():fallback();}

 }
 session=slot->owner.get();selected_declared=slot;selected_frontend=true;
 resident_mod=slot->request->mod();resident_ids=slot->request->compiled().actor_ids();
 reported=false;depth_reported=false;first_depth_serial=0;
 service_selected(c,true);
 auto*context=active_context.load(std::memory_order_acquire);
 // Once staging/start was attempted, never silently execute the fallback.
 if(!context||!selected_activation||
    (context->state()!=RC::State::Started&&context->state()!=RC::State::Running))return denied();
 auto poll=[context](Cpu*){
  P::Progress progress;
  if(context->state()==RC::State::Released&&!context->retained()){
   progress.phase=P::Progress::Phase::Retired;progress.natural=context->take_completion();
  }else if(context->state()==RC::State::Quarantined||context->state()==RC::State::Refused){
   progress.phase=P::Progress::Phase::Quarantined;progress.reason=story_scenes::StopReason::Failed;
  }
  return progress;
 };
 P::RunOwnerCell::Endpoint endpoint{std::move(poll),[context](story_scenes::StopReason){context->request_cancel();}};
 auto cell=std::shared_ptr<P::RunOwnerCell>(new P::RunOwnerCell(selected_activation,context->run_identity(),std::move(endpoint)));
 if(!context->attach_run_owner(cell))return denied();
 auto run=std::unique_ptr<P::Run>(new P::Run(selected_activation,
     [cell](Cpu*cpu){return cell->poll(cpu);},
     [cell](story_scenes::StopReason reason){cell->queue_stop(reason);},cell));
 LOG("[native_wait] frontend owns scene=%s:%s logic_step=%llu",mod.c_str(),scene.id.c_str(),
     static_cast<unsigned long long>(clock_step?clock_step():0));
 return P::Attempt(P::Attempt::Kind::Owned,std::move(run));
}
void state_loaded(){
 if(auto*context=active_context.load(std::memory_order_acquire))context->invalidate();
 active_depth.store(nullptr,std::memory_order_release);
 depth_reported=false;
 if(owner_registered.load(std::memory_order_acquire))story_native_stage_owner_observer::state_loaded();
 reported=false;
 frame_cpu=nullptr;frame_address=0;frame_ticket={};
}
}
