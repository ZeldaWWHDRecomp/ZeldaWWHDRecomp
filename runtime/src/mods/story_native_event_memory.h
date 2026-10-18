// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include <functional>
namespace story_native_wait_controller {class Controller;}
namespace story_native_restore_resume {class Boundary;}
struct Cpu;
namespace story_native_event_memory {
using namespace story_native_event_observation;
struct Range {uint32_t address=0,bytes=0;};
struct Config {uint32_t stamp=0,link=0,link_pid=0;Range record;};
struct Scope {
 story_native_event_lease::Identity identity{};
 uint32_t link=0,link_pid=0;
 bool exclusive=false,bindings_verified=false,fenced=false,foreign=false,fault=false;
 uint32_t manager_depth=0,camera_depth=0,npc_depth=0,callback_depth=0,foreign_depth=0;
 uint64_t logic_step=0,serial=0; // tracker serial changes on every native entry
};
using Query=std::function<bool(Scope&)>;
using VerifyAllocation=std::function<bool(const story_native_event_lease::Identity&,const Config&)>;
using Read=std::function<bool(uint32_t,std::span<uint8_t>)>;
using Write=std::function<bool(uint32_t,std::span<const uint8_t>)>;
enum class Phase {Idle,Preparing,Publishing,Running,Rollback,Draining,Restoring,Closed,Quarantined,Restored};
enum class Error {None,Configuration,Ownership,Phase,Span,Payload,Read,Write};
// A private host Backend capability, never a guest/public memory service. Query
// and allocation verification are pure and must not recurse into this Policy.
// This object neither creates an entry fence nor installs any native hooks.
class Policy {
 friend class story_native_wait_controller::Controller;
 bool prepare_restore_open(const story_native_event_observation::Result&,const story_native_restore_resume::Boundary&,Cpu*);
 void open_restored_unchecked()noexcept;
 bool adopt_restored(const story_native_event_observation::Result&,const story_native_restore_resume::Boundary&,Cpu*);
 Expected expected_;Config config_;Query query_;VerifyAllocation allocation_;Read read_;Write write_;
 std::array<Range,32> reads_{};unsigned count_=0;bool configured_=false;
 Phase phase_=Phase::Idle;Error error_=Error::None;
 uint64_t logic_step_=0,serial_=0;
 std::array<uint8_t,0xF4> control_before_{};
 bool scope(Scope&,bool writing);
 bool fail(Error);
public:
 Policy(Expected,Config,Query,VerifyAllocation,Read,Write);
 Phase phase()const{return phase_;}Error error()const{return error_;}
 bool transition(Phase); // strict state graph; fenced writes require zero depth
 bool read(uint32_t,std::span<uint8_t>);
 bool write(uint32_t,std::span<const uint8_t>);
};
}
