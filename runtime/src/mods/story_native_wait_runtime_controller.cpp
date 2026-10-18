// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_runtime_controller.h"
#include "story_native_wait_run_footprint.h"
#include "story_native_wait_runtime_cancel_camera.h"
#include "story_native_wait_runtime_manager_cancel.h"
#include "story_native_wait_runtime_model_graph.h"
#include "story_native_restore_resume.h"
#include "story_native_restore_current_owner.h"
#include "story_native_restore_claim.h"
#include "story_native_restore_front.h"
#include "story_native_stage_restore_group.h"
#include "story_native_scene_run_owner.h"
#include "story_native_scene_checkpoint.h"
#include "story_native_scene_retire.h"
#include "savestate_native_finished_load.h"
#include "story_native_wait_session_idle.h"
#include "story_native_wait_saved_baseline.h"
#include "story_native_scene_activation.h"
#include "story_native_wait_runtime_cancel_consumers.h"
#include <cstdlib>
#include <charconv>
#include <thread>
#include "native_quiescence.h"
#include "story_native_wait_runtime_consumers.h"
#include "story_native_wait_runtime_route_capture.h"
#include "story_native_wait_runtime_terminal.h"
#include "story_native_wait_actor_cast.h"
#include "story_native_camera_capture.h"
#include "story_native_camera_policy.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <stdexcept>
namespace story_native_wait_runtime_controller {namespace {
namespace D=story_native_event_depth;namespace T=story_native_manager_terminal;
namespace C=story_native_camera_return;namespace R=story_native_wait_owned_routes;
namespace W=story_native_wait_controller;namespace M=story_native_event_memory;
namespace J=story_native_wait_journal;
namespace F=story_native_wait_run_footprint;
struct RunLife {std::shared_ptr<const story_native_scene_playback::Activation>activation;std::array<story_native_people_admission::Reference,2>references;std::weak_ptr<story_native_scene_playback::RunOwnerCell>owner;std::shared_ptr<const void>original_frontend_life;std::atomic<bool>alive{true};uint64_t last_restore_epoch=0,pending_epoch=0;bool restore_revoked=false;};
uint32_t word(std::span<const uint8_t>b,unsigned p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m){return uint64_t(a)<uint64_t(b)+m&&uint64_t(b)<uint64_t(a)+n;}
const story_native_camera_capture::Layout CameraLayout{GD(0x1046F0B0),GD(0x101D55F0),GD(0x101D5634),GD(0x101D5538),{GC(0x025021A4),GC(0x02502288),GC(0x024FFA3C),GC(0x0250235C),GC(0x024FFC40)},{GD(0x10049348),GD(0x10049350),GD(0x10044878),GD(0x1004487C)}};
}
struct SavedRunReceipt::Capture {
 // Original host preclaim provenance only. Its old game-thread ticket is
 // revoked by load; this cannot authorize bind/resume after restoration.
 std::shared_ptr<story_native_wait_runtime_model_graph::Context>preclaim_model;
 std::shared_ptr<RunLife>life;const void*issuer=nullptr;std::thread::id thread;
 std::shared_ptr<const threads::NativeQuiescence>saved_park;
 std::unique_ptr<story_native_wait_session::Session::Fence>fence;
 std::optional<story_native_wait_runtime_model_graph::Binding>model_binding;
 std::optional<story_native_model_saved_graph::Receipt>model_saved;
 std::optional<story_native_restore_resume::SavedRunOrigin>origin;
 Expected expected;Layout arena;Stage stage;F::Footprint footprint;
 std::optional<story_native_stage_owner::Tracker::Scan>stage_scan;
 std::optional<threads::QuiescenceWitness>parked;Cpu*requester=nullptr;
 std::optional<story_native_event_observation::Result> observation;
 M::Scope scope;bool committed=false,consumed=false,checkpoint_issued=false;
 ~Capture(){if((fence||stage_scan)&&thread!=std::this_thread::get_id())std::terminate();}
};
SavedRunCheckpoint::SavedRunCheckpoint(std::shared_ptr<const SavedRunReceipt::Capture>c):capture_(std::move(c)){}
bool SavedRunCheckpoint::matches_saved(const ss::native_gate::SavedView&view)const{
 try{return capture_&&capture_->committed&&capture_->origin&&F::matches_saved(capture_->arena,view,capture_->footprint)&&(!capture_->preclaim_model||(capture_->model_saved&&capture_->preclaim_model->matches_saved(*capture_->model_saved,view)));}catch(...){return false;}
}
std::optional<SavedRunReceipt>SavedRunCheckpoint::fork_attempt()const{
 try{if(!capture_||!capture_->committed||!capture_->origin)return {};
  auto p=std::make_shared<SavedRunReceipt::Capture>();p->preclaim_model=capture_->preclaim_model;p->life=capture_->life;p->issuer=capture_->issuer;p->thread=std::this_thread::get_id();
  p->origin=capture_->origin;p->expected=capture_->expected;p->arena=capture_->arena;p->stage=capture_->stage;p->footprint=capture_->footprint;p->observation=capture_->observation;p->scope=capture_->scope;p->committed=true;
  if(capture_->model_saved)p->model_saved=capture_->model_saved->fork_host();return SavedRunReceipt(std::move(p));
 }catch(...){return {};}
}
struct RunRestorePermit::Pending {
 Context*current_owner=nullptr;std::shared_ptr<RunLife>current_life;
 std::optional<story_native_restore_front::Front>current_front;
 std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>load_owner;
 std::shared_ptr<const story_native_restore_resume::CurrentOwnerRevocation>revocation;
 std::shared_ptr<story_native_wait_runtime_model_graph::Context>preclaim_model;
 std::optional<story_native_model_saved_graph::Receipt>model_saved;
 std::optional<story_native_restore_resume::SavedRunOrigin>origin;
 std::shared_ptr<RunLife>life;const void*issuer=nullptr;std::thread::id thread;uint64_t epoch=0;
 std::unique_ptr<story_native_wait_session::Session::Fence>fence;
 std::optional<story_native_stage_owner_observer::RestorePermit>stage;
 std::optional<story_native_stage_restore_group::StageRestoreGroup::Participant>stage_group;
 std::optional<ss::native_gate::SavedView>saved_view;
 F::Footprint footprint;M::Scope scope;std::optional<threads::QuiescenceWitness>parked;Cpu*requester=nullptr;bool consumed=false,suspend_attempted=false,suspended=false;
 ~Pending(){if(fence&&thread!=std::this_thread::get_id())std::terminate();if(life&&life->pending_epoch==epoch)life->pending_epoch=0;}
};
SavedRunReceipt::SavedRunReceipt(std::shared_ptr<Capture>p):capture_(std::move(p)){}
SavedRunReceipt::SavedRunReceipt(SavedRunReceipt&&)noexcept=default;
SavedRunReceipt&SavedRunReceipt::operator=(SavedRunReceipt&&)noexcept=default;
SavedRunReceipt::~SavedRunReceipt()=default;
RunRestorePermit::RunRestorePermit(std::unique_ptr<Pending>p):pending_(std::move(p)){}
RunRestorePermit::RunRestorePermit(RunRestorePermit&&)noexcept=default;
RunRestorePermit&RunRestorePermit::operator=(RunRestorePermit&&)noexcept=default;
RunRestorePermit::~RunRestorePermit()=default;
struct Context::Impl {
 story_native_wait_session::Session&session;D::Tracker&depth;
 std::shared_ptr<RunLife>run_life=std::make_shared<RunLife>();
 std::optional<story_native_scene_completion::Completion>natural_completion;
 const Expected expected;const Layout arena;const Stage stage;const Ticket ticket;Cpu*const cpu;
 const story_native_wait_runtime_backend::Bundle backend;
 std::shared_ptr<story_native_stage_binding::Cell>stage_cell;bool renewing_stage=false;
 const std::array<story_native_people_admission::Reference,2>refs;const Sources sources;
 story_native_people_pose_record::Poses poses{};
 uint32_t captured_ship=0,captured_ship_pid=0;
 std::atomic<bool>claim{false},fault{false},cancel{false},explicit_cancel{false};std::atomic<State>state{State::Ready};
 std::unique_ptr<W::Controller>controller;
 std::unique_ptr<story_native_people_bridge::Bridge>people;
 std::unique_ptr<story_native_wait_actor_cast::Cast>casting;
 std::unique_ptr<T::Terminal>terminal;std::unique_ptr<C::Barrier>camera;
 std::unique_ptr<story_native_event_end_guard::Guard>end;
 std::unique_ptr<R::Routes>routes;
 std::unique_ptr<story_native_wait_runtime_consumers::Adapter>consumers;
 std::unique_ptr<story_native_wait_runtime_route_capture::Adapter>route_capture;
 story_native_wait_consumer_write::Writer writer;
 std::optional<story_native_wait_runtime_terminal::Bundle>terminal_sources;
 story_native_wait_runtime_manager_cancel::Execution*active_cancel_execution=nullptr;
 std::optional<story_native_camera_cancel_barrier::Completion>cancel_returned;
 const story_native_cancel_observation::Result*cancel_release_proof=nullptr;
 const story_native_outer_return::OuterReturn*cancel_release_outer=nullptr;
 bool cancel_queue_probed=false;bool cancel_driver_done=false,cancel_driver_refused=false,cancel_cleanup_attempted=false,cancel_outer_logged=false;
 unsigned cancel_driver_attempts=0;
 bool restored_dormant=false,restore_open_attempted=false;
 std::optional<story_native_restore_resume::SavedRunOrigin>restored_origin;
 bool stopping=false,cancel_ready_probed=false,cancel_probe_refused=false;unsigned cancel_probe_attempts=0;
 bool cancel_execution_probed=false,cancel_execution_refused=false,cancel_model_pending_probed=false,cancel_model_pending_refused=false;unsigned cancel_execution_attempts=0;
 std::atomic<bool>diagnosed{false};std::atomic<uint32_t>trace_function{0},trace_receiver{0};std::array<std::atomic<uint32_t>,3>trace_args{};std::array<std::array<std::atomic<uint64_t>,7>,2>owned_entries{};
 static thread_local Impl*native_terminal;
 static thread_local Impl*native_control;
 Impl(story_native_wait_session::Session&s,D::Tracker&d,Expected e,Layout a,Stage st,Ticket t,Cpu*c,story_native_wait_runtime_backend::Bundle b,std::array<story_native_people_admission::Reference,2>r,Sources so):session(s),depth(d),expected(std::move(e)),arena(a),stage(st),ticket(t),cpu(c),backend(std::move(b)),refs(std::move(r)),sources(std::move(so)){run_life->activation=sources.activation;run_life->references=refs;}
 bool held()const{return session.fence_held();}
 std::optional<Stage>current_stage(const story_native_stage_owner::Tracker::Scan&pin)const{
  if(backend.stage_binding){if(!backend.stage_binding->matches_original(stage))return {};return backend.stage_binding->current(pin);}
  const auto now=story_native_stage_owner_observer::snapshot(pin);return now&&now->control==stage.control&&now->receipt==stage.receipt?now:std::nullopt;
 }
 bool retained(const Expected&e)const noexcept{return e==expected&&claim.load(std::memory_order_acquire)&&controller&&controller->retained(e);}
 bool thread(const Cpu*c)const noexcept{return c==cpu&&story_native_event_game_thread::verify(c,ticket);}
 bool activation_valid()const{
  if(!sources.activation)return true;const auto&d=sources.activation->declaration();
  if(!d||!d->current()||sources.activation->definition_hash()!=d->definition_hash()||!expected.program||d->compiled().program()!=*expected.program||d->compiled().room()!=expected.room_number)return false;
  const auto&name=d->compiled().stage();if(name.empty()||name.size()>expected.stage_name.size())return false;
  for(unsigned n=0;n<expected.stage_name.size();++n)if(expected.stage_name[n]!=(n<name.size()?uint8_t(name[n]):0))return false;
  for(unsigned n=0;n<2;++n){const auto&a=d->actors()[n];if(refs[n].mod!=d->mod()||refs[n].id!=d->compiled().actor_ids()[n]||a.token!=expected.actors[n].token||a.fingerprint!=expected.actors[n].fingerprint||a.subtype!=expected.actors[n].subtype)return false;}
  return true;
 }
 bool raw_camera(story_native_event_observation::Camera&c)const{return story_native_camera_capture::capture(backend.sources.read,CameraLayout,expected,c);}
 bool live_receipt()const{
  if(!held()||!controller||!casting)return false;
  std::array<uint8_t,4>bytes{};if(!backend.sources.read(expected.identity.image+0xE4,bytes))return false;
  const auto state=word(bytes);
  if(restored_dormant){const auto baseline=session.baseline();const auto d=depth.snapshot();return restored_origin&&baseline&&*baseline==expected&&d.state==D::State::Draining&&d.idle()&&!d.fault&&d.coverage&&(state==1||state==2||state==4);}
  if(restored_origin&&controller->restored_active(expected)){const auto d=depth.snapshot();return (d.state==D::State::Active||d.state==D::State::Draining)&&d.coverage&&!d.fault&&(state==0||state==1||state==2||state==4);}
  const bool initialized=casting->initialized(expected);
  if(state==2||state==4)return initialized;
  if(state==1)return !initialized&&(controller->state()==W::State::Empty||controller->state()==W::State::Started);
  if(state==0){const auto journal=session.journal();return initialized||(controller->state()==W::State::Empty&&journal&&journal->phase==J::Phase::Preparing);}
  return false;
 }
 bool scope(M::Scope&s)const{return held()&&thread(cpu)&&backend.sources.session&&backend.sources.session(s)&&live_receipt()&&s.identity==expected.identity&&s.exclusive&&s.bindings_verified&&!s.fault&&!s.foreign;}
 void diagnose(const char*where,const Cpu*c,uint32_t function,uint32_t receiver,bool source_ok=false,const M::Scope*observed=nullptr)noexcept{
  if(diagnosed.exchange(true))return;
  try{
   const auto d=depth.snapshot();const auto parent=depth.current_owned_call();
   M::Scope sampled;const bool actual_source=observed?source_ok:(held()&&backend.sources.session&&backend.sources.session(sampled));const auto&s=observed?*observed:sampled;
   auto diagnostic_pin=story_native_stage_owner_observer::tracker().try_scan();
   const auto owner=story_native_stage_owner_observer::snapshot(diagnostic_pin);
   const auto wanted=current_stage(diagnostic_pin);
   const bool stage_match=owner&&wanted&&owner->control==wanted->control&&owner->receipt==wanted->receipt;
   const auto baseline=held()?session.baseline():std::optional<Expected>{};
   LOG("[native_wait] first fault where=%s fn=%08X receiver=%08X current_r3=%08X state=%u retained=%u held=%u thread=%u source=%u baseline=%u stage=%u parent=%u parent_receiver=%08X",
       where,function,receiver,c?c->r[3]:0,unsigned(state.load()),unsigned(retained(expected)),unsigned(held()),unsigned(thread(c)),unsigned(actual_source),unsigned(baseline&&*baseline==expected),unsigned(stage_match),parent?unsigned(parent->entry):~0u,parent?parent->receiver:0);
   LOG("[native_wait] first fault depth serial=%llu state=%u coverage=%u fault=%u npc=%u camera=%u manager=%u callback=%u foreign=%u scope identity=%u exclusive=%u bindings=%u fenced=%u fault=%u foreign=%u serial=%llu step=%llu",
       static_cast<unsigned long long>(d.serial),unsigned(d.state),unsigned(d.coverage),unsigned(d.fault),d.npc_depth,d.camera_depth,d.manager_depth,d.callback_depth,d.foreign_depth,unsigned(s.identity==expected.identity),unsigned(s.exclusive),unsigned(s.bindings_verified),unsigned(s.fenced),unsigned(s.fault),unsigned(s.foreign),static_cast<unsigned long long>(s.serial),static_cast<unsigned long long>(s.logic_step));
   LOG("[native_wait] first fault helper routes=%u people=%u barrier=%u terminal=%u cancel=%u terminal_bundle=%u cpu=%p expected_cpu=%p sp=%08X",
       unsigned(routes&&routes->fault()),unsigned(people&&people->fault()),unsigned(camera&&camera->quarantined()),unsigned(terminal&&terminal->quarantined()),unsigned(cancel.load()),unsigned(bool(terminal_sources)),static_cast<const void*>(c),static_cast<const void*>(cpu),c?c->r[1]:0);
   for(unsigned n=0;n<2;++n)LOG("[native_wait] owned entry counts actor=%u execute=%llu event_move=%llu common=%llu wait_start=%llu wait_process=%llu",n,static_cast<unsigned long long>(owned_entries[n][0].load()),static_cast<unsigned long long>(owned_entries[n][1].load()),static_cast<unsigned long long>(owned_entries[n][2].load()),static_cast<unsigned long long>(owned_entries[n][3].load()),static_cast<unsigned long long>(owned_entries[n][4].load()));
   for(unsigned n=0;n<2;++n)LOG("[native_wait] fault TURN entry counts actor=%u start=%llu process=%llu",n,static_cast<unsigned long long>(owned_entries[n][5].load()),static_cast<unsigned long long>(owned_entries[n][6].load()));
   LOG("[native_wait] first fault casting=%u initialized=%u",unsigned(casting&&casting->fault()),unsigned(casting&&casting->initialized(expected)));
   const bool captured=trace_function.load()==function&&trace_receiver.load()==receiver;
   const std::array<uint32_t,3>args={captured?trace_args[0].load():(c?c->r[4]:0),captured?trace_args[1].load():(c?c->r[5]:0),captured?trace_args[2].load():(c?c->r[6]:0)};
   std::array<char,33>name{};bool name_read=true,terminated=false;unsigned name_bytes=0;
   if(args[0]<mem::kMem2Start||uint64_t(args[0])+32>mem::kMem2End)name_read=false;
   if(name_read)for(unsigned n=0;n<32;++n){std::array<uint8_t,1>byte{};if(!backend.sources.read(args[0]+n,byte)){name_read=false;break;}if(!byte[0]){terminated=true;break;}name[n]=byte[0]>=32&&byte[0]<127?char(byte[0]):'.';++name_bytes;}
   LOG("[native_wait] first fault args captured=%u entry_r4=%08X entry_r5=%08X entry_r6=%08X current_r4=%08X current_r5=%08X current_r6=%08X name_read=%u terminated=%u bytes=%u native_name=%.32s",
       unsigned(captured),args[0],args[1],args[2],c?c->r[4]:0,c?c->r[5]:0,c?c->r[6]:0,unsigned(name_read),unsigned(terminated),name_bytes,name.data());
   std::array<uint8_t,0x328>actor{};const uint32_t actor_at=args[1];
   const bool actor_read=actor_at>=mem::kMem2Start&&uint64_t(actor_at)+actor.size()<=mem::kMem2End&&backend.sources.read(actor_at,actor);
   std::array<uint8_t,4>ship_word{},ship_pid{};const bool ship_global_read=backend.sources.read(GD(0x1046F0B0)+0x5B3C,ship_word);const uint32_t ship_at=word(ship_word);
   const bool ship_pid_read=ship_global_read&&ship_at>=mem::kMem2Start&&uint64_t(ship_at)+8<=mem::kMem2End&&backend.sources.read(ship_at+4,ship_pid);
   LOG("[native_wait] first fault actor r5=%08X read=%u pid=%08X class=%04X profile=%08X vtable=%08X phase=%u room=%u status=%08X ship_global=%08X/%08X global_read=%u pid_read=%u captured_ship=%08X/%08X captured_link=%08X/%08X",
       actor_at,unsigned(actor_read),word(actor,4),unsigned(uint16_t(actor[0xE])<<8|actor[0xF]),word(actor,0x10),word(actor,0xB4),unsigned(actor[0xC]),unsigned(actor[0x326]),word(actor,0x2E0),ship_at,word(ship_pid),unsigned(ship_global_read),unsigned(ship_pid_read),captured_ship,captured_ship_pid,backend.config.memory.link,backend.config.memory.link_pid);
   std::array<uint8_t,0x520>image{};std::array<uint8_t,4>camera_play{},current_event{};
   const bool image_read=backend.sources.read(expected.identity.image,image);
   const bool manager_read=backend.sources.read(expected.identity.manager+0x20,camera_play)&&backend.sources.read(expected.identity.manager+0x53C,current_event);
   LOG("[native_wait] first fault event image_read=%u state=%u manager_read=%u camera_play=%u current_event=%08X",unsigned(image_read),word(image,0xE4),unsigned(manager_read),word(camera_play),word(current_event));
   if(image_read)for(unsigned n=0;n<4;++n){const unsigned at=0xF0+n*0x50+0x38;
    LOG("[native_wait] first fault staff=%u mutable=%08X/%08X/%08X/%08X",n,word(image,at),word(image,at+4),word(image,at+8),word(image,at+12));}

  }catch(...){LOG("[native_wait] first fault diagnostic exception fn=%08X receiver=%08X",function,receiver);}
 }
 std::shared_ptr<const void>frontend_identity()const{return run_life->original_frontend_life?run_life->original_frontend_life:std::shared_ptr<const void>(run_life);}
 bool saved_frontend_current(const story_native_restore_resume::SavedRunOrigin&o){
  const auto&saved=o.frontend_checkpoint();if(!sources.activation)return !saved;
  auto owner=run_life->owner.lock();if(!saved||!owner||saved->activation()!=sources.activation||saved->cell()!=owner||saved->original_run_life()!=frontend_identity()||!saved->definition_current()||!activation_valid())return false;
  auto fresh=story_scenes::capture_native_checkpoint(*sources.activation,owner);
  return fresh&&fresh->activation()==saved->activation()&&fresh->cell()==saved->cell()&&fresh->original_run_life()==saved->original_run_life()&&fresh->mod()==saved->mod()&&fresh->definition()==saved->definition()&&fresh->scene_index()==saved->scene_index()&&fresh->generation()==saved->generation();
 }
 bool saved_origin_current(const story_native_restore_resume::SavedRunOrigin&o){
  if(!saved_frontend_current(o))return false;
  const auto d=depth.snapshot();M::Scope now;
  if(o.expected()!=expected||!run_life->alive||d.state!=D::State::Active||!d.coverage||!d.idle()||!scope(now)||now.fenced||now.logic_step!=o.scope().logic_step||now.serial!=o.scope().serial||now.link!=o.scope().link||now.link_pid!=o.scope().link_pid||depth.record()!=o.depth_record()||!D::Tracker::valid_idle_record(o.depth_record(),expected))return false;
  const bool turn=expected.program&&expected.program->kind()==story_native_event_program::Kind::TurnCamera;
  if(turn!=bool(o.clock()))return false;
  if(turn){story_native_event_collect::Clock current;if(!backend.config.collector.clock||!backend.config.collector.clock(current)||current!=*o.clock()||current.identity!=expected.identity||current.logic_step!=now.logic_step||current.serial!=now.serial)return false;}
  M::Scope after;return scope(after)&&after.logic_step==now.logic_step&&after.serial==now.serial&&depth.record()==o.depth_record();
 }
 bool capture_active(story_native_event_observation::Result&result,M::Scope&at){
  namespace O=story_native_event_observation;
  const auto d=depth.snapshot();if(!retained(expected)||fault||!scope(at)||!d.coverage||!d.idle()||d.state!=D::State::Active)return false;
  auto allocation=[&](uint32_t image,uint32_t bytes,uint32_t stamp,uint32_t generation){return image==expected.identity.image&&bytes==expected.identity.bytes&&stamp==backend.config.memory.stamp&&generation==expected.identity.generation&&backend.sources.allocation(expected.identity,backend.config.memory);};
  auto camera_guard=[&](const O::Camera&c,const auto&i){return story_native_camera_policy::verify(CameraLayout.tables,backend.sources.read,c,i).valid;};
  auto guards=[&](story_native_event_collect::Guards&g){M::Scope now;if(!scope(now)||now.fenced||now.manager_depth||now.camera_depth||now.npc_depth||now.callback_depth||now.foreign_depth)return false;g={};g.exclusive=true;return true;};
  story_native_event_collect::Collector collector(expected,backend.config.collector,backend.sources.read,backend.sources.actor,allocation,camera_guard,guards);
  O::Captured current;if(!collector.collect(current))return false;
  result=O::validate(current.image[0xE7]==1?O::Operation::Publish:O::Operation::Running,expected,current);
  M::Scope after;return result.valid()&&scope(after)&&at.logic_step==after.logic_step&&at.serial==after.serial&&at.link==after.link&&at.link_pid==after.link_pid;
 }
 void invalidate()noexcept{
  if(!fault.load())diagnose("invalidate",cpu,trace_function,trace_receiver);
  if(!fault.exchange(true))LOG("[native_wait] runtime context quarantined state=%u",unsigned(state.load()));cancel.store(true);state.store(State::Quarantined);
  try{auto d=depth.snapshot();if(d.state==D::State::Active&&d.idle())depth.begin_drain();}catch(...){}
  if(!run_life->restore_revoked&&!restored_dormant){run_life->alive=false;try{if(held())session.quarantine_native();}catch(...){}}
 }
 void protect(story_native_event_invoke::Config&c)const{
  c.protected_ranges.push_back({arena.allocation.address,arena.allocation.bytes});
  c.protected_ranges.push_back({expected.identity.manager-0xF4,0x634});
  for(unsigned n=0;n<2;++n){c.protected_ranges.push_back({expected.actors[n].address,0x920});c.protected_ranges.push_back({poses[n].identity.morf,0xC8});c.protected_ranges.push_back({poses[n].identity.model,0xBC});}
  c.protected_ranges.push_back({expected.camera.address-0x248,0xB28});
 }
 bool stack(Cpu*c,story_native_event_invoke::Config&out,bool lazy){
  if(!held()||!thread(c)||!sources.stack||!sources.stack(c,out))return false;
  protect(out);if(out.protected_ranges.size()>16)return false;
  return !lazy||ensure_terminal();
 }
 bool boundary(story_native_camera_restore::Boundary&out){
  out={};M::Scope s;if(!retained(expected)||!scope(s))return false;
  const auto d=depth.snapshot();if(!d.coverage||!d.idle()||d.state!=D::State::Draining||!s.fenced||s.serial!=d.serial)return false;
  std::array<uint8_t,0xF4>control{};std::array<uint8_t,0x20>manager{};
  if(!backend.sources.read(expected.identity.manager-0xF4,control)||!backend.sources.read(expected.identity.manager,manager))return false;
  const auto base=expected.identity.image;std::array<uint32_t,8>p={base,base+0x40,base+0xF0,base+0x230,base+0x410,0,base+0x510,0};if(expected.program){const auto offsets=expected.program->pool_offsets();for(unsigned n=0;n<8;++n)p[n]=offsets[n]?base+offsets[n]:n?0:base;}
  for(unsigned n=0;n<8;++n)if(word(manager,n*4)!=p[n])return false;
  out.identity=expected.identity;out.exclusive=out.fenced=out.manager_owned=true;out.logic_step=s.logic_step;out.serial=s.serial;
  out.control_mode=control[0xC2];out.partner1=word(control,0xC4);out.partner2=word(control,0xC8);out.talk_partner=word(control,0xCC);out.item_partner=word(control,0xD0);const uint32_t id=uint32_t(control[0xD8])<<8|control[0xD9];out.event_id=id&0x8000?int32_t(id)-65536:int32_t(id);
  return control[0xC0]==0&&out.control_mode==3&&out.partner1==~0u&&out.partner2==~0u&&out.talk_partner==~0u&&out.item_partner==~0u&&out.event_id==-1;
 }
 bool detach(Cpu*c,const Expected&e){
  if(c!=cpu||e!=expected||!consumers)return false;
  auto source=consumers->source();story_native_wait_consumer_write::State live;
  if(!source.capture(live)){LOG("[native_wait] consumer restore capture refused");return false;}
  auto plan=story_native_wait_detach_plan::prepare(expected,poses,live.people,live.camera,live.boundary,source.camera,true,true,true,false);
  if(!plan.valid()){LOG("[native_wait] consumer restore plan refused camera_type=%u next=%u style=%u bg=%u trim=%u",live.camera.current_type,live.camera.next_type,live.camera.style,live.camera.bg,live.camera.trim);return false;}
  const auto result=writer.apply(plan,expected,poses,source);LOG("[native_wait] consumer restore result=%u",unsigned(result));return result==story_native_wait_consumer_write::Result::Complete;
 }
 bool end_proc(Cpu*c,uint32_t fn,uint32_t manager,int32_t index,uint32_t force){
  // This capability exists solely inside Terminal's actual synchronous TLS
  // close scope. It cannot turn a caller's Context boolean into native authority.
  if(!held()||!thread(c)||fault||fn!=GC(0x0254465C)||manager!=expected.identity.manager||index||force||!terminal||!terminal->native_close_scope()||native_terminal)return false;
  M::Scope s;story_native_event_invoke::Config config;
  if(!retained(expected)||!scope(s)||!s.fenced||!depth.snapshot().idle()||!stack(c,config,false)||!sources.routes||!sources.routes())return false;
  const uint32_t sp=c->r[1];if((sp&15)||sp<0x40)return false;const uint32_t frame=sp-0x40;
  if(!config.stack.address||!config.stack.bytes||frame<config.stack.address||uint64_t(frame)+0x40>uint64_t(config.stack.address)+config.stack.bytes)return false;
  for(const auto&r:config.protected_ranges)if(!r.address||!r.bytes||uint64_t(r.address)+r.bytes>0x100000000ull||overlap(frame,0x40,r.address,r.bytes))return false;
  const Cpu saved=*c;std::array<uint8_t,0x40>before{},after{};bool called=false,restored=false;
  try{
   if(!backend.sources.read(frame,before))return false;
   native_terminal=this;
   try{sources.call(c,fn,{manager,0,0});called=true;}catch(...){called=false;}
   native_terminal=nullptr;
   restored=sources.write(frame,before)&&backend.sources.read(frame,after)&&after==before;
  }catch(...){native_terminal=nullptr;restored=false;}
  *c=saved;
  M::Scope after_scope;
  if(!called||!restored||!terminal->native_close_scope()||!thread(c)||!sources.routes()||!scope(after_scope)||!after_scope.fenced||after_scope.serial!=s.serial||after_scope.logic_step!=s.logic_step||!depth.snapshot().idle()){invalidate();return false;}
  return true;
 }
 bool ensure_terminal(){
  if(terminal_sources)return true;
  if(!controller||!retained(expected)||!session.baseline())return false;
  story_native_wait_runtime_terminal::Callbacks callbacks;
  callbacks.retained=[this](const Expected&e){return retained(e);};
  callbacks.end_proc=[this](Cpu*c,uint32_t f,uint32_t manager,int32_t index,uint32_t force){return end_proc(c,f,manager,index,force);};
  callbacks.completion=[this]{return terminal?terminal->completion():T::Completion{};};
  callbacks.camera_route=[this]{return sources.routes&&sources.routes();};
  callbacks.quarantine=[this]{invalidate();};
  auto terminal_backend=backend.sources;terminal_backend.session=[this](M::Scope&s){return backend.sources.session(s)&&live_receipt();};
  auto bundle=story_native_wait_runtime_terminal::make(session,depth,expected,arena,stage,ticket,cpu,backend.config,std::move(terminal_backend),std::move(callbacks),backend.stage_binding);
  if(!bundle)return false;
  T::Identity identity;identity.lease=expected.identity;identity.event=expected.identity.image+0x40;identity.index=0;
  for(unsigned n=0;n<2;++n){const auto&a=expected.actors[n];identity.actors[n]={a.address,a.pid,a.token,a.profile,a.subtype,poses[n].status,a.fingerprint};}
  if(bundle->identity!=identity)return false;
  terminal_sources=std::move(bundle);LOG("[native_wait] runtime terminal sources sealed");return true;
 }
 bool initialize(std::shared_ptr<const threads::NativeQuiescence>stage_park={}){
  if(!held()||!thread(cpu)||!activation_valid()||!sources.write||!sources.call||!sources.stack||!sources.routes||!sources.routes()||!backend.sources.read||!backend.sources.actor||!backend.sources.session){LOG("[native_wait] initialize refused prerequisites held=%u thread=%u activation=%u write=%u call=%u stack=%u routes=%u",unsigned(held()),unsigned(thread(cpu)),unsigned(activation_valid()),unsigned(bool(sources.write)),unsigned(bool(sources.call)),unsigned(bool(sources.stack)),unsigned(bool(sources.routes)));return false;}
  auto baseline=restored_dormant?session.baseline():session.staged_expected();auto p=restored_dormant?session.baseline_poses():session.staged_poses();if(!baseline||*baseline!=expected||!p||session.arena()!=arena){LOG("[native_wait] initialize refused baseline expected=%u poses=%u arena=%u",unsigned(baseline&&*baseline==expected),unsigned(bool(p)),unsigned(session.arena()==arena));return false;}poses=*p;
  if(backend.stage_binding){
   if(!stage_park){std::string why;auto q=threads::try_native_quiescence(cpu,ticket,100,why);if(!q)return false;stage_park=std::make_shared<threads::NativeQuiescence>(std::move(*q));}
   if(stage_park->read_only()||!stage_park->current(cpu))return false;
   auto pin=story_native_stage_owner_observer::tracker().try_scan();const auto now=story_native_stage_owner_observer::snapshot(pin);
   if(!now||now->control!=stage.control||now->receipt!=stage.receipt)return false;
   auto cell=story_native_stage_binding::Cell::create(pin,stage,cpu,ticket,run_life,stage_park);
   if(!cell||!backend.stage_binding->attach(cell,*stage_park,pin))return false;stage_cell=std::move(cell);
  }
  // Capture the actual global Ship binding before any native claim. This is
  // immutable identity input to the separate typed retail proof, never a
  // generic actor-memory or lifetime authority.
  std::array<uint8_t,4>ship{},ship_pid{};
  if(backend.sources.read(GD(0x1046F0B0)+0x5B3C,ship)){
   const uint32_t at=word(ship);
   if(!(at&3)&&at>=mem::kMem2Start&&uint64_t(at)+0x328<=mem::kMem2End&&backend.sources.read(at+4,ship_pid)&&word(ship_pid)&&word(ship_pid)!=~0u){captured_ship=at;captured_ship_pid=word(ship_pid);}
  }
  T::Identity identity;identity.lease=expected.identity;identity.event=expected.identity.image+0x40;identity.index=0;
  for(unsigned n=0;n<2;++n){const auto&a=expected.actors[n];identity.actors[n]={a.address,a.pid,a.token,a.profile,a.subtype,poses[n].status,a.fingerprint};}
  terminal=std::make_unique<T::Terminal>(identity,[this](T::Observation&o){return terminal_sources&&terminal_sources->probe(o);},[this](uint32_t a,T::Actor&o){return terminal_sources&&terminal_sources->actor(a,o);},[this](uint32_t m,int32_t i,uint32_t f){if(!terminal_sources)throw std::runtime_error("native WAIT terminal unavailable");terminal_sources->invoke(m,i,f);});
  C::Sources camera_source;
  camera_source.capture=[this](const Cpu*c,C::Capture&o){return terminal_sources&&held()&&terminal_sources->camera.capture(c,o);};
  camera_source.verify_camera=[this](const auto&c,const auto&i){return terminal_sources&&terminal_sources->camera.verify_camera(c,i);};
  camera_source.owned_thread=[this](const Cpu*c){return thread(c);};camera_source.route=[this]{return terminal_sources&&sources.routes&&sources.routes();};
  camera=std::make_unique<C::Barrier>(expected.identity,expected.camera,std::move(camera_source));
  auto end_identity=story_native_event_end_guard::Identity{expected.identity.manager,expected.identity.image,expected.identity.bytes,expected.identity.generation,expected.identity.program,0,expected.identity.stage,expected.identity.room};
  end=std::make_unique<story_native_event_end_guard::Guard>(end_identity);
  story_native_wait_runtime_consumers::Sources consumer_source;
  consumer_source.read=backend.sources.read;consumer_source.write=sources.write;consumer_source.resident=backend.sources.actor;
  consumer_source.retained=[this](const Expected&e){return retained(e);};consumer_source.boundary=[this](auto&b){return boundary(b);};consumer_source.quarantine=[this]{invalidate();};
  consumers=std::make_unique<story_native_wait_runtime_consumers::Adapter>(expected,poses,std::move(consumer_source));if(!consumers->configured()){LOG("[native_wait] initialize refused consumers");return false;}
  story_native_wait_runtime_route_capture::Sources route_source;
  route_source.read=backend.sources.read;route_source.resident=backend.sources.actor;route_source.retained=[this](const Expected&e){return retained(e);};route_source.camera=[this](auto&c){return raw_camera(c);};route_source.pending_order=[this](const Expected&e){auto j=session.journal();return held()&&e==expected&&retained(e)&&controller&&controller->state()==W::State::Started&&j&&j->phase==J::Phase::Published;};
  if(captured_ship&&captured_ship_pid)route_source.ship=story_native_wait_runtime_route_capture::Ship{captured_ship,captured_ship_pid};
  route_capture=std::make_unique<story_native_wait_runtime_route_capture::Adapter>(expected,std::move(route_source));
  story_native_people_bridge::Sources people_source;
  people_source.admission.read=backend.sources.read;people_source.admission.write=sources.write;
  people_source.admission.lease=[this](const auto&i){M::Scope s;return i==expected.identity&&retained(expected)&&!fault&&scope(s);};
  people_source.admission.current_scope=[this](const auto&a,const auto&i){return depth.current_actor_scope(a,i);};
  people_source.admission.detach_boundary=[this](const auto&i){story_native_camera_restore::Boundary b;return i==expected.identity&&boundary(b);};
  if(!stack(cpu,people_source.stack,false)){LOG("[native_wait] initialize refused stack");return false;}
  people_source.stack_read=backend.sources.read;people_source.stack_write=sources.write;people_source.guest_call=sources.call;
  people_source.verify_stack=[this](const Cpu*c,const story_native_event_invoke::Config&expected_stack){story_native_event_invoke::Config now;if(!stack(const_cast<Cpu*>(c),now,false)||now.stack.address!=expected_stack.stack.address||now.stack.bytes!=expected_stack.stack.bytes||now.protected_ranges.size()!=expected_stack.protected_ranges.size())return false;for(unsigned n=0;n<now.protected_ranges.size();++n)if(now.protected_ranges[n].address!=expected_stack.protected_ranges[n].address||now.protected_ranges[n].bytes!=expected_stack.protected_ranges[n].bytes)return false;return true;};
  people_source.verify_routes=sources.routes;people_source.verify_owned_thread=[this](const Cpu*c){return thread(c)&&!fault;};people_source.queue_cancel=[this](const auto&i){if(i!=expected.identity)return false;invalidate();return true;};
  people=std::make_unique<story_native_people_bridge::Bridge>(expected,refs,std::move(people_source),depth);if(!people->configured()){LOG("[native_wait] initialize refused people");return false;}
  story_native_wait_actor_cast::Sources cast_sources;
  cast_sources.read=backend.sources.read;
  cast_sources.retained=[this](const Expected&e){return retained(e);};
  cast_sources.owned_thread=[this](const Cpu*c){
   story_native_event_invoke::Config stack_config;M::Scope observed;
   return held()&&thread(c)&&!fault&&stack(const_cast<Cpu*>(c),stack_config,false)&&backend.sources.session(observed)&&observed.identity==expected.identity&&observed.exclusive&&observed.bindings_verified&&!observed.fault;
  };
  cast_sources.resident=backend.sources.actor;cast_sources.queue_cancel=[this]{invalidate();};
  casting=std::make_unique<story_native_wait_actor_cast::Cast>(expected,depth,std::move(cast_sources));if(!casting->configured()){LOG("[native_wait] initialize refused casting");return false;}
  R::Sources route_sources;
  route_sources.retained=[this](const Expected&e){return retained(e);};route_sources.owned_thread=[this](const Cpu*c){return thread(c)&&!fault;};route_sources.actor_identity=[this](const auto&a){return backend.sources.actor(a,expected.identity);};route_sources.camera_identity=[this]{story_native_event_observation::Camera c;return raw_camera(c);};
  route_sources.retail_safe=[this](const Cpu*c,uint32_t f){return route_capture->retail_safe(c,f);};route_sources.end_observation=[this](Cpu*c,auto&o){return route_capture->end_observation(c,o);};route_sources.queue_cancel=[this]{cancel.store(true);};route_sources.terminal_invoking=[this]{return native_terminal==this&&terminal->native_close_scope();};route_sources.control_release=[this]{return native_control==this;};
  routes=std::make_unique<R::Routes>(expected,depth,*people,*terminal,*camera,*end,std::move(route_sources));
  W::Sources controller_sources;controller_sources.validation=backend.sources;controller_sources.validation.session=[this](M::Scope&s){return backend.sources.session(s)&&live_receipt();};controller_sources.camera_tables=CameraLayout.tables;controller_sources.write=sources.write;
  controller_sources.stack=[this](Cpu*c,auto&config){return stack(c,config,true);};
  controller_sources.call=[this](Cpu*c,uint32_t f,std::initializer_list<uint32_t>args){
   const bool control=f==GC(0x0253EFA8);if(control){auto j=session.journal();
    const bool normal=stopping&&terminal->completed()&&camera->returned();
    const bool cancelled=cancel_release_proof&&cancel_release_outer&&cancel_release_proof->phase()==story_native_cancel_observation::Phase::Detached&&cancel_release_proof->matches(expected)&&cancel_release_proof->current_binding(c,depth,*cancel_release_outer);
    if(args.size()!=1||*args.begin()!=expected.identity.manager-0xF4||(!normal&&!cancelled)||!j||j->phase!=J::Phase::Detaching||native_control)throw std::runtime_error("native WAIT control release boundary");native_control=this;
   }
   LOG("[native_wait] runtime native call=%08X",f);
   try{auto value=sources.call(c,f,args);LOG("[native_wait] runtime native returned=%08X value=%u",f,value);if(control)native_control=nullptr;return value;}catch(...){LOG("[native_wait] runtime native exception=%08X",f);if(control)native_control=nullptr;throw;}
  };
  controller_sources.claim=[this](const Expected&e,const Layout&a,const Stage&s){if(e!=expected||a!=arena||s.control!=stage.control||s.receipt!=stage.receipt||fault)return false;claim.store(true,std::memory_order_release);return true;};
  controller_sources.retained=[this](const Expected&e){return e==expected&&claim.load(std::memory_order_acquire)&&!fault;};
  controller_sources.detach=[this](Cpu*c,const Expected&e){return detach(c,e);};controller_sources.quarantine=[this]{invalidate();};
  // Keep the context resident after release; no deallocation is authorized here.
  controller_sources.arm_clock=backend.arm_clock;
  controller_sources.release=[this](const Expected&e){if(e!=expected)return false;claim.store(false,std::memory_order_release);return true;};
  controller_sources.cancel_rearm=[this](Cpu*c,const Expected&e,const M::Scope&origin){
   const char*reason="exception";std::string why;
   const bool okay=[&](){try{
   const auto deny=[&](const char*stage){reason=stage;return false;};
   if(c!=cpu||e!=expected||!thread(c)||!held()||fault||retained(e)||claim.load(std::memory_order_acquire)||
      state!=State::Released||!controller||controller->state()!=W::State::Released||
      !run_life->alive||run_life->restore_revoked||run_life->pending_epoch||!activation_valid()||
      threads::current_thread_holds_native_quiescence()||story_native_stage_owner::current_thread_holds_lifetime_pin())return deny("context");
   // The issuer's Session try-fence is already held. No guest path blocks on
   // that fence; acquire physical Q before the NEW nonblocking Stage pin.
   // This is data-only and deliberately does not reopen the released Model.
   auto q=threads::try_native_quiescence(c,ticket,100,why);
   if(!q||q->read_only()||!q->current(c))return deny("quiescence");
   auto pin=story_native_stage_owner_observer::tracker().try_scan();const auto now=story_native_stage_owner_observer::snapshot(pin);const auto wanted=current_stage(pin);
   if(!now||!wanted||now->control!=wanted->control||now->receipt!=wanted->receipt)return deny("stage");
   const auto same=[&](const M::Scope&s){return s.identity==origin.identity&&s.link==origin.link&&s.link_pid==origin.link_pid&&s.logic_step==origin.logic_step&&s.serial==origin.serial&&s.fenced&&s.exclusive&&s.bindings_verified&&!s.foreign&&!s.fault&&!s.manager_depth&&!s.camera_depth&&!s.npc_depth&&!s.callback_depth&&!s.foreign_depth;};
   const auto baseline=session.baseline();const auto journal=session.journal();M::Scope before,after;
   if(!baseline||*baseline!=expected||!journal||journal->phase!=J::Phase::Unpublished)return deny("journal");
   if(!backend.sources.session(before))return deny("before-source");if(!same(before))return deny("before-scope");
   if(!session.clean_rearm_roots())return deny("roots");
   if(!backend.sources.session(after))return deny("after-source");if(!same(after))return deny("after-scope");
   if(!q->current(c)||!pin.current()||fault||retained(expected))return deny("final");
   reason="verified";return true;
   // pin and Q die here, BEFORE Session::rearm calls native read-only Search.
  }catch(...){return false;}}();
   // Logging can take a host mutex: all physical Q/Stage guards have died.
   LOG("[native_wait] cancel rearm source result=%u stage=%s reason=%s sp=%08X step=%llu serial=%llu",unsigned(okay),reason,why.c_str(),c?c->r[1]:0,static_cast<unsigned long long>(origin.logic_step),static_cast<unsigned long long>(origin.serial));return okay;};
  controller=std::make_unique<W::Controller>(session,depth,expected,arena,stage,backend.config,std::move(controller_sources));return true;
 }
};
thread_local Context::Impl*Context::Impl::native_terminal=nullptr;
thread_local Context::Impl*Context::Impl::native_control=nullptr;
Context::Context(std::unique_ptr<Impl>i):impl_(std::move(i)){}
Context::~Context(){impl_->run_life->alive=false;}
std::unique_ptr<Context>Context::prepare(story_native_wait_session::Session&s,D::Tracker&d,Expected e,Layout a,Stage st,Ticket t,Cpu*c,story_native_wait_runtime_backend::Bundle b,std::array<story_native_people_admission::Reference,2>refs,Sources sources){
 try{auto impl=std::make_unique<Impl>(s,d,std::move(e),a,st,t,c,std::move(b),std::move(refs),std::move(sources));if(!impl->initialize()){LOG("[native_wait] runtime context initialization refused");return {};}LOG("[native_wait] runtime context ready (publication disabled by default)");return std::unique_ptr<Context>(new Context(std::move(impl)));}catch(const std::exception&e){LOG("[native_wait] runtime context initialization exception: %s",e.what());return {};}catch(...){LOG("[native_wait] runtime context initialization exception");return {};}
}
bool Context::stage_renewal_needed()const{
 const auto&x=*impl_;
 if(!x.thread(x.cpu)||x.renewing_stage||x.fault||x.restored_dormant||x.run_life->restore_revoked||!x.run_life->alive||!x.stage_cell||!x.backend.stage_binding||!x.sources.model_graph||!x.retained(x.expected))return false;
 const auto d=x.depth.snapshot();if(d.state!=D::State::Active||!d.coverage||d.fault||!d.idle())return false;
 auto pin=story_native_stage_owner_observer::tracker().try_scan();
 if(!pin.current())return false;
 const auto current=story_native_stage_owner_observer::snapshot(pin);
 return current&&!x.stage_cell->current(pin);
}
bool Context::renew_stage(Cpu*c){
 auto&x=*impl_;namespace O=story_native_event_observation;
 if(c!=x.cpu||!x.thread(c)||x.renewing_stage||x.fault||x.restored_dormant||x.run_life->restore_revoked||!x.run_life->alive||!x.stage_cell||!x.backend.stage_binding||!x.sources.model_graph||!x.retained(x.expected)||!x.activation_valid())return false;
 const auto before=x.depth.snapshot();if(before.state!=D::State::Active||!before.coverage||before.fault||!before.idle())return false;
 x.renewing_stage=true;struct Reset{bool&b;~Reset(){b=false;}}reset{x.renewing_stage};
 try{
  std::string why;auto parked=threads::try_native_quiescence(c,x.ticket,100,why);if(!parked)return false;
  auto q=std::make_shared<threads::NativeQuiescence>(std::move(*parked));
  auto fence=x.session.try_fence();if(!fence)return false;
  auto pin=story_native_stage_owner_observer::tracker().try_scan();const auto next=story_native_stage_owner_observer::snapshot(pin);
  if(!next)return false;
  if(auto current=x.current_stage(pin))return current->control==next->control&&current->receipt==next->receipt;
  auto candidate=x.backend.stage_binding->candidate(q,pin,*next);if(!candidate)return false;
  const auto&b=candidate->bundle();const auto journal=x.session.journal();
  if(!journal||(journal->phase!=J::Phase::Published&&journal->phase!=J::Phase::Running))return false;
  auto same=[&](const M::Scope&a,const M::Scope&z){return a.identity==z.identity&&a.logic_step==z.logic_step&&a.serial==z.serial&&a.link==z.link&&a.link_pid==z.link_pid;};
  auto scope=[&](M::Scope&v){const auto d=x.depth.snapshot();return q->current(c)&&candidate->current()&&x.thread(c)&&x.held()&&x.retained(x.expected)&&!x.fault&&x.activation_valid()&&d.state==D::State::Active&&d.coverage&&!d.fault&&d.idle()&&d.serial==before.serial&&b.sources.session(v)&&v.identity==x.expected.identity&&v.exclusive&&v.bindings_verified&&!v.fault&&!v.foreign&&!v.fenced&&!v.manager_depth&&!v.camera_depth&&!v.npc_depth&&!v.callback_depth&&!v.foreign_depth&&v.serial==before.serial&&x.live_receipt();};
  M::Scope initial;if(!scope(initial))return false;
  auto validate=[&](){
   M::Scope at;if(!scope(at)||!same(initial,at))return false;
   if(!x.sources.model_graph->validate_stage_candidate(*candidate,why))return false;
   auto allocation=[&](uint32_t image,uint32_t bytes,uint32_t stamp,uint32_t generation){return image==x.expected.identity.image&&bytes==x.expected.identity.bytes&&stamp==b.config.memory.stamp&&generation==x.expected.identity.generation&&b.sources.allocation(x.expected.identity,b.config.memory);};
   auto camera=[&](const O::Camera&v,const auto&id){return story_native_camera_policy::verify(CameraLayout.tables,b.sources.read,v,id).valid;};
   auto guards=[&](story_native_event_collect::Guards&g){M::Scope now;if(!scope(now)||!same(initial,now))return false;g={};g.exclusive=true;return true;};
   story_native_event_collect::Collector collector(x.expected,b.config.collector,b.sources.read,b.sources.actor,allocation,camera,guards);
   O::Captured captured;if(!collector.collect(captured))return false;
   const auto result=O::validate(captured.image[0xE7]==1?O::Operation::Publish:O::Operation::Running,x.expected,captured);
   M::Scope end;return result.valid()&&scope(end)&&same(initial,end);
  };
  if(!validate())return false;
  story_native_stage_binding::Cell::Validation proof(x.stage_cell,*next,q,validate);
  auto certificate=story_native_stage_binding::Cell::stage(std::move(proof),pin);
  return certificate&&x.stage_cell->commit(std::move(*certificate),pin);
 }catch(...){return false;}
}
std::unique_ptr<Context>Context::prepare_restored(const story_native_restore_resume::Boundary&boundary,story_native_wait_session::Session&session,D::Tracker&depth,Stage stage,Ticket ticket,Cpu*cpu,story_native_wait_runtime_backend::Bundle backend,std::array<story_native_people_admission::Reference,2>refs,Sources sources){
 return prepare_restored(boundary,session,depth,stage,ticket,cpu,[backend=std::move(backend)](const Expected&,const Layout&)mutable->std::optional<story_native_wait_runtime_backend::Bundle>{return std::move(backend);},std::move(refs),std::move(sources));
}
std::unique_ptr<Context>Context::prepare_restored(const story_native_restore_resume::Boundary&boundary,story_native_wait_session::Session&session,D::Tracker&depth,Stage stage,Ticket ticket,Cpu*cpu,RestoredBackendFactory factory,std::array<story_native_people_admission::Reference,2>refs,Sources sources){
 const char*phase="origin";
 auto refuse=[&]()->std::unique_ptr<Context>{LOG("[native_wait] restored adoption refused phase=%s",phase);return {};};
 try{
  const auto&completion=boundary.completion();const auto&origin=completion.origin();const auto saved=origin.session_baseline();
  if(!cpu||!boundary.current(cpu)||!depth.restoring(boundary)||!saved||!completion.original_model()||!origin.life_||!sources.restore_read||!factory)return refuse();
  const auto original_life=std::static_pointer_cast<const RunLife>(origin.life_);
  const auto frontend=origin.frontend_checkpoint();const auto frontend_life=original_life->original_frontend_life?original_life->original_frontend_life:origin.life_;
  if(original_life->activation){if(!frontend||frontend->activation()!=original_life->activation||frontend->original_run_life()!=frontend_life||!frontend->definition_current())return refuse();}else if(frontend)return refuse();
  // Preserve the exact host activation. Saved identifiers can never mint one.
  if(sources.activation&&sources.activation!=original_life->activation)return refuse();
  for(unsigned n=0;n<2;++n)if(refs[n].mod!=original_life->references[n].mod||refs[n].id!=original_life->references[n].id)return refuse();
  sources.activation=original_life->activation;
  std::string why;Layout restored_arena;phase="session";
  {
   auto parked=threads::try_native_quiescence(cpu,ticket,100,why);if(!parked)return refuse();
   auto fence=session.try_fence();if(!fence)return refuse();
   auto pin=story_native_stage_owner_observer::tracker().try_scan();const auto current=story_native_stage_owner_observer::snapshot(pin);
   if(!current||current->control!=stage.control||current->receipt!=stage.receipt||!session.adopt_restored(boundary,*parked,sources.restore_read))return refuse();restored_arena=session.arena();
  }
  std::optional<story_native_wait_runtime_backend::Bundle>made;
  {auto fence=session.try_fence();if(!fence||!session.adopted_restored(boundary))return refuse();phase="backend";made=factory(origin.expected(),restored_arena);if(!made||!session.adopted_restored(boundary)||!boundary.current(cpu))return refuse();}
  auto backend=std::move(*made);
  // Rebind copies only ORIGINAL graph baselines. Both native and model entry
  // admission remain disabled on the resulting provisional context.
  phase="model";auto model=completion.original_model()->rebind_saved(boundary,session,depth,stage,ticket,cpu,backend,why);if(!model)return refuse();
  phase="clock";if(origin.expected().program&&origin.expected().program->kind()==story_native_event_program::Kind::TurnCamera&&!model->restore_clock(boundary,why))return refuse();
  sources.model_graph=model;
  phase="helpers";auto impl=std::make_unique<Impl>(session,depth,origin.expected(),restored_arena,stage,ticket,cpu,std::move(backend),std::move(refs),std::move(sources));
  impl->restored_dormant=true;impl->restored_origin=origin;impl->run_life->owner=frontend?frontend->cell():original_life->owner;impl->run_life->original_frontend_life=original_life->original_frontend_life?original_life->original_frontend_life:origin.life_;
  auto binding=model->bind(why);if(!binding||!binding->current()||!session.fence_held()||!session.adopted_restored(boundary)){LOG("[native_wait] restored adoption binding reason=%s",why.c_str());return refuse();}phase="initialize";if(!impl->initialize(binding->quiescence()))return refuse();
  phase="controller";if(!model->matches_live(*binding,completion.saved_graph())||!saved->matches_live(impl->sources.restore_read))return refuse();story_native_restore_claim::Transfer transfer(boundary,*binding,session);
  impl->claim.store(true,std::memory_order_release);
  const auto restored=impl->controller->rehydrate(cpu,boundary,std::move(transfer));
  const bool stable=binding->current()&&boundary.current(cpu)&&depth.restoring(boundary)&&model->matches_live(*binding,completion.saved_graph())&&saved->matches_live(impl->sources.restore_read);
  if(!stable||restored!=W::State::RestoredDormant){LOG("[native_wait] restored adoption controller refused state=%u",unsigned(restored));impl->fault=true;impl->state=State::Quarantined;}else impl->state=State::RestoredDormant;
  return std::unique_ptr<Context>(new Context(std::move(impl)));
 }catch(...){return refuse();}
}
std::shared_ptr<const void>Context::run_identity()const{const auto&x=*impl_;return x.frontend_identity();}
bool Context::attach_run_owner(std::shared_ptr<story_native_scene_playback::RunOwnerCell>owner){
 auto&x=*impl_;if(!owner||x.restored_origin||!x.sources.activation||owner->activation()!=x.sources.activation||!x.run_life->owner.expired())return false;x.run_life->owner=std::move(owner);return true;
}
bool Context::resume_restored(const story_native_restore_resume::Boundary&boundary,story_native_restore_front::Front&&front){
 auto&x=*impl_;if(x.restore_open_attempted||!x.restored_dormant||x.state!=State::RestoredDormant||x.fault)return false;x.restore_open_attempted=true;
 try{
  const auto&completion=boundary.completion();const auto&origin=completion.origin();const auto saved=origin.session_baseline();
  if(front.replacement_){const auto&r=*front.replacement_;if(r.session!=&x.session||r.activation!=x.sources.activation||r.frontend!=bool(x.sources.activation)||(r.activation&&!r.declared)||r.mod!=x.refs[0].mod||r.mod!=x.refs[1].mod||r.ids[0]!=x.refs[0].id||r.ids[1]!=x.refs[1].id)throw std::runtime_error("restored runtime selection");}
  if(!front.current()||front.old_context_==this||(front.old_depth_&&(!front.old_context_||front.old_depth_!=&front.old_context_->impl_->depth))||!restore_front_current(boundary,front.old_context_)||!x.restored_origin||!x.restored_origin->same_run(origin)||!x.sources.model_graph||!saved||!x.thread(x.cpu)||!boundary.current(x.cpu)||!x.depth.restoring(boundary))throw std::runtime_error("restored front provenance");
  const auto&revoked=completion.current_owner();
  if(revoked->context_slot_&&(revoked->context_slot_!=front.context_||revoked->depth_slot_!=front.depth_))throw std::runtime_error("restored front storage");
  std::string why;auto binding=x.sources.model_graph->bind(why);if(!binding||!binding->current())throw std::runtime_error("restored final graph binding");
  auto q=binding->quiescence();if(!q||q->read_only()||!q->current(x.cpu)||!x.session.adopted_restored(boundary)||!saved->matches_live(x.sources.restore_read)||!x.activation_valid()||!x.controller->prepare_restore_open(x.cpu,boundary,*q)||!x.ensure_terminal())throw std::runtime_error("restored final observation");
  const auto&proof=*x.controller->restored_result_;
  auto model_open=x.sources.model_graph->prepare_restore_open(boundary,*binding,proof,why);if(!model_open)throw std::runtime_error("restored model prepare");
  std::shared_ptr<story_native_scene_checkpoint::PreparedRestore>frontend_open;
  const auto saved_frontend=origin.frontend_checkpoint();
  auto*previous=revoked->owner_;
  const auto previous_cell=previous?previous->impl_->run_life->owner.lock():nullptr;
  if(saved_frontend&&previous_cell!=saved_frontend->cell()){
   if(!x.sources.prepare_frontend_restore||!saved_frontend->definition_current()||!restore_front_current(boundary,previous))throw std::runtime_error("historical frontend issuer");
   story_native_scene_checkpoint::CurrentRunRevocation revocation(previous,previous?previous->impl_->frontend_identity():std::shared_ptr<const void>{},completion.restore_epoch(),previous_cell,revoked);
   frontend_open=x.sources.prepare_frontend_restore(*saved_frontend,std::move(revocation));
   if(!frontend_open)throw std::runtime_error("historical frontend reservation");
  }
  std::optional<story_native_scene_playback::RunOwnerCell::PreparedResume>owner_open;
  auto owner=x.run_life->owner.lock();
  if(x.sources.activation){
   if(!owner||owner->activation()!=x.sources.activation)throw std::runtime_error("restored frontend lineage");
   story_native_scene_playback::RunOwnerCell::Endpoint endpoint;
   endpoint.poll=[this](Cpu*){story_native_scene_playback::Progress p;if(state()==State::Released&&!retained()){p.phase=story_native_scene_playback::Progress::Phase::Retired;p.natural=take_completion();}else if(state()==State::Quarantined||state()==State::Refused){p.phase=story_native_scene_playback::Progress::Phase::Quarantined;p.reason=story_scenes::StopReason::Failed;}return p;};
   endpoint.queue_stop=[this](story_scenes::StopReason){request_cancel();};
   story_native_scene_playback::RunOwnerCell::ResumeCertificate certificate(*owner,x.sources.activation,run_identity(),std::move(endpoint));
   owner_open=owner->prepare_resume(std::move(certificate));if(!owner_open)throw std::runtime_error("restored frontend reservation");
  }
  if(!front.current()||!binding->current()||!boundary.current(x.cpu)||!saved->matches_live(x.sources.restore_read))throw std::runtime_error("restored final stability");
  // This is LAST: the token owns Tracker's nonrecursive mutex. Every check,
  // allocation, Source and guest read has completed before acquiring it.
  auto depth_open=x.depth.prepare_restore_open(boundary,*q,proof);if(!depth_open)throw std::runtime_error("restored depth prepare");
  // Callback-free/nonthrowing host commit. Physical Q excludes guest entry
  // throughout these partial stores; no validation follows the first flip.
  model_open->open_unchecked();x.controller->open_restored_unchecked();depth_open->open_unchecked();
  x.restored_dormant=false;x.state=proof.event_state()==1?State::Started:State::Running;
  front.commit_unchecked(this,&x.depth);if(owner_open)owner_open->commit_unchecked();if(frontend_open)frontend_open->commit_unchecked();return true;
 }catch(const std::exception&e){LOG("[native_wait] restored resume refused: %s",e.what());x.invalidate();return false;}catch(...){x.invalidate();return false;}
}
bool Context::retained()const noexcept{return impl_->retained(impl_->expected);}
State Context::state()const noexcept{return impl_->state.load();}
std::optional<story_native_scene_completion::Completion>Context::take_completion(){if(impl_->explicit_cancel){impl_->natural_completion.reset();return {};}auto result=std::move(impl_->natural_completion);impl_->natural_completion.reset();return result;}
bool Context::rearm(Cpu*c){
 auto&x=*impl_;try{
  const auto rearm_depth=x.depth.snapshot();LOG("[native_wait] rearm depth state=%u coverage=%u fault=%u serial=%llu idle=%u",unsigned(rearm_depth.state),unsigned(rearm_depth.coverage),unsigned(rearm_depth.fault),static_cast<unsigned long long>(rearm_depth.serial),unsigned(rearm_depth.idle()));
  LOG("[native_wait] rearm attempt sp=%08X state=%u controller=%u fault=%u alive=%u revoked=%u pending=%llu retained=%u thread=%u",c?c->r[1]:0,unsigned(x.state.load()),x.controller?unsigned(x.controller->state()):~0u,unsigned(x.fault.load()),unsigned(x.run_life->alive),unsigned(x.run_life->restore_revoked),static_cast<unsigned long long>(x.run_life->pending_epoch),unsigned(x.retained(x.expected)),unsigned(c&&x.thread(c)));
  if(!c||!x.thread(c)||x.fault||x.run_life->restore_revoked||!x.run_life->alive||x.run_life->pending_epoch||x.state!=State::Released||!x.controller||x.controller->state()!=W::State::Released||x.retained(x.expected)){LOG("[native_wait] rearm refused stage=context");return false;}
  std::unique_ptr<story_native_wait_session::Session::Fence>fence;if(!x.session.fence_held())fence=x.session.try_fence();if(!x.session.fence_held()){LOG("[native_wait] rearm refused stage=fence");return false;}
  auto certificate=x.controller->take_rearm_certificate();if(!certificate){LOG("[native_wait] rearm refused stage=certificate");return false;}
  const bool okay=x.session.rearm(std::move(*certificate),c);
  LOG("[native_wait] rearm session result=%u sp=%08X",unsigned(okay),c->r[1]);
  // The old issuer/Tracker remain alive through consumption. Even success
  // cannot reactivate this Context or reuse its immutable Expected/sources.
  x.run_life->restore_revoked=true;x.run_life->alive=false;x.fault=true;
  return okay;
 }catch(...){x.run_life->restore_revoked=true;x.run_life->alive=false;x.fault=true;return false;}
}
std::optional<SavedRunReceipt> Context::begin_saved_run(){
 auto&x=*impl_;try{
  auto*c=threads::current();if(!c||!x.thread(c)||!x.run_life->alive||x.run_life->pending_epoch||x.fault||x.cancel||!x.controller||!x.retained(x.expected)||
   (x.state!=State::Started&&x.state!=State::Running))return {};
  auto parked=threads::current_quiescence(c);if(!parked||!parked->current(c))return {};
  std::shared_ptr<const story_native_scene_checkpoint::Checkpoint>frontend;
  if(x.sources.activation){auto owner=x.run_life->owner.lock();if(!owner||!x.activation_valid())return {};auto captured=story_scenes::capture_native_checkpoint(*x.sources.activation,owner);if(!captured||captured->activation()!=x.sources.activation||captured->original_run_life()!=x.frontend_identity()||!captured->definition_current())return {};frontend=std::make_shared<const story_native_scene_checkpoint::Checkpoint>(std::move(*captured));}
  auto p=std::make_shared<SavedRunReceipt::Capture>();p->parked=*parked;p->requester=c;p->life=x.run_life;p->issuer=&x;p->thread=std::this_thread::get_id();
  p->preclaim_model=x.sources.model_graph;
  if(p->preclaim_model){
   std::string why;auto borrowed=threads::borrow_native_read_quiescence(c,x.ticket,*parked,why);if(!borrowed)return {};
   p->saved_park=std::make_shared<threads::NativeQuiescence>(std::move(*borrowed));

  }
  p->fence=x.session.try_fence();
  if(!p->fence)return {};
  if(p->preclaim_model){std::string why;p->model_binding=p->preclaim_model->bind(p->saved_park,why);if(!p->model_binding)return {};}
  p->stage_scan.emplace(story_native_stage_owner_observer::tracker().try_scan());
  const auto wanted=x.current_stage(*p->stage_scan);
  const auto stage=story_native_stage_owner_observer::snapshot(*p->stage_scan);
  if(!wanted||!stage||stage->control!=wanted->control||stage->receipt!=wanted->receipt||!x.controller->checkpoint(c))return {};
  auto fail=[&]()->std::optional<SavedRunReceipt>{x.invalidate();return {};};
  if(x.controller->state()==W::State::Running)x.state=State::Running;
  story_native_event_observation::Result observation;M::Scope at;
  if(!x.capture_active(observation,at)){LOG("[native_wait] saved run capture refused observation");return fail();}
  p->preclaim_model=x.sources.model_graph;p->expected=x.expected;p->arena=x.arena;p->stage=*stage;p->scope=at;p->observation=observation;
  if(!F::capture(x.backend.sources.read,x.arena,x.expected,x.poses,CameraLayout.tables,p->footprint)){LOG("[native_wait] saved run capture refused footprint");return fail();}
  if(p->preclaim_model){p->model_saved=p->preclaim_model->capture_saved(*p->model_binding);if(!p->model_saved){LOG("[native_wait] saved run capture refused model graph");return fail();}}
  std::optional<story_native_event_collect::Clock>saved_clock;
  if(x.expected.program&&x.expected.program->kind()==story_native_event_program::Kind::TurnCamera){story_native_event_collect::Clock clock;if(!x.backend.config.collector.clock||!x.backend.config.collector.clock(clock)||clock.identity!=x.expected.identity||clock.logic_step!=at.logic_step||clock.serial!=at.serial)return fail();saved_clock=clock;}
  const auto session_baseline=x.session.capture_saved_baseline(p->footprint,x.backend.sources.read);if(!session_baseline)return fail();
  p->origin=story_native_restore_resume::SavedRunOrigin(x.expected,x.depth.record(),at,saved_clock,x.run_life,session_baseline,std::move(frontend));
  M::Scope after;if(!x.scope(after)||at.logic_step!=after.logic_step||at.serial!=after.serial||!p->parked->current(c)||!x.saved_origin_current(*p->origin))return fail();
  // Serialization itself re-enters the real thread registry. Retain only
  // the Session fence across it, never a borrowed registry pin or Stage lock.
  p->stage_scan.reset();p->model_binding.reset();p->saved_park.reset();
  return SavedRunReceipt(std::move(p));
 }catch(...){x.invalidate();return {};}
}
bool Context::commit_saved_run(SavedRunReceipt&receipt,std::unique_ptr<story_native_wait_session::Session::Fence>*retain_fence){
 if(retain_fence&&*retain_fence)return false;
 auto&x=*impl_;const auto p=receipt.capture_;
 if(!p||p->thread!=std::this_thread::get_id())return false;
 if(p->issuer!=&x||p->life!=x.run_life||p->consumed||p->committed||!p->fence)return false;
 bool ok=false;try{
  auto*c=threads::current();M::Scope at;
  if(!p->parked||!p->parked->current(c)||c!=p->requester||!x.thread(c)||p->stage_scan||p->model_binding||p->saved_park)throw std::runtime_error("saved commit freeze");
  if(p->preclaim_model){std::string why;auto borrowed=threads::borrow_native_read_quiescence(c,x.ticket,*p->parked,why);if(!borrowed)throw std::runtime_error("saved commit registry borrow");p->saved_park=std::make_shared<threads::NativeQuiescence>(std::move(*borrowed));p->model_binding=p->preclaim_model->bind(p->saved_park,why);if(!p->model_binding)throw std::runtime_error("saved commit model binding");}
  auto scan=story_native_stage_owner_observer::tracker().try_scan();if(!scan.current())throw std::runtime_error("saved commit Stage pin");p->stage_scan.emplace(std::move(scan));
  const auto stage=story_native_stage_owner_observer::snapshot(*p->stage_scan);
  ok=p->origin&&x.saved_origin_current(*p->origin)&&p->life->alive&&p->parked&&p->parked->current(c)&&c==p->requester&&p->stage_scan&&p->stage_scan->current()&&x.thread(c)&&x.retained(x.expected)&&stage&&stage->control==p->stage.control&&stage->receipt==p->stage.receipt&&
   x.scope(at)&&at.logic_step==p->scope.logic_step&&at.serial==p->scope.serial&&F::matches_live(x.backend.sources.read,p->footprint)&&(!p->preclaim_model||(p->model_saved&&p->model_binding&&p->preclaim_model->matches_live(*p->model_binding,*p->model_saved)));
  M::Scope after;ok=ok&&x.scope(after)&&after.logic_step==at.logic_step&&after.serial==at.serial&&p->parked->current(c);
 }catch(...){ok=false;}
 p->committed=ok;p->consumed=!ok;p->stage_scan.reset();p->model_binding.reset();if(retain_fence)*retain_fence=std::move(p->fence);else p->fence.reset();p->saved_park.reset();if(!ok)x.invalidate();return ok;
}
std::optional<SavedRunCheckpoint>Context::checkpoint_saved_run(SavedRunReceipt&receipt){
 auto&x=*impl_;auto saved=receipt.capture_;if(!saved||saved->thread!=std::this_thread::get_id()||!saved->committed||saved->consumed||saved->checkpoint_issued||saved->fence||saved->stage_scan||saved->model_binding||saved->saved_park||saved->issuer!=&x||saved->life!=x.run_life||!saved->origin||saved->expected!=x.expected)return {};
 saved->checkpoint_issued=true;
 try{SavedRunCheckpoint source(saved);auto copy=source.fork_attempt();if(!copy)return {};return SavedRunCheckpoint(std::move(copy->capture_));}catch(...){return {};}
}
std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>Context::prepare_current_load(story_native_restore_front::Front&&front,const ss::native_gate::SavedView&view,uint64_t epoch){
 using Owner=story_native_restore_resume::CurrentLoadOwner;
 struct State {
  std::thread::id host=std::this_thread::get_id();Cpu*cpu=nullptr;
  std::optional<threads::QuiescenceWitness>parked;
  Impl*live=nullptr;std::shared_ptr<RunLife>life;
  std::unique_ptr<story_native_wait_session::Session::Fence>fence;
  std::atomic<Context*>*context=nullptr;std::atomic<D::Tracker*>*depth=nullptr;
  Context*prior=nullptr;D::Tracker*prior_depth=nullptr;
  const uint8_t*alloc_begin=nullptr,*alloc_end=nullptr;
  const ss::MemoryRegion*regions=nullptr;size_t region_count=0;const ss::MemoryChunks*chunks=nullptr;
  uint64_t epoch=0,step=0,serial=0;std::atomic<bool>closed{false},active_restore{false};
  ~State(){if(fence&&host!=std::this_thread::get_id())std::terminate();if(life&&life->pending_epoch==epoch)life->pending_epoch=0;}
 };
 try{
  auto*c=threads::current();auto parked=threads::current_quiescence(c);
  if(!epoch||!c||!parked||!parked->current(c)||!front.current()||!view.allocations.ok||(!front.old_context_&&front.old_depth_))return {};
  auto s=std::make_shared<State>();s->cpu=c;s->parked=*parked;s->context=front.context_;s->depth=front.depth_;s->prior=front.old_context_;s->prior_depth=front.old_depth_;s->epoch=epoch;
  s->alloc_begin=view.allocations.p;s->alloc_end=view.allocations.e;s->regions=view.regions.data();s->region_count=view.regions.size();s->chunks=&view.chunks;
  std::shared_ptr<const story_native_restore_resume::CurrentOwnerRevocation>revocation;
  if(s->prior){
   auto&live=*s->prior->impl_;s->live=&live;s->life=live.run_life;
   if((s->prior_depth&&s->prior_depth!=&live.depth)||!live.thread(c)||live.fault||live.cancel||!s->life->alive||s->life->restore_revoked||s->life->pending_epoch||!live.controller)return {};
   s->fence=live.session.try_fence();if(!s->fence)return {};
   story_native_event_observation::Result observation;M::Scope at;
   if(!live.capture_active(observation,at)||!front.current()||!parked->current(c))return {};
   M::Scope after;if(!live.scope(after)||at.logic_step!=after.logic_step||at.serial!=after.serial)return {};
   s->step=after.logic_step;s->serial=after.serial;
   revocation=std::shared_ptr<const story_native_restore_resume::CurrentOwnerRevocation>(new story_native_restore_resume::CurrentOwnerRevocation(s->prior,s->life,live.expected,epoch,s->context,s->depth,s->prior_depth));
   // All allocations and fresh source checks precede host-only revocation.
  }else revocation=std::shared_ptr<const story_native_restore_resume::CurrentOwnerRevocation>(new story_native_restore_resume::CurrentOwnerRevocation(nullptr,{},Expected{},epoch,s->context,s->depth,nullptr));
  auto current=[](const void*opaque,Cpu*cpu,const threads::QuiescenceWitness&w,const ss::native_gate::SavedView&v,uint64_t attempt)noexcept{
   try{const auto&s=*static_cast<const State*>(opaque);
    if(s.host!=std::this_thread::get_id()||s.closed.load(std::memory_order_acquire)||cpu!=s.cpu||!s.parked||!s.parked->current(cpu)||!w.current(cpu)||attempt!=s.epoch||!v.allocations.ok||v.allocations.p!=s.alloc_begin||v.allocations.e!=s.alloc_end||v.regions.data()!=s.regions||v.regions.size()!=s.region_count||&v.chunks!=s.chunks||s.context->load(std::memory_order_acquire)!=s.prior)return false;
    const auto*depth=s.depth->load(std::memory_order_acquire);if(depth&&depth!=s.prior_depth)return false;
    if(!s.live)return !depth;
    if(!s.fence||!s.live->session.fence_held()||s.life!=s.live->run_life||!s.life->restore_revoked||s.life->alive||!s.live->fault||!s.live->controller->restore_revoked_)return false;
    const auto d=s.live->depth.snapshot();return d.coverage&&!d.fault&&d.idle()&&d.serial==s.serial&&(d.state==D::State::Active||d.state==D::State::Draining);
   }catch(...){return false;}
  };
  auto holds=[](const void*opaque,const story_native_wait_session::Session&session)noexcept{const auto&s=*static_cast<const State*>(opaque);return s.host==std::this_thread::get_id()&&!s.closed.load(std::memory_order_acquire)&&s.live&&&s.live->session==&session&&s.fence&&session.fence_held();};
  auto close=[](void*opaque)noexcept{auto&s=*static_cast<State*>(opaque);if(s.closed.load(std::memory_order_acquire))return;if(s.host!=std::this_thread::get_id())std::terminate();s.closed.store(true,std::memory_order_release);if(s.life&&s.life->pending_epoch==s.epoch)s.life->pending_epoch=0;s.fence.reset();};
  auto mark_active=[](void*opaque)noexcept{auto&s=*static_cast<State*>(opaque);return s.host==std::this_thread::get_id()&&!s.closed.load(std::memory_order_acquire)&&!s.active_restore.exchange(true,std::memory_order_acq_rel);};
  auto has_active=[](const void*opaque)noexcept{const auto&s=*static_cast<const State*>(opaque);return s.host!=std::this_thread::get_id()||s.closed.load(std::memory_order_acquire)||s.active_restore.load(std::memory_order_acquire);};
  auto owner=std::shared_ptr<Owner>(new Owner(s,current,holds,close,mark_active,has_active,epoch,std::move(revocation)));
  if(s->live){s->live->controller->revoke_for_restore();s->life->restore_revoked=true;s->life->alive=false;s->life->pending_epoch=epoch;s->live->fault=true;s->live->cancel=true;s->live->state=story_native_wait_runtime_controller::State::Quarantined;}
  front.consumed_=true;return owner;
 }catch(...){return {};}
}
bool Context::finish_empty_load(const ss::native_gate::FinishedLoad&finished,story_native_restore_front::Front&&front,RetireFrontend retire_frontend)noexcept{
 try{
  if(!finished.current()||!front.current())return false;
  const auto&owner=finished.owner();auto*c=threads::current();auto parked=threads::current_quiescence(c);
  if(!owner||!parked||!owner->current(c,*parked,finished.view(),owner->epoch())||!owner->has_active_||!owner->revocation_)return false;
  // The immutable actual runtime aggregate covers ALL declared Sessions plus
  // its private bootstrap Session. A private/pilot exposure denies save. Only
  // a fully validated Context saved-active child can set this private marker.
  // Such a child already published its genuine post-service completion: leave
  // the old front intact for its distinct final Model/Tracker transaction.
  if(owner->has_active_(owner->state_.get()))return true;
  const auto&proof=owner->revocation_;
  if(proof->context_slot_!=front.context_||proof->depth_slot_!=front.depth_||proof->owner_!=front.old_context_||proof->epoch_!=owner->epoch())return false;
  auto*old=proof->owner_;
  if(old&&(!old->impl_->run_life->restore_revoked||old->impl_->run_life->alive||!old->impl_->fault||!old->impl_->controller||!old->impl_->controller->restore_revoked_))return false;
  if(front.old_depth_&&(!old||front.old_depth_!=&old->impl_->depth))return false;
  std::shared_ptr<story_native_scene_checkpoint::PreparedRetire>retirement;
  if(old){
   if(!retire_frontend)return false;
   auto cell=old->impl_->run_life->owner.lock();
   story_native_scene_checkpoint::CurrentRunRevocation revocation(old,old->impl_->frontend_identity(),owner->epoch(),std::move(cell),proof);
   retirement=retire_frontend(std::move(revocation));if(!retirement||!retirement->current())return false;
  }
  // Preallocate empty metadata before the final stores. No SDK owner means no
  // native Player to remove; ordinary legacy state-load cleanup is preserved.
  if(front.selection_)front.replacement_.emplace();
  if(!finished.current()||!front.current()||!owner->current(c,*parked,finished.view(),owner->epoch())||(retirement&&!retirement->current()))return false;
  // All children succeeded and the REAL legacy park/fences remain held. This
  // only REMOVES revoked authority: no guest/native calls, model/Tracker opens,
  // natural completion, rewards or fallible work follows the first store.
  front.commit_unchecked(nullptr,nullptr);if(retirement)retirement->commit_unchecked();return true;
 }catch(...){return false;}
}
std::optional<RunRestorePermit> Context::prepare_run_restore(SavedRunReceipt&&receipt,const ss::native_gate::SavedView&view,uint64_t epoch){
 return prepare_run_restore(std::move(receipt),view,epoch,*this);
}
std::optional<RunRestorePermit> Context::prepare_run_restore(SavedRunReceipt&&receipt,const ss::native_gate::SavedView&view,uint64_t epoch,Context&current_owner,std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>group){
 auto&x=*impl_;auto&live=*current_owner.impl_;const auto saved=receipt.capture_;
 if(!saved||saved->thread!=std::this_thread::get_id())return {};
 if(saved->consumed)return {};saved->consumed=true;
 try{
  auto*c=threads::current();
  // Saved lineage is immutable and may outlive its old Context's admission.
  // The independent actual current front supplies all live ownership checks.
  if(!saved->committed||!saved->origin||saved->fence||saved->issuer!=&x||saved->life!=x.run_life||
    &live.session!=&x.session||!live.run_life->alive||live.run_life->restore_revoked||
    !live.sources.restore_read||!x.sources.restore_read||!epoch||epoch<=x.run_life->last_restore_epoch||
    x.run_life->pending_epoch||live.run_life->pending_epoch||!live.thread(c)||live.fault||live.cancel||
    saved->expected!=x.expected||saved->arena!=x.arena||saved->preclaim_model!=x.sources.model_graph)return {};
  auto parked=threads::current_quiescence(c);if(!parked||!parked->current(c))return {};
  auto p=std::make_unique<RunRestorePermit::Pending>();p->parked=*parked;p->requester=c;p->life=x.run_life;p->issuer=&x;p->thread=std::this_thread::get_id();p->epoch=epoch;p->current_owner=&current_owner;p->current_life=live.run_life;p->fence=x.session.try_fence();
  if(!p->fence)return {};
  story_native_event_observation::Result current;M::Scope at;
  if(!live.capture_active(current,at)||!saved->observation||!saved->observation->valid()||!saved->observation->matches(x.expected)||
    !saved->observation||!saved->observation->valid()||!saved->observation->matches(x.expected)||
   !F::matches_saved(x.arena,view,saved->footprint)||(saved->preclaim_model&&(!saved->model_saved||!saved->preclaim_model->matches_saved(*saved->model_saved,view))))return {};
  // Distinct historical Stage API still requires the exact original resource
  // Candidate/control; a new observer generation alone is not a new resource.
  if(group){p->stage_group=group->join(saved->stage,view,c,*parked);if(!p->stage_group)return {};p->saved_view.emplace(view);}
  else {p->stage=story_native_stage_owner_observer::prepare_restore_same_resource(saved->stage,view);if(!p->stage)return {};}
  M::Scope after;if(!live.scope(after)||at.logic_step!=after.logic_step||at.serial!=after.serial||!p->parked->current(c))return {};
  p->preclaim_model=saved->preclaim_model;p->origin=saved->origin;p->model_saved=std::move(saved->model_saved);p->footprint=saved->footprint;p->scope=after;x.run_life->last_restore_epoch=epoch;x.run_life->pending_epoch=epoch;
  return RunRestorePermit(std::move(p));
 }catch(...){return {};}
}
std::optional<RunRestorePermit> Context::prepare_run_restore(SavedRunReceipt&&receipt,const ss::native_gate::SavedView&view,uint64_t epoch,story_native_restore_front::Front&&front,std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>group,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>load_owner){
 if(!front.current())return {};
 if(load_owner){
  auto&x=*impl_;const auto saved=receipt.capture_;
  if(!saved||saved->thread!=std::this_thread::get_id()||saved->consumed)return {};saved->consumed=true;
  try{auto*c=threads::current();auto parked=threads::current_quiescence(c);
   const auto&revoked=load_owner->revocation_;
   if(!parked||!load_owner->current(c,*parked,view,epoch)||!revoked||revoked->context_slot_!=front.context_||revoked->depth_slot_!=front.depth_||revoked->owner_!=front.old_context_||
     !saved->committed||!saved->origin||saved->fence||saved->issuer!=&x||saved->life!=x.run_life||!x.sources.restore_read||!epoch||epoch<=x.run_life->last_restore_epoch||
     (x.run_life->pending_epoch&&x.run_life->pending_epoch!=epoch)||saved->expected!=x.expected||saved->arena!=x.arena||saved->preclaim_model!=x.sources.model_graph||
     !saved->observation||!saved->observation->valid()||!saved->observation->matches(x.expected)||!F::matches_saved(x.arena,view,saved->footprint)||
     (saved->preclaim_model&&(!saved->model_saved||!saved->preclaim_model->matches_saved(*saved->model_saved,view))))return {};
   auto p=std::make_unique<RunRestorePermit::Pending>();p->thread=std::this_thread::get_id();p->load_owner=load_owner;p->saved_view.emplace(view);
   if(!revoked->owner_){
    auto clean=x.session.begin_idle_capture(c,*parked);if(!clean)return {};
    auto clean_proof=x.session.commit_idle_capture(std::move(*clean),c,*parked,&p->fence);if(!clean_proof||!p->fence)return {};
   }else if(!load_owner->holds_session(x.session)){p->fence=x.session.try_fence();if(!p->fence)return {};}
   if(!x.session.fence_held())return {};
   if(group){p->stage_group=group->join(saved->stage,view,c,*parked);if(!p->stage_group)return {};}
   else {p->stage=story_native_stage_owner_observer::prepare_restore_same_resource(saved->stage,view);if(!p->stage)return {};}
   if(!front.current()||!load_owner->current(c,*parked,view,epoch))return {};
   p->parked=*parked;p->requester=c;p->life=x.run_life;p->issuer=&x;p->epoch=epoch;p->current_owner=revoked->owner_;if(p->current_owner)p->current_life=p->current_owner->impl_->run_life;
   p->preclaim_model=saved->preclaim_model;p->origin=saved->origin;p->model_saved=std::move(saved->model_saved);p->footprint=saved->footprint;p->revocation=revoked;p->current_front.emplace(std::move(front));
   // Only this fully validated saved ACTIVE checkpoint can mark the load;
   // absence of a caller-provided flag never means an inactive snapshot.
   if(!load_owner->mark_active_||!load_owner->mark_active_(load_owner->state_.get()))return {};
   x.run_life->last_restore_epoch=epoch;x.run_life->pending_epoch=epoch;
   return RunRestorePermit(std::move(p));
  }catch(...){return {};}
 }

 if(front.old_context_){
  if(front.old_depth_&&front.old_depth_!=&front.old_context_->impl_->depth)return {};
  auto permit=prepare_run_restore(std::move(receipt),view,epoch,*front.old_context_,std::move(group));
  if(permit)permit->pending_->current_front.emplace(std::move(front));return permit;
 }
 // A null resolver is not authority: only the actual private runtime slot
 // capability PLUS genuine released/rearmed native-clean Session provenance.
 if(front.old_depth_)return {};
 auto&x=*impl_;const auto saved=receipt.capture_;
 if(!saved||saved->thread!=std::this_thread::get_id()||saved->consumed)return {};
 saved->consumed=true;
 try{auto*c=threads::current();
  if(!saved->committed||!saved->origin||saved->fence||saved->issuer!=&x||saved->life!=x.run_life||!x.sources.restore_read||
   !epoch||epoch<=x.run_life->last_restore_epoch||x.run_life->pending_epoch||saved->expected!=x.expected||saved->arena!=x.arena||saved->preclaim_model!=x.sources.model_graph||
   !saved->observation||!saved->observation->valid()||!saved->observation->matches(x.expected)||
   !F::matches_saved(x.arena,view,saved->footprint)||(saved->preclaim_model&&(!saved->model_saved||!saved->preclaim_model->matches_saved(*saved->model_saved,view))))return {};
  auto parked=threads::current_quiescence(c);if(!parked||!parked->current(c))return {};
  auto clean=x.session.begin_idle_capture(c,*parked);if(!clean)return {};
  auto p=std::make_unique<RunRestorePermit::Pending>();p->thread=std::this_thread::get_id();
  auto proof=x.session.commit_idle_capture(std::move(*clean),c,*parked,&p->fence);
  if(!proof||!p->fence||!front.current())return {};
  if(group){p->stage_group=group->join(saved->stage,view,c,*parked);if(!p->stage_group)return {};p->saved_view.emplace(view);}
  else {p->stage=story_native_stage_owner_observer::prepare_restore_same_resource(saved->stage,view);if(!p->stage)return {};}
  if(!front.current()||!parked->current(c))return {};
  p->parked=*parked;p->requester=c;p->life=x.run_life;p->issuer=&x;p->epoch=epoch;p->current_front.emplace(std::move(front));
  p->preclaim_model=saved->preclaim_model;p->origin=saved->origin;p->model_saved=std::move(saved->model_saved);p->footprint=saved->footprint;
  x.run_life->last_restore_epoch=epoch;x.run_life->pending_epoch=epoch;
  return RunRestorePermit(std::move(p));
 }catch(...){return {};}
}
bool Context::suspend_run_restore(RunRestorePermit&permit){
 auto&x=*impl_;auto*p=permit.pending_.get();if(!p||p->thread!=std::this_thread::get_id())return false;
 if(p->suspend_attempted||p->consumed)return false;p->suspend_attempted=true;
 if(p->load_owner){
  try{auto*c=threads::current();
   const bool ok=p->issuer==&x&&p->life==x.run_life&&p->epoch==x.run_life->pending_epoch&&p->revocation&&p->parked&&p->saved_view&&p->current_front&&p->current_front->current()&&x.session.fence_held()&&(p->fence||p->load_owner->holds_session(x.session))&&(p->stage||p->stage_group)&&p->load_owner->current(c,*p->parked,*p->saved_view,p->epoch);
   if(ok){p->current_front->consumed_=true;p->suspended=true;}return ok;
  }catch(...){return false;}
 }

 if(!p->current_owner){
  try{auto*c=threads::current();auto&front=p->current_front;
   if(p->issuer!=&x||p->life!=x.run_life||p->epoch!=x.run_life->pending_epoch||!p->fence||(!p->stage&&!p->stage_group)||!p->parked||!p->parked->current(c)||c!=p->requester||!front||!front->current()||front->old_context_||front->old_depth_)return false;
   p->revocation=std::shared_ptr<const story_native_restore_resume::CurrentOwnerRevocation>(new story_native_restore_resume::CurrentOwnerRevocation(nullptr,{},x.expected,p->epoch,front->context_,front->depth_,nullptr));
   front->consumed_=true;p->suspended=true;return true;
  }catch(...){return false;}
 }
 auto&live=*p->current_owner->impl_;bool ok=false;
 try{auto*c=threads::current();M::Scope at;
  ok=p->issuer==&x&&p->life==x.run_life&&p->current_life==live.run_life&&live.run_life->alive&&
   !live.run_life->restore_revoked&&p->epoch==x.run_life->pending_epoch&&p->fence&&(p->stage||p->stage_group)&&
   p->parked&&p->parked->current(c)&&c==p->requester&&live.thread(c)&&live.retained(live.expected)&&live.scope(at)&&at.logic_step==p->scope.logic_step&&at.serial==p->scope.serial;
  if(ok&&p->current_front&&!p->current_front->current())ok=false;
  if(ok){auto*front=p->current_front?&*p->current_front:nullptr;
   p->revocation=std::shared_ptr<const story_native_restore_resume::CurrentOwnerRevocation>(new story_native_restore_resume::CurrentOwnerRevocation(p->current_owner,live.run_life,live.expected,p->epoch,front?front->context_:nullptr,front?front->depth_:nullptr,front?front->old_depth_:nullptr));
   if(front)front->consumed_=true;
  }
 }catch(...){ok=false;}
 // No restored guest bytes are touched. Revocation always targets the actual
 // current owner, never an old saved issuer inferred from snapshot identity.
 if(live.controller)live.controller->revoke_for_restore();
 live.run_life->restore_revoked=true;live.fault=true;live.cancel=true;live.state=State::Quarantined;p->suspended=ok;
 if(!ok)live.run_life->alive=false;
 return ok;
}
bool Context::finish_run_restore(RunRestorePermit&&permit,std::unique_ptr<story_native_wait_session::Session::Fence>*retain_fence){
 if(retain_fence&&*retain_fence)return false;
 auto&x=*impl_;auto*p=permit.pending_.get();if(!p||p->thread!=std::this_thread::get_id())return false;
 if(p->consumed)return false;p->consumed=true;bool ok=false;std::optional<story_native_stage_owner_observer::Snapshot>finished_stage;
 try{
  if(p->issuer==&x&&p->life==x.run_life&&((p->load_owner&&p->saved_view&&p->parked&&p->load_owner->current(p->requester,*p->parked,*p->saved_view,p->epoch))||(!p->load_owner&&(!p->current_owner||(p->current_life==p->current_owner->impl_->run_life&&p->current_life->alive))))&&p->revocation&&p->suspended&&p->epoch==x.run_life->pending_epoch&&x.session.fence_held()&&(p->fence||(p->load_owner&&p->load_owner->holds_session(x.session)))&&(p->stage||p->stage_group)&&p->parked&&p->parked->current(p->requester)&&
    F::matches_live(x.sources.restore_read,p->footprint)&&(!p->preclaim_model||(p->model_saved&&p->model_saved->matches_live(x.sources.restore_read)))){
   if(p->stage_group){if(p->saved_view)finished_stage=p->stage_group->finish(*p->saved_view,p->requester,*p->parked);}
   else finished_stage=story_native_stage_owner_observer::finish_restore(std::move(*p->stage));
   const auto&stage=finished_stage;
   ok=stage&&stage->control==x.stage.control&&stage->receipt.owner==x.stage.receipt.owner&&F::matches_live(x.sources.restore_read,p->footprint)&&(!p->preclaim_model||(p->model_saved&&p->model_saved->matches_live(x.sources.restore_read)))&&p->parked->current(p->requester);
  }
 }catch(...){ok=false;}
 try{if(ok&&p->preclaim_model){
  auto completion=story_native_restore_resume::Completion::make(x.expected,p->preclaim_model,std::move(*p->model_saved),*p->origin,p->epoch,p->requester,*p->parked,x.sources.restored_boundary,finished_stage?&*finished_stage:nullptr,p->revocation);
  ok=completion&&completion->publish();
 }}catch(...){ok=false;}
 if(!ok&&p->stage_group)p->stage_group->invalidate();
 // This old controller is never reattached. Restored bytes grant no admission;
 // fresh scratch/process/model/thread proofs and a Pending front are separate.
 x.fault=true;x.state=State::Quarantined;x.run_life->alive=false;
 if(p->current_owner){auto&live=*p->current_owner->impl_;live.fault=true;live.state=State::Quarantined;live.run_life->alive=false;}
 if(retain_fence)*retain_fence=std::move(p->fence);permit.pending_.reset();return ok;
}
bool Context::suspended_for_restore(const story_native_restore_resume::Boundary&b)const noexcept{
 try{const auto&x=*impl_;const auto&owner=b.completion().current_owner();
  return b.current(threads::current())&&owner&&owner->owner_==this&&owner->life_==x.run_life&&owner->expected_==x.expected&&owner->epoch_==b.completion().restore_epoch()&&x.run_life->restore_revoked&&!x.run_life->alive&&x.fault&&x.controller&&x.controller->restore_revoked_;
 }catch(...){return false;}
}
bool Context::restore_front_current(const story_native_restore_resume::Boundary&b,const Context*observed)noexcept{
 try{const auto&proof=b.completion().current_owner();
  if(!b.current(threads::current())||!proof||proof->owner_!=observed||proof->epoch_!=b.completion().restore_epoch())return false;
  if(proof->context_slot_){
   if(!proof->depth_slot_||proof->context_slot_->load(std::memory_order_acquire)!=observed)return false;
   const auto*depth=proof->depth_slot_->load(std::memory_order_acquire);
   if(depth&&(depth!=proof->prior_depth_||!observed||depth!=&observed->impl_->depth))return false;
  }else if(!observed)return false;
  return observed?observed->suspended_for_restore(b):!proof->life_;
 }catch(...){return false;}
}
void Context::request_cancel()noexcept{impl_->explicit_cancel.store(true,std::memory_order_release);impl_->cancel.store(true,std::memory_order_release);}
void Context::invalidate()noexcept{impl_->invalidate();}
void Context::outer_return(Cpu*c,const story_native_outer_return::OuterReturn&outer)noexcept{
 auto&x=*impl_;
 // Diagnostic data from the actual issued callback, never a synthetic return
 // or saved CPU. No token is claimed for ordinary non-cancellation frames.
 if(!x.cancel_returned){
  try{if(!x.cancel_outer_logged&&c&&outer.current(c,x.expected)&&(std::getenv("WWHD_TEST_NATIVE_CANCEL_EXECUTION_PROBE")||std::getenv("WWHD_TEST_NATIVE_CANCEL_AFTER_TICKS"))){x.cancel_outer_logged=true;LOG("[native_wait] cancel outer return boundary sp=%08X serial=%llu native_calls=0",c->r[1],static_cast<unsigned long long>(x.depth.snapshot().serial));}}catch(...){}
  return;
 }
 if(x.cancel_cleanup_attempted){x.invalidate();return;}x.cancel_cleanup_attempted=true;
 namespace X=story_native_cancel_observation;namespace U=story_native_wait_runtime_cancel_consumers;
 try{
  if(!c||!x.thread(c)||x.fault||x.run_life->restore_revoked||!x.retained(x.expected)||!x.sources.model_graph||!x.sources.model_graph->prepared()||!x.cancel_returned->matches(x.expected)||outer.root_serial()!=x.cancel_returned->after().outer_serial||!outer.claim(c,x.expected))throw std::runtime_error("cancel outer return admission");
  auto make=[&](story_native_wait_runtime_model_graph::Binding&binding){return U::Adapter::make(x.expected,x.arena,x.session,x.depth,c,outer,*x.sources.model_graph,binding,x.backend,*x.cancel_returned,{[owner=&x](const Expected&e){return owner->retained(e);},x.sources.write,x.sources.call,[owner=&x]{owner->invalidate();}});};
  X::Result closed,detached;
  {
   // Binding itself acquires physicalQ FIRST, then a real Session try-fence,
   // nonblocking Stage pin and exact native refresh dispatch guard.
     std::string why;auto binding=x.sources.model_graph->bind(why);if(!binding||!binding->current())throw std::runtime_error("cancel closed binding: "+why);
   if(!x.controller->prepare_cancel(c,outer))throw std::runtime_error("cancel actual idle entry fence");
   auto adapter=make(*binding);if(!adapter)throw std::runtime_error("cancel closed producer");
   X::Validator validator(x.expected,adapter->observation_sources());closed=validator.closed(c,x.depth,outer,*x.cancel_returned);
   if(!closed.valid()||!adapter->use_closed(closed)||x.controller->begin_cancel(c,outer,closed)!=W::State::Cancelling)throw std::runtime_error("cancel closed/write-ahead proof");
   const auto refresh=adapter->restore();if(!refresh)throw std::runtime_error("cancel exact consumer/native refresh");
   detached=validator.detached(c,x.depth,outer,closed,*refresh);if(!detached.valid()||!detached.current_binding(c,x.depth,outer))throw std::runtime_error("cancel detached proof");
   LOG("[native_wait] cancel detached root=%llu serial=%llu sp=%08X",static_cast<unsigned long long>(outer.root_serial()),static_cast<unsigned long long>(detached.serial()),c->r[1]);
  } // Validator/adapter die before Binding: Stage/fence release BEFORE Q thaw.
  {
   // Broad EndControl is deliberately outside physical parking and Stage pins.
   if(threads::current_thread_holds_native_quiescence()||story_native_stage_owner::current_thread_holds_lifetime_pin())throw std::runtime_error("cancel broad release still pinned");
   auto hold=x.session.try_fence();if(!hold||!outer.claimed_current(c,x.expected,x.depth))throw std::runtime_error("cancel release entry fence");
   struct ReleaseScope {Impl&owner;ReleaseScope(Impl&o,const X::Result&r,const story_native_outer_return::OuterReturn&t):owner(o){owner.cancel_release_proof=&r;owner.cancel_release_outer=&t;}~ReleaseScope(){owner.cancel_release_proof=nullptr;owner.cancel_release_outer=nullptr;}}release_scope(x,detached,outer);
   if(x.controller->release_cancel_native(c,outer,detached)!=W::State::Cancelling)throw std::runtime_error("cancel broad control release");
  }
  {
   // A distinct NEW physical park/readback validates exact retail manager,
   // flags/control/consumers while preserving the genuine partial image.
     std::string why;auto binding=x.sources.model_graph->bind(why);if(!binding||!binding->current())throw std::runtime_error("cancel released binding: "+why);
   auto adapter=make(*binding);if(!adapter||!adapter->use_closed(closed))throw std::runtime_error("cancel released producer");
   X::Validator validator(x.expected,adapter->observation_sources());const auto released=validator.released(c,x.depth,outer,detached);
   if(!released.released()||!released.current_binding(c,x.depth,outer)||x.controller->finish_cancel(c,outer,released)!=W::State::Released)throw std::runtime_error("cancel final retirement proof");
   x.cancel_returned.reset();x.state=State::Released;LOG("[native_wait] cancel release result=%u retained=%u",unsigned(x.state.load()),unsigned(x.retained(x.expected)));
  }
 }catch(const std::exception&e){LOG("[native_wait] cancel cleanup failure=%s",e.what());x.invalidate();}catch(...){LOG("[native_wait] cancel cleanup failure unknown");x.invalidate();}
}
bool Context::clean_cancel_routes_retired(const Cpu*c)const{
 const auto&x=*impl_;
 if(!c||!x.held()||!x.thread(c)||x.state!=State::Released||x.fault||x.restored_dormant||
    !x.run_life->alive||x.run_life->restore_revoked||x.run_life->pending_epoch||
    x.claim.load(std::memory_order_acquire)||x.retained(x.expected)||!x.controller||
    x.controller->state()!=W::State::Released||!x.controller->clean_cancel_retired(c))return false;
 const auto d=x.depth.snapshot();
 return d.coverage&&!d.fault&&d.state==D::State::Draining&&d.idle();
}
bool Context::outer(const Cpu*c,uint32_t f,D::Call&out)noexcept{
 auto&x=*impl_;
 auto denied=[&]()->bool{
  if(!c)return false;
  for(auto entry:{D::Entry::PeopleExecute,D::Entry::CameraExecute,D::Entry::ManagerRun}){
   if(f!=D::entry_address(entry))continue;
   out={entry,f,c->r[3],c->r[3],0,0,x.expected.identity,D::Authority::InvalidOwned};
   return true; // returning false would execute an unguarded native original
  }
  return false;
 };
 try{
  if(x.run_life->restore_revoked||x.restored_dormant)return denied();
  std::unique_ptr<story_native_wait_session::Session::Fence>hold;if(!x.held())hold=x.session.try_fence();
  if(!x.held()){x.diagnose("outer-fence",c,f,c?c->r[3]:0);x.invalidate();return denied();}
  // Genuine cancellation retired its claim before this later retail root.
  // Do not enter the old Draining Tracker; failed/active owners still guard.
  if(clean_cancel_routes_retired(c))return false;
  if(c&&x.retained(x.expected)){
   const bool candidate=(f==D::entry_address(D::Entry::ManagerRun)&&c->r[3]==x.expected.identity.manager)||
    (f==D::entry_address(D::Entry::CameraExecute)&&c->r[3]==x.expected.camera.address-0x248)||
    (f==D::entry_address(D::Entry::PeopleExecute)&&(c->r[3]==x.expected.actors[0].address||c->r[3]==x.expected.actors[1].address));
   if(candidate){M::Scope s;const bool source=x.backend.sources.session(s);if(!source||!x.live_receipt()||s.identity!=x.expected.identity||!s.exclusive||!s.bindings_verified||s.fault||s.foreign||s.foreign_depth){x.diagnose("outer-source",c,f,c->r[3],source,&s);x.invalidate();return denied();}}
  }
  const bool resolved=x.routes&&x.routes->outer(c,f,out);
  if(resolved&&out.authority==D::Authority::InvalidOwned)x.diagnose("outer-classification",c,f,c?c->r[3]:0);
  return resolved;
 }catch(...){x.diagnose("outer-exception",c,f,c?c->r[3]:0);x.invalidate();return denied();}
}
R::Decision Context::route(Cpu*c,uint32_t f,const std::function<void()>&next){
 auto&x=*impl_;
 struct Trace {Impl&x;uint32_t function,receiver;std::array<uint32_t,3>args;~Trace(){x.trace_function=function;x.trace_receiver=receiver;for(unsigned n=0;n<3;++n)x.trace_args[n]=args[n];}}trace{x,x.trace_function,x.trace_receiver,{x.trace_args[0].load(),x.trace_args[1].load(),x.trace_args[2].load()}};
 x.trace_function=f;x.trace_receiver=c?c->r[3]:0;for(unsigned n=0;n<3;++n)x.trace_args[n]=c?c->r[4+n]:0;
 auto refuse=[&]{if(c&&f==GC(0x02542D88))c->r[3]=0xFFFFFFFFu;else if(c&&f==GC(0x02542A38))c->r[3]=0;return R::Decision::Handled;};
 std::unique_ptr<story_native_wait_session::Session::Fence>hold;
 try{
 std::unique_ptr<story_native_camera_cancel_barrier::Barrier>cancel_ready_probe;
 if(x.run_life->restore_revoked||x.restored_dormant)return refuse();
 if(!x.held())hold=x.session.try_fence();
 if(!x.held()){x.diagnose("route-fence",c,f,x.trace_receiver);x.invalidate();return refuse();}
  if(clean_cancel_routes_retired(c))return R::Decision::Delegate;
  if(x.retained(x.expected)){
   M::Scope s;
   // Validate actual Stage/session/allocation for every retained entry. A
   // verified foreign aggregate remains foreign; it is never converted into
   // an owned scope just to satisfy this lifetime check.
   const bool source=x.backend.sources.session(s);if(!source||!x.live_receipt()||s.identity!=x.expected.identity||!s.exclusive||!s.bindings_verified||s.fault){x.diagnose("route-source",c,f,x.trace_receiver,source,&s);x.invalidate();return refuse();}
   if(x.depth.current_owned_call()&&(s.foreign||s.foreign_depth)){x.diagnose("route-foreign-owned",c,f,x.trace_receiver,source,&s);x.invalidate();return refuse();}
  }
  if(c&&x.active_cancel_execution){
   if(f==GC(0x0254465C)&&x.active_cancel_execution->native_end_proc_scope(c,f,c->r[3],int32_t(c->r[4]),c->r[5])){next();return R::Decision::Handled;}
   if(f==GC(0x02542C9C)){auto value=x.active_cancel_execution->route_all_off(c,next);if(value){c->r[3]=*value;return R::Decision::Handled;}}
  }
  // Private read-only prerequisite probe. It cannot invoke force1 or Run;
  // its actual-frame/Stage captures live only through this existing route.
  const char*probe_env=std::getenv("WWHD_TEST_NATIVE_CANCEL_READY_PROBE");
  if(!x.cancel_ready_probed&&x.cancel_probe_attempts<120&&f==GC(0x024FE3E8)&&probe_env&&std::strcmp(probe_env,"1")==0&&x.expected.program&&
     x.expected.program->kind()==story_native_event_program::Kind::TurnCamera){
   story_native_event_observation::Camera observed;
   if(x.raw_camera(observed)&&observed.current_type==x.expected.camera.event_type&&observed.next_type==x.expected.camera.event_type&&observed.elapsed11C){
    ++x.cancel_probe_attempts;
    auto source=story_native_wait_runtime_cancel_camera::make(x.session,x.depth,x.expected,x.arena,x.stage,x.ticket,c,x.backend,
      {[owner=&x](const Expected&e){return owner->retained(e);},[owner=&x]{owner->invalidate();}});
    const bool factory=bool(source);bool ready=false;
    if(source){cancel_ready_probe=std::make_unique<story_native_camera_cancel_barrier::Barrier>(x.expected,std::move(*source));ready=bool(cancel_ready_probe->ready(c,f));}
    if(ready||!x.cancel_probe_refused)LOG("[native_wait] cancel ready probe factory=%u ready=%u type=%u next=%u style=%u elapsed=%u serial=%llu attempt=%u native_calls=0",
      unsigned(factory),unsigned(ready),observed.current_type,observed.next_type,observed.style,observed.elapsed11C,static_cast<unsigned long long>(x.depth.snapshot().serial),x.cancel_probe_attempts);
    if(ready)x.cancel_ready_probed=true;else x.cancel_probe_refused=true;
   }
  }
  // Private scheduler/closed-dispatch prerequisite only. probe never calls
  // force1 or the original Run; each attempt releases Stage before thawing.
  const char*execution_env=std::getenv("WWHD_TEST_NATIVE_CANCEL_EXECUTION_PROBE");
  if((!x.cancel_execution_probed||!x.cancel_model_pending_probed)&&x.cancel_execution_attempts<120&&f==GC(0x024FE3E8)&&execution_env&&std::strcmp(execution_env,"1")==0&&x.expected.program&&x.expected.program->kind()==story_native_event_program::Kind::TurnCamera){
   story_native_event_observation::Camera observed;
   if(x.raw_camera(observed)&&observed.current_type==x.expected.camera.event_type&&observed.next_type==x.expected.camera.event_type&&observed.elapsed11C){
    ++x.cancel_execution_attempts;
    auto execution=story_native_wait_runtime_manager_cancel::Execution::prepare(x.session,x.depth,x.expected,x.arena,x.stage,x.ticket,c,x.backend,
      {[owner=&x](const Expected&e){return owner->retained(e);},x.sources.write,x.sources.call,[owner=&x]{owner->invalidate();}});
    story_native_wait_runtime_manager_cancel::Probe proof;if(execution)proof=execution->probe(c);
    const bool ready=execution&&proof.quiescent&&proof.dispatch&&proof.current;
    if(ready||!x.cancel_execution_refused)LOG("[native_wait] cancel execution probe factory=%u quiescent=%u dispatch=%u current=%u reason=%s attempt=%u native_calls=0",unsigned(bool(execution)),unsigned(proof.quiescent),unsigned(proof.dispatch),unsigned(proof.current),proof.reason.c_str(),x.cancel_execution_attempts);
    if(ready)x.cancel_execution_probed=true;else x.cancel_execution_refused=true;
    if(!x.cancel_model_pending_probed){
     std::string why;bool inspected=false;
     const auto frame=x.depth.current_owned_frame();
     if(x.sources.model_graph&&x.sources.model_graph->prepared()&&frame){auto proof=x.sources.model_graph->inspect_pending_camera(*frame,why);inspected=proof&&proof->current();}
     else why="missing authentic preclaim model";
     if(inspected||!x.cancel_model_pending_refused)LOG("[native_wait] cancel model pending inspection actual=%u reason=%s attempt=%u native_calls=0",unsigned(inspected),why.c_str(),x.cancel_execution_attempts);
     if(inspected)x.cancel_model_pending_probed=true;else x.cancel_model_pending_refused=true;
    }
   }
  }
  // Dedicated private cancellation driver. All readiness is authenticated at
  // this exact pending original; a refusal leaves the genuine Run to delegate.
  const char*cancel_after=std::getenv("WWHD_TEST_NATIVE_CANCEL_AFTER_TICKS");
  const bool queued_cancel=x.cancel.load(std::memory_order_acquire);
  if((queued_cancel||cancel_after)&&!x.cancel_driver_done&&x.cancel_driver_attempts<120&&f==GC(0x024FE3E8)&&x.expected.program&&x.expected.program->kind()==story_native_event_program::Kind::TurnCamera){
   unsigned trigger=0;bool valid_trigger=queued_cancel;
   if(!queued_cancel){const auto end=cancel_after+std::strlen(cancel_after);const auto parsed=std::from_chars(cancel_after,end,trigger);valid_trigger=parsed.ec==std::errc{}&&parsed.ptr==end&&trigger&&trigger<x.expected.program->watchdog();}
   auto refused=[&](const std::string&why){if(!x.cancel_driver_refused)LOG("[native_wait] cancel driver refused reason=%s native_calls=0",why.c_str());x.cancel_driver_refused=true;};
   if(!valid_trigger){x.cancel_driver_done=true;refused("invalid private trigger");}
   else {
    story_native_event_collect::Clock clock;
    if(x.backend.active_clock&&x.backend.active_clock(clock)&&clock.identity==x.expected.identity&&clock.elapsed_ticks>=trigger){
     ++x.cancel_driver_attempts;const auto frame=x.depth.current_owned_frame();std::string why;
     if(!x.sources.model_graph||!x.sources.model_graph->prepared()||!frame){x.cancel_driver_done=true;refused("missing genuine preclaim model");}
     else {
      auto inspection=x.sources.model_graph->inspect_pending_camera(*frame,why);
      if(!inspection||!inspection->current())refused(why.empty()?"pending model inspection":why);
      else if(!inspection->refresh_ready()){x.cancel_driver_done=true;refused("refresh admission or native stack budget closed");}
      else {
       auto camera_source=story_native_wait_runtime_cancel_camera::make(x.session,x.depth,x.expected,x.arena,x.stage,x.ticket,c,x.backend,{[owner=&x](const Expected&e){return owner->retained(e);},[owner=&x]{owner->invalidate();}});
       auto execution=story_native_wait_runtime_manager_cancel::Execution::prepare(x.session,x.depth,x.expected,x.arena,x.stage,x.ticket,c,x.backend,{[owner=&x](const Expected&e){return owner->retained(e);},x.sources.write,x.sources.call,[owner=&x]{owner->invalidate();}});
       if(!camera_source||!execution)refused("missing native cancellation factory");
       else {
        story_native_camera_cancel_barrier::Barrier returned(x.expected,std::move(*camera_source));const auto ready=returned.ready(c,f);
        if(!ready||!inspection->current()||!inspection->refresh_ready())refused("native pending Run readiness");
        else {
         x.active_cancel_execution=execution.get();story_native_manager_cancel::Result result;
         try{result=execution->run(c);}catch(...){x.active_cancel_execution=nullptr;throw;}
         x.active_cancel_execution=nullptr;
         if(result==story_native_manager_cancel::Result::Refused&&!x.fault)refused("native cancellation preflight");
         else {
          x.cancel_driver_done=true;
          if(result!=story_native_manager_cancel::Result::Completed||returned.run(c,f,*ready,execution->completion(),next)!=story_native_camera_cancel_barrier::Result::Returned)throw std::runtime_error("native cancellation/Run barrier");
          x.cancel_returned=returned.completion();if(!x.cancel_returned->matches(x.expected))throw std::runtime_error("native cancellation return receipt");
          LOG("[native_wait] cancel driver returned elapsed=%u root=%llu",clock.elapsed_ticks,static_cast<unsigned long long>(x.cancel_returned->after().outer_serial));
          return R::Decision::Handled;
         }
        }
       }
      }
     }
    }
   }
  }
  // Scoped casting must precede the old World Actors candidate filter. Only
  // the helper's actual synchronous startProc scope can select SDK actors;
  // Delegate preserves that recorded prior owner exactly once below.
  if(f==GC(0x02543C6C)||f==GC(0x02542A38)){
   const auto decision=f==GC(0x02543C6C)?x.casting->start(c,next):x.casting->candidate(c);
   if(x.casting->fault()){x.diagnose("route-casting",c,f,x.trace_receiver);x.invalidate();return refuse();}
   if(decision==story_native_wait_actor_cast::Decision::Handled)return R::Decision::Handled;
  }
  if(auto owned=x.depth.current_owned_call())for(unsigned n=0;n<2;++n)if(owned->subject==x.expected.actors[n].address&&owned->identity==x.expected.identity){
   const std::array<D::Entry,7>entries={D::Entry::PeopleExecute,D::Entry::PeopleEventMove,D::Entry::CommonCut,D::Entry::WaitStart,D::Entry::WaitProcess,D::Entry::TurnStart,D::Entry::TurnProcess};
   for(unsigned k=0;k<entries.size();++k)if(f==D::entry_address(entries[k]))++x.owned_entries[n][k];
  }
  auto result=x.routes->route(c,f,next);if(result==R::Decision::Delegate)next();if(x.routes->fault()||x.people->fault()||x.camera->quarantined()||x.terminal->quarantined()){x.diagnose("route-helper",c,f,x.trace_receiver);x.invalidate();return refuse();}return R::Decision::Handled;}catch(...){x.diagnose("route-exception",c,f,x.trace_receiver);x.invalidate();refuse();throw;}
}
State Context::tick(Cpu*c,bool allow_start){
 auto&x=*impl_;if(x.restored_dormant)return state();std::unique_ptr<story_native_wait_session::Session::Fence>hold;if(!x.held())hold=x.session.try_fence();
 if(!x.held()||!x.thread(c)){x.invalidate();return state();}
 try{
  if(x.fault||x.casting->fault()||x.routes->fault()||x.people->fault()||x.camera->quarantined()||x.terminal->quarantined()){x.invalidate();return state();}
  if(state()==State::Ready){if(x.explicit_cancel)return x.state=State::Refused;if(!allow_start)return state();if(!x.activation_valid())throw std::runtime_error("native scene activation changed");auto status=x.controller->start(c);LOG("[native_wait] runtime start result=%u",unsigned(status));if(status!=W::State::Started){if(status==W::State::Refused&&!retained())x.state=State::Refused;else x.invalidate();return state();}x.state=State::Started;LOG("[native_wait] runtime event published");return state();}
  if(state()==State::Released||state()==State::Refused||state()==State::Quarantined)return state();
  const char*queue_after=std::getenv("WWHD_TEST_NATIVE_QUEUE_CANCEL_AFTER_TICKS");
  if(queue_after&&!x.cancel_queue_probed&&x.expected.program&&x.expected.program->kind()==story_native_event_program::Kind::TurnCamera){
   unsigned trigger=0;const auto end=queue_after+std::strlen(queue_after);const auto parsed=std::from_chars(queue_after,end,trigger);
   if(parsed.ec!=std::errc{}||parsed.ptr!=end||!trigger||trigger>=x.expected.program->watchdog())throw std::runtime_error("queued cancel private trigger");
   story_native_event_collect::Clock clock;M::Scope actual;const auto d=x.depth.snapshot();
   if(!x.scope(actual)||d.state!=D::State::Active||!d.coverage||!d.idle()||!x.backend.config.collector.clock||!x.backend.config.collector.clock(clock)||clock.identity!=x.expected.identity||clock.logic_step!=actual.logic_step||clock.serial!=actual.serial)throw std::runtime_error("queued cancel host boundary");
   if(clock.elapsed_ticks>=trigger){request_cancel();x.cancel_queue_probed=true;LOG("[native_wait] cancel host queued elapsed=%u step=%llu serial=%llu idle=1 native_calls=0",clock.elapsed_ticks,static_cast<unsigned long long>(clock.logic_step),static_cast<unsigned long long>(clock.serial));}
  }
  if(x.terminal->completed()){
   if(!x.camera->returned()){x.state=State::AwaitCamera;return state();}
   x.stopping=true;auto status=x.controller->stop_terminal(c,*x.terminal,*x.camera);x.stopping=false;LOG("[native_wait] runtime stop result=%u",unsigned(status));if(status!=W::State::Released)x.invalidate();else {
    x.state=State::Released;
    const auto journal=x.session.journal();
    if(x.sources.activation&&!x.explicit_cancel){
     if(!x.activation_valid()||!x.terminal->completed()||!x.camera->returned()||x.retained(x.expected)||!journal||journal->phase!=J::Phase::Unpublished)throw std::runtime_error("native scene completion proof");
     x.natural_completion=story_native_scene_completion::Completion(x.expected,x.sources.activation,x.run_life);
    }
    for(unsigned n=0;n<2;++n)LOG("[native_wait] released owned entry counts actor=%u execute=%llu event_move=%llu common=%llu wait_start=%llu wait_process=%llu",n,static_cast<unsigned long long>(x.owned_entries[n][0].load()),static_cast<unsigned long long>(x.owned_entries[n][1].load()),static_cast<unsigned long long>(x.owned_entries[n][2].load()),static_cast<unsigned long long>(x.owned_entries[n][3].load()),static_cast<unsigned long long>(x.owned_entries[n][4].load()));
    for(unsigned n=0;n<2;++n)LOG("[native_wait] released TURN entry counts actor=%u start=%llu process=%llu",n,static_cast<unsigned long long>(x.owned_entries[n][5].load()),static_cast<unsigned long long>(x.owned_entries[n][6].load()));
    LOG("[native_wait] released native receipts casting=%u terminal=%u camera_returned=%u retained=%u",unsigned(x.casting->initialized(x.expected)),unsigned(x.terminal->completed()),unsigned(x.camera->returned()),unsigned(x.retained(x.expected)));
   }return state();
  }
  std::array<uint8_t,4>event{};if(!x.backend.sources.read(x.expected.identity.image+0xE4,event))throw std::runtime_error("native WAIT state read");
  // The normal CLOSE intent is consumed only by the independently verified
  // Terminal path. Early cancellation uses the dedicated pending CameraRun
  // and genuine outer-return path; other cancellation intents remain denied.
  if(x.cancel&&(word(event)!=4||x.end->intent().force_seen||x.explicit_cancel)){
   story_native_event_collect::Clock clock;M::Scope actual;
   if(x.cancel_driver_done||x.cancel_driver_attempts>=120||!x.expected.program||x.expected.program->kind()!=story_native_event_program::Kind::TurnCamera||!x.sources.model_graph||!x.sources.model_graph->prepared()||!x.scope(actual)||!x.backend.config.collector.clock||!x.backend.config.collector.clock(clock)||clock.identity!=x.expected.identity||clock.logic_step!=actual.logic_step||clock.serial!=actual.serial||clock.elapsed_ticks>=x.expected.program->watchdog())throw std::runtime_error("native queued cancel boundary/deadline");
   return state(); // retain for the next genuine pending CameraRun; no native call here
  }
  if(word(event)==4){
   if(!x.session.persist_depth(x.depth.record())||!x.depth.begin_drain()||!x.ensure_terminal()||x.terminal->close()!=T::Result::Completed||!x.camera->arm(x.terminal->completion())||!x.depth.cancel_drain())throw std::runtime_error("native WAIT terminal close");
   LOG("[native_wait] runtime terminal closed; awaiting actual camera Run");x.cancel.store(false);x.state=State::AwaitCamera;return state();
  }
  auto status=x.controller->observe(c);if(status==W::State::Quarantined||status==W::State::TerminalReady)throw std::runtime_error("native WAIT unexpected terminal");x.state=status==W::State::Running?State::Running:State::Started;return state();
 }catch(const std::exception&e){LOG("[native_wait] runtime tick exception: %s",e.what());x.stopping=false;x.invalidate();return state();}catch(...){LOG("[native_wait] runtime tick exception");x.stopping=false;x.invalidate();return state();}
}
}
