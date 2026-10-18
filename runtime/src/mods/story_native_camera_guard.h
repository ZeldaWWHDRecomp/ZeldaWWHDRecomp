// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_lease.h"
#include <array>
#include <cstdint>
namespace story_native_camera_guard {
struct Tuple {
 int32_t current_type=0,next_type=0,event_type=0,saved_return=-1;
 int32_t style=0,current_mode=0,next_mode=0;
 uint32_t mode_gate=0,run_gate=0,type_counter=0;
 bool operator==(const Tuple&)const=default;
};
struct Snapshot {
 story_native_event_lease::Identity identity;
 uint32_t camera=0,pid=0,flags=0;
 // Native dCamParam::Change: table GD1004487C, count GD10044878.
 uint32_t style_table=0,style_count=0,param_pointer=0;
 int32_t param_index=-1;
 Tuple tuple;
};
// Mandatory side-effect-free native table/profile verifier. The pure guard
// does not invent native type/style/mode counts or read any guest pointer.
using VerifyTuple=bool(*)(const Tuple&);
struct Guards {
 bool exclusive=false,foreign=false,manager_owned=false,manager_detached=false;
 uint32_t manager_depth=0,camera_depth=0,npc_depth=0;
};
enum class Phase {BeforePublish,Published,Running,PlanDetach,VerifyDetached};
enum class Reason {None,Identity,Ownership,Busy,NativeTuple,Baseline,ReturnHazard,Transition,Manager};
struct Word {uint32_t offset=0,value=0;};
class Result {
 bool valid_=false,restore_=false;Reason reason_=Reason::None;
 std::array<Word,11> writes_{};
 friend Result evaluate(Phase,const Snapshot&,const Snapshot&,const Guards&,VerifyTuple);
public:
 bool valid()const{return valid_;}
 Reason reason()const{return reason_;}
 bool has_restore_plan()const{return valid_&&restore_;}
 const std::array<Word,11>& writes()const{return writes_;}
};
// Read-only transition proof/atomic typed restore plan, NOT permission to free
// an event image. Work/EventData/view/actor caches and manager references still
// require the independent collector/lease check. Backend must apply the whole
// plan at a quiescent owned boundary BEFORE detaching manager/cameraPlay or
// restoring EventData+0C. Never run nextType between individual writes.
Result evaluate(Phase,const Snapshot& baseline,const Snapshot& live,const Guards&,VerifyTuple);
}
