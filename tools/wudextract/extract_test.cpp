// End-to-end test of wwhd-extract on a synthetic Wii U disc image built here with made-up keys
// and made-up file contents (no game data, no real keys): partition table, system partition with
// a ticket, game partition with a raw (CBC) cluster and a hashed (H0) cluster, as .wud and .wux.
// Checks extraction results byte for byte and the error codes for wrong/malformed keys and damage.
//
// usage: extract_test WWHD_EXTRACT_EXE WORKDIR      (run by ctest as "extract_synthetic")
#include "crypto.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;
using namespace wudcrypto;

static const uint64_t SECTOR = 0x8000;
static int failures = 0;

static void expect(bool ok, const std::string& what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}

static void put_be32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off] = (uint8_t)(v >> 24), b[off + 1] = (uint8_t)(v >> 16), b[off + 2] = (uint8_t)(v >> 8), b[off + 3] = (uint8_t)v;
}
static void put_be16(std::vector<uint8_t>& b, size_t off, uint16_t v) { b[off] = (uint8_t)(v >> 8), b[off + 1] = (uint8_t)v; }
static void put_le32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; i++) b[off + i] = (uint8_t)(v >> (8 * i));
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

struct FileSpec {
    std::string path;  // "dir/sub/name"
    std::vector<uint8_t> data;
    int cluster;
    bool skip = false;  // FST flag 0x80 (not extracted)
};

// Builds an FST (plain) and places file data into cluster buffers.
// offset_factor 0x20; cluster 0 is raw, cluster 1 (if any) hashed.
struct PartitionBuilder {
    std::vector<FileSpec> files;
    std::vector<int> hash_modes;  // per cluster

    struct Node {
        std::map<std::string, Node> dirs;
        std::vector<const FileSpec*> files;
    };

    // returns FST bytes; cluster_data[i] = plaintext cluster content (raw clusters: as laid out;
    // hashed clusters: file data stream, blocked later)
    std::vector<uint8_t> build(std::vector<std::vector<uint8_t>>& cluster_data, const std::vector<uint32_t>& cluster_sector) {
        const uint32_t factor = 0x20;
        cluster_data.assign(hash_modes.size(), {});
        Node root;
        for (auto& f : files) {
            Node* n = &root;
            size_t s = 0, p;
            while ((p = f.path.find('/', s)) != std::string::npos) {
                n = &n->dirs[f.path.substr(s, p - s)];
                s = p + 1;
            }
            n->files.push_back(&f);
        }
        struct E {
            uint32_t tno, off, size;
            uint16_t cl;
        };
        std::vector<E> ents;
        std::string names(1, '\0');
        auto add_name = [&](const std::string& n) {
            uint32_t o = (uint32_t)names.size();
            names += n;
            names += '\0';
            return o;
        };
        ents.push_back({0x01000000, 0, 0, 0});  // root dir, size patched later
        auto file_offset = [&](const FileSpec& f) {
            auto& cd = cluster_data[f.cluster];
            size_t pos = (cd.size() + factor - 1) / factor * factor;
            cd.resize(pos);
            cd.insert(cd.end(), f.data.begin(), f.data.end());
            return (uint32_t)(pos / factor);
        };
        std::function<void(const Node&, uint32_t)> walk = [&](const Node& n, uint32_t parent) {
            for (auto* f : n.files) {
                uint32_t nm = add_name(f->path.substr(f->path.rfind('/') == std::string::npos ? 0 : f->path.rfind('/') + 1));
                uint32_t flags = f->skip ? 0x80 : 0;
                ents.push_back({(flags << 24) | nm, file_offset(*f), (uint32_t)f->data.size(), (uint16_t)f->cluster});
            }
            for (auto& [name, sub] : n.dirs) {
                size_t idx = ents.size();
                ents.push_back({0x01000000 | add_name(name), parent, 0, 0});
                walk(sub, (uint32_t)idx);
                ents[idx].size = (uint32_t)ents.size();  // end index
            }
        };
        walk(root, 0);
        ents[0].size = (uint32_t)ents.size();
        size_t ncl = hash_modes.size();
        size_t ft = 0x20 + ncl * 0x20;
        std::vector<uint8_t> fst(ft + ents.size() * 0x10 + names.size());
        put_be32(fst, 0, 0x46535400);
        put_be32(fst, 4, factor);
        put_be32(fst, 8, (uint32_t)ncl);
        for (size_t i = 0; i < ncl; i++) {
            put_be32(fst, 0x20 + i * 0x20, cluster_sector[i]);
            put_be32(fst, 0x20 + i * 0x20 + 4, 0);
            fst[0x20 + i * 0x20 + 0x14] = (uint8_t)hash_modes[i];
        }
        for (size_t i = 0; i < ents.size(); i++) {
            put_be32(fst, ft + i * 0x10, ents[i].tno);
            put_be32(fst, ft + i * 0x10 + 4, ents[i].off);
            put_be32(fst, ft + i * 0x10 + 8, ents[i].size);
            put_be16(fst, ft + i * 0x10 + 14, ents[i].cl);
        }
        memcpy(&fst[ft + ents.size() * 0x10], names.data(), names.size());
        return fst;
    }
};

