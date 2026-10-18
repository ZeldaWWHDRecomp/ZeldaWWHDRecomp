// SPDX-License-Identifier: MPL-2.0
#include "runtime.h"
#include <vector>
#include <map>
#include <cstdint>
#include <span>
#include <utility>
#include <memory>
#define STORY_NATIVE_WAIT_SESSION_FIXTURE_ONLY
#include "story_native_wait_session_test.cpp"
#include "native_quiescence.h"
#include "state_memory.h"
#include <map>
namespace threads {class NativeQuiescenceIssuer{public:static QuiescenceWitness witness(Cpu*c,bool&live){return QuiescenceWitness([c,&live](const Cpu*x){return live&&c==x;});}};}
#ifdef STORY_NATIVE_PRISTINE_STAGE_TEST
#include "mods/story_native_wait_pristine_snapshot.h"
#include <vector>
#include <cstdint>
#include <span>
#include <utility>
#include <memory>
namespace PS=story_native_wait_pristine_snapshot;namespace PO=story_native_stage_owner_observer;
namespace {
PO::Candidate pristine_stage(){PO::Candidate c;c.info=0x60810000;c.archive_wrapper=0x60811000;c.archive_holder=0x60812000;c.archive_resource=0x60818000;c.archive_base=0x6081F000;c.archive_bytes=0x3000;c.outer_base=0x60820000;c.outer_bytes=0x1000;c.resource=0x60820240;c.resource_bytes=0x80;c.entry=0x60820118;c.archive_key=0x1004DF68;c.resource_key=0x1004DF70;c.request_heap=c.pack_heap=0x60830000;c.data_heap=0x60831000;c.data_heap_parent=0x60832000;c.stage_name[0]='s';return c;}
story_native_setinfo_observation::Result pristine_inspect(const Cpu*,const PO::Args&,uint32_t r,PO::Candidate&out){out=pristine_stage();return r==1?story_native_setinfo_observation::Result::Valid:story_native_setinfo_observation::Result::Candidate;}
bool pristine_live(uint32_t control,const PO::Candidate&e,PO::Candidate&out){out=pristine_stage();return control==0x60700000&&e==out;}
// Isolated lifetime/parser-source fixture; real native SavedView parser has its
// own complete resource-graph contract. This is not a production issuer.
bool pristine_saved(const ss::native_gate::SavedView&,uint32_t c,const PO::Candidate&e,PO::Candidate&out){return pristine_live(c,e,out);}
void pristine_issue(){const auto*o=PO::observer();const PO::Args a{{0x60700000,0x60700010,0x60700020,0}};std::lock_guard lock(*o->lifetime_fence);const auto k=o->before_setinfo(nullptr,a);assert(k);o->after_setinfo(nullptr,a,1,k);assert(PO::snapshot());}
}
#endif
static auto pristine_begin(S::Session&s,Cpu*c,const threads::QuiescenceWitness&w){
#ifdef STORY_NATIVE_PRISTINE_STAGE_TEST
 return PS::begin(s,c,w);
#else
 return s.begin_pristine_capture(c,w);
#endif
}
template<class Capture>static auto pristine_commit(S::Session&s,Capture&&p,Cpu*c,const threads::QuiescenceWitness&w){
#ifdef STORY_NATIVE_PRISTINE_STAGE_TEST
 (void)s;return PS::commit(std::forward<Capture>(p),c,w);
#else
 return s.commit_pristine_capture(std::forward<Capture>(p),c,w);
#endif
}
template<class Checkpoint>static auto pristine_prepare(S::Session&s,const Checkpoint&p,const ss::native_gate::SavedView&v,Cpu*c,const threads::QuiescenceWitness&w,uint64_t epoch){
#ifdef STORY_NATIVE_PRISTINE_STAGE_TEST
 return PS::prepare(s,p,v,c,w,epoch);
#else
 return s.prepare_pristine_restore(p,v,c,w,epoch);
#endif
}
template<class Restore>static auto pristine_adopt(S::Session&s,Restore&&p,const ss::native_gate::SavedView&v,Cpu*c,const threads::QuiescenceWitness&w,std::unique_ptr<S::Session::Fence>*f){
#ifdef STORY_NATIVE_PRISTINE_STAGE_TEST
 (void)s;PO::state_loaded();return PS::finish(std::forward<Restore>(p),v,c,w,f);
#else
 return s.adopt_pristine(std::forward<Restore>(p),v,c,w,f);
#endif
}
int main(){(void)covered;for(unsigned mode=0;mode<14;++mode){
#ifdef STORY_NATIVE_PRISTINE_STAGE_TEST
 if(mode==1)assert(PO::configure(pristine_inspect,pristine_live,pristine_saved));
 if(mode)pristine_issue();
#endif
 auto*guest=map_arena();struct Unmap{uint8_t*p;~Unmap(){unmap_arena(p);}}unmap{guest};{
 Readiness f;S::Session session(request(),f.source());assert(session.startup());Cpu cpu{};cpu.r[1]=0x600300C0;bool live=true;auto witness=threads::NativeQuiescenceIssuer::witness(&cpu,live);
 assert(!pristine_begin(session,&cpu,witness));auto hold=session.try_fence();assert(hold);
 if(mode==8){hold.reset();assert(session.inspect(&cpu));hold=session.try_fence();assert(hold&&session.stage_image());assert(!pristine_begin(session,&cpu,witness));continue;}
 // A different scene may own native globals. This proof covers only this
 // genuinely unexposed arena and never consults global event readiness.
 pending_native=true;active_scene=true;
 if(mode==12){f.put(play+0x52C4,arena_at+0x100);assert(!pristine_begin(session,&cpu,witness));continue;}
 auto capture=pristine_begin(session,&cpu,witness);assert(capture);
 if(mode==11){f.put(play+0x52C4,arena_at+0x100);assert(!pristine_commit(session,std::move(*capture),&cpu,witness));continue;}

 if(mode==1){hold.reset();hold=session.try_fence();assert(hold&&!pristine_commit(session,std::move(*capture),&cpu,witness));continue;}
 if(mode==2){live=false;assert(!pristine_commit(session,std::move(*capture),&cpu,witness));continue;}
 if(mode==3){mem::ptr(arena_at)[0x300]=1;assert(!pristine_commit(session,std::move(*capture),&cpu,witness));continue;}
 if(mode==4){std::thread other([&]{assert(!pristine_commit(session,std::move(*capture),&cpu,witness));});other.join();}
 if(mode==5){session.revoke_unexposed();assert(!pristine_commit(session,std::move(*capture),&cpu,witness)&&!pristine_begin(session,&cpu,witness)&&!session.unexposed());continue;}
 if(mode==6){Cpu other{};assert(!pristine_commit(session,std::move(*capture),&other,witness)&&!pristine_commit(session,std::move(*capture),&cpu,witness));continue;}
 auto checkpoint=pristine_commit(session,std::move(*capture),&cpu,witness);
#ifdef STORY_NATIVE_PRISTINE_STAGE_TEST
 assert(checkpoint&&checkpoint->has_stage()==bool(mode));
#endif
 assert(checkpoint&&!pristine_commit(session,std::move(*capture),&cpu,witness));
 std::map<uint32_t,std::vector<uint8_t>>saved;
 auto copy=[&](uint32_t at,std::span<const uint8_t>input){for(size_t n=0;n<input.size();++n){const auto base=(at+uint32_t(n))&~(ss::kMemoryChunk-1);auto&b=saved[base];if(b.empty())b.resize(ss::kMemoryChunk);b[at+uint32_t(n)-base]=input[n];}};
 for(const auto&[at,b]:f.memory)copy(at,b);copy(arena_at,std::span(mem::ptr(arena_at),story_native_event_arena::kBytes));
 std::vector<ss::MemoryRegion>regions;ss::MemoryChunks chunks;for(auto&[at,b]:saved){chunks.emplace(at,b.data());if(!regions.empty()&&regions.back().base+regions.back().size==at)regions.back().size+=ss::kMemoryChunk;else regions.push_back({at,ss::kMemoryChunk});}
 auto&bytes=saved.at(arena_at);ss::Writer alloc;alloc.u32(mem::runtime_top());const auto records=mem::runtime_alloc_log();alloc.u32(uint32_t(records.size()));for(const auto&r:records)alloc.pod(r);
 auto view=[&]{return ss::native_gate::SavedView{ss::Reader(alloc.b.data(),alloc.b.size()),regions,chunks};};
 assert(checkpoint->matches_saved(view())&&checkpoint->journal()==session.journal());bytes[0x300]=1;assert(!checkpoint->matches_saved(view()));bytes[0x300]=0;assert(checkpoint->matches_saved(view()));
 if(mode==13){const auto at=play+0x52C4;const auto base=at&~(ss::kMemoryChunk-1);saved.at(base)[at-base]^=1;assert(!checkpoint->matches_saved(view()));saved.at(base)[at-base]^=1;const auto cam=cam_process+0x248+0x8A8;const auto cb=cam&~(ss::kMemoryChunk-1);saved.at(cb)[cam-cb]^=1;assert(!checkpoint->matches_saved(view()));continue;}
 if(mode==9){hold.reset();auto abandoned=pristine_prepare(session,*checkpoint,view(),&cpu,witness,1);assert(abandoned);abandoned.reset();hold=session.try_fence();assert(hold&&session.quarantined()&&!session.expected());continue;}
 if(mode==10){session.revoke_unexposed();hold.reset();assert(!pristine_prepare(session,*checkpoint,view(),&cpu,witness,1));hold=session.try_fence();assert(hold&&!session.unexposed());continue;}
 if(mode==7||mode==0){hold.reset();auto restore=pristine_prepare(session,*checkpoint,view(),&cpu,witness,1);assert(restore&&!session.expected());assert(pristine_adopt(session,std::move(*restore),view(),&cpu,witness,&hold));assert(hold&&!session.unexposed()&&!session.expected());
#ifdef STORY_NATIVE_PRISTINE_STAGE_TEST
 if(mode==0)assert(!PO::snapshot());else assert(PO::snapshot());
#endif

 auto descendant=pristine_begin(session,&cpu,witness);assert(descendant);auto next=pristine_commit(session,std::move(*descendant),&cpu,witness);assert(next&&next->matches_saved(view()));hold.reset();assert(!pristine_prepare(session,*checkpoint,view(),&cpu,witness,1));
 auto repeat=pristine_prepare(session,*checkpoint,view(),&cpu,witness,2);assert(repeat&&pristine_adopt(session,std::move(*repeat),view(),&cpu,witness,&hold));assert(hold&&!session.unexposed());hold.reset();assert(!session.inspect(&cpu));pending_native=false;active_scene=false;assert(session.inspect(&cpu));hold=session.try_fence();assert(hold&&session.stage_image());assert(!pristine_begin(session,&cpu,witness));}

 }}}
