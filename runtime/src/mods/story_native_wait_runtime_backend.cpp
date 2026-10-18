// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_runtime_backend.h"
#include "guest_mods.h"
#include "story_native_wait_session.h"
#include "guest_addr.h"
#include <algorithm>
#include <memory>
#include <utility>
namespace story_native_wait_runtime_backend {namespace {
namespace D=story_native_event_depth;
using Boundary=story_native_wait_readiness::Boundary;
const uint32_t Play=GD(0x1046F0B0),Stage=GD(0x1047E6C4),Rooms=GD(0x1047E8F0);
bool span(uint32_t at,size_t bytes){return bytes&&uint64_t(at)+bytes<=0x100000000ULL&&
 ((at>=mem::kMem2Start&&uint64_t(at)+bytes<=mem::kMem2End)||
  (at>=mem::kRuntimeStart&&uint64_t(at)+bytes<=mem::runtime_top()&&uint64_t(at)+bytes<=mem::kHostStart));}
bool equal(const D::Snapshot&a,const D::Snapshot&b){return a.npc_depth==b.npc_depth&&a.camera_depth==b.camera_depth&&a.manager_depth==b.manager_depth&&a.callback_depth==b.callback_depth&&a.foreign_depth==b.foreign_depth&&a.fault==b.fault&&a.coverage==b.coverage&&a.state==b.state&&a.serial==b.serial;}
// Classification only: callers below obtain every fact from the actual held
// Session/Source. This private function cannot create a clock or lease receipt.
bool pending_clock(story_native_wait_journal::Phase phase,uint32_t state,uint64_t step,uint64_t initial,
 bool staged,bool sealed,bool retained){
 if(!step||step!=initial)return false;
 if(state==0)return phase==story_native_wait_journal::Phase::Preparing&&(staged||sealed);
 return state==1&&(phase==story_native_wait_journal::Phase::Preparing||phase==story_native_wait_journal::Phase::Published)&&sealed&&retained;
}
struct CandidatePin {
 std::shared_ptr<const threads::NativeQuiescence>park;
 std::optional<story_native_stage_owner::Tracker::Scan>scan;
 story_native_stage_owner_observer::Snapshot stage;
 Cpu*cpu{};story_native_event_game_thread::Ticket ticket;
 std::thread::id thread=std::this_thread::get_id();
 bool current()const{return thread==std::this_thread::get_id()&&park&&!park->read_only()&&park->current(cpu)&&
  story_native_event_game_thread::verify(cpu,ticket)&&scan&&scan->holds_lifetime_of(story_native_stage_owner_observer::tracker())&&
  scan->current()&&scan->receipt()==stage.receipt;}
};
struct Context {
 Expected expected;story_native_event_arena::Layout arena;Config config;
 story_native_stage_owner_observer::Snapshot stage;
 story_native_event_game_thread::Ticket ticket;Cpu*cpu;D::Tracker*depth;
 Callbacks callbacks;Boundary initial;
 std::optional<story_native_event_collect::Clock> origin,last_clock;
 uint32_t restored_elapsed=0;
 bool restored_clock=false;
 std::shared_ptr<StageBinding>stage_binding{};std::shared_ptr<CandidatePin>candidate_pin{};
 bool read_window()const{return !candidate_pin||candidate_pin->current();}
 bool turn()const{return expected.program&&expected.program->kind()==story_native_event_program::Kind::TurnCamera;}
 std::optional<uint32_t>elapsed(uint64_t step)const{
  if(!turn())return {};
  if(!origin)return 0;
  if(step<origin->logic_step)return {};
  const auto delta=step-origin->logic_step;const auto watchdog=expected.program->watchdog();
  if(delta>=watchdog||restored_elapsed>=watchdog-delta)return {};
  return uint32_t(delta+restored_elapsed);
 }
 story_native_event_arena::Instance selected()const{return turn()?arena.turn:arena.wait;}
 bool raw(uint32_t at,std::span<uint8_t>bytes)const{
  return span(at,bytes.size())&&callbacks.read&&callbacks.read(at,bytes);
 }
 bool scalar(uint32_t at,uint32_t&out)const{
  out=0;std::array<uint8_t,4>b{};if((at&3)||!raw(at,b))return false;
  out=uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];return true;
 }
 bool thread(Boundary&out)const{
  out={};uint32_t bottom=0,top=0,guest_bottom=0,guest_top=0;
  if(!cpu||threads::current()!=cpu||threads::current_thread()!=ticket.guest||
     !story_native_event_game_thread::verify(cpu,ticket)||
     !threads::current_stack_bounds(cpu,bottom,top)||bottom>=top||
     !span(bottom,uint64_t(top)-bottom)||!span(ticket.guest,0x39C)||
     !scalar(ticket.guest+0x394,guest_top)||!scalar(ticket.guest+0x398,guest_bottom)||
     // Native HyoiKam eventProc uses a 0x18-byte frame before its
     // read-only StaffLookup. Mutation bridges retain their own 16-byte gate.
     guest_top!=top||guest_bottom!=bottom||(cpu->r[1]&7)||
     uint64_t(bottom)+0x40>cpu->r[1]||cpu->r[1]>top||
     !callbacks.boundary||!callbacks.boundary(cpu,out)||
     out.guest_thread!=ticket.guest||out.stack_begin!=bottom||out.stack_end!=top||
     !out.exclusive_idle)return false;
  if(initial.guest_thread&&(out.guest_thread!=initial.guest_thread||
     out.stack_begin!=initial.stack_begin||out.stack_end!=initial.stack_end||
     out.logic_step<initial.logic_step))return false;
  return true;
 }
 bool allocation(const story_native_event_lease::Identity&i,const story_native_event_memory::Config&memory)const{
  const auto&want=config.memory;const auto allocation=selected();
  return i==expected.identity&&i.image==allocation.image.address&&i.bytes==allocation.image.bytes&&
   memory.stamp==want.stamp&&memory.link==want.link&&memory.link_pid==want.link_pid&&
   memory.record.address==want.record.address&&memory.record.bytes==want.record.bytes&&
   want.stamp==allocation.stamp.address&&want.record.address==allocation.record.address&&
   want.record.bytes==allocation.record.bytes&&
   story_native_event_arena::owns(arena,mem::runtime_alloc_log(),mem::runtime_top())&&
   callbacks.allocation&&callbacks.allocation(i,memory);
 }
 bool actor(const story_native_event_observation::Actor&a,const story_native_event_lease::Identity&i)const{
  if(i!=expected.identity)return false;
  for(const auto&want:expected.actors){
   if(a.address!=want.address)continue;
   return a.pid==want.pid&&a.token==want.token&&a.profile==want.profile&&a.subtype==want.subtype&&
    a.fingerprint==want.fingerprint&&a.profile==0x175&&a.subtype==1&&
    callbacks.resident&&callbacks.resident(a,i);
  }
  return false;
 }
 bool session()const{
  if(!callbacks.session||!callbacks.session->fence_held())return false;
  auto baseline=callbacks.session->baseline();
  if(!baseline)baseline=callbacks.session->staged_expected();
  return baseline&&*baseline==expected&&callbacks.baseline&&callbacks.baseline(expected);
 }
 void stage_history()const noexcept{
  try{const auto history=story_native_stage_owner_observer::mutation_history();
   for(size_t i=0;i<history.count;++i){const auto&e=history.events[i];
    LOG("[native_wait] Stage mutation seq=%llu phase=%u fn=%08X args=%08X/%08X/%08X/%08X cookie=%llu generation=%u/%u",
     static_cast<unsigned long long>(e.sequence),unsigned(e.phase),e.canonical,
     e.args.r3_r6[0],e.args.r3_r6[1],e.args.r3_r6[2],e.args.r3_r6[3],
     static_cast<unsigned long long>(e.cookie),e.before_generation,e.after_generation);
   }
  }catch(...){} // Passive output must not replace the original refusal.
 }
 bool query(story_native_event_memory::Scope&out)const{
  out={};try{
   if(!read_window()||!session()||!callbacks.fence||!callbacks.fence()||!callbacks.routes||!callbacks.routes()||
      !guestmods::routes_owned()||
      !allocation(expected.identity,config.memory))return false;
   Boundary entry;if(!thread(entry))return false;
   const auto before=depth->snapshot();
   if(!before.coverage||before.fault||(before.state!=D::State::Active&&before.state!=D::State::Draining)||
      (before.state==D::State::Draining&&!before.idle()))return false;
   auto scan=story_native_stage_owner_observer::tracker().try_scan();
   if(!scan.current()){
    LOG("[native_wait] backend Stage scan refused locked=%u valid=%u expected_generation=%llu observed_generation=%llu",
        unsigned(scan.holds_lifetime_of(story_native_stage_owner_observer::tracker())),unsigned(scan.valid()),
        static_cast<unsigned long long>(stage.receipt.generation),static_cast<unsigned long long>(scan.receipt().generation));
    stage_history();return false;
   }
   const auto current=story_native_stage_owner_observer::snapshot(scan);
   story_native_setinfo_candidate::Candidate candidate;
   const auto wanted=stage_binding?stage_binding->current(scan):std::optional(stage);
   if(!wanted||!scan.valid()||scan.receipt()!=wanted->receipt||!current||
      current->control!=wanted->control||current->receipt!=wanted->receipt||
      !story_native_setinfo_observation::inspect_live(wanted->control,wanted->receipt.owner,candidate)||
      candidate!=wanted->receipt.owner||!scan.current()){
    LOG("[native_wait] backend Stage binding refused expected_generation=%llu observed_generation=%llu owner=%u",
        static_cast<unsigned long long>(stage.receipt.generation),static_cast<unsigned long long>(scan.receipt().generation),unsigned(bool(current)));
    stage_history();return false;
   }
   uint32_t link=0,pid=0,stage_pid=0,room_pid=0;
   std::array<uint8_t,8>stage_name{};
   if(!scalar(Play+0x5B34,link)||link!=config.memory.link||!scalar(link+4,pid)||pid!=config.memory.link_pid||
      !scalar(Stage,stage_pid)||stage_pid!=expected.identity.stage||
      !scalar(Rooms+expected.room_number*0x22C,room_pid)||room_pid!=expected.identity.room||
      !raw(Play+0x5134,stage_name)||stage_name!=expected.stage_name)return false;
   for(const auto&a:expected.actors)if(!actor(a,expected.identity))return false;
   Boundary final;
   const auto after=depth->snapshot();
   if(!equal(before,after)||!scan.current()||!thread(final)||final!=entry||
      !callbacks.fence()||!callbacks.routes()||!guestmods::routes_owned()||
      !session()||!allocation(expected.identity,config.memory))return false;
   out.identity=expected.identity;out.link=link;out.link_pid=pid;
   out.exclusive=out.bindings_verified=true;
   out.fenced=after.state==D::State::Draining;
   out.foreign=after.foreign_depth!=0;out.fault=after.fault;
   out.npc_depth=after.npc_depth;out.camera_depth=after.camera_depth;out.manager_depth=after.manager_depth;
   out.callback_depth=after.callback_depth;out.foreign_depth=after.foreign_depth;
   out.logic_step=entry.logic_step;out.serial=after.serial;return true;
  }catch(...){out={};return false;}
 }
 bool idle(const story_native_event_memory::Scope&s)const{
  return s.identity==expected.identity&&s.exclusive&&s.bindings_verified&&!s.foreign&&!s.fault&&
   !s.npc_depth&&!s.camera_depth&&!s.manager_depth&&!s.callback_depth&&!s.foreign_depth&&s.logic_step;
 }
 static bool same_scope(const story_native_event_memory::Scope&a,const story_native_event_memory::Scope&b){
  return a.identity==b.identity&&a.link==b.link&&a.link_pid==b.link_pid&&a.exclusive==b.exclusive&&
   a.bindings_verified==b.bindings_verified&&a.fenced==b.fenced&&a.foreign==b.foreign&&a.fault==b.fault&&
   a.npc_depth==b.npc_depth&&a.camera_depth==b.camera_depth&&a.manager_depth==b.manager_depth&&
   a.callback_depth==b.callback_depth&&a.foreign_depth==b.foreign_depth&&a.logic_step==b.logic_step&&a.serial==b.serial;
 }
 bool clock(story_native_event_collect::Clock&out){
  out={};try{
   if(!turn())return false;
   story_native_event_memory::Scope before,after;
   if(!query(before)||!idle(before))return false;
   const auto journal=callbacks.session->journal();if(!journal)return false;
   uint32_t state=~0u;if(!scalar(expected.identity.image+0xE4,state))return false;
   if(!origin){
    // Prepare and the first Publish collection precede arm_clock. Both must
    // remain inside the exact factory boundary; ORDER needs the sealed Session
    // and a separately retained real controller claim, never a state-byte claim.
    const bool staged=state==0&&bool(callbacks.session->staged_expected());
    const bool sealed=(state==0||state==1)&&bool(callbacks.session->baseline());
    const bool retained=state==1&&callbacks.retained&&callbacks.retained(expected);
    if(!pending_clock(journal->phase,state,before.logic_step,initial.logic_step,staged,sealed,retained))return false;
   }else if(!callbacks.retained||!callbacks.retained(expected)||!callbacks.session->baseline()||
     journal->phase==story_native_wait_journal::Phase::Preparing||journal->phase==story_native_wait_journal::Phase::Unpublished||
     journal->phase==story_native_wait_journal::Phase::Quarantined||before.logic_step<origin->logic_step||before.serial<origin->serial||
     (!restored_clock&&!before.serial&&(state!=1||journal->phase!=story_native_wait_journal::Phase::Published||before.logic_step!=origin->logic_step)))return false;
   const auto ticks=elapsed(before.logic_step);
   if(!ticks||!query(after)||!idle(after)||!same_scope(before,after))return false;
   story_native_event_collect::Clock current{expected.identity,before.logic_step,before.serial,*ticks};
   if(last_clock&&(current.logic_step<last_clock->logic_step||current.serial<last_clock->serial||current.elapsed_ticks<last_clock->elapsed_ticks))return false;
   last_clock=current;out=current;return true;
  }catch(...){out={};return false;}
 }
 bool active_clock(story_native_event_collect::Clock&out){
  out={};try{
   if(!turn()||!origin||!callbacks.retained||!callbacks.retained(expected))return false;
   const auto witness=depth->current_owned_frame();if(!witness||!witness->current(*depth)||witness->expected()!=expected||witness->root_serial()!=witness->current_serial())return false;
   const auto&call=witness->root_call();if(call.entry!=D::Entry::CameraExecute||call.function!=D::entry_address(D::Entry::CameraExecute)||call.receiver!=expected.camera.address-0x248||call.subject!=call.receiver||call.pid!=expected.camera.pid||call.token||call.identity!=expected.identity)return false;
   story_native_event_memory::Scope before,after;
   if(!query(before)||before.fenced||before.camera_depth!=1||before.npc_depth||before.manager_depth||before.callback_depth||before.foreign_depth||before.foreign||before.fault||!before.logic_step)return false;
   const auto journal=callbacks.session->journal();const auto baseline=callbacks.session->baseline();
   if(!journal||!baseline||*baseline!=expected||(journal->phase!=story_native_wait_journal::Phase::Published&&journal->phase!=story_native_wait_journal::Phase::Running)||before.logic_step<origin->logic_step||before.serial<origin->serial)return false;
   const auto ticks=elapsed(before.logic_step);if(!ticks||!query(after)||!same_scope(before,after)||!witness->current(*depth))return false;
   story_native_event_collect::Clock current{expected.identity,before.logic_step,before.serial,*ticks};
   if(last_clock&&(current.logic_step<last_clock->logic_step||current.serial<last_clock->serial||current.elapsed_ticks<last_clock->elapsed_ticks))return false;
   last_clock=current;out=current;return true;
  }catch(...){out={};return false;}
 }
 bool arm(const story_native_event_observation::Result&published){
  try{
   if(!turn()||origin||!published.valid()||published.operation()!=story_native_event_observation::Operation::Publish||
     published.event_state()!=1||!published.matches(expected))return false;
   const auto journal=callbacks.session->journal();
   if(!journal||journal->phase!=story_native_wait_journal::Phase::Published||!callbacks.session->baseline()||
     !callbacks.retained||!callbacks.retained(expected))return false;
   story_native_event_collect::Clock now;
   if(!clock(now)||now.logic_step!=initial.logic_step)return false;
   uint32_t state=~0u;if(!scalar(expected.identity.image+0xE4,state)||state!=1)return false;
   story_native_event_collect::Clock final;if(!clock(final)||final!=now)return false;
   origin=now;return true;
  }catch(...){return false;}
 }
 bool restore_clock(const RestoreClockSeed&seed){
  try{
   if(!turn()||origin||!seed.current()||seed.expected()!=expected||
    seed.saved().identity!=expected.identity||seed.saved().elapsed_ticks>=expected.program->watchdog()||
    !callbacks.retained||!callbacks.retained(expected))return false;
   story_native_event_memory::Scope before,after;
   if(!query(before)||!idle(before)||!before.fenced||before.serial!=seed.saved().serial)return false;
   const auto journal=callbacks.session->journal();const auto baseline=callbacks.session->baseline();
   if(!journal||!baseline||*baseline!=expected||
    (journal->phase!=story_native_wait_journal::Phase::Published&&journal->phase!=story_native_wait_journal::Phase::Running)||
    !query(after)||!same_scope(before,after)||!seed.current())return false;
   // Keep the actual new host step as the base. Subtracting saved elapsed
   // from it could underflow after a load; the saved offset stays separate.
   origin=story_native_event_collect::Clock{expected.identity,before.logic_step,before.serial,0};
   restored_elapsed=seed.saved().elapsed_ticks;
   restored_clock=true;
   last_clock=story_native_event_collect::Clock{expected.identity,before.logic_step,before.serial,restored_elapsed};
   return true;
  }catch(...){return false;}
 }
};
Bundle bundle_for(const std::shared_ptr<Context>&state){
  Bundle result;result.config=state->config;result.stage_binding=state->stage_binding;
  if(state->turn()){result.config.collector.clock=[state](auto&out){return state->clock(out);};if(!state->candidate_pin)result.arm_clock=[state](const auto&published){return state->arm(published);};result.active_clock=[state](auto&out){return state->active_clock(out);};}
  if(state->turn()&&!state->candidate_pin)result.restore_clock=[state](const RestoreClockSeed&seed){return state->restore_clock(seed);};
  result.sources.read=[state](uint32_t at,std::span<uint8_t>bytes){
   try{return state->read_window()&&state->callbacks.session->fence_held()&&state->callbacks.fence()&&story_native_event_game_thread::verify(state->cpu,state->ticket)&&state->raw(at,bytes);}catch(...){return false;}
  };
  result.sources.actor=[state](const auto&a,const auto&i){try{return state->read_window()&&state->callbacks.session->fence_held()&&state->callbacks.fence()&&story_native_event_game_thread::verify(state->cpu,state->ticket)&&state->actor(a,i);}catch(...){return false;}};
  result.sources.allocation=[state](const auto&i,const auto&c){try{return state->read_window()&&state->callbacks.session->fence_held()&&state->callbacks.fence()&&story_native_event_game_thread::verify(state->cpu,state->ticket)&&state->allocation(i,c);}catch(...){return false;}};
  result.sources.session=[state](auto&scope){return state->query(scope);};
  return result;
}
}
struct StageBinding::State {
 std::weak_ptr<Context>source;
 story_native_stage_owner_observer::Snapshot original;
 std::shared_ptr<story_native_stage_binding::Cell>cell;
 bool candidate_only=false;
};
struct StageBinding::Candidate::State {Bundle value;std::shared_ptr<CandidatePin>pin;std::shared_ptr<StageBinding::State>origin;};
class StageBindingFactory {
public:
 static std::shared_ptr<StageBinding>make(const std::shared_ptr<Context>&source,bool candidate_only){
  auto s=std::make_shared<StageBinding::State>();s->source=source;s->original=source->stage;s->candidate_only=candidate_only;
  return std::shared_ptr<StageBinding>(new StageBinding(std::move(s)));
 }
 static StageBinding::Candidate candidate(Bundle value,std::shared_ptr<CandidatePin>pin,std::shared_ptr<StageBinding::State>origin){
  return StageBinding::Candidate(std::make_unique<StageBinding::Candidate::State>(StageBinding::Candidate::State{std::move(value),std::move(pin),std::move(origin)}));
 }
};
StageBinding::StageBinding(std::shared_ptr<State>s):state_(std::move(s)){}
StageBinding::Candidate::Candidate(std::unique_ptr<State>s):state_(std::move(s)){}
StageBinding::Candidate::Candidate(Candidate&&)noexcept=default;
StageBinding::Candidate&StageBinding::Candidate::operator=(Candidate&&)noexcept=default;
StageBinding::Candidate::~Candidate()=default;
const Bundle&StageBinding::Candidate::bundle()const{return state_->value;}
const void*StageBinding::Candidate::origin_binding()const{return state_?state_->origin.get():nullptr;}
const story_native_stage_owner_observer::Snapshot&StageBinding::Candidate::stage()const{return state_->pin->stage;}
std::shared_ptr<const threads::NativeQuiescence>StageBinding::Candidate::quiescence()const{return current()?state_->pin->park:nullptr;}
bool StageBinding::Candidate::current()const noexcept{try{return state_&&state_->pin&&state_->pin->current();}catch(...){return false;}}
bool StageBinding::matches_original(const story_native_stage_owner_observer::Snapshot&s)const noexcept{
 return state_&&s.control==state_->original.control&&s.receipt==state_->original.receipt;
}
std::optional<story_native_stage_owner_observer::Snapshot>StageBinding::current(
 const story_native_stage_owner::Tracker::Scan&scan)const noexcept{
 try{auto source=state_?state_->source.lock():nullptr;
  if(!source||!source->read_window()||!source->callbacks.session->fence_held()||
   !story_native_event_game_thread::verify(source->cpu,source->ticket)||
   !scan.holds_lifetime_of(story_native_stage_owner_observer::tracker())||!scan.current())return {};
  const auto cell=std::atomic_load(&state_->cell);if(cell)return cell->current(scan);
  if(scan.receipt()!=state_->original.receipt)return {};return state_->original;
 }catch(...){return {};}
}
bool StageBinding::attach(std::shared_ptr<story_native_stage_binding::Cell>cell,
 const threads::NativeQuiescence&q,const story_native_stage_owner::Tracker::Scan&scan){
 try{auto source=state_?state_->source.lock():nullptr;
  if(!source||state_->candidate_only||!cell||q.read_only()||!q.current(source->cpu)||
   !source->callbacks.session->fence_held()||!current(scan))return false;
  const auto actual=cell->current(scan);if(!actual||actual->control!=state_->original.control||actual->receipt!=state_->original.receipt)return false;
  std::shared_ptr<story_native_stage_binding::Cell>empty;
  return q.current(source->cpu)&&scan.current()&&std::atomic_compare_exchange_strong(&state_->cell,&empty,cell);
 }catch(...){return false;}
}
std::optional<StageBinding::Candidate>StageBinding::candidate(
 std::shared_ptr<const threads::NativeQuiescence>q,const story_native_stage_owner::Tracker::Scan&scan,
 story_native_stage_owner_observer::Snapshot next)const{
 try{auto source=state_?state_->source.lock():nullptr;
  if(!source||state_->candidate_only||!std::atomic_load(&state_->cell)||!q||q->read_only()||!q->current(source->cpu)||
   !source->callbacks.session->fence_held()||!story_native_event_game_thread::verify(source->cpu,source->ticket)||
   !scan.holds_lifetime_of(story_native_stage_owner_observer::tracker())||!scan.current()||
   next.receipt!=scan.receipt()||next.control!=state_->original.control||next.receipt.owner!=state_->original.receipt.owner)return {};
  const auto depth=source->depth->snapshot();const auto journal=source->callbacks.session->journal();
  if(!depth.idle()||depth.state!=D::State::Active||!depth.coverage||depth.fault||!journal||
   (journal->phase!=story_native_wait_journal::Phase::Published&&journal->phase!=story_native_wait_journal::Phase::Running)||
   !source->callbacks.retained||!source->callbacks.retained(source->expected))return {};
  const auto actual=story_native_stage_owner_observer::snapshot(scan);
  if(!actual||actual->control!=next.control||actual->receipt!=next.receipt)return {};
  auto pin=std::make_shared<CandidatePin>();pin->park=std::move(q);pin->stage=next;pin->cpu=source->cpu;pin->ticket=source->ticket;
  pin->scan.emplace(story_native_stage_owner_observer::tracker().try_scan());if(!pin->current())return {};
  auto copy=std::make_shared<Context>(*source);copy->stage=next;copy->candidate_pin=pin;
  copy->stage_binding=StageBindingFactory::make(copy,true);
  auto value=bundle_for(copy);story_native_event_memory::Scope scope;
  if(!value.sources.session(scope)||!pin->current())return {};
  return StageBindingFactory::candidate(std::move(value),std::move(pin),state_);
 }catch(...){return {};}
}
std::optional<Bundle> make(Expected expected,story_native_event_arena::Layout arena,Config config,
 story_native_stage_owner_observer::Snapshot stage,story_native_event_game_thread::Ticket ticket,
 Cpu*cpu,D::Tracker&depth,Callbacks callbacks){
 try{
  const bool turn=expected.program&&expected.program->kind()==story_native_event_program::Kind::TurnCamera;
  const auto selected=turn?arena.turn:arena.wait;
  if((expected.program&&expected.program->frames()!=expected.frames)||(turn&&(!callbacks.retained||config.collector.clock)))return {};
  if(!cpu||!ticket.epoch||!ticket.guest||!stage.control||!stage.receipt.generation||
     !story_native_event_arena::valid(arena)||expected.room_number>63||
     config.collector.stamp!=selected.stamp.address||config.memory.stamp!=config.collector.stamp||
     !callbacks.session||!callbacks.read||!callbacks.resident||!callbacks.fence||!callbacks.routes||
     !callbacks.baseline||!callbacks.boundary||!callbacks.allocation)return {};
  auto state=std::make_shared<Context>(Context{std::move(expected),arena,config,std::move(stage),ticket,cpu,&depth,std::move(callbacks),{},{},{},0,false});
  state->stage_binding=StageBindingFactory::make(state,false);
  if(!state->thread(state->initial))return {};
  std::array<uint8_t,24>manager{};
  if(!state->raw(state->expected.identity.manager+0x24,manager)||
     !std::equal(manager.begin(),manager.begin()+12,config.collector.manager_exception.begin())||
     !std::equal(manager.begin()+12,manager.end(),config.collector.manager_goal.begin()))return {};
  story_native_event_memory::Scope initial;if(!state->query(initial))return {};
  return bundle_for(state);
 }catch(...){return {};}
}
}
