// SPDX-License-Identifier: MPL-2.0
#include "story_native_turn_checkpoint.h"
#include "story_native_turn_camera_observation.h"
#include <algorithm>
namespace story_native_turn_checkpoint {namespace {
bool range(Range r){return r.address&&r.bytes&&uint64_t(r.address)+r.bytes<=0x100000000ULL;}
bool overlap(Range a,Range b){return uint64_t(a.address)<uint64_t(b.address)+b.bytes&&uint64_t(b.address)<uint64_t(a.address)+a.bytes;}
uint32_t word(std::span<const uint8_t>b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
}
Result saved(Transaction t,const story_native_turn_record::Record&authorized,const Layout&l,const std::array<uint8_t,64>&stamp,std::span<const Region>regions){
 auto fail=[](Reason r){return Result{false,r};};using P=story_native_event_lease::Phase;
 if(t!=Transaction::Stable||(authorized.phase!=P::Published&&authorized.phase!=P::Running))return fail(Reason::Transaction);
 const auto&e=authorized.expected;Range image{e.identity.image,e.identity.bytes};
 if(uint64_t(e.identity.manager)+0x3C+1280>0x100000000ULL||uint64_t(l.flags.address)!=uint64_t(e.identity.manager)+0x3C)return fail(Reason::Layout);
 const std::array<Range,5> spans={l.record,l.stamp,l.depth,l.flags,image};
 if(l.record.bytes<20||l.record.bytes>story_native_turn_record::kMaximumBytes||l.stamp.bytes!=64||l.depth.bytes!=96||l.flags.bytes!=1280||image.bytes!=0xE60||(image.address&15))return fail(Reason::Layout);
 for(unsigned n=0;n<spans.size();++n){if(!range(spans[n])||(spans[n].address&3))return fail(Reason::Layout);for(unsigned k=0;k<n;++k)if(overlap(spans[n],spans[k]))return fail(Reason::Layout);}
 if(regions.empty()||regions.size()>65536)return fail(Reason::Regions);
 std::vector<Region>sorted(regions.begin(),regions.end());std::sort(sorted.begin(),sorted.end(),[](auto&a,auto&b){return a.address<b.address;});
 uint64_t end=0;for(const auto&r:sorted){if(r.bytes.empty()||r.bytes.size()>0x100000000ULL||!r.address||uint64_t(r.address)+r.bytes.size()>0x100000000ULL||r.address<end)return fail(Reason::Regions);end=uint64_t(r.address)+r.bytes.size();}
 auto copy=[&](Range span,std::span<uint8_t>out){uint64_t at=span.address,stop=at+span.bytes;size_t n=0;for(const auto&r:sorted){auto end=uint64_t(r.address)+r.bytes.size();if(end<=at)continue;if(r.address>at)return false;size_t take=size_t(std::min(stop,end)-at);std::copy_n(r.bytes.begin()+size_t(at-r.address),take,out.begin()+n);n+=take;at+=take;if(at==stop)return true;}return false;};
 std::vector<uint8_t>record(l.record.bytes);std::array<uint8_t,64>got_stamp{};story_native_event_depth::Record depth{};std::array<uint8_t,1280>flags{};
 story_native_turn_camera_observation::Capture c;c.identity=e.identity;
 if(!copy(l.record,record)||!copy(l.stamp,got_stamp)||!copy(l.depth,depth)||!copy(l.flags,flags)||!copy(image,c.image))return fail(Reason::Missing);
 story_native_turn_record::Record decoded;std::vector<uint8_t>expected_record;
 if(!story_native_turn_record::decode(record,decoded)||!story_native_turn_record::encode(authorized,expected_record)||record!=expected_record)return fail(Reason::Record);
 if(got_stamp!=stamp)return fail(Reason::Stamp);
 if(!story_native_event_depth::Tracker::valid_idle_record(depth,e))return fail(Reason::Depth);
 // Before native startProc, these are the untouched retail scratch flags,
 // including any bits that happen to share this program's completion indices.
 // startProc clears all 320 words; a running snapshot may contain only this
 // program's flags. The original retail backup stays in the authorized record.
 const bool native_started=word(c.image,0xE4)>=2;
 c.completion_flags=native_started?(word(flags,0)&0x1FFu):0;
 if(c.completion_flags!=decoded.completion_flags)return fail(Reason::Flags);
 if(!native_started){if(flags!=e.retail_flags)return fail(Reason::Flags);}
 else for(size_t at=0;at<flags.size();++at){const uint8_t mask=at==2?1:at==3?0xFF:0;if(flags[at]&~mask)return fail(Reason::Flags);}
 c.elapsed_ticks=decoded.elapsed_ticks;c.exclusive=true;
 const auto phase=decoded.phase==P::Published?story_native_turn_camera_observation::Phase::Published:story_native_turn_camera_observation::Phase::Running;
 if(!story_native_turn_camera_observation::validate(phase,e.identity,decoded.program,c,decoded.watchdog_ticks).valid)return fail(Reason::Image);
 return {true,Reason::None};
}
}
