// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_controller.h"
#include "story_native_camera_policy.h"
#include "story_native_event_image.h"
#include <stdexcept>
#include <utility>
namespace story_native_wait_controller {namespace {
namespace O=story_native_event_observation;
namespace J=story_native_wait_journal;
namespace I=story_native_event_install;
struct Operation {
 bool&busy;Cpu*&active;
 Operation(bool&b,Cpu*&a,Cpu*c):busy(b),active(a){busy=true;active=c;}
 ~Operation(){active=nullptr;busy=false;}
};
}
Controller::Controller(story_native_wait_session::Session&s,D::Tracker&d,Expected e,Layout l,Stage stage,B::Config c,Sources sources):session_(s),depth_(d),expected_(std::move(e)),layout_(l),stage_(stage),config_(c),sources_(std::move(sources)){
 auto query=[this](M::Scope&s){return this->query(s);};
 auto allocation=[this](const auto&i,const auto&c){return sources_.validation.allocation&&sources_.validation.allocation(i,c);};
 auto raw_read=[this](uint32_t a,std::span<uint8_t>b){return sources_.validation.read&&sources_.validation.read(a,b);};
 auto raw_write=[this](uint32_t a,std::span<const uint8_t>b){return sources_.write&&sources_.write(a,b);};
 memory_=std::make_unique<M::Policy>(expected_,config_.memory,query,allocation,raw_read,raw_write);
 auto read=[this](uint32_t a,std::span<uint8_t>b){return memory_->read(a,b);};
 auto camera=[this,read](const auto&c,const auto&i){return story_native_camera_policy::verify(sources_.camera_tables,read,c,i).valid;};
 auto actor=[this](const auto&a,const auto&i){return sources_.validation.actor&&sources_.validation.actor(a,i);};
 auto native_allocation=[this,allocation](uint32_t image,uint32_t bytes,uint32_t stamp,uint32_t generation){return image==expected_.identity.image&&bytes==expected_.identity.bytes&&stamp==config_.memory.stamp&&generation==expected_.identity.generation&&allocation(expected_.identity,config_.memory);};
 auto guards=[this](story_native_event_collect::Guards&g){M::Scope s;if(!this->query(s))return false;g.exclusive=s.exclusive&&s.bindings_verified;g.foreign=s.foreign||s.fault;g.manager_depth=s.manager_depth;g.camera_depth=s.camera_depth;g.npc_depth=s.npc_depth;return !s.callback_depth&&!s.foreign_depth;};
 collector_=std::make_unique<story_native_event_collect::Collector>(expected_,config_.collector,read,actor,native_allocation,camera,guards);
 I::Backend backend;
 backend.read=read;
 backend.write=[this](uint32_t a,std::span<const uint8_t>b){if(memory_->write(a,b))return true;quarantine();return false;};
 backend.capture=[this](Cpu*c,O::Captured&out){return capture(c,out);};
 backend.scope=[this](const Expected&e){M::Scope s;return e==expected_&&this->query(s);};
 backend.detach=[this](Cpu*c,const Expected&e){return detach(c,e);};
 backend.invoke=[this](Cpu*c,uint32_t f,std::initializer_list<uint32_t>registers,std::initializer_list<uint32_t>stack){if(cancel_control_invoke_)return cancel_control_invoke_(c,f,registers,stack);return invoke(c,f,registers,stack);};
 install_=std::make_shared<I::Install>(std::move(backend),expected_);
}
Controller::~Controller(){
 if(rearm_seal_){std::lock_guard lock(rearm_seal_->mutex);rearm_seal_->live=false;rearm_seal_->verify={};}
}
std::optional<story_native_wait_session::Session::RearmCertificate> Controller::take_rearm_certificate(){
 if(cancel_rearm_factory_)return cancel_rearm_factory_(*this);
 if(rearm_issued_||state_!=State::Released||retained_||!session_.fence_held()||
    !release_scope_||!sources_.release)return {};
 const auto journal=session_.journal();
 if(!journal||journal->phase!=J::Phase::Unpublished||session_.quarantined())return {};
 auto seal=std::make_shared<story_native_wait_session::Session::RearmCertificate::Seal>();
 seal->owner=&session_;seal->expected=expected_;seal->journal=*journal;
 seal->verify=[session=&session_,depth=&depth_,expected=expected_,config=config_,sources=sources_,
               released_scope=*release_scope_,release_clock=release_clock_,release_cpu=release_cpu_,install=install_](Cpu*c){
 if(!c||c!=release_cpu||!session->fence_held()||!sources.retained||sources.retained(expected))return false;
 const auto snapshot=depth->snapshot();
 if(!snapshot.coverage||!snapshot.idle()||snapshot.state!=D::State::Draining||snapshot.serial!=released_scope.serial)return false;
 auto scope=[&](M::Scope&out){
  if(!sources.validation.session(out))return false;
  const auto&origin=released_scope;
  return out.identity==expected.identity&&out.link==origin.link&&out.link_pid==origin.link_pid&&
   out.logic_step==origin.logic_step&&out.serial==origin.serial&&out.fenced&&out.exclusive&&
   out.bindings_verified&&!out.fault&&!out.foreign&&!out.manager_depth&&!out.camera_depth&&
   !out.npc_depth&&!out.callback_depth&&!out.foreign_depth;
 };
 M::Scope before;if(!scope(before))return false;
 auto collector_config=config.collector;
 if(expected.program&&expected.program->kind()==story_native_event_program::Kind::TurnCamera){
  if(!release_clock)return false;
  collector_config.clock=[&](story_native_event_collect::Clock&out){M::Scope actual;if(!scope(actual))return false;out=*release_clock;return out.identity==actual.identity&&out.logic_step==actual.logic_step&&out.serial==actual.serial;};
 }
 auto allocation=[&](uint32_t image,uint32_t bytes,uint32_t stamp,uint32_t generation){return image==expected.identity.image&&bytes==expected.identity.bytes&&stamp==config.memory.stamp&&generation==expected.identity.generation&&sources.validation.allocation(expected.identity,config.memory);};
 auto camera=[&](const O::Camera&camera,const auto&identity){return story_native_camera_policy::verify(sources.camera_tables,sources.validation.read,camera,identity).valid;};
 auto guards=[&](story_native_event_collect::Guards&out){M::Scope actual;if(!scope(actual))return false;out={};out.exclusive=true;return true;};
 story_native_event_collect::Collector collector(expected,collector_config,sources.validation.read,sources.validation.actor,allocation,camera,guards);
 O::Captured now;if(!collector.collect(now))return false;
 const auto released=O::validate(O::Operation::ReleaseTerminal,expected,now);
 M::Scope after;
 const bool valid=released.released()&&install->can_free(released)&&scope(after);
 const auto final=depth->snapshot();
 return valid&&final.coverage&&final.state==D::State::Draining&&final.serial==snapshot.serial&&final.idle();

 };
 rearm_seal_=seal;rearm_issued_=true;
 return story_native_wait_session::Session::RearmCertificate(std::move(seal));
}

bool Controller::retained(const Expected&e)const noexcept{return retained_&&e==expected_;}
bool Controller::prerequisites()const{
 const bool turn=expected_.program&&expected_.program->kind()==story_native_event_program::Kind::TurnCamera;
 const auto&instance=turn?layout_.turn:layout_.wait;
 return (!turn||bool(sources_.arm_clock))&&session_.fence_held()&&sources_.validation.read&&sources_.validation.session&&sources_.validation.allocation&&sources_.validation.actor&&sources_.write&&sources_.call&&sources_.stack&&sources_.claim&&sources_.retained&&sources_.detach&&sources_.quarantine&&story_native_event_arena::valid(layout_)&&config_.memory.stamp==instance.stamp.address&&config_.collector.stamp==instance.stamp.address&&sources_.camera_tables.types==expected_.camera.type_table&&sources_.camera_tables.styles==expected_.camera.style_table&&sources_.camera_tables.type_count&&sources_.camera_tables.style_count&&expected_.identity.image==instance.image.address&&expected_.identity.bytes==instance.image.bytes;
}
bool Controller::query(M::Scope&out){
 out={};if(restore_revoked_||!active_cpu_||!session_.fence_held()||state_==State::Quarantined||!retained_||!sources_.retained(expected_)||!sources_.validation.session(out)||out.identity!=expected_.identity)return false;
 auto d=depth_.snapshot();return d.coverage&&d.idle()&&(d.state==D::State::Active||d.state==D::State::Draining)&&out.serial==d.serial&&out.fenced==(d.state==D::State::Draining)&&out.exclusive&&out.bindings_verified&&!out.foreign&&!out.fault&&!out.manager_depth&&!out.camera_depth&&!out.npc_depth&&!out.callback_depth&&!out.foreign_depth;
}
void Controller::quarantine()noexcept{
 if(rearm_seal_)rearm_seal_->live=false;
 state_=State::Quarantined;
 try{auto d=depth_.snapshot();if(d.state==D::State::Active&&d.idle())depth_.begin_drain();}catch(...){}
 // Once issued, retained_ is deliberately never revoked on a failure path.
 try{if(!restore_dormant_&&!restore_revoked_&&session_.fence_held())session_.quarantine_native();}catch(...){}
 if(quarantine_notified_)return;quarantine_notified_=true;
 try{if(sources_.quarantine)sources_.quarantine();}catch(...){}
}
bool Controller::capture(Cpu*c,O::Captured&out){
 if(c!=active_cpu_||!collector_){LOG("[native_wait] controller capture failed stage=configuration");return false;}
 M::Scope before,after;
 if(!query(before)){LOG("[native_wait] controller capture failed stage=before-scope phase=%u memory_error=%u",unsigned(memory_->phase()),unsigned(memory_->error()));return false;}
 if(!collector_->collect(out)){LOG("[native_wait] controller capture failed stage=collector error=%u phase=%u memory_error=%u",unsigned(collector_->error()),unsigned(memory_->phase()),unsigned(memory_->error()));return false;}
 if(!query(after)){LOG("[native_wait] controller capture failed stage=after-scope");return false;}
 const bool stable=before.identity==after.identity&&before.serial==after.serial&&before.logic_step==after.logic_step&&before.fenced==after.fenced;
 if(!stable)LOG("[native_wait] controller capture failed stage=changed before_serial=%llu after_serial=%llu before_step=%llu after_step=%llu",static_cast<unsigned long long>(before.serial),static_cast<unsigned long long>(after.serial),static_cast<unsigned long long>(before.logic_step),static_cast<unsigned long long>(after.logic_step));
 return stable;
}
uint32_t Controller::invoke(Cpu*c,uint32_t f,std::initializer_list<uint32_t>registers,std::initializer_list<uint32_t>stack){
 try{
  M::Scope scope;story_native_event_invoke::Config config;
  if(restore_dormant_||restore_revoked_||c!=active_cpu_||!query(scope)||!scope.fenced||!sources_.stack(c,config))throw std::runtime_error("native WAIT invocation boundary");
  config.protected_ranges.push_back({layout_.allocation.address,layout_.allocation.bytes});
  config.protected_ranges.push_back({expected_.identity.manager-0xF4,0x634});
  for(const auto&a:expected_.actors)config.protected_ranges.push_back({a.address,0x920});
  config.protected_ranges.push_back({expected_.camera.address-0x248,0xB28});
  story_native_event_invoke::Bridge bridge(std::move(config),sources_.validation.read,sources_.write,sources_.call);
  auto result=bridge.invoke(c,f,registers,stack);
  if(!result.ok||!result.restored)throw std::runtime_error("native WAIT invocation failed");
  return result.value;
 }catch(...){quarantine();throw;}
}
bool Controller::detach(Cpu*c,const Expected&e){
 if(c!=active_cpu_||e!=expected_)return false;
 try{
  O::Captured now;if(!capture(c,now))return false;
  auto terminal=O::validate(O::Operation::Terminal,expected_,now);
  if(!terminal.valid())return false;
  const auto journal=session_.journal();if(!journal)return false;
  if(journal->phase!=J::Phase::Draining&&journal->phase!=J::Phase::Detaching&&!session_.transition_native(J::Phase::Draining,terminal))return false;
  const auto draining=session_.journal();if(!draining)return false;
  if(draining->phase!=J::Phase::Detaching&&!session_.transition_native(J::Phase::Detaching,terminal))return false;
  if(!sources_.detach(c,e)){quarantine();return false;}
  return true;
 }catch(...){quarantine();return false;}
}
bool Controller::checkpoint_current(Cpu*c){
 if(c!=active_cpu_||!session_.fence_held()||!retained_)return false;
 const auto depth=depth_.snapshot();M::Scope before;
 if(!depth.coverage||depth.state!=D::State::Active||!depth.idle()||!query(before))return false;
 O::Captured now;if(!capture(c,now))return false;
 const auto operation=now.image.size()==expected_.identity.bytes&&now.image[0xE7]==1?O::Operation::Publish:O::Operation::Running;
 const auto result=O::validate(operation,expected_,now);if(!result.valid()||!session_.persist_depth(depth_.record()))return false;
 const bool turn=expected_.program&&expected_.program->kind()==story_native_event_program::Kind::TurnCamera;
 const uint32_t elapsed=turn?now.elapsed_ticks:0;
 const uint32_t flags=turn&&operation==O::Operation::Running?
  (uint32_t(now.flags[0])<<24|uint32_t(now.flags[1])<<16|uint32_t(now.flags[2])<<8|now.flags[3])&expected_.program->completion_mask():0;
 auto guard=[&]{M::Scope actual;const auto current=depth_.snapshot();return current.coverage&&current.state==D::State::Active&&
  current.idle()&&current.serial==depth.serial&&query(actual)&&actual.logic_step==before.logic_step&&actual.serial==before.serial&&
  actual.link==before.link&&actual.link_pid==before.link_pid;};
 return session_.checkpoint_native(result,elapsed,flags,guard,sources_.write)&&guard();
}
bool Controller::checkpoint(Cpu*c){
 if(busy_||!c||c!=started_cpu_||(state_!=State::Started&&state_!=State::Running)||!session_.fence_held())return false;
 Operation operation(busy_,active_cpu_,c);
 try{
  const auto depth=depth_.snapshot();
  if(depth.fault)throw std::runtime_error("checkpoint depth fault");
  if(!depth.coverage||depth.state!=D::State::Active||!depth.idle())return false;
  M::Scope before,after;
  if(!query(before))throw std::runtime_error("checkpoint source");
  // Native completion may precede the next host observation. NONE is not a
  // stable active snapshot and must not trigger terminal/native cleanup here.
  std::array<uint8_t,4> state{};
  if(!sources_.validation.read(expected_.identity.image+0xE4,state)||!query(after)||
     before.logic_step!=after.logic_step||before.serial!=after.serial)
   throw std::runtime_error("checkpoint state capture");
  const uint32_t native_state=uint32_t(state[0])<<24|uint32_t(state[1])<<16|uint32_t(state[2])<<8|state[3];
  if(native_state==0)return false;
  O::Captured now;if(!capture(c,now))throw std::runtime_error("checkpoint capture");
  const auto checked=O::validate(native_state==1?O::Operation::Publish:O::Operation::Running,expected_,now);
  // ORDER can be between native consumer initialization steps; deny this
  // snapshot without changing its journal or replaying any native operation.
  if(!checked.valid()&&native_state==1&&state_==State::Started)return false;
  if(!checked.valid())throw std::runtime_error("checkpoint observation");
  if(install_->observe(c)!=(native_state==1?I::Status::Started:I::Status::Running))
   throw std::runtime_error("checkpoint lease observation");
  if(!query(after)||after.logic_step!=before.logic_step||after.serial!=before.serial)
   throw std::runtime_error("checkpoint changed source");
  if(native_state!=1&&state_==State::Started){
   if(!session_.transition_native(J::Phase::Running,checked))throw std::runtime_error("checkpoint running journal");
   state_=State::Running;
  }
  if(checkpoint_current(c))return true;
 }catch(...){}
 quarantine();return false;
}
State Controller::start(Cpu*c){
 if(busy_||state_!=State::Empty||!c)return state_;
 if(!prerequisites())return state_=State::Refused;
 Operation operation(busy_,active_cpu_,c);
 try{
  auto staged=session_.staged_expected();auto d=depth_.snapshot();
  if(!staged||*staged!=expected_||session_.arena()!=layout_||!depth_.ready()||!d.coverage||!d.idle()||d.state!=D::State::Active||!session_.stage_depth(depth_.record()))return state_=State::Refused;
  retained_=true; // visible to routing BEFORE a claim callback or native entry
  if(!sources_.claim(expected_,layout_,stage_)||!sources_.retained(expected_)||!depth_.begin_drain())throw std::runtime_error("native WAIT claim refused");
  O::Captured now;if(!capture(c,now))throw std::runtime_error("native WAIT prepare capture");
  auto prepare=O::validate(O::Operation::Prepare,expected_,now);
  if(!prepare.valid()||!session_.begin_publication(prepare)||!memory_->transition(M::Phase::Preparing)||!memory_->transition(M::Phase::Publishing))throw std::runtime_error("native WAIT preparing seal");
  if(!install_||install_->start(c)!=I::Status::Started||!capture(c,now))throw std::runtime_error("native WAIT start failed");
  auto published=O::validate(O::Operation::Publish,expected_,now);
  if(!published.valid()||!session_.transition_native(J::Phase::Published,published)||!depth_.cancel_drain()||!memory_->transition(M::Phase::Running)||!session_.persist_depth(depth_.record()))throw std::runtime_error("native WAIT publish journal");
  if(expected_.program&&expected_.program->kind()==story_native_event_program::Kind::TurnCamera&&(!sources_.arm_clock||!sources_.arm_clock(published)))throw std::runtime_error("native TURN clock arm");
  if(!checkpoint_current(c))throw std::runtime_error("native publication checkpoint");
  started_cpu_=c;return state_=State::Started;
 }catch(...){quarantine();return state_;}
}
State Controller::observe(Cpu*c){
 if(busy_||!c||(state_!=State::Started&&state_!=State::Running))return state_;
 if(!session_.fence_held()){quarantine();return state_;}
 Operation operation(busy_,active_cpu_,c);
 bool diagnosed=false;
 auto diagnose=[&](const char*stage)noexcept{
  if(diagnosed)return;diagnosed=true;
  try{
   auto word=[](std::span<const uint8_t>b,unsigned p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];};
   const auto j=session_.journal();
   LOG("[native_wait] controller observation failure stage=%s controller=%u journal=%u session_quarantined=%u collector=%u memory_phase=%u memory_error=%u",stage,unsigned(state_),j?unsigned(j->phase):~0u,unsigned(session_.quarantined()),unsigned(collector_->error()),unsigned(memory_->phase()),unsigned(memory_->error()));
   for(unsigned n=0;n<2;++n){std::array<uint8_t,0x6C>cut{};std::array<uint8_t,4>status{};const auto&a=expected_.actors[n];const bool got=sources_.validation.read(a.address+0x3E0,cut)&&sources_.validation.read(a.address+0x2E0,status);
    LOG("[native_wait] controller actor=%u address=%08X read=%u cut=%08X/%08X/%08X/%08X status=%08X baseline_cut=%08X/%08X/%08X/%08X",n,a.address,unsigned(got),word(cut),word(cut,4),word(cut,8),word(cut,0x10),word(status),word(a.cut),word(a.cut,4),word(a.cut,8),word(a.cut,0x10));}
   std::array<uint8_t,0x540>manager{};const bool got_manager=sources_.validation.read(expected_.identity.manager,manager);unsigned flag_mismatch=~0u;for(unsigned p=0;p<expected_.retail_flags.size();++p){const uint8_t mask=p==3?0x3F:0;if((manager[0x3C+p]&~mask)!=(expected_.retail_flags[p]&~mask)){flag_mismatch=p;break;}}
   LOG("[native_wait] controller manager read=%u camera_play=%u current=%08X flags0=%08X flags_first_unowned_mismatch=%u baseline_byte=%02X actual_byte=%02X",unsigned(got_manager),word(manager,0x20),word(manager,0x53C),word(manager,0x3C),flag_mismatch,flag_mismatch<expected_.retail_flags.size()?unsigned(expected_.retail_flags[flag_mismatch]):0,flag_mismatch<expected_.retail_flags.size()?unsigned(manager[0x3C+flag_mismatch]):0);
   std::array<uint8_t,0x520>image{};const bool got_image=sources_.validation.read(expected_.identity.image,image);auto canonical=story_native_event_image::make_wait_fixture(expected_.frames);unsigned mismatch=~0u;
   if(got_image)for(unsigned p=0;p<image.size();++p){bool mutable_byte=p>=0xE4&&p<0xE8;for(unsigned n=0;n<4;++n)mutable_byte|=p>=0xF0+n*0x50+0x38&&p<0xF0+n*0x50+0x48;if(!mutable_byte&&image[p]!=canonical[p]){mismatch=p;break;}}
   LOG("[native_wait] controller image read=%u state=%u first_immutable_mismatch=%u expected=%02X actual=%02X",unsigned(got_image),word(image,0xE4),mismatch,mismatch<canonical.size()?unsigned(canonical[mismatch]):0,mismatch<image.size()?unsigned(image[mismatch]):0);
  }catch(...){LOG("[native_wait] controller observation diagnostic exception stage=%s",stage);}
 };
 try{
  if(!session_.persist_depth(depth_.record())){diagnose("persist-depth");throw std::runtime_error("native WAIT depth persistence");}
  O::Captured now;if(!capture(c,now)){diagnose("capture");throw std::runtime_error("native WAIT observation");}
  if(O::validate(O::Operation::Terminal,expected_,now).valid())return state_=State::TerminalReady;
  const auto checked=O::validate(now.image.size()==expected_.identity.bytes&&now.image[0xE7]==1?O::Operation::Publish:O::Operation::Running,expected_,now);
  if(!checked.valid()){LOG("[native_wait] controller pre-observe validation operation=%u reason=%u event_state=%u",unsigned(checked.operation()),unsigned(checked.reason()),checked.event_state());diagnose("pre-install-validation");}
  auto status=install_->observe(c);
  if(status==I::Status::Running){auto running=O::validate(O::Operation::Running,expected_,now);if(!running.valid()||(state_==State::Started&&!session_.transition_native(J::Phase::Running,running))||!checkpoint_current(c))throw std::runtime_error("native WAIT running checkpoint");return state_=State::Running;}
  if(status!=I::Status::Started){LOG("[native_wait] controller Install.observe returned=%u",unsigned(status));diagnose("install-observe");throw std::runtime_error("native WAIT observation failed");}
  if(!checkpoint_current(c))throw std::runtime_error("native published checkpoint");
  return state_;
 }catch(const std::exception&e){LOG("[native_wait] controller observe exception: %s",e.what());quarantine();return state_;}catch(...){LOG("[native_wait] controller observe exception");quarantine();return state_;}
}
State Controller::stop_terminal(Cpu*c,const story_native_manager_terminal::Terminal&t,const story_native_camera_return::Barrier&camera){
 if(busy_||!c||(state_!=State::Started&&state_!=State::Running&&state_!=State::TerminalReady))return state_;
 if(!session_.fence_held()){quarantine();return state_;}
 Operation operation(busy_,active_cpu_,c);
 try{
  if(!session_.persist_depth(depth_.record())||!depth_.begin_drain()||!memory_->transition(M::Phase::Draining)||!memory_->transition(M::Phase::Restoring))throw std::runtime_error("native WAIT terminal fence");
  O::Captured terminal_now;if(!capture(c,terminal_now))throw std::runtime_error("native terminal preflight capture");
  const auto terminal_check=O::validate(O::Operation::Terminal,expected_,terminal_now);
  LOG("[native_wait] controller terminal preflight valid=%u reason=%u camera_type=%u next=%u bg=%u trim=%u",unsigned(terminal_check.valid()),unsigned(terminal_check.reason()),terminal_now.camera.current_type,terminal_now.camera.next_type,terminal_now.camera.bg,terminal_now.camera.trim);
  auto camera_word=[](const auto&b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];};
  LOG("[native_wait] controller terminal retail baseline bg=%u trim=%u style=%u modes=%u/%u actual bg=%u trim=%u style=%u modes=%u/%u saved_type=%08X",expected_.camera.bg,expected_.camera.trim,expected_.camera.style,camera_word(expected_.camera.mode_phase13C,0),camera_word(expected_.camera.mode_phase13C,4),terminal_now.camera.bg,terminal_now.camera.trim,terminal_now.camera.style,camera_word(terminal_now.camera.mode_phase13C,0),camera_word(terminal_now.camera.mode_phase13C,4),camera_word(terminal_now.camera.event_data,0xC));
  const auto camera_diagnostic=O::diagnose_camera(expected_,terminal_now,true);
  LOG("[native_wait] controller terminal camera_failure=%u offset=%X actual=%08X expected=%08X",unsigned(camera_diagnostic.failure),camera_diagnostic.offset,camera_diagnostic.actual,camera_diagnostic.expected);
  LOG("[native_wait] controller terminal Work tags baseline=%08X actual=%08X",camera_word(expected_.camera.work37C,0),camera_word(terminal_now.camera.work37C,0));
  if(!terminal_check.valid())throw std::runtime_error("native terminal copied policy");
  if(install_->stop_terminal(c,t,camera)!=I::Status::Released)throw std::runtime_error("native WAIT terminal release");
  O::Captured now;if(!capture(c,now))throw std::runtime_error("native WAIT release capture");
  auto released=O::validate(O::Operation::ReleaseTerminal,expected_,now);
  if(!released.released()||!install_->can_free(released))throw std::runtime_error("native WAIT release proof");
  M::Scope release_scope;if(!query(release_scope))throw std::runtime_error("native WAIT release scope");
  if(expected_.program&&expected_.program->kind()==story_native_event_program::Kind::TurnCamera){
   story_native_event_collect::Clock clock;if(!config_.collector.clock||!config_.collector.clock(clock)||clock.identity!=expected_.identity||clock.logic_step!=release_scope.logic_step||clock.serial!=release_scope.serial)throw std::runtime_error("native TURN release clock");
   release_clock_=clock;
  }
  if(!session_.transition_native(J::Phase::Unpublished,released)||!memory_->transition(M::Phase::Closed))throw std::runtime_error("native WAIT release journal");
  release_scope_=release_scope;release_cpu_=c;
  if(sources_.release){if(!sources_.release(expected_))throw std::runtime_error("native WAIT claim release");retained_=false;}
  return state_=State::Released;
 }catch(const std::exception&e){LOG("[native_wait] controller terminal failure=%s collector=%u memory_phase=%u memory_error=%u",e.what(),unsigned(collector_->error()),unsigned(memory_->phase()),unsigned(memory_->error()));quarantine();return state_;}catch(...){LOG("[native_wait] controller terminal failure unknown");quarantine();return state_;}
}
}
