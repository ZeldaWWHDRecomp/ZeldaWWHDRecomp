// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_turn_record.h"
#include "mods/story_native_turn_checkpoint.h"
#include "mods/story_native_event_depth.h"
#include "mods/story_native_event_saved_flags.h"
#include "mods/story_native_event_arena.h"
#include <cassert>
#include <limits>
#include <algorithm>
#include <string>
#include <array>
#include <vector>
#include <cstdint>
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


 namespace C=story_native_turn_checkpoint;namespace D=story_native_event_depth;namespace A=story_native_event_arena;namespace F=story_native_event_saved_flags;
 auto arena=A::make(0x60000000);e.identity.image=arena.turn.image.address;e.identity.manager=0x2000FFC0;
 v.phase=L::Phase::Published;v.completion_flags=0;
 std::vector<uint8_t>record;assert(T::encode(v,record));std::vector<uint8_t>image;std::string why;assert(story_native_turn_camera_image::make(v.program,image,why));put(image,0xE4,1);
 D::Tracker tracker(e,{{11,11,true},{0,0,true},{22,22,true},true});auto depth=tracker.record();std::array<uint8_t,64>stamp{};stamp[0]=0x54;
 // ORDER1 precedes the genuine native start, so even owned flag bits retain
 // their retail baseline. Native start, not the host record, clears the flags.
 auto flags=e.retail_flags;
 std::vector<uint8_t>runtime(0x10000),native(0x20000);
 auto copy=[&](uint32_t at,const auto&bytes){auto*dest=at>=0x60000000?runtime.data()+at-0x60000000:native.data()+at-0x20000000;std::copy(bytes.begin(),bytes.end(),dest);};
 copy(arena.turn.image.address,image);copy(arena.turn.record.address,record);copy(arena.turn.stamp.address,stamp);copy(arena.turn.depth.address,depth);copy(e.identity.manager+0x3C,flags);
 std::vector<ss::MemoryRegion>regions={{0x20000000,0x20000},{0x60000000,0x10000}};ss::Writer w;
 ss::capture_regions(w,regions,[&](uint32_t at){return at>=0x60000000?runtime.data()+at-0x60000000:native.data()+at-0x20000000;},[](auto,size_t){return true;});
 ss::Reader reader(w.b.data(),w.b.size());std::vector<ss::MemoryRegion>parsed;ss::MemoryChunks chunks;assert(ss::read_regions(reader,parsed,chunks));
 F::Flags got_flags;assert(F::read(e.identity.manager,parsed,chunks,got_flags)&&got_flags==flags); // crosses native64KiB boundary
 std::vector<uint8_t>got_record(record.size()),got_image(image.size());std::array<uint8_t,64>got_stamp{};D::Record got_depth{};
 const auto top=arena.allocation.address+A::kBytes;
 ss::Writer allocations;allocations.u32(top);allocations.u32(1);allocations.pod(mem::AllocRec{arena.allocation.address,A::kBytes,123});
 auto allocation_view=[&](){return ss::Reader(allocations.b.data(),allocations.b.size());};
 assert(A::read_saved(arena,allocation_view(),parsed,chunks,arena.turn.record.address,got_record));assert(A::read_saved(arena,allocation_view(),parsed,chunks,arena.turn.image.address,got_image));assert(A::read_saved(arena,allocation_view(),parsed,chunks,arena.turn.stamp.address,got_stamp));assert(A::read_saved(arena,allocation_view(),parsed,chunks,arena.turn.depth.address,got_depth));
 C::Layout layout{{arena.turn.record.address,uint32_t(record.size())},{arena.turn.stamp.address,64},{arena.turn.depth.address,96},{e.identity.manager+0x3C,1280}};
 std::vector<C::Region>views={{layout.record.address,got_record},{layout.stamp.address,got_stamp},{layout.depth.address,got_depth},{layout.flags.address,got_flags},{e.identity.image,got_image}};
 assert(C::saved(C::Transaction::Stable,v,layout,stamp,views).valid);
 put(got_flags,0,(uint32_t(got_flags[0])<<24|uint32_t(got_flags[1])<<16|uint32_t(got_flags[2])<<8|got_flags[3])&~0x1FFu);
 assert(!C::saved(C::Transaction::Stable,v,layout,stamp,views).valid);
 got_flags=flags;assert(C::saved(C::Transaction::Stable,v,layout,stamp,views).valid);
 got_flags.fill(0xAA);assert(!F::read(0x68000000,parsed,chunks,got_flags)&&got_flags[0]==0xAA);assert(!F::read(e.identity.manager+1,parsed,chunks,got_flags));
 auto missing=parsed;missing.erase(missing.begin());assert(!F::read(e.identity.manager,missing,chunks,got_flags)&&got_flags[0]==0xAA);
 auto overlap=parsed;overlap.push_back(parsed[0]);assert(!F::read(e.identity.manager,overlap,chunks,got_flags));
 auto null=chunks;null[0x20010000]=nullptr;assert(!F::read(e.identity.manager,parsed,null,got_flags)&&got_flags[0]==0xAA);
 ss::MemoryChunks sparse;assert(F::read(e.identity.manager,parsed,sparse,got_flags)&&got_flags==F::Flags{});
 assert(!A::read_saved(arena,allocation_view(),parsed,chunks,e.identity.manager+0x3C,got_flags)); // arena cannot authorize MEM2flags
 ss::Writer too_early;too_early.u32(top-1);too_early.u32(1);too_early.pod(mem::AllocRec{arena.allocation.address,A::kBytes,123});
 assert(!A::read_saved(arena,ss::Reader(too_early.b.data(),too_early.b.size()),parsed,chunks,arena.turn.record.address,got_record));
}
