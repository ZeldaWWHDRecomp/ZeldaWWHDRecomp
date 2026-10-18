// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_camera_guard.h"
#include <cassert>
#include <stdexcept>
namespace C=story_native_camera_guard;
bool native_tuple(const C::Tuple&t){return t.current_type>=0&&t.current_type<8&&t.next_type>=0&&t.next_type<8&&t.event_type==7&&t.saved_return>=-1&&t.saved_return<8&&t.style>=0&&t.style<100&&t.current_mode>=0&&t.current_mode<4&&t.next_mode>=0&&t.next_mode<4;}
bool throwing(const C::Tuple&){throw std::runtime_error("invalid pureverifier");}
int main(){
 C::Snapshot original;original.identity={0x10010000,0x60010000,0xC20,10,11,12,13};original.camera=0x20000248;original.pid=42;original.tuple={2,2,7,-1,10,1,1,0,0,9};
 original.style_table=0x1004487C;original.style_count=100;original.param_index=10;original.param_pointer=original.style_table+10*0x84;
 C::Guards idle;idle.exclusive=true;idle.manager_detached=true;assert(C::evaluate(C::Phase::BeforePublish,original,original,idle,native_tuple).valid());
 C::Guards owned;owned.exclusive=true;owned.manager_owned=true;auto entered=original;entered.tuple.current_type=7;entered.tuple.next_type=7;entered.tuple.saved_return=2;entered.tuple.style=20;entered.tuple.current_mode=0;entered.tuple.next_mode=0;entered.tuple.mode_gate=1;entered.tuple.type_counter=0;entered.param_index=20;entered.param_pointer=entered.style_table+20*0x84;
 assert(C::evaluate(C::Phase::Published,original,original,owned,native_tuple).valid());assert(C::evaluate(C::Phase::Running,original,entered,owned,native_tuple).valid());
 auto not_run=entered;not_run.tuple.next_type=2;assert(C::evaluate(C::Phase::Running,original,not_run,owned,native_tuple).valid()); // exactNotRun native branch
 auto plan=C::evaluate(C::Phase::PlanDetach,original,entered,owned,native_tuple);assert(plan.valid()&&plan.has_restore_plan());const auto&w=plan.writes();assert(w.size()==11&&w[0].offset==0x51C&&w[0].value==2&&w[1].offset==0x520&&w[1].value==2&&w[2].offset==0x408&&w[2].value==0xFFFFFFFFu);assert(w[4].offset==0x13C&&w[4].value==1&&w[7].offset==0x518&&w[7].value==0);
 assert(w[9].offset==0x8A8&&w[9].value==original.param_pointer&&w[10].offset==0x8AC&&w[10].value==10);
 for(unsigned k=0;k<6;++k){auto corrupt=entered;switch(k){case 0:corrupt.param_index=10;break;case 1:corrupt.param_pointer+=4;break;case 2:corrupt.style_table+=4;break;case 3:corrupt.style_count=20;break;case 4:corrupt.tuple.style=-1;break;case 5:corrupt.style_count=4097;break;}assert(!C::evaluate(C::Phase::PlanDetach,original,corrupt,owned,native_tuple).valid());}
 // Concrete lifetime hazard: staffclear/EventDatarestore cannot detach modes.
 auto early=entered;early.tuple.saved_return=-1;auto bad=C::evaluate(C::Phase::PlanDetach,original,early,owned,native_tuple);assert(!bad.valid()&&bad.reason()==C::Reason::ReturnHazard&&!bad.has_restore_plan());assert(!C::evaluate(C::Phase::VerifyDetached,original,entered,idle,native_tuple).valid());
 auto half=entered;half.tuple.current_type=2;half.tuple.saved_return=-1;assert(!C::evaluate(C::Phase::PlanDetach,original,half,owned,native_tuple).valid());
 auto wrong_return=entered;wrong_return.tuple.saved_return=3;assert(!C::evaluate(C::Phase::Running,original,wrong_return,owned,native_tuple).valid());
 auto wrong_next=entered;wrong_next.tuple.next_type=4;assert(!C::evaluate(C::Phase::Running,original,wrong_next,owned,native_tuple).valid());
 auto restored=original;assert(C::evaluate(C::Phase::VerifyDetached,original,restored,idle,native_tuple).valid());restored.tuple.style=20;assert(!C::evaluate(C::Phase::VerifyDetached,original,restored,idle,native_tuple).valid());
 for(unsigned k=0;k<8;++k){auto other=entered;switch(k){case 0:other.pid++;break;case 1:other.camera+=4;break;case 2:other.identity.generation++;break;case 3:other.identity.stage++;break;case 4:other.identity.room++;break;case 5:other.identity.program++;break;case 6:other.identity.image+=16;break;case 7:other.flags|=0x20000000;break;}assert(!C::evaluate(C::Phase::PlanDetach,original,other,owned,native_tuple).valid());}
 for(unsigned k=0;k<5;++k){auto guards=owned;switch(k){case 0:guards.foreign=true;break;case 1:guards.exclusive=false;break;case 2:guards.camera_depth=1;break;case 3:guards.manager_depth=1;break;case 4:guards.npc_depth=1;break;}assert(!C::evaluate(C::Phase::PlanDetach,original,entered,guards,native_tuple).valid());}
 auto baseline=original;baseline.tuple.current_type=7;baseline.tuple.next_type=7;assert(!C::evaluate(C::Phase::BeforePublish,baseline,baseline,idle,native_tuple).valid());baseline=original;baseline.tuple.saved_return=2;assert(!C::evaluate(C::Phase::BeforePublish,baseline,baseline,idle,native_tuple).valid());
 assert(!C::evaluate(C::Phase::Running,original,entered,owned,nullptr).valid());assert(!C::evaluate(C::Phase::Running,original,entered,owned,throwing).valid());auto forged=entered;forged.tuple.current_type=999;assert(!C::evaluate(C::Phase::Running,original,forged,owned,native_tuple).valid());
 // A type restore plan intentionally does not expose can_free/image-release.
 assert(!C::evaluate(C::Phase::VerifyDetached,original,original,owned,native_tuple).valid());
}
