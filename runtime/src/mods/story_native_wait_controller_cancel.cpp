// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_controller.h"
#include "runtime.h"
#include "guest_addr.h"
#include <algorithm>
#include <stdexcept>
#include <thread>
namespace story_native_wait_controller {namespace {
namespace C=story_native_cancel_observation;
namespace J=story_native_wait_journal;
namespace I=story_native_event_install;
struct Operation {
 bool&busy;Cpu*&active;
 Operation(bool&b,Cpu*&a,Cpu*c):busy(b),active(a){busy=true;active=c;}
 ~Operation(){active=nullptr;busy=false;}
};
}
struct Controller::CancelOrigin {
 C::Result released;M::Scope scope;Cpu*cpu=nullptr;
 std::thread::id thread=std::this_thread::get_id();
};
bool Controller::cancel_retired_current(const Cpu*c)const{try{
 if(!c||c!=started_cpu_||state_!=State::Released||retained_||restore_dormant_||restore_revoked_||
    !session_.fence_held()||session_.quarantined()||!cancel_origin_||!sources_.retained||
    sources_.retained(expected_)||cancel_origin_->cpu!=c||cancel_origin_->thread!=std::this_thread::get_id()||
    !cancel_origin_->released.released()||!cancel_origin_->released.matches(expected_)||
    !install_||!install_->can_free(cancel_origin_->released))return false;
 const auto baseline=session_.baseline();const auto journal=session_.journal();
 return baseline&&*baseline==expected_&&journal&&journal->phase==J::Phase::Unpublished;
}catch(...){return false;}}
std::optional<story_native_wait_session::Session::RearmCertificate>Controller::take_cancel_rearm_certificate(){
 if(rearm_issued_||state_!=State::Released||retained_||!session_.fence_held()||!cancel_origin_||!sources_.cancel_rearm||!sources_.validation.session||!sources_.retained||threads::current_thread_holds_native_quiescence()||story_native_stage_owner::current_thread_holds_lifetime_pin())return {};
 const auto journal=session_.journal();if(!journal||journal->phase!=J::Phase::Unpublished||session_.quarantined())return {};
 auto seal=std::make_shared<story_native_wait_session::Session::RearmCertificate::Seal>();
 seal->owner=&session_;seal->expected=expected_;seal->journal=*journal;
 seal->verify=[session=&session_,depth=&depth_,expected=expected_,sources=sources_,origin=cancel_origin_,install=install_,attempt=std::optional<M::Scope>{}](Cpu*c)mutable{
  if(!c||c!=origin->cpu||origin->thread!=std::this_thread::get_id()||!session->fence_held()||sources.retained(expected)||!origin->released.released()||!origin->released.matches(expected)||!install->can_free(origin->released)){LOG("[native_wait] cancel rearm verify refused stage=origin");return false;}
  const auto same=[](const M::Scope&a,const M::Scope&b){return a.identity==b.identity&&a.link==b.link&&a.link_pid==b.link_pid&&a.logic_step==b.logic_step&&a.serial==b.serial&&a.fenced==b.fenced&&a.exclusive==b.exclusive&&a.bindings_verified==b.bindings_verified&&a.foreign==b.foreign&&a.fault==b.fault&&a.manager_depth==b.manager_depth&&a.camera_depth==b.camera_depth&&a.npc_depth==b.npc_depth&&a.callback_depth==b.callback_depth&&a.foreign_depth==b.foreign_depth;};
  const auto query=[&](M::Scope&s){
   const auto d=depth->snapshot();if(!d.coverage||d.fault||!d.idle()||d.state!=D::State::Draining||d.serial<origin->scope.serial||!sources.validation.session(s))return false;
   return s.identity==expected.identity&&s.link==origin->scope.link&&s.link_pid==origin->scope.link_pid&&s.logic_step>=origin->scope.logic_step&&s.serial==d.serial&&s.fenced&&s.exclusive&&s.bindings_verified&&!s.foreign&&!s.fault&&!s.manager_depth&&!s.camera_depth&&!s.npc_depth&&!s.callback_depth&&!s.foreign_depth;
  };
  const auto deny=[](const char*stage){LOG("[native_wait] cancel rearm verify refused stage=%s",stage);return false;};
  M::Scope before,after;if(!query(before))return deny("before-source");
  if(attempt&&!same(*attempt,before))return deny("attempt-boundary");
  if(!sources.cancel_rearm(c,expected,before))return deny("fresh-source");
  if(!query(after))return deny("after-source");if(!same(before,after))return deny("source-change");
  if(sources.retained(expected))return deny("retained");
  // The synchronous OuterReturn has expired. Its genuine Released origin is
  // history, while each verification obtains NEW physical lifetime/idle proof.
  // Both sides of native readiness must stay at this fresh step and serial.
  attempt=after;return true;
 };
 rearm_seal_=seal;rearm_issued_=true;return story_native_wait_session::Session::RearmCertificate(std::move(seal));
}
bool Controller::prepare_cancel(Cpu*c,const story_native_outer_return::OuterReturn&outer){
 if(busy_||!c||(state_!=State::Started&&state_!=State::Running))return false;
 Operation operation(busy_,active_cpu_,c);try{
  M::Scope before,after;const auto d=depth_.snapshot();
  if(cancel_admission_||c!=started_cpu_||!session_.fence_held()||!outer.claimed_current(c,expected_,depth_)||!d.coverage||!d.idle()||d.state!=D::State::Active||!query(before)||before.fenced||before.serial!=d.serial)throw std::runtime_error("cancel active admission");
  // Persist the REAL Active idle record before fencing. Draining bytes are
  // never relabelled Active to satisfy the portable depth codec.
  if(!session_.persist_depth(depth_.record())||!depth_.begin_drain()||!outer.claimed_current(c,expected_,depth_)||!query(after)||!after.fenced||before.logic_step!=after.logic_step||before.serial!=after.serial)throw std::runtime_error("cancel entry fence");
  cancel_binding_=outer.binding();cancel_admission_=after;return true;
 }catch(const std::exception&e){LOG("[native_wait] cancellation admission failure=%s",e.what());quarantine();return false;}catch(...){quarantine();return false;}
}
State Controller::begin_cancel(Cpu*c,const story_native_outer_return::OuterReturn&outer,const C::Result&r){
 if(busy_||!c||(state_!=State::Started&&state_!=State::Running))return state_;
 Operation operation(busy_,active_cpu_,c);try{
  M::Scope scope;const auto poses=session_.baseline_poses();
  if(!cancel_admission_||!cancel_binding_.current(c,expected_,depth_,outer)||cancel_admission_->logic_step!=r.logic_step()||cancel_admission_->serial!=r.serial()||c!=started_cpu_||!session_.fence_held()||cancel_closed_||cancel_detached_||r.phase()!=C::Phase::Closed||!r.matches(expected_)||!poses||!r.matches_baseline(*poses)||!r.current_binding(c,depth_,outer)||!outer.claimed_current(c,expected_,depth_)||!query(scope)||!scope.fenced||scope.serial!=r.serial()||scope.logic_step!=r.logic_step())throw std::runtime_error("cancel closed source");
  if(!memory_->transition(M::Phase::Draining)||!memory_->transition(M::Phase::Restoring)||install_->begin_cancel(c,depth_,outer,r)!=I::Status::Draining)throw std::runtime_error("cancel closed lease");
  // Write-ahead journal is durable before a consumer adapter can restore even
  // one angle/cut/cache byte. Neither transition projects normal completion.
  if(!session_.transition_cancel_native(J::Phase::Draining,r)||!session_.transition_cancel_native(J::Phase::Detaching,r)||!r.current_binding(c,depth_,outer)||!outer.claimed_current(c,expected_,depth_)||!query(scope)||scope.serial!=r.serial()||scope.logic_step!=r.logic_step())throw std::runtime_error("cancel detach journal");
  cancel_closed_=r;return state_=State::Cancelling;
 }catch(const std::exception&e){LOG("[native_wait] cancellation begin failure=%s",e.what());quarantine();return state_;}catch(...){quarantine();return state_;}
}
State Controller::release_cancel_native(Cpu*c,const story_native_outer_return::OuterReturn&outer,const C::Result&r){
 if(busy_||!c||state_!=State::Cancelling)return state_;
 Operation operation(busy_,active_cpu_,c);try{
  M::Scope scope;const auto poses=session_.baseline_poses();const auto journal=session_.journal();
  if(threads::current_thread_holds_native_quiescence()||story_native_stage_owner::current_thread_holds_lifetime_pin()||c!=started_cpu_||!session_.fence_held()||!cancel_closed_||cancel_detached_||r.phase()!=C::Phase::Detached||!r.same_cancellation(*cancel_closed_)||!poses||!r.matches_baseline(*poses)||!journal||journal->phase!=J::Phase::Detaching||!r.current_binding(c,depth_,outer)||!outer.claimed_current(c,expected_,depth_)||!query(scope)||!scope.fenced||scope.serial!=r.serial()||scope.logic_step!=r.logic_step())throw std::runtime_error("cancel detached source");
  cancel_detached_=r; // native attempt is one-use even if it throws
  struct NativeRelease {Controller&owner;NativeRelease(Controller&o,const story_native_outer_return::OuterReturn&t):owner(o){owner.cancel_control_invoke_=[p=&o](Cpu*c,uint32_t f,std::initializer_list<uint32_t>a,std::initializer_list<uint32_t>s){return p->invoke_cancel_control(c,f,a,s);};owner.cancel_invoking_outer_=&t;}~NativeRelease(){owner.cancel_control_invoke_={};owner.cancel_invoking_outer_=nullptr;}}native_release(*this,outer);
  if(install_->release_cancel_native(c,depth_,outer,r)!=I::Status::Draining||!r.current_binding(c,depth_,outer)||!outer.claimed_current(c,expected_,depth_)||!query(scope)||scope.serial!=r.serial()||scope.logic_step!=r.logic_step())throw std::runtime_error("cancel control release");
  return state_;
 }catch(const std::exception&e){LOG("[native_wait] cancellation release failure=%s",e.what());quarantine();return state_;}catch(...){quarantine();return state_;}
}
uint32_t Controller::invoke_cancel_control(Cpu*c,uint32_t function,std::initializer_list<uint32_t>args,std::initializer_list<uint32_t>stack_args){
 try{
  namespace V=story_native_event_invoke;
  const auto end_control=GC(0x0253EFA8),clear_base=GC(0x0254273C);
  const bool ending=function==end_control;
  if(!c||c!=active_cpu_||c!=started_cpu_||state_!=State::Cancelling||!cancel_detached_||!cancel_invoking_outer_||stack_args.size()!=0||args.size()!=1||function!=(cancel_control_calls_==0?end_control:cancel_control_calls_==1?clear_base:0)||*args.begin()!=(ending?expected_.identity.manager-0xF4:expected_.identity.manager))throw std::runtime_error("cancel control capability");
  auto admitted=[&](M::Scope&scope){const auto journal=session_.journal();return !threads::current_thread_holds_native_quiescence()&&!story_native_stage_owner::current_thread_holds_lifetime_pin()&&session_.fence_held()&&journal&&journal->phase==J::Phase::Detaching&&cancel_detached_->current_binding(c,depth_,*cancel_invoking_outer_)&&query(scope)&&scope.fenced&&scope.serial==cancel_detached_->serial()&&scope.logic_step==cancel_detached_->logic_step();};
  M::Scope before;if(!admitted(before))throw std::runtime_error("cancel control boundary");
  V::Config config;if(!sources_.stack||!sources_.stack(c,config))throw std::runtime_error("cancel control stack source");
  auto valid=[](V::Range r){return r.address&&r.bytes&&uint64_t(r.address)+r.bytes<=0x100000000ull;};
  auto overlap=[](V::Range a,V::Range b){return uint64_t(a.address)<uint64_t(b.address)+b.bytes&&uint64_t(b.address)<uint64_t(a.address)+a.bytes;};
  auto protect=[&](V::Range r){if(!valid(r))return false;for(auto&old:config.protected_ranges){if(old.address<=r.address&&uint64_t(r.address)+r.bytes<=uint64_t(old.address)+old.bytes)return true;if(r.address<=old.address&&uint64_t(old.address)+old.bytes<=uint64_t(r.address)+r.bytes){old=r;return true;}}config.protected_ranges.push_back(r);return true;};
  const auto poses=session_.baseline_poses();if(!poses||!cancel_detached_->matches_baseline(*poses)||!protect({layout_.allocation.address,layout_.allocation.bytes})||!protect({expected_.identity.manager-0xF4,0x634})||!protect({expected_.camera.address-0x248,0xB28}))throw std::runtime_error("cancel control protected allocation");
  for(unsigned n=0;n<2;++n)if(!protect({expected_.actors[n].address,0x920})||!protect({(*poses)[n].identity.morf,0xC8})||!protect({(*poses)[n].identity.model,0x170}))throw std::runtime_error("cancel control protected model");
  // Audited exact mode3 EndControl has only a 0x10 native frame and no calls;
  // guest_call reserves 0x40. ClearBase has no frame/calls, only eight stores.
  // The 8-byte native ABI is observed at the real CameraExecute return. The
  // generic native Bridge retains its independent strict 16-byte contract.
  constexpr uint32_t budget=0x50;const uint32_t sp=c->r[1];
  if((sp&7)||sp<budget||!valid(config.stack)||config.protected_ranges.empty()||config.protected_ranges.size()>16||uint64_t(config.stack.address)+budget>sp||sp>uint64_t(config.stack.address)+config.stack.bytes)throw std::runtime_error("cancel control native stack");
  const V::Range used{sp-budget,budget};for(auto r:config.protected_ranges)if(!valid(r)||overlap(used,r))throw std::runtime_error("cancel control native overlap");
  const Cpu saved=*c;const uint32_t frame=sp-V::kFrameBytes;std::array<uint8_t,V::kFrameBytes>bytes{},readback{};
  if(!sources_.validation.read(frame,bytes))throw std::runtime_error("cancel control outgoing read");
  M::Scope current;if(!admitted(current)||current.logic_step!=before.logic_step||current.serial!=before.serial)throw std::runtime_error("cancel control changed source");
  std::array<uint8_t,0xF4>control{};if(!sources_.validation.read(expected_.identity.manager-0xF4,control))throw std::runtime_error("cancel control fresh mode");
  auto word=[&](unsigned p){return uint32_t(control[p])<<24|uint32_t(control[p+1])<<16|uint32_t(control[p+2])<<8|control[p+3];};
  if(control[0xC0]||control[0xC2]!=(ending?3:0)||(ending&&(word(0xC4)!=~0u||word(0xC8)!=~0u||word(0xCC)!=~0u||word(0xD0)!=~0u||control[0xD6]||control[0xD7]||control[0xD8]!=0xFF||control[0xD9]!=0xFF||control[0xDE]!=0xFF||control[0xDF]!=0xFF||control[0xE8]||control[0xE9])))throw std::runtime_error("cancel control mode3 closure");
  ++cancel_control_calls_;uint32_t result=0;bool called=false,restored=false;
  try{result=sources_.call(c,function,args);called=true;}catch(...){}
  // Callback guards inspect the ORIGINAL caller registers even on exception.
  *c=saved;
  try{const bool wrote=sources_.write(frame,bytes);const bool read=sources_.validation.read(frame,readback);restored=wrote&&read&&readback==bytes;}catch(...){restored=false;}
  *c=saved;M::Scope after;
  if(!called||!restored||!admitted(after)||after.logic_step!=before.logic_step||after.serial!=before.serial)throw std::runtime_error("cancel control call/restore");
  return result;
 }catch(...){quarantine();throw;}
}
State Controller::finish_cancel(Cpu*c,const story_native_outer_return::OuterReturn&outer,const C::Result&r){
 if(busy_||!c||state_!=State::Cancelling)return state_;
 Operation operation(busy_,active_cpu_,c);try{
  M::Scope scope;const auto poses=session_.baseline_poses();
  if(c!=started_cpu_||!session_.fence_held()||!cancel_closed_||!cancel_detached_||!r.released()||!r.same_cancellation(*cancel_detached_)||!poses||!r.matches_baseline(*poses)||!r.current_binding(c,depth_,outer)||!outer.claimed_current(c,expected_,depth_)||!query(scope)||!scope.fenced||scope.serial!=r.serial()||scope.logic_step!=r.logic_step())throw std::runtime_error("cancel released source");
  if(install_->retire_cancel(c,depth_,outer,r)!=I::Status::Released||!install_->can_free(r)||!session_.transition_cancel_native(J::Phase::Unpublished,r)||!memory_->transition(M::Phase::Closed)||!r.current_binding(c,depth_,outer)||!outer.claimed_current(c,expected_,depth_))throw std::runtime_error("cancel final retirement");
  // A distinct opaque origin is minted only while the genuine partial release
  // and claimed OuterReturn are live. Never synthesize natural terminal flags.
  auto origin=std::make_shared<CancelOrigin>(CancelOrigin{r,scope,c});
  release_scope_.reset();release_clock_.reset();release_cpu_=nullptr;
  if(sources_.release){if(!sources_.release(expected_))throw std::runtime_error("cancel claim release");retained_=false;
   if(!r.current_binding(c,depth_,outer)||!outer.claimed_current(c,expected_,depth_)||!sources_.retained||sources_.retained(expected_))throw std::runtime_error("cancel clean origin release");
   cancel_origin_=std::move(origin);
   cancel_retired_query_=[](const Controller&owner,const Cpu*cpu){return owner.cancel_retired_current(cpu);};
   cancel_rearm_factory_=[](Controller&owner){return owner.take_cancel_rearm_certificate();};
  }
  return state_=State::Released;
 }catch(const std::exception&e){LOG("[native_wait] cancellation finish failure=%s",e.what());quarantine();return state_;}catch(...){quarantine();return state_;}
}
}
