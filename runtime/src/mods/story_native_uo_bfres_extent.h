// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_sarc_extent.h"
#include <array>
namespace story_native_uo_bfres_extent {
using Read=story_native_sarc_extent::Read;
struct Range {uint32_t address{},bytes{};};
struct Input {
 // Independently captured allocation bound, not a SARC/header declaration.
 Range parent_allocation{};
 uint32_t parent_heap{},archive_parent_heap{},holder_heap{},resource_address{},archive_file{};
 std::array<uint8_t,0x28> heap{};
 std::array<uint8_t,0x44> resource{};
};
using Extent=story_native_sarc_extent::Extent;
// Data-only bounded parser. Caller retains the actual stage/resource fence and
// independently establishes native registry identities and allocation lifetime.
// No result grants native-call, publication, ownership or release authority.
bool verify(const Read&,const Input&,Extent&);
}
