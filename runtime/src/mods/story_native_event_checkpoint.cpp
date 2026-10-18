// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_checkpoint.h"
#include "story_native_event_record.h"
#include <algorithm>
namespace story_native_event_checkpoint {namespace {
using Phase=story_native_event_lease::Phase;
bool stable(Phase p){return p==Phase::Published||p==Phase::Running;}
bool range(Range r){return r.address&&r.bytes&&uint64_t(r.address)+r.bytes<=0x100000000ull;}
bool overlaps(Range a,Range b){return uint64_t(a.address)<uint64_t(b.address)+b.bytes&&uint64_t(b.address)<uint64_t(a.address)+a.bytes;}
uint32_t word(std::span<const uint8_t>b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
}
Result capture(bool quiescent,Transaction transaction,Phase phase,const story_native_event_observation::Expected&e,const story_native_event_depth::Record&depth){
 if(!quiescent)return {false,Reason::Quiescence};if(transaction!=Transaction::Stable||!stable(phase))return {false,Reason::Transaction};
 if(!story_native_event_depth::Tracker::valid_idle_record(depth,e))return {false,Reason::Depth};return {true,Reason::None,phase};
}
Result saved(Transaction transaction,const story_native_event_observation::Expected&e,const Layout&layout,const std::array<uint8_t,64>&stamp,std::span<const Region>regions){
 auto fail=[](Reason r){return Result{false,r};};if(transaction!=Transaction::Stable)return fail(Reason::Transaction);
 const Range image{e.identity.image,e.identity.bytes};const std::array<Range,4> allocations={layout.record,layout.stamp,layout.depth,image};
 if(layout.record.bytes<20||layout.record.bytes>story_native_event_record::kMaximumBytes||layout.stamp.bytes!=64||layout.depth.bytes!=96||image.bytes!=0x520||(image.address&15))return fail(Reason::Layout);
 for(unsigned a=0;a<allocations.size();++a){if(!range(allocations[a])||(allocations[a].address&3))return fail(Reason::Layout);for(unsigned b=0;b<a;++b)if(overlaps(allocations[a],allocations[b]))return fail(Reason::Layout);}
 if(regions.empty()||regions.size()>65536)return fail(Reason::Regions);
 std::vector<Region> ordered(regions.begin(),regions.end());std::sort(ordered.begin(),ordered.end(),[](auto&a,auto&b){return a.address<b.address;});
 uint64_t end=0;for(const auto&r:ordered){if(r.bytes.empty()||r.bytes.size()>0x100000000ull||!r.address||uint64_t(r.address)+r.bytes.size()>0x100000000ull||r.address<end)return fail(Reason::Regions);end=uint64_t(r.address)+r.bytes.size();}
 auto copy=[&](Range wanted,std::span<uint8_t>out){uint64_t at=wanted.address,stop=at+wanted.bytes;size_t n=0;for(const auto&r:ordered){uint64_t finish=uint64_t(r.address)+r.bytes.size();if(finish<=at)continue;if(r.address>at)return false;const size_t take=size_t(std::min(stop,finish)-at);std::copy_n(r.bytes.begin()+size_t(at-r.address),take,out.begin()+n);n+=take;at+=take;if(at==stop)return true;}return false;};
 std::vector<uint8_t> record(layout.record.bytes);std::array<uint8_t,64> got_stamp{};story_native_event_depth::Record depth{};std::array<uint8_t,0x520> image_bytes{};
 // Establish complete provenance for ALL spans before parsing any payload.
 if(!copy(layout.record,record)||!copy(layout.stamp,got_stamp)||!copy(layout.depth,depth)||!copy(image,image_bytes))return fail(Reason::Missing);
 story_native_event_observation::Expected decoded;Phase phase;if(!story_native_event_record::decode(record,decoded,phase)||decoded!=e)return fail(Reason::Record);
 if(got_stamp!=stamp)return fail(Reason::Stamp);
 const auto checkpoint=capture(true,transaction,phase,e,depth);if(!checkpoint.valid)return checkpoint;
 // Minimal stable publication consistency; full mutable WAIT grammar/consumer
 // identity is deliberately delegated to the independent live collector.
 const auto state=word(image_bytes,0xE4);if((phase==Phase::Published&&state!=1)||(phase==Phase::Running&&state!=2&&state!=4))return fail(Reason::Image);
 return checkpoint;
}
}
