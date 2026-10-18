// SPDX-License-Identifier: MPL-2.0
#include "story_native_bfres_extent.h"
#include <array>
#include <limits>

namespace story_native_bfres_extent { namespace {
constexpr uint32_t kMaximumFile = 64u * 1024u * 1024u;
constexpr uint32_t kMaximumRaw = 1024;
uint32_t be32(std::span<const uint8_t> b) {
    return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 |
           uint32_t(b[2]) << 8 | uint32_t(b[3]);
}
bool range(uint32_t offset, uint32_t length, uint32_t total) {
    return uint64_t(offset) + length <= total;
}
bool load(const Read& read, uint32_t base, uint32_t total,
          uint32_t offset, std::span<uint8_t> bytes) {
    return range(offset, uint32_t(bytes.size()), total) &&
           uint64_t(base) + offset + bytes.size() <= std::numeric_limits<uint32_t>::max() &&
           read(base + offset, bytes);
}
bool word(const Read& read, uint32_t base, uint32_t total,
          uint32_t offset, uint32_t& result) {
    std::array<uint8_t, 4> bytes{};
    if (!load(read, base, total, offset, bytes)) return false;
    result = be32(bytes);
    return true;
}
bool relative(const Read& read, uint32_t base, uint32_t total,
              uint32_t field, uint32_t& target) {
    uint32_t displacement{};
    if (!word(read, base, total, field, displacement) || !displacement) return false;
    const int64_t address = int64_t(field) + int32_t(displacement);
    if (address < 0 || address >= total) return false;
    target = uint32_t(address);
    return true;
}
bool matches(const Read& read, uint32_t base, uint32_t total,
             uint32_t string, std::string_view name) {
    if (string < 4 || name.empty() || name.size() > 63 ||
        !range(string, uint32_t(name.size() + 1), total)) return false;
    uint32_t length{};
    if (!word(read, base, total, string - 4, length) || length != name.size()) return false;
    std::array<uint8_t, 64> text{};
    if (!load(read, base, total, string,
              std::span<uint8_t>(text.data(), name.size() + 1)) || text[name.size()]) return false;
    for (size_t i = 0; i < name.size(); ++i)
        if (text[i] != uint8_t(name[i])) return false;
    return true;
}
} // namespace

bool find(const Read& read, uint32_t outer_base, uint32_t outer_bytes,
          std::string_view name, uint32_t expected_resource, Extent& out) {
    out = {};
    if (!read || !outer_base || !expected_resource || outer_bytes < 0x6c ||
        outer_bytes > kMaximumFile || uint64_t(outer_base) + outer_bytes >
                                      uint64_t(std::numeric_limits<uint32_t>::max()) + 1)
        return false;
    try {
        std::array<uint8_t, 0x6c> header{};
        if (!load(read, outer_base, outer_bytes, 0, header) ||
            header[0] != 'F' || header[1] != 'R' || header[2] != 'E' || header[3] != 'S' ||
            be32(std::span<const uint8_t>(header).subspan(0x0c, 4)) != outer_bytes ||
            header[8] != 0xfe || header[9] != 0xff) return false;
        const uint32_t count = uint32_t(header[0x66]) << 8 | header[0x67];
        if (!count || count > kMaximumRaw) return false;
        uint32_t dictionary{};
        if (!relative(read, outer_base, outer_bytes, 0x4c, dictionary) ||
            !range(dictionary, 8, outer_bytes)) return false;
        uint32_t dictionary_count{};
        if (!word(read, outer_base, outer_bytes, dictionary + 4, dictionary_count) ||
            dictionary_count != count ||
            !range(dictionary, 8 + (count + 1) * 16, outer_bytes)) return false;
        bool found = false;
        Extent candidate{};
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t entry = dictionary + 8 + (i + 1) * 16;
            uint32_t string{}, reference{};
            if (!relative(read, outer_base, outer_bytes, entry + 8, string) ||
                !relative(read, outer_base, outer_bytes, entry + 12, reference)) return false;
            if (!matches(read, outer_base, outer_bytes, string, name)) continue;
            if (found || !range(reference, 8, outer_bytes)) return false;
            uint32_t displacement{}, bytes{};
            if (!word(read, outer_base, outer_bytes, reference, displacement) ||
                !word(read, outer_base, outer_bytes, reference + 4, bytes) || !bytes) return false;
            const int64_t offset = int64_t(reference) + int32_t(displacement);
            if (offset < 0 || offset > UINT32_MAX ||
                !range(uint32_t(offset), bytes, outer_bytes) ||
                uint64_t(outer_base) + offset != expected_resource) return false;
            candidate = {expected_resource, bytes, outer_base + entry};
            found = true;
        }
        if (!found) return false;
        out = candidate;
        return true;
    } catch (...) { return false; }
}
} // namespace story_native_bfres_extent
