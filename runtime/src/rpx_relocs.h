// SPDX-License-Identifier: MPL-2.0
// The game module's relocations to its own symbols, applied the way the system loader applies them.
//
// cking.rpx is linked at its load address, so the retail sections already hold these values and
// applying them changes nothing. TWWHD Randomizer 1.2.0 redirects function-table entries and data
// pointers by editing only the relocation addends; the section bytes keep the vanilla pointers. Copying
// the raw sections (as the loader did before issue #383) ran vanilla item functions instead of the
// randomizer's, and crashed on one no longer translated. tools/rpx.py apply_internal_relocations is the
// same rule for the recompiler, so the translated text and the loaded image agree.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace rpx_relocs {

constexpr uint32_t kShtSymtab = 2, kShtRela = 4, kShtNobits = 8, kShtRplImports = 0x80000002;
constexpr uint32_t kShfAlloc = 2;
constexpr uint32_t kAddr32 = 1, kAddr16Lo = 4, kAddr16Hi = 5, kAddr16Ha = 6;

struct Section {
    uint32_t type = 0, flags = 0, addr = 0, link = 0, info = 0;
    std::vector<uint8_t> data;  // inflated
};

inline uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// Applies every R_PPC_ADDR32 / ADDR16_LO / ADDR16_HI / ADDR16_HA relocation whose symbol is defined in
// the module (not an import, not undefined) to the allocated section it targets. Returns how many places
// changed; -1 for a relocation outside its section or a bad symbol/section index (nothing is half-applied
// for that relocation; earlier ones stay applied, and the caller refuses the module).
inline long apply(std::vector<Section>& sections) {
    long changed = 0;
    for (const Section& rela : sections) {
        if (rela.type != kShtRela) continue;
        if (rela.info >= sections.size() || rela.link >= sections.size()) return -1;
        Section& target = sections[rela.info];
        const Section& symtab = sections[rela.link];
        if (symtab.type != kShtSymtab) return -1;
        if (!(target.flags & kShfAlloc) || target.type == kShtNobits) continue;
        for (size_t k = 0; k + 12 <= rela.data.size(); k += 12) {
            const uint8_t* r = &rela.data[k];
            uint32_t offset = be32(r), info = be32(r + 4), addend = be32(r + 8);
            uint32_t type = info & 0xFF, symbol = info >> 8;
            if (type != kAddr32 && type != kAddr16Lo && type != kAddr16Hi && type != kAddr16Ha) continue;
            if ((uint64_t)symbol * 16 + 16 > symtab.data.size()) return -1;
            const uint8_t* s = &symtab.data[symbol * 16];
            uint32_t value = be32(s + 4);
            uint16_t shndx = (uint16_t)(s[14] << 8 | s[15]);
            if (shndx == 0 || shndx >= 0xFF00) continue;  // undefined / special: stays as linked
            if (shndx >= sections.size()) return -1;
            if (sections[shndx].type == kShtRplImports) continue;  // imports: the runtime's own slots
            uint32_t v = value + addend;
            uint8_t bytes[4];
            size_t n;
            if (type == kAddr32) {
                bytes[0] = v >> 24; bytes[1] = v >> 16; bytes[2] = v >> 8; bytes[3] = v; n = 4;
            } else {
                uint16_t h = type == kAddr16Lo ? (uint16_t)v : type == kAddr16Hi ? (uint16_t)(v >> 16)
                                                                                : (uint16_t)((v + 0x8000) >> 16);
                bytes[0] = h >> 8; bytes[1] = (uint8_t)h; n = 2;
            }
            uint64_t pos = (uint64_t)offset - target.addr;
            if (offset < target.addr || pos + n > target.data.size()) return -1;
            if (memcmp(&target.data[pos], bytes, n) == 0) continue;
            memcpy(&target.data[pos], bytes, n);
            changed++;
        }
    }
    return changed;
}

}  // namespace rpx_relocs
