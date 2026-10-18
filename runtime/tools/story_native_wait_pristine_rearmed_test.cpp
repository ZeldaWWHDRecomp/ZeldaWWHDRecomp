// SPDX-License-Identifier: MPL-2.0
#include "runtime.h"
#include "mods/story_native_wait_session.h"
#include "native_quiescence.h"
#include "state_memory.h"
#include <cassert>
#include <map>
#include <algorithm>
#include <vector>
#include <optional>
#include <cstdint>
#include <utility>
#include <memory>
namespace threads {
// Isolated scheduler witness; the Session and terminal/rearm lineage are real.
class NativeQuiescenceIssuer {public:static QuiescenceWitness witness(Cpu*c){return QuiescenceWitness([c](const Cpu*x){return x==c;});}};
}
namespace {
std::optional<story_native_wait_session::PristineCheckpoint> pristine;
std::map<uint32_t,std::vector<uint8_t>> original;
template<class F,class S,class H>void before(F&f,S&s,Cpu&cpu,H&){
 auto w=threads::NativeQuiescenceIssuer::witness(&cpu);
 auto cap=s.begin_pristine_capture(&cpu,w);assert(cap);
 pristine=s.commit_pristine_capture(std::move(*cap),&cpu,w);assert(pristine);
 original=f.memory;const auto l=s.arena();original[l.allocation.address]=std::vector<uint8_t>(mem::ptr(l.allocation.address),mem::ptr(l.allocation.address)+l.allocation.bytes);
}
template<class F,class S,class H>void after(F&f,S&s,Cpu&cpu,H&hold){
 auto w=threads::NativeQuiescenceIssuer::witness(&cpu);assert(!s.unexposed()&&s.expected());hold.reset();
 std::map<uint32_t,std::vector<uint8_t>> pages;
 for(const auto&[at,b]:original)for(size_t n=0;n<b.size();++n){const auto base=(at+uint32_t(n))&~(ss::kMemoryChunk-1);auto&v=pages[base];if(v.empty())v.resize(ss::kMemoryChunk);v[at+uint32_t(n)-base]=b[n];}
 std::vector<ss::MemoryRegion>regions;ss::MemoryChunks chunks;for(auto&[at,b]:pages){chunks.emplace(at,b.data());if(!regions.empty()&&regions.back().base+regions.back().size==at)regions.back().size+=ss::kMemoryChunk;else regions.push_back({at,ss::kMemoryChunk});}
 ss::Writer alloc;alloc.u32(mem::runtime_top());const auto records=mem::runtime_alloc_log();alloc.u32(uint32_t(records.size()));for(const auto&r:records)alloc.pod(r);
 ss::native_gate::SavedView view{ss::Reader(alloc.b.data(),alloc.b.size()),regions,chunks};assert(pristine->matches_saved(view));
 // Current Session is genuinely rearmed, not pristine and not a live owner.
 auto pending=s.prepare_pristine_restore(*pristine,view,&cpu,w,1);assert(pending);
 const auto l=s.arena();for(const auto&[at,b]:original){if(at==l.allocation.address)std::copy(b.begin(),b.end(),mem::ptr(at));else {auto*p=f.pointer(at,b.size());assert(p);std::copy(b.begin(),b.end(),p);}}
 std::unique_ptr<typename S::Fence>retained;assert(s.adopt_pristine(std::move(*pending),view,&cpu,w,&retained));assert(retained&&!s.unexposed()&&!s.expected()&&!s.quarantined());
 auto descendant=s.begin_pristine_capture(&cpu,w);assert(descendant&&s.commit_pristine_capture(std::move(*descendant),&cpu,w));retained.reset();pristine.reset();original.clear();
}
}
#define STORY_NATIVE_SESSION_PRISTINE_BEFORE_STAGE(f,s,c,h) before(f,s,c,h)
#define STORY_NATIVE_SESSION_IDLE_TEST_HOOK(f,s,c,h) do {after(f,s,c,h);h.reset();unmap_arena(guest);return;} while(false)
#define STORY_NATIVE_WAIT_TERMINAL_FIXTURE_ONLY
#include "story_native_wait_controller_terminal_test.cpp"
#include <algorithm>
#include <vector>
#include <optional>
#include <cstdint>
#include <utility>
#include <memory>
int main(){(void)covered;terminal_run(0);}
