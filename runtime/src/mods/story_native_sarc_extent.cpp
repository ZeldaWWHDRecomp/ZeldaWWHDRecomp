// SPDX-License-Identifier: MPL-2.0
#include "story_native_sarc_extent.h"
#include <array>
#include <limits>

namespace story_native_sarc_extent { namespace {
constexpr uint32_t kMaxArchive = 64u * 1024u * 1024u;
constexpr uint32_t kMaxEntries = 8192;
uint16_t be16(const uint8_t* b) { return uint16_t(b[0]) << 8 | b[1]; }
uint32_t be32(const uint8_t* b) {
    return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 |
           uint32_t(b[2]) << 8 | b[3];
}
bool range(uint32_t offset, uint32_t length, uint32_t total) {
    return uint64_t(offset) + length <= total;
}
bool load(const Read& read, uint32_t base, uint32_t total,
          uint32_t offset, std::span<uint8_t> dst) {
    return range(offset, uint32_t(dst.size()), total) &&
           uint64_t(base) + offset + dst.size() <= uint64_t(UINT32_MAX) + 1 &&
           read(base + offset, dst);
}
bool name_at(const Read& read, uint32_t base, uint32_t total,
             uint32_t at, std::string_view name) {
    if (name.empty() || name.size() > 127 || !range(at, uint32_t(name.size()+1), total))
        return false;
    std::array<uint8_t, 128> text{};
    if (!load(read,base,total,at,std::span<uint8_t>(text.data(),name.size()+1)) ||
        text[name.size()] != 0) return false;
    for (size_t i=0;i<name.size();++i) if (text[i]!=uint8_t(name[i])) return false;
    return true;
}
} // namespace

bool find(const Read& read, uint32_t archive_resource, std::string_view name,
          uint32_t expected_file, Extent& out) {
    out={};
    if (!read || !archive_resource || !expected_file || name.empty() || name.size()>127 ||
        uint64_t(archive_resource)+0x20 > uint64_t(UINT32_MAX)+1) return false;
    try {
        std::array<uint8_t,12> descriptor{};
        if (!read(archive_resource+0x14,descriptor)) return false;
        const uint32_t base=be32(descriptor.data());
        const uint32_t bytes=be32(descriptor.data()+4);
        const uint32_t allocation=be32(descriptor.data()+8);
        if (!base || bytes<0x20 || bytes>allocation || allocation>kMaxArchive ||
            uint64_t(base)+allocation>uint64_t(UINT32_MAX)+1) return false;
        std::array<uint8_t,0x20> header{};
        if (!load(read,base,allocation,0,header) ||
            header[0]!='S'||header[1]!='A'||header[2]!='R'||header[3]!='C'||
            be16(header.data()+4)!=0x14 || be16(header.data()+6)!=0xfeff ||
            be32(header.data()+8)!=bytes || be16(header.data()+0x10)!=0x100 ||
            header[0x14]!='S'||header[0x15]!='F'||header[0x16]!='A'||header[0x17]!='T'||
            be16(header.data()+0x18)!=0x0c) return false;
        const uint32_t data=be32(header.data()+0x0c);
        const uint32_t count=be16(header.data()+0x1a);
        const uint32_t hash_key=be32(header.data()+0x1c);
        if (!count || count>kMaxEntries || !hash_key ||
            !range(0x20,count*16+8,bytes)) return false;
        const uint32_t sfnt=0x20+count*16;
        std::array<uint8_t,8> names_header{};
        if (!load(read,base,bytes,sfnt,names_header) ||
            names_header[0]!='S'||names_header[1]!='F'||names_header[2]!='N'||names_header[3]!='T'||
            be16(names_header.data()+4)!=8) return false;
        const uint32_t names=sfnt+8;
        if (data<names || data>bytes) return false;
        uint32_t wanted_hash=0;
        for (unsigned char ch:name) wanted_hash=wanted_hash*hash_key+ch;
        bool found=false;
        Extent candidate{};
        for (uint32_t i=0;i<count;++i) {
            std::array<uint8_t,16> entry{};
            if (!load(read,base,bytes,0x20+i*16,entry)) return false;
            const uint32_t hash=be32(entry.data());
            const uint32_t attr=be32(entry.data()+4);
            const uint32_t start=be32(entry.data()+8);
            const uint32_t end=be32(entry.data()+12);
            if (end<start || !range(data,end,bytes)) return false;
            if (hash!=wanted_hash) continue;
            if (!(attr&0x01000000u)) return false;
            const uint64_t at=uint64_t(names)+uint64_t(attr&0x00ffffffu)*4;
            if (at>UINT32_MAX || at>=data) return false;
            if (!name_at(read,base,data,uint32_t(at),name)) continue;
            if (found || end==start || uint64_t(base)+data+start!=expected_file)
                return false;
            candidate={expected_file,end-start,base,bytes,allocation};
            found=true;
        }
        if (!found) return false;
        out=candidate;
        return true;
    } catch (...) { return false; }
}
} // namespace story_native_sarc_extent
