// Native J3D material descriptions (gx2/native_j3d_desc.h). Decomp build, fast mode, WWHD_NATIVE_DRAW=ON only.
// Game side: the decomp's entry points (port_native_draw.h wwhd_j3ddesc_*): keys, define/env captures, tables.
// Render side (stage D0): the verify of every Nth described draw against the site's own calls (the register file at
// the site's draw and what that draw resolved), and the inventory counters.
#include "../../third_party/cemu/cemu_shim.h"  // (as the gx2 sources get it: -include; for gx2_regs.h)
#include "gx2/native_j3d_desc.h"
#include "gx2/native_j3d_desc_draw.h"
#include "gx2/native_draw.h"
#include "gx2/gx2_cmd.h"
#include "gx2/gx2_regs.h"
#include "gx2/state_groups.h"
#ifdef WWHD_HAS_VULKAN
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "gfx/vulkan/shaders.h"
#endif
#include "runtime.h"
#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
namespace interp { uint64_t logic_steps(); }  // (interp.cpp: the logic step, WWHD_PROC_DIGEST's frame column)

using namespace Latte;

namespace native_draw::j3ddesc {

int mode() {
    static const int v = [] {
        const int m = packets_requested() ? switch_value("WWHD_NATIVE_J3D_DESC", 2) : 0;
        const int k = m == 1 || m == 2 ? m : 0;
        if (packets_requested() && m)
            LOG("[j3d-desc] J3D material descriptions %s (WWHD_NATIVE_J3D_DESC=%d)",
                k == 1 ? "described and verified" : k == 2 ? "drawn from their descriptions" : "off (unknown mode)", m);
        return k;
    }();
    return v;
}

namespace {
// ---------------------------------------------------------------- counters (both threads; read at the render swap)
enum Count : uint32_t {
    // game side: the inventory
    cHook, cDescribed, cSkip0, cDescribedPass0 = cSkip0 + kReasons, cDescribedPass1, cDescribedPass2,
    cDefines, cDefNew, cDefShader, cDefDrawState, cDefStatePart, cDefProjection, cDefFetch, cDefSamplerTable,
    cDefVariable, cDefContent, cDefRefresh, cDefSame, cEnvCaptures, cEnvNew, cEnvKey, cEnvFrame, cEnvSame, cEnvVersions,
    cMeshes, cRecording, cBlocks, cListBindsCount,
    // render side: verify
    cDraws, cUnknown, cVerifyDue, cSiteDraws, cInterrupted, cMeshMismatch, cImageMismatch, cEnvMismatch,
    cBlockMismatch, cListChanged, cVerifiedOk, cResolvedSeen, cResFirst, cResSame, cResShader, cResFetch, cResPipeline,
    cResTargets, cResTextures, cAmbientWords, cAmbientDraws, cAmbientChanged, cPassInstances, cAmbientDescEnv, cResSurfaceOnly,
    // ambient changes by class: per pass instance (P) and per (desc, env) (D); classes: vertex buffers, texture and
    // sampler words, uniform-block words, ALU constants, vertex semantics, other
    cAmbP0, cAmbP1, cAmbP2, cAmbP3, cAmbP4, cAmbP5, cAmbD0, cAmbD1, cAmbD2, cAmbD3, cAmbD4, cAmbD5, cBaseVertex,
    cExtraRace, cTlHit, cTlEnvHit, cTlMeshHit, cProgFlagOk, cProgFlagWrong, cProgBind, cAmbIgnored, cAmbLive,
    cAmbRefused, cEnvDirty,
    cCounts
};
const char* const kReasonNames[kReasons] = {"described", "pass masked", "no resource", "named frame (wii_pipeline)",
                                            "no program", "list binds", "foreign command in capture",
                                            "port feature", "table full", "prepare only", "not recording"};
const char* count_name(uint32_t c) {
    static const char* const names[] = {
        "hook calls", "described", nullptr, "described pass 0", "described pass 1", "described pass 2",
        "defines", "define: new", "define: shader", "define: draw state", "define: state part",
        "define: projection flags", "define: fetch", "define: sampler table", "define: streams/samplers",
        "define: texture/sampler content", "define: refresh", "define: same image (kept)", "env captures", "env: new",
        "env: key", "env: texture/projection content", "env: same image (kept)", "env versions", "meshes", "recording", "blocks",
        "list binds", "desc draws", "unknown records", "verify due", "site draws", "interrupted",
        "verify: mesh mismatches", "verify: image mismatches", "verify: env mismatches", "verify: block mismatches",
        "verify: list bytes changed", "verify: ok", "resolved: seen", "resolved: first", "resolved: same",
        "resolved differs: shaders", "resolved differs: fetch", "resolved differs: pipeline",
        "resolved differs: targets", "resolved differs: textures", "ambient words", "ambient draws",
        "ambient changed mid-pass (D1 fallback)", "pass instances",
        "ambient differs for the same desc and env", "resolved differs: textures, same surfaces (views/samplers only)",
        "pass-snapshot change: vertex buffers", "pass-snapshot change: textures/samplers",
        "pass-snapshot change: uniform blocks", "pass-snapshot change: ALU constants", "pass-snapshot change: semantics",
        "pass-snapshot change: other", "desc+env change: vertex buffers", "desc+env change: textures/samplers",
        "desc+env change: uniform blocks", "desc+env change: ALU constants", "desc+env change: semantics",
        "desc+env change: other", "draws with a mesh base vertex", "extra-block slot race (not a mismatch)",
        "key cache hits (no lock)", "env cache hits (no lock)", "mesh cache hits (no lock)",
        "site capture: program-bind flag right", "site capture: program-bind flag WRONG", "draws that bind the program",
        "ambient: leftover words past the program's counts (ignored by rule)",
        "ambient: read-but-unbound block words (taken live)", "ambient: refused (D1 translated path)",
        "env: a bound frame texture is dirty (the site's bind updates it)"};
    if (c >= cSkip0 && c < cSkip0 + kReasons) return kReasonNames[c - cSkip0];
    const uint32_t i = c < cSkip0 ? c : c - kReasons + 1;
    return i < sizeof names / sizeof names[0] && names[i] ? names[i] : "?";
}
std::atomic<uint64_t> g_count[cCounts];
inline void count(uint32_t c, uint64_t n = 1) { g_count[c].fetch_add(n, std::memory_order_relaxed); }
// game-thread timing (WWHD_NATIVE_J3D_DESC_TIMING=1): ns in the key compare (begin) and in the whole hook
std::atomic<uint64_t> g_keyNs{0}, g_hookNs{0}, g_defineNs{0}, g_timed{0}, g_contentNs{0}, g_envNs{0}, g_siteNs{0},
    g_siteTimed{0};
bool timing() {
    static const bool v = switch_value("WWHD_NATIVE_J3D_DESC_TIMING", 0) != 0;
    return v;
}
inline uint64_t now_ns() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline bool in(uint32_t r, uint32_t first, uint32_t words) { return r >= first && r < first + words; }
inline bool readable(uint32_t addr, uint32_t size) {
    return addr >= 0x00800000u && size < 0x100000 && uint64_t(addr) + size <= 0xF0000000ull;
}
bool block_reg(uint32_t r) {
    return in(r, mmSQ_VTX_UNIFORM_BLOCK_START, 16 * 7) || in(r, mmSQ_PS_UNIFORM_BLOCK_START, 16 * 7) ||
           in(r, mmSQ_GS_UNIFORM_BLOCK_START, 16 * 7);
}
bool texture_reg(uint32_t r) {
    return in(r, mmSQ_TEX_RESOURCE_WORD0, 0x1000) || in(r, REGADDR::SQ_TEX_SAMPLER_WORD0_0, 18 * 3 * 3);
}
// the net register writes of a command list (display lists decoded from BYTES when given, else guest memory)
void decode(const uint32_t* w, uint32_t n, std::vector<std::pair<uint32_t, uint32_t>>& out, int depth,
            const uint8_t** lists = nullptr, const uint8_t* listsEnd = nullptr) {
    uint32_t i = 0;
    while (i < n) {
        const uint32_t op = w[i] & 0xFF, m = w[i] >> 8;
        if (i + 1 + m > n) return;
        if (op == gx2::OP_SET_REGS && m >= 1) {
            for (uint32_t k = 1; k < m; ++k) out.push_back({w[i + 1] + k - 1, w[i + 1 + k]});
        } else if (op == gx2::OP_CALL && m == 2 && depth < 4) {
            if (lists && *lists && *lists + w[i + 2] <= listsEnd) {
                std::vector<uint32_t> copy(w[i + 2] / 4);
                std::memcpy(copy.data(), *lists, copy.size() * 4);
                *lists += w[i + 2];
                decode(copy.data(), uint32_t(copy.size()), out, depth + 1);
            } else if (readable(w[i + 1], w[i + 2])) {
                decode(reinterpret_cast<const uint32_t*>(mem::ptr(w[i + 1])), w[i + 2] / 4, out, depth + 1);
            }
        }
        i += 1 + m;
    }
}
}  // namespace

// ---------------------------------------------------------------- tables (published records, immutable)
std::atomic<const Desc*> g_descs[0x10000];
std::atomic<const Env*> g_envs[0x10000];
std::atomic<const Mesh*> g_meshes[0x10000];
const Desc* find_desc(uint32_t id, uint32_t version) {
    for (const Desc* d = id < 0x10000 ? g_descs[id].load(std::memory_order_acquire) : nullptr; d; d = d->prev)
        if (d->version == version) return d;
    return nullptr;
}
const Env* find_env(uint32_t id, uint32_t version) {
    for (const Env* e = id < 0x10000 ? g_envs[id].load(std::memory_order_acquire) : nullptr; e; e = e->prev)
        if (e->version == version) return e;
    return nullptr;
}
const Mesh* find_mesh(uint32_t id) { return id < 0x10000 ? g_meshes[id].load(std::memory_order_acquire) : nullptr; }

// ---------------------------------------------------------------- game side
namespace {
std::atomic<uint64_t> g_gameFrame{1};
struct Key4 {
    uint32_t a, b, c, d;
    bool operator==(const Key4& o) const { return a == o.a && b == o.b && c == o.c && d == o.d; }
};
struct Key4Hash {
    size_t operator()(const Key4& k) const {
        return size_t((uint64_t(k.a) << 32 | k.c) * 0x9E3779B97F4A7C15ull ^ (uint64_t(k.b) << 32 | k.d) * 0xC2B2AE3D27D4EB4Full);
    }
};
// a content range (a sampler object's GX2Texture words, a sampler key's words): compared with its copy once per game
// frame, whichever description touches it first; a change bumps its version
struct GameObject {
    uint32_t addr = 0, bytes = 0;
    std::atomic<uint32_t> version{0};
    std::atomic<uint64_t> frame{0};  // the game frame it was compared in (the first toucher compares, under mu)
    std::mutex mu;
    std::vector<uint8_t> copy;
};
struct GameDesc {
    uint16_t id = 0, version = 0;
    const Desc* rec = nullptr;
    std::vector<uint32_t> key;
    std::vector<std::pair<GameObject*, uint32_t>> objects;  // its content ranges and their versions at define
};
struct GameObject;
struct GameEnv {
    uint16_t id = 0, version = 0;
    const Env* rec = nullptr;
    std::vector<uint32_t> key;
    std::vector<std::pair<GameObject*, uint32_t>> objects;  // its 7 textures (and pass 0's projection) and versions
    uint64_t frame = 0;
};
struct MeshKey {
    uint32_t w[5];
    bool operator==(const MeshKey& o) const { return !std::memcmp(w, o.w, sizeof w); }
};
struct MeshKeyHash {
    size_t operator()(const MeshKey& k) const {
        return size_t((uint64_t(k.w[3]) << 32 | k.w[1]) * 0x9E3779B97F4A7C15ull ^ (k.w[0] << 8 | k.w[2] | uint64_t(k.w[4]) << 16));
    }
};
struct Game {
    std::mutex mu;
    std::unordered_map<Key4, GameDesc, Key4Hash> descs;
    std::unordered_map<Key4, GameEnv, Key4Hash> envs;
    std::unordered_map<MeshKey, uint16_t, MeshKeyHash> meshes;
    std::unordered_map<uint64_t, GameObject> objects;
    uint32_t nextDesc = 1, nextEnv = 1, nextMesh = 1;
};
// the object's state this game frame (g.mu held)
// the object's state this game frame: compared with its copy by the first thread that touches it in the frame (its own
// lock); every other touch in that frame reads the version without a lock
GameObject* touch(GameObject* o, uint64_t frame) {
    if (o->frame.load(std::memory_order_acquire) == frame) return o;
    std::lock_guard<std::mutex> lk(o->mu);
    if (o->frame.load(std::memory_order_relaxed) == frame) return o;
    if (readable(o->addr, o->bytes)) {
        const uint8_t* b = mem::ptr(o->addr);
        if (o->copy.size() != o->bytes || std::memcmp(o->copy.data(), b, o->bytes)) {
            o->copy.assign(b, b + o->bytes);
            o->version.fetch_add(1, std::memory_order_relaxed);
        }
    }
    o->frame.store(frame, std::memory_order_release);
    return o;
}
// WWHD_NATIVE_J3D_DESC_TLCACHE (default 1; 0: off): each recording thread keeps its own copy of the keys it last
// found valid (descriptions, envs, meshes); a hit compares with that copy and the objects' versions, no lock
bool tlcache_on() {
    static const bool v = native_draw::switch_value("WWHD_NATIVE_J3D_DESC_TLCACHE", 1) != 0;
    return v;
}
struct TlDesc {
    std::vector<uint32_t> key;
    std::vector<std::pair<GameObject*, uint32_t>> objects;
    GameDesc* desc = nullptr;
    uint16_t id = 0, version = 0;
};
struct TlEnv {
    std::vector<uint32_t> key;
    std::vector<std::pair<GameObject*, uint32_t>> objects;
    GameEnv* env = nullptr;
    uint16_t id = 0, version = 0;
};
thread_local std::unordered_map<Key4, TlDesc, Key4Hash> t_descs;
thread_local std::unordered_map<Key4, TlEnv, Key4Hash> t_envs;
thread_local std::unordered_map<MeshKey, uint16_t, MeshKeyHash> t_meshes;
bool objects_hold(const std::vector<std::pair<GameObject*, uint32_t>>& objects, uint64_t frame) {
    for (const auto& [o, version] : objects)
        if (touch(o, frame)->version.load(std::memory_order_relaxed) != version) return false;
    return true;
}
Game& game() {
    static Game g;
    return g;
}
// the draw being described on this thread
struct Current {
    uint64_t t0 = 0, tEnv = 0;
    bool open = false;
    GameDesc* desc = nullptr;  // (stable: unordered_map nodes)
    GameEnv* env = nullptr;
    uint32_t descId = 0, descVersion = 0, envId = 0, envVersion = 0, meshId = 0, flags = 0, pass = 0;
    bool envDirty = false;  // (wwhd_j3ddesc_flag(2)) a bound frame texture is dirty: the env is captured
    uint32_t blockCount = 0, blocks[kMaxBlocks * kBlockWords] = {};
    bool overflow = false;
    // a define or env capture in progress
    uint32_t segment = kSegNone;
    bool foreign = false, listBinds = false;
    std::vector<uint32_t> seg[kSegs];
    std::vector<uint8_t> lists;
    std::vector<uint32_t> key;
    std::vector<std::pair<GameObject*, uint32_t>> objects, envObjects;
    uint32_t why = 0;
    // WWHD_NATIVE_J3D_DESC_SITECAP=1 (mode 1, debug): the site's own commands after the hook, up to its draw
    bool siteCap = false, preCap = false, siteTime = false;
    uint64_t tSite = 0;
    std::vector<uint32_t> site, pre;
    const Desc* capDesc = nullptr;
    const Env* capEnv = nullptr;
    uint32_t capBlocks = 0, capBlock[kMaxBlocks * kBlockWords] = {}, capFlags = 0;
};
thread_local Current t_cur;
bool sitecap_on() {
    static const bool v = switch_value("WWHD_NATIVE_J3D_DESC_SITECAP", 0) != 0;
    return v;
}
// the site capture's findings: registers the site wrote that the description does not hold / holds another value of
std::mutex g_siteMu;
std::unordered_map<uint32_t, uint64_t> g_siteExtra, g_siteDiff, g_pre;
uint64_t g_preOps[64];
uint64_t g_siteDraws = 0;
// (debug) a command list as op/register summaries
void summary(const std::vector<uint32_t>& w, char* line, size_t size) {
    int j = 0;
    for (size_t i = 0; i < w.size() && j < int(size) - 24;) {
        const uint32_t op = w[i] & 0xFF, m = w[i] >> 8;
        if (op == gx2::OP_SET_REGS && m) j += std::snprintf(line + j, size - j, " %04X+%u", w[i + 1], m - 1);
        else j += std::snprintf(line + j, size - j, " op%u(%08X)", op, m ? w[i + 1] : 0);
        i += 1 + m;
    }
}
void site_compare(Current& c) {
    if (g_trace && c.capDesc && c.capDesc->pass == 2) {
        static int left = 6;
        const Mesh* mm = find_mesh(c.meshId);
        if (mm && mm->count == uint32_t(native_draw::switch_value("WWHD_NATIVE_J3D_DESC_UNITS", 0)) && left > 0) {
            --left;
            char line[2000];
            summary(c.site, line, sizeof line);
            LOG("[j3d-desc] site commands of desc %u (env %u, %zu image words):%s", c.capDesc->id, c.capEnv ? c.capEnv->id : 0,
                c.capEnv ? c.capEnv->image.size() : 0, line);
            for (size_t i = 0; i < c.site.size();) {
                const uint32_t op = c.site[i] & 0xFF, m = c.site[i] >> 8;
                if (op == gx2::OP_CALL && m == 2 && readable(c.site[i + 1], c.site[i + 2])) {
                    const uint32_t* lw = reinterpret_cast<const uint32_t*>(mem::ptr(c.site[i + 1]));
                    std::vector<uint32_t> l(lw, lw + c.site[i + 2] / 4);
                    summary(l, line, sizeof line);
                    LOG("[j3d-desc]   list %08X (%u bytes):%s", c.site[i + 1], c.site[i + 2], line);
                }
                i += 1 + m;
            }
            summary(c.capDesc->segment[kSegProgram], line, sizeof line);
            LOG("[j3d-desc]   desc program segment:%s", line);
        }
    }
    std::vector<std::pair<uint32_t, uint32_t>> all, d;
    decode(c.site.data(), uint32_t(c.site.size()), all, 0);
    std::unordered_map<uint32_t, uint32_t> want;
    if (c.capEnv) {
        decode(c.capEnv->image.data(), uint32_t(c.capEnv->image.size()), d, 0);
        for (auto& [r, v] : d) want[r] = v;
    }
    if (c.capDesc) {
        d.clear();
        const uint8_t* lists = c.capDesc->lists.data();
        const uint8_t* end = lists + c.capDesc->lists.size();
        for (uint32_t sg = kSegProgram; sg <= kSegState; ++sg)
            decode(c.capDesc->segment[sg].data(), uint32_t(c.capDesc->segment[sg].size()), d, 0, &lists, end);
        for (auto& [r, v] : d) want[r] = v;
    }
    for (uint32_t i = 0; i < c.capBlocks; ++i) {
        const uint32_t* b = c.capBlock + i * kBlockWords;
        const uint32_t stage = b[0] >> 16, loc = b[0] & 0xFFFF;
        const uint32_t base = stage == 0 ? mmSQ_VTX_UNIFORM_BLOCK_START : stage == 1 ? mmSQ_PS_UNIFORM_BLOCK_START
                                                                                  : mmSQ_GS_UNIFORM_BLOCK_START;
        const uint32_t w[7] = {b[1], b[2] - 1, 0, 1, 0, 0, 0xC0000000u};
        for (uint32_t k = 0; k < 7; ++k) want[base + loc * 7 + k] = w[k];
    }
    std::unordered_map<uint32_t, uint32_t> got;
    for (auto& [r, v] : all) got[r] = v;
    // the program-bind flag: the site's commands hold the program's list call (or its stage programs) exactly when set
    bool bound = got.count(mmSQ_PGM_START_PS) != 0;
    for (size_t i = 0; i < c.site.size() && !bound;) {
        const uint32_t op = c.site[i] & 0xFF, m = c.site[i] >> 8;
        if (op == gx2::OP_CALL && m == 2 && c.capDesc)
            for (size_t j = 0; j + 2 < c.capDesc->segment[kSegProgram].size(); ++j)
                if ((c.capDesc->segment[kSegProgram][j] & 0xFF) == gx2::OP_CALL &&
                    c.capDesc->segment[kSegProgram][j + 1] == c.site[i + 1])
                    bound = true;
        i += 1 + m;
    }
    count(bound == ((c.capFlags & kFlagProgramBind) != 0) ? cProgFlagOk : cProgFlagWrong);
    std::lock_guard<std::mutex> lk(g_siteMu);
    ++g_siteDraws;
    for (auto& [r, v] : got) {
        auto it = want.find(r);
        if (it == want.end()) ++g_siteExtra[r];
        else if (it->second != v) ++g_siteDiff[r];
    }
}

GameObject* object(Game& g, uint32_t addr, uint32_t bytes, uint64_t frame) {
    auto [it, fresh] = g.objects.try_emplace(uint64_t(addr) << 32 | bytes);
    GameObject* o = &it->second;
    if (fresh) o->addr = addr, o->bytes = bytes;
    return touch(o, frame);
}
// an env's objects: the 7 frame textures (agl::TextureSampler, 0x198 bytes: the bind reads its GX2Texture, sampler
// words, border and dirty byte) and, pass 0, the projection's render state (0x70 bytes)
void env_objects(Game& g, const uint32_t* key, uint32_t words, uint64_t frame,
                 std::vector<std::pair<GameObject*, uint32_t>>& out) {
    out.clear();
    for (uint32_t i = 5; i < 12 && i < words; ++i)
        if (key[i]) {
            GameObject* o = object(g, key[i], 0x198, frame);
            out.push_back({o, o->version});
        }
    if (key[4]) {
        GameObject* o = object(g, key[4], 0x70, frame);
        out.push_back({o, o->version});
    }
}
// the variable part's content ranges {address, bytes} (J3DNativeDesc.h DescKey) as objects with their versions now
void objects_of(Game& g, const uint32_t* key, uint32_t words, uint64_t frame,
                std::vector<std::pair<GameObject*, uint32_t>>& out) {
    out.clear();
    uint32_t i = kKeyFixed;
    if (words <= i) return;
    const uint32_t streams = key[i];
    i += 1 + streams * 4;
    if (i >= words) return;
    const uint32_t samplers = key[i];
    i += 1 + samplers * 2;
    if (i >= words) return;
    const uint32_t ranges = key[i++];
    for (uint32_t r = 0; r < ranges && i + 1 < words; ++r, i += 2) {
        auto [it, fresh] = g.objects.try_emplace(uint64_t(key[i]) << 32 | key[i + 1]);
        GameObject* o = &it->second;
        if (fresh) o->addr = key[i], o->bytes = key[i + 1];
        touch(o, frame);
        out.push_back({o, o->version});
    }
}
// the first key word that differs: the define's reason
uint32_t key_reason(const std::vector<uint32_t>& a, const uint32_t* b, uint32_t n) {
    const uint32_t m = std::min<uint32_t>(uint32_t(a.size()), n);
    uint32_t i = 0;
    while (i < m && a[i] == b[i]) ++i;
    if (i < 6) return cDefShader;  // (identity words: never differ for the same map key but the record/frame)
    if (i <= 12) return cDefShader;
    if (i <= 25) return cDefDrawState;
    if (i <= 35) return cDefStatePart;
    if (i == 36) return cDefProjection;
    if (i <= 38) return cDefFetch;
    if (i <= 40) return cDefSamplerTable;
    return cDefVariable;
}
}  // namespace
#ifdef WWHD_DECOMP_FAST
// drawoff (drawoff/drawoff.h): a J3D draw is open on this thread (its binds stay as they are: described draws keep
// their register state across passes)
bool game_draw_open() { return t_cur.open; }
#endif

thread_local bool t_capturing = false;
extern bool g_trace;

bool capture(uint32_t op, const uint32_t* p, uint32_t n) {
    Current& c = t_cur;
    if (c.siteTime) {  // (timing: the site's own bind code, hook to draw: what mode 2 no longer runs)
        if (op == gx2::OP_DRAW_INDEXED || op == gx2::OP_DRAW) {
            c.siteTime = false;
            t_capturing = false;
            g_siteNs.fetch_add(now_ns() - c.tSite, std::memory_order_relaxed);
            g_siteTimed.fetch_add(1, std::memory_order_relaxed);
        }
        return false;
    }
    if (c.preCap) {  // (passive: what this thread emits between its J3D draws)
        if (op == gx2::OP_SET_REGS || op == gx2::OP_CALL) {
            if (op == gx2::OP_CALL) {
                std::lock_guard<std::mutex> lk(g_siteMu);
                ++g_preOps[op & 63];
            }
            c.pre.push_back(op | (n << 8));
            c.pre.insert(c.pre.end(), p, p + n);
        } else {
            std::lock_guard<std::mutex> lk(g_siteMu);
            ++g_preOps[op < 0x40 ? op : 0x30 + (op & 15)];
        }
        return false;
    }
    if (c.siteCap) {  // (passive: the site's commands go out as they are)
        if (op == gx2::OP_DRAW_INDEXED || op == gx2::OP_DRAW) {
            c.siteCap = false;
            site_compare(c);
            c.preCap = true;  // (t_capturing stays on: until the next J3D hook on this thread)
            c.pre.clear();
        } else if (op != gx2::OP_DEBUG_CONTEXT) {
            c.site.push_back(op | (n << 8));
            c.site.insert(c.site.end(), p, p + n);
        }
        return false;
    }
    if (op == gx2::OP_DEBUG_CONTEXT) return true;
    if (op != gx2::OP_SET_REGS && op != gx2::OP_INVALIDATE) {
        if (!c.foreign) {
            static std::atomic<uint32_t> said{0};
            if (said.fetch_add(1) < 8) LOG("[j3d-desc] capture: command %u (%u words) in segment %u", op, n, c.segment);
        }
        c.foreign = true;
    }
    std::vector<uint32_t>& s = c.seg[c.segment < kSegs ? c.segment : 0];
    s.push_back(op | (n << 8));
    s.insert(s.end(), p, p + n);
    return true;
}

bool capture_regs(uint32_t first, const uint32_t* values, uint32_t count) {
    thread_local std::vector<uint32_t> w;
    w.resize(count + 1);
    w[0] = first;
    std::memcpy(w.data() + 1, values, count * 4);
    return capture(gx2::OP_SET_REGS, w.data(), count + 1);
}

// WWHD_NATIVE_J3D_DESC_WRITESET=1 (debug, every mode): the write-set proof of the description path. The decomp names,
// at the start of each J3D draw, the game memory its site may write outside its GX2 calls (wwhd_j3ddesc_watch) and
// says when the draw is over (wwhd_j3ddesc_watch_done: the site's end in modes 0/1, submit's return in mode 2). Each
// draw's post-state (each named range's bytes then) is hashed per range into the logic step the draw began in (the
// frame column of WWHD_PROC_DIGEST; the swap count drifts against it between runs in the title); the
// frame's value is the sum of its distinct range states (several recording threads: independent of their order).
// Two frames after a frame ends: "[j3d-desc] writeset frame N hash H states S draws D drawsum X late L" (mode 0 and
// mode 2 runs must log the same H, S and D, up to a frame offset; X, the per-draw sum, is informative only).
// (Not hashed at the swap: other threads keep writing some named bytes while the next frame records, and a range named
// in an earlier frame may since have been freed and reused.)
int writeset_level() {
    static const int v = native_draw::switch_value("WWHD_NATIVE_J3D_DESC_WRITESET", 0);
    return v;
}
bool writeset_on() { return writeset_level() != 0; }
struct WsFrame {
    uint64_t sum = 0, draws = 0;
    // the distinct post-states (range, bytes) of the frame and their sum: the proof's value. (The per-draw sum also
    // counts how many draws saw each state: that depends on the recording threads' interleaving, e.g. a pass-0 draw
    // before or after another thread's pass-1 polygon-offset setters on the same draw state, in mode 0 too.)
    std::unordered_map<uint64_t, uint64_t> states;  // state -> address << 32 | bytes
    uint64_t stateSum = 0;
    std::map<uint64_t, std::string> fresh;  // (WRITESET=2) states not in the last logged frame: "ADDR+BYTES HEX
    std::set<std::string> dump;  // (WRITESET_DUMP frames) the distinct post-states "ADDR+BYTES HEX"
};
std::mutex g_watchMu;
std::map<uint64_t, WsFrame> g_wsFrames;
uint64_t g_wsLogged = 0;  // frames <= this were logged; a draw that still lands there is "late"
uint64_t g_wsLate = 0;
struct WsDraw {
    bool open = false;
    uint64_t frame = 0;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
};
thread_local WsDraw t_ws;
const std::vector<uint64_t>& ws_dump_frames() {
    // WWHD_NATIVE_J3D_DESC_WRITESET_DUMP=F1,F2,...: those frames' distinct post-states, one line each
    static const std::vector<uint64_t> v = [] {
        std::vector<uint64_t> r;
        if (const char* e = std::getenv("WWHD_NATIVE_J3D_DESC_WRITESET_DUMP"))
            for (const char* q = e; *q;) {
                r.push_back(std::strtoull(q, const_cast<char**>(&q), 10));
                if (*q == ',') ++q; else break;
            }
        return r;
    }();
    return v;
}
// (WRITESET=2) the last logged frame's states, and a range's bytes as "ADDR+BYTES HEX"
std::unordered_map<uint64_t, uint64_t> g_wsLast;
std::string ws_hex(uint32_t addr, uint32_t bytes) {
    char head[32];
    std::snprintf(head, sizeof head, "%08X+%X ", addr, bytes);
    std::string line = head;
    if (!readable(addr, bytes)) return line + "-";
    char hex[4];
    const uint8_t* b = mem::ptr(addr);
    for (uint32_t i = 0; i < bytes && i < 0x200; ++i) std::snprintf(hex, sizeof hex, "%02X", b[i]), line += hex;
    return line;
}
// BYTES | 0x80000000 (the current core's render context): hashed without its address (the same draw may be recorded
// on another core's thread from run to run, in mode 0 too: seen in the title, 6 draws moving between 104B4650 and
// 104B4640 with the same contents)
constexpr uint32_t kWsAnyAddress = 0x80000000u;
void ws_watch(uint32_t addr, uint32_t bytes) {
    WsDraw& d = t_ws;
    if (!d.open) d.open = true, d.frame = interp::logic_steps(), d.ranges.clear();
    d.ranges.emplace_back(addr, bytes);
}
void ws_done() {
    WsDraw& d = t_ws;
    if (!d.open) return;
    d.open = false;
    uint64_t h = 0;
    thread_local std::vector<std::pair<uint64_t, uint64_t>> states;
    states.clear();
    for (const auto& [addr, flagged] : d.ranges) {
        const uint32_t bytes = flagged & ~kWsAnyAddress;
        uint64_t r = 0xCBF29CE484222325ull;
        r = (r ^ ((flagged & kWsAnyAddress) ? 0 : addr)) * 0x100000001B3ull;
        r = (r ^ bytes) * 0x100000001B3ull;
        if (readable(addr, bytes)) {
            const uint8_t* b = mem::ptr(addr);
            for (uint32_t i = 0; i < bytes; ++i) r = (r ^ b[i]) * 0x100000001B3ull;
        }
        r ^= r >> 33, r *= 0xFF51AFD7ED558CCDull, r ^= r >> 33;  // (mixed before the sums)
        states.emplace_back(r, uint64_t(addr) << 32 | bytes);
        h = (h ^ r) * 0x100000001B3ull;
    }
    const auto& dumpAt = ws_dump_frames();
    const bool dump = std::find(dumpAt.begin(), dumpAt.end(), d.frame) != dumpAt.end();
    std::vector<std::string> lines;
    if (dump)
        for (const auto& [addr, flagged] : d.ranges) {
            const uint32_t bytes = flagged & ~kWsAnyAddress;
            if (!readable(addr, bytes)) continue;
            char head[32];
            std::snprintf(head, sizeof head, "%08X+%X ", addr, bytes);
            std::string line = head;
            char hex[4];
            const uint8_t* b = mem::ptr(addr);
            for (uint32_t i = 0; i < bytes && i < 0x200; ++i) std::snprintf(hex, sizeof hex, "%02X", b[i]), line += hex;
            lines.push_back(std::move(line));
        }
    std::lock_guard<std::mutex> lk(g_watchMu);
    if (d.frame <= g_wsLogged) {
        ++g_wsLate;
        return;
    }
    WsFrame& f = g_wsFrames[d.frame];
    f.sum += h, ++f.draws;
    for (const auto& [r, where] : states)
        if (f.states.emplace(r, where).second) {
            f.stateSum += r;
            if (writeset_level() >= 2 && !g_wsLast.count(r)) f.fresh[r] = ws_hex(uint32_t(where >> 32), uint32_t(where));
        }
    for (auto& l : lines) f.dump.insert(std::move(l));
}
void game_swap() {
    g_gameFrame.fetch_add(1, std::memory_order_relaxed);
    if (!writeset_on()) return;
    const uint64_t frame = interp::logic_steps();
    std::lock_guard<std::mutex> lk(g_watchMu);
    while (!g_wsFrames.empty() && g_wsFrames.begin()->first + 2 <= frame) {
        auto it = g_wsFrames.begin();
        for (const auto& l : it->second.dump)
            LOG("[j3d-desc] writeset dump frame %llu %s", (unsigned long long)it->first, l.c_str());
        if (writeset_level() >= 2) {
            for (const auto& [r, l] : it->second.fresh)
                LOG("[j3d-desc] writeset change frame %llu + %016llX %s", (unsigned long long)it->first, (unsigned long long)r, l.c_str());
            for (const auto& [r, where] : g_wsLast)
                if (!it->second.states.count(r))
                    LOG("[j3d-desc] writeset change frame %llu - %016llX %08X+%X", (unsigned long long)it->first,
                        (unsigned long long)r, unsigned(where >> 32), unsigned(uint32_t(where)));
            g_wsLast = it->second.states;
        }
        LOG("[j3d-desc] writeset frame %llu hash %016llX states %zu draws %llu drawsum %016llX late %llu",
            (unsigned long long)it->first, (unsigned long long)it->second.stateSum, it->second.states.size(),
            (unsigned long long)it->second.draws, (unsigned long long)it->second.sum, (unsigned long long)g_wsLate);
        g_wsLogged = it->first;
        g_wsFrames.erase(it);
    }
    if (frame >= 2 && g_wsLogged < frame - 2) g_wsLogged = frame - 2;
}

}  // namespace native_draw::j3ddesc

