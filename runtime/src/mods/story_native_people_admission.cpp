// SPDX-License-Identifier: MPL-2.0
#include "story_native_people_admission.h"
#include "story_actors.h"
#include "story_native_event_image.h"
#include <algorithm>
namespace story_native_people_admission {namespace {
uint32_t word(std::span<const uint8_t>b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
}
bool Adapter::owned(const Actor&a,const story_native_event_lease::Identity&i)const{
 if(i!=expected_.identity)return false;
 for(unsigned n=0;n<2;++n){const auto&w=expected_.actors[n];if(a.address!=w.address)continue;
  story_actors::ResidentEventIdentity identity;
  return a.pid==w.pid&&a.token==w.token&&a.profile==0x175&&a.subtype==1&&
   story_actors::resident_event_identity(references_[n].mod,references_[n].id,a.address,a.pid,identity)&&
   identity.actor==a.address&&identity.pid==a.pid&&identity.token==a.token&&identity.subtype==1&&identity.room==expected_.room_number&&
   identity.stage_pid==i.stage&&identity.room_pid==i.room&&identity.fingerprint==w.fingerprint;
 }return false;
}
bool Adapter::lease(const story_native_event_lease::Identity&i)const{
 return i==expected_.identity&&sources_.lease&&sources_.lease(i)&&owned(expected_.actors[0],i)&&owned(expected_.actors[1],i);
}
bool Adapter::action(const story_native_people_events::Binding&binding,uint32_t&out)const{
 out=0xFFFFFFFFu;if(binding.staff<0||binding.staff>1||binding.lease!=expected_.identity||binding.name!=expected_.identity.image+0xF0+uint32_t(binding.staff)*0x50||!owned(binding.actor,binding.lease)||!lease(binding.lease))return false;
 std::vector<uint8_t>image(expected_.identity.bytes);if(!sources_.read(expected_.identity.image,image))return false;
 const auto state=word(image,0xE4);if(state!=2&&state!=4)return false;
 auto canonical=expected_.program?std::vector<uint8_t>(expected_.program->pristine().begin(),expected_.program->pristine().end()):story_native_event_image::make_wait_fixture(expected_.frames);
 for(unsigned p=0;p<image.size();++p){bool mutable_byte=p>=0xE4&&p<0xE8;for(unsigned staff=0;staff<4;++staff)mutable_byte|=p>=0xF0+staff*0x50+0x38&&p<0xF0+staff*0x50+0x48;if(!mutable_byte&&image[p]!=canonical[p])return false;}
 const auto row=0xF0+uint32_t(binding.staff)*0x50;
 const auto selected=expected_.program?expected_.program->cut(unsigned(binding.staff),word(image,row+0x38)):std::optional<story_native_event_program::Cut>{story_native_event_program::Cut{unsigned(binding.staff),0,expected_.frames}};
 if(!selected||(!expected_.program&&word(image,row+0x38)!=uint32_t(binding.staff))||(word(image,row+0x3C)!=selected->action&&word(image,row+0x3C)!=0xFFFFFFFFu)||word(image,row+0x40)>>16||((uint32_t(image[row+0x42])<<8)|image[row+0x43])>selected->maximum_timer||image[row+0x44]||image[row+0x45]>1||image[row+0x46]>2||image[row+0x47]>1)return false;
 out=selected->action;return true;
}
Adapter::Adapter(Expected e,std::array<Reference,2>r,Sources s):expected_(std::move(e)),references_(std::move(r)),sources_(std::move(s)){
 if((expected_.program?(expected_.identity.bytes!=expected_.program->bytes()||expected_.frames!=expected_.program->frames()):expected_.identity.bytes!=0x520)||!expected_.frames||expected_.frames>32767||!sources_.read||!sources_.write||!sources_.invoke||!sources_.lease||!sources_.current_scope||!sources_.detach_boundary)return;
 for(unsigned n=0;n<2;++n){const auto&a=expected_.actors[n];if(references_[n].mod.empty()||references_[n].id.empty()||a.subtype!=1||std::all_of(a.fingerprint.begin(),a.fingerprint.end(),[](uint8_t v){return !v;}))return;}
 if(references_[0].mod==references_[1].mod&&references_[0].id==references_[1].id)return;
 story_native_people_events::Backend b;
 b.read=sources_.read;b.write=[this](uint32_t a,std::span<const uint8_t>bytes){if(!lease(expected_.identity))return false;for(const auto&actor:expected_.actors){if((a==actor.address+0x440&&bytes.size()==1&&bytes[0]<=1&&sources_.current_scope(actor,expected_.identity))||(a==actor.address+0x3E0&&bytes.size()==actor.cut.size()&&sources_.detach_boundary(expected_.identity)&&std::equal(bytes.begin(),bytes.end(),actor.cut.begin())))return sources_.write(a,bytes);}return false;};
 b.invoke=sources_.invoke;b.actor=[this](const Actor&a,const auto&i){return owned(a,i);};b.lease=[this](const auto&i){return lease(i);};b.action=[this](const auto&binding,uint32_t&out){return action(binding,out);};b.capabilities={true,true,false,false,false};
 router_=std::make_unique<story_native_people_events::Router>(std::move(b),expected_);
}
bool Adapter::configured()const{return router_&&router_->configured();}
story_native_people_events::Dispatch Adapter::event_move(Cpu*c,uint32_t actor){
 int index=-1;for(unsigned n=0;n<2;++n)if(expected_.actors[n].address==actor)index=int(n);
 if(index<0)return story_native_people_events::Dispatch::Foreign;
 if(!configured())return story_native_people_events::Dispatch::Refused;
 try{if(!lease(expected_.identity)||!sources_.current_scope(expected_.actors[index],expected_.identity))return story_native_people_events::Dispatch::Refused;return router_->event_move(c,actor);}catch(...){return story_native_people_events::Dispatch::Refused;}
}
bool Adapter::staff_id(Cpu*c){return configured()&&router_->staff_id(c);}
bool Adapter::detach_people(Cpu*c){try{return configured()&&lease(expected_.identity)&&sources_.detach_boundary(expected_.identity)&&router_->detach_people(c);}catch(...){return false;}}
}
