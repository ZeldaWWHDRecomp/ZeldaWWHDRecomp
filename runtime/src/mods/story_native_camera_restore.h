// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include <functional>
namespace story_native_camera_restore {
using story_native_event_observation::Camera;
using story_native_event_observation::Expected;
using story_native_event_lease::Identity;
struct Boundary {
 Identity identity{};
 bool exclusive=false,fenced=false,foreign=false,fault=false,manager_owned=false;
 uint32_t manager_depth=0,camera_depth=0,npc_depth=0,callback_depth=0,foreign_depth=0;
 // Verified pre-control mode3, with no talk/demo partner ownership. A narrow
 // end_proc hook cannot protect mode1/2 partner writes occurring before it.
 uint32_t control_mode=0,partner1=0xFFFFFFFFu,partner2=0xFFFFFFFFu,talk_partner=0xFFFFFFFFu,item_partner=0xFFFFFFFFu;
 int32_t event_id=-1;
 uint64_t logic_step=0,serial=0;
};
using VerifyCamera=std::function<bool(const Camera&,const Identity&)>;
struct Segment {
 uint32_t offset=0,bytes=0;
 std::array<uint8_t,0x114> data{};
};
enum class Reason {None,Identity,Boundary,Control,Policy,Snapshot,Transition,Poststate};
class Plan {
 bool valid_=false;Reason reason_=Reason::Identity;
 Expected expected_{};Boundary boundary_{};
 std::array<Segment,12> segments_{};
 friend Plan prepare(const Expected&,const Camera&,const Boundary&,VerifyCamera);
public:
 bool valid()const{return valid_;}Reason reason()const{return reason_;}
 uint32_t camera()const{return expected_.camera.address;}
 std::span<const Segment> segments()const{return valid_?std::span<const Segment>(segments_):std::span<const Segment>();}
 // Fresh capture after an external all-or-quarantine transaction, before any
 // native callback resumes. This is not manager detachment/free permission.
 bool verify_post(const Camera&,const Boundary&,VerifyCamera)const;
};
// Pure plan only: no reader, writer, invoker, hook, or publication capability.
// Caller must hold the real shared entry fence across ALL segments and postcheck.
// Interrupted/partial application is quarantined; never resume native nextType.
Plan prepare(const Expected&,const Camera& live,const Boundary&,VerifyCamera);
}
