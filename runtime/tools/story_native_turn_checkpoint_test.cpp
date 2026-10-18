// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_turn_record.h"
#include "mods/story_native_turn_checkpoint.h"
#include "mods/story_native_event_depth.h"
#include <cassert>
#include <limits>
#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
#include <utility>
namespace T=story_native_turn_record;namespace L=story_native_event_lease;
void put(auto&b,size_t p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-n*8));}
int main(){T::Record v;auto&e=v.expected;e.identity={0x20000000,0x60000000,0xE60,1,2,3,4};e.stage_name={'s','e','a'};e.room_number=11;
 e.camera.address=0x20003248;e.camera.pid=12;e.camera.event_type=3;e.camera.type_table=0x10049350;e.camera.type_count=63;e.camera.style_table=e.camera.param_pointer=0x1004487C;e.camera.style_count=145;put(e.camera.event_data,12,~0u);
 for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x20001000+n*0x1000;a.pid=10+n;a.token=1+n;a.profile=0x175;a.subtype=1;for(unsigned k=0;k<32;++k)a.fingerprint[k]=uint8_t(k*7+n+1);auto&s=v.poses[n];s.identity={a.address,a.pid,a.token,1,2,3,0x30000000+n*0x1000,0x31000000+n*0x1000};s.angles[2]=uint8_t(3+n);s.joint[0]=uint8_t(9+n);s.status=0xA000+s.identity.pid;s.target_mode=1;}

 auto pattern=[](auto&array,unsigned seed){for(size_t k=0;k<array.size();++k)array[k]=uint8_t(k*29+seed);};
 auto&c=e.camera;pattern(c.type_gate518,1);pattern(c.engine100,2);pattern(c.mode_phase13C,3);pattern(c.initial_view010,4);pattern(c.done100,5);pattern(c.work37C,6);pattern(c.positions0A4,7);pattern(c.view03C,8);pattern(c.event_data,9);
 for(unsigned k=0;k<4;++k){c.mode_phase13C[k]=0;c.mode_phase13C[4+k]=0;c.event_data[0x0C+k]=0xFF;}
 pattern(e.retail_flags,11);for(unsigned k=0;k<8;++k)e.retail_manager[k]=0x12000000+k*4;e.retail_camera_play=19;e.retail_current_event=27;
 for(unsigned n=0;n<2;++n){auto&s=v.poses[n];pattern(s.joint,30+n);pattern(s.target,40+n);pattern(s.velocity,50+n);s.head_only=1;s.noticed=1;s.talk_ready=1;}
 v.program.camera.start_eye={0,100,200};v.program.camera.start_center={0,30,0};v.program.camera.end_eye={100,100,200};v.program.camera.end_center={100,30,0};v.program.actor_a_target={100,150,100};v.program.actor_b_target={-100,150,100};v.phase=L::Phase::Running;v.elapsed_ticks=17;v.completion_flags=128;v.watchdog_ticks=900;

 namespace C=story_native_turn_checkpoint;namespace D=story_native_event_depth;
 v.phase=L::Phase::Published;v.completion_flags=0;
 std::vector<uint8_t>b;assert(T::encode(v,b));std::vector<uint8_t>image;std::string why;assert(story_native_turn_camera_image::make(v.program,image,why));put(image,0xE4,1);
 D::Coverage coverage{{11,11,true},{0,0,true},{22,22,true},true};D::Tracker tracker(e,coverage);auto depth=tracker.record();
 std::array<uint8_t,64>stamp{};stamp[0]=0x54;auto flags=e.retail_flags;
 C::Layout layout{{0x60002000,uint32_t(b.size())},{0x60007000,64},{0x60007100,96},{e.identity.manager+0x3C,1280}};
 std::vector<C::Region>regions={{layout.record.address,b},{layout.stamp.address,stamp},{layout.depth.address,depth},{layout.flags.address,flags},{e.identity.image,image}};
 auto check=[&](const auto&rs){return C::saved(C::Transaction::Stable,v,layout,stamp,rs);};assert(check(std::span<const C::Region>(regions)).valid);
 auto split=regions;split.pop_back();split.push_back({e.identity.image,std::span(image).first(123)});split.push_back({e.identity.image+123,std::span(image).subspan(123)});assert(check(std::span<const C::Region>(split)).valid);split.back().address++;assert(!check(std::span<const C::Region>(split)).valid);
 for(unsigned n=0;n<5;++n){auto missing=regions;missing.erase(missing.begin()+n);assert(!check(std::span<const C::Region>(missing)).valid);}
 auto overlap=regions;overlap.push_back(regions[0]);assert(!check(std::span<const C::Region>(overlap)).valid);
 auto swapped=v;std::swap(swapped.expected.actors[0].fingerprint,swapped.expected.actors[1].fingerprint);assert(!C::saved(C::Transaction::Stable,swapped,layout,stamp,regions).valid);
 auto config=v;config.expected.actors[0].fingerprint[31]^=1;assert(!C::saved(C::Transaction::Stable,config,layout,stamp,regions).valid);
 auto wrong=v;wrong.expected.identity.generation++;assert(!C::saved(C::Transaction::Stable,wrong,layout,stamp,regions).valid);
 auto badstamp=stamp;badstamp[7]++;assert(!C::saved(C::Transaction::Stable,v,layout,badstamp,regions).valid);
 flags[3]^=1;assert(!check(std::span<const C::Region>(regions)).valid);flags[3]^=1;flags[6]^=1;assert(!check(std::span<const C::Region>(regions)).valid);flags[6]^=1;
 image[0xD90]^=1;assert(!check(std::span<const C::Region>(regions)).valid);image[0xD90]^=1;
 depth[95]^=1;assert(!check(std::span<const C::Region>(regions)).valid);depth[95]^=1;
 assert(!C::saved(C::Transaction::Detaching,v,layout,stamp,regions).valid);
 // Arena record span is a 0x4000 capacity, NOT the encoded length.
 auto wrong_flags=layout;wrong_flags.flags.address+=4;assert(!C::saved(C::Transaction::Stable,v,wrong_flags,stamp,regions).valid);
 auto overflow=v;overflow.expected.identity.manager=0xFFFFFFF0;auto overflow_layout=layout;overflow_layout.flags.address=0x2C;assert(!C::saved(C::Transaction::Stable,overflow,overflow_layout,stamp,regions).valid);
 auto capacity_layout=layout;capacity_layout.record.bytes=0x4000;
 assert(!C::saved(C::Transaction::Stable,v,capacity_layout,stamp,regions).valid);
 auto padded=b;padded.resize(0x4000);auto padded_regions=regions;padded_regions[0].bytes=padded;
 assert(!C::saved(C::Transaction::Stable,v,capacity_layout,stamp,padded_regions).valid);
 assert(check(std::span<const C::Region>(regions)).valid); // exact saved header length succeeds
 // Running grammar validates actual native staff cuts/actions, not just state.
 v.phase=L::Phase::Running;assert(T::encode(v,b));regions[0].bytes=b;put(image,0xE4,2);
 for(unsigned n=0;n<4;++n){put(image,0xF0+n*80+0x38,n<2?7+n:n);put(image,0xF0+n*80+0x3C,n<2?3:n==2?4:0);for(unsigned k=0x40;k<0x48;++k)image[0xF0+n*80+k]=0;}
 // A pre-start retail backup is not a valid running scratch array.
 assert(check(std::span<const C::Region>(regions)).reason==C::Reason::Flags);
 flags.fill(0);assert(check(std::span<const C::Region>(regions)).valid);
 // Every unrelated flag was cleared by the genuine native start. Reintroducing
 // even one retail bit must fail; the saved backup itself remains unchanged.
 flags[6]=e.retail_flags[6];assert(check(std::span<const C::Region>(regions)).reason==C::Reason::Flags);flags[6]=0;
 flags[2]=2;assert(check(std::span<const C::Region>(regions)).reason==C::Reason::Flags);flags[2]=0;
 flags[3]=1;assert(check(std::span<const C::Region>(regions)).reason==C::Reason::Flags);flags[3]=0;
 assert(check(std::span<const C::Region>(regions)).valid);put(image,0xF0+0x3C,4);assert(!check(std::span<const C::Region>(regions)).valid);
}
