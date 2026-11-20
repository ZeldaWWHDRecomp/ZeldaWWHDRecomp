// cheapib stage 2, particles (WWHD_CHEAPIB_S2 bit 2048; decomp build, fast mode). See cheapib_s2.h.
//
// JPA particles (scene- and actor-owned emitters alike) are drawn by JPAEmitterManager's draw functions (draw, drawView
// and the three substep variants, 02821734..02821C28) from the particle layer jobs. Per drawn particle the draw visitors
// build its quad (4 vertices of 20 bytes, world space, camera-facing for billboards) into the particle's vertex pool
// buffer mBuffers[mSwapIndex] (JPAGpuPool<4> at particle +0xE0, the child's at +0xE4), set its colours / texture matrix
// in its uniform pair (mpGpuState +0xEC, a RenderUniformPair: the advance in finishGpuBuffers 02825158 queues the upload
// into mBuffers[mPreviousIndex], which the draw binds), flip the pool (the draw binds the buffer just written) and issue
// the draw (drawGpu 02825284). A replayed in-between calls the logic pass's lists: they bind those buffers, so without
// this the particles stand still for the whole step.
//
// Here, on a replayed in-between (after interp_fx has put every particle at this pass's fraction: fx_hold_blend), the
// logic pass's manager draw calls are run again in "build" mode (decomp port points PORT_CHEAPIB_PTCL, game build only):
// - no draw is issued (drawGpu returns at once) and no render state is applied (JPADraw_l::draw's pass state);
// - the uniform pair advances from the index the logic pass advanced from (mActiveIndex set to mPreviousIndex first), so
//   its upload goes into the buffer the logic list binds; stage 1's drain lands it before the painter's snapshot;
// - the vertex pool does not flip: every pool the logic pass drew has its swap index set so that the visitors build
//   into the buffer the logic list does NOT bind (the one the next logic pass fills; nothing reads it before then, as
//   for the sea grid; put back after), and at the particle's draw the bytes that draw reads (a stripe: its vertices,
//   at least as many as the logic draw's) go to the logic buffer on the render thread, in order before this pass's
//   calls (OP_CHEAPIB_VTX, cheapib_s2_vertex.cpp vtx_emit); pools finished but not drawn are copied whole.
// The list of noted draws closes when the step's first replayed pass starts. The graphics task's particle captures
// (groups 0-6: substeps and camera views, 02821868..02821AE8) run on every pass, replayed ones included; a replayed
// pass's own call of a draw the logic pass noted is skipped (op 1-5 returns 1): the rerun builds it, and a real draw
// would write into and flip the pools the logic list binds (before this, each replay noted them again: the first
// replays rebuilt fewer particles than later ones, and every other pass wrote into the bound buffer).
// The world draws go through JPADraw_l::drawHD (0282D9C0): in build mode its shader / vertex-state bind / texture
// uniforms / render state are skipped and the emitter's vertex-state pair uploads into the logic pass's buffer (op 9).
// The draw info is the logic pass's (copied when it was noted) with this pass's camera and projection where the logic
// pass drew with the main ones: the billboard corners and the emitter's dynamic uniform (JPADraw_l::updateUniforms; in
// build mode its frame pairs advance from the logic pass's index like op 9's, no bind) follow a moving camera.
// Every vertex buffer a logic pass's particle draw binds is a site range (vtx_site: b1 notes the slices holding its
// bytes and rebuilds them from guest memory on replays).
#include "cheapib/cheapib_s2.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "cheapib/cheapib.h"
#include "guest_addr.h"
#include "ppc.h"
#include "runtime.h"

namespace interp {
bool hold_pass();
uint64_t logic_steps();
float pass_t();
bool record_pass();
}  // namespace interp

