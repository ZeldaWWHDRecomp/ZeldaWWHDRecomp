// SPDX-License-Identifier: MPL-2.0
// The pauses written into the game's texts (No pauses in texts, instant_text.cpp): the wait code of a message
// file (MSBT, "MsgStdBn", big endian) is the UTF-16BE control sequence 0E 0001 0006 0002 <frames>: group 1, tag 6,
// two bytes of parameter, the frames to wait. patch() sets the frames of each wait in a file's TXT2 strings to 0 and
// returns where (offsets in the file) and what they were; restore() puts them back where the wait is still a 0.
// Pure (a byte span), so msbt_waits_test checks it on fixture files.
//
// From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (MPL-2.0), mods/instant_text.cpp patch_msbt; the TWWHD
// Randomizer deletes the same code from its message files.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace mods::msbt {

inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
inline void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }

constexpr char kMagic[8] = {'M', 's', 'g', 'S', 't', 'd', 'B', 'n'};

// the file's size when `file` (at most `avail` bytes readable) is a plausible big-endian MSBT, else 0
inline uint32_t file_size(const uint8_t* file, size_t avail) {
    if (avail < 0x20 || memcmp(file, kMagic, 8) || be16(file + 8) != 0xFEFF) return 0;
    const uint32_t size = be32(file + 0x12);
    const int sections = be16(file + 0xE);
    if (size < 0x30 || size > (32u << 20) || size > avail || sections <= 0 || sections > 16) return 0;
    return size;
}

// whether offset `f` (the frames of a wait) is still a wait's frames
inline bool is_wait_frames(const uint8_t* file, size_t f) {
    return f >= 8 && be16(file + f - 8) == 0x0E && be16(file + f - 6) == 1 && be16(file + f - 4) == 6 &&
           be16(file + f - 2) == 2;
}

using Saved = std::vector<std::pair<uint32_t, uint16_t>>;  // offset of a wait's frames, the game's value

// every wait with frames != 0 in the TXT2 strings set to 0; returns how many (appended to `saved`)
inline int patch(uint8_t* file, size_t avail, Saved& saved) {
    const uint32_t size = file_size(file, avail);
    if (!size) return 0;
    const int sections = be16(file + 0xE);
    int n = 0;
    uint32_t sec = 0x20;
    for (int k = 0; k < sections && sec + 16 <= size; k++) {
        const uint32_t ssize = be32(file + sec + 4), data = sec + 16;
        if (ssize > size || data + ssize > size) break;
        if (!memcmp(file + sec, "TXT2", 4) && ssize >= 4) {
            const uint32_t count = be32(file + data);
            for (uint32_t i = 0; i < count && 4 + 4 * uint64_t(i) + 4 <= ssize; i++) {
                const uint32_t start = be32(file + data + 4 + 4 * i);
                if (start >= ssize) continue;
                uint32_t p = data + start;
                const uint32_t stop = data + ssize;
                while (p + 2 <= stop) {
                    const uint16_t ch = be16(file + p);
                    if (ch == 0) break;
                    if (ch == 0x0E) {
                        if (p + 8 > stop) break;
                        const uint16_t grp = be16(file + p + 2), tag = be16(file + p + 4), psz = be16(file + p + 6);
                        if (grp == 1 && tag == 6 && psz == 2 && p + 10 <= stop) {
                            const uint32_t f = p + 8;
                            if (const uint16_t v = be16(file + f)) {
                                saved.push_back({f, v});
                                put16(file + f, 0);
                                n++;
                            }
                        }
                        p += 8 + psz;
                    } else if (ch == 0x0F) {
                        p += 6;  // a closing tag: group, tag
                    } else {
                        p += 2;
                    }
                }
            }
        }
        sec = data + ((ssize + 15) & ~15u);
    }
    return n;
}

// the saved waits back, where the file still holds a wait with 0 frames there; returns how many
inline int restore(uint8_t* file, size_t avail, const Saved& saved) {
    int n = 0;
    for (const auto& [f, v] : saved)
        if (f + 2 <= avail && is_wait_frames(file, f) && be16(file + f) == 0) {
            put16(file + f, v);
            n++;
        }
    return n;
}

}  // namespace mods::msbt
