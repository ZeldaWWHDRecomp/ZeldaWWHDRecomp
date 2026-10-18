// SPDX-License-Identifier: MPL-2.0
#include "story_native_model_saved_graph.h"
#include <algorithm>
namespace story_native_model_saved_graph {namespace {
constexpr size_t MaxRanges=4096,MaxBytes=32*1024*1024;
bool valid(Range r){return r.address&&r.bytes&&r.bytes<=MaxBytes&&uint64_t(r.address)+r.bytes<=0x100000000ull;}
bool normalize(const Trace&t,std::vector<Range>&out){
 if(t.reads.empty()||t.writes.empty()||t.reads.size()+t.writes.size()>MaxRanges)return false;
 out=t.reads;out.insert(out.end(),t.writes.begin(),t.writes.end());for(auto r:out)if(!valid(r))return false;
 std::sort(out.begin(),out.end(),[](auto a,auto b){return a.address<b.address;});
 std::vector<Range>merged;for(auto r:out){if(!merged.empty()&&r.address<=uint64_t(merged.back().address)+merged.back().bytes){auto&b=merged.back();const auto end=std::max(uint64_t(b.address)+b.bytes,uint64_t(r.address)+r.bytes);if(end-b.address>MaxBytes)return false;b.bytes=uint32_t(end-b.address);}else merged.push_back(r);}
 size_t n=0;for(auto r:merged){n+=r.bytes;if(n>MaxBytes)return false;}out=std::move(merged);return true;
}
}
Lineage Lineage::issue(){return Lineage(std::make_shared<Identity>());}
Receipt::Receipt(story_native_event_observation::Expected e,Lineage l,std::vector<Bytes>b):expected_(std::move(e)),lineage_(std::move(l)),bytes_(std::move(b)){}
std::optional<Receipt>Receipt::capture(const story_native_event_observation::Expected&e,const Lineage&l,const Trace&t,const story_native_event_memory::Read&read){try{
 if(!l.valid()||!read||!e.identity.image||!e.identity.generation)return {};
 std::vector<Range>ranges;if(!normalize(t,ranges))return {};std::vector<Bytes>bytes;
 for(auto r:ranges){Bytes b{r.address,std::vector<uint8_t>(r.bytes)};if(!read(r.address,b.bytes))return {};bytes.push_back(std::move(b));}
 Receipt result(e,l,std::move(bytes));if(!result.matches_live(read))return {};return result;
 }catch(...){return {};}}
bool Receipt::matches(const story_native_event_observation::Expected&e,const Lineage&l)const noexcept{return lineage_.valid()&&l.valid()&&lineage_.identity_==l.identity_&&expected_==e&&!bytes_.empty();}
size_t Receipt::byte_count()const noexcept{size_t n=0;for(const auto&b:bytes_)n+=b.bytes.size();return n;}
bool Receipt::matches_live(const story_native_event_memory::Read&read)const{try{
 if(!lineage_.valid()||bytes_.empty()||!read)return false;
 // Two complete samples reject changes spanning disjoint graph dependencies.
 for(unsigned sample=0;sample<2;++sample)for(const auto&b:bytes_){std::vector<uint8_t>v(b.bytes.size());if(!read(b.address,v)||v!=b.bytes)return false;}return true;
 }catch(...){return false;}}
bool Receipt::matches_saved(const ss::native_gate::SavedView&v)const{try{
 if(!lineage_.valid()||bytes_.empty()||v.regions.empty()||v.regions.size()>64)return false;
 for(size_t n=0;n<v.regions.size();++n){const auto&r=v.regions[n];if(!r.size||(r.base&(ss::kMemoryChunk-1))||(r.size&(ss::kMemoryChunk-1))||uint64_t(r.base)+r.size>0x100000000ull)return false;for(size_t j=0;j<n;++j){const auto&o=v.regions[j];if(uint64_t(r.base)<uint64_t(o.base)+o.size&&uint64_t(o.base)<uint64_t(r.base)+r.size)return false;}}
 auto covered=[&](uint32_t p,size_t n){return std::any_of(v.regions.begin(),v.regions.end(),[&](const auto&r){return p>=r.base&&uint64_t(p)+n<=uint64_t(r.base)+r.size;});};
 for(const auto&[at,data]:v.chunks)if(!data||(at&(ss::kMemoryChunk-1))||!covered(at,ss::kMemoryChunk))return false;
 for(const auto&b:bytes_){if(!covered(b.address,b.bytes.size()))return false;for(size_t off=0;off<b.bytes.size();){const uint32_t at=b.address+uint32_t(off),base=at&~(ss::kMemoryChunk-1);const size_t n=std::min(b.bytes.size()-off,size_t(ss::kMemoryChunk-(at-base)));const auto chunk=v.chunks.find(base);if(chunk==v.chunks.end()||!std::equal(b.bytes.begin()+off,b.bytes.begin()+off+n,chunk->second+(at-base)))return false;off+=n;}}
 return true;
 }catch(...){return false;}}
}