namespace cheapib {
namespace {

constexpr uint32_t kView = 0x104B45F8, kProj = 0x104B470C;
// JPAEmitterManager draw functions by port point op (1 drawView, 2 drawIndirect, 3 drawFirstView, 4 drawSecondView, 5 draw)
constexpr uint32_t kDrawFn[6] = {0, 0x02821734, 0x02821868, 0x028219A8, 0x02821AE8, 0x02821C28};
constexpr uint32_t kInfoWords = 0x50 / 4;  // JPADrawInfo: +0 camera matrix pointer, +4 parameters (projection, 16 words)
// particle: +0xE0 vertex pool (parent), +0xE4 the child's pool; JPAGpuPool<4>: 4 buffers of 0x254 (+0 the CPU data
// pointer the visitors write, +0x150 the vertex buffer's data pointer the draw binds, +0x24C capacity in vertices), state
// at +0x950 (+0 swap index, +8 element size)
constexpr uint32_t kPoolParent = 0xE0, kPoolChild = 0xE4, kBuf = 0x254, kBufData = 0, kBufBound = 0x150, kBufCap = 0x24C;
constexpr uint32_t kPoolState = 4 * kBuf, kPoolSwap = kPoolState, kPoolElem = kPoolState + 8;
constexpr uint32_t kPairActive = 0x48, kPairPrevious = 0x4C;

struct Note {
    int op;
    uint32_t mgr, group;
    uint32_t info[kInfoWords];  // raw guest words
    uint32_t cam[12];
    bool cam_view, proj_main;
};
struct Bound {
    uint32_t index;  // the buffer the draw binds (and its +2 pair)
    uint32_t bytes;  // a stripe's vertex bytes the draw reads (0: the whole buffer)
};
struct S {
    std::mutex mu;  // (the notes: the particle layer job's thread)
    std::vector<Note> notes;
    uint64_t step = ~0ull;
    bool open = false;
    // the step's first replayed pass has started: later draws of the step are a replayed pass's own (the graphics task's
    // particle captures run on every pass), not the logic pass's
    bool closed = false;
    std::unordered_map<uint32_t, Bound> bound_now;  // (game thread, the rerun's copy) for the copies' sizes
    uint64_t own_skipped = 0, own_passed = 0;
    std::unordered_map<uint32_t, Bound> bound;  // (under mu) pool -> what the logic pass's draw binds
    // build (game thread)
    bool build = false;
    std::vector<uint32_t> ranges;  // (guest address, size, offset) of this pass's vertex copies
    std::vector<uint8_t> bytes;
    std::vector<std::pair<uint32_t, uint32_t>> pending;  // (pool, built side) finished, not yet drawn
    // counters (per report)
    uint64_t passes = 0, calls = 0, ns = 0, copies = 0, copy_bytes = 0, pairs = 0, draws_dropped = 0, bad = 0;
    uint64_t logic_notes = 0, logic_steps = 0, cam_view = 0, proj_main = 0, logic_draws = 0;
    uint32_t dbg[3] = {};  // (debug, WWHD_CHEAPIB_S2_PLOG: this pass's draws, pair advances, finished pools)
} g;

// the site ranges: sorted, coalesced [lo, hi); two arrays, the readers' one published by pointer (writers are rare once
// the particle pools have all been drawn: they are allocated once)
struct Ranges {
    std::vector<std::pair<uint32_t, uint32_t>> v;
};
Ranges g_rs[2];
std::atomic<const Ranges*> g_ranges{nullptr};
std::mutex g_rmu;
std::vector<std::pair<uint32_t, uint32_t>> g_rnew;  // (under g_rmu) the master copy
int g_rcur = 0;

bool contains(const std::vector<std::pair<uint32_t, uint32_t>>& v, uint32_t lo, uint32_t hi) {
    auto it = std::upper_bound(v.begin(), v.end(), std::make_pair(lo, 0xFFFFFFFFu));
    if (it == v.begin()) return false;
    --it;
    return it->first <= lo && hi <= it->second;
}
void add_range(uint32_t lo, uint32_t size) {
    if (!lo || !size || size > 0x10000) return;
    const uint32_t hi = lo + size;
    if (const Ranges* r = g_ranges.load(std::memory_order_acquire); r && contains(r->v, lo, hi)) return;
    std::lock_guard<std::mutex> lk(g_rmu);
    if (contains(g_rnew, lo, hi)) return;
    g_rnew.emplace_back(lo, hi);
    std::sort(g_rnew.begin(), g_rnew.end());
    std::vector<std::pair<uint32_t, uint32_t>> m;
    for (const auto& r : g_rnew) {
        if (!m.empty() && r.first <= m.back().second + 64) m.back().second = std::max(m.back().second, r.second);  // (gaps of
        else m.push_back(r);  // one 64-byte alignment step: the pool allocator's padding)
    }
    g_rnew = m;
    g_rcur ^= 1;
    g_rs[g_rcur].v = m;
    g_ranges.store(&g_rs[g_rcur], std::memory_order_release);
}

bool ptcl_on() { return (s2_mask() & 2048) != 0; }

// A manager draw's camera matrix (its draw info +0) can live on the drawing thread's stack: the particle layer jobs run
// on runtime threads whose guest stacks are runtime-owned (kRuntimeStart..kRuntimeEnd), the game thread's in MEM2.
// Before, only MEM2 cameras were noted: a 2D group drawn from a layer job got a zero camera, so the rerun built its
// emitter frame uniform with a zero draw matrix and the rupee sparkle vanished on every replayed pass of that step.
// Which thread drew it depended on host timing (c2 on or off, any render load). WWHD_CHEAPIB_S2_PTCL_CAM=0: the old
// MEM2-only rule (test).
bool cam_noted(uint32_t cam) {
    static const bool fix = !getenv("WWHD_CHEAPIB_S2_PTCL_CAM") || atoi(getenv("WWHD_CHEAPIB_S2_PTCL_CAM")) != 0;
    const uint64_t end = uint64_t(cam) + 48;
    if (cam >= mem::kMem2Start && cam < mem::kMem2End) return true;  // (the old rule, unchanged)
    return fix && cam >= mem::kRuntimeStart && end <= mem::kRuntimeEnd;
}

uint32_t buf_size(uint32_t pool, uint32_t b) {
    const uint32_t cap = ld32(b + kBufCap), elem = ld32(pool + kPoolElem);
    const uint64_t n = uint64_t(cap) * elem;
    return n > 0 && n <= 0x4000 ? (uint32_t)n : 0;
}

// a logic pass's particle draw: the buffers it binds are site ranges
void logic_draw(uint32_t p, uint32_t kind, uint32_t prim, uint32_t count) {
    const uint32_t pool = ld32(p + (kind == 1 ? kPoolChild : kPoolParent));
    if (pool < 0x10000000 || pool >= 0x50000000) return;
    const uint32_t active = ld32(pool + kPoolSwap) == 0 ? 1 : 0;
    g.bound[pool] = Bound{active, kind == 1 && prim ? prim * ld32(pool + kPoolElem) : 0};
    const uint32_t n = kind == 2 ? 1 : std::min<uint32_t>(count, 2);
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t b = pool + kBuf * (active + 2 * i);
        const uint32_t size = buf_size(pool, b);
        const uint32_t at = ld32(b + kBufData);
        if (at >= 0x10000000 && at < 0x50000000) add_range(at, size ? size : 80);
    }
}

// OP 1-5: a manager draw on the logic pass
void note(int op, uint32_t mgr, uint32_t info, uint32_t group) {
    if (!info) return;
    Note n{op, mgr, group};
    for (uint32_t i = 0; i < kInfoWords; i++) n.info[i] = ld32(info + 4 * i);
    const uint32_t cam = n.info[0];
    if (cam_noted(cam)) {
        for (int i = 0; i < 12; i++) n.cam[i] = ld32(cam + 4 * i);
        n.cam_view = true;  // (n.cam, n.info: host-order words, as ld32 reads them)
        for (int i = 0; i < 12; i++) n.cam_view &= n.cam[i] == ld32(GD(kView) + 4 * i);
    } else {
        memset(n.cam, 0, sizeof n.cam);
        n.cam_view = false;
    }
    n.proj_main = true;
    for (int i = 0; i < 16; i++) n.proj_main &= n.info[1 + i] == ld32(GD(kProj) + 4 * i);
    {
        static std::atomic<int> tr{6};
        if (!n.cam_view && tr.load() > 0 && tr-- > 0) {
            const uint32_t* v = (const uint32_t*)ppc_ptr(GD(kView));
            const uint32_t* pj = (const uint32_t*)ppc_ptr(GD(kProj));
            LOG("[cheapib] s2 particles debug: op %d group %u camera %08X: %08X %08X %08X %08X / view %08X %08X %08X %08X; "
                "params %08X %08X %08X %08X / proj %08X %08X %08X %08X", op, group, cam, (n.cam[0]),
                (n.cam[1]), (n.cam[2]), (n.cam[3]), __builtin_bswap32(v[0]),
                __builtin_bswap32(v[1]), __builtin_bswap32(v[2]), __builtin_bswap32(v[3]), (n.info[1]),
                (n.info[2]), (n.info[3]), (n.info[4]),
                __builtin_bswap32(pj[0]), __builtin_bswap32(pj[1]), __builtin_bswap32(pj[2]), __builtin_bswap32(pj[3]));
        }
    }
    std::lock_guard<std::mutex> lk(g.mu);
    const uint64_t step = interp::logic_steps();
    if (g.step != step) {  // (the particle layer job runs after the per-frame function: logic_done has been)
        g.notes.clear();
        g.bound.clear();
        g.step = step;
        g.closed = false;
        g.logic_steps++;
    }
    if (g.closed) return;  // (a replayed pass's own draw)
    g.notes.push_back(n);
    g.logic_notes++;
    if (n.cam_view) g.cam_view++;
    if (n.proj_main) g.proj_main++;
}

// a built pool's buffers [other + 2i] (i < n) go to the logic pass's [other ^ 1 + 2i]: at most `limit` bytes each (the
// vertices the draw uses; 0 = the whole buffer)
void copy_pool(uint32_t pool, uint32_t other, uint32_t n, uint32_t limit) {
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t src_b = pool + kBuf * (other + 2 * i), dst_b = pool + kBuf * ((other ^ 1) + 2 * i);
        const uint32_t src = ld32(src_b + kBufData), dst = ld32(dst_b + kBufData), full = buf_size(pool, src_b);
        auto ok = [](uint32_t x) { return x >= 0x10000000 && x < 0x50000000; };
        if (!ok(src) || !ok(dst) || !full || full != buf_size(pool, dst_b)) {
            if (src || dst) {
                static int tr = 8;
                if (tr > 0) {
                    tr--;
                    LOG("[cheapib] s2 particles: pool %08X buffer %u skipped: src %08X dst %08X bound %08X size %u/%u",
                        pool, (other ^ 1) + 2 * i, src, dst, ld32(dst_b + kBufBound), full, buf_size(pool, dst_b));
                }
                g.bad++;
            }
            continue;
        }
        const uint32_t size = limit && limit < full ? limit : full;
        g.ranges.insert(g.ranges.end(), {dst, size, (uint32_t)g.bytes.size()});
        g.bytes.insert(g.bytes.end(), PPC_MEM_BASE + src, PPC_MEM_BASE + src + size);
        add_range(dst, full);
        g.copies++;
        g.copy_bytes += size;
    }
}