static void encrypt(const uint8_t key[16], const uint8_t iv_in[16], uint8_t* data, size_t len) {
    uint8_t iv[16];
    memcpy(iv, iv_in, 16);
    aes128_cbc_encrypt(Aes128Enc(key), iv, data, len);
}

// Writes a partition (header, FST, clusters) at image sector `base_sector`; returns sectors used.
static uint32_t write_partition(std::vector<uint8_t>& img, uint32_t base_sector, PartitionBuilder& pb, const uint8_t key[16]) {
    // layout: sector 0 header, sector 1.. FST, then clusters
    std::vector<std::vector<uint8_t>> cdata;
    std::vector<uint32_t> csec(pb.hash_modes.size(), 0);
    auto fst = pb.build(cdata, csec);  // first pass for sizes
    uint32_t fst_sectors = (uint32_t)((fst.size() + SECTOR - 1) / SECTOR);
    uint32_t next = 1 + fst_sectors;
    std::vector<std::vector<uint8_t>> cblob(cdata.size());
    for (size_t c = 0; c < cdata.size(); c++) {
        csec[c] = next;
        if (pb.hash_modes[c] == 2) {
            size_t nblk = (cdata[c].size() + 0xFC00 - 1) / 0xFC00;
            std::vector<uint8_t> blob(nblk * 0x10000);
            std::vector<uint8_t> hashes(0x400);
            for (size_t b = 0; b < nblk; b++) {
                std::vector<uint8_t> d(0xFC00, 0);
                size_t n = std::min<size_t>(0xFC00, cdata[c].size() - b * 0xFC00);
                memcpy(d.data(), cdata[c].data() + b * 0xFC00, n);
                std::vector<uint8_t> h(0x400, 0);
                sha1(d.data(), d.size(), &h[(b % 16) * 20]);
                uint8_t iv0[16] = {};
                std::vector<uint8_t> eh = h;
                encrypt(key, iv0, eh.data(), eh.size());
                uint8_t ivd[16];
                memcpy(ivd, &h[(b % 16) * 20], 16);
                encrypt(key, ivd, d.data(), d.size());
                memcpy(&blob[b * 0x10000], eh.data(), 0x400);
                memcpy(&blob[b * 0x10000 + 0x400], d.data(), 0xFC00);
            }
            cblob[c] = blob;
        } else {
            std::vector<uint8_t> blob = cdata[c];
            blob.resize((blob.size() + SECTOR - 1) / SECTOR * SECTOR + SECTOR);
            uint8_t iv[16] = {(uint8_t)(c >> 8), (uint8_t)c};
            encrypt(key, iv, blob.data(), blob.size());
            cblob[c] = blob;
        }
        next += (uint32_t)((cblob[c].size() + SECTOR - 1) / SECTOR);
    }
    std::vector<std::vector<uint8_t>> cdata2;
    fst = pb.build(cdata2, csec);  // second pass with cluster positions
    std::vector<uint8_t> efst = fst;
    efst.resize((fst.size() + 15) / 16 * 16);
    uint8_t iv0[16] = {};
    encrypt(key, iv0, efst.data(), efst.size());
    uint64_t base = (uint64_t)base_sector * SECTOR;
    img.resize(std::max<size_t>(img.size(), base + (uint64_t)next * SECTOR));
    std::vector<uint8_t> hdr(0x60, 0);
    put_be32(hdr, 0x14, (uint32_t)fst.size());
    put_be32(hdr, 0x18, 1);
    memcpy(&img[base], hdr.data(), hdr.size());
    memcpy(&img[base + SECTOR], efst.data(), efst.size());
    for (size_t c = 0; c < cblob.size(); c++) memcpy(&img[base + (uint64_t)csec[c] * SECTOR], cblob[c].data(), cblob[c].size());
    return next;
}

