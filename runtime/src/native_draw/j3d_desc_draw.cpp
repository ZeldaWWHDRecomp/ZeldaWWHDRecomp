// Native J3D material descriptions, stage D1, render side (gx2/native_j3d_desc_draw.h). Decomp build, fast mode,
// WWHD_NATIVE_DRAW=ON only.
#if defined(WWHD_DECOMP_FAST)
#include "native_draw/pardecide_ops.h"
#endif
#include "../../third_party/cemu/cemu_shim.h"  // (as the gx2 sources get it: -include; for gx2_regs.h)
#include "gx2/native_j3d_desc_draw.h"
#include "gx2/native_draw.h"
#include "gx2/native_pass.h"
#include "gx2/gx2_cmd.h"
#include "gx2/gx2_regs.h"
#include "runtime.h"
#include "framegen.h"
#include "cloud_proto.h"
#include "aspect.h"
#include "gfx/renderer.h"
#include "gfx/stereo.h"
#ifdef WWHD_HAS_VULKAN
#include "gfx/vulkan/api.h"
namespace gfxvk {
bool gamepad_backdrop_capture_active();
}
#endif
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

using namespace Latte;

namespace gx2 {
void execute(const uint32_t* words, uint32_t count);
}

namespace native_draw::j3ddesc::d1 {

bool on() {
    static const bool v = mode() == 2 && std::getenv("WWHD_NO_RENDER_THREAD") == nullptr;
    return v;
}
bool debug_on() {
    static const bool v = switch_value("WWHD_NATIVE_J3D_DESC_D1DEBUG", 0) != 0;
    return v;
}
uint32_t verify_every() {
    static const uint32_t v = uint32_t(switch_value("WWHD_NATIVE_J3D_DESC_VERIFY", 0));
    return v;
}
bool timing_on() {
    static const bool v = switch_value("WWHD_NATIVE_J3D_DESC_D1TIMING", 0) != 0;
    return v;
}
inline uint64_t tnow() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct Timed {  // (WWHD_NATIVE_J3D_DESC_D1TIMING=1) ns into a counter
    Count c;
    uint64_t t0;
    explicit Timed(Count k) : c(k), t0(timing_on() ? tnow() : 0) {}
    ~Timed() { if (t0) count(c, tnow() - t0); }
};
bool Item::value(uint32_t reg, uint32_t& v) const {
    auto it = std::lower_bound(regs.begin(), regs.end(), reg);
    if (it == regs.end() || *it != reg) return false;
    v = values[size_t(it - regs.begin())];
    return true;
}

namespace {
uint32_t* g_regs = nullptr;
Apply g_apply = nullptr;
uint64_t g_counts[cCounts] = {};
uint64_t g_frames = 0;
const char* const kCountNames[cCounts] = {
    "draws", "replayed", "full", "verified", "MISMATCH", "items", "items refused", "states", "states refused", "runs",
    "settles", "settled words", "class checks", "class words", "J3D-written checks", "LOST", "gated",
    "no state", "no state for the pass", "state refused", "item refused", "surfaces", "binds", "ambient",
    "ambient (J3D-written)", "texture", "index", "uniform offset", "draw us", "settle us", "invalidates", "live vertex buffers", "J3D-written words looked up", "invalidate us", "replay checks us", "replay textures us", "replay indices us", "replay blocks/sets us", "replay record us", "replay after us", "vk dispatch us", "pass begun", "pass begin refused (clouds)", "pass begin refused (no targets)", "pass begin refused (no state for the targets)", "pass begin refused (subset)", "fallback full draw us", "fallback state making us", "make memo hits", "make memo classes kept", "support built", "support reused"};
// reasons (debug): first characters of the why strings, counted by text
std::unordered_map<std::string, uint64_t> g_why;

// ---------------------------------------------------------------- the J3D-written set W
uint64_t g_w[0x10000 / 64];
}  // namespace
uint64_t g_wGen = 1;
bool written(uint32_t reg) { return reg < 0x10000 && (g_w[reg >> 6] >> (reg & 63) & 1); }
void add_written(const uint32_t* regs, size_t n) {
    bool grew = false;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t r = regs[i];
        if (r >= 0x10000) continue;
        const uint64_t bit = uint64_t(1) << (r & 63);
        if (!(g_w[r >> 6] & bit)) g_w[r >> 6] |= bit, grew = true;
    }
    if (grew) ++g_wGen;
}
void add_written_block(uint32_t first) {
    uint32_t r[7];
    for (uint32_t w = 0; w < 7; ++w) r[w] = first + w;
    add_written(r, 7);
}
void count(Count c, uint64_t n) { g_counts[c] += n; }
void note(const char* why) {
    if (debug_on() && why) ++g_why[why];
}
// (WWHD_NATIVE_J3D_DESC_D1DEBUG=2) a fallback reason per site: desc id, pass, env id
void note_site(const char* why, const Item& it) {
    static const bool v = switch_value("WWHD_NATIVE_J3D_DESC_D1DEBUG", 0) >= 2;
    if (!v || !why) return;
    char k[160];
    std::snprintf(k, sizeof k, "[desc %u pass %u env %u] %s", it.desc->id, it.desc->pass, it.env->id, why);
    ++g_why[k];
}

