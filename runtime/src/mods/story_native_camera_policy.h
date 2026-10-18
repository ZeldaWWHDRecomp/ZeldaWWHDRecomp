// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include <functional>
#include <span>
namespace story_native_camera_policy {
using Read=std::function<bool(uint32_t,std::span<uint8_t>)>;
struct Tables {
 // Actual regional EAs, mapped ONCE by caller: type count/table and style
 // count/table (USA source anchors10049348/10049350/10044878/1004487C).
 uint32_t type_count=0,types=0,style_count=0,styles=0;
};
enum class Reason {None,Configuration,Read,Count,Type,Mode,Style,Cache,Transition};
struct Result {bool valid=false;Reason reason=Reason::Configuration;};
// Read-only fixed-capacity native table verifier, suitable for Collector's
// VerifyCamera callback via a capturing lambda. Does not prove camera ownership
// or lease identity against declarations; collector retains those checks.
// Private E60 additionally requires actual selected retail algorithm Follow1.
Result verify(const Tables&,const Read&,const story_native_event_observation::Camera&,
 const story_native_event_lease::Identity&);
}
