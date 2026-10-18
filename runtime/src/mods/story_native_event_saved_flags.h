// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "runtime.h"
#include "state_memory.h"
#include <array>
#include <span>
namespace story_native_event_saved_flags {
using Flags=std::array<uint8_t,1280>;
// Exact authorized MEM2 manager+3C only. Inputs MUST come from successful
// ss::read_regions; absent sparse chunks inside regions mean zero. Never reads
// live guest memory. Failure leaves output untouched. Not manager ownership.
bool read(uint32_t expected_manager,std::span<const ss::MemoryRegion>,const ss::MemoryChunks&,Flags&);
}
