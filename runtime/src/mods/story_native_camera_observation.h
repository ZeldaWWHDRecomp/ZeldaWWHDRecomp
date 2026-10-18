// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_camera_image.h"
#include "story_native_event_lease.h"
#include <array>
#include <functional>
#include <span>
namespace story_native_camera_observation {
enum class Phase {Pristine,Published,Running};
enum class Reason {None,Identity,Read,Image,State,Staff,Busy,Ownership};
struct Capture {
 story_native_event_lease::Identity identity;
 std::array<uint8_t,story_native_camera_image::kImageBytes> image{};
 bool exclusive=false,foreign=false;
 uint32_t manager_depth=0,camera_depth=0,npc_depth=0;
};
// Reader must copy exactly the requested bounded span or return false. Capture
// performs one fixed-size read, never follows guest-provided section pointers.
using Reader=std::function<bool(uint32_t,std::span<uint8_t>)>;
bool collect(const story_native_event_lease::Identity&,const Reader&,Capture&);
struct Result {bool valid=false;Reason reason=Reason::Image;uint32_t event_state=0,camera_cut=0,camera_action=0;};
// Structural C20 live-image proof ONLY: no manager publication, camera cache
// restoration, semantic completion or image-release authority. Backend must
// separately validate manager/pool pointers, actors and typed camera capture.
Result validate(Phase,const story_native_event_lease::Identity& expected,
 const story_native_camera_image::Program&,const Capture&);
}