// ---------------------------------------------------------------- items
namespace {
// the net register writes of a command list (gx2::emit encoding), display lists from the define's bytes (consumed in
// order, as the D0 builder stores them): false if anything but register writes and list calls is in it, or a list's
// bytes are missing (decoding guest memory now could see a list rewritten since the define)
bool decode(const uint32_t* w, uint32_t n, std::vector<std::pair<uint32_t, uint32_t>>& out, int depth,
            const uint8_t** lists, const uint8_t* listsEnd, const char*& why, std::vector<uint32_t>& inv) {
    uint32_t i = 0;
    while (i < n) {
        const uint32_t op = w[i] & 0xFF, m = w[i] >> 8;
        if (i + 1 + m > n) return why = "truncated command", false;
        if (op == gx2::OP_SET_REGS && m >= 1) {
            for (uint32_t k = 1; k < m; ++k) out.push_back({w[i + 1] + k - 1, w[i + 1 + k]});
        } else if (op == gx2::OP_CALL && m == 2 && depth < 4) {
            if (!lists || !*lists || *lists + w[i + 2] > listsEnd) return why = "list bytes", false;
            std::vector<uint32_t> copy(w[i + 2] / 4);
            std::memcpy(copy.data(), *lists, copy.size() * 4);
            *lists += w[i + 2];
            const uint8_t* none = nullptr;  // (a list called from a list: its bytes were not kept)
            if (!decode(copy.data(), uint32_t(copy.size()), out, depth + 1, &none, nullptr, why, inv)) return false;
        } else if (op == gx2::OP_INVALIDATE && m == 3) {
            inv.insert(inv.end(), {w[i + 1], w[i + 2], w[i + 3]});
        } else if (op != gx2::OP_NOP) {
            static const char* names[256] = {};
            static char text[256][40];
            if (!names[op]) std::snprintf(text[op], sizeof text[op], "not a register write (op %02X)", op), names[op] = text[op];
            return why = names[op], false;
        }
        i += 1 + m;
    }
    return true;
}
std::unique_ptr<Item> make_item(const Desc* d, const Env* e) {
    auto it = std::make_unique<Item>();
    it->desc = d;
    it->env = e;
    std::vector<std::pair<uint32_t, uint32_t>> all;
    const char* why = nullptr;
    bool ok = decode(e->image.data(), uint32_t(e->image.size()), all, 0, nullptr, nullptr, why, it->invalidates);
    const uint8_t* lists = d->lists.data();
    const uint8_t* end = lists + d->lists.size();
    for (uint32_t s = kSegProgram; ok && s <= kSegState; ++s)
        ok = decode(d->segment[s].data(), uint32_t(d->segment[s].size()), all, 0, &lists, end, why,
                    s == kSegProgram ? it->programInvalidates : it->invalidates);
    if (ok && lists != end) ok = false, why = "list bytes left over";
    if (!ok) {
        it->refused = why;
        count(cItemsRefused);
        return it;
    }
    // net: the last write of each register
    std::unordered_map<uint32_t, uint32_t> last;
    for (const auto& [r, v] : all)
        if (r < gx2::kNumRegs) last[r] = v;
    std::vector<std::pair<uint32_t, uint32_t>> net(last.begin(), last.end());
    std::sort(net.begin(), net.end());
    for (const auto& [r, v] : net) it->regs.push_back(r), it->values.push_back(v);
    for (size_t i = 0; i < net.size();) {
        const uint32_t first = net[i].first;
        uint32_t k = 0;
        while (i < net.size() && net[i].first == first + k && k < 64) ++k, ++i;
        it->runs.push_back(first);
        it->runs.push_back(k);
    }
    add_written(it->regs.data(), it->regs.size());
    count(cItems);
    return it;
}
// per desc id: the version last seen and its items (one per env version drawn with it)
struct DescSlot {
    uint32_t version = ~0u;
    const Desc* desc = nullptr;
    std::vector<std::unique_ptr<Item>>* items = nullptr;
};
std::vector<DescSlot> g_descSlots;
struct EnvSlot {
    uint32_t version = ~0u;
    const Env* env = nullptr;
};
std::vector<EnvSlot> g_envSlots;
std::vector<const Mesh*> g_meshSlots;
// items of every desc version (records live forever: their pointers are stable keys)
std::unordered_map<const Desc*, std::vector<std::unique_ptr<Item>>> g_items;

Item* item_of(uint32_t descId, uint32_t descVer, uint32_t envId, uint32_t envVer) {
    if (descId >= g_descSlots.size()) g_descSlots.resize(std::max<size_t>(descId + 1, g_descSlots.size() * 2));
    DescSlot& ds = g_descSlots[descId];
    if (ds.version != descVer || !ds.desc) {
        ds.desc = find_desc(descId, descVer);
        if (!ds.desc) return nullptr;
        ds.version = descVer;
        ds.items = &g_items[ds.desc];
    }
    if (envId >= g_envSlots.size()) g_envSlots.resize(std::max<size_t>(envId + 1, g_envSlots.size() * 2));
    EnvSlot& es = g_envSlots[envId];
    if (es.version != envVer || !es.env) {
        es.env = find_env(envId, envVer);
        if (!es.env) return nullptr;
        es.version = envVer;
    }
    for (auto& x : *ds.items)
        if (x->env == es.env) return x.get();
    ds.items->push_back(make_item(ds.desc, es.env));
    note_site("new item", *ds.items->back());
    return ds.items->back().get();
}
const Mesh* mesh_of(uint32_t id) {
    if (id >= g_meshSlots.size()) g_meshSlots.resize(std::max<size_t>(id + 1, g_meshSlots.size() * 2));
    const Mesh*& m = g_meshSlots[id];
    if (!m) m = find_mesh(id);
    return m;
}

// ---------------------------------------------------------------- the run
struct Entry {
    Item* item;
    uint32_t blockCount;
    Block blocks[kMaxBlocks];
};
std::vector<Entry> g_run;
uint64_t g_settleStamp = 0;
uint64_t g_seen[0x10000 / 64];
uint32_t g_vals[0x10000];
std::vector<uint32_t> g_list;
bool runcheck_on() {
    static const bool v = switch_value("WWHD_NATIVE_J3D_DESC_RUNCHECK", 0) != 0;
    return v;
}
std::vector<uint32_t> g_check;  // (RUNCHECK) the register file with the run applied one draw at a time
uint32_t g_checkSaid = 0;
}  // namespace

