// Draw runs (Vulkan; on by default in the normal build, off in the decomp fast build; draw_batch::switch_on). Included by gx2_core.cpp only, after g_regs and execute_one.
// PROVENANCE: the run logic follows VR-WakerHD's changes to gx2_core.cpp (held_*, index runs, gather_indices; commit
// e06fb7b99cbb, MPL-2.0 like every changed ZeldaWWHDRecomp file, discussion #535); "draw batching from
// VR-WakerHD (thanks @mkooner)". Rewritten here as one file with a single hook at the top of execute_one.
//
// Two kinds of runs, each with its own switch:
// - Same-state runs (WWHD_VK_BATCH_RUNS; =0 turns them off): consecutive draws with the same arguments (one instance) between which
//   nothing changes but vertex-shader uniform block addresses (word 0 of SQ_VTX_UNIFORM_BLOCK_*), e.g. the grass (one
//   tuft a draw, its own matrix block). From its second draw on such a run is held; any other command, or a register
//   write that changes anything else, first draws it: offered to the renderer as one draw of `count` instances with
//   draw_batch::current() set (gfx/draw_batch.h; the Vulkan renderer draws it with the vertex shader's batched variant,
//   gfx/vulkan/batch_draw.h) and, if the renderer leaves draw_batch::consumed() false (nothing drawn), drawn piece by
//   piece, each with its own block addresses, exactly as without runs.
// - Index runs (WWHD_VK_INDEX_RUNS; =0 turns them off): consecutive indexed triangle lists or strips with the same primitive, index
//   type and base vertex, one instance, no register value changed and no other command between them, become one draw:
//   a list whose pieces are one contiguous range as that range; otherwise the pieces' indices copied into a ring of
//   host-only guest memory (strips joined by zero-area triangles that keep every piece's triangles at their own
//   parity, so winding and provoking vertex are the same). Non-indexed lists whose vertices follow on are joined too.
//   A restart index in a strip piece, or a run too large for the ring: drawn piece by piece.
// Every piece reads the same vertices, indices, blocks and state as its own draw; what is not proven equal draws as
// before. A run's draws are executed through execute_one as any draw (profiler, native pass checks, the renderer).
#pragma once
// (gx2_core.cpp includes gfx/draw_batch.h at file scope and defines its variables)

