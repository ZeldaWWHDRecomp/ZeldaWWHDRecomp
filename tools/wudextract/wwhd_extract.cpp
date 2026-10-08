// wwhd-extract: reads the game from a Wii U disc image (.wud/.wux) or a Cemu Wii U archive (.wua)
// and extracts it; used by the installer.
//
// Disc images: native port of tools/wudextract.py (itself a port of Cemu's
// src/Cafe/Filesystem/WUD/wud.cpp and FST/FST.cpp, Copyright (c) Cemu contributors, Mozilla Public
// License 2.0, see runtime/third_party/cemu/LICENSE.txt), without Python or pycryptodome.
// Cemu archives: ZArchive files (zarchive.h), already decrypted, so no keys; one folder per title
// (<title id>_v<version>, e.g. 0005000010143500_v0 for the game, 0005000e10143500_v.. for an update).
//
// usage:
//   wwhd-extract [KEYS] [--progress] info    IMAGE          check the keys, print the title
//   wwhd-extract [KEYS]              list    IMAGE          list the game partition's files
//   wwhd-extract [KEYS] [--progress] extract IMAGE OUTDIR   extract the game partition
//   wwhd-extract [--title T]         info    ARCHIVE.wua    list the titles (and the selected one)
//   wwhd-extract                     list    ARCHIVE.wua    list all files
//   wwhd-extract [--title T] [--progress] extract ARCHIVE.wua OUTDIR
//                                   check the archive's SHA-256, then extract one title's folder
//                                   (code, content, meta) into OUTDIR
//
// KEYS (no keys are included in this project; they come from your own console):
//   --disc-key FILE     the disc key (default: IMAGE with the extension replaced by .key)
//   --common-key FILE   the Wii U common key (default: WIIU_COMMON_KEY environment variable,
//                       then common.key next to IMAGE or in the current directory)
//   --keys-stdin        read "disc <32 hex digits>" / "common <32 hex digits>" lines from stdin
// A key file holds 16 raw bytes or 32 hex digits (whitespace ignored). Keys are never printed.
// --title T: a title id (16 hex digits; the highest version of it is used) or a folder name
// (0005000010143500_v0). Without it an archive with a single title uses that one.
// --only GLOB (repeatable): extract only the files whose path in the title (code/..., content/...,
// meta/...) matches one of the patterns, without case; '*' matches any run of characters ('/' too),
// '?' one character. The setup uses it to take only the language files of a second disc
// (content/Common/Pack/permanent_2d_*.pack, meta/meta.xml).
//
// info on an archive prints "format wua", one "title ID VERSION FOLDER FILES BYTES" line per title
// folder, and for the selected title "selected FOLDER", "title_id", "version", "files", "bytes".
// extract --progress prints "phase verify" / "phase extract", each followed by "progress DONE TOTAL".
//
// Exit codes: 0 ok, 2 usage, 3 disc key missing/malformed, 4 disc key does not match the image,
// 5 common key missing/malformed, 6 common key wrong, 7 not a Wii U disc image or archive / unreadable,
// 8 corrupt image or archive (hash mismatch), 9 cannot write output (disk full, permissions),
// 10 the archive does not contain the requested title (or several titles and no --title).
#include "crypto.h"
#include "zarchive.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace fs = std::filesystem;
using namespace wudcrypto;

namespace {

constexpr uint64_t SECTOR = 0x8000;
constexpr uint64_t BLOCK_SIZE = 0x10000;
constexpr uint64_t BLOCK_HASH_SIZE = 0x400;
constexpr uint64_t BLOCK_FILE_SIZE = 0xFC00;

struct Fail {
    int code;
    std::string msg;
};
[[noreturn]] void fail(int code, const std::string& msg) { throw Fail{code, msg}; }

fs::path upath(const std::string& s) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}

std::string ustr(const fs::path& p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t le32(const uint8_t* p) { return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]; }
uint64_t le64(const uint8_t* p) { return ((uint64_t)le32(p + 4) << 32) | le32(p); }

// ---- keys

struct Key {
    uint8_t b[16];
    bool set = false;
};

