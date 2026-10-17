// World editor: stage and room tables from the game's files (stage_files.h).
#include "stage_files.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>

namespace editor {
namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
float bef(const uint8_t* p) {
    const uint32_t u = be32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// Yaz0 (the game's compression); empty on bad data
Bytes yaz0(const Bytes& in) {
    if (in.size() < 16 || std::memcmp(in.data(), "Yaz0", 4) != 0) return in;
    const uint32_t size = be32(&in[4]);
    if (size > 256u << 20) return {};
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
            if (n == 0) {
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

// A SARC's members (name -> [start, end) in the data); big-endian (the Wii U's) only.
struct Member {
    std::string name;
    uint32_t start = 0, end = 0;
};
bool sarc_index(const uint8_t* d, size_t n, std::vector<Member>& out, uint32_t& data) {
    if (n < 0x20 || std::memcmp(d, "SARC", 4) != 0 || be16(d + 6) != 0xFEFF) return false;
    data = be32(d + 0xC);
    const size_t sfat = be16(d + 4);
    if (sfat + 0xC > n || std::memcmp(d + sfat, "SFAT", 4) != 0) return false;
    const unsigned count = be16(d + sfat + 6);
    const size_t nodes = sfat + be16(d + sfat + 4), sfnt = nodes + size_t(count) * 16;
    if (sfnt + 8 > n || std::memcmp(d + sfnt, "SFNT", 4) != 0) return false;
    const size_t names = sfnt + be16(d + sfnt + 4);
    for (unsigned i = 0; i < count; i++) {
        const uint8_t* node = d + nodes + size_t(i) * 16;
        const uint32_t attr = be32(node + 4);
        Member m;
        m.start = be32(node + 8);
        m.end = be32(node + 12);
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
    out.resize(size_t(f.gcount()));
    return out.size() == size;
}
// The packs' member lists, read once (their headers only).
struct Pack {
    fs::path file;
    uint32_t data = 0;
    std::vector<Member> members;
};
const std::vector<Pack>& packs(const std::string& game_dir) {
    static std::string for_dir;
    static std::vector<Pack> cached;
    if (for_dir == game_dir) return cached;
    for_dir = game_dir;
    cached.clear();
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::path(game_dir) / "content" / "Common" / "Pack", ec)) {
        if (e.path().extension() != ".pack") continue;
        Bytes head;
        const auto size = fs::file_size(e.path(), ec);
        if (ec || !read_range(e.path(), 0, size_t(std::min<uint64_t>(size, 1u << 20)), head)) continue;
        Pack p;
        p.file = e.path();
        if (sarc_index(head.data(), head.size(), p.members, p.data)) cached.push_back(std::move(p));
    }
    return cached;
}
std::vector<std::string> stage_dir_names(const std::string& game_dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::path(game_dir) / "content" / "Common" / "Stage", ec)) names.push_back(e.path().filename().string());
    return names;
}
}  // namespace

std::vector<std::pair<std::string, std::vector<int>>> stage_rooms_from_names(const std::vector<std::string>& names) {
    static const std::regex room(R"(^([A-Za-z0-9_]{1,7})_Room([0-9]{1,2})\.szs$)");
    std::map<std::string, std::vector<int>> stages;
    for (const auto& name : names) {
        std::smatch m;
        const std::string base = name.substr(name.find_last_of('/') == std::string::npos ? 0 : name.find_last_of('/') + 1);
        if (!std::regex_match(base, m, room)) continue;
        const int n = std::stoi(m[2]);
        if (n > 63) continue;
        auto& list = stages[m[1]];
        if (std::find(list.begin(), list.end(), n) == list.end()) list.push_back(n);
    }
    std::vector<std::pair<std::string, std::vector<int>>> out;
    for (auto& [stage, rooms] : stages) {
        std::sort(rooms.begin(), rooms.end());
        out.push_back({stage, rooms});
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}

std::vector<Spawn> parse_spawns(const std::vector<uint8_t>& d) {
    std::vector<Spawn> out;
    if (d.size() < 4) return out;
    const uint32_t chunks = be32(d.data());
    if (chunks > 256 || 4 + size_t(chunks) * 12 > d.size()) return out;
    for (uint32_t c = 0; c < chunks; c++) {
        const uint8_t* h = d.data() + 4 + size_t(c) * 12;
        if (std::memcmp(h, "PLYR", 4) != 0) continue;
        const uint32_t count = be32(h + 4), at = be32(h + 8);
        if (count > 256 || uint64_t(at) + uint64_t(count) * 0x20 > d.size()) continue;
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t* e = d.data() + at + size_t(i) * 0x20;
            Spawn s;
            s.room = int(be32(e + 8) & 0x3F);
            s.position = {bef(e + 0xC), bef(e + 0x10), bef(e + 0x14)};
            s.angle = be16(e + 0x1A);
            s.point = e[0x1D];
            out.push_back(s);
        }
    }
    return out;
}

