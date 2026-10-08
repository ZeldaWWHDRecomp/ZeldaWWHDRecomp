// End-to-end test of wwhd-extract on synthetic Cemu Wii U archives (.wua, ZArchive format) written
// here by a minimal ZArchive writer (zstd-compressed and stored 64 KiB blocks, offset records, name
// table, breadth-first file tree, SHA-256 footer, as the reference writer lays them out), with
// made-up title folders and made-up file contents (no game data): listing, title selection,
// extraction byte for byte, and the error codes for a missing title and damaged archives.
//
// usage: wua_test WWHD_EXTRACT_EXE WORKDIR      (run by ctest as "extract_wua")
#include "crypto.h"

#include <zstd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <queue>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool ok, const std::string& what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}

static std::vector<uint8_t> pattern(size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    uint32_t x = seed * 2654435761u + 1;
    for (auto& c : v) {
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        c = (uint8_t)x;
    }
    return v;
}

// half random, half zeros, in 40000-byte runs: some blocks compress, some are stored as they are
static std::vector<uint8_t> mixed(size_t n, uint32_t seed) {
    std::vector<uint8_t> v = pattern(n, seed);
    for (size_t i = 0; i < n; i++)
        if ((i / 40000) % 2) v[i] = 0;
    return v;
}

static void write_file(const fs::path& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary);
    f.write((const char*)d.data(), (std::streamsize)d.size());
}
static std::vector<uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
static std::string read_text(const fs::path& p) {
    auto d = read_file(p);
    return std::string(d.begin(), d.end());
}
static std::string q(const fs::path& p) { return "\"" + p.string() + "\""; }

static int run(const std::string& cmd) {
#ifdef _WIN32
    return system(("\"" + cmd + "\"").c_str());  // cmd.exe /c strips one level of outer quotes
#else
    int r = system(cmd.c_str());
    return WIFEXITED(r) ? WEXITSTATUS(r) : 99;
#endif
}

static void put_be(std::vector<uint8_t>& b, size_t off, uint64_t v, int n) {
    for (int i = 0; i < n; i++) b[off + i] = (uint8_t)(v >> (8 * (n - 1 - i)));
}
static uint64_t get_be(const std::vector<uint8_t>& b, size_t off, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | b[off + i];
    return v;
}

// ---- a minimal ZArchive writer (test only)

struct ZWriter {
    static constexpr size_t BLOCK = 64 * 1024;
    struct Node {
        std::string name;
        bool file = false;
        uint64_t offset = 0, size = 0;
        std::vector<int> kids;
        uint32_t first = 0;
    };
    std::vector<Node> nodes{Node{}};  // 0: root
    std::vector<uint8_t> stream;      // uncompressed data, files in the order they were added
    std::vector<std::pair<uint64_t, uint32_t>> blocks;  // after finish(): compressed offset and length

    int child(int dir, const std::string& name, bool file) {
        for (int k : nodes[dir].kids)
            if (nodes[k].name == name) return k;
        Node n;
        n.name = name, n.file = file;
        nodes.push_back(n);
        nodes[dir].kids.push_back((int)nodes.size() - 1);
        return (int)nodes.size() - 1;
    }
    void add(const std::string& path, const std::vector<uint8_t>& data) {
        int d = 0;
        size_t s = 0, p;
        while ((p = path.find('/', s)) != std::string::npos) d = child(d, path.substr(s, p - s), false), s = p + 1;
        int f = child(d, path.substr(s), true);
        nodes[f].offset = stream.size(), nodes[f].size = data.size();
        stream.insert(stream.end(), data.begin(), data.end());
    }
    void mkdir(const std::string& path) {
        int d = 0;
        size_t s = 0, p;
        std::string x = path + "/";
        while ((p = x.find('/', s)) != std::string::npos) d = child(d, x.substr(s, p - s), false), s = p + 1;
    }