namespace draw_runs {

inline bool batch_on() {
    static const bool on = [] {
        const bool v = render::vulkan() && draw_batch::switch_on("WWHD_VK_BATCH_RUNS");
        LOG("[gx2] same-state draw runs %s", v ? "on (WWHD_VK_BATCH_RUNS=0 turns them off)" : "off");
        return v;
    }();
    return on;
}
inline bool index_on() {
    static const bool on = [] {
        const bool v = render::vulkan() && draw_batch::switch_on("WWHD_VK_INDEX_RUNS");
        LOG("[gx2] index draw runs %s", v ? "on (WWHD_VK_INDEX_RUNS=0 turns them off)" : "off");
        return v;
    }();
    return on;
}
inline bool any_on() {
    static const bool on = batch_on() || index_on();
    return on;
}

constexpr uint32 kVsBlocks = mmSQ_VTX_UNIFORM_BLOCK_START;
inline bool vs_block_addr(uint32 reg) { return reg >= kVsBlocks && reg < kVsBlocks + 16 * 7 && (reg - kVsBlocks) % 7 == 0; }
inline void vs_block_addrs(uint32* out) {
    for (uint32 i = 0; i < 16; ++i) out[i] = g_regs[kVsBlocks + i * 7];
}
// (draw threads T1: forwarded like every other change to the register file, register_forward.h; a draw thread's copy
// then draws each piece, and the restore after the run, with the same addresses)
inline void set_vs_block_addrs(const uint32* addrs) {
    for (uint32 i = 0; i < 16; ++i) {
        g_regs[kVsBlocks + i * 7] = addrs[i];
        forward::note(kVsBlocks + i * 7, &g_regs[kVsBlocks + i * 7], 1);
    }
}
inline uint32 draw_instances(Op op, const uint32* p) { return op == OP_DRAW ? p[3] : p[5]; }

// ---- statistics (logged every 600 frames while a switch is on)
struct Stats {
    uint64_t frames = 0;
    uint64_t heldRuns = 0, heldPieces = 0, batchedRuns = 0, batchedPieces = 0;
    uint64_t indexRuns = 0, indexPieces = 0, indexCopied = 0, indexReused = 0, indexRefused = 0;
    uint64_t notCandidate = 0, notAllowed = 0, breakWrite = 0, breakOp[256] = {};  // (why the next draw could not join)
};
inline Stats g_stats;

// ---- same-state runs
struct Held {
    bool on = false;
    Op op = OP_NOP;
    uint32 n = 0;
    uint32 args[6] = {};
    draw_batch::Batch batch;
};
inline Held g_held;
// the last candidate draw executed here: valid while nothing but vertex-shader block addresses changed since
struct Last {
    bool valid = false;
    Op op = OP_NOP;
    uint32 n = 0;
    uint32 args[6] = {};
};
inline Last g_last;
inline bool g_inside = false;  // (a run's draws executing: not intercepted again)

// a register write that changes anything but a vertex-shader uniform block address
inline bool write_breaks(const uint32* p, uint32 n) {
    if (!n) return false;
    const uint32 first = p[0], count = n - 1;
    if (first >= kNumRegs || count > kNumRegs - first) return true;
    const uint32* v = p + 1;
    if (!memcmp(&g_regs[first], v, count * 4)) return false;
    for (uint32 i = 0; i < count; ++i)
        if (g_regs[first + i] != v[i] && !vs_block_addr(first + i)) return true;
    return false;
}
inline bool write_changes(const uint32* p, uint32 n) {
    if (!n) return false;
    const uint32 first = p[0], count = n - 1;
    if (first >= kNumRegs || count > kNumRegs - first) return true;
    return memcmp(&g_regs[first], p + 1, count * 4) != 0;
}

inline void exec_draw(Op op, const uint32* a, uint32 n) {
    const bool was = g_inside;
    g_inside = true;
    struct Restore { bool was; ~Restore() { g_inside = was; } } restore{was};
    execute_one(op, a, n);
}

// the held run drawn: as one draw of all its pieces if the renderer takes it, else piece by piece
inline void held_flush() {
    if (!g_held.on) return;
    g_held.on = false;
    g_last.valid = false;
    const draw_batch::Batch& b = g_held.batch;
    uint32 saved[16];
    vs_block_addrs(saved);
    struct Restore {
        const uint32* saved;
        ~Restore() {
            draw_batch::current() = nullptr;
            set_vs_block_addrs(saved);
        }
    } restore{saved};
    ++g_stats.heldRuns;
    g_stats.heldPieces += b.count;
    uint32 a[6];
    memcpy(a, g_held.args, sizeof a);
    const uint32 instSlot = g_held.op == OP_DRAW ? 3 : 5;
    bool done = false;
    if (b.count > 1) {
        set_vs_block_addrs(b.addrs[0]);
        draw_batch::consumed() = false;
        draw_batch::current() = &b;
        a[instSlot] = b.count;
        exec_draw(g_held.op, a, g_held.n);
        draw_batch::current() = nullptr;
        done = draw_batch::consumed();
        a[instSlot] = 1;
    }
    if (done) {
        ++g_stats.batchedRuns;
        g_stats.batchedPieces += b.count;
    }
    for (uint32 k = 0; !done && k < b.count; ++k) {
        set_vs_block_addrs(b.addrs[k]);
        exec_draw(g_held.op, a, g_held.n);
    }
}

// whether this draw may begin or join a same-state run (the renderer's and the build's conditions)
bool hold_allowed();  // (gx2_core.cpp: the build's own conditions, e.g. no J3D draw name pending)

// the held run: this draw joins it (true), or it is drawn now (false; no run held after)
inline bool held_join_or_flush(Op op, const uint32* p, uint32 n) {
    if (!g_held.on) return false;
    draw_batch::Batch& b = g_held.batch;
    if (op == g_held.op && n == g_held.n && !memcmp(p, g_held.args, n * 4) && b.count < draw_batch::kMaxPieces) {
        uint32* addrs = b.addrs[b.count++];
        vs_block_addrs(addrs);
        for (uint32 i = 0; i < 16; ++i)
            if (addrs[i] != b.addrs[0][i]) b.blocks |= 1u << i;
        return true;
    }
    held_flush();
    return false;
}
// a draw that is not part of an index run: joins the held run, starts one (true: taken), or is executed by the caller
// right after this (false; noted: the next draw may continue it)
inline bool held_take(Op op, const uint32* p, uint32 n) {
    if (held_join_or_flush(op, p, n)) return true;
    if (!batch_on()) return false;
    const bool candidate = (n == 4 || n == 6) && draw_instances(op, p) == 1;
    if (candidate && g_last.valid && op == g_last.op && n == g_last.n && !memcmp(p, g_last.args, n * 4)) {
        g_last.valid = false;
        g_held.on = true;
        g_held.op = op;
        g_held.n = n;
        memcpy(g_held.args, p, n * 4);
        g_held.batch.count = 1;
        g_held.batch.blocks = 0;
        vs_block_addrs(g_held.batch.addrs[0]);
        return true;
    }
    g_last.valid = candidate && hold_allowed() && draw_batch::can_hold() && draw_batch::can_hold()(g_regs);
    if (!candidate) ++g_stats.notCandidate;
    else if (!g_last.valid) ++g_stats.notAllowed;
    if (g_last.valid) {
        g_last.op = op;
        g_last.n = n;
        memcpy(g_last.args, p, n * 4);
    }
    return false;
}
// a draw executed now (not intercepted): the next draw may continue it
inline void held_draw_now(Op op, const uint32* p, uint32 n) {
    if (!held_take(op, p, n)) exec_draw(op, p, n);
}

// ---- index runs
constexpr uint32 kPrimTriangles = 4, kPrimTriangleStrip = 6;
constexpr uint32 kIndexRunMax = 256;             // pieces in a run
constexpr uint32 kIndexScratchBytes = 8u << 20;  // the copied indices' ring (several frames' worth)
constexpr uint32 kPieceMax = 1u << 20;           // elements in a piece
struct IndexRun {
    bool on = false;
    Op op = OP_DRAW_INDEXED;
    uint32 n = 6;
    uint32 args[6] = {};  // the first piece's
    uint32 pieces = 0, total = 0;
    uint32 counts[kIndexRunMax] = {}, addrs[kIndexRunMax] = {};  // addrs: indices, or the first vertex
    bool contiguous = true;
};
inline IndexRun g_irun;
inline uint32 index_size(uint32 type) { return type == 1 || type == 9 ? 4 : 2; }  // GX2_INDEX_TYPE_U32(_LE)
inline bool index_le(uint32 type) { return type == 0 || type == 1; }             // GX2_INDEX_TYPE_U16_LE, U32_LE
inline bool index_type_known(uint32 type) { return type == 0 || type == 1 || type == 4 || type == 9; }
inline uint32 list_vertices(uint32 prim) {
    switch (prim) {
    case 1: return 1;               // points
    case 2: return 2;               // lines
    case 4: case 0x11: return 3;    // triangles, rects
    case 0x13: return 4;            // quads
    default: return 0;
    }
}
inline bool run_shape(Op op, const uint32* p, uint32 n) {
    if (op == OP_DRAW_INDEXED)
        return n == 6 && (p[0] == kPrimTriangles || p[0] == kPrimTriangleStrip) && p[5] == 1 && p[1] >= 3 &&
               p[1] < kPieceMax && index_type_known(p[2]);
    const uint32 v = op == OP_DRAW && n == 4 ? list_vertices(p[0]) : 0;
    return v && p[3] == 1 && p[1] && p[1] < kPieceMax && p[1] % v == 0;
}
inline bool index_candidate(Op op, const uint32* p, uint32 n) {
    // (a geometry shader may read the primitive ID: no runs)
    return index_on() && run_shape(op, p, n) && !g_regs[Latte::REGADDR::VGT_GS_MODE] && hold_allowed();
}
inline bool index_continues(Op op, const uint32* p, uint32 n) {
    const IndexRun& r = g_irun;
    if (op != r.op || n != r.n || p[0] != r.args[0] || r.pieces >= kIndexRunMax || !run_shape(op, p, n) ||
        r.total + p[1] > kIndexScratchBytes / 16)
        return false;
    if (op == OP_DRAW_INDEXED) return p[2] == r.args[2] && p[4] == r.args[4];  // same index type and base vertex
    return p[2] == r.addrs[r.pieces - 1] + r.counts[r.pieces - 1];  // (non-indexed: the vertices follow on)
}
inline void index_add(Op op, const uint32* p) {
    IndexRun& r = g_irun;
    const uint32 k = r.pieces++;
    r.counts[k] = p[1];
    r.total += p[1];
    if (op == OP_DRAW_INDEXED) {
        r.addrs[k] = p[3];
        if (k) r.contiguous &= p[3] == r.addrs[k - 1] + r.counts[k - 1] * index_size(p[2]);
    } else {
        r.addrs[k] = p[2];
    }
}
// A copy kept for the frames after: the same run copied again with the same bytes gets the earlier copy's address,
// so the renderer's buffer cache finds its converted indices there. Exact: the bytes are compared, and the cache still
// checks the copy's pages for writes. Reused only while the ring has moved on less than half its size since
struct KeptCopy { uint64_t pos = 0; uint32 at = 0, bytes = 0, count = 0; };
inline std::unordered_map<uint64_t, KeptCopy> g_keptCopies;
inline uint64_t run_shape_key(const IndexRun& r) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; h ^= h >> 29; };
    mix(r.args[0] | uint64_t(r.args[2]) << 32);
    mix(r.pieces);
    for (uint32 k = 0; k < r.pieces; ++k) mix(r.counts[k] | uint64_t(r.addrs[k]) << 32);
    return h;
}
// the run's indices (in their own index type, unchanged values) copied into the ring; 0 when they cannot be
inline uint32 gather_indices(uint32& outCount) {
    static uint32 ring = 0, at = 0;
    static uint64_t head = 0;  // bytes the ring has moved on in all (the skipped end of each lap included)
    static std::vector<uint8_t> scratch;
    const IndexRun& r = g_irun;
    const uint32 type = r.args[2], size = index_size(type);
    const bool le = index_le(type), strip = r.args[0] == kPrimTriangleStrip;
    const uint64_t bytes = (uint64_t(r.total) + (strip ? 3ull * r.pieces : 0)) * size;
    if (bytes > kIndexScratchBytes / 4) return 0;
    if (!ring) ring = mem::host_alloc(kIndexScratchBytes, 0x100);
    if (scratch.size() < bytes) scratch.resize(size_t(bytes));
    uint8_t* out = scratch.data();
    const uint32 restart = size == 4 ? 0xFFFFFFFFu : 0xFFFFu;
    auto get = [&](const uint8_t* e) -> uint32 {
        if (size == 2) return le ? uint32(e[0] | e[1] << 8) : uint32(e[0] << 8 | e[1]);
        return le ? uint32(e[0] | e[1] << 8 | e[2] << 16 | uint32(e[3]) << 24)
                  : uint32(uint32(e[0]) << 24 | e[1] << 16 | e[2] << 8 | e[3]);
    };
    uint32 len = 0;
    const uint8_t* lastBytes = nullptr;
    auto put = [&](const uint8_t* e) {
        memcpy(out + size_t(size) * len++, e, size);
        lastBytes = e;
    };
    for (uint32 k = 0; k < r.pieces; ++k) {
        const uint8_t* in = (const uint8_t*)mem::ptr(r.addrs[k]);
        for (uint32 i = 0; i < r.counts[k]; ++i) {
            const uint8_t* e = in + size_t(size) * i;
            if (strip && get(e) == restart) return 0;  // (a restart index: a new strip on the seam's index)
            if (strip && k && i == 0) {
                const uint8_t* prev = lastBytes;
                if (len & 1) put(prev);  // (the next piece starts on an even index: its own winding)
                put(prev);
                put(e);
            }
            put(e);
        }
    }
    const uint32 used = len * size;
    outCount = len;
    const uint64_t key = run_shape_key(r);
    auto it = g_keptCopies.find(key);
    if (it != g_keptCopies.end()) {
        const KeptCopy& c = it->second;
        if (c.count == len && c.bytes == used && head - c.pos <= kIndexScratchBytes / 2 &&
            !memcmp(mem::ptr(ring + c.at), out, used)) {
            ++g_stats.indexReused;
            return ring + c.at;
        }
    }
    const uint32 step = uint32((uint64_t(used) + 0xFF) & ~0xFFull);
    if (at + step > kIndexScratchBytes) head += kIndexScratchBytes - at, at = 0;
    const uint32 dst = ring + at;
    memcpy(mem::ptr(dst), out, used);
    if (g_keptCopies.size() > 8192) g_keptCopies.clear();
    g_keptCopies[key] = {head, at, used, len};
    at += step;
    head += step;
    ++g_stats.indexCopied;
    return dst;
}
// the index run drawn: one draw, or (one piece, or refused) as the draws it is
inline void index_flush() {
    if (!g_irun.on) return;
    g_irun.on = false;
    IndexRun& r = g_irun;
    uint32 a[6];
    memcpy(a, r.args, sizeof a);
    if (r.pieces == 1) { held_draw_now(r.op, a, r.n); return; }
    if (r.op == OP_DRAW) {
        a[1] = r.total;
    } else if (r.args[0] == kPrimTriangles && r.contiguous) {
        a[1] = r.total;
    } else {
        uint32 count = 0;
        const uint32 addr = gather_indices(count);
        if (!addr) {  // (too large for the ring, or a restart index: piece by piece, as before)
            ++g_stats.indexRefused;
            for (uint32 k = 0; k < r.pieces; ++k) {
                a[1] = r.counts[k], a[3] = r.addrs[k];
                held_draw_now(OP_DRAW_INDEXED, a, 6);
            }
            return;
        }
        a[1] = count, a[3] = addr;
    }
    ++g_stats.indexRuns;
    g_stats.indexPieces += r.pieces;
    held_draw_now(r.op, a, r.n);
}

