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
    uint32_t controller = 0; // 0 absent/unknown, 1 GamePad, 2 Pro Controller (slot label only: a load keeps the host's choice)
    uint32_t reserved = 0;
};
// Older readers reject every nonzero reserved value before reading payloads.
// Keep this required feature outside optional payload sections.
inline constexpr uint32_t kRequiresSaveProfile = 1;
// The build kind that wrote a full state (bits 8-9 of `reserved`; docs/portable-save-states.md): 0 the normal build,
// 1 the decomp build, 2 the decomp build's fast mode. A full state holds the game's whole memory and threads as that
// build left them, so it loads only in a build of the same kind (savestate.cpp read_header). Normal states keep 0,
// so their files are unchanged; older readers refuse a decomp state (a nonzero reserved bit).
inline constexpr uint32_t kBuildKindShift = 8;
inline constexpr uint32_t kBuildKindMask = 3u << kBuildKindShift;
inline constexpr uint32_t kKnownReservedBits = kRequiresSaveProfile | kBuildKindMask;
inline uint32_t build_kind(uint32_t reserved) { return (reserved & kBuildKindMask) >> kBuildKindShift; }
inline const char* build_kind_name(uint32_t kind) {
    return kind == 2 ? "decomp version (fast mode)" : kind == 1 ? "decomp version (plain build)" : "normal game";
}
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
    if (h.controller > 2 || (h.reserved & ~kKnownReservedBits) || build_kind(h.reserved) > 2) {
        why = "invalid controller mode"; return false;
    }
    return true;
}
inline const char* controller_label(int mode) {
    return mode == 2 ? "Pro Controller" : mode == 1 ? "GamePad" : "";
}
} // namespace ss