    std::vector<uint8_t> finish() {
        std::vector<uint8_t> out, data = stream;
        data.resize((data.size() + BLOCK - 1) / BLOCK * BLOCK);
        // blocks: zstd, or stored when that is not smaller
        std::vector<uint8_t> tmp(ZSTD_compressBound(BLOCK));
        blocks.clear();
        for (size_t b = 0; b < data.size() / BLOCK; b++) {
            size_t n = ZSTD_compress(tmp.data(), tmp.size(), &data[b * BLOCK], BLOCK, 6);
            blocks.push_back({out.size(), n >= BLOCK ? (uint32_t)BLOCK : (uint32_t)n});
            if (n >= BLOCK) out.insert(out.end(), &data[b * BLOCK], &data[b * BLOCK] + BLOCK);
            else out.insert(out.end(), tmp.data(), tmp.data() + n);
        }
        uint64_t sec[6][2] = {};
        sec[0][0] = 0, sec[0][1] = out.size();
        while (out.size() % 8) out.push_back(0);
        // offset records: per 16 blocks the offset of the first, then 16 x (length - 1)
        sec[1][0] = out.size();
        for (size_t r = 0; r < (blocks.size() + 15) / 16; r++) {
            std::vector<uint8_t> rec(40, 0);
            put_be(rec, 0, blocks[r * 16].first, 8);
            for (size_t k = 0; k < 16 && r * 16 + k < blocks.size(); k++) put_be(rec, 8 + 2 * k, blocks[r * 16 + k].second - 1, 2);
            out.insert(out.end(), rec.begin(), rec.end());
        }
        sec[1][1] = out.size() - sec[1][0];
        // names (one per node, in node order) and the breadth-first tree
        sec[2][0] = out.size();
        std::vector<uint32_t> name_off(nodes.size(), 0x7FFFFFFF);
        for (size_t i = 1; i < nodes.size(); i++) {
            name_off[i] = (uint32_t)(out.size() - sec[2][0]);
            const std::string& n = nodes[i].name;
            if (n.size() >= 0x80) out.push_back((uint8_t)((n.size() & 0x7F) | 0x80)), out.push_back((uint8_t)(n.size() >> 7));
            else out.push_back((uint8_t)n.size());
            out.insert(out.end(), n.begin(), n.end());
        }
        sec[2][1] = out.size() - sec[2][0];
        auto lt = [&](int a, int b) {
            std::string x = nodes[a].name, y = nodes[b].name;
            for (auto& c : x) c = (char)tolower((unsigned char)c);
            for (auto& c : y) c = (char)tolower((unsigned char)c);
            return x < y;
        };
        std::vector<int> order;
        std::queue<int> qu;
        qu.push(0);
        uint32_t next = 1;
        while (!qu.empty()) {
            int i = qu.front();
            qu.pop();
            order.push_back(i);
            std::sort(nodes[i].kids.begin(), nodes[i].kids.end(), lt);
            nodes[i].first = next;
            next += (uint32_t)nodes[i].kids.size();
            for (int k : nodes[i].kids) qu.push(k);
        }
        sec[3][0] = out.size();
        for (int i : order) {
            const Node& n = nodes[i];
            std::vector<uint8_t> e(16, 0);
            put_be(e, 0, (n.file ? 0x80000000u : 0) | name_off[i], 4);
            if (n.file) {
                put_be(e, 4, (uint32_t)n.offset, 4);
                put_be(e, 8, (uint32_t)n.size, 4);
                put_be(e, 12, (uint32_t)((n.size >> 32) << 16 | (n.offset >> 32)), 4);
            } else {
                put_be(e, 4, n.first, 4);
                put_be(e, 8, n.kids.size(), 4);
            }
            out.insert(out.end(), e.begin(), e.end());
        }
        sec[3][1] = out.size() - sec[3][0];
        sec[4][0] = sec[5][0] = out.size();
        std::vector<uint8_t> ft(144, 0);
        for (int i = 0; i < 6; i++) put_be(ft, 16 * i, sec[i][0], 8), put_be(ft, 16 * i + 8, sec[i][1], 8);
        put_be(ft, 128, out.size() + 144, 8);
        put_be(ft, 136, 0x61bf3a01, 4);
        put_be(ft, 140, 0x169f52d6, 4);
        out.insert(out.end(), ft.begin(), ft.end());
        rehash(out);
        return out;
    }

