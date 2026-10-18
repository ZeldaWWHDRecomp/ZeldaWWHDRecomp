// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_model_saved_graph.h"
#include <cassert>
#include <array>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <span>
#include <utility>
// Standalone primitive issuer fixture ONLY. This target never links the real
// runtime Context, ticket, dispatch, lease or native invocation machinery.
namespace story_native_wait_runtime_model_graph {
class Context {public:
 static auto lineage(){return story_native_model_saved_graph::Lineage::issue();}
 static auto capture(const story_native_event_observation::Expected&e,const story_native_model_saved_graph::Lineage&l,const story_native_model_saved_graph::Trace&t,const story_native_event_memory::Read&r){return story_native_model_saved_graph::Receipt::capture(e,l,t,r);}
};}
int main(){
 namespace S=story_native_model_saved_graph;using Issuer=story_native_wait_runtime_model_graph::Context;
 constexpr uint32_t Base=0x30000000;std::array<uint8_t,ss::kMemoryChunk*2>memory{};for(size_t n=0;n<memory.size();++n)memory[n]=uint8_t(n*17);
 auto read=[&](uint32_t p,std::span<uint8_t>b){if(p<Base||uint64_t(p)+b.size()>uint64_t(Base)+memory.size())return false;std::copy_n(memory.begin()+p-Base,b.size(),b.begin());return true;};
 story_native_event_observation::Expected e;e.identity.image=Base;e.identity.generation=7;
 auto lineage=Issuer::lineage(),other=Issuer::lineage();S::Trace t{{{Base+10,20},{Base+20,40},{Base+ss::kMemoryChunk-4,8}},{{Base+60,10},{Base+80,4}}};
 auto receipt=Issuer::capture(e,lineage,t,read);assert(receipt&&receipt->span_count()==3&&receipt->byte_count()==72&&receipt->matches(e,lineage)&&!receipt->matches(e,other));
 auto changed=e;changed.identity.generation++;assert(!receipt->matches(changed,lineage));changed=e;changed.actors[1].pid++;assert(!receipt->matches(changed,lineage));assert(!receipt->matches(e,S::Lineage{}));
 auto moved=std::move(*receipt);assert(!receipt->matches(e,lineage)&&moved.matches_live(read));
 std::vector<ss::MemoryRegion>regions{{Base,uint32_t(memory.size())}};ss::MemoryChunks chunks;chunks[Base]=memory.data();chunks[Base+ss::kMemoryChunk]=memory.data()+ss::kMemoryChunk;
 ss::native_gate::SavedView view{{},regions,chunks};assert(moved.matches_saved(view));
 memory[80]^=1;assert(!moved.matches_live(read)&&!moved.matches_saved(view));memory[80]^=1;
 chunks.erase(Base+ss::kMemoryChunk);assert(!moved.matches_saved(view));chunks[Base+ss::kMemoryChunk]=memory.data()+ss::kMemoryChunk;
 std::fill(memory.begin(),memory.end(),0);auto zero=Issuer::capture(e,lineage,t,read);assert(zero&&zero->matches_saved(view));chunks.erase(Base);assert(!zero->matches_saved(view));chunks[Base]=memory.data();
 chunks[Base]=nullptr;assert(!zero->matches_saved(view));chunks[Base]=memory.data();
 // SavedView keeps a span of regions: build a new view after each change (push_back reallocates).
 regions.push_back(regions[0]);assert(!zero->matches_saved(ss::native_gate::SavedView{{},regions,chunks}));regions.pop_back();
 regions[0].size=ss::kMemoryChunk;assert(!zero->matches_saved(ss::native_gate::SavedView{{},regions,chunks}));regions[0].size=memory.size();
 S::Trace bad=t;bad.writes.clear();assert(!Issuer::capture(e,lineage,bad,read));bad=t;bad.reads[0]={0xFFFFFFFC,8};assert(!Issuer::capture(e,lineage,bad,read));bad=t;bad.reads[0].bytes=0;assert(!Issuer::capture(e,lineage,bad,read));bad=t;bad.reads.resize(4096,{Base,1});assert(!Issuer::capture(e,lineage,bad,read));
 unsigned reads=0;auto changing=[&](uint32_t p,std::span<uint8_t>b){bool ok=read(p,b);if(++reads==4&&ok)b[0]^=1;return ok;};assert(!Issuer::capture(e,lineage,t,changing));
 auto throwing=[](uint32_t,std::span<uint8_t>)->bool{throw 1;};assert(!Issuer::capture(e,lineage,t,throwing)&&!zero->matches_live(throwing));
}