bool parse_key(const std::string& data, Key& k) {
    if (data.size() == 16) {
        memcpy(k.b, data.data(), 16);
        k.set = true;
        return true;
    }
    std::string h;
    for (char c : data)
        if (!isspace((unsigned char)c)) h += c;
    if (h.size() == 34 && (h.compare(0, 2, "0x") == 0 || h.compare(0, 2, "0X") == 0)) h = h.substr(2);
    if (h.size() != 32) return false;
    for (int i = 0; i < 16; i++) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = nib(h[2 * i]), lo = nib(h[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        k.b[i] = (uint8_t)(hi << 4 | lo);
    }
    k.set = true;
    return true;
}

bool read_small_file(const fs::path& p, std::string& out) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return false;
    if (fs::file_size(p, ec) > 4096) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

void load_key_file(const fs::path& p, Key& k, int code, const char* what) {
    std::string d;
    if (!read_small_file(p, d)) fail(code, std::string(what) + " not found or unreadable: " + ustr(p));
    if (!parse_key(d, k))
        fail(code, std::string(what) + " in " + ustr(p) +
                       " is malformed: expected 16 raw bytes or one line of 32 hex digits");
}

// ---- disc image

struct Wud {
    std::ifstream f;
    bool compressed = false;
    uint32_t sector_size = 0;
    uint64_t size = 0, sector_base = 0;
    std::vector<uint32_t> index;

    explicit Wud(const fs::path& p) : f(p, std::ios::binary) {
        if (!f) fail(7, "cannot open the disc image " + ustr(p));
        uint8_t hdr[32] = {};
        f.read((char*)hdr, 32);
        if (f.gcount() != 32) fail(7, "the disc image is too small");
        if (le32(hdr) == 0x30585557 && le32(hdr + 4) == 0x1099D02E) {  // "WUX0"
            compressed = true;
            sector_size = le32(hdr + 8);
            size = le64(hdr + 16);
            if (sector_size < 0x100 || sector_size > 0x1000000 || size == 0 || size > (1ull << 40))
                fail(7, "corrupt .wux header");
            uint64_t n = (size + sector_size - 1) / sector_size;
            std::vector<uint8_t> raw(n * 4);
            f.read((char*)raw.data(), (std::streamsize)raw.size());
            if ((uint64_t)f.gcount() != raw.size()) fail(7, "truncated .wux index");
            index.resize(n);
            for (uint64_t i = 0; i < n; i++) index[i] = le32(&raw[i * 4]);
            uint64_t off = 32 + 4 * n;
            sector_base = (off + sector_size - 1) / sector_size * sector_size;
        } else {
            f.seekg(0, std::ios::end);
            size = (uint64_t)f.tellg();
        }
    }

    void raw_read(uint64_t off, uint8_t* dst, uint64_t len) {
        f.clear();
        f.seekg((std::streamoff)off);
        f.read((char*)dst, (std::streamsize)len);
        if ((uint64_t)f.gcount() != len) fail(7, "the disc image is truncated or unreadable");
    }

    void read(uint64_t offset, uint8_t* dst, uint64_t length) {
        if (offset + length > size) fail(7, "read beyond the end of the disc image (truncated dump?)");
        if (!compressed) return raw_read(offset, dst, length);
        while (length > 0) {
            uint64_t sec_off = offset % sector_size;
            uint64_t n = std::min<uint64_t>(sector_size - sec_off, length);
            uint64_t real = index[offset / sector_size];
            raw_read(sector_base + real * sector_size + sec_off, dst, n);
            dst += n;
            offset += n;
            length -= n;
        }
    }
};

struct Entry {
    std::string path;
    bool is_dir;
    uint32_t offset, size;
    uint16_t cluster;
    uint8_t flags;
};

struct Cluster {
    uint32_t offset, size;
    uint8_t hash_mode;
};

struct FST {
    Wud& wud;
    uint64_t base;
    Aes128Dec key;
    uint32_t offset_factor = 0;
    std::vector<Cluster> clusters;
    std::vector<Entry> entries;

    // returns false (instead of failing) when the FST magic is wrong: the caller knows which key it was
    FST(Wud& w, uint64_t base_, uint64_t fst_offset, uint32_t fst_size, const uint8_t k[16], bool& ok)
        : wud(w), base(base_), key(k) {
        ok = false;
        if (fst_size < 0x20 || fst_size > 0x4000000) return;
        uint64_t padded = (fst_size + 15) & ~15ull;
        std::vector<uint8_t> d(padded);
        wud.read(base + fst_offset, d.data(), padded);
        uint8_t iv[16] = {};
        aes128_cbc_decrypt(key, iv, d.data(), padded);
        d.resize(fst_size);
        if (be32(&d[0]) != 0x46535400) return;  // "FST\0"
        offset_factor = be32(&d[4]);
        uint32_t ncluster = be32(&d[8]);
        auto need = [&](uint64_t end) {
            if (end > d.size()) fail(7, "corrupt file table");
        };
        need(0x20 + (uint64_t)ncluster * 0x20);
        for (uint32_t i = 0; i < ncluster; i++) {
            const uint8_t* c = &d[0x20 + i * 0x20];
            clusters.push_back({be32(c), be32(c + 4), c[0x14]});
        }
        uint64_t ft = 0x20 + (uint64_t)ncluster * 0x20;
        need(ft + 0x10);
        uint32_t nentries = be32(&d[ft + 8]);
        uint64_t names = ft + (uint64_t)nentries * 0x10;
        need(names);
        auto name_at = [&](uint32_t o) {
            uint64_t s = names + o;
            need(s + 1);
            uint64_t e = s;
            while (e < d.size() && d[e]) e++;
            return std::string((const char*)&d[s], (size_t)(e - s));
        };
        std::vector<std::pair<std::string, uint32_t>> stack{{"", nentries}};
        for (uint32_t i = 0; i < nentries; i++) {
            while (stack.size() > 1 && i >= stack.back().second) stack.pop_back();
            const uint8_t* p = &d[ft + (uint64_t)i * 0x10];
            uint32_t tno = be32(p);
            Entry e;
            e.flags = (uint8_t)(tno >> 24);
            std::string name = i ? name_at(tno & 0xFFFFFF) : "";
            e.path = stack.back().first.empty() ? name : stack.back().first + "/" + name;
            e.is_dir = e.flags & 1;
            e.offset = be32(p + 4);
            e.size = be32(p + 8);
            e.cluster = be16(p + 14);
            if (!e.is_dir && e.cluster >= clusters.size()) fail(7, "corrupt file table (cluster index)");
            if (e.is_dir && i) stack.push_back({e.path, e.size});
            entries.push_back(std::move(e));
        }
        ok = true;
    }

    uint64_t cluster_base(uint16_t cl) const { return base + (uint64_t)clusters[cl].offset * SECTOR; }

    template <class Sink>
    void read_file(const Entry& e, Sink&& out) {
        uint8_t mode = clusters[e.cluster].hash_mode;
        uint64_t pos = (uint64_t)e.offset * offset_factor;
        uint64_t remaining = e.size;
        uint64_t cbase = cluster_base(e.cluster);
        if (mode == 2) {  // hashed: 64 KiB blocks of 1 KiB hashes + 63 KiB data
            uint64_t blk = pos / BLOCK_FILE_SIZE, within = pos % BLOCK_FILE_SIZE;
            std::vector<uint8_t> raw(BLOCK_SIZE);
            while (remaining > 0) {
                wud.read(cbase + blk * BLOCK_SIZE, raw.data(), BLOCK_SIZE);
                uint8_t iv[16] = {};
                aes128_cbc_decrypt(key, iv, raw.data(), BLOCK_HASH_SIZE);
                const uint8_t* h0 = raw.data() + (blk % 16) * 20;
                uint8_t iv2[16];
                memcpy(iv2, h0, 16);
                uint8_t* fdata = raw.data() + BLOCK_HASH_SIZE;
                aes128_cbc_decrypt(key, iv2, fdata, BLOCK_FILE_SIZE);
                uint8_t digest[20];
                sha1(fdata, BLOCK_FILE_SIZE, digest);
                if (memcmp(digest, h0, 20))
                    fail(8, "hash mismatch in " + e.path + " (block " + std::to_string(blk) +
                                "): the disc image is damaged");
                uint64_t n = std::min(remaining, BLOCK_FILE_SIZE - within);
                out(fdata + within, n);
                remaining -= n;
                within = 0;
                blk++;
            }
        } else {  // raw: CBC over the whole cluster; IV = cluster index for its first sector
            uint64_t blk = pos / SECTOR, within = pos % SECTOR;
            uint8_t iv[16] = {};
            if (blk == 0) {
                iv[0] = (uint8_t)(e.cluster >> 8);
                iv[1] = (uint8_t)e.cluster;
            } else {
                wud.read(cbase + blk * SECTOR - 16, iv, 16);
            }
            const uint64_t chunk_sectors = 64;
            std::vector<uint8_t> buf;
            while (remaining > 0) {
                uint64_t need_bytes = within + remaining;
                uint64_t sectors = std::min<uint64_t>(chunk_sectors, (need_bytes + SECTOR - 1) / SECTOR);
                buf.resize(sectors * SECTOR);
                wud.read(cbase + blk * SECTOR, buf.data(), buf.size());
                aes128_cbc_decrypt(key, iv, buf.data(), buf.size());
                uint64_t n = std::min<uint64_t>(remaining, buf.size() - within);
                out(buf.data() + within, n);
                remaining -= n;
                within = 0;
                blk += sectors;
            }
        }
    }
};

struct Disc {
    std::unique_ptr<Wud> wud;
    std::unique_ptr<FST> gm;
    std::string title_id;
};

Disc open_disc(const fs::path& image, Key disc_key, Key common_key) {
    Disc disc;
    disc.wud = std::make_unique<Wud>(image);
    Wud& wud = *disc.wud;
    uint8_t magic[4];
    if (wud.size < SECTOR * 4) fail(7, "not a Wii U disc image (too small)");
    wud.read(SECTOR * 2, magic, 4);
    if (be32(magic) != 0xCC549EB9) fail(7, "not a Wii U disc image (.wud/.wux): " + ustr(image));
    if (!disc_key.set) fail(3, "no disc key given");
    std::vector<uint8_t> pt(SECTOR);
    wud.read(SECTOR * 3, pt.data(), SECTOR);
    {
        uint8_t iv[16] = {};
        aes128_cbc_decrypt(Aes128Dec(disc_key.b), iv, pt.data(), pt.size());
    }
    if (be32(&pt[0]) != 0xCCA6E67B)
        fail(4, "the disc key does not match this disc image (it must be the key dumped together with this "
                "disc: 16 raw bytes or 32 hex digits)");
    uint32_t nparts = be32(&pt[0x1C]);
    if (nparts == 0 || nparts > 15) fail(7, "corrupt partition table");
    struct Part {
        std::string name;
        uint64_t addr;
    };
    std::vector<Part> parts;
    for (uint32_t i = 0; i < nparts; i++) {
        const uint8_t* ent = &pt[0x800 + i * 0x80];
        std::string name((const char*)ent, strnlen((const char*)ent, 31));
        parts.push_back({name, (uint64_t)be32(ent + 0x20) * SECTOR});
    }
    auto find = [&](const char* prefix) -> int {
        for (size_t i = 0; i < parts.size(); i++)
            if (parts[i].name.compare(0, 2, prefix) == 0) return (int)i;
        return -1;
    };
    int si_idx = find("SI"), gm_idx = find("GM");
    if (si_idx < 0 || gm_idx < 0) fail(7, "the disc has no system/game partition");
    auto part_fst = [&](uint64_t base, const uint8_t* key, bool& ok) {
        uint8_t ph[0x60];
        wud.read(base, ph, sizeof ph);
        uint32_t fst_size = be32(ph + 0x14), fst_sector = be32(ph + 0x18);
        return std::make_unique<FST>(wud, base, (uint64_t)fst_sector * SECTOR, fst_size, key, ok);
    };
    bool ok = false;
    auto si = part_fst(parts[si_idx].addr, disc_key.b, ok);
    if (!ok) fail(4, "the disc key does not decrypt the system partition (wrong key or damaged image)");
    char tik_name[32];
    snprintf(tik_name, sizeof tik_name, "%02x/title.tik", gm_idx);
    const Entry* tik_e = nullptr;
    for (auto& e : si->entries)
        if (!e.is_dir && e.path == tik_name) tik_e = &e;
    if (!tik_e) fail(7, "the disc has no ticket for its game partition");
    std::vector<uint8_t> tik;
    si->read_file(*tik_e, [&](const uint8_t* p, uint64_t n) { tik.insert(tik.end(), p, p + n); });
    if (tik.size() < 0x1E4) fail(7, "corrupt ticket");
    char tid[17];
    for (int i = 0; i < 8; i++) snprintf(tid + 2 * i, 3, "%02x", tik[0x1DC + i]);
    disc.title_id = tid;
    if (!common_key.set) fail(5, "no Wii U common key given");
    uint8_t title_key[16], iv[16] = {};
    memcpy(title_key, &tik[0x1BF], 16);
    memcpy(iv, &tik[0x1DC], 8);
    aes128_cbc_decrypt(Aes128Dec(common_key.b), iv, title_key, 16);
    disc.gm = part_fst(parts[gm_idx].addr, title_key, ok);
    memset(title_key, 0, sizeof title_key);
    if (!ok)
        fail(6, "the Wii U common key is wrong: the game partition could not be decrypted (the common key is "
                "the same for every Wii U; 16 raw bytes or 32 hex digits)");
    return disc;
}

bool selected(const Entry& e) { return !e.is_dir && !(e.flags & 0x80); }

// ---- Cemu Wii U archive (.wua)

struct Title {
    std::string id;      // 16 hex digits, lower case
    unsigned version;
    std::string folder;  // as named in the archive
    uint32_t node;
    uint64_t files = 0, bytes = 0;
};

bool is_hex(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](char c) { return isxdigit((unsigned char)c) != 0; });
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// case-insensitive glob: '*' any run of characters (also '/'), '?' one character
bool glob_match(const std::string& pat, const std::string& s) {
    size_t p = 0, i = 0, star = std::string::npos, mark = 0;
    while (i < s.size()) {
        if (p < pat.size() && (pat[p] == '?' || tolower((unsigned char)pat[p]) == tolower((unsigned char)s[i]))) {
            p++, i++;
        } else if (p < pat.size() && pat[p] == '*') {
            star = p++, mark = i;
        } else if (star != std::string::npos) {
            p = star + 1, i = ++mark;
        } else {
            return false;
        }
    }
    while (p < pat.size() && pat[p] == '*') p++;
    return p == pat.size();
}