    // the footer's SHA-256 over the whole archive (its own field counted as zeros)
    static void rehash(std::vector<uint8_t>& a) {
        size_t h = a.size() - 144 + 96;
        memset(&a[h], 0, 32);
        wudcrypto::Sha256 s;
        s.update(a.data(), a.size());
        uint8_t d[32];
        s.final(d);
        memcpy(&a[h], d, 32);
    }
};

struct FileSpec {
    std::string path;
    std::vector<uint8_t> data;
};

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: wua_test WWHD_EXTRACT_EXE WORKDIR\n");
        return 2;
    }
    fs::path exe = fs::absolute(argv[1]), work = fs::absolute(argv[2]);
    fs::remove_all(work);
    fs::create_directories(work);
    std::string x = q(exe);

    // base game (USA), its update, a DLC-style title and a stray file at the top
    std::vector<FileSpec> base = {{"code/cking.rpx", pattern(100000, 1)},
                                  {"code/app.xml", pattern(37, 2)},
                                  {"meta/meta.xml", pattern(300, 3)},
                                  {"content/Audiores/big.bin", mixed(2 * 1024 * 1024 + 1234, 4)},  // > 16 blocks
                                  {"content/empty.bin", {}},
                                  {"content/a/b/c/deep.bin", pattern(70000, 5)}};
    std::vector<FileSpec> update = {{"code/cking.rpx", pattern(100004, 11)},
                                    {"content/patched.bin", pattern(5000, 12)},
                                    {"meta/meta.xml", pattern(10, 13)}};
    ZWriter w;
    for (auto& f : update) w.add("0005000e10143500_v16/" + f.path, f.data);  // data order != tree order
    for (auto& f : base) w.add("0005000010143500_v0/" + f.path, f.data);
    w.add("0005000c10143500_v5/content/0010/dlc.bin", pattern(999, 21));
    w.mkdir("0005000010143500_v0/content/emptydir");
    w.add("readme.txt", pattern(12, 22));
    std::vector<uint8_t> arc = w.finish();
    fs::path wua = work / "game.wua";
    write_file(wua, arc);
    {
        bool some_stored = false, some_packed = false;
        for (auto& b : w.blocks) (b.second == ZWriter::BLOCK ? some_stored : some_packed) = true;
        expect(some_stored && some_packed && w.blocks.size() > 16, "synthetic archive has stored and compressed blocks, > 16");
    }

    auto check_tree = [&](const fs::path& out, const std::vector<FileSpec>& files, const std::string& what) {
        bool ok = true;
        size_t n = 0;
        for (auto& f : files) ok &= fs::exists(out / f.path) && read_file(out / f.path) == f.data;
        for (auto& e : fs::recursive_directory_iterator(out))
            if (e.is_regular_file()) n++;
        expect(ok && n == files.size(), what);
    };

    // info: all titles, and the selected one
    expect(run(x + " --title 0005000010143500 info " + q(wua) + " > " + q(work / "info.txt")) == 0, "info --title (no keys)");
    {
        std::string s = read_text(work / "info.txt");
        uint64_t bytes = 0;
        for (auto& f : base) bytes += f.data.size();
        expect(s.find("format wua\n") == 0, "info: format line");
        expect(s.find("title 0005000010143500 0 0005000010143500_v0 6 " + std::to_string(bytes) + "\n") != std::string::npos,
               "info: base title line (files, bytes)");
        expect(s.find("title 0005000e10143500 16 0005000e10143500_v16 3 ") != std::string::npos, "info: update title line");
        expect(s.find("title 0005000c10143500 5 0005000c10143500_v5 1 999\n") != std::string::npos, "info: DLC title line");
        expect(s.find("readme") == std::string::npos, "info: files at the top are not titles");
        expect(s.find("selected 0005000010143500_v0\ntitle_id 0005000010143500\nversion 0\nfiles 6\nbytes " +
                      std::to_string(bytes) + "\n") != std::string::npos,
               "info: selected title");
    }
    expect(run(x + " info " + q(wua) + " > " + q(work / "info_all.txt")) == 0, "info without --title lists the titles");
    expect(read_text(work / "info_all.txt").find("selected") == std::string::npos, "  ... and selects none of several");
    expect(run(x + " list " + q(wua) + " > " + q(work / "list.txt")) == 0, "list");
    {
        std::string s = read_text(work / "list.txt");
        expect(s.find("0005000010143500_v0/content/a/b/c/deep.bin") != std::string::npos &&
                   s.find("0005000e10143500_v16/content/patched.bin") != std::string::npos,
               "list shows the files of every title");
    }

    // extract the base game: only its folder, byte for byte, with progress
    expect(run(x + " --title 0005000010143500 --progress extract " + q(wua) + " " + q(work / "out") + " > " +
               q(work / "progress.txt")) == 0,
           "extract the base title");
    check_tree(work / "out", base, "extracted files match (and nothing from the other titles)");
    {
        std::string s = read_text(work / "progress.txt");
        expect(s.find("phase verify\nprogress 0 " + std::to_string(arc.size()) + "\n") == 0 &&
                   s.find("phase extract\nprogress 0 ") != std::string::npos,
               "progress: verify, then extract");
    }
    // by folder name, upper case: the update
    expect(run(x + " --title 0005000E10143500_V16 extract " + q(wua) + " " + q(work / "out_upd") + " 2> " +
               q(work / "log.txt")) == 0,
           "extract a title by its folder name (any case)");
    check_tree(work / "out_upd", update, "update files match");

    // the extension does not matter (recognized by the footer)
    fs::copy_file(wua, work / "game.bin");
    expect(run(x + " --title 0005000010143500 info " + q(work / "game.bin") + " > " + q(work / "info3.txt")) == 0,
           "archive recognized without the .wua extension");

    // a single title needs no --title
    {
        ZWriter one;
        for (auto& f : base) one.add("0005000010143500_v0/" + f.path, f.data);
        write_file(work / "one.wua", one.finish());
        expect(run(x + " extract " + q(work / "one.wua") + " " + q(work / "out_one") + " 2> " + q(work / "log1.txt")) == 0,
               "single-title archive without --title");
        check_tree(work / "out_one", base, "single-title files match");
    }

    // wrong title: the European game only
    {
        ZWriter eu;
        for (auto& f : base) eu.add("0005000010143600_v0/" + f.path, f.data);
        write_file(work / "eu.wua", eu.finish());
        expect(run(x + " --title 0005000010143500 info " + q(work / "eu.wua") + " > " + q(work / "info_eu.txt") + " 2> " +
                   q(work / "e_eu.txt")) == 10,
               "title not in the archive -> exit 10");
        expect(read_text(work / "info_eu.txt").find("title 0005000010143600 0 ") != std::string::npos,
               "  ... info still lists what is there");
        expect(read_text(work / "e_eu.txt").find("0005000010143600_v0") != std::string::npos, "  ... message names it");
        expect(run(x + " --title 0005000010143500 extract " + q(work / "eu.wua") + " " + q(work / "out_eu") + " 2> " +
                   q(work / "e_eu2.txt")) == 10 &&
                   !fs::exists(work / "out_eu"),
               "  ... extract refuses, writes nothing");
        expect(run(x + " extract " + q(wua) + " " + q(work / "out_multi") + " 2> " + q(work / "e_multi.txt")) == 10,
               "several titles and no --title -> exit 10");
        ZWriter none;
        none.add("code/cking.rpx", pattern(10, 1));
        write_file(work / "none.wua", none.finish());
        expect(run(x + " info " + q(work / "none.wua") + " > " + q(work / "o.txt") + " 2> " + q(work / "e_none.txt")) == 10,
               "no title folders -> exit 10");
    }

    // --only on a European archive (a language source): just the packs and meta.xml, without case
    {
        ZWriter eu;
        std::vector<FileSpec> want = {{"content/Common/Pack/permanent_2d_EuGerman.pack", pattern(400, 31)},
                                      {"content/Common/Pack/permanent_2d_EuFrench.pack", pattern(500, 32)},
                                      {"meta/meta.xml", pattern(300, 3)}};
        for (auto& f : want) eu.add("0005000010143600_v0/" + f.path, f.data);
        eu.add("0005000010143600_v0/code/cking.rpx", pattern(1000, 33));
        eu.add("0005000010143600_v0/content/Common/Pack/permanent_3d.pack", pattern(600, 34));
        write_file(work / "eu_lang.wua", eu.finish());
        expect(run(x + " --title 0005000010143600 --only CONTENT/Common/Pack/permanent_2d_*.pack --only meta/meta.xml "
                   "--progress extract " + q(work / "eu_lang.wua") + " " + q(work / "out_lang") + " > " +
                   q(work / "progress_lang.txt")) == 0,
               "extract --only (archive)");
        check_tree(work / "out_lang", want, "  ... only the matching files");
        expect(read_text(work / "progress_lang.txt").find("phase extract\nprogress 0 1200\n") != std::string::npos,
               "  ... progress counts only them");
    }

    // damaged archives
    {
        std::vector<uint8_t> t(arc.begin(), arc.end() - 1);
        write_file(work / "truncated.wua", t);
        expect(run(x + " info " + q(work / "truncated.wua") + " 2> " + q(work / "e1.txt")) == 7, "truncated archive -> exit 7");
        write_file(work / "garbage.wua", pattern(5000, 9));
        expect(run(x + " info " + q(work / "garbage.wua") + " 2> " + q(work / "e2.txt")) == 7, "not an archive -> exit 7");
        expect(read_text(work / "e2.txt").find("not a Cemu Wii U archive") != std::string::npos, "  ... message");
        expect(run(x + " info " + q(work / "missing.wua") + " 2> " + q(work / "e3.txt")) == 7, "missing file -> exit 7");

        // one flipped byte in a stored block: the structure reads fine, the SHA-256 check stops the extraction
        size_t stored = 0;
        for (auto& b : w.blocks)
            if (b.second == ZWriter::BLOCK) stored = b.first + 1000;
        std::vector<uint8_t> d = arc;
        d[stored] ^= 0x40;
        write_file(work / "flipped.wua", d);
        expect(run(x + " --title 0005000010143500 info " + q(work / "flipped.wua") + " > " + q(work / "o2.txt")) == 0,
               "flipped data byte: info still works");
        expect(run(x + " --title 0005000010143500 extract " + q(work / "flipped.wua") + " " + q(work / "out_flip") + " 2> " +
                   q(work / "e4.txt")) == 8 &&
                   !fs::exists(work / "out_flip"),
               "flipped data byte: extract -> exit 8 before writing anything");
        expect(read_text(work / "e4.txt").find("SHA-256") != std::string::npos, "  ... message");

        // a broken zstd frame with a matching hash (the hash alone would not catch a damaged writer)
        size_t packed = 0;
        for (auto& b : w.blocks)
            if (b.second < ZWriter::BLOCK) packed = b.first;
        d = arc;
        d[packed] ^= 0xFF;  // the frame magic
        ZWriter::rehash(d);
        write_file(work / "badframe.wua", d);
        expect(run(x + " --title 0005000010143500 extract " + q(work / "badframe.wua") + " " + q(work / "out_bf") + " 2> " +
                   q(work / "e5.txt")) == 8,
               "broken zstd block -> exit 8");
        expect(read_text(work / "e5.txt").find("cannot be decompressed") != std::string::npos, "  ... message");

        // a folder pointing past the end of the tree
        d = arc;
        size_t tree = (size_t)get_be(d, d.size() - 144 + 48, 8);
        put_be(d, tree + 8, 100000, 4);  // the root's entry count
        ZWriter::rehash(d);
        write_file(work / "badtree.wua", d);
        expect(run(x + " info " + q(work / "badtree.wua") + " 2> " + q(work / "e6.txt")) == 7, "corrupt file tree -> exit 7");

        // a ".." name inside the title folder
        ZWriter evil;
        evil.add("0005000010143500_v0/code/cking.rpx", pattern(10, 1));
        evil.add("0005000010143500_v0/../escape.bin", pattern(10, 2));
        write_file(work / "evil.wua", evil.finish());
        expect(run(x + " --title 0005000010143500 extract " + q(work / "evil.wua") + " " + q(work / "out_evil") + " 2> " +
                   q(work / "e7.txt")) == 7 &&
                   !fs::exists(work / "escape.bin"),
               "unsafe name -> exit 7, nothing written outside");
    }

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    if (!failures && !getenv("WWHD_KEEP_TEST")) fs::remove_all(work);
    return failures ? 1 : 0;
}
