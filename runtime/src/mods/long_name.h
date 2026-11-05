// SPDX-License-Identifier: MPL-2.0
// Save file names up to 16 characters (Settings > Change Link's name; the game's own name entry allows 8).
//
// Where the name lives:
//   - cking.sav keeps the game's 8 characters, as always: an unmodded game, the file select and any other tool
//     read those (and see a valid save);
//   - the whole name is a record in the story block after the game's bytes of cking.sav (story_block.h; kind
//     story_mail::kRecordLongName in the Quest Log's area: key index * 2 + 0 / 1 the UTF-16 character's high /
//     low byte, key kLengthKey the length). Every copy of cking.sav carries it: the save folder (also a cloud
//     one), the save backups, Android's save export and import, the randomizer's per-seed saves;
//   - the whole name holds only while the save's own name is its first 8 characters. A save copied without
//     the block, a new game in that file or a name given by the game's own name entry shows the save's own name.
// In play: the game reads a file's name through its card name object (SaveMgr card info +0xA0 + file * 0x40:
// +0 the text's pointer, +8 the capacity in characters with the end, +0xC the file's own 9-character buffer).
// While the whole name holds, the pointer is aimed at a copy of it with capacity 17, so the game's texts show all
// of it; the game's save then copies 9 characters without an end into cking.sav, so after the game writes the
// file (story_store::written) that name is cut back to 8 characters with its end and the file's CRC made again.
// A full save state keeps the pointer; the copy is written again on every controller read (service()).
// From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (MPL-2.0), mods/save_names.cpp (the pointer, the
// capacities); the storage in the story block and the save fix are ours.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "story_block.h"
#include "story_mail_book.h"

struct Cpu;

