// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_observation.h"
#include "story_native_event_image.h"
#include "story_native_turn_camera_observation.h"
#include <algorithm>
#include <bit>
#include <cmath>
namespace story_native_event_observation {namespace {
uint32_t word(std::span<const uint8_t>b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool same(const story_native_event_lease::Identity&a,const story_native_event_lease::Identity&b){return a.manager==b.manager&&a.image==b.image&&a.bytes==b.bytes&&a.stage==b.stage&&a.room==b.room&&a.generation==b.generation&&a.program==b.program;}
bool actor_same(const Actor&a,const Actor&b){return a.address==b.address&&a.pid==b.pid&&a.token==b.token&&a.profile==b.profile&&a.subtype==b.subtype;}
bool span(uint32_t p,uint32_t n){return p&&!(p&3)&&uint64_t(p)+n<=0x100000000ull;}
bool overlaps(uint32_t p,uint32_t n,const Expected&e){return uint64_t(p)<uint64_t(e.identity.image)+e.identity.bytes&&uint64_t(e.identity.image)<uint64_t(p)+n;}
bool in_image(uint32_t p,const Expected&e){return p>=e.identity.image&&uint64_t(p)<uint64_t(e.identity.image)+e.identity.bytes;}
bool turn_camera(const Expected&e,const Captured&c,bool terminal,CameraDiagnostic*diagnostic=nullptr){
 const auto&camera=c.camera;const auto&base=e.camera;const auto&work=camera.work37C;
 auto fail=[&](CameraFailure f,uint32_t p=0,uint32_t actual=0,uint32_t expected=0){if(diagnostic)*diagnostic={f,p,actual,expected};return false;};
 auto numeric=[&](std::span<const uint8_t>b,unsigned p,unsigned count,unsigned native){for(unsigned n=0;n<count;++n)if(!std::isfinite(std::bit_cast<float>(word(b,p+n*4))))return fail(CameraFailure::NonFinite,native+p+n*4,word(b,p+n*4));return true;};
 if(!numeric(camera.view03C,0,1,0x3C)||!numeric(camera.view03C,8,6,0x3C)||!numeric(camera.view03C,0x24,1,0x3C)||!numeric(camera.initial_view010,0,9,0x10)||!numeric(camera.initial_view010,0x28,1,0x10))return false;
 for(unsigned n=0;n<2;++n)if(!numeric(camera.positions0A4,n*0x20,7,0xA4))return false;
 const auto selected=e.program->cut(2,word(c.image,0x1C8));
 if(!selected)return fail(CameraFailure::SelectedCut,0x1C8,word(c.image,0x1C8));
 if(word(c.image,0x1CC)!=selected->action)return fail(CameraFailure::SelectedCache,0x1CC,word(c.image,0x1CC),selected->action);
 if(!terminal&&camera.current_type!=camera.event_type)return fail(CameraFailure::CurrentType,0,camera.current_type,camera.event_type);
 if(camera.flags&0x20000000u)return fail(CameraFailure::Debug,0,camera.flags);
 for(const auto&v:std::array<std::pair<unsigned,uint32_t>,5>{{{4,2},{0x1C,2},{0x20,0},{0x24,1},{0x28,0}}})if(word(camera.event_data,v.first)!=v.second)return fail(CameraFailure::EventProperty,0x3FC+v.first,word(camera.event_data,v.first),v.second);
 // Ordinary Run restores type, then derives trim/BG from the retail mode/style.
 const bool returned=terminal&&camera.current_type==base.current_type&&camera.next_type==base.next_type&&word(camera.event_data,0xC)==0xFFFFFFFFu;
 if(returned){
  if(camera.style!=base.style)return fail(CameraFailure::ReturnedStyle,0,camera.style,base.style);
  for(unsigned p:{0u,4u})if(word(camera.mode_phase13C,p)!=word(base.mode_phase13C,p))return fail(CameraFailure::ReturnedMode,0x13C+p,word(camera.mode_phase13C,p),word(base.mode_phase13C,p));
 }
 if(camera.bg!=(returned?base.bg:9))return fail(CameraFailure::Background,0x68,camera.bg,returned?base.bg:9);
 if(camera.trim!=(returned?base.trim:2))return fail(CameraFailure::Trim,0,camera.trim,returned?base.trim:2);
 for(unsigned p=0;p<camera.event_data.size();++p)if((p>=0x14&&p<0x1C)||p>=0x2C)if(camera.event_data[p]!=base.event_data[p])return fail(CameraFailure::RetainedEventData,0x3FC+p,camera.event_data[p],base.event_data[p]);
 for(unsigned p=0;p<camera.done100.size();++p)if(camera.done100[p]>1)return fail(CameraFailure::DoneBoolean,0x100+p,camera.done100[p],1);
 // Actual returned Follow engine025028B8 uses this pointer-free0x7C union
 // (raw02502A44 selects camera+37C;02502F1C initializes FLLW). Admit
 // only the exact captured retail style/modes above. Production camera_policy
 // additionally proves the selected retail algorithm is Follow1 from tables;
 // onTypeChange/onModeChange reset counters without initializing the union.
 if(returned){
  if(word(work,0)!=0x464C4C57u)return fail(CameraFailure::FollowTag,0x37C,word(work,0),0x464C4C57u);
  // Every listed float is initialized by native Follow. Packed s16 angles,
  // signed turn counter14, padding63 and tail7C..7F are not float/pointer fields.
  if(!numeric(work,8,2,0x37C)||!numeric(work,0x1C,8,0x37C)||!numeric(work,0x40,8,0x37C)||!numeric(work,0x64,6,0x37C))return false;
  if(int32_t(word(work,4))<=0)return fail(CameraFailure::FollowCounter,0x380,word(work,4),1);
  if(word(work,0x10)>0x50)return fail(CameraFailure::FollowCounter,0x38C,word(work,0x10),0x50);
  if(word(work,0x3C)>1)return fail(CameraFailure::FollowBoolean,0x3B8,word(work,0x3C),1);
  for(unsigned p:{0x60u,0x61u,0x62u})if(work[p]>1)return fail(CameraFailure::FollowBoolean,0x37C+p,work[p],1);
  return true;
 }
 const auto canonical=e.program->pristine();
 auto copied=[&](unsigned dst,unsigned src,unsigned count){for(unsigned p=0;p<count;++p)if(work[dst+p]!=canonical[src+p])return fail(CameraFailure::WorkCopied,0x37C+dst+p,work[dst+p],canonical[src+p]);return true;};
 auto scalar=[&](unsigned p,uint32_t expected){return word(work,p)==expected||fail(CameraFailure::WorkScalar,0x37C+p,word(work,p),expected);};
 auto byte=[&](unsigned p,uint8_t expected){return work[p]==expected||fail(CameraFailure::WorkByte,0x37C+p,work[p],expected);};
 if(word(c.image,0x1C8)==2)return byte(0,1)&&copied(4,0xD80,12)&&copied(0x10,0xD8C,12)&&copied(0x1C,0xD98,4)&&scalar(0x20,0)&&scalar(0x24,0)&&byte(0x28,'-')&&byte(0x29,'-')&&byte(0x2A,0)&&scalar(0x2C,e.program->turn_definition()->camera.hold_timer)&&byte(0x30,1)&&scalar(0x34,0)&&scalar(0x38,0)&&scalar(0x3C,0);
 if(!(copied(0,0xDA0,12)&&copied(0xC,0xDAC,12)&&copied(0x18,0xDD0,4)&&scalar(0x1C,0)&&copied(0x20,0xDB8,12)&&copied(0x2C,0xDC4,12)&&copied(0x38,0xDD4,4)&&scalar(0x3C,0)&&scalar(0x40,0)&&byte(0x48,'-')&&byte(0x49,'-')&&byte(0x4A,'-')&&byte(0x4B,'-')&&byte(0x4C,0)&&scalar(0x50,e.program->turn_definition()->camera.move_timer)&&scalar(0x54,0)&&scalar(0x58,0x3F800000u)))return false;
 const float radius=std::bit_cast<float>(word(work,0x5C));
 if(!std::isfinite(radius)||radius<0)return fail(CameraFailure::Radius,0x3D8,word(work,0x5C));
 return byte(0x64,1)&&scalar(0x68,0);
}
}
bool valid_camera_transition(const Camera&base,const Camera&now){
 if(!base.type_table||!base.type_count||base.type_count>4096||base.current_type>=base.type_count||base.next_type>=base.type_count||base.event_type>=base.type_count||now.type_table!=base.type_table||now.type_count!=base.type_count||now.current_type>=now.type_count||now.next_type>=now.type_count||word(base.mode_phase13C,0)>=20||word(base.mode_phase13C,4)>=20||word(now.mode_phase13C,0)>=20||word(now.mode_phase13C,4)>=20||!base.style_table||!base.style_count||base.style_count>4096||base.style>=base.style_count||base.param_index!=base.style||uint64_t(base.style_table)+uint64_t(base.style)*0x84!=base.param_pointer||now.style_table!=base.style_table||now.style_count!=base.style_count||now.style>=now.style_count||now.param_index!=now.style||uint64_t(now.style_table)+uint64_t(now.style)*0x84!=now.param_pointer||base.current_type==0xFFFFFFFFu||base.current_type!=base.next_type||base.current_type==base.event_type||base.event_type==0xFFFFFFFFu||word(base.event_data,0x0C)!=0xFFFFFFFFu||now.event_type!=base.event_type)return false;
 const bool idle=now.current_type==base.current_type&&now.next_type==base.next_type&&word(now.event_data,0x0C)==0xFFFFFFFFu;
 const bool entered=now.current_type==base.event_type&&(now.next_type==base.event_type||now.next_type==base.next_type)&&word(now.event_data,0x0C)==base.current_type;
 return idle||entered;
}
CameraDiagnostic diagnose_camera(const Expected&e,const Captured&c,bool terminal) noexcept{
 try{
  if(!e.program||e.program->kind()!=story_native_event_program::Kind::TurnCamera||c.image.size()!=e.program->bytes())return {CameraFailure::Definition};
  if(!span(e.camera.address,0x8E0)||overlaps(e.camera.address,0x8E0,e)||!e.camera.pid||e.camera.pid==0xFFFFFFFFu||c.camera.address!=e.camera.address||c.camera.pid!=e.camera.pid)return {CameraFailure::Identity};
  if(word(c.camera.work37C,8)!=c.camera.timer||word(e.camera.work37C,8)!=e.camera.timer||word(c.camera.engine100,8)!=c.camera.counter108||word(c.camera.engine100,0x18)!=c.camera.counter118||word(c.camera.engine100,0x1C)!=c.camera.elapsed11C||!std::equal(c.camera.done100.begin(),c.camera.done100.end(),c.camera.engine100.begin()))return {CameraFailure::RawConsistency};
  if(!valid_camera_transition(e.camera,c.camera))return {CameraFailure::Transition};
  for(unsigned n=0;n<8;++n){unsigned p=0x3C+n*0x14;if(in_image(word(c.camera.event_data,p),e))return {CameraFailure::BorrowedPointer,0x3FC+p,word(c.camera.event_data,p)};}
  if(in_image(word(c.camera.event_data,0xCC),e))return {CameraFailure::BorrowedPointer,0x4C8,word(c.camera.event_data,0xCC)};
  CameraDiagnostic d;turn_camera(e,c,terminal,&d);return d;
 }catch(...){return {CameraFailure::Definition};}
}
Result validate(Operation op,const Expected&e,const Captured&c){
 Result r;r.identity_=e.identity;r.operation_=op;auto fail=[&](Reason why){r.reason_=why;return r;};
 const auto&i=e.identity;
 const bool turn=e.program&&e.program->kind()==story_native_event_program::Kind::TurnCamera;
 const uint32_t extent=e.program?e.program->bytes():0x520;
 if(e.program&&e.program->frames()!=e.frames)return fail(Reason::Definition);
 if(!span(i.manager,0x540)||overlaps(i.manager,0x540,e)||!e.stage_name[0]||!span(i.image,extent)||(i.image&15)||i.bytes!=extent||!i.stage||!i.room||!i.generation||!i.program||e.room_number>63||!e.frames||e.frames>32767)return fail(Reason::Definition);
 if(!same(i,c.identity)||e.stage_name!=c.stage_name||e.room_number!=c.room_number||!c.resource_generation_matches)return fail(Reason::Identity);
 if(c.foreign)return fail(Reason::Foreign);
 if(c.image.size()!=extent)return fail(Reason::Image);
 auto canonical=turn?std::vector<uint8_t>(e.program->pristine().begin(),e.program->pristine().end()):story_native_event_image::make_wait_fixture(e.frames);
 const uint32_t state=word(c.image,0x40+0xA4);r.event_state_=state;
 const bool terminal=op==Operation::Terminal||op==Operation::TerminalDrain||op==Operation::ReleaseTerminal;
 const bool live_staff=state>=2||terminal;
 if(state!=0&&state!=1&&state!=2&&state!=4)return fail(Reason::Image);
 if(terminal&&state!=0)return fail(Reason::Image);
 if((op==Operation::Prepare||op==Operation::Publish)&&state>1)return fail(Reason::Image);
 if(op==Operation::Running&&state!=2&&state!=4)return fail(Reason::Image);
 // Only native event state and documented staff current/cache/timer/state bytes
 // are mutable. All names, section offsets, indices, pools and chains stay exact.
 for(size_t p=0;p<canonical.size();++p){bool mutable_byte=p>=0xE4&&p<0xE8;
  for(unsigned s=0;s<4;++s){size_t start=0xF0+s*0x50;mutable_byte|=p>=start+0x38&&p<start+0x48;}
  if(!mutable_byte&&c.image[p]!=canonical[p])return fail(Reason::Image);
 }
 if(turn){
  // ORDER cancellation remains unsupported. Normal native completion has a
  // distinct exhausted grammar, never a forged Running state or retail flag
  // projection. External opaque Terminal/CameraReturn receipts remain required.
  if(op==Operation::Release)return fail(Reason::Image);
  namespace T=story_native_turn_camera_observation;
  T::Capture tc;tc.identity=c.identity;std::copy(c.image.begin(),c.image.end(),tc.image.begin());
  tc.exclusive=true;tc.foreign=c.foreign;tc.elapsed_ticks=c.elapsed_ticks;
  // Retail flags are still unrelated scratch bytes before native startProc.
  if(live_staff&&op!=Operation::ReleaseTerminal)tc.completion_flags=word(c.flags,0)&e.program->completion_mask();
  const auto phase=terminal?(op==Operation::ReleaseTerminal?T::Phase::ExhaustedReleased:T::Phase::Terminal):state>=2?T::Phase::Running:op==Operation::Prepare?T::Phase::Pristine:T::Phase::Published;
  auto checked=T::validate(phase,i,*e.program->turn_definition(),tc,e.program->watchdog());
  if(!checked.valid)return fail(checked.reason==T::Reason::Staff||checked.reason==T::Reason::Barrier?Reason::Staff:Reason::Image);
 }
 if(!turn)for(unsigned s=0;s<4;++s){const size_t p=0xF0+s*0x50;const uint32_t current=word(c.image,p+0x38),cache=word(c.image,p+0x3C);
  if(!live_staff){for(size_t k=0x38;k<0x48;++k)if(c.image[p+k]!=canonical[p+k])return fail(Reason::Staff);}
  else {
   if((s<3&&current!=s)||(s==3&&(current<3||current>5)))return fail(Reason::Staff);
   // Native action tables: NPC WAIT=0; CAMERA WAIT=1; TIMEKEEPER COUNTDOWN=0/WAIT=1.
   const uint32_t action=s<2?0:s==2?1:current==3?0:1;
   if(cache!=0xFFFFFFFFu&&cache!=action)return fail(Reason::Staff);
   if(word(c.image,p+0x40)>>16 || ((uint32_t(c.image[p+0x42])<<8)|c.image[p+0x43])>e.frames||c.image[p+0x44]||c.image[p+0x45]>1||c.image[p+0x46]>2||c.image[p+0x47]>1)return fail(Reason::Staff);
  }
 }
 const bool release=op==Operation::Release||op==Operation::ReleaseTerminal,idle=release||op==Operation::Prepare,detached=idle||op==Operation::Drain||op==Operation::TerminalDrain;
 std::array<uint32_t,8> published={i.image,i.image+0x40,i.image+0xF0,i.image+0x230,i.image+0x410,0,i.image+0x510,0};
 if(turn){const auto offsets=e.program->pool_offsets();for(unsigned n=0;n<8;++n)published[n]=i.image+offsets[n];}
 if(c.manager!=(idle?e.retail_manager:published))return fail(Reason::Manager);
 if(idle){for(auto p:c.manager)if(in_image(p,e))return fail(Reason::Manager);if(c.camera_play!=e.retail_camera_play||c.current_event!=e.retail_current_event||c.flags!=e.retail_flags)return fail(Reason::Manager);}
 else {
  if(terminal){if(c.current_event||c.camera_play)return fail(Reason::Manager);}
  else if((c.current_event!=0&&(state<2||c.current_event!=i.image+0x40))||c.camera_play!=(state>=2?1u:0u))return fail(Reason::Manager);
  // Native startProc clears all 320 flag words before engine resume. While
  // running, only this fixture's flags0..5 may be set in that scratch array.
  // Before native start, and after release above, the retail backup is exact.
  // A real producer must additionally authenticate its native-start receipt;
  // these byte checks alone never grant ownership or invocation authority.
  if(!live_staff){if(c.flags!=e.retail_flags)return fail(Reason::Manager);}
  else for(size_t k=0;k<c.flags.size();++k){const uint8_t mask=turn?(k==2?1:k==3?0xFF:0):(k==3?0x3F:0);
   if(c.flags[k]&~mask)return fail(Reason::Manager);
  }
 }
 for(unsigned n=0;n<2;++n){const auto&a=e.actors[n];const auto&b=c.actors[n];
  if(!span(a.address,0xA24)||overlaps(a.address,0xA24,e)||in_image(word(a.cut,0),e)||!a.pid||a.pid==0xFFFFFFFFu||!a.token||!a.profile||!actor_same(a,b))return fail(Reason::Identity);
  if(detached||(state==0&&!terminal)){if(b.cut!=a.cut)return fail(Reason::Staff);}
  else if(state==1){
   if(word(b.cut,0)!=i.image+0xF0+n*0x50||word(b.cut,8)!=a.address||word(b.cut,4)!=word(a.cut,4)||word(b.cut,0x10)!=0xFFFFFFFFu)return fail(Reason::Staff);
  }
  else {
   const auto selected=turn?e.program->cut(n,word(c.image,0xF0+n*0x50+0x38)):std::optional<story_native_event_program::Cut>{story_native_event_program::Cut{n,0,e.frames}};
   if(turn&&terminal&&word(b.cut,0x10)!=0)return fail(Reason::Staff);
   if(!selected||word(b.cut,0)!=i.image+0xF0+n*0x50||word(b.cut,8)!=a.address||word(b.cut,4)!=n||(word(b.cut,0x10)!=selected->action&&word(b.cut,0x10)!=0xFFFFFFFFu))return fail(Reason::Staff);
  }
  if(turn&&(a.profile!=0x175||a.subtype!=1||a.fingerprint!=b.fingerprint))return fail(Reason::Identity);
 }
 if(e.actors[0].address==e.actors[1].address||e.actors[0].pid==e.actors[1].pid||e.actors[0].token==e.actors[1].token)return fail(Reason::Definition);
 if(!span(e.camera.address,0x8E0)||overlaps(e.camera.address,0x8E0,e)||!e.camera.pid||e.camera.pid==0xFFFFFFFFu||c.camera.address!=e.camera.address||c.camera.pid!=e.camera.pid)return fail(Reason::Camera);
 if(word(c.camera.work37C,8)!=c.camera.timer||word(e.camera.work37C,8)!=e.camera.timer||word(c.camera.engine100,8)!=c.camera.counter108||word(c.camera.engine100,0x18)!=c.camera.counter118||word(c.camera.engine100,0x1C)!=c.camera.elapsed11C||!std::equal(c.camera.done100.begin(),c.camera.done100.end(),c.camera.engine100.begin()))return fail(Reason::Camera);
 if(!valid_camera_transition(e.camera,c.camera))return fail(Reason::Camera);
 for(unsigned n=0;n<8;++n)if(in_image(word(c.camera.event_data,0x3C+n*0x14),e))return fail(Reason::Camera);
 if(in_image(word(c.camera.event_data,0xCC),e))return fail(Reason::Camera);
 if(detached||!live_staff){if(c.camera.event_data!=e.camera.event_data||c.camera.flags!=e.camera.flags||c.camera.timer!=e.camera.timer||c.camera.current_type!=e.camera.current_type||c.camera.next_type!=e.camera.next_type||c.camera.event_type!=e.camera.event_type||c.camera.style!=e.camera.style||c.camera.type_table!=e.camera.type_table||c.camera.type_count!=e.camera.type_count||c.camera.style_table!=e.camera.style_table||c.camera.style_count!=e.camera.style_count||c.camera.param_pointer!=e.camera.param_pointer||c.camera.param_index!=e.camera.param_index||c.camera.bg!=e.camera.bg||c.camera.trim!=e.camera.trim||c.camera.counter108!=e.camera.counter108||c.camera.counter118!=e.camera.counter118||c.camera.elapsed11C!=e.camera.elapsed11C||c.camera.type_gate518!=e.camera.type_gate518||c.camera.engine100!=e.camera.engine100||c.camera.mode_phase13C!=e.camera.mode_phase13C||c.camera.initial_view010!=e.camera.initial_view010||c.camera.done100!=e.camera.done100||c.camera.work37C!=e.camera.work37C||c.camera.positions0A4!=e.camera.positions0A4||c.camera.view03C!=e.camera.view03C)return fail(Reason::Camera);}
 else {
  if(turn){if(!turn_camera(e,c,terminal))return fail(Reason::Camera);}
  else {
  if((!terminal&&c.camera.current_type!=c.camera.event_type)||c.camera.flags&0x20000000u||word(c.camera.event_data,4)!=2||c.camera.timer>e.frames)return fail(Reason::Camera);
  // WAIT never owns a spline or a stage-event pointer. Preserve their captured
  // bytes and cached pointer slots; do not silently admit other camera actions.
  for(unsigned n=0;n<8;++n){size_t p=0x2C+n*0x14+0x10;if(word(c.camera.event_data,p)!=word(e.camera.event_data,p)||in_image(word(c.camera.event_data,p),e))return fail(Reason::Camera);}
  if(!std::equal(c.camera.event_data.begin()+0xCC,c.camera.event_data.end(),e.camera.event_data.begin()+0xCC))return fail(Reason::Camera);
  }
 }
 if(idle){if(!c.control_released||c.control_owned)return fail(Reason::Control);}
 else if(!c.control_owned||c.control_released)return fail(Reason::Control);
 if(c.manager_depth||c.camera_depth||c.npc_depth)return fail(Reason::Busy);
 r.expected_=e;r.valid_=true;r.reason_=Reason::None;r.release_=release;return r;
}
}
