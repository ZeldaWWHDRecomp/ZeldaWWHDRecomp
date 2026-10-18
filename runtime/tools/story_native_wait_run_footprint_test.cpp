// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_wait_run_footprint.h"
#include "mods/story_native_wait_runtime_controller.h"
#include <cassert>
#include <map>
#include <type_traits>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <span>
using namespace story_native_wait_run_footprint;
namespace A=story_native_event_arena;
namespace RC=story_native_wait_runtime_controller;
static_assert(!std::is_default_constructible_v<RC::SavedRunReceipt>);
static_assert(!std::is_copy_constructible_v<RC::SavedRunReceipt>);
static_assert(!std::is_default_constructible_v<RC::RunRestorePermit>);
static_assert(!std::is_copy_constructible_v<RC::RunRestorePermit>);
struct Fixture {
 A::Layout arena=A::make(0x60010000);
 story_native_event_observation::Expected expected;
 story_native_people_pose_record::Poses poses;
 story_native_camera_policy::Tables tables{0x10049348,0x10049350,0x10044878,0x1004487C};
 std::map<uint32_t,std::vector<uint8_t>>memory;
 std::vector<ss::MemoryRegion>regions;ss::MemoryChunks chunks;ss::Writer allocations;
 Fixture(){
  expected.identity={0x200020F4,arena.wait.image.address,0x520,12,13,4,123};
  expected.camera.address=0x21000248;expected.camera.type_table=tables.types;expected.camera.style_table=tables.styles;expected.camera.type_count=63;expected.camera.style_count=145;
  for(unsigned n=0;n<2;++n){auto&a=expected.actors[n];a.address=0x30000000+n*0x10000;a.pid=90+n;a.token=7+n;poses[n].identity={a.address,a.pid,a.token,12,13,4,0x40000000+n*0x10000,0x40001000+n*0x10000};}
  for(uint32_t at:{0x60010000u,0x20000000u,0x21000000u,0x30000000u,0x30010000u,0x40000000u,0x40010000u,0x10040000u}){
   auto&b=memory[at];b.resize(ss::kMemoryChunk);for(size_t n=0;n<b.size();++n)b[n]=uint8_t(n*31+(at>>16));regions.push_back({at,ss::kMemoryChunk});chunks.emplace(at,b.data());
  }
  allocations.u32(arena.allocation.address+A::kBytes);allocations.u32(1);allocations.pod(mem::AllocRec{arena.allocation.address,A::kBytes,123});
 }
 bool read(uint32_t at,std::span<uint8_t>b){for(const auto&[base,bytes]:memory)if(at>=base&&uint64_t(at)+b.size()<=uint64_t(base)+bytes.size()){std::copy_n(bytes.begin()+at-base,b.size(),b.begin());return true;}return false;}
 auto reader(){return [this](uint32_t a,std::span<uint8_t>b){return read(a,b);};}
 auto view(){return ss::native_gate::SavedView{ss::Reader{allocations.b.data(),allocations.b.size()},regions,chunks};}
};
int main(){
 Fixture f;Footprint captured;assert(capture(f.reader(),f.arena,f.expected,f.poses,f.tables,captured)&&captured.size()==13);
 assert(matches_live(f.reader(),captured)&&matches_saved(f.arena,f.view(),captured));
 for(const auto&pose:f.poses){const auto model=std::find_if(captured.begin(),captured.end(),[&](const auto&b){return b.address==pose.identity.model;});assert(model!=captured.end()&&model->bytes.size()==0x170);auto&last=f.memory.at(pose.identity.model&~(ss::kMemoryChunk-1))[(pose.identity.model&(ss::kMemoryChunk-1))+0x16F];last^=1;assert(!matches_saved(f.arena,f.view(),captured));last^=1;}

 // Every captured native span is exact. No edited native actor/camera/image or
 // table can acquire the opaque host receipt merely by decoding saved bytes.
 for(const auto&b:captured){const uint32_t chunk=b.address&~(ss::kMemoryChunk-1);auto&byte=f.memory.at(chunk)[b.address-chunk];byte^=1;assert(!matches_saved(f.arena,f.view(),captured));byte^=1;}
 auto chunks=f.chunks;f.chunks.erase(0x30000000);assert(!matches_saved(f.arena,f.view(),captured));f.chunks=chunks;
 f.chunks[0x30000000]=nullptr;assert(!matches_saved(f.arena,f.view(),captured));f.chunks=chunks;
 f.chunks.emplace(0x22000001,f.memory.begin()->second.data());assert(!matches_saved(f.arena,f.view(),captured));f.chunks=chunks;
 const auto regions=f.regions;f.regions.push_back(f.regions.front());assert(!matches_saved(f.arena,f.view(),captured));f.regions=regions;
 f.regions[0].size++;assert(!matches_saved(f.arena,f.view(),captured));f.regions=regions;
 auto saved_allocations=f.allocations.b;f.allocations.b[12]^=1;assert(!matches_saved(f.arena,f.view(),captured));f.allocations.b=saved_allocations;
 auto wrong=f.arena;wrong=A::make(wrong.allocation.address+0x10000);assert(!matches_saved(wrong,f.view(),captured));
 Footprint malformed=captured;malformed.push_back(captured.front());assert(!matches_live(f.reader(),malformed)&&!matches_saved(f.arena,f.view(),malformed));
 malformed=captured;malformed[0].address=0xFFFFFFF0;assert(!matches_saved(f.arena,f.view(),malformed));
 unsigned first_actor=0;auto changing=[&](uint32_t at,std::span<uint8_t>b){if(at==f.expected.actors[0].address&&++first_actor==2)f.memory.at(at)[3]^=1;return f.read(at,b);};
 Footprint failed=captured;assert(!capture(changing,f.arena,f.expected,f.poses,f.tables,failed)&&failed.empty());f.memory.at(f.expected.actors[0].address)[3]^=1;
 failed=captured;assert(!capture([](uint32_t,std::span<uint8_t>)->bool{throw 1;},f.arena,f.expected,f.poses,f.tables,failed)&&failed.empty());
 auto poses=f.poses;poses[1].identity.model=poses[0].identity.model;assert(!capture(f.reader(),f.arena,f.expected,poses,f.tables,failed)&&failed.empty());
 poses=f.poses;poses[1].identity.pid++;assert(!capture(f.reader(),f.arena,f.expected,poses,f.tables,failed)&&failed.empty());
 // Omitted sparse chunks are explicit saved zeroes, never live fallback.
 for(auto&[at,b]:f.memory)std::fill(b.begin(),b.end(),0);
 assert(capture(f.reader(),f.arena,f.expected,f.poses,f.tables,captured));f.chunks.clear();assert(matches_saved(f.arena,f.view(),captured));
 f.memory.at(0x30000000)[0]=99;assert(matches_saved(f.arena,f.view(),captured)&&!matches_live(f.reader(),captured));
}
