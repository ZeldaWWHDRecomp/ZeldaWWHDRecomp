// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_end_guard.h"
#include "story_native_event_observation.h"
#include "story_native_people_pose_capture.h"
#include <optional>
struct Cpu;
namespace story_native_wait_runtime_route_capture {
using Expected=story_native_event_observation::Expected;
using Camera=story_native_event_observation::Camera;
struct Ship {uint32_t address{},pid{};};
struct Sources {
 story_native_people_pose_capture::Read read;
 std::function<bool(const Expected&)> retained;
 story_native_people_pose_capture::VerifyActor resident;
 // Actual typed raw camera capture, including native instance/PID metadata.
 std::function<bool(Camera&)> camera;
 // Live controller receipt for this successful native order(index0), after
 // Install.start/Published commit and before native manager start. Guest ORDER
 // bytes or a restored journal alone must never implement this predicate.
 std::function<bool(const Expected&)> pending_order;
 // Immutable actual Play::mShipActor/PID snapshot captured before publication.
 // The native Ship destructor does not clear its global pointer; a fresh PID
 // read alone cannot certify that an old global still denotes this instance.
 std::optional<Ship> ship;
};
// Read-only live observations. No hooks, writes, native calls, journal/claim
// issuance or publication authority. Caller supplies real retained ownership.
class Adapter {
 Expected expected_;Sources sources_;
 bool read(uint32_t,std::span<uint8_t>)const;
 bool retained()const;
 bool context()const;
 bool image()const;
 bool name_miss(uint32_t)const;
 bool native_name(uint32_t)const;
 bool people(uint32_t)const;
 bool ship(uint32_t)const;
 bool active_layer(uint32_t)const;
public:
 Adapter(Expected,Sources);
 // Supports unrelated native People and exact no-match staff lookups while
 // the private image is published. Unsupported/ambiguous retail callbacks fail
 // closed; exact owned-address reuse never becomes ordinary by PID mismatch.
 bool retail_safe(const Cpu*,uint32_t function)const noexcept;
 bool end_observation(Cpu*,story_native_event_end_guard::Observation&)const noexcept;
};
}
