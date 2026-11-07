// SPDX-License-Identifier: MPL-2.0
// rpx_relocs.h (issue #383): the loader applies relocations to the module's own symbols, like the system
// loader. Synthetic sections only: a table whose bytes keep a stub pointer while its relocation was
// redirected (TWWHD Randomizer 1.2.0's item-function table edits), imports and undefined symbols left alone.
#include "rpx_relocs.h"
#include <cassert>
#include <cstdio>

using rpx_relocs::Section;

static void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
static uint32_t get32(const std::vector<uint8_t>& v, size_t o) { return rpx_relocs::be32(&v[o]); }
static void sym(std::vector<uint8_t>& v, uint32_t value, uint16_t shndx) {
    put32(v, 0); put32(v, value); put32(v, 0); v.push_back(0); v.push_back(0); v.push_back(shndx >> 8); v.push_back(shndx & 0xFF);
}
static void rel(std::vector<uint8_t>& v, uint32_t off, uint32_t symbol, uint32_t type, int32_t addend) {
    put32(v, off); put32(v, symbol << 8 | type); put32(v, (uint32_t)addend);
}

// 0 null, 1 .text, 2 .data, 3 import, 4 .symtab, 5 .rela.data, 6 .rela.text
static std::vector<Section> module(bool redirected, uint32_t bad_offset = 0) {
    std::vector<Section> s(7);
    s[1] = {1, 6, 0x02000000, 0, 0, {}};
    for (uint32_t w : {0x4E800020u, 0x4E800020u, 0x4E800020u, 0x3C601000u, 0x38630000u}) put32(s[1].data, w);
    s[2] = {1, 3, 0x10000000, 0, 0, {}};
    for (uint32_t w : {0x02000004u, 0x02000004u, 0u, 0u}) put32(s[2].data, w);
    s[3] = {rpx_relocs::kShtRplImports, 2, 0xC0000000, 0, 0, std::vector<uint8_t>(8)};
    s[4] = {rpx_relocs::kShtSymtab, 0, 0, 0, 0, {}};
    sym(s[4].data, 0, 0); sym(s[4].data, 0x02000000, 1); sym(s[4].data, 0x10000000, 2);
    sym(s[4].data, 0xC0000000, 3); sym(s[4].data, 0, 0);
    s[5] = {rpx_relocs::kShtRela, 0, 0, 4, 2, {}};
    rel(s[5].data, 0x10000000, 1, rpx_relocs::kAddr32, redirected ? 8 : 4);
    rel(s[5].data, 0x10000004, 1, rpx_relocs::kAddr32, 4);
    rel(s[5].data, 0x10000008, 3, rpx_relocs::kAddr32, 0);       // import
    rel(s[5].data, 0x1000000C, 4, rpx_relocs::kAddr32, 0x1234);  // undefined
    if (bad_offset) rel(s[5].data, bad_offset, 1, rpx_relocs::kAddr32, 0);
    s[6] = {rpx_relocs::kShtRela, 0, 0, 4, 1, {}};
    rel(s[6].data, 0x0200000E, 2, rpx_relocs::kAddr16Ha, redirected ? 0x40 : 0);
    rel(s[6].data, 0x02000012, 2, rpx_relocs::kAddr16Lo, redirected ? 0x40 : 0);
    rel(s[6].data, 0x02000000, 1, 10 /* REL24: not applied */, 0x100);
    return s;
}

int main() {
    {   // the retail case: everything already holds its relocated value
        auto s = module(false);
        auto before = s;
        assert(rpx_relocs::apply(s) == 0);
        for (size_t i = 0; i < s.size(); i++) assert(s[i].data == before[i].data);
    }
    {   // redirected table entry and immediate are written; the rest stays as linked
        auto s = module(true);
        assert(rpx_relocs::apply(s) == 2);
        assert(get32(s[2].data, 0) == 0x02000008);
        assert(get32(s[2].data, 4) == 0x02000004);
        assert(get32(s[2].data, 8) == 0 && get32(s[2].data, 12) == 0);
        assert(get32(s[1].data, 12) == 0x3C601000 && get32(s[1].data, 16) == 0x38630040);
        assert(get32(s[1].data, 0) == 0x4E800020);
        assert(rpx_relocs::apply(s) == 0);  // idempotent
    }
    {   // a relocation outside its section refuses the module
        auto s = module(true, 0x10000010);
        assert(rpx_relocs::apply(s) == -1);
    }
    puts("rpx_relocs ok");
    return 0;
}
