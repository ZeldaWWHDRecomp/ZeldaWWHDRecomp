// SPDX-License-Identifier: MPL-2.0
// Link's colour masks in the port (link_masks.h): the game's 3D pack, the worker thread, the cache in the user
// folder, get() / find(), and a test aid that shows the tint on the player's own Link.
//
// Cache: <user folder>/link_masks/<key>.bin, key = the pack's size and modification time and the algorithm
// version (a changed pack or a new algorithm makes a new file; the masks inside also carry their textures'
// hashes, and find() matches those, so a stale file can never be applied to other texture bytes). Made on
// the player's machine only; nothing of it is in the repository or uploaded.
//
// Test aid WWHD_LINK_COLOURS=<part>=<RRGGBB>[,<part>=<RRGGBB>...] (parts as part_name(): tunic, hair, skin,
// undershirt, pants, boots, emblem, soles): once the masks are ready, Link's own textures in guest memory are
// found (their FTEX objects, by size, format and the hash of their bytes), copied with tint() and their
// surfaces pointed at the copies, as coop_pose.cpp does for the co-op tunics. For screenshots and checks; not
// a setting and not saved.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>

#include "../platform/host.h"
#include "../runtime.h"
#include "link_masks.h"

namespace link_masks {
namespace {
std::mutex g_mu;
std::condition_variable g_cv;
std::string g_pack3d;                                // host path of the 3D pack the game opened
std::shared_ptr<const std::vector<Mask>> g_masks;    // set once ready
bool g_started = false, g_done = false;
struct Last { const uint8_t* image = nullptr; uint32_t image_size = 0, mip_size = 0; uint64_t head = 0; const Mask* m = nullptr; };
Last g_last;

bool lower_is(const std::string& name, const char* want) {
    if (name.size() != strlen(want)) return false;
    for (size_t i = 0; i < name.size(); i++)
        if (char(tolower(static_cast<unsigned char>(name[i]))) != want[i]) return false;
    return true;
}

std::filesystem::path cache_file(const std::string& pack) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(pack, ec);
    const auto when = std::filesystem::last_write_time(pack, ec).time_since_epoch().count();
    const std::string key = std::to_string((unsigned long long)size) + ":" + std::to_string((long long)when) + ":" + std::to_string(kAlgorithm);
    char name[40];
    snprintf(name, sizeof name, "%016llx.bin", (unsigned long long)hash_bytes(reinterpret_cast<const uint8_t*>(key.data()), key.size()));
    return std::filesystem::path(host::config_dir()) / "link_masks" / name;
}

Bytes read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void work(std::string pack) {
    const auto t0 = std::chrono::steady_clock::now();
    auto masks = std::make_shared<std::vector<Mask>>();
    const std::filesystem::path cache = cache_file(pack);
    bool cached = deserialize(read_file(cache), *masks) && !masks->empty();
    if (!cached) {
        try {
            *masks = compute_from_szs(sarc_member(pack, "Link.szs"));
        } catch (const std::exception& e) {
            LOG("[link-masks] Link's textures not read (%s); no colour masks", e.what());
            masks->clear();
        }
        if (!masks->empty()) {
            std::error_code ec;
            std::filesystem::create_directories(cache.parent_path(), ec);
            const std::filesystem::path tmp = cache.string() + ".tmp";
            const Bytes data = serialize(*masks);
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
            }
            std::filesystem::rename(tmp, cache, ec);
            if (ec) std::filesystem::remove(tmp, ec);  // (the temporary file this function just wrote)
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    for (const Mask& m : *masks)
        LOG("[link-masks] %s %ux%u: tunic %u, hair %u texels (%s)", m.texture.c_str(), m.width, m.height, m.count(Part::kTunic),
            m.count(Part::kHair), cached ? "cached" : "computed");
    LOG("[link-masks] %zu masks ready in %.0f ms (%s)", masks->size(), ms, cache.string().c_str());
    std::lock_guard<std::mutex> lk(g_mu);
    if (!masks->empty()) g_masks = masks;
    g_done = true;
    g_cv.notify_all();
}

std::shared_ptr<const std::vector<Mask>> ready() {
    prepare();
    std::lock_guard<std::mutex> lk(g_mu);
    return g_masks;
}
}  // namespace

void note_open(const std::string& guest_path, const std::string& host_path) {
    const size_t slash = guest_path.find_last_of('/');
    const std::string name = slash == std::string::npos ? guest_path : guest_path.substr(slash + 1);
    if (!lower_is(name, "permanent_3d.pack")) return;
    std::lock_guard<std::mutex> lk(g_mu);
    g_pack3d = host_path;
}

void prepare() {
    std::string pack;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_started || g_pack3d.empty()) return;
        g_started = true;
        pack = g_pack3d;
    }
    std::thread(work, pack).detach();
}

const Mask* get(const std::string& texture) {
    const auto masks = ready();
    if (!masks) return nullptr;
    for (const Mask& m : *masks)
        if (m.texture == texture) return &m;
    return nullptr;
}

