// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_session.h"
#include "story_native_event_backend.h"
#include "story_native_event_install.h"
#include "story_native_event_invoke.h"
#include "story_native_stage_owner_observer.h"
#include "story_native_camera_policy.h"
#include <memory>
namespace story_native_wait_runtime_controller {class Context;}
namespace story_native_restore_claim {class Transfer;}
namespace story_native_restore_resume {class Boundary;}
namespace story_native_wait_controller {
namespace B=story_native_event_backend;
namespace M=story_native_event_memory;
namespace D=story_native_event_depth;
using Expected=story_native_event_observation::Expected;
using Layout=story_native_event_arena::Layout;
using Stage=story_native_stage_owner_observer::Snapshot;
// These production callbacks must capture persistent context by value, never
// preparing_preflight stack locals. Controller adds no substitute authority.
struct Sources {
 B::Sources validation;
 story_native_camera_policy::Tables camera_tables{};
 M::Write write;
 story_native_event_invoke::Call call;
 std::function<bool(Cpu*,story_native_event_invoke::Config&)> stack;
 std::function<bool(const Expected&,const Layout&,const Stage&)> claim;
 std::function<bool(const Expected&)> retained;
 story_native_event_install::Detach detach; // mandatory actual normal writer
 std::function<void()> quarantine; // retain image/deny owned entries, no native
 std::function<bool(const Expected&)> release; // optional; absent retains claim
 std::function<bool(const story_native_event_observation::Result&)> arm_clock; // mandatory TURN, genuine publication boundary
 // Read-only post-cancellation rearm proof. Actual producer holds a short
 // physical Q/Stage window; it must release that window before native Search.
 std::function<bool(Cpu*,const Expected&,const M::Scope&)>cancel_rearm;
};
enum class State {Empty,Started,Running,TerminalReady,Released,Refused,Quarantined,Cancelling,RestoredDormant};
// Dormant orchestration only. No hook registration or publication activation.
// Session and Tracker are explicit stable lifetime dependencies; both must
// outlive this object. All operations require the actual held Session fence.
class Controller {
 story_native_wait_session::Session&session_;D::Tracker&depth_;
 const Expected expected_;const Layout layout_;const Stage stage_;
 const B::Config config_;const Sources sources_;
 std::unique_ptr<M::Policy>memory_;
 std::unique_ptr<story_native_event_collect::Collector>collector_;
 std::shared_ptr<story_native_event_install::Install>install_;
 friend class story_native_wait_runtime_controller::Context;
 State rehydrate(Cpu*,const story_native_restore_resume::Boundary&,story_native_restore_claim::Transfer&&);
 bool prepare_restore_open(Cpu*,const story_native_restore_resume::Boundary&,const threads::NativeQuiescence&);
 void open_restored_unchecked()noexcept;
 void revoke_for_restore()noexcept;
 bool restore_dormant_=false,restore_revoked_=false;
 std::optional<story_native_event_observation::Result>restored_result_;
 Cpu*active_cpu_=nullptr;Cpu*started_cpu_=nullptr;State state_=State::Empty;
 std::atomic<bool> retained_{false};
 std::shared_ptr<story_native_wait_session::Session::RearmCertificate::Seal> rearm_seal_;
 bool rearm_issued_=false;
 Cpu*release_cpu_=nullptr;
 std::optional<M::Scope> release_scope_;
 std::optional<story_native_event_collect::Clock> release_clock_;
 struct CancelOrigin;
 std::shared_ptr<const CancelOrigin>cancel_origin_;
 std::optional<story_native_wait_session::Session::RearmCertificate>(*cancel_rearm_factory_)(Controller&)=nullptr;
 std::optional<story_native_wait_session::Session::RearmCertificate>take_cancel_rearm_certificate();
 bool(*cancel_retired_query_)(const Controller&,const Cpu*)=nullptr;
 bool cancel_retired_current(const Cpu*)const;
 bool clean_cancel_retired(const Cpu*c)const{return cancel_retired_query_&&cancel_retired_query_(*this,c);}
 std::optional<story_native_cancel_observation::Result>cancel_closed_,cancel_detached_;
 story_native_outer_return::OuterReturn::Binding cancel_binding_;
 std::optional<M::Scope>cancel_admission_;
 const story_native_outer_return::OuterReturn*cancel_invoking_outer_=nullptr;
 unsigned cancel_control_calls_=0;
 std::function<uint32_t(Cpu*,uint32_t,std::initializer_list<uint32_t>,std::initializer_list<uint32_t>)>cancel_control_invoke_;
 bool quarantine_notified_=false,busy_=false;
 bool query(M::Scope&);
 bool capture(Cpu*,story_native_event_observation::Captured&);
 bool detach(Cpu*,const Expected&);
 uint32_t invoke_cancel_control(Cpu*,uint32_t,std::initializer_list<uint32_t>,std::initializer_list<uint32_t>);
 uint32_t invoke(Cpu*,uint32_t,std::initializer_list<uint32_t>,std::initializer_list<uint32_t>);
 void quarantine()noexcept;
 bool prerequisites()const;
 bool checkpoint_current(Cpu*);
public:
 Controller(story_native_wait_session::Session&,D::Tracker&,Expected,Layout,Stage,B::Config,Sources);
 ~Controller();
 // Same-boundary only. Proof callbacks are copied by value; Session, Tracker
 // and their production dependencies must outlive this Controller. Destroying
 // this issuer revokes its outstanding certificate before dependency teardown.
 std::optional<story_native_wait_session::Session::RearmCertificate> take_rearm_certificate();
 Controller(const Controller&)=delete;Controller&operator=(const Controller&)=delete;
 static constexpr bool publication_enabled(){return false;}
 bool restored_active(const Expected&e)const noexcept{return e==expected_&&restored_result_.has_value()&&!restore_dormant_&&!restore_revoked_&&(state_==State::Started||state_==State::Running||state_==State::TerminalReady||state_==State::Released);}
 bool retained(const Expected&)const noexcept;
 State state()const{return state_;}
 const Expected&expected()const{return expected_;}
 State start(Cpu*); // entry claim precedes Install.start and every native call
 bool checkpoint(Cpu*); // actual stable idle capture, no native calls
 State observe(Cpu*); // skips Install.observe for terminal state0
 // Same synchronous claimed CameraExecute outer return only. begin persists
 // Detaching before any writer; release runs AFTER bounded physical parking
 // is destroyed; finish requires a newly parked exact Released observation.
 bool prepare_cancel(Cpu*,const story_native_outer_return::OuterReturn&);
 State begin_cancel(Cpu*,const story_native_outer_return::OuterReturn&,const story_native_cancel_observation::Result&);
 State release_cancel_native(Cpu*,const story_native_outer_return::OuterReturn&,const story_native_cancel_observation::Result&);
 State finish_cancel(Cpu*,const story_native_outer_return::OuterReturn&,const story_native_cancel_observation::Result&);
 State stop_terminal(Cpu*,const story_native_manager_terminal::Terminal&,
                     const story_native_camera_return::Barrier&);
};
}