uint64_t g_runSerial = 1;
uint32_t g_runCount = 0;

namespace {
uint64_t g_v[0x10000 / 64];  // V
uint64_t g_vGen = 1;
uint64_t g_runId = 1;        // the open run (a new one at the first push after a settle)
uint64_t g_rstamp[0x10000];  // per watched register: the run that wrote it last, and the value
uint32_t g_rval[0x10000];
inline bool watched(uint32_t r) { return g_v[r >> 6] >> (r & 63) & 1; }
}  // namespace
void push_run(const In& in) {
    if (g_run.empty()) count(cRuns), ++g_runId;
    // the watched registers' run values (item effect, then the blocks)
    Item& it = *in.item;
    if (it.vGen != g_vGen) {
        it.vGen = g_vGen;
        it.vregs.clear(), it.vvals.clear();
        for (size_t k = 0; k < it.regs.size(); ++k)
            if (watched(it.regs[k])) it.vregs.push_back(it.regs[k]), it.vvals.push_back(it.values[k]);
    }
    for (size_t k = 0; k < it.vregs.size(); ++k) g_rstamp[it.vregs[k]] = g_runId, g_rval[it.vregs[k]] = it.vvals[k];
    for (uint32_t b = 0; b < in.blockCount; ++b) {
        const uint32_t first = in.blocks[b].reg;
        if (!(watched(first) | watched(first + 1) | watched(first + 2) | watched(first + 3) | watched(first + 4) |
              watched(first + 5) | watched(first + 6)))
            continue;
        uint32_t w[7];
        block_words(in.blocks[b], w);
        for (uint32_t k = 0; k < 7; ++k)
            if (watched(first + k)) g_rstamp[first + k] = g_runId, g_rval[first + k] = w[k];
    }
    Entry& e = g_run.emplace_back();
    e.item = in.item;
    e.blockCount = in.blockCount;
    std::copy_n(in.blocks, in.blockCount, e.blocks);
    g_runCount = uint32_t(g_run.size());
}

