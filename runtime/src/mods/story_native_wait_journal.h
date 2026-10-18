// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace story_native_wait_journal {
inline constexpr uint32_t kBytes=128,kVersion=1;
enum class Phase:uint32_t {Unpublished,Preparing,Published,Running,Draining,Detaching,Quarantined};
struct Record {uint64_t generation=0,serial=0;uint32_t arena=0;Phase phase=Phase::Unpublished;std::array<uint8_t,32> definition{};bool operator==(const Record&)const=default;};
// Codec output is untrusted data, never a restore/ownership certificate.
bool encode(const Record&,std::span<uint8_t>);
bool decode(std::span<const uint8_t>,Record&);
// Pure transition planning only. Caller must separately prove current allocation,
// definition, exclusive ownership, observed native state and durable write success.
bool transition(const Record&,Phase,Record&);
// Only a proven unpublished/detached boundary may start another generation.
bool next_generation(const Record&,bool independently_detached,Record&);
}
