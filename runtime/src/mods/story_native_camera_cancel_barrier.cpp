// SPDX-License-Identifier: MPL-2.0
#include "story_native_camera_cancel_barrier.h"
#include "guest_addr.h"
#include "ppc.h"
namespace story_native_camera_cancel_barrier {namespace {
uint32_t word(std::span<const uint8_t>b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool same(const M::State&a,const M::State&b){return a.manager==b.manager&&a.image==b.image&&a.actors==b.actors;}
bool parent(const M::Boundary&a,const M::Boundary&b){const auto&x=a.parent;const auto&y=b.parent;return a.expected==b.expected&&a.logic_step==b.logic_step&&a.elapsed==b.elapsed&&x.entry==y.entry&&x.function==y.function&&x.receiver==y.receiver&&x.subject==y.subject&&x.pid==y.pid&&x.token==y.token&&x.identity==y.identity&&x.authority==y.authority;}
bool ordered(const Frame&a,const Frame&b){return a.outer_serial==b.outer_serial&&parent(a.boundary,b.boundary)&&a.boundary.depth.serial<=b.boundary.depth.serial;}
}
Barrier::Barrier(O::Expected e,Sources s):expected_(std::move(e)),sources_(std::move(s)){}
void Barrier::fail()noexcept{if(fault_.exchange(true))return;try{if(sources_.quarantine)sources_.quarantine();}catch(...){} }
bool Barrier::frame(Cpu*c,Frame&f){
 if(!c||!expected_.program||expected_.program->kind()!=story_native_event_program::Kind::TurnCamera||expected_.identity.bytes!=0xE60||!sources_.frame||!sources_.owned_thread||!sources_.read||!sources_.resident||!sources_.camera||!sources_.verify_camera||!sources_.original_route||!sources_.quarantine||!sources_.frame(c,f))return false;
 const auto&b=f.boundary;const auto&d=b.depth;const auto&p=b.parent;
 return f.outer_serial&&d.serial>=f.outer_serial&&b.expected==expected_&&b.logic_step&&b.elapsed<expected_.program->watchdog()&&b.retained&&b.fenced&&b.stage&&b.exclusive&&d.coverage&&!d.fault&&d.state==M::D::State::Active&&d.camera_depth==1&&!d.npc_depth&&!d.manager_depth&&!d.callback_depth&&!d.foreign_depth&&p.entry==M::D::Entry::CameraExecute&&p.function==GC(0x024FFA3C)&&p.authority==M::D::Authority::Owned&&p.identity==expected_.identity&&expected_.camera.address>=0x248&&p.receiver==expected_.camera.address-0x248&&p.subject==p.receiver&&p.pid==expected_.camera.pid&&!p.token;
}
bool Barrier::capture(M::State&s,O::Camera&c){
 if(!sources_.read(expected_.identity.manager,s.manager)||!sources_.read(expected_.identity.image,s.image))return false;
 for(unsigned n=0;n<2;++n)if(!sources_.resident(expected_,n)||!sources_.read(expected_.actors[n].address,s.actors[n])||!sources_.resident(expected_,n))return false;
 return sources_.camera(c)&&sources_.verify_camera(expected_,c);
}
bool Barrier::partial(const M::State&s)const{
 const auto&g=*expected_.program;if(!g.immutable_matches(s.image)||word(s.image,0xE4)!=2||word(s.manager,0x20)!=1||(word(s.manager,0x53C)&&word(s.manager,0x53C)!=expected_.identity.image+0x40))return false;
 const auto pools=g.pool_offsets();for(unsigned n=0;n<8;++n)if(word(s.manager,n*4)!=expected_.identity.image+pools[n])return false;
 for(unsigned p=0;p<0x500;++p)if(s.manager[0x3C+p]&~(p==3?0xFF:p==2?1:0))return false;
 const uint32_t flags=word(s.manager,0x3C);std::array<uint32_t,4>cuts{};
 for(unsigned n=0;n<4;++n){const unsigned p=0xF0+n*0x50;cuts[n]=word(s.image,p+0x38);const auto selected=g.cut(n,cuts[n]);const auto cache=word(s.image,p+0x3C);if(!selected||(cache!=~0u&&cache!=selected->action)||word(s.image,p+0x40)>selected->maximum_timer||s.image[p+0x44]||s.image[p+0x45]>1||s.image[p+0x46]>2||s.image[p+0x47]>1)return false;}
 for(unsigned n=0;n<2;++n){if((cuts[n]==n&&!(flags&(1u<<(7+n))))||((flags&(1u<<n))&&cuts[n]!=n))return false;const auto c=std::span<const uint8_t>(s.actors[n]).subspan(0x3E0,0x6C);const auto selected=g.cut(n,cuts[n]);const uint32_t action=word(c,0x10),staff=word(c,4),cache=word(s.image,0xF0+n*0x50+0x3C);if(word(c,0)!=expected_.identity.image+0xF0+n*0x50||word(c,8)!=expected_.actors[n].address||!selected||(action!=~0u&&action!=selected->action)||(staff!=n&&!(staff==word(expected_.actors[n].cut,4)&&action==~0u&&cache==~0u)))return false;}
 return !(cuts[2]==6&&!(flags&4))&&!((flags&0x40)&&cuts[2]!=6)&&!(cuts[3]>=4&&(flags&0xB)!=0xB)&&!(cuts[3]==5&&(flags&0x50)!=0x50)&&!((flags&0x20)&&cuts[3]!=5);
}
bool Barrier::camera(const M::State&s,const O::Camera&c,bool returned)const{
 O::Captured captured;captured.identity=expected_.identity;captured.image=s.image;captured.camera=c;
 if(O::diagnose_camera(expected_,captured,returned).failure!=O::CameraFailure::None)return false;
 if(returned)return c.current_type==expected_.camera.current_type&&c.next_type==expected_.camera.next_type&&word(c.event_data,0xC)==~0u;
 return c.current_type==expected_.camera.event_type&&c.next_type==expected_.camera.event_type&&word(c.event_data,0xC)==expected_.camera.current_type;
}
std::optional<Ready> Barrier::ready(Cpu*c,uint32_t fn){
 try{Frame admission;if(!c||!sources_.owned_thread||!sources_.owned_thread(c)||!frame(c,admission)||fn!=GC(0x024FE3E8)||c->r[3]!=expected_.camera.address||!sources_.original_route(c,fn))return {}; }catch(...){return {};}
 if(busy_.exchange(true))return {};struct Reset{std::atomic<bool>&b;~Reset(){b=false;}}reset{busy_};
 if(fault_||issued_||attempted_)return {};
 try{Ready r;Frame after;M::State second;O::Camera camera2;if(!frame(c,r.frame_)||!capture(r.state_,r.camera_)||!partial(r.state_)||!camera(r.state_,r.camera_,false)||!capture(second,camera2)||!same(r.state_,second)||r.camera_!=camera2||!frame(c,after)||!ordered(r.frame_,after))return {};
  r.frame_=after;r.owner_=this;ready_=r;issued_=true;return r;
 }catch(...){return {};}
}
Result Barrier::run(Cpu*c,uint32_t fn,const Ready&r,const M::Completion&cancel,const std::function<void()>&original){
 // A foreign thread cannot consume or quarantine the real thread's receipt.
 try{if(!c||!sources_.owned_thread||!sources_.owned_thread(c))return Result::Refused;}catch(...){return Result::Refused;}
 if(busy_.exchange(true)){fail();return Result::Quarantined;}struct Reset{std::atomic<bool>&b;~Reset(){b=false;}}reset{busy_};
 if(fault_)return Result::Quarantined;if(attempted_||completion_.valid())return Result::Refused;
 auto reject=[&]{if(cancel.valid()){fail();return Result::Quarantined;}return Result::Refused;};
 try{Frame before,after;M::State state,post;O::Camera camera_before,camera_after;
  if(!issued_||r.owner_!=this||ready_.owner_!=this||!cancel.matches(expected_)||!original||fn!=GC(0x024FE3E8)||c->r[3]!=expected_.camera.address||!sources_.original_route(c,fn)||!frame(c,before)||!ordered(ready_.frame_,before)||!parent(ready_.frame_.boundary,cancel.before())||!parent(cancel.before(),cancel.after())||ready_.frame_.boundary.depth.serial>cancel.before().depth.serial||cancel.after().depth.serial>before.boundary.depth.serial||!same(ready_.state_,cancel.prestate())||!capture(state,camera_before)||!same(state,cancel.poststate())||camera_before!=ready_.camera_||!camera(state,camera_before,false))return reject();
  attempted_=true;try{original();}catch(...){fail();throw;}
  if(!frame(c,after)||!ordered(before,after)||!sources_.original_route(c,fn)||!capture(post,camera_after)||!same(post,cancel.poststate())||!camera(post,camera_after,true)){fail();return Result::Quarantined;}
  for(unsigned p=0;p<camera_before.event_data.size();++p)if((p<0xC||p>=0x10)&&camera_after.event_data[p]!=camera_before.event_data[p]){fail();return Result::Quarantined;}
  Frame final;if(!frame(c,final)||!ordered(after,final)){fail();return Result::Quarantined;}
  completion_.expected_=expected_;completion_.before_=before;completion_.after_=final;completion_.camera_=camera_after;completion_.cancel_=cancel;completion_.valid_=true;return Result::Returned;
 }catch(...){fail();return Result::Quarantined;}
}
}