// ---------------------------------------------------------------- the decomp's entry points (port_native_draw.h)
using namespace native_draw::j3ddesc;

extern "C" unsigned wwhd_j3ddesc_writeset_on(void) { return writeset_on() ? 1 : 0; }
extern "C" void wwhd_j3ddesc_watch(unsigned addr, unsigned bytes) { ws_watch(addr, bytes); }
extern "C" void wwhd_j3ddesc_watch_done(void) { ws_done(); }
extern "C" unsigned wwhd_j3ddesc_mode(void) {
    const int m = mode();
    if (m) {
        Current& c = t_cur;
        if (c.preCap) {  // (site capture: the registers this thread wrote since its last J3D draw)
            c.preCap = false;
            t_capturing = false;
            std::vector<std::pair<uint32_t, uint32_t>> all;
            decode(c.pre.data(), uint32_t(c.pre.size()), all, 0);
            std::unordered_set<uint32_t> regs;
            for (auto& [r, v] : all) regs.insert(r);
            std::lock_guard<std::mutex> lk(g_siteMu);
            for (uint32_t r : regs) ++g_pre[r];
        }
        c.open = true;
        c.desc = nullptr;
        c.env = nullptr;
        c.blockCount = 0;
        c.overflow = false;
        c.flags = 0;
        c.envDirty = false;
        c.t0 = timing() ? now_ns() : 0;
        count(cHook);
    }
    return unsigned(m);
}
extern "C" void wwhd_j3ddesc_flag(unsigned flag) {
    if (flag == 1 && t_cur.open) t_cur.flags |= kFlagProgramBind;
    // 2: a frame texture the env binds is dirty: the site's bindTextureSampler runs updateSamplerState (game memory:
    // the sampler words, dirty = 0; and new sampler register words), so this draw's env is captured whatever its key
    // (the key's dirty bytes are the values before that capture: a later dirty texture can repeat them)
    if (flag == 2 && t_cur.open) t_cur.envDirty = true;
}
extern "C" void wwhd_j3ddesc_skip(unsigned reason) {
    t_cur.open = false;
    t_cur.siteCap = false;
    t_cur.siteTime = false;
    t_capturing = false;
    count(cSkip0 + (reason < kReasons ? reason : 0));
}
extern "C" unsigned wwhd_j3ddesc_begin(const unsigned* key, unsigned words) {
    Current& c = t_cur;
    if (!c.open || words < kKeyFixed) return 0;
    const uint64_t t = c.t0 ? now_ns() : 0;
    const uint64_t frame = g_gameFrame.load(std::memory_order_relaxed);
    Game& g = game();
    unsigned result = 1;
    const Key4 k4{key[0], key[1], key[2], key[5]};
    if (tlcache_on() && !key[8]) {
        auto it = t_descs.find(k4);
        if (it != t_descs.end() && it->second.key.size() == words &&
            !std::memcmp(it->second.key.data(), key, words * 4)) {
            const uint64_t tc = t ? now_ns() : 0;
            const bool hold = objects_hold(it->second.objects, frame);
            if (tc) g_contentNs.fetch_add(now_ns() - tc, std::memory_order_relaxed);
            if (hold) {
                c.desc = it->second.desc;
                c.pass = key[1];
                c.descId = it->second.id;
                c.descVersion = it->second.version;
                count(cTlHit);
                if (t) g_keyNs.fetch_add(now_ns() - t, std::memory_order_relaxed);
                return 1;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lk(g.mu);
        auto [it, fresh] = g.descs.try_emplace(k4);
        GameDesc& d = it->second;
        c.desc = &d;
        c.pass = key[1];
        uint32_t why = 0;
        if (fresh) why = cDefNew;
        else if (key[8]) why = cDefRefresh;
        else if (d.key.size() != words || std::memcmp(d.key.data(), key, words * 4)) why = key_reason(d.key, key, words);
        else {
            const uint64_t tc = t ? now_ns() : 0;
            for (const auto& [o, version] : d.objects)
                if (touch(o, frame)->version.load(std::memory_order_relaxed) != version) {
                    why = cDefContent;
                    break;
                }
            if (tc) g_contentNs.fetch_add(now_ns() - tc, std::memory_order_relaxed);
        }
        if (why) {
            c.why = why;
            count(why);
            c.key.assign(key, key + words);
            objects_of(g, key, words, frame, c.objects);
            for (auto& s : c.seg) s.clear();
            c.lists.clear();
            c.foreign = c.listBinds = false;
            result = 2;
        } else {
            c.descId = d.id;
            c.descVersion = d.version;
            if (tlcache_on()) t_descs[k4] = TlDesc{d.key, d.objects, &d, d.id, d.version};
        }
    }
    if (t) g_keyNs.fetch_add(now_ns() - t, std::memory_order_relaxed);
    return result;
}
extern "C" void wwhd_j3ddesc_segment(unsigned segment) {
    Current& c = t_cur;
    c.segment = segment < kSegs ? segment : kSegNone;
    t_capturing = c.open && c.segment != kSegNone;
}
extern "C" void wwhd_j3ddesc_list(unsigned addr, unsigned bytes) {
    Current& c = t_cur;
    if (!c.open) return;
    // as call_display_list records it while a list is recorded: OP_CALL {list, size}
    std::vector<uint32_t>& s = c.seg[kSegProgram];
    s.insert(s.end(), {uint32_t(gx2::OP_CALL) | (2u << 8), addr, bytes});
    if (!readable(addr, bytes)) {
        c.listBinds = true;
        return;
    }
    const uint8_t* b = mem::ptr(addr);
    c.lists.insert(c.lists.end(), b, b + bytes);
    // a program list that binds blocks or textures: not a description (its binds would be the draw's)
    std::vector<std::pair<uint32_t, uint32_t>> w;
    decode(reinterpret_cast<const uint32_t*>(b), bytes / 4, w, 1);
    for (const auto& [r, v] : w)
        if (block_reg(r) || texture_reg(r)) {
            c.listBinds = true;
            break;
        }
}
extern "C" unsigned wwhd_j3ddesc_define(void) {
    Current& c = t_cur;
    t_capturing = false;
    if (!c.open || !c.desc) return 0;
    const uint64_t t = c.t0 ? now_ns() : 0;
    if (c.foreign || c.listBinds) {
        if (c.listBinds) count(cListBindsCount);
        wwhd_j3ddesc_skip(c.foreign ? kForeign : kListBinds);
        return 0;
    }
    Game& g = game();
    std::lock_guard<std::mutex> lk(g.mu);
    GameDesc& d = *c.desc;
    const bool fresh = d.rec == nullptr;
    // an unchanged image keeps its version (the key or content moved, the captured state did not)
    bool same = !fresh;
    if (same)
        for (uint32_t s = kSegProgram; s <= kSegState && same; ++s) same = d.rec->segment[s] == c.seg[s];
    if (same) same = d.rec->lists == c.lists;
    d.key = c.key;
    d.objects = c.objects;
    if (same) {
        count(cDefSame);
    } else {
        if (fresh) {
            if (g.nextDesc >= 0x10000) {
                wwhd_j3ddesc_skip(kTableFull);
                return 0;
            }
            d.id = uint16_t(g.nextDesc++);
        } else {
            ++d.version;
        }
        Desc* r = new Desc;  // (immutable once published; kept: a recorded list may name it later)
        r->id = d.id;
        r->version = d.version;
        r->binding = c.key[0];
        r->pass = c.key[1];
        r->record = c.key[2];
        r->frame = c.key[3];
        r->viewClass = c.key[5];
        r->shader = c.key[6];
        r->key = c.key;
        for (uint32_t s = kSegProgram; s <= kSegState; ++s) r->segment[s] = c.seg[s];
        r->lists = c.lists;
        r->prev = d.rec;
        d.rec = r;
        g_descs[d.id].store(r, std::memory_order_release);
        // WWHD_NATIVE_J3D_DESC_DUMP=N: the first N defines' segments, as register ranges (debug)
        static const uint32_t dump = uint32_t(native_draw::switch_value("WWHD_NATIVE_J3D_DESC_DUMP", 0));
        static std::atomic<uint32_t> dumped{0};
        if (dump && dumped.fetch_add(1) < dump)
            for (uint32_t sg = kSegProgram; sg <= kSegState; ++sg) {
                char line[1200];
                int j = std::snprintf(line, sizeof line, "[j3d-desc] dump desc %u pass %u segment %u:", r->id, r->pass, sg);
                const auto& w = r->segment[sg];
                for (size_t i = 0; i < w.size() && j < int(sizeof line) - 40;) {
                    const uint32_t op = w[i] & 0xFF, m = w[i] >> 8;
                    if (op == gx2::OP_SET_REGS && m) j += std::snprintf(line + j, sizeof line - j, " %04X+%u", w[i + 1], m - 1);
                    else j += std::snprintf(line + j, sizeof line - j, " op%u(%u)", op, m);
                    i += 1 + m;
                }
                LOG("%s", line);
            }
        count(cDefines);
        c.flags |= kFlagDefined;
    }
    c.descId = d.id;
    c.descVersion = d.version;
    if (tlcache_on())
        t_descs[Key4{c.key[0], c.key[1], c.key[2], c.key[5]}] = TlDesc{c.key, c.objects, &d, d.id, d.version};
    if (t) g_defineNs.fetch_add(now_ns() - t, std::memory_order_relaxed);
    return 1;
}
extern "C" unsigned wwhd_j3ddesc_env(const unsigned* key, unsigned words) {
    Current& c = t_cur;
    if (!c.open || words < 5) return 0;
    c.tEnv = c.t0 ? now_ns() : 0;
    const uint64_t frame = g_gameFrame.load(std::memory_order_relaxed);
    const Key4 k4{key[0], key[1], key[2], key[3]};
    if (tlcache_on() && !c.envDirty) {  // (D0 fb12d0622: a dirty bound frame texture captures the env)
        auto tl = t_envs.find(k4);
        if (tl != t_envs.end() && tl->second.key.size() == words && !std::memcmp(tl->second.key.data(), key, words * 4) &&
            objects_hold(tl->second.objects, frame)) {
            c.env = tl->second.env;
            c.envId = tl->second.id;
            c.envVersion = tl->second.version;
            count(cTlEnvHit);
            if (c.tEnv) g_envNs.fetch_add(now_ns() - c.tEnv, std::memory_order_relaxed);
            return 1;
        }
    }
    Game& g = game();
    std::lock_guard<std::mutex> lk(g.mu);
    auto [it, fresh] = g.envs.try_emplace(k4);
    GameEnv& e = it->second;
    c.env = &e;
    uint32_t why = 0;
    if (fresh) why = cEnvNew;
    else if (c.envDirty) why = cEnvDirty;
    else if (e.key.size() != words || std::memcmp(e.key.data(), key, words * 4)) why = cEnvKey;
    else
        for (const auto& [o, version] : e.objects)
            if (touch(o, frame)->version != version) {
                why = cEnvFrame;
                break;
            }
    if (!why) {
        c.envId = e.id;
        c.envVersion = e.version;
        if (tlcache_on()) t_envs[k4] = TlEnv{e.key, e.objects, &e, e.id, e.version};
        if (c.tEnv) g_envNs.fetch_add(now_ns() - c.tEnv, std::memory_order_relaxed);
        return 1;
    }
    count(why);
    c.key.assign(key, key + words);
    env_objects(g, key, words, frame, c.envObjects);
    c.seg[kSegEnv].clear();
    c.foreign = false;
    return 2;
}
extern "C" unsigned wwhd_j3ddesc_env_define(void) {
    Current& c = t_cur;
    t_capturing = false;
    if (!c.open || !c.env) return 0;
    if (c.foreign) {
        wwhd_j3ddesc_skip(kForeign);
        return 0;
    }
    count(cEnvCaptures);
    Game& g = game();
    std::lock_guard<std::mutex> lk(g.mu);
    GameEnv& e = *c.env;
    e.key = c.key;
    e.objects = c.envObjects;
    e.frame = g_gameFrame.load(std::memory_order_relaxed);
    if (e.rec && e.rec->image == c.seg[kSegEnv]) {
        count(cEnvSame);
    } else {
        if (!e.rec) {
            if (g.nextEnv >= 0x10000) {
                wwhd_j3ddesc_skip(kTableFull);
                return 0;
            }
            e.id = uint16_t(g.nextEnv++);
        } else {
            ++e.version;
        }
        Env* r = new Env;
        r->id = e.id;
        r->version = e.version;
        r->input = c.key[0];
        r->frame = c.key[1];
        r->pass = c.key[2];
        r->view = c.key[3];
        r->projection = c.key[4];
        r->key = c.key;
        r->image = c.seg[kSegEnv];
        r->gameFrame = e.frame;
        r->prev = e.rec;
        e.rec = r;
        g_envs[e.id].store(r, std::memory_order_release);
        if (g_trace) {  // (debug: every env version, its image size and key)
            char sum[600];
            summary(r->image, sum, sizeof sum);
            LOG("[j3d-desc] env %u v%u image:%s", r->id, r->version, sum);
            char line[600];
            int j = std::snprintf(line, sizeof line, "[j3d-desc] env %u v%u: %zu image words, key:", r->id, r->version,
                                  r->image.size());
            for (size_t i = 0; i < r->key.size() && j < int(sizeof line) - 12; ++i)
                j += std::snprintf(line + j, sizeof line - j, " %08X", r->key[i]);
            LOG("%s", line);
        }
        count(cEnvVersions);
        c.flags |= kFlagEnvMade;
    }
    c.envId = e.id;
    c.envVersion = e.version;
    if (tlcache_on()) t_envs[Key4{c.key[0], c.key[1], c.key[2], c.key[3]}] = TlEnv{e.key, e.objects, &e, e.id, e.version};
    if (c.tEnv) g_envNs.fetch_add(now_ns() - c.tEnv, std::memory_order_relaxed);
    return 1;
}
extern "C" void wwhd_j3ddesc_mesh(unsigned prim, unsigned count_, unsigned indexType, unsigned indexAddr,
                                  unsigned baseVertex) {
    Current& c = t_cur;
    if (!c.open) return;
    const MeshKey k{{prim, count_, indexType, indexAddr, baseVertex}};
    if (tlcache_on()) {
        auto tl = t_meshes.find(k);
        if (tl != t_meshes.end()) {
            c.meshId = tl->second;
            count(cTlMeshHit);
            if (baseVertex) count(cBaseVertex);
            return;
        }
    }
    Game& g = game();
    std::lock_guard<std::mutex> lk(g.mu);
    auto [it, fresh] = g.meshes.try_emplace(k, uint16_t(0));
    if (fresh) {
        it->second = uint16_t(g.nextMesh < 0x10000 ? g.nextMesh++ : 0);
        g_meshes[it->second].store(new Mesh{prim, count_, indexType, indexAddr, baseVertex}, std::memory_order_release);
        count(cMeshes);
    }
    c.meshId = it->second;
    if (tlcache_on()) t_meshes[k] = it->second;
    if (baseVertex) count(cBaseVertex);
}
extern "C" void wwhd_j3ddesc_block(unsigned stage, unsigned location, unsigned addr, unsigned size) {
    Current& c = t_cur;
    if (!c.open) return;
    if (c.blockCount >= kMaxBlocks) {
        c.overflow = true;
        return;
    }
    if ((stage & 0x100) && !(c.flags & kFlagExtraMask)) c.flags |= (c.blockCount + 1) << kFlagExtraShift;
    uint32_t* b = c.blocks + c.blockCount++ * kBlockWords;
    b[0] = (stage & 0xFF) << 16 | (location & 0xFFFF);
    b[1] = addr;
    b[2] = size;
}
extern "C" unsigned wwhd_j3ddesc_draw(void) {
    Current& c = t_cur;
    if (!c.open) return 0;
    c.open = false;
    if (c.overflow || !c.meshId) {
        count(cSkip0 + kForeign);
        return 0;
    }
    if (gx2::recording_display_list()) {
        c.flags |= kFlagRecording;
        count(cRecording);
    }
    uint32_t w[kDrawHeaderWords + kMaxBlocks * kBlockWords];
    w[0] = c.descId, w[1] = c.descVersion, w[2] = c.envId, w[3] = c.envVersion, w[4] = c.meshId, w[5] = c.flags;
    w[6] = c.blockCount;
    std::memcpy(w + kDrawHeaderWords, c.blocks, c.blockCount * kBlockWords * 4);
    gx2::emit(gx2::OP_J3D_DESC_DRAW, w, kDrawHeaderWords + c.blockCount * kBlockWords);
    if (sitecap_on() && c.desc && c.env) {
        c.siteCap = true;
        c.site.clear();
        c.capDesc = find_desc(c.descId, c.descVersion);  // (the draw's own versions: another thread may have defined
        c.capEnv = find_env(c.envId, c.envVersion);      // a newer one under the same map key)
        c.capBlocks = c.blockCount;
        c.capFlags = c.flags;
        std::memcpy(c.capBlock, c.blocks, sizeof c.blocks);
        t_capturing = true;
    }
    count(cDescribed);
    if (c.flags & kFlagProgramBind) count(cProgBind);
    count(cDescribedPass0 + std::min<uint32_t>(c.pass, 2));
    count(cBlocks, c.blockCount);
    if (c.t0) {
        g_hookNs.fetch_add(now_ns() - c.t0, std::memory_order_relaxed);
        g_timed.fetch_add(1, std::memory_order_relaxed);
        if (mode() == 1 && !c.siteCap) {
            c.siteTime = true;
            t_capturing = true;
            c.tSite = now_ns();
        }
    }
    if (mode() == 2) {
        // the site makes no GX2 call: what its GX2Set*UniformBlock calls note for the list being recorded
        // (submitted-uniform snapshots, gx2_resources.cpp uniform_block)
        for (uint32_t i = 0; i < c.blockCount; ++i) gx2::note_uniform_block(c.blocks[i * kBlockWords + 1], c.blocks[i * kBlockWords + 2]);
        return 1;
    }
    return 0;  // (mode 1: the site makes its calls and its draw)
}

// ---------------------------------------------------------------- render side (verify)
namespace native_draw::j3ddesc {
bool g_pending = false;
bool g_resolving = false;
bool g_trace = native_draw::switch_value("WWHD_NATIVE_J3D_DESC_UNITS", 0) != 0;
namespace {
// the last writer of each pixel texture unit's word 0: the command count, the last non-register command and its
// first payload word, the last J3D description draw seen
struct Writer {
    uint64_t seq = 0;
    uint32_t lastOp = 0, lastWord = 0, lastDesc = 0, lastDescSeq = 0;
};
Writer g_unitWriter[8];
uint64_t g_seq = 0;
uint32_t g_lastOp = 0, g_lastWord = 0, g_lastDesc = 0;
uint64_t g_lastDescSeq = 0;
}  // namespace
void trace_write(uint32_t first, uint32_t count) {
    ++g_seq;
    for (uint32_t u = 0; u < 8; ++u) {
        const uint32_t r = REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS + u * 7;
        if (r >= first && r < first + count)
            g_unitWriter[u] = {g_seq, g_lastOp, g_lastWord, g_lastDesc, uint32_t(g_seq - g_lastDescSeq)};
    }
}
void trace_op(uint32_t op, const uint32_t* p, uint32_t n) {
    ++g_seq;
    g_lastOp = op;
    g_lastWord = n ? p[0] : 0;
    if (op == gx2::OP_J3D_DESC_DRAW && n) g_lastDesc = p[0], g_lastDescSeq = g_seq;
}
namespace {
struct Pending {
    const Desc* desc = nullptr;
    const Env* env = nullptr;
    const Mesh* mesh = nullptr;
    uint32_t blockCount = 0, blocks[kMaxBlocks * kBlockWords] = {}, flags = 0;
};
Pending g_draw;
uint64_t g_verifyCounter = 0;
uint32_t verify_every() {
    static const uint32_t v = uint32_t(switch_value("WWHD_NATIVE_J3D_DESC_VERIFY", 1));
    return v;
}
using Effect = std::vector<std::pair<uint32_t, uint32_t>>;
// the net effect: the last write of each register, in the order of those last writes
void net(const Effect& all, Effect& out) {
    std::unordered_map<uint32_t, size_t> last;
    for (size_t i = 0; i < all.size(); ++i) last[all[i].first] = i;
    for (size_t i = 0; i < all.size(); ++i)
        if (last[all[i].first] == i && all[i].first < gx2::kNumRegs) out.push_back(all[i]);
}
struct DescEffect {
    Effect effect;                 // program, material, state segments (lists from the define's bytes)
    std::vector<uint8_t> segOf;    // segment of each entry
};
std::unordered_map<const Desc*, DescEffect> g_descEffects;
std::unordered_map<const Env*, Effect> g_envEffects;
const DescEffect& desc_effect(const Desc* d) {
    auto [it, fresh] = g_descEffects.try_emplace(d);
    if (fresh) {
        Effect all;
        std::vector<uint8_t> seg;
        const uint8_t* lists = d->lists.data();
        const uint8_t* end = d->lists.data() + d->lists.size();
        for (uint32_t s = kSegProgram; s <= kSegState; ++s) {
            const size_t before = all.size();
            decode(d->segment[s].data(), uint32_t(d->segment[s].size()), all, 0, &lists, end);
            seg.resize(all.size(), uint8_t(s));
            (void)before;
        }
        std::unordered_map<uint32_t, size_t> last;
        for (size_t i = 0; i < all.size(); ++i) last[all[i].first] = i;
        for (size_t i = 0; i < all.size(); ++i)
            if (last[all[i].first] == i && all[i].first < gx2::kNumRegs) {
                it->second.effect.push_back(all[i]);
                it->second.segOf.push_back(seg[i]);
            }
    }
    return it->second;
}
const Effect& env_effect(const Env* e) {
    auto [it, fresh] = g_envEffects.try_emplace(e);
    if (fresh) {
        Effect all;
        decode(e->image.data(), uint32_t(e->image.size()), all, 0);
        net(all, it->second);
    }
    return it->second;
}
// the ambient register classes (counters cAmbP0.. / cAmbD0..)
uint32_t amb_class(uint32_t r) {
    if (in(r, mmSQ_VTX_ATTRIBUTE_BLOCK_START, 16 * 7)) return 0;
    if (block_reg(r)) return 2;
    if (texture_reg(r) || in(r, 0xA400, 0x100)) return 1;  // (resources, samplers, border colours)
    if (in(r, mmSQ_ALU_CONSTANT0_0, 0x800)) return 3;
    if (in(r, mmSQ_VTX_SEMANTIC_0, 32) || r == mmSQ_VTX_SEMANTIC_CLEAR) return 4;
    return 5;
}
// mismatch lines (capped)
// (capped per kind: the first word of the text)
std::unordered_map<std::string, uint32_t> g_said;
void say(const char* what) {
    uint32_t& n = g_said[std::string(what, strcspn(what, ":"))];
    if (n < 30 && ++n) LOG("[j3d-desc] VERIFY MISMATCH %s%s", what, n == 30 ? " (no more of this kind)" : "");
}
const char* const kSegNames[kSegs] = {"none", "program", "material", "state", "env"};
// registers the pending draw's description writes (desc, env, blocks), for the ambient set
std::unordered_set<uint32_t> g_written;
// resolutions per (desc, env, targets)
struct ResolvedEntry {
    Resolved r;
    bool valid = false;
};
std::unordered_map<uint64_t, ResolvedEntry> g_resolved;
// ambient: the per-pass snapshot (pass instance = input, pass, view, frame, colour/depth targets)
uint64_t g_passKey = 0;
std::unordered_map<uint32_t, uint32_t> g_passSnap;
std::unordered_map<uint32_t, uint64_t> g_ambientHist, g_changedHist, g_descEnvHist;
// ambient values per (desc, env) at its first verified draw
std::unordered_map<uint64_t, std::unordered_map<uint32_t, uint32_t>> g_descEnvAmbient;
uint64_t g_renderFrame = 0;
}  // namespace

void draw(const uint32_t* regs, const uint32_t* p, uint32_t n) {
    (void)regs;
    g_pending = false;
    g_resolving = false;
    count(cDraws);
    if (n < kDrawHeaderWords || n != kDrawHeaderWords + p[6] * kBlockWords || p[6] > kMaxBlocks) {
        count(cUnknown);
        return;
    }
    const Desc* d = find_desc(p[0], p[1]);
    const Env* e = find_env(p[2], p[3]);
    const Mesh* m = find_mesh(p[4]);
    if (!d || !e || !m) {
        count(cUnknown);
        return;
    }
    if (!verify_every() || ++g_verifyCounter % verify_every()) return;
    count(cVerifyDue);
    g_draw.desc = d;
    g_draw.env = e;
    g_draw.mesh = m;
    g_draw.blockCount = p[6];
    g_draw.flags = p[5];
    std::memcpy(g_draw.blocks, p + kDrawHeaderWords, p[6] * kBlockWords * 4);
    g_pending = true;
}
void command(uint32_t op) {
    if (g_pending) {
        static uint32_t said = 0;
        if (said < 8 && ++said) LOG("[j3d-desc] command %u between a description and the site's draw", op);
        count(cInterrupted);
    }
    g_pending = false;
    g_resolving = false;
}
void site_draw(const uint32_t* regs, const uint32_t* p, uint32_t n) {
    g_pending = false;
    count(cSiteDraws);
    const Pending& q = g_draw;
    const Desc* d = q.desc;
    char what[240];
    bool ok = true;
    // mesh: GX2DrawIndexedEx's arguments
    const Mesh* m = q.mesh;
    if (n < 6 || p[0] != m->prim || p[1] != m->count || p[2] != m->indexType || p[3] != m->indexAddr ||
        p[4] != m->baseVertex || p[5] != 1) {
        count(cMeshMismatch);
        std::snprintf(what, sizeof what, "mesh: desc %u v%u pass %u: draw %X %u %u %08X %u %u, mesh %X %u %u %08X %u",
                      d->id, d->version, d->pass, p[0], p[1], p[2], p[3], n > 4 ? p[4] : 0, n > 5 ? p[5] : 0, m->prim,
                      m->count, m->indexType, m->indexAddr, m->baseVertex);
        say(what);
        ok = false;
    }
    // the description's register effect: env, then the desc's segments, then the blocks
    const DescEffect& de = desc_effect(d);
    const Effect& ee = env_effect(q.env);
    g_written.clear();
    for (size_t i = 0; i < de.effect.size(); ++i) {
        const auto [reg, value] = de.effect[i];
        g_written.insert(reg);
        if (regs[reg] != value) {
            count(cImageMismatch);
            std::snprintf(what, sizeof what, "image: desc %u v%u (pass %u, record %08X, frame %08X) segment %s register %04X holds %08X, desc %08X",
                          d->id, d->version, d->pass, d->record, d->frame, kSegNames[de.segOf[i]], reg, regs[reg], value);
            say(what);
            ok = false;
            break;
        }
    }
    for (const auto& [reg, value] : ee) {
        if (!g_written.insert(reg).second) continue;  // (the desc writes it after the env)
        if (regs[reg] != value) {
            count(cEnvMismatch);
            std::snprintf(what, sizeof what, "env: env %u v%u (pass %u, view %u, input %08X) desc %u register %04X holds %08X, env %08X",
                          q.env->id, q.env->version, q.env->pass, q.env->view, q.env->input, d->id, reg, regs[reg], value);
            say(what);
            ok = false;
            break;
        }
    }
    // blocks: GX2Set*UniformBlock's 7 words (the last bind of a location wins)
    for (uint32_t i = 0; i < q.blockCount; ++i) {
        const uint32_t* b = q.blocks + i * kBlockWords;
        const uint32_t stage = b[0] >> 16, loc = b[0] & 0xFFFF;
        const uint32_t base = stage == 0 ? mmSQ_VTX_UNIFORM_BLOCK_START : stage == 1 ? mmSQ_PS_UNIFORM_BLOCK_START
                                                                                  : mmSQ_GS_UNIFORM_BLOCK_START;
        bool later = false;
        for (uint32_t j = i + 1; j < q.blockCount; ++j) later |= q.blocks[j * kBlockWords] == b[0];
        for (uint32_t w = 0; w < 7; ++w) g_written.insert(base + loc * 7 + w);
        if (later) continue;
        const uint32_t want[7] = {b[1], b[2] - 1, 0, 1, 0, 0, 0xC0000000u};
        const uint32_t extraFirst = (q.flags & kFlagExtraMask) >> kFlagExtraShift;  // (index + 1; 0: none)
        if (extraFirst && i + 1 >= extraFirst && loc < 16 && regs[base + loc * 7] != b[1] &&
            !std::memcmp(regs + base + loc * 7 + 1, want + 1, sizeof want - 4)) {
            // the pass's extra block: another thread flipped its ring index between the description's read and the
            // site's (lead ruling 2026-11-14: the game's own race, counted, not a mismatch)
            count(cExtraRace);
            continue;
        }
        if (loc >= 16 || std::memcmp(regs + base + loc * 7, want, sizeof want)) {
            count(cBlockMismatch);
            std::snprintf(what, sizeof what, "block: %u of %u, desc %u (pass %u, record %08X) stage %u location %u: register %08X %08X, desc %08X %08X",
                          i, q.blockCount, d->id, d->pass, d->record, stage, loc, loc < 16 ? regs[base + loc * 7] : 0,
                          loc < 16 ? regs[base + loc * 7 + 1] : 0, b[1], b[2] - 1);
            say(what);
            if (g_said["block"] <= 30) {  // (every block of the draw, with the register file's words)
                char all[600];
                int j = std::snprintf(all, sizeof all, "[j3d-desc]   blocks of desc %u env %u:", d->id, q.env->id);
                for (uint32_t k = 0; k < q.blockCount && j < int(sizeof all) - 60; ++k) {
                    const uint32_t* x = q.blocks + k * kBlockWords;
                    const uint32_t st = x[0] >> 16, lc = x[0] & 0xFFFF;
                    const uint32_t bs = st == 0 ? mmSQ_VTX_UNIFORM_BLOCK_START : st == 1 ? mmSQ_PS_UNIFORM_BLOCK_START
                                                                                      : mmSQ_GS_UNIFORM_BLOCK_START;
                    j += std::snprintf(all + j, sizeof all - j, " [%u] s%u l%u %08X+%X (reg %08X)", k, st, lc, x[1], x[2],
                                       lc < 16 ? regs[bs + lc * 7] : 0);
                }
                LOG("%s", all);
            }
            ok = false;
        }
    }
    // the program's lists: the bytes the define decoded, against guest memory now (a refresh rewrites in place)
    {
        const uint8_t* lists = d->lists.data();
        size_t off = 0;
        const auto& s = d->segment[kSegProgram];
        for (size_t i = 0; i < s.size();) {
            const uint32_t op = s[i] & 0xFF, k = s[i] >> 8;
            if (op == gx2::OP_CALL && k == 2 && off + s[i + 2] <= d->lists.size() && readable(s[i + 1], s[i + 2])) {
                if (std::memcmp(mem::ptr(s[i + 1]), lists + off, s[i + 2])) {
                    count(cListChanged);
                    ok = false;
                }
                off += s[i + 2];
            }
            i += 1 + k;
        }
    }
    if (ok) count(cVerifiedOk);
    // WWHD_NATIVE_J3D_DESC_UNITS=COUNT[:N]: the first N site draws with mesh index count COUNT: their pixel texture
    // units 0-7 (debug: the D1 render side's comparison)
    {
        static const int want = native_draw::switch_value("WWHD_NATIVE_J3D_DESC_UNITS", 0);
        static int left = [] {
            const char* e = std::getenv("WWHD_NATIVE_J3D_DESC_UNITS");
            const char* c = e ? std::strchr(e, ':') : nullptr;
            return c ? std::atoi(c + 1) : 8;
        }();
        if (want && int(m->count) == want && left > 0) {
            --left;
            LOG("[j3d-desc] units: desc %u v%u env %u v%u pass %u record %08X frame %08X mesh %u", d->id, d->version,
                q.env->id, q.env->version, d->pass, d->record, d->frame, m->count);
            for (uint32_t u = 0; u < 8; ++u) {
                const uint32_t r0 = REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS + u * 7;
                const Writer& w = g_unitWriter[u];
                LOG("[j3d-desc]   ps unit %u: %08X %08X %08X%s; written %llu commands ago, after op %u (%08X), last desc %u (%u commands before that write)",
                    u, regs[r0], regs[r0 + 1], regs[r0 + 2], g_written.count(r0) ? " (description)" : "",
                    (unsigned long long)(g_seq - w.seq), w.lastOp, w.lastWord, w.lastDesc, w.lastDescSeq);
            }
        }
    }
    g_resolving = true;  // (the Vulkan draw reports what it resolved: resolved())
}
void resolved(const uint32_t* regs, const Resolved& r) {
    if (!g_resolving) return;
    g_resolving = false;
    count(cResolvedSeen);
    const Desc* d = g_draw.desc;
    const Env* e = g_draw.env;
    // ambient: registers the draw read that the description does not write. Decision 1 as refined (user, 2026-11-14):
    // no per-pass snapshot; words past the program's own counts (vertex semantics, VS output ids, PS input controls the
    // program's bind did not write) are ignored by rule; uniform-block words of locations the program reads but J3D does
    // not bind are taken live from the register file; any other ambient word that differs from the value at the
    // (desc, env)'s first verified draw sends the draw to the translated path in D1 ("refused"), and the per-draw
    // resolution compare below stays the proof that nothing ignored reaches the translation.
    uint32_t ambient = 0;
    bool ignored = false, live = false, refused = false;
    auto& snap = g_descEnvAmbient[uint64_t(uintptr_t(d)) * 0x9E3779B97F4A7C15ull ^ uint64_t(uintptr_t(e))];
    for (uint32_t reg : r.reads) {
        if (reg >= gx2::kNumRegs || g_written.count(reg) || gx2::state_group(reg) == gx2::kSgTargets) continue;
        ++ambient;
        ++g_ambientHist[reg];
        if (in(reg, mmSQ_VTX_SEMANTIC_0, 32) || reg == mmSQ_VTX_SEMANTIC_CLEAR || in(reg, mmSPI_VS_OUT_ID_0, 10) ||
            in(reg, mmSPI_PS_INPUT_CNTL_0, 32)) {
            ignored = true;
            continue;
        }
        if (block_reg(reg)) {
            live = true;
            continue;
        }
        // CB_TARGET_MASK: only the nibbles of colour targets bound in this pass are observable (lead decision
        // 2026-11-15, an application of decision 1 as refined); the others are leftovers
        uint32_t observed = 0xFFFFFFFFu;
        if (reg == REGADDR::CB_TARGET_MASK) {
            observed = 0;
            for (int i = 0; i < 8; ++i)
                if (r.colors[i]) observed |= 0xFu << (4 * i);
        }
        auto [it, fresh] = snap.try_emplace(reg, regs[reg]);
        if (!fresh && it->second != regs[reg] && !((it->second ^ regs[reg]) & observed)) {
            ignored = true;
            it->second = regs[reg];
            continue;
        }
        if (!fresh && it->second != regs[reg]) {
            ++g_descEnvHist[reg];
            if (!refused) {
                static uint32_t said = 0;
                if (said < 20 && ++said)
                    LOG("[j3d-desc] ambient %04X differs for desc %u env %u: %08X -> %08X (refused)", reg, d->id, e->id,
                        it->second, regs[reg]);
            }
            refused = true;
            it->second = regs[reg];
        }
    }
    count(cAmbientWords, ambient);
    if (ambient) count(cAmbientDraws);
    if (ignored) count(cAmbIgnored);
    if (live) count(cAmbLive);
    if (refused) count(cAmbRefused);
    // resolution per (desc, env, targets)
    uint64_t key = (uint64_t(uintptr_t(d)) * 0x9E3779B97F4A7C15ull) ^ uint64_t(uintptr_t(e));
    for (const void* c : r.colors) key = (key ^ uint64_t(uintptr_t(c))) * 0x100000001B3ull;
    key = (key ^ uint64_t(uintptr_t(r.depth))) * 0x100000001B3ull;
    ResolvedEntry& x = g_resolved[key];
    if (!x.valid) {
        x.r = r;
        x.r.reads.clear();
        x.valid = true;
        count(cResFirst);
        return;
    }
    const Resolved& a = x.r;
    uint32_t why = cResSame;
    if (a.vsKey != r.vsKey || a.psKey != r.psKey) why = cResShader;
    else if (a.fsKey != r.fsKey) why = cResFetch;
    else if (a.pipeline != r.pipeline) why = cResPipeline;
    else if (std::memcmp(a.colors, r.colors, sizeof a.colors) || a.depth != r.depth) why = cResTargets;
    char tex[160] = "";
    if (why == cResSame) {
        bool surfaces = false, other = false;
        for (int s = 0; s < 2; ++s) {
            if (a.textureCount[s] != r.textureCount[s]) surfaces = true;
            for (uint32_t i = 0; i < a.textureCount[s] && i < 18 && i < r.textureCount[s]; ++i) {
                const auto &u = a.textures[s][i], &v = r.textures[s][i];
                if (u.unit != v.unit || u.surface != v.surface) surfaces = true;
                else if (u.view != v.view || u.sampler != v.sampler) other = true;
                if ((u.unit != v.unit || u.surface != v.surface || u.view != v.view || u.sampler != v.sampler) && !tex[0])
                    std::snprintf(tex, sizeof tex, " (stage %d unit %u: surface %s, view %s, sampler %s)", s, v.unit,
                                  u.surface != v.surface ? "differs" : "same", u.view != v.view ? "differs" : "same",
                                  u.sampler != v.sampler ? "differs" : "same");
            }
        }
        if (surfaces) why = cResTextures;
        else if (other) why = cResSurfaceOnly;
    }
    count(why);
    if (why != cResSame) {
        char what[200];
        std::snprintf(what, sizeof what, "resolved: desc %u v%u env %u v%u (pass %u, record %08X) %s%s", d->id,
                      d->version, e->id, e->version, d->pass, d->record, count_name(why), tex);
        say(what);
        x.r = r;
        x.r.reads.clear();
    }
}
void read_regs(const gfxvk::vk::Shader* vs, const gfxvk::vk::Shader* ps, const LatteFetchShader* fs,
               std::vector<uint32_t>& list) {
#ifdef WWHD_HAS_VULKAN
    static std::vector<uint32_t> fixed;
    if (fixed.empty())
        for (uint32_t reg = 0; reg < gx2::kNumRegs; ++reg) {
            const uint8_t g = gx2::state_group(reg);
            if (reg >= mmSQ_VTX_ATTRIBUTE_BLOCK_START && reg < mmSQ_VTX_ATTRIBUTE_BLOCK_START + 7 * 16) continue;
            if (g == gx2::kSgShader || g == gx2::kSgFetch || g == gx2::kSgTargets || g == gx2::kSgOutput ||
                g == gx2::kSgOutputTargets || g == gx2::kSgOutputShader || g == gx2::kSgDynamic ||
                g == gx2::kSgStencilRefMask)
                fixed.push_back(reg);
        }
    list = fixed;
    for (const gfxvk::vk::Shader* sh : {vs, ps}) {
        if (!sh || !sh->dec) continue;
        const uint32_t texbase = sh->vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
        for (int i = 0; i < sh->dec->textureUnitListCount; ++i) {
            const uint32_t unit = sh->dec->textureUnitList[i];
            if (unit >= LATTE_NUM_MAX_TEX_UNITS) continue;
            for (uint32_t w = 0; w < 7; ++w) list.push_back(texbase + unit * 7 + w);
            const uint32_t si = sh->dec->textureUnitSamplerAssignment[unit];
            if (si < 18)
                for (uint32_t w = 0; w < 3; ++w)
                    list.push_back(REGADDR::SQ_TEX_SAMPLER_WORD0_0 + ((sh->vertex ? 18 : 0) + si) * 3 + w);
        }
        const uint32_t blockBase = sh->vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START;
        const uint32_t aluBase = mmSQ_ALU_CONSTANT0_0 + (sh->vertex ? 0x400 : 0);
        for (const auto& g : sh->dec->list_remappedUniformEntries_bufferGroups)
            for (uint32_t w = 0; w < 2; ++w) list.push_back(blockBase + g.kcacheBankIdOffset / 4 + w);
        for (const auto& q : sh->dec->list_quickBufferList)
            for (uint32_t w = 0; w < 2; ++w) list.push_back(blockBase + q.index * 7 + w);
        if (sh->uniforms.offset_remapped >= 0)
            for (const auto& e : sh->dec->list_remappedUniformEntries_register)
                for (uint32_t w = 0; w < 4; ++w)
                    if (e.indexOffset / 4 + w < 0x400) list.push_back(aluBase + e.indexOffset / 4 + w);
        if (sh->uniforms.offset_uniformRegister >= 0)
            for (uint32_t w = 0; w < uint32_t(sh->uniforms.count_uniformRegister) * 4 && w < 0x400; ++w)
                list.push_back(aluBase + w);
    }
    if (fs)
        for (const auto& g : fs->bufferGroups)
            for (uint32_t w = 0; w < 3; ++w) list.push_back(mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + w);
#else
    (void)vs, (void)ps, (void)fs;
    list.clear();
#endif
}
void frame_end() {
    ++g_renderFrame;
    g_pending = false;
    g_resolving = false;
    static uint64_t frames = 0;
    if (++frames % 300) return;
    if (!mode()) return;
    char buf[3000];
    int k = std::snprintf(buf, sizeof buf, "[j3d-desc] last 300 frames, per frame:");
    for (uint32_t c = 0; c < cCounts; ++c) {
        const uint64_t v = g_count[c].exchange(0, std::memory_order_relaxed);
        if (v && k < int(sizeof buf) - 90) k += std::snprintf(buf + k, sizeof buf - k, " %s %.2f,", count_name(c), v / 300.0);
    }
    const uint64_t timed = g_timed.exchange(0);
    const uint64_t keyNs = g_keyNs.exchange(0), hookNs = g_hookNs.exchange(0), defNs = g_defineNs.exchange(0);
    const uint64_t contentNs = g_contentNs.exchange(0), envNs = g_envNs.exchange(0);
    const uint64_t siteNs = g_siteNs.exchange(0), siteTimed = g_siteTimed.exchange(0);
    if (timed && k < int(sizeof buf) - 300)
        k += std::snprintf(buf + k, sizeof buf - k,
                           " game thread (Mac, indication): key compare %.0f ns/draw %.3f ms/frame, whole hook %.0f ns/draw %.3f ms/frame, defines %.3f ms/frame, content checks %.3f ms/frame, env %.3f ms/frame,",
                           double(keyNs) / timed, keyNs / 300.0 / 1e6, double(hookNs) / timed, hookNs / 300.0 / 1e6,
                           defNs / 300.0 / 1e6, contentNs / 300.0 / 1e6, envNs / 300.0 / 1e6);
    if (siteTimed && k < int(sizeof buf) - 200)
        k += std::snprintf(buf + k, sizeof buf - k, " the site's own calls (hook to draw) %.0f ns/draw %.3f ms/frame,",
                           double(siteNs) / siteTimed, siteNs / 300.0 / 1e6);
    LOG("%s descs seen %zu, envs seen %zu", buf, g_descEffects.size(), g_envEffects.size());
    if (sitecap_on()) {
        std::lock_guard<std::mutex> lk(g_siteMu);
        {
            int j = std::snprintf(buf, sizeof buf, "[j3d-desc] site capture: other commands between a thread's J3D draws (op: per frame):");
            for (uint32_t o = 0; o < 64; ++o)
                if (g_preOps[o]) j += std::snprintf(buf + j, sizeof buf - j, " %u %.2f", o, g_preOps[o] / 300.0), g_preOps[o] = 0;
            LOG("%s", buf);
        }
        for (auto* h : {&g_siteExtra, &g_siteDiff, &g_pre}) {
            std::vector<std::pair<uint64_t, uint32_t>> v;
            for (const auto& [reg, n] : *h) v.push_back({n, reg});
            std::sort(v.rbegin(), v.rend());
            int j = std::snprintf(buf, sizeof buf, "[j3d-desc] site capture, %llu draws: %s (register: draws/frame):",
                                  (unsigned long long)g_siteDraws,
                                  h == &g_siteExtra ? "site writes outside the description"
                                  : h == &g_siteDiff ? "site writes another value"
                                                     : "registers written between a thread's J3D draws");
            for (size_t i = 0; i < v.size() && i < 40 && j < int(sizeof buf) - 40; ++i)
                j += std::snprintf(buf + j, sizeof buf - j, " %04X %.2f", v[i].second, v[i].first / 300.0);
            LOG("%s", buf);
            h->clear();
        }
        g_siteDraws = 0;
    }
    // the ambient registers (most frequent) and those that changed mid-pass
    for (auto* h : {&g_ambientHist, &g_descEnvHist}) {
        if (h->empty()) continue;
        std::vector<std::pair<uint64_t, uint32_t>> v;
        for (const auto& [reg, n] : *h) v.push_back({n, reg});
        std::sort(v.rbegin(), v.rend());
        int j = std::snprintf(buf, sizeof buf, "[j3d-desc] %s (register: draws/frame):",
                              h == &g_ambientHist ? "ambient" : "ambient refused (differs for the same desc and env)");
        for (size_t i = 0; i < v.size() && i < 40 && j < int(sizeof buf) - 40; ++i)
            j += std::snprintf(buf + j, sizeof buf - j, " %04X %.2f", v[i].second, v[i].first / 300.0);
        LOG("%s", buf);
        h->clear();
    }
}
void reset() {
    g_descEffects.clear();
    g_envEffects.clear();
    g_resolved.clear();
    g_pending = false;
    g_resolving = false;
    g_passKey = 0;
    g_passSnap.clear();
    g_descEnvAmbient.clear();
    d1::reset();  // (D1 described draws: their states hold shaders and pipelines)
}

}  // namespace native_draw::j3ddesc