inline void flush_all() {
    index_flush();
    held_flush();
}

inline void log_stats() {
    static const bool stats = getenv("WWHD_VK_STATS") != nullptr;  // (counters logged with WWHD_VK_STATS only)
    Stats& s = g_stats;
    if (!stats || ++s.frames < 600) return;
    const double f = double(s.frames);
    LOG("[gx2] draw runs per frame: same-state %.1f runs of %.1f draws, %.1f of them (%.1f draws) drawn as one instanced "
        "draw; index runs %.1f of %.1f draws drawn as one draw (%.1f copied, %.1f copies reused, %.1f refused)",
        s.heldRuns / f, s.heldPieces / f, s.batchedRuns / f, s.batchedPieces / f, s.indexRuns / f, s.indexPieces / f,
        s.indexCopied / f, s.indexReused / f, s.indexRefused / f);
    char ops[256];
    int k = 0;
    ops[0] = 0;
    for (int o = 0; o < 256 && k < 200; ++o)
        if (s.breakOp[o]) k += snprintf(ops + k, sizeof ops - k, " op%d:%.1f", o, s.breakOp[o] / f);
    LOG("[gx2]   draws noted for a run: not candidate %.1f, not allowed %.1f; ended by a write %.1f, by an op:%s",
        s.notCandidate / f, s.notAllowed / f, s.breakWrite / f, ops);
    s = {};
}

