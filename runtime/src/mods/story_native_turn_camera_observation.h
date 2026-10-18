// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_turn_camera_image.h"
#include "story_native_event_lease.h"
#include <array>
#include <functional>
#include <span>
namespace story_native_turn_camera_observation {
enum class Phase {Pristine,Published,Running,
 // Copied exhausted staff after native endProc; producer still authenticates
 // its opaque Terminal/CameraReturn receipts independently.
 Terminal,ExhaustedReleased};
enum class Reason {None,Identity,Image,State,Staff,Barrier,Watchdog,Busy,Ownership};
struct Capture {
 story_native_event_lease::Identity identity;
 std::array<uint8_t,story_native_turn_camera_image::kImageBytes> image{};
 // Explicitly extracted own flags0..8, not a replacement native flag word.
 uint32_t completion_flags=0,elapsed_ticks=0;
 bool exclusive=false,foreign=false;
 uint32_t manager_depth=0,camera_depth=0,npc_depth=0;
};
using Reader=std::function<bool(uint32_t,std::span<uint8_t>)>;
bool collect(const story_native_event_lease::Identity&,const Reader&,Capture&);
struct Result {bool valid=false;Reason reason=Reason::Image;std::array<uint32_t,4> cuts{},actions{};};
// Exact copied E60 structural policy only. Caller must independently capture
// flags under the same quiescent ownership and prove Uo2 subtype1 consumers,
// native manager references and typed camera restore. No release authority.
// Watchdog is external lease elapsed30Hz time, NOT native rewinding Timer;
// hitting its deadline fails closed, never fabricates TURN completion.
Result validate(Phase,const story_native_event_lease::Identity&,
 const story_native_turn_camera_image::Program&,const Capture&,uint32_t watchdog_ticks);
}