const Mask* find(const uint8_t* image, uint32_t image_size, const uint8_t* mips, uint32_t mip_size) {
    const auto masks = ready();  // (kept alive by g_masks for good once set)
    if (!masks || !image) return nullptr;
    const uint64_t head = hash_bytes(image, std::min<uint32_t>(image_size, 256));
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_last.m && g_last.image == image && g_last.image_size == image_size && g_last.mip_size == mip_size && g_last.head == head)
            return g_last.m;
    }
    for (const Mask& m : *masks) {
        if (m.image_size != image_size || m.mip_size != mip_size || (mip_size && !mips)) continue;
        if (hash_bytes(image, image_size, mips, mip_size) != m.hash) continue;
        std::lock_guard<std::mutex> lk(g_mu);
        g_last = {image, image_size, mip_size, head, &m};
        return &m;
    }
    return nullptr;
}

bool wait_ready(int timeout_ms) {
    prepare();
    std::unique_lock<std::mutex> lk(g_mu);
    g_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [] { return g_done; });
    return g_masks != nullptr;
}

// ---- test aid WWHD_LINK_COLOURS ----
namespace {
uint32_t be32(uint32_t ea) {
    const uint8_t* p = mem::ptr(ea);
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
void st_be32(uint32_t ea, uint32_t v) {
    uint8_t* p = mem::ptr(ea);
    p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
}
struct Want { Part part; uint32_t rgb; };
std::vector<Want> parse_colours(const char* s) {
    std::vector<Want> out;
    std::string all(s);
    size_t at = 0;
    while (at < all.size()) {
        size_t end = all.find(',', at);
        if (end == std::string::npos) end = all.size();
        const std::string item = all.substr(at, end - at);
        const size_t eq = item.find('=');
        if (eq != std::string::npos)
            for (int p = 1; p < kParts; p++)
                if (item.substr(0, eq) == part_name(Part(p))) out.push_back({Part(p), uint32_t(strtoul(item.c_str() + eq + 1, nullptr, 16)) & 0xFFFFFF});
        at = end + 1;
    }
    return out;
}
}  // namespace

void frame() {
    static const char* env = getenv("WWHD_LINK_COLOURS");
    static std::vector<Want> want = env && *env ? parse_colours(env) : std::vector<Want>{};
    static unsigned frames = 0, tries = 0;
    static std::vector<uint32_t> done;  // FTEX objects already pointed at a copy
    if (want.empty() || ++frames % 60 || tries >= 120) return;
    const auto masks = ready();
    if (!masks) return;
    tries++;
    // Link's textures among the FTEX objects in MEM2 (the game keeps Link.bfres in its heap and draws its
    // textures in place): "FTEX", then the GX2Surface (+4: dim, width, height, depth, mips, format... +0x20 image
    // size, +0x24 image, +0x28 mip size, +0x2C mips, +0x38 alignment), big-endian
    const uint8_t* base = mem::ptr(mem::kMem2Start);
    const size_t span = mem::kMem2End - mem::kMem2Start;
    unsigned made = 0;
    const uint8_t* const end = base + span - 0xC0;
    for (const uint8_t* p = base; p < end; p += 4) {  // (FTEX objects are 4-byte aligned; memmem is not on every host)
        if (p[0] != 'F' || p[1] != 'T' || p[2] != 'E' || p[3] != 'X') continue;
        const uint32_t tex = mem::guest(p);
        const uint32_t gx = tex + 4;
        for (const Mask& m : *masks) {
            if (be32(gx + 4) != m.width || be32(gx + 8) != m.height || be32(gx + 0x14) != m.format || be32(gx + 0x20) != m.image_size ||
                be32(gx + 0x28) != m.mip_size)
                continue;
            if (std::find(done.begin(), done.end(), tex) != done.end()) break;
            const uint32_t img = be32(gx + 0x24), mip = be32(gx + 0x2C);
            if (img < mem::kMem2Start || img + m.image_size > mem::kMem2End || (m.mip_size && (mip < mem::kMem2Start || mip + m.mip_size > mem::kMem2End))) break;
            if (find(mem::ptr(img), m.image_size, m.mip_size ? mem::ptr(mip) : nullptr, m.mip_size) != &m) break;
            const uint32_t align = std::max<uint32_t>(be32(gx + 0x38), 0x100);
            const uint32_t nimg = mem::host_alloc(m.image_size, align), nmip = m.mip_size ? mem::host_alloc(m.mip_size, align) : 0;
            memcpy(mem::ptr(nimg), mem::ptr(img), m.image_size);
            if (nmip) memcpy(mem::ptr(nmip), mem::ptr(mip), m.mip_size);
            for (const Want& w : want) tint(m, w.part, w.rgb, mem::ptr(nimg), nmip ? mem::ptr(nmip) : nullptr);
            st_be32(gx + 0x24, nimg);
            if (nmip) st_be32(gx + 0x2C, nmip);
            done.push_back(tex);
            made++;
            LOG("[link-masks] test aid: %s at %08X recoloured (copy %08X)", m.texture.c_str(), tex, nimg);
            break;
        }
    }
    if (made) {
        LOG("[link-masks] test aid: %u of Link's textures recoloured (%s)", made, env);
        tries = std::max(tries, 115u);  // a few more looks (a texture loaded later), then no more scans
    }
}

}  // namespace link_masks
