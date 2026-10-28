// SPDX-License-Identifier: MPL-2.0
// msbt_text.h on a message file built here: labels, control codes left out, non-ASCII, bad files.
#include "mods/msbt_text.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include <cstdio>

using Bytes = std::vector<uint8_t>;
namespace {
void be16(Bytes& b, uint32_t v) { b.push_back(uint8_t(v >> 8)), b.push_back(uint8_t(v)); }
void be32(Bytes& b, uint32_t v) { be16(b, v >> 16), be16(b, v); }
void section(Bytes& out, const char* magic, const Bytes& body) {
    out.insert(out.end(), magic, magic + 4);
    be32(out, uint32_t(body.size()));
    out.resize(out.size() + 8, 0);
    out.insert(out.end(), body.begin(), body.end());
    while (out.size() % 16) out.push_back(0xAB);
}
Bytes build(const std::vector<std::pair<std::string, std::vector<uint16_t>>>& entries) {
    Bytes lbl;
    be32(lbl, 1);  // one bucket
    be32(lbl, uint32_t(entries.size()));
    be32(lbl, 12);
    for (size_t i = 0; i < entries.size(); i++) {
        lbl.push_back(uint8_t(entries[i].first.size()));
        lbl.insert(lbl.end(), entries[i].first.begin(), entries[i].first.end());
        be32(lbl, uint32_t(i));
    }
    Bytes txt;
    be32(txt, uint32_t(entries.size()));
    uint32_t off = 4 + 4 * uint32_t(entries.size());
    for (auto& e : entries) be32(txt, off), off += uint32_t(e.second.size() * 2 + 2);
    for (auto& e : entries) {
        for (uint16_t c : e.second) be16(txt, c);
        be16(txt, 0);
    }
    Bytes m(0x20, 0);
    memcpy(m.data(), "MsgStdBn", 8);
    m[8] = 0xFE, m[9] = 0xFF, m[0x0C] = 1, m[0x0F] = 2;
    section(m, "LBL1", lbl);
    section(m, "TXT2", txt);
    return m;
}
}  // namespace

int main() {
    const Bytes m = build({
        {"T_ACT_Command_02", {'S', 'p', 'e', 'a', 'k'}},
        {"T_ACT_Command_04", {0x0E, 0, 3, 2, 0xFFFF, 'L', 'i', 'f', 't'}},  // a colour code first
        {"T_ACT_Command_05", {'E', 'n', 't', 'r', 0xE9, 'e', 0x0F, 0, 3, '!'}},  // non-ASCII, an end tag
        {"T_ACT_Command_06", {'L', 'e', 't', '\n', 'g', 'o', 0xD83D, 0xDE00}},  // a line break, a surrogate pair
    });
    auto t = mods::msbt::texts(m);
    assert(t.size() == 4);
    assert(t["T_ACT_Command_02"] == "Speak");
    assert(t["T_ACT_Command_04"] == "Lift");
    assert(t["T_ACT_Command_05"] == "Entr\xC3\xA9" "e!");
    assert(t["T_ACT_Command_06"] == "Let go\xF0\x9F\x98\x80");
    // bad files: empty maps
    Bytes bad = m;
    bad[8] = 0xFF;  // little-endian: not ours
    assert(mods::msbt::texts(bad).empty());
    assert(mods::msbt::texts(Bytes(m.begin(), m.begin() + 0x30)).empty());
    for (size_t cut = 0; cut < m.size(); cut += 7) mods::msbt::texts(Bytes(m.begin(), m.begin() + cut));  // no crash
    Bytes huge = m;
    huge[0x20 + 4] = 0x7F;  // LBL1 size past the end
    assert(mods::msbt::texts(huge).empty());
    puts("msbt_text: labels, control codes, non-ASCII, surrogates, bad and cut files PASS");
}
