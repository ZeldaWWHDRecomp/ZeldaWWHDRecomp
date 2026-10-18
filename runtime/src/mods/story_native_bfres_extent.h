// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace story_native_bfres_extent {
using Read = std::function<bool(uint32_t, std::span<uint8_t>)>;
struct Extent {
    uint32_t base{};
    uint32_t bytes{};
    uint32_t entry{};
};

// Read an embedded raw-file extent from the BFRES metadata used by the game's
// LoadAllResources (026057F4). outer_bytes must come from the archive file-size
// API, independently of the BFRES header and resource pointer. This verifies
// bytes only; it does not establish owner lifetime or authorize publication.
bool find(const Read& read, uint32_t outer_base, uint32_t outer_bytes,
          std::string_view name, uint32_t expected_resource, Extent& out);
} // namespace story_native_bfres_extent