void add_watched(const uint32_t* regs, size_t n) {
    bool grew = false;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t r = regs[i];
        if (r >= 0x10000 || watched(r)) continue;
        g_v[r >> 6] |= uint64_t(1) << (r & 63);
        grew = true;
    }
    if (!grew) return;
    ++g_vGen;
    settle_run();  // (the open run did not keep the new ones: the register file holds them once settled)
}
bool effective(uint32_t reg, uint32_t& value) {
    value = !g_run.empty() && g_rstamp[reg] == g_runId ? g_rval[reg] : g_regs[reg];
    return true;
}

void settle_run() {
    if (g_run.empty()) return;
    Timed timed(tSettleNs);
    const bool check = runcheck_on();
    if (check) {  // the sequential order, on a copy
        g_check.assign(g_regs, g_regs + gx2::kNumRegs);
        for (const Entry& e : g_run) {
            for (size_t i = 0; i < e.item->regs.size(); ++i) g_check[e.item->regs[i]] = e.item->values[i];
            for (uint32_t b = 0; b < e.blockCount; ++b) {
                uint32_t w[7];
                block_words(e.blocks[b], w);
                for (uint32_t k = 0; k < 7; ++k) g_check[e.blocks[b].reg + k] = w[k];
            }
        }
    }
    ++g_settleStamp;
    count(cSettles);
    g_list.clear();
    auto take = [&](uint32_t r, uint32_t v) {
        uint64_t& bits = g_seen[r >> 6];
        const uint64_t bit = uint64_t(1) << (r & 63);
        if (bits & bit) return;
        bits |= bit;
        g_vals[r] = v;
        g_list.push_back(r);
    };
    // backwards: the first value seen of a register is its last write; an item's effect is taken once (a later draw of
    // the same item wrote the same registers after it)
    for (size_t i = g_run.size(); i-- > 0;) {
        const Entry& e = g_run[i];
        for (uint32_t b = e.blockCount; b-- > 0;) {
            uint32_t w[7];
            block_words(e.blocks[b], w);
            for (uint32_t k = 0; k < 7; ++k) take(e.blocks[b].reg + k, w[k]);
        }
        Item& it = *e.item;
        if (it.settled == g_settleStamp) continue;
        it.settled = g_settleStamp;
        for (size_t k = 0; k < it.regs.size(); ++k) take(it.regs[k], it.values[k]);
    }
    g_run.clear();
    g_runCount = 0;
    std::sort(g_list.begin(), g_list.end());
    count(cSettleWords, g_list.size());
    uint32_t values[64];
    for (size_t i = 0; i < g_list.size();) {
        const uint32_t first = g_list[i];
        uint32_t n = 0;
        while (i < g_list.size() && g_list[i] == first + n && n < 64) {
            values[n] = g_vals[first + n];
            g_seen[(first + n) >> 6] &= ~(uint64_t(1) << ((first + n) & 63));
            ++n, ++i;
        }
        g_apply(first, values, n);
    }
    if (check)
        for (uint32_t r = 0; r < gx2::kNumRegs; ++r)
            if (g_regs[r] != g_check[r] && r != REGADDR::VGT_PRIMITIVE_TYPE) {
                count(cMismatch);
                if (g_checkSaid < 20 && ++g_checkSaid)
                    LOG("[j3d-d1] RUNCHECK MISMATCH register %04X: settled %08X, sequential %08X", r, g_regs[r], g_check[r]);
                break;
            }
}