std::vector<Spawn> find_spawns(const std::vector<uint8_t>& d) {
    auto tag_ok = [](const uint8_t* t) {
        for (int i = 0; i < 4; i++)
            if (!((t[i] >= 'A' && t[i] <= 'Z') || (t[i] >= '0' && t[i] <= '9') || (t[i] >= 'a' && t[i] <= 'z'))) return false;
        return true;
    };
    for (size_t at = 0; at + 12 <= d.size(); at++) {
        if (std::memcmp(&d[at], "PLYR", 4) != 0) continue;
        // the chunk table: a count, then 12-byte entries; PLYR is entry i of it
        for (size_t i = 0; i < 64 && at >= 4 + 12 * i; i++) {
            const size_t start = at - 4 - 12 * i;
            const uint32_t chunks = be32(&d[start]);
            if (chunks <= i || chunks > 64 || start + 4 + size_t(chunks) * 12 > d.size()) continue;
            bool table = true;
            for (uint32_t c = 0; c < chunks && table; c++) table = tag_ok(&d[start + 4 + size_t(c) * 12]);
            if (!table) continue;
            auto spawns = parse_spawns(std::vector<uint8_t>(d.begin() + std::ptrdiff_t(start), d.end()));
            if (!spawns.empty()) return spawns;
        }
    }
    return {};
}

std::vector<std::pair<std::string, std::vector<int>>> game_stage_rooms(const std::string& game_dir) {
    std::vector<std::string> names = stage_dir_names(game_dir);
    for (const auto& p : packs(game_dir))
        for (const auto& m : p.members) names.push_back(m.name);
    return stage_rooms_from_names(names);
}

std::vector<Spawn> room_spawns(const std::string& game_dir, const std::string& stage, int room) {
    const std::string name = stage + "_Room" + std::to_string(room) + ".szs";
    Bytes file;
    const fs::path loose = fs::path(game_dir) / "content" / "Common" / "Stage" / name;
    std::error_code ec;
    if (fs::is_regular_file(loose, ec)) {
        const auto size = fs::file_size(loose, ec);
        if (ec || size > (64u << 20) || !read_range(loose, 0, size_t(size), file)) return {};
    } else {
        for (const auto& p : packs(game_dir))
            for (const auto& m : p.members) {
                const std::string base = m.name.substr(m.name.find_last_of('/') == std::string::npos ? 0 : m.name.find_last_of('/') + 1);
                if (base == name && m.end - m.start <= (64u << 20)) read_range(p.file, uint64_t(p.data) + m.start, m.end - m.start, file);
            }
        if (file.empty()) return {};
    }
    const Bytes sarc = yaz0(file);
    std::vector<Member> members;
    uint32_t data = 0;
    if (!sarc_index(sarc.data(), sarc.size(), members, data)) return {};
    // The room's dzr is a file embedded in the room's BFRES (HD), or a member of its own: found by its
    // chunk table, whose PLYR entry points at the spawn points.
    for (const auto& m : members) {
        if (uint64_t(data) + m.end > sarc.size()) continue;
        const Bytes member(sarc.begin() + data + m.start, sarc.begin() + data + m.end);
        auto spawns = find_spawns(member);
        if (!spawns.empty()) return spawns;
    }
    return {};
}

}  // namespace editor
