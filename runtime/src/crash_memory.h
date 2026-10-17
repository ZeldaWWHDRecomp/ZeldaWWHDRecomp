#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace crash_memory {
using Output = void (*)(int,const char*,size_t);
struct Section { uint32_t address, flags; std::vector<uint8_t> bytes; uint64_t extent=0; };
// Normal startup only. Retains executable/non-writable baseline bytes. Publish the permanent readable
// 4 GiB guest arena only after loader copies; never pass an input-file buffer.
bool enabled();
void install(std::vector<Section> sections, const uint8_t* live_guest_base);
// Normal thread only; all OS queries/formatting precede atomic publication.
void refresh(uint32_t guest_threads, uint32_t runtime_top);
// Bounded, allocation/lock-free; samples only complete trusted code windows in the permanent guest arena.
void note(int fd, Output out, const uint32_t* registers);
}
