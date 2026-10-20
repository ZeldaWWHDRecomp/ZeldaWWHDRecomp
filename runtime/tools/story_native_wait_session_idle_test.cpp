// SPDX-License-Identifier: MPL-2.0
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>
#include "runtime.h"
#include "mods/story_native_wait_session.h"
#include "native_quiescence.h"
#include "state_memory.h"
#ifdef STORY_NATIVE_IDLE_STAGE_TEST
#include "mods/story_native_wait_idle_snapshot.h"
namespace IdleStage=story_native_stage_owner_observer;
namespace IdleSnapshot=story_native_wait_idle_snapshot;
namespace {
IdleStage::Candidate stage_candidate(){IdleStage::Candidate c;c.info=0x60810000;c.archive_wrapper=0x60811000;c.archive_holder=0x60812000;c.archive_resource=0x60818000;c.archive_base=0x6081F000;c.archive_bytes=0x3000;c.outer_base=0x60820000;c.outer_bytes=0x1000;c.resource=0x60820240;c.resource_bytes=0x80;c.entry=0x60820118;c.archive_key=0x1004DF68;c.resource_key=0x1004DF70;c.request_heap=c.pack_heap=0x60830000;c.data_heap=0x60831000;c.data_heap_parent=0x60832000;c.stage_name[0]='s';return c;}
story_native_setinfo_observation::Result stage_inspect(const Cpu*,const IdleStage::Args&,uint32_t result,IdleStage::Candidate&out){out=stage_candidate();return result==1?story_native_setinfo_observation::Result::Valid:story_native_setinfo_observation::Result::Candidate;}
bool stage_live(uint32_t control,const IdleStage::Candidate&expected,IdleStage::Candidate&out){out=stage_candidate();return control==0x60700000&&expected==out;}
// The real saved graph parser is covered by setinfo_saved_test; this fixture
// isolates genuine Tracker/Observer permit sequencing under the Session fence.
bool stage_saved(const ss::native_gate::SavedView&,uint32_t control,const IdleStage::Candidate&expected,IdleStage::Candidate&out){return stage_live(control,expected,out);}
void issue_stage(){const auto*o=IdleStage::observer();const IdleStage::Args a{{0x60700000,0x60700010,0x60700020,0}};std::lock_guard lock(*o->lifetime_fence);const auto cookie=o->before_setinfo(nullptr,a);assert(cookie);o->after_setinfo(nullptr,a,1,cookie);assert(IdleStage::snapshot());}
}
#endif
namespace threads {
// Isolated primitive fixture. Production witnesses are issued by threads.cpp.
class NativeQuiescenceIssuer {public:static QuiescenceWitness witness(Cpu*c,bool&live){return QuiescenceWitness([c,&live](const Cpu*x){return live&&x==c;});}};
}
static unsigned idle_case;
template<class Fixture,class Session,class Hold>
static void idle_test(Fixture&f,Session&s,Cpu&cpu,Hold&hold){
 bool live=true;auto witness=threads::NativeQuiescenceIssuer::witness(&cpu,live);
 const auto before_progress=s.journal();assert(before_progress);
 Cpu foreign=cpu;
 assert(!s.preserve_progress_load(&foreign,witness));
 live=false;assert(!s.preserve_progress_load(&cpu,witness));live=true;
 assert(s.preserve_progress_load(&cpu,witness)&&s.journal()==before_progress);
 assert(!s.begin_idle_capture(&cpu,witness)); // actual nonrecursive fence held
 [[maybe_unused]]const auto retired_arena=s.arena();hold.reset();
#ifndef STORY_NATIVE_IDLE_STAGE_TEST
 if(idle_case==4)f.idle=false; // Another frontend owner must not invalidate this retired arena.
 if(idle_case==5){f.put(f.e.identity.manager,retired_arena.wait.image.address);{auto proof=s.try_fence();assert(proof&&!s.preserve_progress_load(&cpu,witness));}assert(!s.begin_idle_capture(&cpu,witness));return;}
 if(idle_case==6){f.put(f.e.actors[0].address+0x3E0,retired_arena.wait.image.address);{auto proof=s.try_fence();assert(proof&&!s.preserve_progress_load(&cpu,witness));}assert(!s.begin_idle_capture(&cpu,witness));return;}
#endif
 {auto capture=s.begin_idle_capture(&cpu,witness);assert(capture&&s.fence_held());std::thread other([&]{assert(!s.try_fence());});other.join();}
 assert(!s.fence_held());
#ifdef STORY_NATIVE_IDLE_STAGE_TEST
 auto capture=IdleSnapshot::begin(s,&cpu,witness);assert(capture);
#else
 auto capture=s.begin_idle_capture(&cpu,witness);assert(capture);
#endif
 const auto arena=s.arena();
#ifdef STORY_NATIVE_IDLE_STAGE_TEST
 if(idle_case==4){IdleStage::state_loaded();assert(!IdleSnapshot::commit(std::move(*capture),&cpu,witness));return;}
#endif
 if(idle_case==1){auto*p=mem::ptr(arena.allocation.address+0x200);*p^=1;
#ifdef STORY_NATIVE_IDLE_STAGE_TEST
 assert(!IdleSnapshot::commit(std::move(*capture),&cpu,witness));
#else
 assert(!s.commit_idle_capture(std::move(*capture),&cpu,witness));
#endif
 *p^=1;assert(!s.fence_held());return;}
 if(idle_case==2){live=false;
#ifdef STORY_NATIVE_IDLE_STAGE_TEST
 assert(!IdleSnapshot::commit(std::move(*capture),&cpu,witness));capture.reset();
#else
 assert(!s.commit_idle_capture(std::move(*capture),&cpu,witness));
#endif
 assert(!s.fence_held());return;}
 std::unique_ptr<typename Session::Fence>retained;
#ifdef STORY_NATIVE_IDLE_STAGE_TEST
 auto checkpoint=IdleSnapshot::commit(std::move(*capture),&cpu,witness,&retained);
#else
 auto checkpoint=s.commit_idle_capture(std::move(*capture),&cpu,witness,&retained);
#endif
assert(checkpoint&&retained&&s.fence_held());
 #ifndef STORY_NATIVE_IDLE_STAGE_TEST
 assert(!s.commit_idle_capture(std::move(*capture),&cpu,witness));
#endif
 retained.reset();
 std::map<uint32_t,std::vector<uint8_t>>saved;
 auto copy=[&](uint32_t at,std::span<const uint8_t>b){for(size_t n=0;n<b.size();++n){auto base=(at+uint32_t(n))&~(ss::kMemoryChunk-1);auto&chunk=saved[base];if(chunk.empty())chunk.resize(ss::kMemoryChunk);chunk[at+n-base]=b[n];}};
 for(const auto&[at,b]:f.memory)copy(at,b);
 copy(arena.allocation.address,std::span(mem::ptr(arena.allocation.address),arena.allocation.bytes));
 std::vector<ss::MemoryRegion>regions;ss::MemoryChunks chunks;
 for(auto&[at,b]:saved){chunks.emplace(at,b.data());if(!regions.empty()&&regions.back().base+regions.back().size==at)regions.back().size+=ss::kMemoryChunk;else regions.push_back({at,ss::kMemoryChunk});}
 ss::Writer alloc;alloc.u32(mem::runtime_top());const auto records=mem::runtime_alloc_log();alloc.u32(uint32_t(records.size()));for(const auto&r:records)alloc.pod(r);
 auto view=[&]{return ss::native_gate::SavedView{ss::Reader(alloc.b.data(),alloc.b.size()),regions,chunks};};
 assert(checkpoint->matches_saved(view()));
 saved.at(arena.allocation.address)[0x200]^=1;assert(!checkpoint->matches_saved(view()));saved.at(arena.allocation.address)[0x200]^=1;
#ifdef STORY_NATIVE_IDLE_STAGE_TEST
 using StageGroup=story_native_stage_restore_group::StageRestoreGroup;
 std::shared_ptr<StageGroup>group;std::optional<StageGroup::Participant>sibling;
 if(idle_case==0){group=StageGroup::create();sibling=group->join(*IdleStage::snapshot(),view(),&cpu,witness);assert(sibling);}
 auto pending=IdleSnapshot::prepare(s,*checkpoint,view(),&cpu,witness,1,group);
#else
 auto pending=s.prepare_idle_restore(*checkpoint,view(),&cpu,witness,1);
#endif
assert(pending&&s.fence_held()&&!s.expected()&&!s.unexposed());
 if(idle_case==3){pending.reset();auto fence=s.try_fence();assert(fence&&s.quarantined()&&!s.inspect(&cpu));return;}
#ifdef STORY_NATIVE_IDLE_STAGE_TEST
 if(idle_case==5){assert(!IdleSnapshot::finish(std::move(*pending),view(),&cpu,witness));pending.reset();auto pin=s.try_fence();assert(pin&&s.quarantined());return;}
 IdleStage::state_loaded();std::optional<IdleStage::Snapshot>first;
 if(sibling){first=sibling->finish(view(),&cpu,witness);assert(first);}
 assert(IdleSnapshot::finish(std::move(*pending),view(),&cpu,witness,&retained));assert(IdleStage::snapshot());
 if(first)assert(IdleStage::snapshot()->receipt==first->receipt);
 if(group)group->close();
#else
 assert(s.adopt_idle(std::move(*pending),view(),&cpu,witness,&retained));
#endif
assert(retained&&s.fence_held()&&!s.expected()&&!s.unexposed()&&!s.quarantined());
#ifndef STORY_NATIVE_IDLE_STAGE_TEST
 assert(!s.adopt_idle(std::move(*pending),view(),&cpu,witness));
#endif
 retained.reset();
#ifndef STORY_NATIVE_IDLE_STAGE_TEST
 assert(!s.prepare_idle_restore(*checkpoint,view(),&cpu,witness,1));
#else
 assert(!IdleSnapshot::prepare(s,*checkpoint,view(),&cpu,witness,1));
#endif
#ifdef STORY_NATIVE_IDLE_STAGE_TEST
 if(idle_case==0){const auto prior=IdleStage::snapshot();auto again=IdleSnapshot::prepare(s,*checkpoint,view(),&cpu,witness,2);assert(again);IdleStage::state_loaded();assert(IdleSnapshot::finish(std::move(*again),view(),&cpu,witness));const auto next=IdleStage::snapshot();assert(next&&prior&&next->receipt.owner==prior->receipt.owner&&next->receipt.generation>prior->receipt.generation);}
#endif
 f.idle=true;assert(s.inspect(&cpu));hold=s.try_fence();assert(hold&&s.expected()&&!s.unexposed());
}
#define STORY_NATIVE_SESSION_IDLE_TEST_HOOK(f,s,c,h) do {idle_test(f,s,c,h);h.reset();unmap_arena(guest);return;} while(false)
#define STORY_NATIVE_WAIT_TERMINAL_FIXTURE_ONLY
#include "story_native_wait_controller_terminal_test.cpp"
#undef STORY_NATIVE_WAIT_TERMINAL_FIXTURE_ONLY
#undef STORY_NATIVE_SESSION_IDLE_TEST_HOOK
int main(){
 (void)covered;
 {auto*guest=map_arena();Readiness f;S::Session s(request(),f.source());assert(s.startup());Cpu c{};c.r[1]=0x600300C0;bool live=true;auto w=threads::NativeQuiescenceIssuer::witness(&c,live);assert(!s.begin_idle_capture(&c,w));unmap_arena(guest);}
#ifdef STORY_NATIVE_IDLE_STAGE_TEST
 assert(IdleStage::configure(stage_inspect,stage_live,stage_saved));
 for(idle_case=0;idle_case<6;++idle_case){issue_stage();terminal_run(0);}
#else
 for(idle_case=0;idle_case<7;++idle_case)terminal_run(0);
#endif
}