// a manager draw outside the port's build after the step's first replayed pass started: a replayed pass's own draw
// (the graphics task's particle captures, groups 0-6: substeps and camera views, run on every pass; their output is not
// what the replayed lists draw). When the logic pass drew it too, the rerun builds it: skipped (a real draw would write
// and flip the pools the logic list binds). Others (not noted) run as they are.
bool replayed_own(int op, uint32_t mgr, uint32_t group) {
    std::lock_guard<std::mutex> lk(g.mu);
    if (!g.closed || g.step != interp::logic_steps()) return false;
    for (const Note& n : g.notes)
        if (n.op == op && n.mgr == mgr && n.group == group) {
            g.own_skipped++;
            return true;
        }
    g.own_passed++;
    return false;
}

}  // namespace

bool ptcl_site(uint32_t addr, uint32_t size) {
    const Ranges* r = g_ranges.load(std::memory_order_acquire);
    if (!r || r->v.empty()) return false;
    const uint64_t end = uint64_t(addr) + size;
    auto it = std::upper_bound(r->v.begin(), r->v.end(), std::make_pair(addr, 0xFFFFFFFFu));
    if (it != r->v.end() && it->first < end) return true;
    return it != r->v.begin() && (it - 1)->second > addr;
}

int ptcl_point(int op, uint32_t a, uint32_t b, uint32_t c) {
    if (!ptcl_on()) return 0;
    switch (op) {
    case 1: case 2: case 3: case 4: case 5:
        {
            static std::atomic<int> tr{12};
            if (tr.load() > 0 && tr-- > 0)
                LOG("[cheapib] s2 particles debug: draw op %d group %u hold %d replaying %d build %d step %llu", op, c,
                    (int)interp::hold_pass(), (int)replaying(), (int)g.build, (unsigned long long)interp::logic_steps());
        }
        if (g.build || !stage1()) return 0;
        if (replayed_own(op, a, c)) return 1;  // (a replayed pass's own particle capture: the rerun builds it)
        if (!interp::hold_pass()) note(op, a, b, c);
        return 0;
    case 8:
        if (g.build) {
            if (a) {  // the draw's pool: its buffers i < count, a stripe (kind 1) only its `primitive` vertices
                g.draws_dropped++;
                g.dbg[0]++;
                const uint32_t kind = b & 0xFF, prim = b >> 8;
                const uint32_t pool = ld32(a + (kind == 1 ? kPoolChild : kPoolParent));
                for (size_t k = g.pending.size(); k-- > 0;)
                    if (g.pending[k].first == pool) {
                        uint32_t limit = kind == 1 && prim ? prim * ld32(pool + kPoolElem) : 0;
                        if (limit) {  // (the logic list's draw reads its own count: a longer stripe copies that many)
                            auto it = g.bound_now.find(pool);
                            if (it != g.bound_now.end()) limit = it->second.bytes ? std::max(limit, it->second.bytes) : 0;
                        }
                        copy_pool(pool, g.pending[k].second, kind == 2 ? 1 : std::min<uint32_t>(c, 2), limit);
                        g.pending.erase(g.pending.begin() + k);
                        break;
                    }
            }
            return 1;
        }
        if (a && !interp::hold_pass()) {
            std::lock_guard<std::mutex> lk(g.mu);
            if (g.closed || g.step != interp::logic_steps()) return 0;  // (a replayed pass's own draw)
            logic_draw(a, b & 0xFF, b >> 8, c);
            g.logic_draws++;
        }
        return 0;
    case 9:
        if (g.build && a) {
            st32(a + kPairActive, ld32(a + kPairPrevious));
            g.pairs++;
            g.dbg[1]++;
        }
        return 0;
    case 10: {
        if (!g.build) return 0;
        const uint32_t pool = a, other = b & 1;
        g.dbg[2]++;
        g.pending.push_back({pool, other});  // (copied at its draw, with the size the draw uses; else at the end)
        return 1;  // (no flip: the pool stays as the logic pass left it)
    }
    }
    return 0;
}

