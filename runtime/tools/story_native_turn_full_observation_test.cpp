// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_observation.h"
#include <cassert>
#include <algorithm>
#include <vector>
#include <cstdint>
using namespace story_native_event_observation;
static void put(auto&b,size_t p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
int main(){
 story_native_turn_camera_image::Program def;def.camera.start_eye={0,10,20};def.camera.end_eye={10,10,20};
 Expected e;e.program=story_native_event_program::Program::turn(def,301);e.identity={0x20000000,0x60000000,0xE60,1,2,3,4};e.frames=def.wait_timer;e.stage_name={'s','e','a'};
 for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x20001000+n*0x1000;a.pid=10+n;a.token=1+n;a.profile=0x175;a.subtype=1;a.fingerprint[0]=uint8_t(n+1);}
 e.camera.bg=0x7F;e.camera.trim=0;e.camera.address=0x20003000;e.camera.pid=12;e.camera.event_type=2;e.camera.style_table=e.camera.param_pointer=0x1004487C;e.camera.style_count=4;e.camera.type_table=0x10049350;e.camera.type_count=3;put(e.camera.event_data,0xC,0xFFFFFFFFu);put(e.camera.event_data,4,0xFFFFFFFFu);
 std::vector<uint8_t>image(e.program->pristine().begin(),e.program->pristine().end());Captured c;c.identity=e.identity;c.stage_name=e.stage_name;c.image=image;c.actors=e.actors;c.camera=e.camera;c.resource_generation_matches=true;c.control_released=true;
 auto prepared=validate(Operation::Prepare,e,c);assert(prepared.valid()&&prepared.matches(e));auto changed=e;changed.program=story_native_event_program::Program::turn(def,302);assert(!prepared.matches(changed));changed=e;changed.program.reset();assert(!validate(Operation::Prepare,changed,c).valid());changed=e;changed.frames++;assert(validate(Operation::Prepare,changed,c).reason()==Reason::Definition);
 auto bad=c;bad.actors[1].pid++;assert(validate(Operation::Prepare,e,bad).reason()==Reason::Identity);bad=c;bad.control_released=false;assert(validate(Operation::Prepare,e,bad).reason()==Reason::Control);bad=c;bad.camera.event_data[0x3F]=1;assert(validate(Operation::Prepare,e,bad).reason()==Reason::Camera);
 c.control_released=false;c.control_owned=true;auto pools=e.program->pool_offsets();for(unsigned n=0;n<8;++n)c.manager[n]=e.identity.image+pools[n];assert(validate(Operation::Publish,e,c).valid());bad=c;bad.manager[4]=e.identity.image+0x410;assert(validate(Operation::Publish,e,bad).reason()==Reason::Manager);
 put(image,0xE4,1);for(unsigned n=0;n<2;++n){put(c.actors[n].cut,0,e.identity.image+0xF0+n*0x50);put(c.actors[n].cut,8,e.actors[n].address);put(c.actors[n].cut,0x10,0xFFFFFFFFu);}assert(validate(Operation::Publish,e,c).valid());
 put(image,0xE4,2);for(unsigned n=0;n<4;++n)put(image,0xF0+n*0x50+0x38,e.program->initial_cut(n));for(unsigned n=0;n<2;++n){put(c.actors[n].cut,4,n);put(c.actors[n].cut,0x10,3);}c.camera_play=1;c.current_event=e.identity.image+0x40;
 // Structurally legal live TURN still refuses an uninitialized camera tuple;
 // the copied native action4/5 Work policy must also hold.
 assert(validate(Operation::Running,e,c).reason()==Reason::Camera);
 // Audited FIXEDFRM4 copies canonical values into typed Work; no payload
 // address is accepted as a borrowed camera consumer.
 c.camera.bg=9;c.camera.trim=2;c.camera.current_type=c.camera.next_type=c.camera.event_type;put(c.camera.event_data,0xC,e.camera.current_type);put(c.camera.event_data,4,2);put(c.camera.event_data,0x1C,2);put(c.camera.event_data,0x24,1);
 auto copied=[&](size_t dst,size_t src,size_t count){std::copy_n(e.program->pristine().begin()+src,count,c.camera.work37C.begin()+dst);};
 put(image,0x190+0x3C,4);c.camera.work37C[0]=1;copied(4,0xD80,12);copied(0x10,0xD8C,12);copied(0x1C,0xD98,4);c.camera.work37C[0x28]=c.camera.work37C[0x29]='-';put(c.camera.work37C,0x2C,def.camera.hold_timer);c.camera.work37C[0x30]=1;c.camera.timer=0x41200000; // raw Work+8 is Eye.y, not a timer for this action
 assert(validate(Operation::Running,e,c).valid());
 for(unsigned fault=0;fault<8;++fault){auto x=c;if(fault==0)x.camera.flags|=0x20000000;if(fault==1)put(x.camera.event_data,0x3C,e.identity.image+0xD80);if(fault==2)put(x.camera.work37C,0x24,e.actors[0].address);if(fault==3)x.camera.work37C[0x28]='r';if(fault==4)put(x.camera.work37C,0x2C,def.camera.hold_timer+1);if(fault==5)put(x.camera.event_data,0x24,0);if(fault==6)put(x.camera.event_data,0xCC,e.identity.image+0xD80);if(fault==7)x.camera.done100[0]=2;assert(validate(Operation::Running,e,x).reason()==Reason::Camera);}
 for(unsigned fault=0;fault<5;++fault){auto x=c;if(fault==0)put(x.camera.view03C,8,0x7FC00000);if(fault==1)put(x.camera.view03C,0x24,0x7F800000);if(fault==2)put(x.camera.initial_view010,0x18,0xFF800000);if(fault==3)put(x.camera.initial_view010,0x28,0x7FC00000);if(fault==4)put(x.camera.positions0A4,0x38,0x7FC00000);assert(validate(Operation::Running,e,x).reason()==Reason::Camera);}
 c.camera.elapsed11C=1000;put(c.camera.engine100,0x1C,1000);assert(validate(Operation::Running,e,c).valid()); // completion is not immediate detach
 bad=c;bad.actors[1].token++;assert(validate(Operation::Running,e,bad).reason()==Reason::Identity);bad=c;bad.flags[2]=2;assert(validate(Operation::Running,e,bad).reason()==Reason::Manager);bad=c;bad.elapsed_ticks=301;assert(validate(Operation::Running,e,bad).reason()==Reason::Image);
 put(image,0x128,0);assert(validate(Operation::Running,e,c).reason()==Reason::Staff);put(image,0x128,7);
 // UNITRANS5 has a different copied Work layout. Its absent actor PID slot
 // and padding are not initialized by native code and are never zero-assumed.
 put(image,0x190+0x38,6);put(image,0x190+0x3C,5);c.flags[3]=4;c.camera.work37C.fill(0);copied(0,0xDA0,12);copied(0xC,0xDAC,12);copied(0x18,0xDD0,4);copied(0x20,0xDB8,12);copied(0x2C,0xDC4,12);copied(0x38,0xDD4,4);put(c.camera.work37C,0x44,0xDEADBEEF);for(unsigned n=0;n<4;++n)c.camera.work37C[0x48+n]='-';put(c.camera.work37C,0x50,def.camera.move_timer);put(c.camera.work37C,0x58,0x3F800000);c.camera.work37C[0x64]=1;c.camera.timer=0x41A00000;assert(validate(Operation::Running,e,c).valid());
 for(unsigned fault=0;fault<6;++fault){auto x=c;if(fault==0)put(x.camera.work37C,0x40,e.actors[0].address);if(fault==1)put(x.camera.work37C,0x68,e.identity.image+0xD80);if(fault==2)put(x.camera.work37C,0x54,1);if(fault==3)put(x.camera.work37C,0x58,0);if(fault==4)put(x.camera.work37C,0x5C,0x7F800000);if(fault==5)x.camera.work37C[0x48]='r';assert(validate(Operation::Running,e,x).reason()==Reason::Camera);}
 put(image,0xE4,0);assert(!validate(Operation::Terminal,e,c).valid()&&!validate(Operation::ReleaseTerminal,e,c).released());
 // EndProc NONE retains the completed cursors, exact action caches and all
 // nine actual scratch flags. A released baseline has restored retail flags,
 // which must never be projected into invented completion flags.
 for(unsigned n=0;n<4;++n){const uint32_t cuts[]={0,1,6,5},actions[]={0,0,5,1};put(image,0xF0+n*0x50+0x38,cuts[n]);put(image,0xF0+n*0x50+0x3C,actions[n]);}for(unsigned n=0;n<2;++n)put(c.actors[n].cut,0x10,0);c.flags[2]=1;c.flags[3]=0xFF;c.camera_play=0;c.current_event=0;
 auto te=e;te.retail_flags[2]=0xA5;te.retail_flags[3]=0x80;assert(validate(Operation::Terminal,te,c).valid());
 for(unsigned bit=0;bit<9;++bit){auto x=c;x.flags[bit<8?3:2]^=uint8_t(1u<<(bit<8?bit:0));assert(!validate(Operation::Terminal,te,x).valid());}
 for(unsigned n=0;n<4;++n){const auto saved=uint32_t(n<2?0:n==2?5:1);put(image,0xF0+n*0x50+0x3C,0xFFFFFFFFu);assert(validate(Operation::Terminal,te,c).reason()==Reason::Staff);put(image,0xF0+n*0x50+0x3C,saved);}
 auto returned=c;returned.camera.current_type=returned.camera.next_type=te.camera.current_type;put(returned.camera.event_data,0xC,0xFFFFFFFFu);returned.camera.bg=te.camera.bg;returned.camera.trim=te.camera.trim;assert(validate(Operation::Terminal,te,returned).reason()==Reason::Camera);assert(diagnose_camera(te,returned,true).failure==CameraFailure::FollowTag);
 // The entered terminal still uses canonical UNIT Work. Detailed diagnostics
 // identify the exact copied field without widening returned Follow admission.
 auto detailed=c;detailed.camera.work37C[0x20]^=1;auto explanation=diagnose_camera(te,detailed,true);assert(explanation.failure==CameraFailure::WorkCopied&&explanation.offset==0x39C&&explanation.actual==detailed.camera.work37C[0x20]&&explanation.expected==c.camera.work37C[0x20]);
 detailed=c;put(detailed.camera.event_data,0xCC,e.identity.image+0xD80);assert(diagnose_camera(te,detailed,true).failure==CameraFailure::BorrowedPointer);
 detailed=c;detailed.camera.counter108++;assert(diagnose_camera(te,detailed,true).failure==CameraFailure::RawConsistency);
 detailed=c;put(detailed.camera.view03C,8,0x7FC00000);explanation=diagnose_camera(te,detailed,true);assert(explanation.failure==CameraFailure::NonFinite&&explanation.offset==0x44);
 auto live_returned=returned;put(image,0xE4,2);live_returned.camera_play=1;live_returned.current_event=e.identity.image+0x40;assert(!validate(Operation::Running,te,live_returned).valid());put(image,0xE4,0);
 // After genuine return, native Follow overwrites the Work union. Only an
 // exact retail tuple and current FLLW accepts this numeric ABI. The captured
 // union may still belong to an earlier engine before its first actual Run.
 auto follow_expected=te;put(follow_expected.camera.work37C,0,0xDEADBEEF);auto follow=returned;follow.camera.work37C.fill(0);put(follow.camera.work37C,0,0x464C4C57);put(follow.camera.work37C,4,1);put(follow.camera.work37C,0x10,0x50);follow.camera.work37C[0x60]=1;follow.camera.work37C[0x63]=0xFE;put(follow.camera.work37C,0x7C,0xDEADBEEF);follow.camera.timer=0;
 assert(validate(Operation::Terminal,follow_expected,follow).valid()&&diagnose_camera(follow_expected,follow,true).failure==CameraFailure::None);
 for(unsigned fault=0;fault<18;++fault){auto x=follow;auto expected=follow_expected;
  if(fault==0)x.camera.work37C[0]^=1;if(fault==1)put(x.camera.work37C,8,0x7FC00000);if(fault==2)put(x.camera.work37C,0x78,0x7F800000);if(fault==3)put(x.camera.work37C,4,0);if(fault==4)put(x.camera.work37C,0x10,0x51);if(fault==5)put(x.camera.work37C,0x3C,2);if(fault==6)x.camera.work37C[0x61]=2;if(fault==7)x.camera.current_type=x.camera.event_type;if(fault==8){x.camera.style=1;x.camera.param_index=1;x.camera.param_pointer+=0x84;}if(fault==9)put(x.camera.mode_phase13C,0,1);if(fault==10)put(x.camera.event_data,0xCC,e.identity.image+0xD80);if(fault==11)put(x.camera.event_data,0x1C,0);if(fault==12)x.camera.work37C[0x60]=2;if(fault==13)x.camera.work37C[0x62]=2;if(fault==14)x.camera.bg^=1;if(fault==15)x.camera.trim=2;if(fault==16)put(x.camera.event_data,0x14,1);if(fault==17){x.camera.bg=9;x.camera.trim=2;put(x.camera.mode_phase13C,4,1);}
  // Work+8 is also captured as the raw timer field, even for Follow floats.
  x.camera.timer=uint32_t(x.camera.work37C[8])<<24|uint32_t(x.camera.work37C[9])<<16|uint32_t(x.camera.work37C[10])<<8|x.camera.work37C[11];
  assert(validate(Operation::Terminal,expected,x).reason()==Reason::Camera);
 }
 put(image,0xE4,2);auto follow_running=follow;follow_running.camera_play=1;follow_running.current_event=e.identity.image+0x40;assert(!validate(Operation::Running,follow_expected,follow_running).valid());put(image,0xE4,0);
 auto drained=c;drained.camera=te.camera;drained.actors=te.actors;assert(validate(Operation::TerminalDrain,te,drained).valid());
 auto released=drained;released.manager=te.retail_manager;released.flags=te.retail_flags;released.camera_play=te.retail_camera_play;released.current_event=te.retail_current_event;released.control_owned=false;released.control_released=true;auto release=validate(Operation::ReleaseTerminal,te,released);assert(release.valid()&&release.released()&&release.matches(te));bad=released;bad.flags[3]^=1;assert(validate(Operation::ReleaseTerminal,te,bad).reason()==Reason::Manager);bad=released;bad.control_owned=true;assert(validate(Operation::ReleaseTerminal,te,bad).reason()==Reason::Control);

 // E60 cannot be prepared by an extent-only identity. The opaque Prepare
 // receipt binds every Expected field through retirement and release.
 story_native_event_lease::Lease raw;assert(!raw.prepare(e.identity));
 story_native_event_lease::Lease typed;assert(typed.prepare(prepared));story_native_event_lease::Observation observation{e.identity,false,true};assert(typed.publish(observation)&&typed.started(observation)&&typed.finish(observation)&&typed.retire(observation));
 auto original_release=released;original_release.flags=e.retail_flags;const auto original_result=validate(Operation::ReleaseTerminal,e,original_release);assert(original_result.released()&&typed.can_free(original_result));assert(!typed.can_free(release));
 auto different_definition=e;different_definition.program=story_native_event_program::Program::turn(def,302);const auto other_result=validate(Operation::ReleaseTerminal,different_definition,original_release);assert(other_result.released()&&!typed.can_free(other_result));

}
