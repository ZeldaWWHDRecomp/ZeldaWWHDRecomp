// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_checkpoint.h"
#include "mods/story_native_event_record.h"
#include "mods/story_native_event_image.h"
#include <cassert>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
namespace C=story_native_event_checkpoint;namespace D=story_native_event_depth;namespace L=story_native_event_lease;
void put(auto&b,size_t p,uint32_t v){for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(v>>(24-i*8));}
int main(){
 story_native_event_observation::Expected e;e.identity={0x20000000,0x60000000,0x520,1,2,3,4};e.stage_name={'s','e','a'};e.room_number=11;for(unsigned n=0;n<2;++n){e.actors[n].address=0x20001000+n*0x1000;e.actors[n].pid=10+n;e.actors[n].token=1+n;e.actors[n].profile=0x175;e.actors[n].subtype=1;for(unsigned k=0;k<32;++k)e.actors[n].fingerprint[k]=uint8_t(k+n+1);}e.camera.address=0x20003248;e.camera.pid=12;e.camera.event_type=3;e.camera.type_table=0x10049350;e.camera.type_count=63;e.camera.style_table=e.camera.param_pointer=0x1004487C;e.camera.style_count=145;put(e.camera.event_data,12,0xFFFFFFFF);
 D::Coverage coverage{{11,11,true},{0,0,true},{22,22,true},true};D::Tracker tracker(e,coverage);auto depth=tracker.record();assert(C::capture(true,C::Transaction::Stable,L::Phase::Running,e,depth).valid);assert(!C::capture(false,C::Transaction::Stable,L::Phase::Running,e,depth).valid);
 for(auto phase:{L::Phase::Empty,L::Phase::Prepared,L::Phase::Draining,L::Phase::Retired,L::Phase::Quarantined})assert(!C::capture(true,C::Transaction::Stable,phase,e,depth).valid);
 for(auto transaction:{C::Transaction::Publishing,C::Transaction::Invoking,C::Transaction::Draining,C::Transaction::Detaching,C::Transaction::Invalid})assert(!C::capture(true,transaction,L::Phase::Running,e,depth).valid);
 {D::Call call;call.entry=D::Entry::PeopleExecute;call.function=D::entry_address(call.entry);call.receiver=call.subject=e.actors[0].address;call.pid=10;call.token=1;call.identity=e.identity;call.authority=D::Authority::Owned;auto scope=tracker.enter(call);assert(scope.allowed());assert(!C::capture(true,C::Transaction::Stable,L::Phase::Running,e,tracker.record()).valid);}assert(C::capture(true,C::Transaction::Stable,L::Phase::Running,e,tracker.record()).valid);
 assert(tracker.begin_drain());assert(!C::capture(true,C::Transaction::Stable,L::Phase::Running,e,tracker.record()).valid);assert(tracker.cancel_drain());
 std::vector<uint8_t> record;assert(story_native_event_record::encode(e,L::Phase::Running,record));std::array<uint8_t,64> stamp{};stamp[0]=0x4E;auto image=story_native_event_image::make_wait_fixture(e.frames);put(image,0xE4,2);
 C::Layout layout{{0x60001000,uint32_t(record.size())},{0x60002000,64},{0x60002100,96}};
 std::vector<C::Region> regions={{layout.record.address,record},{layout.stamp.address,stamp},{layout.depth.address,depth},{e.identity.image,image}};
 auto check=[&](auto rs){return C::saved(C::Transaction::Stable,e,layout,stamp,rs);};assert(check(std::span<const C::Region>(regions)).valid);
 // Complete provenance may cross adjacent blocks, but never bridge a hole.
 auto split=regions;split.pop_back();split.push_back({e.identity.image,std::span(image).first(123)});split.push_back({e.identity.image+123,std::span(image).subspan(123)});assert(check(std::span<const C::Region>(split)).valid);
 split.back().address++;assert(!check(std::span<const C::Region>(split)).valid);
 for(unsigned n=0;n<4;++n){auto missing=regions;missing.erase(missing.begin()+n);assert(!check(std::span<const C::Region>(missing)).valid);} //no fallback callback exists
 auto overlap=regions;overlap.push_back(regions[0]);assert(!check(std::span<const C::Region>(overlap)).valid);
 auto wrong=e;wrong.identity.generation++;assert(!C::saved(C::Transaction::Stable,wrong,layout,stamp,regions).valid);
 // Independently authorized expected stamp differs from captured bytes.
 auto bad_stamp=stamp;bad_stamp[2]=1;assert(!C::saved(C::Transaction::Stable,e,layout,bad_stamp,regions).valid);
 record[15]^=1;assert(!check(std::span<const C::Region>(regions)).valid);record[15]^=1;
 auto bad_layout=layout;bad_layout.depth.address=e.identity.image;assert(!C::saved(C::Transaction::Stable,e,bad_layout,stamp,regions).valid);
 put(image,0xE4,1);assert(!check(std::span<const C::Region>(regions)).valid);put(image,0xE4,2);
 std::vector<uint8_t> prepared;assert(story_native_event_record::encode(e,L::Phase::Prepared,prepared));auto unsupported=regions;unsupported[0].bytes=prepared;assert(!check(std::span<const C::Region>(unsupported)).valid);
 depth[95]^=1;assert(!check(std::span<const C::Region>(regions)).valid);depth[95]^=1;
}
