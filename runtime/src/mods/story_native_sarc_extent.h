// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace story_native_sarc_extent {
using Read = std::function<bool(uint32_t, std::span<uint8_t>)>;
struct Extent {
    uint32_t base{}, bytes{}, archive_base{}, archive_bytes{}, allocation_bytes{};
};

// Inspect a live SharcArchiveRes-like resource without calling game code.
// Native 0275D21C assigns +14 data, +18 logical bytes, +1C allocation bytes.
// The caller must independently prove resource identity/lifetime under an
// unload fence; this parser alone cannot issue a stage-owner receipt.
bool find(const Read& read, uint32_t archive_resource, std::string_view name,
          uint32_t expected_file, Extent& out);
} // namespace story_native_sarc_extent
