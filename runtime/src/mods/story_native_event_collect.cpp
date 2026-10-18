// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_collect.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
#include <utility>
namespace story_native_event_collect {namespace {
const uint32_t Play=GD(0x1046F0B0),PlayInitialized=GD(0x104753A8),StageProcess=GD(0x1047E6C4),RoomProcesses=GD(0x1047E8F0);
const uint32_t TypeTable=GD(0x10049350),TypeCount=GD(0x10049348);
const uint32_t StyleTable=GD(0x1004487C),StyleCount=GD(0x10044878);
const uint32_t PeopleProfile=GD(0x101C4CD0),CameraProfile0=GD(0x101D55F0),CameraProfile1=GD(0x101D5634),CameraMethods=GD(0x101D5538);
const std::array<uint32_t,5> CameraFunctions={GC(0x025021A4),GC(0x02502288),GC(0x024FFA3C),GC(0x0250235C),GC(0x024FFC40)};
uint32_t word(std::span<const uint8_t>b,size_t p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
uint16_t half(std::span<const uint8_t>b,size_t p){return uint16_t(b[p])<<8|b[p+1];}
void put(std::span<uint8_t>b,size_t p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
uint32_t checksum(std::span<const uint8_t>b){uint32_t h=2166136261u;for(auto x:b)h=(h^x)*16777619u;return h;}
bool span(uint32_t p,size_t n,bool runtime){const uint32_t lo=runtime?mem::kRuntimeStart:mem::kMem2Start,hi=runtime?mem::kHostStart:mem::kMem2End;return !(p&3)&&n&&n<=hi-lo&&p>=lo&&uint64_t(p)+n<=hi;}
}
std::array<uint8_t,64> make_stamp(const Expected&e){std::array<uint8_t,64>b{};const auto&i=e.identity;
 const std::array<uint32_t,15>v={0x4E455631,1,i.generation,i.stage,i.room,i.manager,i.image,i.bytes,uint32_t(i.program>>32),uint32_t(i.program),e.room_number,e.camera.address-0x248,e.camera.pid,e.actors[0].token,e.actors[1].token};
 for(unsigned n=0;n<v.size();++n)put(b,n*4,v[n]);put(b,60,checksum(std::span(b).first(60)));return b;}
Collector::Collector(Expected e,Config c,Read r,VerifyActor a,VerifyAllocation allocation,VerifyCamera camera,GuardSnapshot g):expected_(std::move(e)),config_(c),read_(std::move(r)),actor_(std::move(a)),allocation_(std::move(allocation)),camera_(std::move(camera)),guards_(std::move(g)){}
bool Collector::bytes(uint32_t p,std::span<uint8_t>b,bool runtime){if(!span(p,b.size(),runtime)){error_=Error::Span;return false;}if(!read_(p,b)){error_=Error::Read;return false;}return true;}
bool Collector::collect(Captured&out){
 out={};error_=Error::None;if(busy_){error_=Error::Busy;return false;}
 busy_=true;struct Reset{bool&b;~Reset(){b=false;}}reset{busy_};
 auto fail=[&](Error e){error_=e;return false;};
 try{
 const auto&e=expected_;const auto&i=e.identity;
 const bool turn=e.program&&e.program->kind()==story_native_event_program::Kind::TurnCamera;
 const uint32_t extent=e.program?e.program->bytes():0x520;
 if((e.program&&e.program->frames()!=e.frames)||(turn&&!config_.clock))return fail(Error::Configuration);
 if(!read_||!actor_||!allocation_||!camera_||!guards_||e.room_number>63||i.manager!=Play+0x52C4||i.bytes!=extent||i.image&15||config_.stamp&15||!span(i.image,i.bytes,true)||!span(config_.stamp,64,true)||
    (uint64_t(config_.stamp)<uint64_t(i.image)+i.bytes&&uint64_t(i.image)<uint64_t(config_.stamp)+64))return fail(Error::Configuration);
 Guards guards;if(!guards_(guards)||!guards.exclusive||guards.foreign||!allocation_(i.image,i.bytes,config_.stamp,i.generation))return fail(Error::Ownership);
 Clock before;
 if(turn){
  if(!config_.clock(before)||before.identity!=i||!before.logic_step||before.elapsed_ticks>=e.program->watchdog()||
    (last_clock_&&(before.logic_step<last_clock_->logic_step||before.serial<last_clock_->serial||before.elapsed_ticks<last_clock_->elapsed_ticks)))return fail(Error::Clock);
 }
 std::array<uint8_t,64>stamp{};if(!bytes(config_.stamp,stamp,true))return false;
 if(stamp!=make_stamp(e))return fail(Error::Stamp);
 std::array<uint8_t,4>scalar{};if(!bytes(PlayInitialized,scalar)||!word(scalar))return fail(Error::Stage);
 Captured c;c.identity=i;c.manager_depth=guards.manager_depth;c.camera_depth=guards.camera_depth;c.npc_depth=guards.npc_depth;
 if(!bytes(StageProcess,scalar))return false;c.identity.stage=word(scalar);
 if(!bytes(RoomProcesses+e.room_number*0x22C,scalar))return false;c.identity.room=word(scalar);
 if(!bytes(Play+0x5134,c.stage_name))return false;
 if(c.identity.stage!=i.stage||c.identity.room!=i.room||c.stage_name!=e.stage_name)return fail(Error::Stage);
 c.resource_generation_matches=true;
 if(!bytes(Play+0x5B34,scalar))return false;const uint32_t link=word(scalar);
 std::array<uint8_t,0x328>link_bytes{};if(!bytes(link,link_bytes))return false;
 if(half(link_bytes,14)!=0xA8||!word(link_bytes,4)||word(link_bytes,4)==0xFFFFFFFFu||link_bytes[0x326]!=e.room_number)return fail(Error::Link);
 c.room_number=link_bytes[0x326];
 std::array<uint8_t,0x540>manager{};if(!bytes(i.manager,manager))return false;
 if(!std::equal(config_.manager_exception.begin(),config_.manager_exception.end(),manager.begin()+0x24)||!std::equal(config_.manager_goal.begin(),config_.manager_goal.end(),manager.begin()+0x30))return fail(Error::Manager);
 for(unsigned n=0;n<8;++n)c.manager[n]=word(manager,n*4);c.camera_play=word(manager,0x20);c.current_event=word(manager,0x53C);std::copy_n(manager.begin()+0x3C,1280,c.flags.begin());
 if(!bytes(i.image,std::span(image_).first(extent),true))return false;c.image=std::span(image_).first(extent);
 if(turn&&!before.serial){const auto state=word(c.image,0xE4);if((state!=0&&state!=1)||before.elapsed_ticks||
   (last_clock_&&before.logic_step!=last_clock_->logic_step))return fail(Error::Clock);
  const auto pristine=e.program->pristine();for(unsigned s=0;s<4;++s)for(unsigned p=0x38;p<0x48;++p)
   if(c.image[0xF0+s*0x50+p]!=pristine[0xF0+s*0x50+p])return fail(Error::Clock);
 }
 // Fixed People profile proves class extent and layout before any cut/subtype read.
 std::array<uint8_t,0x30>people_profile{};if(!bytes(PeopleProfile,people_profile))return false;
 if(half(people_profile,8)!=0x175||word(people_profile,0x10)!=0x920)return fail(Error::Actor);
 for(unsigned n=0;n<2;++n){const auto&want=e.actors[n];std::array<uint8_t,0x920>actor{};if(!bytes(want.address,actor))return false;
  Actor a;a.address=want.address;a.pid=word(actor,4);a.token=word(stamp,0x34+n*4);a.profile=half(actor,14);a.subtype=actor[0x2DD];std::copy_n(actor.begin()+0x3E0,a.cut.size(),a.cut.begin());a.fingerprint=want.fingerprint;
  if(want.profile!=0x175||want.subtype!=1||a.pid!=want.pid||a.profile!=0x175||a.subtype!=1||a.token!=want.token||word(actor,0x10)!=PeopleProfile||word(actor,0xB0)!=0x00FF0000||actor[0x905]!=1||actor[0x326]!=e.room_number||half(actor,0xF8)!=0||!actor_(a,i))return fail(Error::Actor);
  c.actors[n]=a;
 }
 if(!bytes(Play+0x5AF8,scalar))return false;const uint32_t process=word(scalar);
 if(!span(process,0xB28,false)||uint64_t(process)+0x248!=e.camera.address)return fail(Error::Camera);
 std::array<uint8_t,0xB28>camera{};if(!bytes(process,camera))return false;
 const uint32_t profile=word(camera,0x10);const uint16_t name=half(camera,14);
 if((name!=0x1DC&&name!=0x1DD)||profile!=(name==0x1DC?CameraProfile0:CameraProfile1)||word(camera,4)!=e.camera.pid||!word(camera,4)||word(camera,4)==0xFFFFFFFFu||word(camera,0x228)!=CameraMethods||word(camera,0x248)!=process)return fail(Error::Camera);
 std::array<uint8_t,0x40>camera_profile{};std::array<uint8_t,20>methods{};if(!bytes(profile,camera_profile)||!bytes(CameraMethods,methods))return false;
 if(half(camera_profile,8)!=name||word(camera_profile,0x10)!=0xB28||word(camera_profile,0x3C)!=CameraMethods)return fail(Error::Camera);
 for(unsigned n=0;n<5;++n)if(word(methods,n*4)!=CameraFunctions[n])return fail(Error::Camera);
 c.camera.address=process+0x248;c.camera.pid=word(camera,4);const size_t body=0x248;
 c.camera.flags=word(camera,body+0x510);c.camera.timer=word(camera,body+0x384);
 c.camera.current_type=word(camera,body+0x51C);c.camera.next_type=word(camera,body+0x520);c.camera.event_type=word(camera,body+0x8B4);c.camera.style=word(camera,body+0x514);c.camera.bg=word(camera,body+0x68);c.camera.trim=word(camera,body+0x600);
 if(!bytes(TypeCount,scalar))return false;c.camera.type_table=TypeTable;c.camera.type_count=word(scalar);
 if(!c.camera.type_count||c.camera.type_count>4096||!span(TypeTable,size_t(c.camera.type_count)*0x40,false))return fail(Error::Camera);
 if(!bytes(StyleCount,scalar))return false;c.camera.style_table=StyleTable;c.camera.style_count=word(scalar);c.camera.param_pointer=word(camera,body+0x8A8);c.camera.param_index=word(camera,body+0x8AC);
 if(!c.camera.style_count||c.camera.style_count>4096||c.camera.style>=c.camera.style_count||c.camera.param_index!=c.camera.style||uint64_t(StyleTable)+uint64_t(c.camera.style)*0x84!=c.camera.param_pointer||!span(StyleTable,size_t(c.camera.style_count)*0x84,false))return fail(Error::Camera);
 std::array<uint8_t,0x84> style_record{};if(!bytes(c.camera.param_pointer,style_record))return false;
 c.camera.counter108=word(camera,body+0x108);c.camera.counter118=word(camera,body+0x118);c.camera.elapsed11C=word(camera,body+0x11C);
 std::copy_n(camera.begin()+body+0x3FC,c.camera.event_data.size(),c.camera.event_data.begin());
 std::copy_n(camera.begin()+body+0x518,c.camera.type_gate518.size(),c.camera.type_gate518.begin());
 std::copy_n(camera.begin()+body+0x100,c.camera.done100.size(),c.camera.done100.begin());
 std::copy_n(camera.begin()+body+0x37C,c.camera.work37C.size(),c.camera.work37C.begin());
 std::copy_n(camera.begin()+body+0xA4,c.camera.positions0A4.size(),c.camera.positions0A4.begin());
 std::copy_n(camera.begin()+body+0x3C,c.camera.view03C.size(),c.camera.view03C.begin());
 std::copy_n(camera.begin()+body+0x100,c.camera.engine100.size(),c.camera.engine100.begin());
 std::copy_n(camera.begin()+body+0x13C,c.camera.mode_phase13C.size(),c.camera.mode_phase13C.begin());
 std::copy_n(camera.begin()+body+0x10,c.camera.initial_view010.size(),c.camera.initial_view010.begin());
 if(!valid_camera_transition(e.camera,c.camera)||!camera_(e.camera,i)||!camera_(c.camera,i))return fail(Error::Camera);
 // Scalar-only owned mode3 or genuinely idle. Scope still must independently
 // own the event generation; matching native mode alone is never authority.
 std::array<uint8_t,0xF4>control{};if(!bytes(Play+0x51D0,control))return false;
 c.control_owned=control[0xC0]==0&&control[0xC2]==3&&word(control,0xC4)==0xFFFFFFFFu&&word(control,0xC8)==0xFFFFFFFFu&&word(control,0xCC)==0xFFFFFFFFu&&word(control,0xD0)==0xFFFFFFFFu&&half(control,0xD6)==0&&half(control,0xD8)==0xFFFF&&control[0xDE]==0xFF&&control[0xDF]==0xFF&&half(control,0xE8)==0;
 c.control_released=control[0xC0]==0&&control[0xC2]==0;
 c.foreign=!c.control_owned&&!c.control_released;
 if(turn){Clock after;if(!config_.clock(after)||after!=before)return fail(Error::Clock);c.elapsed_ticks=before.elapsed_ticks;last_clock_=before;}
 out=c;return true;
 }catch(...){return fail(Error::Read);}
}
}
