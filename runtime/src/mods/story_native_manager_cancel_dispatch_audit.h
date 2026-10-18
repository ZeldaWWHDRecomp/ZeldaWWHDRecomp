// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <vector>
#include <span>
#include <functional>
#include <cstdint>
namespace story_native_manager_cancel_dispatch::detail {
using Read=std::function<bool(uint32_t,std::span<uint8_t>)>;
struct Census {std::array<uint8_t,12>header{};std::vector<std::pair<uint32_t,std::array<uint8_t,20>>>nodes;bool operator==(const Census&)const=default;};
// Data-only bounded traversal proof. Never constructs a dispatch Guard.
bool census(const Read&,uint32_t,Census&) noexcept;
}
