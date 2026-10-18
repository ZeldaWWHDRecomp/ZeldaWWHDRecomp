// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_wait_preflight.h"
#include <cassert>
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>

namespace P=story_native_wait_preflight;
namespace A=story_native_event_arena;
namespace J=story_native_wait_journal;

int main() {
 const auto layout=A::make(0x6000F000);
 const uint32_t top=layout.allocation.address+A::kBytes;
 J::Record journal{9,42,layout.allocation.address,J::Phase::Published};
 journal.definition[0]=0xDA;
 std::array<uint8_t,J::kBytes> bytes{};
 assert(J::encode(journal,bytes));

 std::vector<uint8_t> saved_memory(0x20000);
 std::copy(bytes.begin(),bytes.end(),saved_memory.begin()+0xF000);
 ss::Writer memory;
 std::vector<ss::MemoryRegion> source_regions={{0x60000000,0x20000}};
 ss::capture_regions(memory,source_regions,
  [&](uint32_t address){return saved_memory.data()+address-0x60000000;},
  [](const uint8_t*,size_t){return true;});
 ss::Reader memory_reader(memory.b.data(),memory.b.size());
 std::vector<ss::MemoryRegion> regions;
 ss::MemoryChunks chunks;
 assert(ss::read_regions(memory_reader,regions,chunks));
 ss::Writer allocations;
 allocations.u32(top);
 allocations.u32(1);
 allocations.pod(mem::AllocRec{layout.allocation.address,A::kBytes,123});
 auto alloc_view=[&](){return ss::Reader(allocations.b.data(),allocations.b.size());};
 auto result=P::extract(layout,alloc_view(),regions,chunks);
 assert(result.status==P::Status::Extracted&&result.journal==journal);

 // The extraction is from parsed snapshot chunks; changes to live memory have no effect.
 std::fill(saved_memory.begin(),saved_memory.end(),0xEE);
 assert(P::extract(layout,alloc_view(),regions,chunks).journal==journal);
 auto empty_chunks=ss::MemoryChunks{};
 assert(P::extract(layout,alloc_view(),regions,empty_chunks).status==P::Status::EmptyHeader);
 auto null_chunks=chunks;
 null_chunks[0x60000000]=nullptr;
 assert(P::extract(layout,alloc_view(),regions,null_chunks).status==P::Status::NoSavedArena);
 auto no_rows=allocations;
 no_rows.b.resize(8);
 no_rows.b[4]=0;
 assert(P::extract(layout,ss::Reader(no_rows.b.data(),no_rows.b.size()),regions,chunks).status==P::Status::NoSavedArena);
 auto wrong_layout=A::make(layout.allocation.address+0x10000);
 assert(P::extract(wrong_layout,alloc_view(),regions,chunks).status==P::Status::NoSavedArena);

 // A syntactically valid journal from a different arena is still refused.
 J::Record other=journal;
 other.arena=layout.allocation.address+0x10000;
 assert(J::encode(other,bytes));
 std::vector<uint8_t> altered(0x10000);
 std::copy(bytes.begin(),bytes.end(),altered.begin()+0xF000);
 auto changed_chunks=chunks;
 changed_chunks[0x60000000]=altered.data();
 assert(P::extract(layout,alloc_view(),regions,changed_chunks).status==P::Status::CorruptHeader);
 altered[0xF000]^=1;
 assert(P::extract(layout,alloc_view(),regions,changed_chunks).status==P::Status::CorruptHeader);
}