// The hook (top of execute_one, while any_on()): true when the command was taken into a run (nothing else to do).
// Every other command first draws what is held unless it continues it.
inline bool before_op(Op op, const uint32* p, uint32 n) {
    if (g_inside) return false;
    switch (op) {
    case OP_NOP: case OP_DEBUG_CONTEXT: case OP_CALL: return false;
    case OP_SET_REGS:
        if (g_irun.on && write_changes(p, n)) index_flush();
        if (g_held.on) {
            if (write_breaks(p, n)) held_flush();
        } else if (g_last.valid && write_breaks(p, n)) {
            g_last.valid = false;
            ++g_stats.breakWrite;
        }
        return false;
    case OP_DRAW: case OP_DRAW_INDEXED:
        if (g_irun.on) {
            if (index_continues(op, p, n)) {
                index_add(op, p);
                return true;
            }
            index_flush();
        }
        if (held_join_or_flush(op, p, n)) return true;
        if (index_candidate(op, p, n)) {
            // (g_last stays: the last draw executed; a run of one goes on to the same-state runs when it is drawn)
            IndexRun& r = g_irun;
            r.on = true, r.op = op, r.n = n;
            memcpy(r.args, p, n * 4);
            r.pieces = r.total = 0;
            r.contiguous = true;
            index_add(op, p);
            return true;
        }
        if (held_take(op, p, n)) return true;
        return false;  // (executed as usual; g_last notes it)
    default:
        flush_all();
        if (g_last.valid) ++g_stats.breakOp[op & 0xFF];
        g_last.valid = false;
        if (op == OP_SWAP) log_stats();
        return false;
    }
}

}  // namespace draw_runs
