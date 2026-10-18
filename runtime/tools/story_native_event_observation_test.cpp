// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_observation.h"
#include "mods/story_native_event_image.h"
#include <cassert>
#include <cstdint>
using namespace story_native_event_observation;
static void put(auto&b,size_t p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
int main(){
 Expected e;e.identity={0x20000000,0x60000000,0x520,1,2,3,4};e.stage_name={'s','e','a'};
 e.actors[0].address=0x20001000;e.actors[0].pid=10;e.actors[0].token=1;e.actors[0].profile=0x175;e.actors[0].subtype=1;
 e.actors[1]=e.actors[0];e.actors[1].address+=0x1000;e.actors[1].pid++;e.actors[1].token++;
 e.camera.address=0x20003000;e.camera.pid=12;e.camera.event_type=2;e.camera.style_table=e.camera.param_pointer=0x1004487C;e.camera.style_count=4;e.camera.type_table=0x10049350;e.camera.type_count=3;put(e.camera.event_data,0x0C,0xFFFFFFFFu);put(e.camera.event_data,4,0xFFFFFFFFu);
 auto image=story_native_event_image::make_wait_fixture();Captured c;c.identity=e.identity;c.stage_name=e.stage_name;c.image=image;c.manager={0x60000000,0x60000040,0x600000F0,0x60000230,0x60000410,0,0x60000510,0};c.actors=e.actors;c.camera=e.camera;c.resource_generation_matches=true;c.control_owned=true;
 assert(validate(Operation::Publish,e,c).valid());
 auto explicit_wait=e;explicit_wait.program=story_native_event_program::Program::wait(e.frames);
 auto explicit_result=validate(Operation::Publish,explicit_wait,c);assert(explicit_result.valid()&&explicit_result.matches(explicit_wait)&&!explicit_result.matches(e));
 explicit_wait.program=story_native_event_program::Program::wait(e.frames+1);assert(validate(Operation::Publish,explicit_wait,c).reason()==Reason::Definition);
 auto prepared=c;prepared.manager=e.retail_manager;prepared.control_owned=false;prepared.control_released=true;assert(validate(Operation::Prepare,e,prepared).valid());assert(!validate(Operation::Prepare,e,prepared).released());
 // Publication after exact native cutInfo names cannot authorize alias names/cache.
 auto ordered=c;auto ordered_image=image;put(ordered_image,0xE4,1);ordered.image=ordered_image;
 for(unsigned n=0;n<2;++n){put(ordered.actors[n].cut,0,e.identity.image+0xF0+n*0x50);put(ordered.actors[n].cut,8,e.actors[n].address);put(ordered.actors[n].cut,0x10,0xFFFFFFFFu);}
 assert(validate(Operation::Publish,e,ordered).valid());auto alias=ordered;put(alias.actors[0].cut,0,e.identity.image+0x140);assert(!validate(Operation::Publish,e,alias).valid());alias=ordered;put(alias.actors[0].cut,0x10,0);assert(!validate(Operation::Publish,e,alias).valid());
 auto bad=c;bad.manager[5]=0x60000000;assert(validate(Operation::Publish,e,bad).reason()==Reason::Manager);
 for(unsigned n=0;n<8;++n){bad=c;bad.manager[n]+=4;assert(!validate(Operation::Publish,e,bad).valid());}
 bad=c;bad.foreign=true;assert(validate(Operation::Publish,e,bad).reason()==Reason::Foreign);
 bad=c;bad.stage_name[0]='x';assert(!validate(Operation::Publish,e,bad).valid());
 bad=c;bad.identity.stage++;assert(!validate(Operation::Publish,e,bad).valid());bad=c;bad.identity.room++;assert(!validate(Operation::Publish,e,bad).valid());bad=c;bad.room_number=1;assert(!validate(Operation::Publish,e,bad).valid());
 for(unsigned n=0;n<3;++n){bad=c;if(n==0)bad.actors[0].pid++;if(n==1)bad.actors[0].token++;if(n==2)bad.actors[0].profile++;assert(!validate(Operation::Publish,e,bad).valid());}
 // Legitimate native mutable staff cursors/timers are accepted without allowing
 // immutable names/chains/pools or cross-staff cursor/action changes.
 put(image,0xE4,2);c.camera_play=1;c.current_event=0x60000040;
 for(unsigned n=0;n<4;++n){size_t p=0xF0+n*0x50;put(image,p+0x38,n);image[p+0x46]=2;}
 for(unsigned n=0;n<2;++n){put(c.actors[n].cut,0,0x600000F0+n*0x50);put(c.actors[n].cut,4,n);put(c.actors[n].cut,8,e.actors[n].address);put(c.actors[n].cut,0x10,0);}
 put(c.camera.event_data,4,2);put(c.camera.event_data,0x0C,e.camera.current_type);c.camera.current_type=c.camera.next_type=c.camera.event_type;c.camera.timer=20;put(c.camera.work37C,8,20);
 assert(validate(Operation::Running,e,c).valid());assert(validate(Operation::Restore,e,c).valid());
 put(image,0xF0+0x3C,0);put(image,0x190+0x3C,1);image[0x1E0+0x43]=20;assert(validate(Operation::Running,e,c).valid());
 put(image,0xF0+0x38,1);assert(validate(Operation::Running,e,c).reason()==Reason::Staff);put(image,0xF0+0x38,0);
 put(image,0xF0+0x3C,2);assert(!validate(Operation::Running,e,c).valid());put(image,0xF0+0x3C,0);
 image[0xF0]^=1;assert(validate(Operation::Running,e,c).reason()==Reason::Image);image[0xF0]^=1;
 bad=c;bad.camera.flags|=0x20000000;assert(validate(Operation::Running,e,bad).reason()==Reason::Camera);
 bad=c;put(bad.camera.event_data,0x3C,0x60000510);assert(validate(Operation::Running,e,bad).reason()==Reason::Camera);
 bad=c;bad.flags[8]=1;assert(validate(Operation::Running,e,bad).reason()==Reason::Manager);
 c.flags[3]=0x3F;assert(validate(Operation::Running,e,c).valid());
 assert(!validate(Operation::Release,e,c).released());
 Captured released=c;released.manager=e.retail_manager;released.camera_play=e.retail_camera_play;released.current_event=e.retail_current_event;released.flags=e.retail_flags;released.camera=e.camera;released.actors=e.actors;released.control_owned=false;released.control_released=true;
 auto result=validate(Operation::Release,e,released);assert(result.released());
 // Native manager endProc changes state/cameraPlay, not completed staff
 // cursors or People cuts. Terminal cleanup must represent that retained phase.
 auto terminal_image=image;put(terminal_image,0xE4,0);
 auto terminal=c;terminal.image=terminal_image;terminal.camera_play=0;terminal.current_event=0;
 assert(validate(Operation::Terminal,e,terminal).valid());
 assert(!validate(Operation::Running,e,terminal).valid());
 auto terminal_bad=terminal;terminal_bad.camera_play=1;assert(!validate(Operation::Terminal,e,terminal_bad).valid());
 terminal_bad=terminal;terminal_bad.current_event=e.identity.image+0x40;assert(!validate(Operation::Terminal,e,terminal_bad).valid());
 auto terminal_drain=terminal;terminal_drain.camera=e.camera;terminal_drain.actors=e.actors;
 assert(validate(Operation::TerminalDrain,e,terminal_drain).valid());
 assert(!validate(Operation::TerminalDrain,e,terminal).valid());
 auto terminal_release=released;terminal_release.image=terminal_image;
 auto terminal_result=validate(Operation::ReleaseTerminal,e,terminal_release);assert(terminal_result.released());
 // A nonzero retail scratch backup is preserved until startProc, which clears
 // every word. This is not permission for unrelated bits to become live, and
 // release must restore the exact prior bytes, including native-owned bits.
 auto prior=e;prior.retail_flags[365]=1;prior.retail_flags[3]=0x85;
 auto prior_prepare=prepared;auto prior_prepare_image=story_native_event_image::make_wait_fixture();prior_prepare.image=prior_prepare_image;prior_prepare.flags=prior.retail_flags;
 assert(validate(Operation::Prepare,prior,prior_prepare).valid());
 auto prior_order=ordered;prior_order.flags=prior.retail_flags;
 assert(validate(Operation::Publish,prior,prior_order).valid());
 prior_order.flags[365]=0;assert(validate(Operation::Publish,prior,prior_order).reason()==Reason::Manager);
 prior_order.flags=prior.retail_flags;prior_order.flags[3]^=1;
 assert(validate(Operation::Publish,prior,prior_order).reason()==Reason::Manager);
 assert(validate(Operation::Running,prior,c).valid());
 assert(validate(Operation::Restore,prior,c).valid());
 assert(validate(Operation::Terminal,prior,terminal).valid());
 bad=c;bad.flags[365]=1;assert(validate(Operation::Running,prior,bad).reason()==Reason::Manager);
 bad=c;bad.flags[3]|=0x80;assert(validate(Operation::Running,prior,bad).reason()==Reason::Manager);
 auto prior_release=terminal_release;prior_release.flags=prior.retail_flags;
 assert(validate(Operation::ReleaseTerminal,prior,prior_release).released());
 prior_release.flags.fill(0);assert(validate(Operation::ReleaseTerminal,prior,prior_release).reason()==Reason::Manager);
 assert(!validate(Operation::Release,e,terminal_release).released());
 // Restoring only EventData while leaving native Event type active is unsafe:
 // nextType would consume prior=-1. Partial type-pair restore is also invalid.
 bad=released;bad.camera.current_type=bad.camera.next_type=bad.camera.event_type;assert(!validate(Operation::Release,e,bad).released());
 bad=released;bad.camera.next_type=bad.camera.event_type;assert(!validate(Operation::Release,e,bad).released());
 bad=released;bad.camera.event_type++;assert(!validate(Operation::Release,e,bad).released());
 bad=released;bad.camera.work37C[0]=1;assert(!validate(Operation::Release,e,bad).released());
 bad=released;bad.camera.counter108=1;put(bad.camera.engine100,8,1);assert(!validate(Operation::Release,e,bad).released());
 bad=released;bad.camera.mode_phase13C[0]=1;assert(!validate(Operation::Release,e,bad).released());
 bad=released;bad.camera.initial_view010[0]=1;assert(!validate(Operation::Release,e,bad).released());
 auto poisoned=e;put(poisoned.camera.event_data,0x3C,e.identity.image+0x510);bad=released;bad.camera=poisoned.camera;assert(!validate(Operation::Release,poisoned,bad).released());
 poisoned=e;put(poisoned.actors[0].cut,0,e.identity.image+0xF0);bad=released;bad.actors=poisoned.actors;assert(!validate(Operation::Release,poisoned,bad).released());
 bad=released;bad.manager=c.manager;assert(!validate(Operation::Release,e,bad).released());
 bad=released;bad.actors[0]=c.actors[0];assert(!validate(Operation::Release,e,bad).released());
 bad=released;bad.camera=c.camera;assert(!validate(Operation::Release,e,bad).released());
 for(unsigned n=0;n<3;++n){bad=released;if(n==0)bad.manager_depth=1;if(n==1)bad.camera_depth=1;if(n==2)bad.npc_depth=1;assert(validate(Operation::Release,e,bad).reason()==Reason::Busy);}
 bad=released;bad.resource_generation_matches=false;assert(!validate(Operation::Release,e,bad).released());
 story_native_event_lease::Lease lease;assert(lease.prepare(e.identity));story_native_event_lease::Observation o{e.identity,false,true};assert(lease.publish(o)&&lease.started(o)&&lease.finish(o)&&lease.retire(o));
 assert(!lease.can_free(Result{}));assert(!lease.can_free(validate(Operation::Running,e,c)));assert(lease.can_free(result)&&lease.can_restore_retail(result));
 assert(lease.can_free(terminal_result)&&lease.can_restore_retail(terminal_result));
 auto other=e;other.identity.generation++;bad=released;bad.identity=other.identity;assert(!lease.can_free(validate(Operation::Release,other,bad)));lease.invalidate();assert(!lease.can_free(result));
}