// --only patterns: empty extracts everything
std::vector<std::string> g_only;
bool wanted(const std::string& path) {
    if (g_only.empty()) return true;
    for (auto& g : g_only)
        if (glob_match(g, path)) return true;
    return false;
}

// a name that is safe as one path component on every system
bool safe_name(const std::string& n) {
    if (n.empty() || n == "." || n == "..") return false;
    for (char c : n)
        if (c == '/' || c == '\\' || c == ':' || c == '\0') return false;
    return true;
}

// files below a folder, depth first: (path relative to the folder, node)
void walk(const zarchive::Reader& zr, uint32_t dir, const std::string& prefix,
          std::vector<std::pair<std::string, uint32_t>>& out, int depth = 0) {
    if (depth > 64) fail(7, "corrupt archive (folders nested too deep)");
    for (uint32_t c : zr.children(dir)) {
        const auto& nd = zr.node(c);
        if (!safe_name(nd.name)) fail(7, "unsafe path in the archive: " + prefix + nd.name);
        if (nd.is_file) out.push_back({prefix + nd.name, c});
        else walk(zr, c, prefix + nd.name + "/", out, depth + 1);
    }
}

// title folders at the top of the archive: <16 hex digits>_v<decimal version>
std::vector<Title> archive_titles(const zarchive::Reader& zr) {
    std::vector<Title> titles;
    for (uint32_t c : zr.children(zr.root())) {
        const auto& nd = zr.node(c);
        const std::string& n = nd.name;
        if (nd.is_file || n.size() < 19 || n.size() > 26 || !is_hex(n.substr(0, 16)) || lower(n.substr(16, 2)) != "_v")
            continue;
        std::string v = n.substr(18);
        if (v.empty() || !std::all_of(v.begin(), v.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) continue;
        Title t{lower(n.substr(0, 16)), (unsigned)std::stoul(v), n, c};
        std::vector<std::pair<std::string, uint32_t>> files;
        walk(zr, c, "", files);
        for (auto& f : files) t.files++, t.bytes += zr.node(f.second).size;
        titles.push_back(t);
    }
    return titles;
}

std::string describe(const std::vector<Title>& titles) {
    std::string s;
    for (auto& t : titles) s += (s.empty() ? "" : ", ") + t.folder;
    return s.empty() ? "no title folders" : s;
}

const Title* pick_title(const std::vector<Title>& titles, const std::string& want) {
    if (want.empty()) {
        if (titles.size() == 1) return &titles[0];
        return nullptr;
    }
    const Title* best = nullptr;
    for (auto& t : titles) {
        if (lower(want) == lower(t.folder)) return &t;
        if (lower(want) == t.id && (!best || t.version > best->version)) best = &t;
    }
    return best;
}

int run_archive(const std::string& cmd, const fs::path& path, const std::string& outdir, const std::string& title,
                bool progress) {
    std::unique_ptr<zarchive::Reader> zr;
    try {
        zr = std::make_unique<zarchive::Reader>(path);
    } catch (const zarchive::Error& e) {
        fail(e.damaged ? 8 : 7, e.msg + ": " + ustr(path));
    }
    if (cmd == "list") {
        std::vector<std::pair<std::string, uint32_t>> files;
        walk(*zr, zr->root(), "", files);
        for (auto& f : files) printf("%10llu  %s\n", (unsigned long long)zr->node(f.second).size, f.first.c_str());
        return 0;
    }
    std::vector<Title> titles = archive_titles(*zr);
    const Title* t = pick_title(titles, title);
    if (cmd == "info") {
        printf("format wua\n");
        for (auto& x : titles)
            printf("title %s %u %s %llu %llu\n", x.id.c_str(), x.version, x.folder.c_str(), (unsigned long long)x.files,
                   (unsigned long long)x.bytes);
        if (t)
            printf("selected %s\ntitle_id %s\nversion %u\nfiles %llu\nbytes %llu\n", t->folder.c_str(), t->id.c_str(),
                   t->version, (unsigned long long)t->files, (unsigned long long)t->bytes);
        fflush(stdout);
    }
    if (titles.empty()) fail(10, "the archive contains no Wii U title folders (named like 0005000010143500_v0)");
    if (cmd == "info" && !t && title.empty()) return 0;  // several titles, none asked for: the list is the answer
    if (!t && title.empty())
        fail(10, "the archive contains several titles (" + describe(titles) + "): choose one with --title");
    if (!t) fail(10, "the archive does not contain title " + title + " (it contains " + describe(titles) + ")");
    if (cmd == "info") return 0;

    std::vector<std::pair<std::string, uint32_t>> files, all;
    walk(*zr, t->node, "", all);
    uint64_t wanted_bytes = 0;
    for (auto& f : all)
        if (wanted(f.first)) files.push_back(f), wanted_bytes += zr->node(f.second).size;
    try {
        if (progress) printf("phase verify\n");
        uint64_t last = 0;
        bool ok = zr->verify([&](uint64_t done, uint64_t total) {
            if (progress && (done == 0 || done == total || done - last >= (32u << 20))) {
                last = done;
                printf("progress %llu %llu\n", (unsigned long long)done, (unsigned long long)total);
                fflush(stdout);
            }
        });
        if (!ok)
            fail(8, "the archive is damaged: its SHA-256 does not match (incomplete download or copy, or a disk error)");

        fs::path out = upath(outdir);
        uint64_t done = 0, last_report = 0;
        if (progress) printf("phase extract\nprogress 0 %llu\n", (unsigned long long)wanted_bytes), fflush(stdout);
        for (auto& f : files) {
            fs::path dst = out / upath(f.first);
            std::error_code ec;
            fs::create_directories(dst.parent_path(), ec);
            if (ec) fail(9, "cannot create " + ustr(dst.parent_path()) + ": " + ec.message());
            std::ofstream o(dst, std::ios::binary | std::ios::trunc);
            if (!o) fail(9, "cannot write " + ustr(dst));
            zr->read_file(f.second, [&](const uint8_t* p, uint64_t n) {
                o.write((const char*)p, (std::streamsize)n);
                if (!o) fail(9, "cannot write " + ustr(dst) + " (disk full?)");
                done += n;
                if (progress && done - last_report >= (32u << 20)) {
                    last_report = done;
                    printf("progress %llu %llu\n", (unsigned long long)done, (unsigned long long)wanted_bytes);
                    fflush(stdout);
                }
            });
            o.close();
            if (!o) fail(9, "cannot write " + ustr(dst) + " (disk full?)");
            if (!progress) fprintf(stderr, "%s\n", f.first.c_str());
        }
        if (progress) printf("progress %llu %llu\n", (unsigned long long)done, (unsigned long long)wanted_bytes), fflush(stdout);
    } catch (const zarchive::Error& e) {
        fail(e.damaged ? 8 : 7, e.msg);
    }
    return 0;
}

std::vector<std::string> get_args(int argc, char** argv) {
    std::vector<std::string> a;
#ifdef _WIN32
    (void)argc, (void)argv;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; i++) {
        int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? len - 1 : 0, '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
        a.push_back(s);
    }
    LocalFree(w);
#else
    for (int i = 0; i < argc; i++) a.push_back(argv[i]);
#endif
    return a;
}

