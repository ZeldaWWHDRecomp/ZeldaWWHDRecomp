// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_collect.h"
#include "story_native_event_memory.h"
#include "story_native_event_depth.h"
namespace story_native_event_backend {
using namespace story_native_event_observation;
struct Config {
 story_native_event_memory::Config memory;
 story_native_event_collect::Config collector;
};
// These callbacks must use real loaded-session declaration/allocation records.
// They cannot call native code, mutate ownership, or recurse into preflight.
struct Sources {
 story_native_event_memory::Read read;
 story_native_event_memory::Query session;
 story_native_event_memory::VerifyAllocation allocation;
 story_native_event_collect::VerifyActor actor;
};
enum class Reason {None,Configuration,Disabled,Depth,Session,Collection,Observation,Control,Changed};
struct Report {
 bool preflight=false,publication_enabled=false;
 Reason reason=Reason::Configuration;
 story_native_event_collect::Error collector_error=story_native_event_collect::Error::None;
 Result observation{};
 Camera camera{};
 uint64_t serial=0;
};
// WAIT-only runtime wiring scaffold. All real native pointer/byte observations
// pass through the bounded memory policy, collector, and camera source policy.
// No Write/Invoke/Install backend, hook registration, allocation or publication
// entrypoint exists. Actual runtime Sources and persistent outer-hook Tracker
// still require reviewed production integration.
class Scaffold {
 Expected expected_;Config config_;Sources sources_;
 story_native_event_depth::Tracker&depth_;
 bool busy_=false;
public:
 Scaffold(Expected,Config,Sources,story_native_event_depth::Tracker&);
 static constexpr bool publication_enabled(){return false;}
 Report preflight(Operation); // only Prepare or Running read-only inspection
};
}
