// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_depth.h"
#include <functional>
namespace story_native_event_dispatch {
using Body=std::function<uint32_t()>;
struct Outcome {bool executed=false,observed=false,delegated=false;uint32_t value=0;};
// Isolated exact-chain simulation/helper, NOT installed in guestmods. Caller
// supplies freshly verified Call/native identity. Body owns all Cpu semantics.
// dispatcher wraps entry/replacement/native/return together; original suppresses
// only one exact same-invocation delegated call, otherwise observes a new outer.
// Foreign refusal still executes retail body while Tracker retains sticky fault;
// no denial result is permission to skip an ordinary retail original.
Outcome dispatcher(story_native_event_depth::Tracker&,const story_native_event_depth::Call&,const Body&);
Outcome original(story_native_event_depth::Tracker&,const story_native_event_depth::Call&,const Body&);
}