namespace long_name {

inline constexpr size_t kMax = 16;   // characters
inline constexpr size_t kOwn = 8;    // what the game keeps
inline constexpr uint16_t kLengthKey = 0x100;

// ---- the record in a Quest Log's area (pure; long_name_test) ----
inline std::u16string read(const uint8_t* area) {
    story_mail::Book b(const_cast<uint8_t*>(area), story_block::kAreaRecords);
    if (!b.usable() || !b.has(story_mail::kRecordLongName, kLengthKey)) return {};
    const size_t n = b.get(story_mail::kRecordLongName, kLengthKey);
    if (n <= kOwn || n > kMax) return {};
    std::u16string s;
    for (size_t i = 0; i < n; i++) {
        if (!b.has(story_mail::kRecordLongName, uint16_t(2 * i)) || !b.has(story_mail::kRecordLongName, uint16_t(2 * i + 1)))
            return {};
        const char16_t ch = char16_t(b.get(story_mail::kRecordLongName, uint16_t(2 * i)) << 8 |
                                     b.get(story_mail::kRecordLongName, uint16_t(2 * i + 1)));
        if (!ch) return {};
        s.push_back(ch);
    }
    return s;
}
// the area's records of the name: `name` (over 8 characters) or none ("" erases). False when the area has no
// room (then it holds no name record) or holds data this port did not write.
inline bool write(uint8_t* area, const std::u16string& name) {
    story_mail::Book b(area, story_block::kAreaRecords);
    if (!b.usable()) return false;
    for (uint16_t k = 0; k < 2 * kMax; k++) b.erase(story_mail::kRecordLongName, k);
    b.erase(story_mail::kRecordLongName, kLengthKey);
    if (name.size() <= kOwn) return true;
    const size_t n = std::min(name.size(), kMax);
    if (b.free_records() < 2 * n + 1) return false;
    for (size_t i = 0; i < n; i++) {
        b.set(story_mail::kRecordLongName, uint16_t(2 * i), uint8_t(name[i] >> 8));
        b.set(story_mail::kRecordLongName, uint16_t(2 * i + 1), uint8_t(name[i]));
    }
    b.set(story_mail::kRecordLongName, kLengthKey, uint8_t(n));
    return true;
}
// whether a whole name belongs to a save whose own name is `own` (its first 8 characters)
inline bool holds(const std::u16string& whole, const std::u16string& own) {
    return whole.size() > kOwn && own.size() == kOwn && whole.compare(0, kOwn, own) == 0;
}

// ---- cking.sav (pure; long_name_test): the HD names section and its CRC (tools/savegame/wwsave.py) ----
inline constexpr size_t kSlot = 0xA94, kFileSize = 8966;
inline constexpr size_t kExtra[5] = {16, 4, 20, 18, 220};  // player, status, event, name, map (each three times)
inline size_t names_at() { return 3 * kSlot + 3 * (kExtra[0] + kExtra[1] + kExtra[2]); }
// `counts[s]`: the bytes file s's name contributes (0: as the game counts a name it read, characters + 1)
inline uint32_t extra_crc(const uint8_t* f, const size_t counts[3] = nullptr) {
    std::array<uint8_t, 3 * (16 + 4 + 20 + 18 + 220)> buf{};
    size_t p = 3 * kSlot, o = 0;
    for (size_t sect = 0; sect < 5; sect++)
        for (int s = 0; s < 3; s++, p += kExtra[sect], o += kExtra[sect]) {
            if (sect != 3) {
                memcpy(&buf[o], f + p, kExtra[sect]);
                continue;
            }
            size_t chars = 0;
            while (chars < 9 && (f[p + 2 * chars] || f[p + 2 * chars + 1])) chars++;
            const size_t n = counts && counts[s] ? counts[s] : chars + 1;
            memcpy(&buf[o], f + p, std::min<size_t>(n, 18));  // (the game counts characters + 1 BYTES, zero padded)
        }
    return story_block::crc32(buf.data(), buf.size());
}
inline std::u16string own_name(const uint8_t* f, int slot) {
    const uint8_t* p = f + names_at() + 18 * size_t(slot);
    std::u16string s;
    for (size_t i = 0; i < 9 && (p[2 * i] || p[2 * i + 1]); i++) s.push_back(char16_t(p[2 * i] << 8 | p[2 * i + 1]));
    return s;
}
// a file's name of 9 characters without an end (the game's save while the whole name was in) cut to 8 with its
// end, and the CRC made again. The game counted such a name with the whole name's length (characters + 1 bytes of
// the 18-byte field, seen: 17 for 16 characters), so the file is recognised as the game's by a CRC that matches with
// some count of 10..18 bytes for each such name. Files whose CRC matches no such count are left alone. Returns the
// files changed (bit per slot).
inline unsigned fix_saved_names(uint8_t* f, size_t size) {
    if (size < kFileSize) return 0;
    const uint32_t crc_at = uint32_t(names_at() + 3 * 18 + 3 * 220 + 4);
    const uint32_t stored = story_block::be32(f + crc_at);
    size_t counts[3] = {0, 0, 0};
    unsigned long_ones = 0;
    for (int s = 0; s < 3; s++)
        if (own_name(f, s).size() >= 9) long_ones |= 1u << s;
    if (!long_ones) return 0;
    bool game = false;
    for (size_t a = 10; a <= 18 && !game; a++)
        for (size_t b = 10; b <= 18 && !game; b++)
            for (size_t c = 10; c <= 18 && !game; c++) {
                counts[0] = long_ones & 1 ? a : 0, counts[1] = long_ones & 2 ? b : 0, counts[2] = long_ones & 4 ? c : 0;
                game = extra_crc(f, counts) == stored;
                if (!(long_ones & 4)) c = 18;  // (no need to vary a count that isn't used)
                if (!(long_ones & 2) && c == 18) b = 18;
            }
    if (!game) return 0;  // not a file as the game writes it: left alone
    for (int s = 0; s < 3; s++)
        if (long_ones & (1u << s)) memset(f + names_at() + 18 * size_t(s) + 2 * kOwn, 0, 18 - 2 * kOwn);
    story_block::put32(f + crc_at, extra_crc(f));
    return long_ones;
}

// ---- the port (long_name.cpp) ----
// story_store: cking.sav read (the payload's three areas, or null: no valid block)
void loaded(const uint8_t* payload);
// story_store: the game wrote cking.sav / cking.sav.backup; `file` its bytes (the game's part first, `game_size`
// of them): the names fixed (fix_saved_names) and each Quest Log's whole name put into `areas` (the payload).
// Returns whether the game's part changed.
bool written(uint8_t* file, size_t game_size, uint8_t* areas);
// the player renamed the file being played (player_name.cpp): the whole name (over 8: kept with the next save)
void renamed(int slot, const std::u16string& whole);
// the game thread, every controller read (hle/system_stubs.cpp): the card name objects aimed at the whole names
void service(Cpu* c);
// the file's card name object back on its own buffer (before the game's name setter writes through it)
void release(int slot);
// the whole name of a file while it holds (else its own), for the settings page
std::u16string shown(int slot);

}  // namespace long_name
