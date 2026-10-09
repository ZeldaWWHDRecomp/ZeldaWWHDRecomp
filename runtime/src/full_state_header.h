// Full-state header extension; compression versions and the old prefix stay unchanged.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace ss {
struct FullStateHeader {
    char magic[8];
    uint32_t version;
    uint32_t header_size;
    uint64_t created;       // unix time
    char area[32];          // stage name
    uint8_t build[16];      // LC_UUID of the executable that wrote it (informational)
    uint64_t game_id;       // hash of cking.rpx
    uint32_t cpu_size;      // sizeof(Cpu)
    uint32_t blocks;        // compressed blocks that follow
    uint64_t raw_size;      // payload bytes
    uint32_t controller = 0; // 0 absent/unknown, 1 GamePad, 2 Pro Controller
    uint32_t reserved = 0;
};
constexpr size_t kLegacyHeaderSize = offsetof(FullStateHeader, controller);
static_assert(kLegacyHeaderSize == 96);
inline bool read_full_state_header(FILE* file, FullStateHeader& h, std::string& why) {
    h = {};
    if (fread(&h, kLegacyHeaderSize, 1, file) != 1 || memcmp(h.magic, "WWHDSTAT", 8)) {
        why = "not a save state"; return false;
    }
    if (h.header_size != kLegacyHeaderSize && h.header_size != sizeof h) {
        why = "saved by another version"; return false;
    }
    if (h.header_size == sizeof h &&
        fread(reinterpret_cast<uint8_t*>(&h) + kLegacyHeaderSize, sizeof h - kLegacyHeaderSize, 1, file) != 1) {
        why = "truncated save state header"; return false;
    }
    if (h.controller > 2 || h.reserved) { why = "invalid controller mode"; return false; }
    return true;
}
inline const char* controller_label(int mode) {
    return mode == 2 ? "Pro Controller" : mode == 1 ? "GamePad" : "";
}
// Absent metadata leaves the host's choice intact (legacy states).
inline bool restored_pro_controller(int mode, bool current) {
    return mode == 0 ? current : mode == 2;
}
} // namespace ss
