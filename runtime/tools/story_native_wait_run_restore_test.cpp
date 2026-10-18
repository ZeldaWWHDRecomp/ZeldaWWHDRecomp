// SPDX-License-Identifier: MPL-2.0
#define log_msg fixture_silenced_log
#define STORY_NATIVE_WAIT_SESSION_FIXTURE_ONLY
#include "story_native_wait_session_test.cpp"
#include <algorithm>
#include <string>
#include <array>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>
#include <span>
#include <utility>
#include <functional>
#include <cstring>
#include <memory>
#include <cstdio>
#undef log_msg
#include <cstdarg>
#include <cstdio>
void log_msg(const char*format,...){va_list args;va_start(args,format);std::vfprintf(stderr,format,args);std::fputc('\n',stderr);va_end(args);}
#include "mods/story_native_wait_runtime_controller.h"
#include "mods/story_native_wait_runtime_manager_cancel.h"
#include "mods/story_native_wait_runtime_model_graph.h"
#include "mods/story_native_wait_runtime_cancel_consumers.h"
#include "mods/story_native_wait_runtime_backend.h"
#include "mods/story_native_stage_owner_observer.h"
#include "savestate_native_gate.h"
#include "native_quiescence.h"
#include "mods/story_native_restore_resume.h"
#include "mods/story_native_scene_checkpoint.h"
#include <map>
#include <thread>
#define STORY_NATIVE_SETINFO_CANDIDATE_FIXTURE_ONLY
namespace resource_fixture {
#include "story_native_setinfo_candidate_test.cpp"
}
#ifdef STORY_NATIVE_RUN_REAL_MODEL
// Trusted primitive seam only. Production load-window issuer is savestate.cpp.
namespace ss {class NativeRestorePipelineIssuer {public:static std::unique_ptr<story_native_restore_resume::LoadWindow>open(Cpu*c){return std::unique_ptr<story_native_restore_resume::LoadWindow>(new story_native_restore_resume::LoadWindow(c));}static void threads_loaded(story_native_restore_resume::LoadWindow&w){w.threads_loaded();}static void thawed(story_native_restore_resume::LoadWindow&w){w.after_thaw();}};}
namespace interp {class NativeResumeBoundaryIssuer {public:static void returned(Cpu*c){story_native_restore_resume::LoadWindow::service_return(c);}};}
#endif
// No declared frontend runs in this isolated native fixture. Honest refusal;
// actual Scene checkpoint issuance is covered by the Scene protocol tests.
namespace story_scenes {std::optional<story_native_scene_checkpoint::Checkpoint>capture_native_checkpoint(const story_native_scene_playback::Activation&,const std::shared_ptr<story_native_scene_playback::RunOwnerCell>&)noexcept{return {};}}
namespace RC=story_native_wait_runtime_controller;
namespace RB=story_native_wait_runtime_backend;
namespace SO=story_native_stage_owner_observer;
namespace GT=story_native_event_game_thread;
static thread_local Cpu*current_cpu=nullptr;
// Synthetic scheduler primitive for this isolated Context test. Production's
// trusted issuer exists only in threads.cpp and scans actual parked threads.
static bool fixture_frozen=false,fixture_native_park=false;static uint64_t freeze_epoch=1;
static constexpr uint32_t guest_thread=0x20008000;
#ifdef STORY_NATIVE_RUN_REAL_MODEL
static constexpr uint32_t stack_begin=0x60040000,stack_bytes=0x4000,stack_sp=0x3000;
#else
static constexpr uint32_t stack_begin=0x20010000,stack_bytes=0x1000,stack_sp=0xC0;
#endif
namespace threads {
class NativeQuiescenceIssuer {
public:
 static NativeQuiescence make(std::function<bool(const Cpu*)>v,bool readonly=false){return NativeQuiescence(std::move(v),[]{},readonly);}
 static NativeQuiescence park(Cpu*c,story_native_event_game_thread::Ticket t){fixture_native_park=true;return NativeQuiescence([c,t](const Cpu*p){return fixture_native_park&&!fixture_frozen&&p==c&&current_cpu==c&&story_native_event_game_thread::verify(c,t);},[]{fixture_native_park=false;});}
 static QuiescenceWitness legacy(Cpu*c){const auto epoch=freeze_epoch;return QuiescenceWitness([c,epoch](const Cpu*actual){return fixture_frozen&&epoch==freeze_epoch&&actual==c&&current_cpu==c;});}
};
std::optional<QuiescenceWitness> current_quiescence(Cpu*c){if(!fixture_frozen||!c||c!=current_cpu)return {};return NativeQuiescenceIssuer::legacy(c);}
// No authentic model baseline exists here; borrowing for graph capture is
// explicitly refused rather than minting a physical scheduler capability.
std::optional<NativeQuiescence>borrow_native_read_quiescence(Cpu*c,story_native_event_game_thread::Ticket,const QuiescenceWitness&w,std::string&why){
#ifdef STORY_NATIVE_RUN_REAL_MODEL
 if(story_native_stage_owner::current_thread_holds_lifetime_pin()||!w.current(c)){why="fixture inverse lock or stale freeze";return {};}
 return NativeQuiescenceIssuer::make([c,w](const Cpu*actual){return actual==c&&w.current(actual);},true);
#else
 (void)c;(void)w;why="no fixture model baseline";return {};
#endif
}
Cpu*current(){return current_cpu;}
uint32_t current_thread(){return current_cpu?guest_thread:0;}
bool current_stack_bounds(const Cpu*c,uint32_t&bottom,uint32_t&top){bottom=top=0;if(!c||c!=current_cpu)return false;bottom=stack_begin;top=stack_begin+stack_bytes;return true;}
}
namespace guestmods {bool routes_owned(){return true;}bool native_camera_run_original_current(const Cpu*,const story_native_event_depth::Tracker&)noexcept{return false;}}
// This restore fixture never supplies a genuine pending Run original; the
// separate execution factory contract tests physical parking/dispatch. Refuse
// this unrelated private probe explicitly rather than manufacture authority.
namespace story_native_wait_runtime_manager_cancel {
struct Execution::Impl {};Execution::~Execution()=default;
std::unique_ptr<Execution>Execution::prepare(story_native_wait_session::Session&,M::D::Tracker&,M::O::Expected,story_native_event_arena::Layout,story_native_stage_owner_observer::Snapshot,story_native_event_game_thread::Ticket,Cpu*,story_native_wait_runtime_backend::Bundle,Callbacks){return {};}
Probe Execution::probe(Cpu*){return {};}
M::Result Execution::run(Cpu*){return M::Result::Refused;}
M::Completion Execution::completion()const{return {};}
bool Execution::native_end_proc_scope(const Cpu*,uint32_t,uint32_t,int32_t,uint32_t)const noexcept{return false;}
std::optional<uint32_t>Execution::route_all_off(Cpu*,const std::function<void()>&){return {};}

}
// The restore fixture has no native graph baseline or pending CameraRun. Its
// optional model inspection always refuses; scheduler/graph proof is separate.
#ifndef STORY_NATIVE_RUN_REAL_MODEL
namespace story_native_wait_runtime_model_graph {
bool Context::prepared()const{return false;}
bool Context::validate_stage_candidate(const story_native_wait_runtime_backend::StageBinding::Candidate&,std::string&)const{return false;}
Context::PreparedRestoreOpen::~PreparedRestoreOpen()=default;
void Context::PreparedRestoreOpen::open_unchecked()noexcept{std::terminate();}
std::optional<Context::PreparedRestoreOpen>Context::prepare_restore_open(const story_native_restore_resume::Boundary&,const Binding&,const story_native_event_observation::Result&,std::string&){return {};}

std::shared_ptr<Context>Context::rebind_saved(const story_native_restore_resume::Boundary&,story_native_wait_session::Session&,story_native_event_depth::Tracker&,story_native_stage_owner_observer::Snapshot,story_native_event_game_thread::Ticket,Cpu*,story_native_wait_runtime_backend::Bundle,std::string&)const{return {};}
bool Context::restore_clock(const story_native_restore_resume::Boundary&,std::string&){return false;}
std::shared_ptr<const threads::NativeQuiescence>Binding::quiescence()const{return {};}
std::optional<PendingCameraInspection>Context::inspect_pending_camera(const story_native_event_depth::FrameWitness&,std::string&why){why="no fixture pending native Run";return {};}
bool PendingCameraInspection::current()const noexcept{return false;}
bool PendingCameraInspection::refresh_ready()const noexcept{return false;}
std::optional<story_native_model_saved_graph::Receipt>Context::capture_saved(const Binding&)const{return {};}
bool Context::matches_saved(const story_native_model_saved_graph::Receipt&,const ss::native_gate::SavedView&)const{return false;}
bool Context::matches_live(const Binding&,const story_native_model_saved_graph::Receipt&)const{return false;}
std::optional<Binding>Context::bind(std::shared_ptr<const threads::NativeQuiescence>,std::string&why){why="no fixture model baseline";return {};}
Binding::~Binding()=default;
bool Binding::current()const{return false;}
std::optional<Binding>Context::bind(std::string&why){why="no fixture model baseline";return {};}

}
#endif
namespace threads {std::optional<NativeQuiescence>try_native_quiescence(Cpu*c,story_native_event_game_thread::Ticket t,int,std::string&why){if(fixture_frozen||fixture_native_park||current_thread_holds_native_quiescence()||story_native_stage_owner::current_thread_holds_lifetime_pin()||c!=current_cpu||!story_native_event_game_thread::verify(c,t)){why="fixture native park requester";return {};}
return NativeQuiescenceIssuer::park(c,t);
}}
#ifdef STORY_NATIVE_RUN_REAL_MODEL
namespace guestmods {
std::optional<story_native_refresh_dispatch::Guard>acquire_native_people_refresh_dispatch(){return story_native_refresh_dispatch::Guard([]{return fixture_frozen||fixture_native_park;});}
bool native_camera_run_original_pending(const Cpu*,const story_native_event_depth::Tracker&)noexcept{return false;}
}

