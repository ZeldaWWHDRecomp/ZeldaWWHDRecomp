// SPDX-License-Identifier: MPL-2.0
// The texts of a layout's message file (MsgStdBn, big-endian UTF-16: LBL1 labels, TXT2 texts) as
// label -> UTF-8 text, for showing the game's own words outside the game (the context buttons' action
// words, read from the player's CommandGuide_00 at run time; nothing of the game ships). The game's
// control codes (0x0E group type size params: colours, icons, sizes) are left out. Pure:
// msbt_text_test. Malformed files give an empty or partial map, never a crash.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace mods::msbt {

inline void utf8(std::string& s, uint32_t ch) {
    if (ch < 0x80) s += char(ch);
    else if (ch < 0x800) s += char(0xC0 | ch >> 6), s += char(0x80 | (ch & 0x3F));
    else if (ch < 0x10000) s += char(0xE0 | ch >> 12), s += char(0x80 | (ch >> 6 & 0x3F)), s += char(0x80 | (ch & 0x3F));
    else s += char(0xF0 | ch >> 18), s += char(0x80 | (ch >> 12 & 0x3F)), s += char(0x80 | (ch >> 6 & 0x3F)), s += char(0x80 | (ch & 0x3F));
}

inline std::map<std::string, std::string> texts(const std::vector<uint8_t>& m) {
    std::map<std::string, std::string> out;
    const size_t n = m.size();
    auto be16 = [&](size_t o) -> uint32_t { return o + 2 <= n ? uint32_t(m[o]) << 8 | m[o + 1] : 0; };
    auto be32 = [&](size_t o) -> uint32_t { return o + 4 <= n ? uint32_t(m[o]) << 24 | m[o + 1] << 16 | m[o + 2] << 8 | m[o + 3] : 0; };
    if (n < 0x20 || memcmp(m.data(), "MsgStdBn", 8) || m[8] != 0xFE || m[9] != 0xFF || m[0x0C] != 1) return out;
    size_t lbl = 0, lbl_size = 0, txt = 0, txt_size = 0;
    uint32_t sections = be16(0x0E);
    for (size_t p = 0x20; sections-- && p + 16 <= n;) {
        const size_t size = be32(p + 4);
        if (size > n - p - 16) break;
        if (!memcmp(&m[p], "LBL1", 4)) lbl = p + 16, lbl_size = size;
        else if (!memcmp(&m[p], "TXT2", 4)) txt = p + 16, txt_size = size;
        p = (p + 16 + size + 15) & ~size_t(15);
    }
    if (!lbl || !txt || lbl_size < 4 || txt_size < 4) return out;
    const uint32_t count = be32(txt);
    if (count > (txt_size - 4) / 4) return out;
    std::vector<std::string> labels(count);
    const uint32_t buckets = be32(lbl);
    for (uint32_t b = 0; b < buckets && 4 + size_t(b) * 8 + 8 <= lbl_size; b++) {
        const uint32_t k = be32(lbl + 4 + b * 8);
        size_t q = be32(lbl + 8 + b * 8);
        for (uint32_t i = 0; i < k && q < lbl_size; i++) {
            const size_t len = m[lbl + q];
            if (q + 1 + len + 4 > lbl_size) break;
            const uint32_t index = be32(lbl + q + 1 + len);
            if (index < count) labels[index].assign(reinterpret_cast<const char*>(&m[lbl + q + 1]), len);
            q += 1 + len + 4;
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        const size_t a = be32(txt + 4 + i * 4), e = i + 1 < count ? be32(txt + 8 + i * 4) : txt_size;
        if (labels[i].empty() || a > e || e > txt_size) continue;
        std::string t;
        for (size_t q = a; q + 1 < e;) {
            uint32_t ch = be16(txt + q);
            q += 2;
            if (ch == 0) break;
            if (ch == 0x0E) {  // group, type, parameter size, parameters
                if (q + 6 > e) break;
                q += 6 + be16(txt + q + 4);
                continue;
            }
            if (ch == 0x0F) {  // an end tag (group, type)
                q += 4;
                continue;
            }
            if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
            if (ch >= 0xD800 && ch < 0xDC00 && q + 1 < e) {
                const uint32_t lo = be16(txt + q);
                if (lo >= 0xDC00 && lo < 0xE000) ch = 0x10000 + ((ch - 0xD800) << 10) + (lo - 0xDC00), q += 2;
            }
            utf8(t, ch);
        }
        out[labels[i]] = t;
    }
    return out;
}

}  // namespace mods::msbt
