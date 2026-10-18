// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_runtime_cancel_consumers.h"
#include "story_native_camera_capture.h"
#include "story_native_refresh_stack.h"
#include "guest_addr.h"
#include "runtime.h"
#include "ppc.h"
#include <algorithm>
#include <thread>
#include <cstring>
#include <stdexcept>
namespace story_native_wait_runtime_cancel_consumers {namespace {
namespace P=story_native_people_pose_capture;namespace C=story_native_camera_capture;
const P::Layout People{GD(0x101C4CD0),GD(0x100209C4)};
const C::Layout Camera{GD(0x1046F0B0),GD(0x101D55F0),GD(0x101D5634),GD(0x101D5538),
 {GC(0x025021A4),GC(0x02502288),GC(0x024FFA3C),GC(0x0250235C),GC(0x024FFC40)},
 {GD(0x10049348),GD(0x10049350),GD(0x10044878),GD(0x1004487C)}};
uint32_t word(std::span<const uint8_t>b,unsigned p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
uint16_t half(std::span<const uint8_t>b,unsigned p){return uint16_t(b[p])<<8|b[p+1];}
bool pose(const X::P::Snapshot&a,const X::P::Snapshot&b){return a.identity==b.identity&&a.angles==b.angles&&a.shape==b.shape&&a.joint==b.joint&&a.cut==b.cut&&a.target==b.target&&a.velocity==b.velocity&&a.head_only==b.head_only&&a.noticed==b.noticed&&a.talk_ready==b.talk_ready&&a.target_mode==b.target_mode&&a.status==b.status;}
bool overlap(story_native_event_invoke::Range a,story_native_event_invoke::Range b){return uint64_t(a.address)<uint64_t(b.address)+b.bytes&&uint64_t(b.address)<uint64_t(a.address)+a.bytes;}
}
struct Adapter::Impl {
 X::O::Expected expected;story_native_event_arena::Layout arena;story_native_wait_session::Session*session;X::D::Tracker*tracker;Cpu*cpu;
 const story_native_outer_return::OuterReturn*outer;G::Context*graph;G::Binding*binding;
 story_native_wait_runtime_backend::Bundle backend;X::B::Completion returned;Callbacks callbacks;
 std::optional<X::Result>closed;std::thread::id thread;story_native_cancel_consumer_write::Writer writer;bool restoring=false,attempted=false;
 bool current()const{
  if(thread!=std::this_thread::get_id()||(closed&&!closed->current_binding(cpu,*tracker,*outer))||!binding->current()||!session->fence_held()||!outer->claimed_current(cpu,expected,*tracker)||!callbacks.retained(expected))return false;
  auto q=binding->quiescence();auto d=tracker->snapshot();return q&&q->current(cpu)&&d.idle()&&d.coverage&&!d.fault&&d.state==X::D::State::Draining&&outer->root_serial()==returned.after().outer_serial;
 }
 bool read(uint32_t at,std::span<uint8_t>bytes)const{return current()&&backend.sources.read(at,bytes)&&current();}
 bool resident(const X::O::Actor&a,const story_native_event_lease::Identity&i)const{return current()&&i==expected.identity&&(a==expected.actors[0]||a==expected.actors[1])&&backend.sources.actor(a,i)&&current();}
 bool camera(const X::O::Camera&c)const{return current()&&story_native_camera_policy::verify(Camera.tables,[this](auto at,auto b){return read(at,b);},c,expected.identity).valid&&current();}
 bool sample(X::Sample&out)const{
  out={};try{
   if(!current())return false;story_native_event_memory::Scope scope;if(!backend.sources.session(scope)||scope.identity!=expected.identity||!scope.exclusive||!scope.bindings_verified||!scope.fenced||scope.foreign||scope.fault||scope.serial!=tracker->snapshot().serial)return false;
   story_native_event_collect::Clock clock;if(!backend.config.collector.clock||!backend.config.collector.clock(clock)||clock.identity!=expected.identity||clock.logic_step!=scope.logic_step||clock.serial!=scope.serial||clock.logic_step!=returned.after().boundary.logic_step||clock.elapsed_ticks!=returned.after().boundary.elapsed)return false;
   X::Sample value;std::array<uint8_t,0xF4>control{},after{};
   if(!read(expected.identity.manager,value.state.manager)||!read(expected.identity.image,value.state.image)||!read(expected.identity.manager-0xF4,control))return false;
   auto&o=value.observation;o.identity=expected.identity;o.room_number=expected.room_number;o.stage_name=expected.stage_name;o.resource_generation_matches=true;o.image=value.state.image;o.elapsed_ticks=clock.elapsed_ticks;
   for(unsigned n=0;n<8;++n)o.manager[n]=word(value.state.manager,n*4);o.camera_play=word(value.state.manager,0x20);o.current_event=word(value.state.manager,0x53C);std::copy_n(value.state.manager.begin()+0x3C,0x500,o.flags.begin());
   o.control_owned=!control[0xC0]&&control[0xC2]==3&&word(control,0xC4)==~0u&&word(control,0xC8)==~0u&&word(control,0xCC)==~0u&&word(control,0xD0)==~0u&&!half(control,0xD6)&&half(control,0xD8)==0xFFFF&&control[0xDE]==0xFF&&control[0xDF]==0xFF&&!half(control,0xE8);
   o.control_released=!control[0xC0]&&!control[0xC2];o.foreign=!o.control_owned&&!o.control_released;
   auto q=binding->quiescence();if(!q)return false;
   const auto bounded=[this](auto at,auto bytes){return read(at,bytes);};const auto verify=[this](const auto&a,const auto&i){return resident(a,i);};
   for(unsigned n=0;n<2;++n){if(!read(expected.actors[n].address,value.state.actors[n]))return false;o.actors[n]=expected.actors[n];std::copy_n(value.state.actors[n].begin()+0x3E0,0x6C,o.actors[n].cut.begin());
    if(value.state.manager==returned.cancel().poststate().manager){
     if(closed){if(!story_native_cancel_pose_capture::capture(bounded,verify,People,expected,n,*closed,cpu,*tracker,*outer,*q,value.poses[n]))return false;}
     else if(!story_native_cancel_pose_capture::capture_closed(bounded,verify,People,expected,n,returned,cpu,*tracker,*outer,*q,value.poses[n]))return false;
    }else if(!closed||!P::capture(bounded,verify,People,expected,n,value.poses[n]))return false;
   }
   if(!C::capture(bounded,Camera,expected,o.camera)||!read(expected.identity.manager-0xF4,after)||after!=control)return false;
   auto&b=value.boundary;b.identity=expected.identity;b.exclusive=scope.exclusive;b.fenced=true;b.foreign=scope.foreign||o.foreign;b.fault=scope.fault;b.manager_owned=true;b.logic_step=scope.logic_step;b.serial=scope.serial;b.manager_depth=scope.manager_depth;b.camera_depth=scope.camera_depth;b.npc_depth=scope.npc_depth;b.callback_depth=scope.callback_depth;b.foreign_depth=scope.foreign_depth;b.control_mode=control[0xC2];b.partner1=word(control,0xC4);b.partner2=word(control,0xC8);b.talk_partner=word(control,0xCC);b.item_partner=word(control,0xD0);b.event_id=int16_t(half(control,0xD8));
   story_native_event_memory::Scope final;story_native_event_collect::Clock final_clock;if(!current()||!backend.sources.session(final)||final.serial!=scope.serial||final.logic_step!=scope.logic_step||!backend.config.collector.clock(final_clock)||final_clock!=clock)return false;
   out=std::move(value);out.observation.image=out.state.image;return true;
  }catch(...){out={};return false;}
 }
 bool stack(Cpu*c,X::R::I::Config&out)const{
  out={};uint32_t low=0,high=0;if(c!=cpu||!current()||!threads::current_stack_bounds(c,low,high)||low<0x50000000||low>=high||(c->r[1]&7)||c->r[1]<uint64_t(low)+story_native_refresh_stack::kDownwardBytes||c->r[1]>high)return false;
  out.stack={low,high-low};out.protected_ranges={{arena.allocation.address,arena.allocation.bytes},{expected.identity.manager-0xF4,0x634},{expected.camera.address-0x248,0xB28}};
  for(unsigned n=0;n<2;++n){auto b=graph->baseline(n);if(!b)return false;out.protected_ranges.push_back({expected.actors[n].address,0x920});out.protected_ranges.push_back({b->model.morf,0xC8});out.protected_ranges.push_back({b->model.model,0x170});}
  const X::R::I::Range footprint{c->r[1]-story_native_refresh_stack::kDownwardBytes,story_native_refresh_stack::kDownwardBytes};for(auto r:out.protected_ranges)if(!r.address||!r.bytes||uint64_t(r.address)+r.bytes>0x100000000ull||overlap(out.stack,r)||overlap(footprint,r))return false;return true;
 }
 void quarantine()const noexcept{try{callbacks.quarantine();}catch(...){}}
};
Adapter::Adapter(std::unique_ptr<Impl>p):impl_(std::move(p)){}Adapter::~Adapter()=default;
std::unique_ptr<Adapter>Adapter::make(X::O::Expected e,story_native_event_arena::Layout arena,story_native_wait_session::Session&session,X::D::Tracker&t,Cpu*c,const story_native_outer_return::OuterReturn&outer,G::Context&graph,G::Binding&binding,story_native_wait_runtime_backend::Bundle backend,const X::B::Completion&returned,Callbacks callbacks){try{
 if(!c||!graph.prepared()||!binding.current()||!binding.quiescence()||!returned.matches(e)||!returned.cancel().matches(e)||!callbacks.retained||!callbacks.write||!callbacks.call||!callbacks.quarantine||!backend.sources.read||!backend.sources.actor||!backend.sources.session||!story_native_event_arena::valid(arena))return {};
 const auto baseline=session.baseline_poses();if(!baseline||!pose((*baseline)[0],returned.cancel().baseline()[0])||!pose((*baseline)[1],returned.cancel().baseline()[1]))return {};
 auto p=std::make_unique<Impl>();p->expected=std::move(e);p->arena=arena;p->session=&session;p->tracker=&t;p->cpu=c;p->outer=&outer;p->graph=&graph;p->binding=&binding;p->backend=std::move(backend);p->returned=returned;p->callbacks=std::move(callbacks);p->thread=std::this_thread::get_id();if(!p->current())return {};return std::unique_ptr<Adapter>(new Adapter(std::move(p)));
 }catch(...){return {};}}
X::Sources Adapter::observation_sources(){auto*p=impl_.get();X::Sources s;s.quiescence=p->binding->quiescence();s.sample=[p](Cpu*c,auto&out){return c==p->cpu&&p->sample(out);};s.retained=[p](const auto&e){return e==p->expected&&p->current();};s.resident=[p](const auto&e,unsigned n){return e==p->expected&&n<2&&p->resident(e.actors[n],e.identity);};s.verify_camera=[p](const auto&e,const auto&c){return e==p->expected&&p->camera(c);};return s;}
bool Adapter::use_closed(const X::Result&r){auto&p=*impl_;const auto baseline=p.session->baseline_poses();if(!baseline||!r.current_binding(p.cpu,*p.tracker,*p.outer)||!r.matches_baseline(*baseline)||!p.current()||!r.matches(p.expected)||r.phase()!=X::Phase::Closed||r.returned().after().outer_serial!=p.returned.after().outer_serial||r.returned().cancel().serial()!=p.returned.cancel().serial()||r.returned().cancel().poststate().manager!=p.returned.cancel().poststate().manager||r.returned().cancel().poststate().image!=p.returned.cancel().poststate().image||r.returned().cancel().poststate().actors!=p.returned.cancel().poststate().actors)return false;p.closed=r;return true;}
std::optional<X::R::Completion>Adapter::restore(){auto&p=*impl_;if(!p.current()||!p.closed||p.attempted)return {};p.attempted=true;
 try{
  const auto journal=p.session->journal();if(!journal||journal->phase!=story_native_wait_journal::Phase::Detaching)return {};
  // Establish invocation admission BEFORE any consumer write, so a dormant
  // callback graph cannot turn a denied refresh into a partial restoration.
  std::array<X::R::ModelIdentity,2>models;std::array<X::R::Transform,2>transforms;auto poses=p.returned.cancel().baseline();
  for(unsigned n=0;n<2;++n){auto b=p.graph->baseline(n);if(!b||!p.graph->verify_model(*p.binding,p.expected,n,b->model,b->transform))return {};models[n]=b->model;transforms[n]=b->transform;const auto&raw=p.returned.cancel().poststate().actors[n];poses[n].status=(word(raw,0x2E0)&~0x9800u)|(poses[n].status&0x9800u);}
  story_native_cancel_consumer_write::Sources ws;ws.capture=[&p](auto&out){X::Sample v;if(!p.sample(v))return false;out={v.poses,v.observation.camera,v.boundary};return true;};ws.retained=[&p](const auto&e){return e==p.expected&&p.current();};ws.camera=[&p](const auto&c,const auto&i){return i==p.expected.identity&&p.camera(c);};ws.write=[&p](auto at,auto bytes){return p.current()&&p.callbacks.write(at,bytes)&&p.current();};ws.quarantine=[&p]{p.quarantine();};
  auto q=p.binding->quiescence();if(!q||p.writer.apply(p.cpu,*p.tracker,*p.outer,*q,p.expected,*p.closed,ws)!=story_native_cancel_consumer_write::Result::Complete)return {};
  X::R::Sources rs;rs.capture=[&p](auto&out){X::Sample sample;if(!p.sample(sample))return false;out.poses=sample.poses;out.boundary=sample.boundary;for(unsigned n=0;n<2;++n){G::G::Capture v;if(!p.graph->capture(*p.binding,n,v))return false;out.models[n]=v.model;out.transforms[n]=v.transform;out.animations[n]=v.animation;out.root_calc[n]=v.root_calc;out.mtx_calc[n]=v.mtx_calc;}std::array<uint8_t,4>b{};if(!p.read(GD(0x104B462C),b))return false;out.current_model=word(b);return p.current();};
  rs.retained=[&p](const auto&e){return e==p.expected&&p.current();};rs.boundary=[&p](const Cpu*c,const auto&e,const auto&b){return c==p.cpu&&e==p.expected&&p.current()&&b.serial==p.closed->serial()&&b.logic_step==p.closed->logic_step();};rs.model=[&p](const auto&e,unsigned n,const auto&m,const auto&t){return p.current()&&p.graph->verify_model(*p.binding,e,n,m,t);};rs.stack=[&p](const Cpu*c,const auto&given){X::R::I::Config actual;if(!p.restoring||!p.stack(const_cast<Cpu*>(c),actual)||actual.stack.address!=given.stack.address||actual.stack.bytes!=given.stack.bytes||actual.protected_ranges.size()!=given.protected_ranges.size())return false;for(unsigned n=0;n<actual.protected_ranges.size();++n)if(actual.protected_ranges[n].address!=given.protected_ranges[n].address||actual.protected_ranges[n].bytes!=given.protected_ranges[n].bytes)return false;return true;};rs.read=[&p](auto at,auto bytes){return p.restoring&&p.read(at,bytes);};
  const uint32_t outgoing=p.cpu->r[1]-0x40;
  rs.write=[&p,outgoing](auto at,auto bytes){return p.restoring&&p.current()&&at==outgoing&&bytes.size()==0x40&&p.callbacks.write(at,bytes)&&p.current();};
  rs.call=[&p,models](Cpu*c,uint32_t fn,std::initializer_list<uint32_t>args){if(!p.restoring||!p.current()||c!=p.cpu||args.size()!=1)throw std::runtime_error("cancel refresh scope");bool exact=false;for(unsigned n=0;n<2;++n)if((fn==GC(0x022BF3A0)&&*args.begin()==p.expected.actors[n].address)||(fn==GC(0x025E55A0)&&*args.begin()==models[n].morf))exact=true;if(!exact)throw std::runtime_error("cancel refresh ABI");const Cpu saved=*c;const uint32_t at=c->r[1]-0x40;std::array<uint8_t,0x40>frame{},check{};if(!p.read(at,frame))throw std::runtime_error("cancel refresh outgoing frame");bool called=false,restored=false;uint32_t value=0;try{value=p.callbacks.call(c,fn,args);called=true;}catch(...){}*c=saved;try{restored=p.current()&&p.callbacks.write(at,frame)&&p.read(at,check)&&check==frame;}catch(...){restored=false;}*c=saved;if(!called||!restored||!p.current())throw std::runtime_error("cancel refresh CPU restore");return value;};rs.quarantine=[&p]{p.quarantine();};
  X::R::I::Config stack;p.restoring=true;struct Reset{bool&b;~Reset(){b=false;}}reset{p.restoring};if(!p.stack(p.cpu,stack)){p.quarantine();return {};}
  X::R::Invoker invoker(p.expected,poses,p.closed->boundary(),models,transforms,stack,std::move(rs));if(invoker.run(p.cpu)!=X::R::Result::Complete){p.quarantine();return {};}return invoker.completion();
 }catch(...){p.quarantine();return {};}}
}