void apply_effects(const In& in) {
    settle_run();
    const Item& it = *in.item;
    const uint32_t* v = it.values.data();
    for (size_t k = 0; k < it.runs.size(); k += 2) {
        g_apply(it.runs[k], v, it.runs[k + 1]);
        v += it.runs[k + 1];
    }
    for (uint32_t b = 0; b < in.blockCount; ++b) {
        uint32_t w[7];
        block_words(in.blocks[b], w);
        g_apply(in.blocks[b].reg, w, 7);
    }
}

namespace {
// the translated path without a capture: the effects as the site's calls make them, then the draw
void full(const In& in) {
    if (in.item->refused) {  // (commands as they are: the env image, the segments with their list calls, the blocks)
        settle_run();
        gx2::execute(in.item->env->image.data(), uint32_t(in.item->env->image.size()));
        for (uint32_t s = kSegProgram; s <= kSegState; ++s)
            gx2::execute(in.item->desc->segment[s].data(), uint32_t(in.item->desc->segment[s].size()));
        for (uint32_t b = 0; b < in.blockCount; ++b) {
            uint32_t w[7];
            block_words(in.blocks[b], w);
            g_apply(in.blocks[b].reg, w, 7);
        }
        ++g_runSerial;  // (its writes are not in W: every ambient class is checked again)
    } else {
        apply_effects(in);
    }
    const Mesh& m = *in.mesh;
    render::draw(in.regs, m.prim, m.count, m.indexType, m.indexAddr, m.baseVertex, 1);
    count(cFull);
}
}  // namespace

void draw(uint32_t* regs, Apply apply, const uint32_t* p, uint32_t n) {
    Timed timed(tDrawNs);
    g_regs = regs;
    g_apply = apply;
    count(cDraws);
    ++native_draw::g_epoch;  // (a draw: packets' lowered states see it as any other draw; gx2_core exempts the op for mode 1)
    if (n < kDrawHeaderWords) return count(cLost);
    const uint32_t blocks = p[6];
    if (blocks > kMaxBlocks || n != kDrawHeaderWords + blocks * kBlockWords) return count(cLost);
    static thread_local In in;
    in.regs = regs;
    in.item = item_of(p[0], p[1], p[2], p[3]);
    in.mesh = mesh_of(p[4]);
    if (!in.item || !in.mesh) {  // (the site made no call: nothing else can draw it; never expected)
        static uint32_t said = 0;
        if (said < 20 && ++said)
            LOG("[j3d-d1] LOST a described draw: desc %u v%u env %u v%u mesh %u not published", p[0], p[1], p[2], p[3], p[4]);
        return count(cLost);
    }
    static const uint32_t stageBase[3] = {mmSQ_VTX_UNIFORM_BLOCK_START, mmSQ_PS_UNIFORM_BLOCK_START,
                                          mmSQ_GS_UNIFORM_BLOCK_START};
    in.blockCount = 0;
    in.raw = p + kDrawHeaderWords;
    in.rawCount = blocks;
    for (uint32_t i = 0; i < blocks; ++i) {
        const uint32_t* q = p + kDrawHeaderWords + i * kBlockWords;
        const uint32_t stage = q[0] >> 16, location = q[0] & 0xFFFF;
        if (stage >= 3 || location >= 16) continue;  // (as gx2_core's packet blocks)
        in.blocks[in.blockCount++] = {stageBase[stage] + location * 7, q[1], q[2]};
    }
    if (native_pass::on()) native_pass::note_draw(regs);  // (as for every draw; its counters)
    // the binds' GX2Invalidate calls, in segment order; the program segment's only when the site binds the program in
    // this draw (flag kFlagProgramBind: setProgramShaderMode runs). Refused items: in their commands.
    if (!in.item->refused) {
        Timed timedInv(tInvalidateNs);
        static const bool allInv = switch_value("WWHD_NATIVE_J3D_DESC_D1ALLINV", 0) != 0;  // (before flag 8: every draw)
        if ((p[5] & kFlagProgramBind) || allInv)
            for (size_t k = 0; k + 2 < in.item->programInvalidates.size(); k += 3) {
                const uint32_t* v = in.item->programInvalidates.data() + k;
                count(cInvalidates);
                render::invalidate(v[0], v[1], v[2]);
            }
        for (size_t k = 0; k + 2 < in.item->invalidates.size(); k += 3) {
            const uint32_t* v = in.item->invalidates.data() + k;
            count(cInvalidates);
            render::invalidate(v[0], v[1], v[2]);
        }
    }
    if (in.item->refused) {
        count(fItemRefused);
        note(in.item->refused);
        return full(in);
    }
    // WWHD_NATIVE_J3D_DESC_D1FULL=1 (profile baseline): every described draw the translated way (effects applied, the
    // full draw), timed in the same scope as the described draws
    static const bool allFull = switch_value("WWHD_NATIVE_J3D_DESC_D1FULL", 0) != 0;
    if (allFull) return full(in);
#ifdef WWHD_HAS_VULKAN
    if (render::vulkan()) {
        // port features acting per draw keep the translated path (the D0 builder skips most of them already)
        if (stereo::frame.mode == stereo::Off && !framegen::requested() && !gfxvk::gamepad_backdrop_capture_active() &&
            !cloud_proto::g_active_seq && !aspect::content_clip() && !(render::grass_distance() > 0 && in.mesh->count == 108)) {
            Timed timedVk(tVkNs);
            gfxvk::j3ddesc_draw(in);
            return;
        }
        count(cGated);
    }
#endif
    full(in);
}

