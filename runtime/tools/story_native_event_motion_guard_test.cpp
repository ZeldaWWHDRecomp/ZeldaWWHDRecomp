// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_motion_guard.h"
#include <cassert>
#include <limits>
#include <initializer_list>

using namespace story_native_event_motion_guard;
Observation actor(){Observation o;o.identity={0x47C11000,0x16C,3,0x534541,11,1,5,0x10500000,0x60001000,0x12345678,2};const auto a=o.identity.actor;o.binding={a+0x450,a+0x614,a+0x314,a+0x300,a+0x33C,a+0x320,a+0x328,a,o.identity.pid,1,0x20,30,30};o.native={0x175,0x00FF0000,1,1,0,4};o.position={100,1040,-201500};o.ownership_verified=true;return o;}
Support ground(){Support s;s.index=s.registration_id=2;s.poly_bgw=s.entry_bgw=0x48000000;s.entry_used=1;s.poly_pid=s.entry_pid=0xFFFFFFFF;s.room=11;s.height=1040;return s;}
Limits limits(){return {0,300,-201700,-201300,5,20};}
Preconditions ready(){return {true,true,true};}
void begin(Guard&g){assert(g.begin(actor(),ground(),limits(),ready()));}
int main(){
 // Full successful native-process transaction, duplicate cutEnd requests coalesce.
 Guard g;auto a=actor();auto s=ground();begin(g);assert(!g.begin(a,s,limits(),ready()));assert(g.ride_callback(a.identity)==Decision::Suppress);
 assert(g.cut_end(a.identity,a.identity.manager,a.identity.staff)==Decision::Defer);assert(g.cut_end(a.identity,a.identity.manager,a.identity.staff)==Decision::Defer);
 a.position.x+=2;assert(g.candidate(a,s));assert(g.accept(a,s));auto result=g.commit(a);assert(result.accepted&&result.cut_end&&result.manager==a.identity.manager&&result.staff==2);assert(!g.commit(a).accepted);
 // Passing native process without cutEnd does not manufacture completion.
 g.reset();begin(g);a=actor();a.position.z+=2;assert(g.candidate(a,s));assert(g.accept(a,s));result=g.commit(a);assert(result.accepted&&!result.cut_end);
 // Fresh identity and full scope, including stage/room/PID/token/epoch/program.
 for(unsigned field=0;field<11;++field){g.reset();begin(g);a=actor();switch(field){case 0:++a.identity.actor;break;case 1:++a.identity.pid;break;case 2:++a.identity.token;break;case 3:++a.identity.stage;break;case 4:++a.identity.room;break;case 5:++a.identity.epoch;break;case 6:++a.identity.lease;break;case 7:++a.identity.manager;break;case 8:++a.identity.image;break;case 9:++a.identity.program;break;case 10:++a.identity.staff;break;}assert(!g.candidate(a,s));assert(g.phase()==Phase::Aborted);}
 // Callback guards never enlist a foreign actor; address reuse with another PID delegates.
 g.reset();begin(g);auto foreign=actor().identity;++foreign.pid;assert(g.ride_callback(foreign)==Decision::Delegate);assert(g.cut_end(foreign,foreign.manager,foreign.staff)==Decision::Delegate);
 foreign=actor().identity;++foreign.token;assert(g.ride_callback(foreign)==Decision::Reject);assert(g.phase()==Phase::Aborted);assert(g.cut_end(foreign,foreign.manager,foreign.staff)==Decision::Reject);assert(g.phase()==Phase::Aborted);
 // Failed process remains denial-only until the native scope has returned/reset.
 assert(g.ride_callback(actor().identity)==Decision::Reject);assert(g.cut_end(actor().identity,actor().identity.manager,2)==Decision::Reject);assert(!g.commit(actor()).accepted);
 // Incorrect bound staff/manager cannot alter another parallel track.
 for(bool manager:{false,true}){g.reset();begin(g);const auto i=actor().identity;assert(g.cut_end(i,i.manager+(manager?4:0),i.staff+(manager?0:1))==Decision::Reject);assert(g.ride_callback(i)==Decision::Reject);}
 // Admission requires all callback/isolation preconditions before native execution.
 for(unsigned f=0;f<3;++f){g.reset();auto p=ready();if(f==0)p.owned_scope=false;if(f==1)p.exclusive_lease=false;if(f==2)p.ride_guard_installed=false;assert(!g.begin(actor(),s,limits(),p));assert(g.phase()==Phase::Empty);}
 // Known moving/callback-owned backgrounds must be rejected before invoking CrrPos.
 for(unsigned f=0;f<9;++f){g.reset();auto bad=s;switch(f){case 0:bad.move_flags=1;break;case 1:bad.ride_callback=0x12345678;break;case 2:bad.index=256;break;case 3:++bad.registration_id;break;case 4:bad.entry_used=0;break;case 5:bad.entry_bgw+=4;break;case 6:bad.entry_pid=1;break;case 7:bad.room=12;break;case 8:bad.height=std::numeric_limits<float>::quiet_NaN();break;}assert(!g.begin(actor(),bad,limits(),ready()));}
 // Only ordinary Uo2/Um1 commonMOVE, never seated/foreign/raw variants.
 for(unsigned f=0;f<6;++f){g.reset();a=actor();switch(f){case 0:a.native.profile=0x13B;break;case 1:a.native.parameters=0;break;case 2:a.native.stage_argument=a.native.class_subtype=2;break;case 3:a.native.class_subtype=9;break;case 4:a.native.ground_movement=1;break;case 5:a.native.action=3;break;}assert(!g.begin(a,s,limits(),ready()));}
 g.reset();a=actor();a.native.stage_argument=a.native.class_subtype=9;assert(g.begin(a,s,limits(),ready()));
 // Existing native one-circle pointer binding is mandatory, not just an actor label.
 for(unsigned f=0;f<6;++f){g.reset();a=actor();switch(f){case 0:a.binding.old_position+=4;break;case 1:a.binding.count=3;break;case 2:a.binding.pid++;break;case 3:a.binding.radius=29;break;case 4:a.binding.flags|=4;break;case 5:a.ownership_verified=false;break;}assert(!g.begin(a,s,limits(),ready()));}
 // Full footprint must stay inside the corridor, including after native correction.
 g.reset();auto l=limits();l.min_x=71;assert(!g.begin(actor(),s,l,ready()));
 g.reset();begin(g);a=actor();a.position.x+=6;assert(!g.candidate(a,s));
 g.reset();begin(g);a=actor();a.position.x+=2;assert(g.candidate(a,s));a.position.z+=6;assert(!g.accept(a,s));
 // Actual ground evidence required: flat, uphill and downhill are accepted only grounded.
 for(float rise:{0.f,4.f,-4.f}){g.reset();begin(g);a=actor();a.position.x+=1;assert(g.candidate(a,s));auto next=s;next.height+=rise;a.position.y=next.height;assert(g.accept(a,next));assert(g.commit(a).accepted);}
 g.reset();begin(g);a=actor();assert(g.candidate(a,s));a.binding.flags&=~0x20;assert(!g.accept(a,s));
 g.reset();begin(g);a=actor();assert(g.candidate(a,s));a.position.y+=0.2f;assert(!g.accept(a,s));
 g.reset();begin(g);a=actor();assert(g.candidate(a,s));a.position.y+=21;auto too_high=s;too_high.height+=21;assert(!g.accept(a,too_high));
 // Correction to a callback background or fresh identity loses completion authority.
 g.reset();begin(g);a=actor();assert(g.cut_end(a.identity,a.identity.manager,2)==Decision::Defer);assert(g.candidate(a,s));auto dynamic=s;dynamic.ride_callback=1;assert(!g.accept(a,dynamic));assert(!g.commit(a).cut_end);
 g.reset();begin(g);a=actor();assert(g.candidate(a,s));assert(g.accept(a,s));++a.identity.pid;assert(!g.commit(a).accepted);
 // Fresh position/program must still match the validated result at commit.
 g.reset();begin(g);a=actor();assert(g.candidate(a,s));assert(g.accept(a,s));a.position.x+=1;assert(!g.commit(a).accepted);
 // Admission refuses malformed identities and unbounded configuration.
 for(unsigned f=0;f<5;++f){g.reset();a=actor();switch(f){case 0:a.identity.pid=0;break;case 1:a.identity.token=0;break;case 2:a.identity.room=64;break;case 3:a.identity.program=0;break;case 4:a.identity.staff=20;break;}assert(!g.begin(a,s,limits(),ready()));}
 for(unsigned f=0;f<4;++f){g.reset();auto bad=limits();switch(f){case 0:bad.max_step=0;break;case 1:bad.max_step=51;break;case 2:bad.max_ground_delta=61;break;case 3:bad.max_x=std::numeric_limits<float>::infinity();break;}assert(!g.begin(actor(),s,bad,ready()));}
 // NaN/bounds and future stage reset cannot reuse an in-flight guard accidentally.
 g.reset();begin(g);a=actor();a.position.x=std::numeric_limits<float>::quiet_NaN();assert(!g.candidate(a,s));g.reset();a=actor();++a.identity.epoch;assert(g.begin(a,s,limits(),ready()));
}
