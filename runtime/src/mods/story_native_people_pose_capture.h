// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include "story_native_people_pose_plan.h"
#include <functional>

namespace story_native_people_pose_capture {
using Read = std::function<bool(uint32_t,std::span<uint8_t>)>;
using VerifyActor = std::function<bool(const story_native_event_observation::Actor&,
                                      const story_native_event_lease::Identity&)>;
struct Layout { uint32_t profile{},vtable{}; };
enum class Mode { Baseline, Owned, Exhausted };
// Read-only baseline capture at an independently authenticated idle boundary.
// VerifyActor must call the real pure resident accessor and compare its entire
// immutable declaration against Expected. Read must independently bound memory.
// This is an identity observation, not an allocation lifetime/lease receipt;
// the caller must hold its actual entry/resource fences across capture/use.
// Baseline requires the original cut bytes. Owned requires only this fixture's
// exact bound cut consumer, including its staff/action and borrowed pointers.
// Typed TURN additionally samples the canonical image/current staff twice; only
// its own TURN3 or completed WAIT0 action is recognized, never a foreign staff.
// Exhausted is a copied post-EndProc TURN observation: exact final staff/cache,
// full completion flags and still-bound actor cuts at NONE. The caller must
// separately retain its real terminal transaction/fence; this is no receipt.
// Every mode also verifies actual Uo2 native parameter-table identity and its
// zero horizontal attention offsets/fixed attention and eye heights twice.
// Stationary position/scale invariance still needs the caller's actual graph.
// No guest writes, native invocation, hook installation or release authority.
bool capture(const Read&,const VerifyActor&,const Layout&,
             const story_native_event_observation::Expected&,unsigned actor_index,
             story_native_people_pose_plan::Snapshot& out,Mode mode=Mode::Baseline);
}
