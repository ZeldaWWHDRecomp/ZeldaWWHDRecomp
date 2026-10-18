// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_actor_cast.h"
#include "story_native_event_image.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
namespace story_native_wait_actor_cast {namespace {
uint32_t word(std::span<const uint8_t>b,unsigned p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool range(uint32_t a,size_t n){return !(a&3)&&n&&((a>=mem::kMem2Start&&uint64_t(a)+n<=mem::kMem2End)||(a>=mem::kRuntimeStart&&uint64_t(a)+n<=mem::kHostStart));}
}
thread_local Cast*Cast::current_=nullptr;
Cast::Cast(Expected e,D::Tracker&d,Sources s):expected_(std::move(e)),depth_(d),sources_(std::move(s)){
 configured_=sources_.read&&sources_.retained&&sources_.owned_thread&&sources_.resident&&sources_.queue_cancel&&expected_.identity.bytes==(expected_.program?expected_.program->bytes():0x520)&&(!expected_.program||expected_.program->frames()==expected_.frames)&&expected_.frames&&expected_.frames<=32767&&expected_.identity.generation&&expected_.identity.program;
 for(const auto&a:expected_.actors)configured_&=a.address&&a.pid&&a.token&&a.profile==0x175&&a.subtype==1;
 configured_&=expected_.actors[0].address!=expected_.actors[1].address&&expected_.actors[0].pid!=expected_.actors[1].pid&&expected_.actors[0].token!=expected_.actors[1].token;
}
void Cast::fail()noexcept{fault_=true;if(cancelled_.exchange(true))return;try{if(sources_.queue_cancel)sources_.queue_cancel();}catch(...){} }
bool Cast::retained()noexcept{try{if(sources_.retained)return sources_.retained(expected_);}catch(...){fail();return true;}fail();return true;}
bool Cast::thread(const Cpu*c)const noexcept{try{return c&&sources_.owned_thread&&sources_.owned_thread(c);}catch(...){return false;}}
bool Cast::parent()const{const auto p=depth_.current_owned_call();return p&&p->entry==D::Entry::ManagerMain&&p->identity==expected_.identity&&p->receiver==expected_.identity.manager&&p->subject==expected_.identity.manager&&!p->pid&&!p->token;}
bool Cast::actor(unsigned n)const{return n<2&&sources_.resident&&sources_.resident(expected_.actors[n],expected_.identity);}
bool Cast::manager()const{
 std::array<uint8_t,0x20>b{};if(!range(expected_.identity.manager,b.size())||!sources_.read(expected_.identity.manager,b))return false;
 const auto base=expected_.identity.image;std::array<uint32_t,8>p{base,base+0x40,base+0xF0,base+0x230,base+0x410,0,base+0x510,0};
 if(expected_.program){auto offsets=expected_.program->pool_offsets();for(unsigned n=0;n<8;++n)p[n]=(n==0||offsets[n])?base+offsets[n]:0;}
 for(unsigned n=0;n<8;++n)if(word(b,n*4)!=p[n])return false;return true;
}
bool Cast::image(uint32_t state,bool pristine)const{
 const size_t extent=expected_.program?expected_.program->bytes():0x520;
 std::array<uint8_t,story_native_turn_camera_image::kImageBytes>storage{};auto b=std::span(storage).first(extent);
 if(!range(expected_.identity.image,b.size())||!sources_.read(expected_.identity.image,b)||word(b,0xE4)!=state)return false;
 const auto canonical=expected_.program?std::vector<uint8_t>(expected_.program->pristine().begin(),expected_.program->pristine().end()):story_native_event_image::make_wait_fixture(expected_.frames);
 for(unsigned p=0;p<b.size();++p){bool mutable_byte=p>=0xE4&&p<0xE8;
  for(unsigned n=0;n<4;++n)mutable_byte|=p>=0xF0+n*0x50+0x38&&p<0xF0+n*0x50+0x48;
  if(!mutable_byte&&b[p]!=canonical[p])return false;
 }
 if(pristine)for(unsigned n=0;n<4;++n)for(unsigned p=0x38;p<0x48;++p)if(b[0xF0+n*0x50+p]!=canonical[0xF0+n*0x50+p])return false;
 if(expected_.program&&expected_.program->kind()==story_native_event_program::Kind::TurnCamera&&!pristine){
  for(unsigned n=0;n<4;++n){const auto current=word(b,0xF0+n*0x50+0x38),cache=word(b,0xF0+n*0x50+0x3C);
   if((state==2&&current!=expected_.program->initial_cut(n))||
      (state==1&&current!=0xFFFFFFFFu&&current!=expected_.program->initial_cut(n))||cache!=0xFFFFFFFFu)return false;
  }
 }
 return true;
}
bool Cast::cleared_flags()const{
 std::array<uint8_t,0x500>b{};
 return range(expected_.identity.manager+0x3C,b.size())&&sources_.read(expected_.identity.manager+0x3C,b)&&std::all_of(b.begin(),b.end(),[](uint8_t v){return v==0;});
}
Decision Cast::start(Cpu*c,const Body&next){
 if(!c||!next){fail();return Decision::Handled;}
 if(!retained())return Decision::Delegate;
 const auto reject=[&]{fail();return Decision::Handled;};
 try{
  if(!configured_||fault_||!thread(c)||current_||cpu_||!parent()||c->r[3]!=expected_.identity.manager||c->r[4]!=expected_.identity.image+0x40||
     (c->r[1]&7)||c->r[1]<0x40||!range(c->r[1]-0x40,0x40)||!manager()||!image(1,true))return reject();
  for(unsigned n=0;n<2;++n){std::array<uint8_t,4>b{};if(!actor(n)||!sources_.read(expected_.actors[n].address+0x2E0,b))return reject();status_[n]=word(b);if(status_[n]&0x8000)return reject();}
  seen_={};cpu_=c;param_=c->r[1]-0x38;current_=this;
  struct Reset{Cast&c;~Reset(){current_=nullptr;c.cpu_=nullptr;c.param_=0;}}reset{*this};
  next();
  if(fault_||!retained()||!thread(c)||!parent()||!manager()||!image(2)||!seen_[0]||!seen_[1])return reject();
  for(unsigned n=0;n<2;++n){std::array<uint8_t,4>b{};if(!actor(n)||!sources_.read(expected_.actors[n].address+0x2E0,b)||word(b)!=(status_[n]|0x8000u))return reject();}
  if(!cleared_flags()||!retained())return reject();
  initialized_.store(true);
  return Decision::Handled;
 }catch(...){fail();throw;}
}
Decision Cast::candidate(Cpu*c){
 if(!c){fail();return Decision::Handled;}
 if(!retained())return Decision::Delegate;
 const auto reject=[&]{fail();c->r[3]=0;return Decision::Handled;};
 try{
  std::array<uint8_t,16>param{};
  // A callback inside our actual startProc must use its exact native local.
  if(current_==this&&(cpu_!=c||c->r[4]!=param_))return reject();
  if(!thread(c)||!range(c->r[4],param.size())||!sources_.read(c->r[4],param))return reject();
  const uint32_t name=word(param);int index=-1;
  for(unsigned n=0;n<2;++n)if(name==expected_.identity.image+0xF0+n*0x50)index=int(n);
  if(index<0){
   if(name>=expected_.identity.image&&uint64_t(name)<uint64_t(expected_.identity.image)+expected_.identity.bytes)return reject();
   // Ordinary names/candidates preserve the pre-existing owner exactly once.
   return Decision::Delegate;
  }
  if(!configured_||fault_||current_!=this||cpu_!=c||c->r[4]!=param_||!parent()||!manager()||!image(1)||
     word(param,4)||word(param,8)||param[0xC]||param[0xD])return reject();
  // Native parameter padding E/F is uninitialized and is not authority.
  const auto n=unsigned(index);if(!actor(n))return reject();
  if(c->r[3]!=expected_.actors[n].address){c->r[3]=0;return Decision::Handled;}
  if(seen_[n])return reject();
  std::array<uint8_t,4>status{};if(!sources_.read(c->r[3]+0x2E0,status)||word(status)!=status_[n])return reject();
  if(!retained()||!actor(n))return reject();seen_[n]=true;c->r[3]=expected_.actors[n].address;return Decision::Handled;
 }catch(...){fail();c->r[3]=0;throw;}
}
}
