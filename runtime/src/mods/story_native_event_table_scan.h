// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <span>
namespace story_native_event_table_scan {
struct Resource {
 uint32_t base{},bytes{},generation{},lookup_arc{},lookup_name{};
 // Set only by the actual loaded-stage resource owner/registry verifier.
 bool loaded_retail=false;bool operator==(const Resource&)const=default;
};
struct Context {
 uint32_t stage_pid{},room_pid{},room_number{};std::array<uint8_t,8>stage_name{};
 Resource resource;uint64_t logic_step{},serial{};bool exclusive_idle=false;
 bool operator==(const Context&)const=default;
};
using Read=std::function<bool(uint32_t,std::span<uint8_t>)>;
// Actual current resource/session provenance, never a caller Expected or a
// manager pointer re-labelled as a resource receipt. Missing provider refuses.
using Query=std::function<bool(Context&)>;
enum class Reason {None,Provenance,Read,Layout,Busy,State,Changed};
struct Result {bool idle=false;Reason reason=Reason::Provenance;uint32_t events{},blocking_index=0xFFFFFFFFu,blocking_state{};};
// No writes/native calls/allocations in guest memory. Source-native event count
// and states are bounded by the independently verified resource extent (2 MiB
// maximum), rather than the much smaller authored WAIT image's pool limits.
// Passing this read-only scan is NOT a lease, entry fence or publish authority.
Result inspect(const Read&,const Query&);
}