int usage() {
    fprintf(stderr,
            "usage: wwhd-extract [--disc-key FILE] [--common-key FILE] [--keys-stdin] [--progress] [--only GLOB]...\n"
            "                    info IMAGE | list IMAGE | extract IMAGE OUTDIR\n"
            "       wwhd-extract [--title ID] [--progress] [--only GLOB]... info ARCHIVE.wua | list ARCHIVE.wua |\n"
            "                    extract ARCHIVE.wua OUTDIR\n");
    return 2;
}

int run(const std::vector<std::string>& args) {
    std::string disc_key_file, common_key_file, cmd, image, outdir, title;
    bool keys_stdin = false, progress = false;
    std::vector<std::string> pos;
    for (size_t i = 1; i < args.size(); i++) {
        const std::string& a = args[i];
        if (a == "--disc-key" && i + 1 < args.size()) disc_key_file = args[++i];
        else if (a == "--common-key" && i + 1 < args.size()) common_key_file = args[++i];
        else if (a == "--title" && i + 1 < args.size()) title = args[++i];
        else if (a == "--only" && i + 1 < args.size()) g_only.push_back(args[++i]);
        else if (a == "--keys-stdin") keys_stdin = true;
        else if (a == "--progress") progress = true;
        else if (a == "-h" || a == "--help") return usage();
        else if (a.size() > 1 && a[0] == '-') return usage();
        else pos.push_back(a);
    }
    if (pos.size() < 2) return usage();
    cmd = pos[0];
    image = pos[1];
    if (cmd == "extract") {
        if (pos.size() != 3) return usage();
        outdir = pos[2];
    } else if ((cmd != "info" && cmd != "list") || pos.size() != 2) {
        return usage();
    }
    fs::path img = upath(image);
    // a Cemu archive (recognized by its footer; the extension does not matter): no keys
    if (zarchive::Reader::detect(img)) return run_archive(cmd, img, outdir, title, progress);
    {
        std::string ext = lower(ustr(img.extension()));
        if (ext == ".wua") {
            std::error_code ec;
            if (!fs::is_regular_file(img, ec)) fail(7, "cannot open the archive " + ustr(img));
            return run_archive(cmd, img, outdir, title, progress);  // reports what is wrong with it
        }
    }

    Key disc_key, common_key;
    if (keys_stdin) {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto sp = line.find(' ');
            if (sp == std::string::npos) continue;
            std::string which = line.substr(0, sp), val = line.substr(sp + 1);
            if (which == "disc" && !parse_key(val, disc_key)) fail(3, "the disc key is malformed: expected 32 hex digits");
            if (which == "common" && !parse_key(val, common_key))
                fail(5, "the Wii U common key is malformed: expected 32 hex digits");
        }
    }
    if (!disc_key.set) {
        fs::path p = disc_key_file.empty() ? fs::path(img).replace_extension(".key") : upath(disc_key_file);
        if (disc_key_file.empty() && !fs::exists(p))
            fail(3, "disc key not found: expected " + ustr(p) + " (16 raw bytes or 32 hex digits)");
        load_key_file(p, disc_key, 3, "disc key");
    }
    if (!common_key.set) {
        if (!common_key_file.empty()) {
            load_key_file(upath(common_key_file), common_key, 5, "Wii U common key");
        } else if (const char* env = getenv("WIIU_COMMON_KEY"); env && *env) {
            if (!parse_key(env, common_key)) fail(5, "WIIU_COMMON_KEY is malformed: expected 32 hex digits");
        } else {
            std::error_code ec;
            for (fs::path d : {fs::absolute(img, ec).parent_path(), fs::current_path(ec)}) {
                fs::path p = d / "common.key";
                if (fs::is_regular_file(p, ec)) {
                    load_key_file(p, common_key, 5, "Wii U common key");
                    break;
                }
            }
            if (!common_key.set)
                fail(5, "Wii U common key not found: use --common-key FILE, WIIU_COMMON_KEY, or common.key next to "
                        "the image");
        }
    }

    Disc disc = open_disc(img, disc_key, common_key);
    memset(&disc_key, 0, sizeof disc_key);
    memset(&common_key, 0, sizeof common_key);
    FST& gm = *disc.gm;
    uint64_t total = 0, files = 0;
    for (auto& e : gm.entries)
        if (selected(e)) total += e.size, files++;

    if (cmd == "info") {
        printf("title_id %s\nfiles %llu\nbytes %llu\n", disc.title_id.c_str(), (unsigned long long)files,
               (unsigned long long)total);
        return 0;
    }
    if (cmd == "list") {
        fprintf(stderr, "title id %s\n", disc.title_id.c_str());
        for (auto& e : gm.entries)
            if (!e.is_dir) printf("%10u  %s\n", e.size, e.path.c_str());
        return 0;
    }

    fs::path out = upath(outdir);
    uint64_t done = 0, last_report = 0;
    if (!g_only.empty()) {
        total = 0;
        for (auto& e : gm.entries)
            if (selected(e) && wanted(e.path)) total += e.size;
    }
    if (progress) printf("progress 0 %llu\n", (unsigned long long)total), fflush(stdout);
    for (auto& e : gm.entries) {
        if (!selected(e) || !wanted(e.path)) continue;
        if (e.path.find("..") != std::string::npos || e.path.empty() || e.path[0] == '/' || e.path[0] == '\\')
            fail(7, "unsafe path in the file table: " + e.path);
        fs::path dst = out / upath(e.path);
        std::error_code ec;
        fs::create_directories(dst.parent_path(), ec);
        if (ec) fail(9, "cannot create " + ustr(dst.parent_path()) + ": " + ec.message());
        std::ofstream o(dst, std::ios::binary | std::ios::trunc);
        if (!o) fail(9, "cannot write " + ustr(dst));
        gm.read_file(e, [&](const uint8_t* p, uint64_t n) {
            o.write((const char*)p, (std::streamsize)n);
            if (!o) fail(9, "cannot write " + ustr(dst) + " (disk full?)");
            done += n;
            if (progress && done - last_report >= (32u << 20)) {
                last_report = done;
                printf("progress %llu %llu\n", (unsigned long long)done, (unsigned long long)total);
                fflush(stdout);
            }
        });
        o.close();
        if (!o) fail(9, "cannot write " + ustr(dst) + " (disk full?)");
        if (!progress) fprintf(stderr, "%s\n", e.path.c_str());
    }
    if (progress) printf("progress %llu %llu\n", (unsigned long long)done, (unsigned long long)total), fflush(stdout);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // info/list/--progress output is read by the setup: plain \n line endings on every platform
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try {
        return run(get_args(argc, argv));
    } catch (const Fail& f) {
        fprintf(stderr, "error: %s\n", f.msg.c_str());
        return f.code;
    } catch (const std::exception& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 7;
    }
}
