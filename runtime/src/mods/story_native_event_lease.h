// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <memory>
namespace story_native_event_observation {class Result;}
namespace story_native_cancel_observation {class Result;}
namespace story_native_event_lease {
// Pure guard only: no guest reads, native calls, publication or pointer freeing.
// The runtime adapter must independently verify every supplied observation.
struct Identity { uint32_t manager=0,image=0,bytes=0,stage=0,room=0,generation=0;uint64_t program=0;friend bool operator==(const Identity&,const Identity&)=default; };
struct Observation {Identity identity;bool foreign=false,consumers_detached=false;};
enum class Phase:uint32_t {Empty,Prepared,Published,Running,Draining,Retired,Quarantined};
class Lease {
 Identity identity_{};Phase phase_=Phase::Empty;
 std::shared_ptr<const story_native_event_observation::Result> prepared_;
public:
 Phase phase()const{return phase_;}
 const Identity& identity()const{return identity_;}
 bool prepare(Identity); // legacy WAIT identity guard only
 bool prepare(const story_native_event_observation::Result&); // full validated Prepare, typed program binding
 bool prepare_restore(const story_native_event_observation::Result&); // full validated Restore only
 bool publish(const Observation&);
 bool started(const Observation&);
 bool finish(const Observation&);
 bool retire(const Observation&);
 // Restore only guards identity/phase; adapter validates mutable image separately.
 bool restore(Identity captured,Phase,const Observation&);
 bool can_restore_retail(const story_native_event_observation::Result&)const;
 // Fresh pure validation must prove native consumers AND manager unpublished.
 bool can_free(const story_native_event_observation::Result&)const;
 bool can_free(const story_native_cancel_observation::Result&)const;
 void invalidate();
};
}
