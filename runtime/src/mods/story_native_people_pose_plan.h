// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace story_native_people_pose_plan {
struct Identity {uint32_t actor{},pid{},token{},stage{},room{},generation{},morf{},model{};bool operator==(const Identity&)const=default;};
struct Snapshot {Identity identity{};std::array<uint8_t,6> angles{},shape{};std::array<uint8_t,0x34> joint{};std::array<uint8_t,0x6C> cut{};std::array<uint8_t,12> target{};std::array<uint8_t,2> velocity{};uint8_t head_only{},noticed{},talk_ready{},target_mode{};uint32_t status{};};
struct Patch {uint32_t address{};std::vector<uint8_t> bytes;};
struct Plan {std::vector<Patch> patches;bool refresh_base{},refresh_joints{};};
// Producer authenticates native allocations and captures baseline before binding.
// Plan does not execute memory writes/native calls or authorize image release.
bool make(const Snapshot&,const Identity&current,bool exact_uo2,bool idle,bool detached,bool cancel,Plan&);
}
