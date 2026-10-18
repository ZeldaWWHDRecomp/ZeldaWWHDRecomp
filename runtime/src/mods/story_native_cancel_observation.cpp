// SPDX-License-Identifier: MPL-2.0
#include "story_native_cancel_observation.h"
#include <algorithm>
#include <tuple>
namespace story_native_cancel_observation {namespace {
uint32_t word(std::span<const uint8_t>b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
void put(std::span<uint8_t>b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
bool pose(const P::Snapshot&a,const P::Snapshot&b){return a.identity==b.identity&&a.angles==b.angles&&a.shape==b.shape&&a.joint==b.joint&&a.cut==b.cut&&a.target==b.target&&a.velocity==b.velocity&&a.head_only==b.head_only&&a.noticed==b.noticed&&a.talk_ready==b.talk_ready&&a.target_mode==b.target_mode&&a.status==b.status;}
auto boundary(const C::Boundary&b){return std::tie(b.identity,b.exclusive,b.fenced,b.foreign,b.fault,b.manager_owned,b.manager_depth,b.camera_depth,b.npc_depth,b.callback_depth,b.foreign_depth,b.control_mode,b.partner1,b.partner2,b.talk_partner,b.item_partner,b.event_id,b.logic_step,b.serial);}
bool control(const C::Boundary&b,bool released){return b.control_mode==(released?0u:3u)&&b.partner1==~0u&&b.partner2==~0u&&b.talk_partner==~0u&&b.item_partner==~0u&&b.event_id==-1;}
bool raw_pose(const P::Snapshot&p,const std::array<uint8_t,0x920>&a){
 auto equal=[&](unsigned offset,const auto&bytes){return std::equal(bytes.begin(),bytes.end(),a.begin()+offset);};
 return word(a,0x44C)==p.identity.morf&&equal(0x320,p.angles)&&equal(0x328,p.shape)&&equal(0x3AC,p.joint)&&equal(0x3E0,p.cut)&&equal(0x884,p.target)&&equal(0x8F8,p.velocity)&&a[0x8D8]==p.head_only&&a[0x8FD]==p.noticed&&a[0x8FF]==p.talk_ready&&a[0x90D]==p.target_mode&&word(a,0x2E0)==p.status;
}
auto observation(const O::Captured&c){return std::tie(c.identity,c.room_number,c.stage_name,c.manager,c.camera_play,c.current_event,c.flags,c.actors,c.camera,c.foreign,c.resource_generation_matches,c.control_owned,c.control_released,c.manager_depth,c.camera_depth,c.npc_depth,c.elapsed_ticks);}
}
Validator::Validator(O::Expected e,Sources s):expected_(std::move(e)),sources_(std::move(s)){}
bool Validator::sample(Cpu*c,D::Tracker&t,const story_native_outer_return::OuterReturn&r,Sample&s){
 if(!c||!sources_.quiescence||!sources_.quiescence->current(c)||!sources_.sample||!sources_.retained||!sources_.resident||!sources_.verify_camera||!r.claimed_current(c,expected_,t)||r.expected()!=expected_||!sources_.retained(expected_))return false;
 const auto before=t.snapshot();if(!before.idle()||!before.coverage||before.state!=D::State::Draining||!before.serial)return false;
 for(unsigned n=0;n<2;++n)if(!sources_.quiescence->current(c)||!sources_.resident(expected_,n)||!sources_.quiescence->current(c))return false;
 if(!sources_.quiescence->current(c)||!sources_.sample(c,s)||!sources_.quiescence->current(c))return false;
 const auto&b=s.boundary;const auto&o=s.observation;
 if(b.identity!=expected_.identity||!b.exclusive||!b.fenced||b.foreign||b.fault||!b.manager_owned||b.manager_depth||b.camera_depth||b.npc_depth||b.callback_depth||b.foreign_depth||!b.logic_step||b.serial!=before.serial||o.identity!=expected_.identity||o.stage_name!=expected_.stage_name||o.room_number!=expected_.room_number||o.foreign||!o.resource_generation_matches||o.manager_depth||o.camera_depth||o.npc_depth||o.image.data()!=s.state.image.data()||o.image.size()!=s.state.image.size()||!expected_.program||expected_.program->kind()!=story_native_event_program::Kind::TurnCamera||!expected_.program->immutable_matches(o.image)||word(o.image,0xE4)!=0||o.elapsed_ticks>=expected_.program->watchdog())return false;
 for(unsigned n=0;n<8;++n)if(o.manager[n]!=word(s.state.manager,n*4))return false;
 if(o.camera_play!=word(s.state.manager,0x20)||o.current_event!=word(s.state.manager,0x53C)||!std::equal(o.flags.begin(),o.flags.end(),s.state.manager.begin()+0x3C))return false;
 for(unsigned n=0;n<2;++n){auto expected=expected_.actors[n];expected.cut=o.actors[n].cut;if(!raw_pose(s.poses[n],s.state.actors[n])||o.actors[n]!=expected||s.poses[n].identity.actor!=expected.address||s.poses[n].identity.pid!=expected.pid||s.poses[n].identity.token!=expected.token||s.poses[n].identity.stage!=expected_.identity.stage||s.poses[n].identity.room!=expected_.identity.room||s.poses[n].identity.generation!=expected_.identity.generation||s.poses[n].cut!=o.actors[n].cut||!std::equal(o.actors[n].cut.begin(),o.actors[n].cut.end(),s.state.actors[n].begin()+0x3E0)||s.poses[n].status!=word(s.state.actors[n],0x2E0)||!sources_.resident(expected_,n))return false;}
 const auto after=t.snapshot();if(!after.idle()||!after.coverage||after.state!=D::State::Draining||after.serial!=before.serial||!r.claimed_current(c,expected_,t)||!sources_.retained(expected_)||!sources_.quiescence->current(c)||!sources_.verify_camera(expected_,o.camera)||!sources_.quiescence->current(c))return false;
 const auto final=t.snapshot();return final.idle()&&final.coverage&&final.state==D::State::Draining&&final.serial==before.serial&&r.claimed_current(c,expected_,t);
}
bool Validator::stable(const Sample&a,const Sample&b)const{return a.state.manager==b.state.manager&&a.state.image==b.state.image&&a.state.actors==b.state.actors&&observation(a.observation)==observation(b.observation)&&boundary(a.boundary)==boundary(b.boundary)&&pose(a.poses[0],b.poses[0])&&pose(a.poses[1],b.poses[1]);}
bool Validator::restored_actors(const Sample&s,const Result&r)const{
 const auto&native=r.returned_.cancel().poststate();
 for(unsigned n=0;n<2;++n){auto target=r.baseline_[n];target.status=(word(native.actors[n],0x2E0)&~0x9800u)|(target.status&0x9800u);if(!pose(s.poses[n],target))return false;auto actor=native.actors[n];P::Plan plan;if(!P::make(target,s.poses[n].identity,true,true,true,true,plan))return false;for(const auto&p:plan.patches){const auto offset=p.address-expected_.actors[n].address;if(uint64_t(offset)+p.bytes.size()>actor.size())return false;std::copy(p.bytes.begin(),p.bytes.end(),actor.begin()+offset);}put(actor,0x2E0,target.status);if(actor!=s.state.actors[n])return false;}
 return true;
}
bool Validator::exact_partial(const Sample&s,const Result&r,bool detached)const{
 const auto&native=r.returned_.cancel().poststate();
 if(s.state.image!=native.image||s.state.manager!=native.manager||!control(s.boundary,false)||!s.observation.control_owned||s.observation.control_released)return false;
 if(!detached){for(unsigned n=0;n<2;++n)if(s.poses[n].identity!=r.baseline_[n].identity)return false;return s.state.actors==native.actors&&s.observation.camera==r.returned_.camera();}
 return s.observation.camera==expected_.camera&&restored_actors(s,r);
}
Result Validator::finish(Phase p,const Sample&s,const B::Completion&b,const story_native_outer_return::OuterReturn&outer)const{Result r;r.valid_=true;r.phase_=p;r.expected_=expected_;r.returned_=b;r.binding_=outer.binding();r.baseline_=b.cancel().baseline();r.boundary_=s.boundary;r.step_=s.boundary.logic_step;r.serial_=s.boundary.serial;r.root_=b.after().outer_serial;return r;}
Result Validator::closed(Cpu*c,D::Tracker&t,const story_native_outer_return::OuterReturn&outer,const B::Completion&returned){
 try{if(!returned.matches(expected_)||!returned.cancel().matches(expected_)||outer.root_serial()!=returned.after().outer_serial||returned.before().outer_serial!=returned.after().outer_serial)return {};
 Sample a,b;if(!sample(c,t,outer,a)||a.boundary.logic_step!=returned.after().boundary.logic_step||a.boundary.serial<returned.after().boundary.depth.serial)return {};
 auto result=finish(Phase::Closed,a,returned,outer);if(!exact_partial(a,result,false)||!sample(c,t,outer,b)||!stable(a,b))return {};return result;}catch(...){return {};}
}
Result Validator::detached(Cpu*c,D::Tracker&t,const story_native_outer_return::OuterReturn&outer,const Result&closed,const R::Completion&refresh){
 try{if(!closed.current_binding(c,t,outer)||!closed.matches(expected_)||closed.phase()!=Phase::Closed||outer.root_serial()!=closed.root_)return {};Sample a,b;if(!sample(c,t,outer,a)||a.boundary.logic_step!=closed.step_||a.boundary.serial!=closed.serial_||!exact_partial(a,closed,true)||!refresh.matches(expected_,[&]{auto targets=closed.baseline_;for(unsigned n=0;n<2;++n)targets[n].status=(word(closed.returned_.cancel().poststate().actors[n],0x2E0)&~0x9800u)|(targets[n].status&0x9800u);return targets;}(),a.boundary)||!sample(c,t,outer,b)||!stable(a,b))return {};return finish(Phase::Detached,a,closed.returned_,outer);}catch(...){return {};}
}
Result Validator::released(Cpu*c,D::Tracker&t,const story_native_outer_return::OuterReturn&outer,const Result&detached){
 try{if(!detached.current_binding(c,t,outer)||!detached.matches(expected_)||detached.phase()!=Phase::Detached||outer.root_serial()!=detached.root_)return {};Sample a,b;if(!sample(c,t,outer,a)||a.boundary.logic_step!=detached.step_||a.boundary.serial!=detached.serial_||!control(a.boundary,true)||a.observation.control_owned||!a.observation.control_released||a.observation.manager!=expected_.retail_manager||a.observation.flags!=expected_.retail_flags||a.observation.camera_play!=expected_.retail_camera_play||a.observation.current_event!=expected_.retail_current_event||a.observation.camera!=expected_.camera||a.state.image!=detached.returned_.cancel().poststate().image)return {};
 // Every other manager byte remains exactly the native cancellation poststate.
 auto manager=detached.returned_.cancel().poststate().manager;for(unsigned n=0;n<8;++n)put(manager,n*4,expected_.retail_manager[n]);put(manager,0x20,expected_.retail_camera_play);put(manager,0x53C,expected_.retail_current_event);std::copy(expected_.retail_flags.begin(),expected_.retail_flags.end(),manager.begin()+0x3C);if(manager!=a.state.manager)return {};
 if(!restored_actors(a,detached))return {};for(unsigned n=0;n<2;++n)if(a.observation.actors[n]!=expected_.actors[n])return {};
 if(!sample(c,t,outer,b)||!stable(a,b))return {};return finish(Phase::Released,a,detached.returned_,outer);}catch(...){return {};}
}
}
