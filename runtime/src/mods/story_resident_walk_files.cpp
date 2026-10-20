// SPDX-License-Identifier: MPL-2.0
#include "story_resident_walk_files.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "story_resident_types.h"
#include "story_resident_walk_clip.h"

namespace story_resident_walk_files {
namespace fs = std::filesystem;
namespace {
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
std::string base_name(const std::string& n) {
    const size_t s = n.find_last_of('/');
    return s == std::string::npos ? n : n.substr(s + 1);
}
struct Member {
    std::string name;
    uint32_t start = 0, end = 0;
};
// a SARC's member list (big endian); `data` is where the members' data starts
bool index(const uint8_t* d, size_t n, std::vector<Member>& out, uint32_t& data) {
    if (n < 0x20 || std::memcmp(d, "SARC", 4) || be16(d + 6) != 0xFEFF) return false;
    data = be32(d + 0xC);
    const size_t sfat = be16(d + 4);
    if (sfat + 0xC > n || std::memcmp(d + sfat, "SFAT", 4)) return false;
    const unsigned count = be16(d + sfat + 6);
    const size_t nodes = sfat + be16(d + sfat + 4), sfnt = nodes + size_t(count) * 16;
    if (sfnt + 8 > n || std::memcmp(d + sfnt, "SFNT", 4)) return false;
    const size_t names = sfnt + be16(d + sfnt + 4);
    for (unsigned i = 0; i < count; i++) {
        const uint8_t* node = d + nodes + size_t(i) * 16;
        const uint32_t attr = be32(node + 4);
        Member m;
        m.start = be32(node + 8), m.end = be32(node + 12);
        if (attr & 0x01000000) {
            const size_t at = names + size_t(attr & 0xFFFF) * 4;
            if (at >= n) return false;
            const char* s = reinterpret_cast<const char*>(d + at);
            m.name.assign(s, strnlen(s, n - at));
        }
        if (m.end < m.start) return false;
        out.push_back(std::move(m));
    }
    return true;
}
bool read_range(const fs::path& file, uint64_t at, size_t size, Bytes& out) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    f.seekg(std::streamoff(at));
    out.resize(size);
    f.read(reinterpret_cast<char*>(out.data()), std::streamsize(size));
    return size_t(f.gcount()) == size;
}
}  // namespace

Bytes yaz0(const Bytes& in) {
    if (in.size() < 16 || std::memcmp(in.data(), "Yaz0", 4)) return in;
    const uint32_t size = be32(&in[4]);
    if (size > (64u << 20)) return {};
    Bytes out;
    out.reserve(size);
    size_t p = 16;
    while (out.size() < size) {
        if (p >= in.size()) return {};
        uint8_t code = in[p++];
        for (int i = 0; i < 8 && out.size() < size; i++, code <<= 1) {
            if (code & 0x80) {
                if (p >= in.size()) return {};
                out.push_back(in[p++]);
                continue;
            }
            if (p + 1 >= in.size()) return {};
            const uint8_t b1 = in[p], b2 = in[p + 1];
            p += 2;
            const size_t dist = size_t((b1 & 0xF) << 8 | b2) + 1;
            size_t n = b1 >> 4;
            if (!n) {
                if (p >= in.size()) return {};
                n = size_t(in[p++]) + 0x12;
            } else
                n += 2;
            if (dist > out.size()) return {};
            for (size_t k = 0; k < n && out.size() < size; k++) out.push_back(out[out.size() - dist]);
        }
    }
    return out;
}

Bytes sarc_member(const Bytes& sarc, const std::string& name) {
    std::vector<Member> members;
    uint32_t data = 0;
    if (!index(sarc.data(), sarc.size(), members, data)) return {};
    for (const auto& m : members)
        if (base_name(m.name) == name && uint64_t(data) + m.end <= sarc.size())
            return Bytes(sarc.begin() + std::ptrdiff_t(data + m.start), sarc.begin() + std::ptrdiff_t(data + m.end));
    return {};
}

Bytes archive_bfres(const std::string& game_dir, const std::string& archive) {
    if (archive.empty() || archive.find_first_of("/\\.:") != std::string::npos) return {};
    const fs::path common = fs::path(game_dir) / "content" / "Common";
    const std::string file = archive + ".szs";
    Bytes szs;
    std::error_code ec;
    // the permanent packs first (where the game keeps the villagers), then the loose object archive
    std::vector<fs::path> packs;
    for (const auto& e : fs::directory_iterator(common / "Pack", ec))
        if (e.path().extension() == ".pack") packs.push_back(e.path());
    std::sort(packs.begin(), packs.end());
    for (const auto& pack : packs) {
        const auto size = fs::file_size(pack, ec);
        Bytes head;
        if (ec || !read_range(pack, 0, size_t(std::min<uint64_t>(size, 1u << 20)), head)) continue;
        std::vector<Member> members;
        uint32_t data = 0;
        if (!index(head.data(), head.size(), members, data)) continue;
        for (const auto& m : members)
            if (base_name(m.name) == file && m.end - m.start <= (32u << 20) && uint64_t(data) + m.end <= size) {
                read_range(pack, uint64_t(data) + m.start, m.end - m.start, szs);
                break;
            }
        if (!szs.empty()) break;
    }
    if (szs.empty()) {
        const fs::path loose = common / "Object" / file;
        const auto size = fs::file_size(loose, ec);
        if (ec || size > (32u << 20) || !read_range(loose, 0, size_t(size), szs)) return {};
    }
    return sarc_member(yaz0(szs), archive + ".bfres");
}

bool build(const std::string& game_dir, unsigned kind, Clip& out, std::string& why) {
    namespace clip = story_resident_walk_clip;
    if (kind >= story_resident_types::kBorrowedWalkCount) return why = "no such borrowed walk", false;
    const auto& w = story_resident_types::kBorrowedWalks[kind];
    const Bytes source = archive_bfres(game_dir, w.source), target = archive_bfres(game_dir, w.target);
    if (source.empty() || target.empty()) return why = std::string("the game files have no ") + (source.empty() ? w.source : w.target) + " archive", false;
    std::vector<clip::Bone> from, to;
    clip::Clip walk, source_idle, target_idle;
    clip::Built built;
    if (!clip::skeleton(source, w.source_model, from, why) || !clip::skeleton(target, w.target_model, to, why) || !clip::clip(source, w.clip, walk, why) ||
        !clip::clip(source, w.source_idle, source_idle, why) || !clip::clip(target, w.target_idle, target_idle, why) ||
        !clip::retarget(from, walk, to, w.factor, w.name, built, why, &source_idle, &target_idle))
        return false;
    out.fska = clip::encode(built, to);
    out.frames = built.end + 1;
    out.stride = built.stride;
    out.largest_step = built.largest_step;
    return true;
}

}  // namespace story_resident_walk_files