// the logic pass's per-frame function returned
void s2_ptcl_logic_done() {
    if (!ptcl_on()) return;
    // (nothing: the particle layer job runs after this; the list belongs to the step it was noted in)
}

// a replayed in-between (cheapib_models.h replay, before stage 1's drain: the particles' queued uploads land with it)
void s2_ptcl_rerun(Cpu* c) {
    if (!ptcl_on() || !replaying() || verifying()) return;
    std::vector<Note> notes;
    std::unordered_map<uint32_t, Bound> bound;
    {
        std::lock_guard<std::mutex> lk(g.mu);
        static int tr = 40;
        if (tr > 0 && g.step != interp::logic_steps()) {
            tr--;
            LOG("[cheapib] s2 particles debug: rerun skipped: open %d step %llu now %llu notes %zu", (int)g.open,
                (unsigned long long)g.step, (unsigned long long)interp::logic_steps(), g.notes.size());
        }
        if (g.step != interp::logic_steps()) return;  // (this step's logic pass drew no particles)
        g.closed = true;
        notes = g.notes;
        bound = g.bound;
        g.bound_now = g.bound;
    }
    // every pool the logic pass drew builds into its other buffer: the game's own swap index can have moved since
    // (a replayed pass's particle captures draw for real); put back after the build
    std::vector<std::pair<uint32_t, uint32_t>> swaps;
    swaps.reserve(bound.size());
    for (const auto& [pool, b] : bound) {
        swaps.emplace_back(pool, ld32(pool + kPoolSwap));
        st32(pool + kPoolSwap, b.index ^ 1);
    }
    const auto t0 = std::chrono::steady_clock::now();
    g.passes++;
    // the draw info and its camera matrix in guest scratch below the stack (the draw's own frames go below that)
    Cpu cc = *c;
    const uint32_t top = (c->r[1] - 0x100) & ~0xFu;
    const uint32_t info = top - 0x60, cam = top - 0x90;
    cc.r[1] = (top - 0x400) & ~0xFu;
    g.ranges.clear();
    g.bytes.clear();
    memset(g.dbg, 0, sizeof g.dbg);
    g.pending.clear();
    g.build = true;
    for (const Note& n : notes) {
        for (uint32_t i = 0; i < kInfoWords; i++) st32(info + 4 * i, n.info[i]);
        // this pass's camera where the logic pass drew with the main camera (view, projection); others keep theirs (the
        // 2D groups). The billboard corners and the emitter's dynamic uniform (JPADraw_l::updateUniforms: rebuilt in
        // build mode into the buffer the logic list binds) both come from it, as on a full rerun.
        for (int i = 0; i < 12; i++) st32(cam + 4 * i, n.cam_view ? ld32(GD(kView) + 4 * i) : n.cam[i]);
        st32(info, cam);
        if (n.proj_main)
            for (int i = 0; i < 16; i++) st32(info + 4 + 4 * i, ld32(GD(kProj) + 4 * i));
        Cpu k = cc;
        guest_call(&k, kDrawFn[n.op], {n.mgr, info, n.group});
        g.calls++;
    }
    g.build = false;
    for (const auto& pk : g.pending) copy_pool(pk.first, pk.second, 2, 0);  // (finished, never drawn: whole buffers)
    g.pending.clear();
    for (const auto& [pool, v] : swaps) st32(pool + kPoolSwap, v);
    {  // debug: WWHD_CHEAPIB_S2_PLOG=n logs n replayed passes' vertex hash (do the passes of a step differ?)
        static int left = getenv("WWHD_CHEAPIB_S2_PLOG") ? atoi(getenv("WWHD_CHEAPIB_S2_PLOG")) : 0;
        if (left > 0) {
            left--;
            uint64_t h = 1469598103934665603ull;
            for (uint8_t b : g.bytes) h = (h ^ b) * 1099511628211ull;
            LOG("[cheapib] s2 particles debug: step %llu pass t %.3f record %d: %zu bytes, hash %016llX; draws %u, "
                "pair advances %u, finished pools %u",
                (unsigned long long)interp::logic_steps(), interp::pass_t(), (int)interp::record_pass(), g.bytes.size(),
                (unsigned long long)h, g.dbg[0], g.dbg[1], g.dbg[2]);
        }
    }
    if (!g.ranges.empty()) vtx_emit(std::move(g.ranges), std::move(g.bytes));
    g.ranges = {};
    g.bytes = {};
    g.ns += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    if (g.passes % 900 == 0) {
        auto per = [](uint64_t v, uint64_t d) { return d ? double(v) / double(d) : 0.0; };
        const Ranges* r = g_ranges.load(std::memory_order_acquire);
        LOG("[cheapib] s2 particles: per replayed pass %.1f us, manager draws %.1f, particle quads copied %.1f (%.1f KB), "
            "pairs %.1f, draws dropped %.1f, bad pools %llu; logic: draw notes %.1f per step (main camera %.0f%%, main "
            "projection %.0f%%), particle draws %.1f per step; site ranges %zu; replayed passes' own draws skipped %.1f, "
            "run %.1f per pass",
            per(g.ns, g.passes) / 1000.0, per(g.calls, g.passes), per(g.copies, g.passes), per(g.copy_bytes, g.passes) / 1024.0,
            per(g.pairs, g.passes), per(g.draws_dropped, g.passes), (unsigned long long)g.bad, per(g.logic_notes, g.logic_steps),
            100.0 * per(g.cam_view, g.logic_notes), 100.0 * per(g.proj_main, g.logic_notes), per(g.logic_draws, g.logic_steps),
            r ? r->v.size() : (size_t)0, per(g.own_skipped, g.passes), per(g.own_passed, g.passes));
        g.passes = g.calls = g.ns = g.copies = g.copy_bytes = g.pairs = g.draws_dropped = g.bad = 0;
        g.logic_notes = g.logic_steps = g.cam_view = g.proj_main = g.logic_draws = 0;
        g.own_skipped = g.own_passed = 0;
    }
}

}  // namespace cheapib

extern "C" int wwhd_cheapib_ptcl(int op, unsigned a, unsigned b, unsigned c) { return cheapib::ptcl_point(op, a, b, c); }
