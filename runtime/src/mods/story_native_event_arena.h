// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "runtime.h"
#include "state_memory.h"
#include <array>
#include <span>
namespace story_native_event_arena {
inline constexpr uint32_t kVersion=1,kBytes=0x7000;
struct Span {uint32_t address=0,bytes=0;bool operator==(const Span&)const=default;};
struct Instance {Span header,image,stamp,depth,record;bool operator==(const Instance&)const=default;};
struct Layout {uint32_t version=0;Span allocation;Instance wait,turn;bool operator==(const Layout&)const=default;};
// Deterministic layout only. Production allocates kBytes ONCE at startup using
// runtime_alloc(kBytes,16), then separately initializes original bytes/records.
Layout make(uint32_t base);
bool valid(const Layout&);
// Exact allocation identity (not a containing range or caller-tag guess). The
// allocator log/top are host facts, never loaded guest-controlled substitutes.
bool owns(const Layout&,std::span<const mem::AllocRec>,uint32_t runtime_top);
// RALC is the exact saved allocation section (top, count, AllocRec rows).
// Its arena row, top, and saved memory coverage must all agree. Rounded region
// coverage alone must not admit an object allocated after the snapshot.
bool saved(const Layout&,ss::Reader saved_allocations,std::span<const ss::MemoryRegion>);
// Input chunks/regions must come from successful ss::read_regions, whose parser
// validates complete chunk payloads. Missing sparse chunks within a saved region
// represent zero. Outside saved spans always fails: NEVER falls back to live EA.
bool read_saved(const Layout&,ss::Reader saved_allocations,std::span<const ss::MemoryRegion>,const ss::MemoryChunks&,uint32_t address,std::span<uint8_t>);
}
