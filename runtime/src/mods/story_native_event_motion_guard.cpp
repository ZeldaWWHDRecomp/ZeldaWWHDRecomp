// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_motion_guard.h"
#include <cmath>
namespace story_native_event_motion_guard {namespace {
bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::fabs(p.x)<=1000000&&std::fabs(p.y)<=1000000&&std::fabs(p.z)<=1000000;}
bool same(Point a,Point b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool address(uint32_t a,uint32_t n){return !(a&3)&&a>=0x10000000u&&a<=0x50000000u-n;}
bool identity(const Identity&i){return address(i.actor,0x920)&&i.pid&&i.pid!=0xFFFFFFFFu&&i.token&&i.stage&&i.room<64&&i.epoch&&i.lease&&i.program&&i.staff>=0&&i.staff<20&&address(i.manager,0x520)&&((i.image>=0x10000000u&&i.image<0x50000000u)||(i.image>=0x60000000u&&i.image<0x68000000u))&&!(i.image&3);}
bool bound(const Observation&o){const auto&a=o.identity;const auto&b=o.binding;const auto&n=o.native;return o.ownership_verified&&identity(a)&&n.profile==0x175&&n.parameters==0x00FF0000u&&(n.stage_argument==1||n.stage_argument==9)&&n.class_subtype==n.stage_argument&&n.ground_movement==0&&n.action==4&&b.acch==a.actor+0x450&&b.circle==a.actor+0x614&&b.position==a.actor+0x314&&b.old_position==a.actor+0x300&&b.speed==a.actor+0x33C&&b.angle==a.actor+0x320&&b.shape_angle==a.actor+0x328&&b.actor==a.actor&&b.pid==a.pid&&b.count==1&&b.radius==30&&b.circle_height==30&&!(b.flags&(1u|2u|4u|0x4000u))&&finite(o.position);}
bool same_binding(const Binding&a,const Binding&b){return a.acch==b.acch&&a.circle==b.circle&&a.position==b.position&&a.old_position==b.old_position&&a.speed==b.speed&&a.angle==b.angle&&a.shape_angle==b.shape_angle&&a.actor==b.actor&&a.pid==b.pid&&a.count==b.count&&a.radius==b.radius&&a.circle_height==b.circle_height;}
}
bool Guard::current(const Observation&o)const{return o.identity==identity_&&o.native==native_&&bound(o)&&same_binding(o.binding,binding_);}
bool Guard::within(Point p)const{return finite(p)&&p.x-binding_.radius>=limits_.min_x&&p.x+binding_.radius<=limits_.max_x&&p.z-binding_.radius>=limits_.min_z&&p.z+binding_.radius<=limits_.max_z&&std::hypot(double(p.x)-origin_.x,double(p.z)-origin_.z)<=limits_.max_step&&std::fabs(double(p.y)-origin_.y)<=limits_.max_ground_delta;}
bool Guard::support(const Support&s,Point p)const{return s.index<256&&s.registration_id==s.index&&(s.entry_used&1)&&address(s.poly_bgw,0xBC)&&s.poly_bgw==s.entry_bgw&&s.poly_pid==s.entry_pid&&s.room==identity_.room&&!(s.move_flags&1)&&!s.ride_callback&&std::isfinite(s.height)&&std::fabs(double(s.height)-origin_.y)<=limits_.max_ground_delta&&std::fabs(double(p.y)-s.height)<=limits_.max_ground_delta;}
bool Guard::begin(const Observation&o,const Support&s,Limits l,Preconditions p){
 if(phase_!=Phase::Empty||!p.owned_scope||!p.exclusive_lease||!p.ride_guard_installed||!bound(o)||!std::isfinite(l.min_x)||!std::isfinite(l.max_x)||!std::isfinite(l.min_z)||!std::isfinite(l.max_z)||l.min_x>=l.max_x||l.min_z>=l.max_z||!std::isfinite(l.max_step)||l.max_step<=0||l.max_step>50||!std::isfinite(l.max_ground_delta)||l.max_ground_delta<0||l.max_ground_delta>60)return false;
 identity_=o.identity;binding_=o.binding;native_=o.native;origin_=o.position;limits_=l;
 if(!within(origin_)||!support(s,origin_)||std::fabs(double(origin_.y)-s.height)>0.1){reset();return false;}
 phase_=Phase::Running;return true;
}
Decision Guard::cut_end(const Identity&caller,uint32_t manager,int32_t staff){
 if(phase_!=Phase::Running&&phase_!=Phase::Candidate&&phase_!=Phase::Validated&&phase_!=Phase::Aborted)return Decision::Delegate;
 if(caller.actor!=identity_.actor||caller.pid!=identity_.pid)return Decision::Delegate;
 if(phase_==Phase::Aborted)return Decision::Reject;
 if(caller!=identity_||manager!=identity_.manager||staff!=identity_.staff){abort();return Decision::Reject;}
 end_=true;return Decision::Defer;
}
Decision Guard::ride_callback(const Identity&rider){
 if(phase_!=Phase::Running&&phase_!=Phase::Candidate&&phase_!=Phase::Validated&&phase_!=Phase::Aborted)return Decision::Delegate;
 if(rider.actor!=identity_.actor||rider.pid!=identity_.pid)return Decision::Delegate;
 if(phase_==Phase::Aborted)return Decision::Reject;
 if(rider!=identity_){abort();return Decision::Reject;}
 return Decision::Suppress;
}
bool Guard::candidate(const Observation&o,const Support&s){if(phase_!=Phase::Running||!current(o)||!within(o.position)||!support(s,o.position)){abort();return false;}phase_=Phase::Candidate;return true;}
bool Guard::accept(const Observation&o,const Support&s){if(phase_!=Phase::Candidate||!current(o)||!within(o.position)||!support(s,o.position)||!(o.binding.flags&0x20)||std::fabs(double(o.position.y)-s.height)>0.1){abort();return false;}validated_=o.position;phase_=Phase::Validated;return true;}
Completion Guard::commit(const Observation&o){if(phase_!=Phase::Validated||!current(o)||!same(o.position,validated_)){abort();return{};}Completion result{true,end_,identity_.manager,identity_.staff};phase_=Phase::Committed;end_=false;return result;}
void Guard::abort(){phase_=Phase::Aborted;end_=false;}
void Guard::reset(){identity_={};binding_={};native_={};origin_={};validated_={};limits_={};phase_=Phase::Empty;end_=false;}
}