static void write_file(const fs::path& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary);
    f.write((const char*)d.data(), (std::streamsize)d.size());
}
static void write_text(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
}
static std::vector<uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
static std::string hex(const uint8_t* k) {
    std::string s;
    char b[3];
    for (int i = 0; i < 16; i++) snprintf(b, 3, "%02X", k[i]), s += b;
    return s;
}

static std::string q(const fs::path& p) { return "\"" + p.string() + "\""; }

static int run(const std::string& cmd) {
#ifdef _WIN32
    int r = system(("\"" + cmd + "\"").c_str());  // cmd.exe /c strips one level of outer quotes
    return r;
#else
    int r = system(cmd.c_str());
    return WIFEXITED(r) ? WEXITSTATUS(r) : 99;
#endif
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: extract_test WWHD_EXTRACT_EXE WORKDIR\n");
        return 2;
    }
    fs::path exe = fs::absolute(argv[1]), work = fs::absolute(argv[2]);
    fs::remove_all(work);
    fs::create_directories(work);

    // made-up keys (test only)
    uint8_t disc_key[16], common_key[16], title_key[16];
    for (int i = 0; i < 16; i++) disc_key[i] = (uint8_t)(0x11 * i + 3), common_key[i] = (uint8_t)(0xA5 ^ (i * 7)),
                                 title_key[i] = (uint8_t)(i * 13 + 1);
    const uint8_t title_id[8] = {0x00, 0x05, 0x00, 0x00, 0x10, 0x14, 0x35, 0x00};

    // game partition files
    PartitionBuilder gm;
    gm.hash_modes = {0, 2};
    gm.files = {{"code/cking.rpx", pattern(100000, 1), 0},
                {"code/app.xml", pattern(37, 2), 0},
                {"meta/meta.xml", pattern(0, 3), 0},
                {"content/Audiores/big.bin", pattern(0xFC00 * 2 + 1234, 4), 1},
                {"content/small.bin", pattern(17, 5), 1},
                {"content/Common/Pack/permanent_2d_EuGerman.pack", pattern(300, 10), 1},
                {"content/skipme.bin", pattern(64, 6), 0, true}};
    // ticket in the system partition: encrypted title key + title id
    std::vector<uint8_t> tik(0x350, 0);
    memcpy(&tik[0x1DC], title_id, 8);
    {
        uint8_t iv[16] = {};
        memcpy(iv, title_id, 8);
        uint8_t etk[16];
        memcpy(etk, title_key, 16);
        encrypt(common_key, iv, etk, 16);
        memcpy(&tik[0x1BF], etk, 16);
    }
    PartitionBuilder si;
    si.hash_modes = {0};
    si.files = {{"01/title.tik", tik, 0}, {"01/title.tmd", pattern(100, 9), 0}};

    std::vector<uint8_t> img(SECTOR * 8, 0);
    uint32_t si_sector = 8;
    uint32_t si_used = write_partition(img, si_sector, si, disc_key);
    uint32_t gm_sector = si_sector + si_used + 2;
    write_partition(img, gm_sector, gm, title_key);
    img.resize(img.size() + SECTOR * 3);  // zero tail (dedupes in the .wux)
    // disc header magic + encrypted partition table
    std::vector<uint8_t> magic(4);
    put_be32(magic, 0, 0xCC549EB9);
    memcpy(&img[SECTOR * 2], magic.data(), 4);
    std::vector<uint8_t> pt(SECTOR, 0);
    put_be32(pt, 0, 0xCCA6E67B);
    put_be32(pt, 0x1C, 2);
    memcpy(&pt[0x800], "SI", 2);
    put_be32(pt, 0x800 + 0x20, si_sector);
    memcpy(&pt[0x880], "GM0005000010143500", 18);
    put_be32(pt, 0x880 + 0x20, gm_sector);
    uint8_t iv0[16] = {};
    encrypt(disc_key, iv0, pt.data(), pt.size());
    memcpy(&img[SECTOR * 3], pt.data(), SECTOR);

    fs::path wud = work / "disc.wud";
    write_file(wud, img);
    write_file(work / "disc.key", std::vector<uint8_t>(disc_key, disc_key + 16));
    write_text(work / "common_hex.txt", hex(common_key) + "\n");
    write_file(work / "common_raw.bin", std::vector<uint8_t>(common_key, common_key + 16));

    // .wux: index + deduplicated sectors
    {
        uint32_t ss = (uint32_t)SECTOR;
        uint64_t n = img.size() / ss;
        std::vector<uint8_t> hdr(32 + 4 * n, 0);
        put_le32(hdr, 0, 0x30585557);
        put_le32(hdr, 4, 0x1099D02E);
        put_le32(hdr, 8, ss);
        put_le32(hdr, 16, (uint32_t)img.size());
        put_le32(hdr, 20, (uint32_t)((uint64_t)img.size() >> 32));
        std::vector<std::vector<uint8_t>> uniq;
        std::map<std::vector<uint8_t>, uint32_t> seen;
        for (uint64_t i = 0; i < n; i++) {
            std::vector<uint8_t> s(img.begin() + i * ss, img.begin() + (i + 1) * ss);
            auto it = seen.find(s);
            uint32_t idx;
            if (it == seen.end()) {
                idx = (uint32_t)uniq.size();
                seen[s] = idx;
                uniq.push_back(s);
            } else {
                idx = it->second;
            }
            put_le32(hdr, 32 + 4 * i, idx);
        }
        expect(uniq.size() < n, "synthetic .wux deduplicates sectors");
        std::vector<uint8_t> wux = hdr;
        wux.resize((wux.size() + ss - 1) / ss * ss);
        for (auto& s : uniq) wux.insert(wux.end(), s.begin(), s.end());
        write_file(work / "disc.wux", wux);
        write_file(work / "disc_wux.key", std::vector<uint8_t>(disc_key, disc_key + 16));
    }

    std::string x = q(exe);
    std::string ck = " --common-key " + q(work / "common_hex.txt");
    auto check_tree = [&](const fs::path& out, const std::string& what) {
        bool ok = true;
        for (auto& f : gm.files) {
            fs::path p = out / f.path;
            if (f.skip) ok &= !fs::exists(p);
            else ok &= fs::exists(p) && read_file(p) == f.data;
        }
        expect(ok, what);
    };

    expect(run(x + ck + " info " + q(wud) + " > " + q(work / "info.txt")) == 0, "info with correct keys");
    {
        auto t = read_file(work / "info.txt");
        std::string s(t.begin(), t.end());
        expect(s.find("title_id 0005000010143500") != std::string::npos, "info reports the title id");
        expect(s.find(hex(common_key)) == std::string::npos && s.find(hex(disc_key)) == std::string::npos,
               "info does not print keys");
    }
    expect(run(x + ck + " --progress extract " + q(wud) + " " + q(work / "out_wud") + " > " + q(work / "progress.txt")) == 0,
           "extract .wud");
    check_tree(work / "out_wud", "extracted .wud files match");
    {
        auto t = read_file(work / "progress.txt");
        std::string s(t.begin(), t.end());
        expect(s.find("progress 0 ") == 0 && s.find("\nprogress ") != std::string::npos, "progress lines");
    }
    expect(run(x + " --disc-key " + q(work / "disc_wux.key") + " --common-key " + q(work / "common_raw.bin") +
               " extract " + q(work / "disc.wux") + " " + q(work / "out_wux") + " 2> " + q(work / "log.txt")) == 0,
           "extract .wux (raw-bytes key files)");
    check_tree(work / "out_wux", "extracted .wux files match");

    // --only: just the language files of a disc (the setup's language source), patterns without case
    expect(run(x + ck + " --only content/common/pack/permanent_2d_*.pack --only META/meta.xml --progress extract " + q(wud) +
               " " + q(work / "out_only") + " > " + q(work / "progress_only.txt")) == 0,
           "extract --only");
    {
        bool ok = true;
        for (auto& f : gm.files) {
            fs::path p = work / "out_only" / f.path;
            bool want = f.path == "meta/meta.xml" || f.path.rfind("content/Common/Pack/", 0) == 0;
            ok &= want ? fs::exists(p) && read_file(p) == f.data : !fs::exists(p);
        }
        expect(ok, "--only extracts only the matching files");
        auto t = read_file(work / "progress_only.txt");
        std::string s(t.begin(), t.end());
        expect(s.find("progress 0 300\n") == 0, "--only progress counts only the matching files");
    }

    // keys over stdin
    write_text(work / "keys.txt", "disc " + hex(disc_key) + "\r\ncommon " + hex(common_key) + "\n");
    expect(run(x + " --keys-stdin --disc-key " + q(work / "nonexistent") + " info " + q(work / "disc.wux") + " < " +
               q(work / "keys.txt") + " > " + q(work / "info2.txt")) == 0,
           "keys from stdin");

    // error cases
    uint8_t bad[16];
    memcpy(bad, disc_key, 16);
    bad[5] ^= 1;
    write_file(work / "bad_disc.key", std::vector<uint8_t>(bad, bad + 16));
    expect(run(x + ck + " --disc-key " + q(work / "bad_disc.key") + " info " + q(wud) + " 2> " + q(work / "e1.txt")) == 4,
           "wrong disc key -> exit 4");
    memcpy(bad, common_key, 16);
    bad[0] ^= 0x80;
    write_text(work / "bad_common.txt", hex(bad));
    expect(run(x + " --common-key " + q(work / "bad_common.txt") + " info " + q(wud) + " 2> " + q(work / "e2.txt")) == 6,
           "wrong common key -> exit 6");
    write_text(work / "malformed.txt", "1234 not a key");
    expect(run(x + " --common-key " + q(work / "malformed.txt") + " info " + q(wud) + " 2> " + q(work / "e3.txt")) == 5,
           "malformed common key -> exit 5");
    expect(run(x + ck + " --disc-key " + q(work / "malformed.txt") + " info " + q(wud) + " 2> " + q(work / "e4.txt")) == 3,
           "malformed disc key -> exit 3");
    write_file(work / "notadisc.wud", pattern(SECTOR * 5, 77));
    expect(run(x + ck + " --disc-key " + q(work / "disc.key") + " info " + q(work / "notadisc.wud") + " 2> " +
               q(work / "e5.txt")) == 7,
           "not a disc image -> exit 7");
    {
        auto e = read_file(work / "e2.txt");
        std::string s(e.begin(), e.end());
        expect(s.find("common key is wrong") != std::string::npos, "wrong common key message");
        expect(s.find(hex(bad)) == std::string::npos && s.find(hex(common_key)) == std::string::npos,
               "error messages do not print keys");
    }
    // damage one byte in the hashed cluster's data area -> hash mismatch
    {
        std::vector<uint8_t> dmg = img;
        uint64_t off = 0;
        // find the hashed cluster: last nonzero region before the zero tail; flip a byte well inside it
        for (uint64_t i = dmg.size() - SECTOR * 3; i-- > 0;)
            if (dmg[i]) {
                off = i - 0x8000;
                break;
            }
        dmg[off] ^= 0xFF;
        write_file(work / "damaged.wud", dmg);
        write_file(work / "damaged.key", std::vector<uint8_t>(disc_key, disc_key + 16));
        expect(run(x + ck + " extract " + q(work / "damaged.wud") + " " + q(work / "out_dmg") + " 2> " +
                   q(work / "e6.txt")) == 8,
               "damaged hashed block -> exit 8");
    }
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    if (!failures && !getenv("WWHD_KEEP_TEST")) fs::remove_all(work);
    return failures ? 1 : 0;
}