#include "story_native_wait_saved_model_fixture.h"
#endif
// No physical model baseline exists in this restore-only fixture. A cancel
// adapter therefore cannot be manufactured from its saved footprint bytes.
namespace story_native_wait_runtime_cancel_consumers {
struct Adapter::Impl {};Adapter::~Adapter()=default;
std::unique_ptr<Adapter>Adapter::make(X::O::Expected,story_native_event_arena::Layout,story_native_wait_session::Session&,X::D::Tracker&,Cpu*,const story_native_outer_return::OuterReturn&,G::Context&,G::Binding&,story_native_wait_runtime_backend::Bundle,const X::B::Completion&,Callbacks){return {};}
X::Sources Adapter::observation_sources(){return {};}
bool Adapter::use_closed(const X::Result&){return false;}
std::optional<X::R::Completion>Adapter::restore(){return {};}
}
struct ResourceMapping {
 std::map<uint32_t,std::vector<uint8_t>> chunks;
 std::map<uint32_t,std::pair<uint32_t,size_t>>relocations;
 uint32_t relocated(uint32_t at)const{for(const auto&[base,to]:relocations)if(at>=base&&uint64_t(at)<uint64_t(base)+to.second)return to.first+at-base;return at;}
 explicit ResourceMapping(const resource_fixture::Fixture&f){
  unsigned index=0;for(const auto&r:f.regions)relocations.emplace(r.base,std::pair{0x35000000u+index++*0x100000u,r.bytes.size()});
  for(const auto&r:f.regions){auto data=r.bytes;for(size_t p=0;p+4<=data.size();p+=4){const uint32_t v=uint32_t(data[p])<<24|uint32_t(data[p+1])<<16|uint32_t(data[p+2])<<8|data[p+3];resource_fixture::w32(data,unsigned(p),relocated(v));}
   for(size_t p=0;p<data.size();++p){const auto at=relocated(r.base)+uint32_t(p),base=at&~(ss::kMemoryChunk-1);auto&b=chunks[base];if(b.empty())b.resize(ss::kMemoryChunk);b[at-base]=data[p];}
  }
  auto&global=chunks[0x101F0000];global.resize(ss::kMemoryChunk);resource_fixture::w32(global,0x4F7C,relocated(resource_fixture::Mgr));
  for(const auto&[at,b]:chunks){
#ifdef _WIN32
   auto*p=VirtualAlloc(PPC_MEM_BASE+at,ss::kMemoryChunk,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
   auto*p=mmap(PPC_MEM_BASE+at,ss::kMemoryChunk,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0);
#endif
   assert(p==PPC_MEM_BASE+at);std::memcpy(p,b.data(),b.size());
  }
 }
 ~ResourceMapping(){for(const auto&[at,b]:chunks){(void)b;
#ifdef _WIN32
  assert(VirtualFree(PPC_MEM_BASE+at,0,MEM_RELEASE));
#else
  assert(!munmap(PPC_MEM_BASE+at,ss::kMemoryChunk));
#endif
 }}
};
struct SavedMemory {
 ss::Writer allocation;std::map<uint32_t,std::vector<uint8_t>>bytes;std::vector<ss::MemoryRegion>regions;ss::MemoryChunks chunks;
 void add(uint32_t at,std::span<const uint8_t>input){for(size_t n=0;n<input.size();++n){auto base=(at+uint32_t(n))&~(ss::kMemoryChunk-1);auto&b=bytes[base];if(b.empty())b.resize(ss::kMemoryChunk);b[at+uint32_t(n)-base]=input[n];}}
 SavedMemory(const Readiness&f,const ResourceMapping&resources,const story_native_event_arena::Layout&a){
  allocation.u32(mem::runtime_top());allocation.u32(uint32_t(allocations.size()));for(const auto&r:allocations)allocation.pod(r);
  for(const auto&[at,b]:resources.chunks)add(at,std::span(mem::ptr(at),b.size()));
  for(const auto&[at,b]:f.memory){
#ifdef STORY_NATIVE_RUN_REAL_MODEL
   // Sparse fixture aliases are views of the same address space. Serialize
   // effective bytes, so a later zero backing view cannot overwrite its parent.
   std::vector<uint8_t>actual(b.size());for(size_t n=0;n<actual.size();++n){const auto*p=const_cast<Readiness&>(f).pointer(at+uint32_t(n),1);assert(p);actual[n]=*p;}add(at,actual);
#else
   add(at,b);
#endif
  }
  add(a.allocation.address,std::span(mem::ptr(a.allocation.address),a.allocation.bytes));
#ifdef STORY_NATIVE_CROSS_SESSION_MULTI_ARENA
  add(0x60050000,std::span(mem::ptr(0x60050000),story_native_event_arena::kBytes));
#endif
  for(auto&[at,b]:bytes){regions.push_back({at,ss::kMemoryChunk});chunks.emplace(at,b.data());}
 }
 auto view(){return ss::native_gate::SavedView{ss::Reader(allocation.b.data(),allocation.b.size()),regions,chunks};}
 void restore(const std::function<bool(uint32_t,std::span<const uint8_t>)>&write,const ResourceMapping&resources,const story_native_event_arena::Layout&a){
  for(const auto&[at,b]:resources.chunks)std::copy_n(bytes.at(at).begin(),b.size(),mem::ptr(at));
  const auto&b=bytes.at(a.allocation.address&~(ss::kMemoryChunk-1));std::copy_n(b.begin()+(a.allocation.address&(ss::kMemoryChunk-1)),a.allocation.bytes,mem::ptr(a.allocation.address));
  (void)write; // unchanged native object bytes in this stable ORDER fixture
 }
};
static void run(unsigned fault){
 fixture_frozen=false;++freeze_epoch;auto resources=resource_fixture::fixture();
#ifdef STORY_NATIVE_RUN_REAL_MODEL
 resources.at(resource_fixture::Mgr).resize(0x1131C);
#endif
 ResourceMapping mapped(resources);auto*guest=map_arena();Readiness f;auto req=request();
#ifdef STORY_NATIVE_RUN_REAL_MODEL_TURN
 f.put(0x1004487C+4,1);f.half(0x10049350+2*0x40+0x18,1);f.put(0x1004487C+0x84+4,11);
 story_native_turn_camera_image::Program turn;turn.camera.start_eye={0,10,20};turn.camera.end_eye={10,10,20};req.program=story_native_event_program::Program::turn(turn,301);req.fixture_program="sdk-private-turn-v1";
#endif
 S::Session session(req,f.source());assert(session.startup());Cpu cpu{};cpu.r[1]=0x600300C0;
#ifdef STORY_NATIVE_INITIAL_IMPORT_FLOW
 STORY_NATIVE_INITIAL_IMPORT_FLOW;
 fixture_frozen=false;++freeze_epoch;current_cpu=nullptr;unmap_arena(guest);return;
#endif
#ifdef STORY_NATIVE_CONTEXT_PRISTINE_BEFORE_NATIVE
 STORY_NATIVE_CONTEXT_PRISTINE_BEFORE_NATIVE;
#endif
#ifdef STORY_NATIVE_PASSIVE_CONTROL_BEFORE_INSPECT
 STORY_NATIVE_PASSIVE_CONTROL_BEFORE_INSPECT;
#endif
 assert(session.inspect(&cpu));auto hold=session.try_fence();auto expected=*session.expected();const auto arena=session.arena();const auto selected=expected.program&&expected.program->kind()==story_native_event_program::Kind::TurnCamera?arena.turn:arena.wait;
 #ifdef STORY_NATIVE_RUN_REAL_MODEL
 SavedModelFixture graph;graph.quiescence.reset();graph.guard.reset(); // data fixture holds no scheduler authority
 // The native Stage and Uo graph MUST share their real archive-cache manager.
 // Rebase the Uo cache into the Stage manager, then add the existing Stage
 // holder as a second canonical pool node. No conflicting same-address views.
 const uint32_t old_loader=0x21000000,new_loader=mapped.relocated(resource_fixture::Mgr);
 auto loader_bytes=std::move(graph.memory.at(old_loader));graph.memory.erase(old_loader);graph.memory[new_loader]=std::move(loader_bytes);
 for(auto&[at,bytes]:graph.memory){(void)at;for(size_t off=0;off+4<=bytes.size();off+=4){const uint32_t value=uint32_t(bytes[off])<<24|uint32_t(bytes[off+1])<<16|uint32_t(bytes[off+2])<<8|bytes[off+3];if(value>=old_loader&&value<old_loader+0x1131C)graph_put(bytes,unsigned(off),new_loader+value-old_loader);}}
 const uint32_t stage_node=new_loader+0x28+0x54;auto&loader=graph.memory.at(new_loader);
 std::copy_n(mem::ptr(mapped.relocated(resource_fixture::HolderNode)),0x54,loader.begin()+0x28+0x54);
 graph_put(loader,0x28,stage_node);graph_put(loader,0x20,2);
 graph_put(loader,0x28+0x54+0xC,stage_node+0x20);graph_put(loader,0x28+0x54+0x10,0x100E20A4);graph_put(loader,0x28+0x54+0x14,0x100E231C);graph_put(loader,0x28+0x54+0x1C,new_loader+0x14);
 std::copy(loader.begin(),loader.end(),mem::ptr(new_loader));
 f.memory[0x104B4868].resize(48);f.memory[0x104B4628].resize(8);f.memory[0x1048D0CC].resize(48); // audited J3D global scratch in the graph write trace
 for(const auto&[at,bytes]:graph.memory){
  if(at==expected.actors[0].address||at==expected.actors[1].address){for(unsigned off:{0xF4u,0x2DCu,0x3B4u,0x44Cu,0x8A0u,0x904u})std::copy_n(bytes.begin()+off,4,f.memory[at].begin()+off);}
  else f.memory[at]=bytes;
 }
#endif
 story_native_people_pose_record::Poses poses;for(unsigned n=0;n<2;++n){const auto&a=expected.actors[n];poses[n].identity={a.address,a.pid,a.token,expected.identity.stage,expected.identity.room,expected.identity.generation,0x30000000+n*0x2000,0x30001000+n*0x2000};poses[n].cut=a.cut;
#ifdef STORY_NATIVE_RUN_REAL_MODEL
 poses[n].identity.morf=graph.morf[n];poses[n].identity.model=graph.model[n];
#else
 f.memory[poses[n].identity.morf].resize(0xC8);f.memory[poses[n].identity.model].resize(0x170);
#endif
 }
 assert((expected.program&&expected.program->kind()==story_native_event_program::Kind::TurnCamera?session.stage_image(poses):(session.stage_image()&&session.stage_poses(poses))));D::Tracker depth(expected,{{1,1,true},{2,2,true},{3,3,true},true});
#ifdef STORY_NATIVE_RUN_REAL_MODEL
 // Actual bounded fixture allocation, distinct from the SDK arena. The
 // production backend independently requires thread stacks below runtime_top.
 allocations.push_back({stack_begin,stack_bytes,124});
#endif
 f.memory[stack_begin].resize(stack_bytes);f.memory[guest_thread].resize(0x400);f.put(guest_thread+0x394,stack_begin+stack_bytes);f.put(guest_thread+0x398,stack_begin);cpu.r[1]=stack_begin+stack_sp;current_cpu=&cpu;GT::state_loaded();assert(GT::observe_logic(&cpu));
 auto read=[&](uint32_t at,std::span<uint8_t>out){
#ifdef STORY_NATIVE_CROSS_SESSION_MULTI_ARENA
 if(at>=0x60050000&&uint64_t(at)+out.size()<=0x60050000+story_native_event_arena::kBytes){std::copy_n(mem::ptr(at),out.size(),out.begin());return true;}
#endif
 if(at>=arena.allocation.address&&uint64_t(at)+out.size()<=uint64_t(arena.allocation.address)+arena.allocation.bytes){std::copy_n(mem::ptr(at),out.size(),out.begin());return true;}auto*p=f.pointer(at,out.size());if(!p){
#ifdef STORY_NATIVE_RUN_REAL_MODEL
 for(size_t n=0;n<out.size();++n){auto*q=f.pointer(at+uint32_t(n),1);if(!q)return false;out[n]=*q;}return true;
#else
 return false;
#endif
 }std::copy_n(p,out.size(),out.begin());return true;};
 auto write=[&](uint32_t at,std::span<const uint8_t>in){
#ifdef STORY_NATIVE_CROSS_SESSION_MULTI_ARENA
 if(at>=0x60050000&&uint64_t(at)+in.size()<=0x60050000+story_native_event_arena::kBytes){std::copy(in.begin(),in.end(),mem::ptr(at));return true;}
#endif
 auto*p=at>=arena.allocation.address&&uint64_t(at)+in.size()<=uint64_t(arena.allocation.address)+arena.allocation.bytes?mem::ptr(at):f.pointer(at,in.size());if(!p)return false;std::copy(in.begin(),in.end(),p);return true;};
 auto w=[&](uint32_t at,uint32_t v){std::array<uint8_t,4>b{};for(unsigned n=0;n<4;++n)b[n]=uint8_t(v>>(24-8*n));assert(write(at,b));};
 SO::state_loaded();const auto*observer=SO::observer();SO::Args args{{mapped.relocated(resource_fixture::Ctrl),mapped.relocated(resource_fixture::Args),mapped.relocated(resource_fixture::Args+0x20),0}};const auto cookie=observer->before_setinfo(&cpu,args);assert(cookie);cpu.r[3]=1;observer->after_setinfo(&cpu,args,1,cookie);const auto stage=SO::snapshot();assert(stage);
 RC::Context*actual_context=nullptr;uint64_t step=100;RB::Config config;config.collector=f.config;config.collector.stamp=selected.stamp.address;config.memory={selected.stamp.address,link_actor,55,{selected.record.address,selected.record.bytes}};RB::Callbacks callbacks;callbacks.session=&session;callbacks.retained=[&](const auto&e){return e==expected&&actual_context&&actual_context->retained();};callbacks.read=read;callbacks.resident=[&](const Actor&a,const auto&i){return i==expected.identity&&std::any_of(expected.actors.begin(),expected.actors.end(),[&](const auto&saved){return a.address==saved.address&&a.pid==saved.pid&&a.token==saved.token&&a.profile==saved.profile&&a.subtype==saved.subtype&&a.fingerprint==saved.fingerprint;});};callbacks.fence=[&]{return session.fence_held();};callbacks.routes=[]{return true;};callbacks.baseline=[&](const auto&e){auto current=session.baseline();if(!current)current=session.staged_expected();return current&&*current==e;};callbacks.boundary=[&](const Cpu*c,story_native_wait_readiness::Boundary&b){if(c!=current_cpu)return false;b={guest_thread,stack_begin,stack_begin+stack_bytes,step,true};return true;};callbacks.allocation=[&](const auto&i,const auto&c){return i==expected.identity&&c.stamp==config.memory.stamp;};
 [[maybe_unused]] const auto restore_callbacks=callbacks;auto backend=RB::make(expected,arena,config,*stage,GT::ticket(&cpu),&cpu,depth,std::move(callbacks));assert(backend);
 unsigned native_calls=0,restore_reads=0,resume_boundaries=0;RC::Sources source;[[maybe_unused]] RC::Sources restored_sources;
#ifdef STORY_NATIVE_RUN_REAL_MODEL
 source.restored_boundary=[&](Cpu*c,const story_native_restore_resume::Boundary&boundary){assert(c==&cpu&&boundary.current(c));const auto&origin=boundary.completion().origin();assert(origin.expected()==expected&&origin.scope().logic_step==100&&origin.scope().serial==0&&D::Tracker::valid_idle_record(origin.depth_record(),expected));
#ifdef STORY_NATIVE_RUN_REAL_MODEL_TURN
 assert(origin.clock()&&origin.clock()->identity==expected.identity&&origin.clock()->logic_step==100&&origin.clock()->serial==0&&origin.clock()->elapsed_ticks==0);
#else
 assert(!origin.clock());
#endif

#ifdef STORY_NATIVE_REBIND_OBSERVER
 STORY_NATIVE_REBIND_OBSERVER(c,boundary,session,depth,expected,arena,config,restore_callbacks,step);
#endif
#ifdef STORY_NATIVE_ADOPT_OBSERVER
 STORY_NATIVE_ADOPT_OBSERVER(c,boundary,session,expected,arena,config,restore_callbacks,restored_sources,native_calls,actual_context,depth,fault);
#endif
 ++resume_boundaries;};
#endif
#ifdef STORY_NATIVE_RUN_REAL_MODEL
 std::string graph_reason;source.model_graph=story_native_wait_runtime_model_graph::Context::prepare(session,depth,expected,*stage,GT::ticket(&cpu),&cpu,*backend,graph_reason);if(!source.model_graph)std::fprintf(stderr,"graph prepare failure=%s\n",graph_reason.c_str());assert(source.model_graph&&source.model_graph->prepared());
#endif
source.restore_read=[&](auto at,auto bytes){++restore_reads;return read(at,bytes);};if(fault==6)source.restore_read={};source.write=write;source.routes=[]{return true;};source.stack=[](Cpu*,story_native_event_invoke::Config&c){c={};c.stack={stack_begin,stack_bytes};return true;};source.call=[&](Cpu*,uint32_t fn,std::initializer_list<uint32_t>regs){++native_calls;std::vector<uint32_t>a(regs);switch(fn){
 case 0x0253EC0C:f.byte(play+0x51D0+0xC0,1);return 1u;
 case 0x0253FD1C:f.byte(play+0x51D0+0xC0,0);f.byte(play+0x51D0+0xC2,3);for(unsigned p:{0xC4,0xC8,0xCC,0xD0})w(play+0x51D0+p,~0u);f.half(play+0x51D0+0xD8,0xFFFF);f.half(play+0x51D0+0xDE,0xFFFF);return 1u;
 case 0x0254273C:for(unsigned n=0;n<8;++n)w(expected.identity.manager+n*4,0);return 0u;
 case 0x025443B4:{const auto wait_image=story_native_event_image::make_wait_fixture(expected.frames);const auto pristine=expected.program?std::vector<uint8_t>(expected.program->pristine().begin(),expected.program->pristine().end()):std::vector<uint8_t>(wait_image.begin(),wait_image.end());for(unsigned n=0;n<8;++n){const auto p=n?uint32_t(pristine[(n-1)*8])<<24|uint32_t(pristine[(n-1)*8+1])<<16|uint32_t(pristine[(n-1)*8+2])<<8|pristine[(n-1)*8+3]:0;const auto at=(n? n-1:0)*8;const auto count=uint32_t(pristine[at+4])<<24|uint32_t(pristine[at+5])<<16|uint32_t(pristine[at+6])<<8|pristine[at+7];w(expected.identity.manager+n*4,n?(count?expected.identity.image+p:0):expected.identity.image);}return 0u;}
 case 0x0259F7D4:w(a[0],a[1]);w(a[0]+8,a[2]);w(a[0]+0x10,~0u);return 0u;
 case 0x02544534:w(expected.identity.image+0xE4,1);return 1u;
 default:assert(false);return 0u;}};
 restored_sources=source;restored_sources.restored_boundary={};
 auto context=RC::Context::prepare(session,depth,expected,arena,*stage,GT::ticket(&cpu),&cpu,*backend,{{{"test","first"},{"test","second"}}},std::move(source));assert(context);actual_context=context.get();
#ifdef STORY_NATIVE_CROSS_SESSION_BEFORE_START
 STORY_NATIVE_CROSS_SESSION_BEFORE_START;
#endif
 assert(!context->rearm(&cpu)&&native_calls==0);assert(!context->begin_saved_run());assert(context->tick(&cpu,true)==RC::State::Started&&native_calls==7);hold.reset();assert(!context->rearm(&cpu)&&native_calls==7);assert(!context->begin_saved_run());
#ifdef STORY_NATIVE_CONTEXT_PRISTINE_AFTER_NATIVE
 STORY_NATIVE_CONTEXT_PRISTINE_AFTER_NATIVE;
 context.reset();fixture_frozen=false;++freeze_epoch;current_cpu=nullptr;hold.reset();unmap_arena(guest);return;
#endif
#ifdef STORY_NATIVE_STAGE_RENEW_FLOW
 STORY_NATIVE_STAGE_RENEW_FLOW;
 context.reset();current_cpu=nullptr;hold.reset();unmap_arena(guest);return;
#endif
 fixture_frozen=true;++freeze_epoch;
#ifdef STORY_NATIVE_ACTIVE_GATE_FLOW
 STORY_NATIVE_ACTIVE_GATE_FLOW;
 context.reset();fixture_frozen=false;++freeze_epoch;current_cpu=nullptr;hold.reset();unmap_arena(guest);return;
#endif
 Cpu other=cpu;current_cpu=&other;assert(!context->begin_saved_run());current_cpu=&cpu;
#ifdef STORY_NATIVE_CROSS_SESSION_SETUP
 STORY_NATIVE_CROSS_SESSION_SETUP;
#endif
 auto receipt=context->begin_saved_run();assert(receipt&&session.fence_held()&&native_calls==7);assert(!context->checkpoint_saved_run(*receipt));SavedMemory saved(f,mapped,arena);
 bool foreign_commit=true;std::thread([&]{foreign_commit=context->commit_saved_run(*receipt);}).join();assert(!foreign_commit&&session.fence_held());
 #ifdef STORY_NATIVE_RUN_REAL_MODEL
 if(fault==11){f.put(0x1048D0CC,0xBAD);assert(!context->commit_saved_run(*receipt)&&context->state()==RC::State::Quarantined);}
 else
#endif
 if(fault==8){fixture_frozen=false;assert(!context->commit_saved_run(*receipt)&&context->state()==RC::State::Quarantined);}
 else if(fault==1){++step;assert(!context->commit_saved_run(*receipt)&&context->state()==RC::State::Quarantined);}
 else {
  assert(context->commit_saved_run(*receipt)&&!session.fence_held()&&native_calls==7);assert(!context->commit_saved_run(*receipt));
  auto checkpoint=context->checkpoint_saved_run(*receipt);assert(checkpoint&&!context->checkpoint_saved_run(*receipt)&&checkpoint->matches_saved(saved.view())&&checkpoint->matches_saved(saved.view()));
#ifdef STORY_NATIVE_HISTORICAL_RESTORE_FLOW
  STORY_NATIVE_HISTORICAL_RESTORE_FLOW;
  context.reset();fixture_frozen=false;++freeze_epoch;current_cpu=nullptr;hold.reset();unmap_arena(guest);return;
#endif
#ifdef STORY_NATIVE_CROSS_SESSION_FLOW_INCLUDE
#include STORY_NATIVE_CROSS_SESSION_FLOW_INCLUDE
#include <algorithm>
#include <string>
#include <array>
#include <vector>
#include <optional>
#include <cstdint>
#include <span>
#include <utility>
#include <functional>
#include <cstring>
#include <memory>
  context.reset();fixture_frozen=false;++freeze_epoch;current_cpu=nullptr;hold.reset();unmap_arena(guest);return;
#endif
#ifdef STORY_NATIVE_CROSS_SESSION_FLOW
  STORY_NATIVE_CROSS_SESSION_FLOW;
  context.reset();fixture_frozen=false;++freeze_epoch;current_cpu=nullptr;hold.reset();unmap_arena(guest);return;
#endif
  if(fault==0){
   auto failed=checkpoint->fork_attempt();auto independent=checkpoint->fork_attempt();assert(failed&&independent);
   const auto page=expected.actors[0].address&~(ss::kMemoryChunk-1);const auto offset=expected.actors[0].address&(ss::kMemoryChunk-1);saved.bytes.at(page)[offset]^=1;
   assert(!checkpoint->matches_saved(saved.view())&&!context->prepare_run_restore(std::move(*failed),saved.view(),1));saved.bytes.at(page)[offset]^=1;
   assert(checkpoint->matches_saved(saved.view())&&!context->prepare_run_restore(std::move(*failed),saved.view(),1));receipt=std::move(independent);
  }
  fixture_frozen=false;++freeze_epoch;
  #ifdef STORY_NATIVE_RUN_REAL_MODEL
  if(fault==15){auto fence=session.try_fence();assert(fence);D::Call actual{D::Entry::ManagerRun,D::entry_address(D::Entry::ManagerRun),expected.identity.manager,expected.identity.manager,0,0,expected.identity,D::Authority::Owned};{auto native_entry=depth.enter(actual);assert(native_entry.allowed());}step+=5;}
#endif
  if(fault==10){assert(!context->prepare_run_restore(std::move(*receipt),saved.view(),1));}
  else {fixture_frozen=true;++freeze_epoch;
  #ifdef STORY_NATIVE_RUN_REAL_MODEL
  if(fault==12){saved.bytes.at(0x10480000)[0xD0CC]^=1;assert(!context->prepare_run_restore(std::move(*receipt),saved.view(),1));}
  else
#endif
  if(fault==2){saved.bytes.at(expected.actors[0].address&~(ss::kMemoryChunk-1))[expected.actors[0].address&(ss::kMemoryChunk-1)]^=1;assert(!context->prepare_run_restore(std::move(*receipt),saved.view(),1));saved.bytes.at(expected.actors[0].address&~(ss::kMemoryChunk-1))[expected.actors[0].address&(ss::kMemoryChunk-1)]^=1;assert(!context->prepare_run_restore(std::move(*receipt),saved.view(),2));}
  else if(fault==3){context->invalidate();assert(!context->prepare_run_restore(std::move(*receipt),saved.view(),1));}
  else if(fault==6){assert(!context->prepare_run_restore(std::move(*receipt),saved.view(),1));}
  else {
   bool foreign_prepare=true;std::thread([&]{foreign_prepare=bool(context->prepare_run_restore(std::move(*receipt),saved.view(),1));}).join();assert(!foreign_prepare);
   auto permit=context->prepare_run_restore(std::move(*receipt),saved.view(),1);assert(permit&&session.fence_held()&&native_calls==7);
   if(fault==4){assert(!context->finish_run_restore(std::move(*permit)));}
   else {
    bool foreign_suspend=true;std::thread([&]{foreign_suspend=context->suspend_run_restore(*permit);}).join();assert(!foreign_suspend);
    assert(context->suspend_run_restore(*permit)&&!context->suspend_run_restore(*permit));
    std::array<uint8_t,0x100> before{};assert(read(selected.header.address,before));
    D::Call call;cpu.r[3]=expected.identity.manager;assert(context->outer(&cpu,D::entry_address(D::Entry::ManagerRun),call)&&call.authority==D::Authority::InvalidOwned);
    unsigned delegated=0;assert(context->route(&cpu,D::entry_address(D::Entry::ManagerMain),[&]{++delegated;})==story_native_wait_owned_routes::Decision::Handled&&!delegated);
    saved.restore(write,mapped,arena);context->invalidate();SO::state_loaded();GT::state_loaded();
    if(fault==5)mem::ptr(selected.image.address)[0]^=1;
#ifdef STORY_NATIVE_RUN_REAL_MODEL
    if(fault==13)f.put(0x1048D0CC,0xBAD);
#endif
    if(fault==7)SO::state_loaded();
    if(fault==9){fixture_frozen=false;++freeze_epoch;fixture_frozen=true;++freeze_epoch;}
    bool foreign_finish=true;std::thread([&]{foreign_finish=context->finish_run_restore(std::move(*permit));}).join();assert(!foreign_finish);
    #ifdef STORY_NATIVE_RUN_REAL_MODEL
    auto load_window=fault==14?nullptr:ss::NativeRestorePipelineIssuer::open(&cpu);
#endif
    assert(context->finish_run_restore(std::move(*permit))==(fault!=5&&fault!=7&&fault!=9&&fault!=13&&fault!=14));if(fault==9)assert(!restore_reads);
#ifdef STORY_NATIVE_RUN_REAL_MODEL
    if(load_window)ss::NativeRestorePipelineIssuer::threads_loaded(*load_window);fixture_frozen=false;++freeze_epoch;if(load_window)ss::NativeRestorePipelineIssuer::thawed(*load_window);load_window.reset();interp::NativeResumeBoundaryIssuer::returned(&cpu);
    if(fault!=5&&fault!=7&&fault!=9&&fault!=13&&fault!=14){assert(GT::verify(&cpu,GT::ticket(&cpu)));assert(resume_boundaries==1);}else assert(!resume_boundaries);
#endif
    std::array<uint8_t,0x100> after{};assert(read(selected.header.address,after)&&after==before&&native_calls==7);
    assert(context->outer(&cpu,D::entry_address(D::Entry::ManagerRun),call)&&call.authority==D::Authority::InvalidOwned);
    context->invalidate();assert(read(selected.header.address,after)&&after==before);
   }
  }
 }
 }
 context.reset();fixture_frozen=false;++freeze_epoch;current_cpu=nullptr;hold.reset();unmap_arena(guest);
}
int main(){assert(SO::configure(story_native_setinfo_observation::inspect_detailed,story_native_setinfo_observation::inspect_live));
#ifdef STORY_NATIVE_RUN_REAL_MODEL
 #ifdef STORY_NATIVE_RESTORE_CASE_COUNT
 for(unsigned n=0;n<STORY_NATIVE_RESTORE_CASE_COUNT;++n)run(n);
#else
 for(unsigned n=0;n<16;++n)run(n);
#endif
#else
 for(unsigned n=0;n<11;++n)run(n);
#endif
}
