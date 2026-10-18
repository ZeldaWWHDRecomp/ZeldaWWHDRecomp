// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_memory.h"
#include "story_native_event_image.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
#include <utility>
namespace story_native_event_memory {namespace {
const uint32_t Play=GD(0x1046F0B0),Initialized=GD(0x104753A8),Stage=GD(0x1047E6C4),Rooms=GD(0x1047E8F0);
const uint32_t People=GD(0x101C4CD0),Camera0=GD(0x101D55F0),Camera1=GD(0x101D5634),Methods=GD(0x101D5538);
const uint32_t TypeCount=GD(0x10049348),Types=GD(0x10049350),StyleCount=GD(0x10044878),Styles=GD(0x1004487C);
bool span(Range r,bool runtime){uint32_t lo=runtime?mem::kRuntimeStart:mem::kMem2Start,hi=runtime?mem::kHostStart:mem::kMem2End;return !(r.address&3)&&r.bytes&&r.address>=lo&&uint64_t(r.address)+r.bytes<=hi;}
bool contains(Range r,uint32_t a,size_t n){return n&&a>=r.address&&uint64_t(a)+n<=uint64_t(r.address)+r.bytes;}
bool overlaps(Range a,Range b){return uint64_t(a.address)<uint64_t(b.address)+b.bytes&&uint64_t(b.address)<uint64_t(a.address)+a.bytes;}
uint32_t word(std::span<const uint8_t>b){return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];}
}
Policy::Policy(Expected e,Config c,Query q,VerifyAllocation a,Read r,Write w):expected_(std::move(e)),config_(c),query_(std::move(q)),allocation_(std::move(a)),read_(std::move(r)),write_(std::move(w)){
 const auto&i=expected_.identity;const Range image{i.image,i.bytes},stamp{c.stamp,64};
 if(!query_||!allocation_||!read_||!write_||i.manager!=Play+0x52C4||(expected_.program?(i.bytes!=expected_.program->bytes()||expected_.frames!=expected_.program->frames()):i.bytes!=0x520)||(i.image&15)||(c.stamp&15)||(c.record.address&15)||c.record.bytes<20||c.record.bytes>(expected_.program&&expected_.program->kind()==story_native_event_program::Kind::TurnCamera?16384u:4096u)||!i.generation||!i.stage||!i.room||!i.program||!expected_.frames||expected_.frames>32767||expected_.room_number>63||!span(image,true)||!span(stamp,true)||!span(c.record,true)||overlaps(image,stamp)||overlaps(image,c.record)||overlaps(stamp,c.record)||!span({c.link,0x328},false)||!c.link_pid||c.link_pid==0xFFFFFFFFu||expected_.camera.address<0x248||!span({expected_.camera.address-0x248,0xB28},false))return;
 for(const auto&actor:expected_.actors)if(actor.profile!=0x175||actor.subtype!=1||!actor.pid||actor.pid==0xFFFFFFFFu||!actor.token||!span({actor.address,0x920},false))return;
 if(expected_.actors[0].address==expected_.actors[1].address||expected_.actors[0].pid==expected_.actors[1].pid||expected_.actors[0].token==expected_.actors[1].token)return;
 auto add=[&](Range r){if(count_==reads_.size()||(!span(r,false)&&!span(r,true)))return false;reads_[count_++]=r;return true;};
 const std::array<Range,23>ranges={image,stamp,c.record,Range{expected_.actors[0].address,0x920},Range{expected_.actors[1].address,0x920},Range{expected_.camera.address-0x248,0xB28},Range{c.link,0x328},Range{Initialized,4},Range{Stage,4},Range{Rooms+expected_.room_number*0x22C,4},Range{Play+0x5134,8},Range{Play+0x5B34,4},Range{Play+0x5AF8,4},Range{Play+0x51D0,0xF4},Range{i.manager,0x540},Range{People,0x30},Range{Camera0,0x40},Range{Camera1,0x40},Range{Methods,20},Range{TypeCount,4},Range{Types,63*0x40},Range{StyleCount,4},Range{Styles,145*0x84}};
 for(auto range:ranges)if(!add(range))return;configured_=true;
}
bool Policy::fail(Error e){error_=e;phase_=Phase::Quarantined;return false;}
bool Policy::scope(Scope&s,bool writing){
 if(!configured_)return fail(Error::Configuration);
 if(!query_(s)||!s.exclusive||!s.bindings_verified||s.foreign||s.fault||s.identity!=expected_.identity||s.link!=config_.link||s.link_pid!=config_.link_pid||!allocation_(expected_.identity,config_))return fail(Error::Ownership);
 if(writing&&(!s.fenced||s.manager_depth||s.camera_depth||s.npc_depth||s.callback_depth||s.foreign_depth))return fail(Error::Ownership);
 return true;
}
bool Policy::transition(Phase next){
 if(phase_==Phase::Quarantined||phase_==Phase::Closed)return false;
 const bool edge=(phase_==Phase::Idle&&next==Phase::Preparing)||(phase_==Phase::Preparing&&(next==Phase::Publishing||next==Phase::Rollback))||(phase_==Phase::Publishing&&(next==Phase::Running||next==Phase::Rollback))||(phase_==Phase::Rollback&&next==Phase::Closed)||(phase_==Phase::Running&&next==Phase::Draining)||(phase_==Phase::Draining&&next==Phase::Restoring)||(phase_==Phase::Restoring&&next==Phase::Closed);
 if(!edge)return fail(Error::Phase);
 try{
 Scope s;if(!scope(s,next!=Phase::Running))return false;
 if(next==Phase::Running){if(s.fenced||s.manager_depth||s.camera_depth||s.npc_depth||s.callback_depth||s.foreign_depth)return fail(Error::Ownership);}
 if((phase_==Phase::Preparing||phase_==Phase::Publishing||phase_==Phase::Rollback||phase_==Phase::Draining||phase_==Phase::Restoring)&&next!=Phase::Running&&(s.logic_step!=logic_step_||s.serial!=serial_))return fail(Error::Phase);
 if(next==Phase::Preparing){
  if(!read_(expected_.identity.manager-0xF4,control_before_))return fail(Error::Read);
  if(control_before_[0xC0]||control_before_[0xC2])return fail(Error::Ownership);
  logic_step_=s.logic_step;serial_=s.serial;
 }
 if(next==Phase::Draining){logic_step_=s.logic_step;serial_=s.serial;}
 phase_=next;error_=Error::None;return true;
 }catch(...){return fail(Error::Ownership);}
}
bool Policy::read(uint32_t a,std::span<uint8_t>b){
 try{Scope s;if(!scope(s,false))return false;
 if((a&3)||!b.size()||b.size()>19140||uint64_t(a)+b.size()>0x100000000ull)return fail(Error::Span);
 bool allowed=false;for(unsigned n=0;n<count_;++n)allowed|=contains(reads_[n],a,b.size());if(!allowed)return fail(Error::Span);
 if(!read_(a,b))return fail(Error::Read);error_=Error::None;return true;
 }catch(...){return fail(Error::Read);}
}
bool Policy::write(uint32_t a,std::span<const uint8_t>b){
 if(phase_==Phase::Quarantined||phase_==Phase::Closed)return false;
 try{Scope s;if(!scope(s,true))return false;
 if((a&3)||!b.size()||uint64_t(a)+b.size()>0x100000000ull)return fail(Error::Span);
 const bool early=phase_==Phase::Preparing||phase_==Phase::Publishing||phase_==Phase::Rollback;
 const bool late=phase_==Phase::Draining||phase_==Phase::Restoring;
 if((early||late)&&(s.logic_step!=logic_step_||s.serial!=serial_))return fail(Error::Phase);
 bool allowed=false;
 if(phase_==Phase::Preparing&&a==expected_.identity.image&&b.size()==expected_.identity.bytes){auto canonical=expected_.program?std::vector<uint8_t>(expected_.program->pristine().begin(),expected_.program->pristine().end()):story_native_event_image::make_wait_fixture(expected_.frames);if(!std::equal(b.begin(),b.end(),canonical.begin(),canonical.end()))return fail(Error::Payload);allowed=true;}
 if(phase_==Phase::Rollback&&a==expected_.identity.manager-0xF4&&b.size()==control_before_.size()){if(!std::equal(b.begin(),b.end(),control_before_.begin()))return fail(Error::Payload);allowed=true;}
 const uint32_t manager=expected_.identity.manager;
 if((phase_==Phase::Publishing||phase_==Phase::Rollback||phase_==Phase::Restoring)&&b.size()==4){
  uint32_t desired=0;bool field=false;
  if(a>=manager&&a<manager+0x20){const unsigned n=(a-manager)/4;std::array<uint32_t,8>published={expected_.identity.image,expected_.identity.image+0x40,expected_.identity.image+0xF0,expected_.identity.image+0x230,expected_.identity.image+0x410,0,expected_.identity.image+0x510,0};if(expected_.program){const auto offsets=expected_.program->pool_offsets();for(unsigned k=0;k<8;++k)published[k]=offsets[k]?expected_.identity.image+offsets[k]:k?0:expected_.identity.image;}desired=phase_==Phase::Publishing?published[n]:expected_.retail_manager[n];field=true;}
  if(a==manager+0x20){desired=phase_==Phase::Publishing?0:expected_.retail_camera_play;field=true;}
  if(a==manager+0x53C){desired=phase_==Phase::Publishing?0:expected_.retail_current_event;field=true;}
  if(field){if(word(b)!=desired)return fail(Error::Payload);allowed=true;}
 }
 if((phase_==Phase::Rollback||phase_==Phase::Restoring)&&a==manager+0x3C&&b.size()==expected_.retail_flags.size()){if(!std::equal(b.begin(),b.end(),expected_.retail_flags.begin()))return fail(Error::Payload);allowed=true;}
 if(!allowed)return fail(Error::Phase);
 if(!write_(a,b))return fail(Error::Write);error_=Error::None;return true;
 }catch(...){return fail(Error::Write);}
}
}