void frame_end() {
    ++g_frames;
#if defined(WWHD_DECOMP_FAST)
    if (::native_draw::pardecide::level() && g_frames % 300 == 0) ::native_draw::pardecide::report(300.0);
#endif
    if (timing_on() && g_frames % 300 == 0) {  // (any mode) the render thread's CPU time per frame, last 300 frames
        static uint64_t last = 0;
        timespec ts{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        const uint64_t cpu = uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
        if (last) LOG("[j3d-d1] render thread CPU %.3f ms/frame (last 300 frames, mode %d)", (cpu - last) / 300.0 / 1e6, mode());
        last = cpu;
    }
    if (g_frames % 300 || !g_counts[cDraws]) return;
    char buf[8192];
    int j = std::snprintf(buf, sizeof buf, "[j3d-d1] per frame:");
    for (uint32_t c = 0; c < cCounts && j < int(sizeof buf) - 64; ++c)
        if (g_counts[c])
            j += std::snprintf(buf + j, sizeof buf - j, " %s %.2f;", kCountNames[c],
                               g_counts[c] / 300.0 / (c == tDrawNs || c == tSettleNs || c == tInvalidateNs || (c >= pChecks && c <= tVkNs) || c == tFallbackFullNs || c == tFallbackMakeNs ? 1000.0 : 1.0));
    LOG("%s", buf);
    std::fill(std::begin(g_counts), std::end(g_counts), 0);
    if (debug_on() && !g_why.empty()) {
        std::vector<std::pair<uint64_t, std::string>> v;
        for (auto& [k, c] : g_why) v.push_back({c, k});
        std::sort(v.rbegin(), v.rend());
        j = std::snprintf(buf, sizeof buf, "[j3d-d1] fallback reasons per frame:");
        for (size_t i = 0; i < v.size() && i < 48 && j < int(sizeof buf) - 200; ++i)
            j += std::snprintf(buf + j, sizeof buf - j, " %s %.2f;", v[i].second.c_str(), v[i].first / 300.0);
        LOG("%s", buf);
        g_why.clear();
    }
}

void reset() {
    ++g_runSerial;  // (the open run keeps its items: they are never freed)
#ifdef WWHD_HAS_VULKAN
    gfxvk::j3ddesc_clear();  // (also clears the items' render pointers)
#endif
    // (the slots find their records again: a D0 reset may renumber them; items of records kept stay valid)
    g_descSlots.clear();
    g_envSlots.clear();
    g_meshSlots.clear();
}

}  // namespace native_draw::j3ddesc::d1
