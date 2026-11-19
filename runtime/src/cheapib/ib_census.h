// ib_census: measurement only (cheapib design, 2026-11-16; off unless WWHD_IB_CENSUS is set). For each pass in a
// window it walks the command stream the game hands to the render thread (top level and every called display list,
// recursively, read from guest memory at the call) and writes one line per draw: its structure key and hashes of the
// state it changed since the previous draw (render-state registers and texture words; ALU constants), of the
// contents of the uniform blocks it has bound, and of its vertex and index data. Addresses are left out of the state
// hashes (ring slices move between passes): only contents count. Offline, cheapib/tools/ib_diff.py compares an
// in-between pass with its step's logic pass draw by draw.
//   WWHD_IB_CENSUS=path  WWHD_IB_CENSUS_FROM=first pass (interp pass count, default 600)  WWHD_IB_CENSUS_N=passes (24)
// Included by gx2_core.cpp only (uses its snapshot_readable); game threads, under its own mutex.
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "cheapib.h"

namespace interp {
bool hold_pass();
bool record_pass();
float pass_t();
uint64_t logic_steps();
uint64_t pass_count();
}  // namespace interp

namespace gx2 {
namespace ib_census {

inline uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h *= 0xFF51AFD7ED558CCDull;
    return h ^ (h >> 33);
}
inline uint64_t hash_bytes(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull ^ n;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) { uint64_t v; memcpy(&v, p + i, 8); h = mix(h, v); }
    uint64_t t = 0;
    for (size_t k = 0; i < n; ++i, ++k) t |= uint64_t(p[i]) << (8 * k);
    return mix(h, t);
}

struct State {
    bool on = false, active = false;
    FILE* out = nullptr;
    uint64_t from = 600, count = 24, done = 0;
    std::mutex mu;
    // shadow of the registers that matter here
    uint32_t ub[3][16][2] = {};     // stage, slot: address, size
    uint32_t ubEpoch[3][16] = {};   // program epoch the slot was bound in
    uint32_t attr[16][2] = {};
    uint32_t attrEpoch[16] = {};
    uint32_t prog[3] = {};          // PS, VS, FS starts
    uint32_t progEpoch = 1;
    uint64_t hState = 0, hAlu = 0;  // since the previous draw
    uint32_t nState = 0, nAlu = 0;
    // per pass
    std::vector<char> buf;
    uint64_t items = 0, calls = 0, words = 0, listBytes = 0, topWords = 0, maxDepth = 0;
    uint64_t invCount[16] = {}, invBytes[16] = {};
    std::unordered_map<uint64_t, uint64_t> memo;  // (addr << 32 | size) -> content hash, this pass
    uint64_t ubBytes = 0, vtxBytes = 0;
    // WWHD_CHEAPIB_VERIFY (a second State, vst()): the draws of the current window, each with its uniform blocks
    bool verify = false;
    struct Block { uint8_t slot; uint32_t addr, size, declared; };  // declared: the shader's size for it (0 unknown)
    struct Draw { uint32_t op; uint32_t first, count; };  // blocks[first, first + count)
    std::vector<Draw> draws;
    std::vector<Block> blocks;
};
inline State& st() { static State s; return s; }
inline State& vst() { static State s; s.verify = true; return s; }

inline void init() {
    static std::once_flag once;
    std::call_once(once, [] {
        State& s = st();
        const char* p = getenv("WWHD_IB_CENSUS");
        if (!p) return;
        s.out = fopen(p, "w");
        if (!s.out) return;
        if (const char* e = getenv("WWHD_IB_CENSUS_FROM")) s.from = strtoull(e, nullptr, 10);
        if (const char* e = getenv("WWHD_IB_CENSUS_N")) s.count = strtoull(e, nullptr, 10);
        s.on = true;
    });
}

template <class Readable>
inline uint64_t content(State& s, uint32_t addr, uint32_t size, Readable readable, uint64_t* bytes) {
    if (!addr || !size || size > (64u << 20)) return 0;
    const uint64_t key = (uint64_t(addr) << 32) | size;
    auto it = s.memo.find(key);
    if (it != s.memo.end()) return it->second;
    uint64_t h = 1;
    if (readable(addr, size)) { h = hash_bytes((const uint8_t*)mem::ptr(addr), size); *bytes += size; }
    s.memo.emplace(key, h);
    return h;
}

inline void put(State& s, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
inline void put(State& s, const char* fmt, ...) {
    if (s.verify) return;
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    const int k = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (k > 0) s.buf.insert(s.buf.end(), tmp, tmp + std::min<int>(k, sizeof tmp - 1));
}

// register writes: classify, shadow the bindings
inline void regs(State& s, uint32_t first, const uint32_t* v, uint32_t count) {
    static const uint32_t ubStart[3] = {mmSQ_VTX_UNIFORM_BLOCK_START, mmSQ_PS_UNIFORM_BLOCK_START, mmSQ_GS_UNIFORM_BLOCK_START};
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t r = first + i, x = v[i];
        if (r >= mmSQ_ALU_CONSTANT0_0 && r < mmSQ_ALU_CONSTANT0_0 + 0x800) {
            s.hAlu = mix(s.hAlu, (uint64_t(r) << 32) | x);
            s.nAlu++;
            continue;
        }
        bool done = false;
        for (int g = 0; g < 3 && !done; ++g)
            if (r >= ubStart[g] && r < ubStart[g] + 7 * 16) {
                const uint32_t slot = (r - ubStart[g]) / 7, w = (r - ubStart[g]) % 7;
                if (w == 0) { s.ub[g][slot][0] = x; s.ubEpoch[g][slot] = s.progEpoch; }
                else if (w == 1) s.ub[g][slot][1] = x + 1;
                done = true;
            }
        if (done) continue;
        if (r >= mmSQ_VTX_ATTRIBUTE_BLOCK_START && r < mmSQ_VTX_ATTRIBUTE_BLOCK_START + 7 * 16) {
            const uint32_t slot = (r - mmSQ_VTX_ATTRIBUTE_BLOCK_START) / 7, w = (r - mmSQ_VTX_ATTRIBUTE_BLOCK_START) % 7;
            if (w == 0) { s.attr[slot][0] = x; s.attrEpoch[slot] = s.progEpoch; }
            else if (w == 1) s.attr[slot][1] = x + 1;
            else s.hState = mix(s.hState, (uint64_t(r) << 32) | x);  // stride words
            continue;
        }
        if (r == mmSQ_PGM_START_FS || r == mmSQ_PGM_START_FS + 1) {  // fetch shader: an address (it moves), not state
            if (r == mmSQ_PGM_START_FS) { s.prog[2] = x; s.progEpoch++; }
            continue;
        }
        if (r == mmSQ_PGM_START_PS || r == mmSQ_PGM_START_VS) {
            s.prog[r == mmSQ_PGM_START_PS ? 0 : 1] = x;
            s.progEpoch++;
        }
        s.hState = mix(s.hState, (uint64_t(r) << 32) | x);
        s.nState++;
    }
}

template <class Readable>
inline void draw_item(State& s, uint32_t op, uint64_t skey, uint64_t extra, uint32_t indexAddr, uint32_t indexBytes,
                      Readable readable) {
    if (s.verify) {
        State::Draw d{op, (uint32_t)s.blocks.size(), 0};
        for (int g = 0; g < 3; ++g)
            for (int k = 0; k < 16; ++k)
                if (s.ub[g][k][0] && s.ubEpoch[g][k] + 1 >= s.progEpoch) {
                    // the size the bound program declares for the block (GX2UniformBlock): the bytes it can read
                    const uint32_t declared = g < 2 ? cheapib::declared_block_size(s.prog[g == 0 ? 1 : 0], g == 1, k) : 0;
                    s.blocks.push_back({uint8_t(g * 16 + k), s.ub[g][k][0], s.ub[g][k][1], declared});
                    d.count++;
                }
        s.draws.push_back(d);
        s.hState = s.hAlu = 0;
        s.nState = s.nAlu = 0;
        (void)skey; (void)extra; (void)indexAddr; (void)indexBytes; (void)readable;
        return;
    }
    uint64_t ubh = 0, vh = 0, ubSize = 0, vSize = 0;
    uint32_t ubn = 0;
    char tok[1024];
    int tk = 0;
    auto token = [&](char kind, uint32_t slot, uint32_t size, uint64_t h) {
        if (tk < (int)sizeof tok - 40) tk += snprintf(tok + tk, sizeof tok - tk, "%s%c%u:%u:%08x", tk ? "," : "", kind, slot, size, uint32_t(h ^ (h >> 32)));
    };
    for (int g = 0; g < 3; ++g)
        for (int k = 0; k < 16; ++k)
            if (s.ub[g][k][0] && s.ubEpoch[g][k] + 1 >= s.progEpoch) {  // (bound for this program or the one before)
                const uint64_t c = content(s, s.ub[g][k][0], s.ub[g][k][1], readable, &s.ubBytes);
                ubh = mix(ubh, mix(uint64_t(g * 16 + k), c));
                token('u', g * 16 + k, s.ub[g][k][1], c);
                ubSize += s.ub[g][k][1];
                ubn++;
            }
    for (int k = 0; k < 16; ++k)
        if (s.attr[k][0] && s.attrEpoch[k] + 1 >= s.progEpoch) {
            const uint64_t c = content(s, s.attr[k][0], s.attr[k][1], readable, &s.vtxBytes);
            vh = mix(vh, mix(uint64_t(k), c));
            token('v', k, s.attr[k][1], c);
            vSize += s.attr[k][1];
        }
    if (indexAddr && indexBytes) {
        const uint64_t c = content(s, indexAddr, indexBytes, readable, &s.vtxBytes);
        vh = mix(vh, c);
        vSize += indexBytes;
        token('i', 0, indexBytes, c);
    }
    if (!tk) tok[tk++] = '-';
    tok[tk] = 0;
    const uint64_t key = mix(mix(skey, s.prog[0]), s.prog[1]);
    // op key state alu ub vtx nState nAlu ubCount ubSize vtxSize
    put(s, "%x %016llx %016llx %016llx %016llx %016llx %u %u %u %llu %llu ", op, (unsigned long long)key,
        (unsigned long long)mix(s.hState, extra), (unsigned long long)s.hAlu, (unsigned long long)ubh, (unsigned long long)vh,
        s.nState, s.nAlu, ubn, (unsigned long long)ubSize, (unsigned long long)vSize);
    s.buf.insert(s.buf.end(), tok, tok + tk);
    s.buf.push_back('\n');
    s.items++;
    s.hState = s.hAlu = 0;
    s.nState = s.nAlu = 0;
}

template <class Readable>
inline void walk(State& s, uint32_t op, const uint32_t* p, uint32_t n, int depth, Readable readable);

#ifdef WWHD_NATIVE_DRAW
// the verify: a described J3D draw's programs (SQ_PGM_START_PS / VS of its program segment and the lists it calls,
// as the define captured them) into s.prog, for the declared block sizes
inline void desc_programs(State& s, uint32_t id, uint32_t version) {
    using namespace native_draw::j3ddesc;
    static std::unordered_map<const Desc*, std::pair<uint32_t, uint32_t>> memo;  // (under the verify's mutex)
    const Desc* d = find_desc(id, version);
    if (!d) return;
    auto it = memo.find(d);
    if (it == memo.end()) {
        std::pair<uint32_t, uint32_t> pr{0, 0};
        const uint8_t* lists = d->lists.data();
        const uint8_t* end = lists + d->lists.size();
        auto scan = [&](auto&& self, const uint32_t* w, uint32_t nw, int depth) -> void {
            for (uint32_t i = 0; i < nw;) {
                const uint32_t op = w[i] & 0xFF, m = w[i] >> 8;
                if (i + 1 + m > nw) return;
                if (op == OP_SET_REGS && m >= 1) {
                    for (uint32_t k = 1; k < m; ++k) {
                        const uint32_t r = w[i + 1] + k - 1;
                        if (r == mmSQ_PGM_START_PS) pr.first = w[i + 1 + k];
                        if (r == mmSQ_PGM_START_VS) pr.second = w[i + 1 + k];
                    }
                } else if (op == OP_CALL && m == 2 && depth < 4 && lists + w[i + 2] <= end) {
                    std::vector<uint32_t> copy(w[i + 2] / 4);
                    memcpy(copy.data(), lists, copy.size() * 4);
                    lists += w[i + 2];
                    self(self, copy.data(), uint32_t(copy.size()), depth + 1);
                }
                i += 1 + m;
            }
        };
        scan(scan, d->segment[kSegProgram].data(), uint32_t(d->segment[kSegProgram].size()), 0);
        it = memo.emplace(d, pr).first;
    }
    if (it->second.first) s.prog[0] = it->second.first;
    if (it->second.second) s.prog[1] = it->second.second;
}
#endif

template <class Readable>
inline void walk_list(State& s, uint32_t addr, uint32_t bytes, int depth, Readable readable) {
    if (depth > 8 || !readable(addr, bytes)) return;
    s.calls++;
    s.listBytes += bytes;
    if ((uint64_t)depth > s.maxDepth) s.maxDepth = depth;
    const uint32_t* w = (const uint32_t*)mem::ptr(addr);
    const uint32_t nw = bytes / 4;
    if (depth == 1) put(s, "L %u\n", bytes);  // a list called from the stream (one agl command): its draws follow
    for (uint32_t i = 0; i < nw;) {
        const uint32_t op = w[i] & 0xFF, n = w[i] >> 8;
        if (i + 1 + n > nw) break;
        walk(s, op, w + i + 1, n, depth, readable);
        i += 1 + n;
    }
    if (depth == 1) put(s, "l\n");
}

template <class Readable>
inline void walk(State& s, uint32_t op, const uint32_t* p, uint32_t n, int depth, Readable readable) {
    s.words += 1 + n;
    switch (op) {
    case OP_SET_REGS:
        if (n) regs(s, p[0], p + 1, n - 1);
        return;
    case OP_SET_PROJ_REGS:
        if (n) { for (uint32_t i = 1; i < n; ++i) s.hAlu = mix(s.hAlu, p[i]); s.nAlu += n - 1; }
        return;
    case OP_CALL:
        if (n >= 2) walk_list(s, p[0], p[1], depth + 1, readable);
        return;
    case OP_INVALIDATE:
        if (n >= 3) { const uint32_t k = p[0] & 0x4F ? (p[0] & 0x40 ? 8 | (p[0] & 7) : p[0] & 7) : 15; s.invCount[k & 15]++; s.invBytes[k & 15] += p[2]; }
        return;
    case OP_DRAW:
        draw_item(s, op, mix(op, n ? p[0] : 0), n >= 2 ? p[1] : 0, 0, 0, readable);
        return;
    case OP_DRAW_INDEXED: {
        if (n < 4) return;
        const uint32_t type = p[2], isz = (type == 1 || type == 9) ? 4 : 2;
        draw_item(s, op, mix(mix(op, p[0]), type), mix(p[1], n >= 5 ? p[4] : 0), p[3], p[1] * isz, readable);
        return;
    }
#ifdef WWHD_NATIVE_DRAW
    case OP_PACKET_DRAW: {
        using namespace native_draw;
        if (n < kDrawHead) return;
        const uint32_t kind = p[1], blocks = p[2] & 0xFF, hasVertex = (p[2] >> 8) & 1, noFetch = (p[2] >> 9) & 1,
                       buffers = (p[2] >> 12) & 0xF, hasRange = (p[2] >> 16) & 1, regRanges = (p[2] >> 20) & 0xF;
        static const uint32_t stageBase[3] = {mmSQ_VTX_UNIFORM_BLOCK_START, mmSQ_PS_UNIFORM_BLOCK_START, mmSQ_GS_UNIFORM_BLOCK_START};
        const uint32_t* q = p + kDrawHead;
        const uint32_t* end = p + n;
        for (uint32_t i = 0; i < blocks && q + kBlockWords <= end; ++i, q += kBlockWords) {
            const uint32_t stage = q[0] >> 16, location = q[0] & 0xFFFF;
            const uint32_t w[2] = {q[1], q[2] - 1};
            if (stage < 3 && location < 16) regs(s, stageBase[stage] + location * 7, w, 2);
        }
        if (hasVertex) {
            if (!noFetch && q + kVertexHead <= end) { regs(s, mmSQ_PGM_START_FS, q, 1); q += kVertexHead; }
            for (uint32_t i = 0; i < buffers && q + kBufferWords <= end; ++i, q += kBufferWords) {
                const uint32_t w[3] = {q[1], q[2] - 1, (q[3] & 0xFFFF) << 11};
                if (q[0] < 16) regs(s, mmSQ_VTX_ATTRIBUTE_BLOCK_START + q[0] * 7, w, 3);
            }
        }
        uint32_t idxCount = 0, idxAddr = 0;
        if (hasRange && q + kRangeWords <= end) { idxCount = q[0]; idxAddr = q[1]; q += kRangeWords; }
        for (uint32_t i = 0; i < regRanges && q + kRegHead <= end; ++i) {
            const uint32_t words = q[1];
            for (uint32_t k = 0; k < words && q + kRegHead + k < end; ++k) s.hAlu = mix(s.hAlu, (uint64_t(q[0]) << 32) ^ q[kRegHead + k]);
            s.nAlu += words;
            q += kRegHead + words;
        }
        if (kind == kNoDraw) return;  // (its effects only)
        // (the index size: the kind's; 2 bytes assumed, a content hash either way)
        draw_item(s, op, mix(mix(op, p[0]), kind), 0, idxAddr, idxCount * 2, readable);
        return;
    }
    case OP_J3D_DESC_DRAW: {
        using namespace native_draw::j3ddesc;
        if (n < kDrawHeaderWords) return;
        const uint32_t blocks = p[6];
        if (s.verify) desc_programs(s, p[0], p[1]);  // (its program, for the declared block sizes)
        static const uint32_t stageBase[3] = {mmSQ_VTX_UNIFORM_BLOCK_START, mmSQ_PS_UNIFORM_BLOCK_START, mmSQ_GS_UNIFORM_BLOCK_START};
        s.progEpoch++;  // (its blocks alone count for it)
        for (uint32_t i = 0; i < blocks && kDrawHeaderWords + 3 * i + 3 <= n; ++i) {
            const uint32_t* q = p + kDrawHeaderWords + 3 * i;
            const uint32_t stage = q[0] >> 16, location = q[0] & 0xFFFF;
            const uint32_t w[2] = {q[1], q[2] - 1};
            if (stage < 3 && location < 16) regs(s, stageBase[stage] + location * 7, w, 2);
        }
        // key: desc id, env id, mesh id; state: the versions and flags (the flag bits of a (re)definition masked)
        const uint64_t key = mix(mix(mix(op, p[0]), p[2]), p[4]);
        draw_item(s, op, key, mix(mix(p[1], p[3]), p[5] & ~uint32_t(kFlagDefined | kFlagEnvMade)), 0, 0, readable);
        return;
    }
    case OP_PACKET_DEFINE:
        s.hState = mix(s.hState, hash_bytes((const uint8_t*)p, 4 * n));
        s.nState++;
        return;
#endif
    case OP_SWAP:
    case OP_DRAW_DONE:
    case OP_FLUSH:
    case OP_SUBMITTED_UNIFORMS:
    case OP_DEBUG_CONTEXT:
    case OP_FENCE:
        return;
    default:  // clears, copies, scan-outs, contexts, pass descriptions, layout ops: structure
        put(s, "x%x %u\n", op, n);
        s.hState = mix(s.hState, hash_bytes((const uint8_t*)p, 4 * n));
        s.nState++;
        return;
    }
}

template <class Readable>
inline void end_pass(State& s) {
    const uint64_t pass = interp::pass_count();
    fprintf(s.out, "P %llu step %llu hold %d record %d t %.4f items %llu calls %llu words %llu top %llu listbytes %llu depth %llu ubbytes %llu vtxbytes %llu inv",
            (unsigned long long)pass, (unsigned long long)interp::logic_steps(), (int)interp::hold_pass(), (int)interp::record_pass(),
            interp::pass_t(), (unsigned long long)s.items, (unsigned long long)s.calls, (unsigned long long)s.words,
            (unsigned long long)s.topWords, (unsigned long long)s.listBytes, (unsigned long long)s.maxDepth,
            (unsigned long long)s.ubBytes, (unsigned long long)s.vtxBytes);
    for (int k = 0; k < 16; ++k)
        if (s.invCount[k]) fprintf(s.out, " %x:%llu:%llu", k, (unsigned long long)s.invCount[k], (unsigned long long)s.invBytes[k]);
    fputc('\n', s.out);
    fwrite(s.buf.data(), 1, s.buf.size(), s.out);
    fputs("E\n", s.out);
    fflush(s.out);
    s.buf.clear();
    s.items = s.calls = s.words = s.listBytes = s.topWords = s.maxDepth = s.ubBytes = s.vtxBytes = 0;
    memset(s.invCount, 0, sizeof s.invCount);
    memset(s.invBytes, 0, sizeof s.invBytes);
    s.memo.clear();
}

// gx2_core.cpp enqueue / enqueue_regs (top-level stream, game threads)
// WWHD_CHEAPIB_VERIFY (cheapib stage 1, Mac): the painter's draws of a verified step's passes. A window opens at a
// pass's swap (its painter follows) and is closed by cheapib at the next pass's entry, before any patch of that pass:
// the logic pass's window keeps its draws and the contents of their blocks; each in-between window (drawn in full on
// a verified step, with the patch written into the logic pass's slots right after the frame) compares, draw by draw,
// what the full pass bound with what the logic pass's blocks hold now (patched).
namespace verify {
struct V {
    std::mutex mu;
    int kind = -1;  // the open window's pass: 0 logic, 1..3 in-between, -1 none
    std::vector<State::Draw> logic;
    std::vector<State::Block> logicBlocks;
    std::vector<uint32_t> logicBytesAt;  // per logic block: offset into logicBytes
    std::vector<uint8_t> logicBytes;     // each logic block's bytes (its span) at the close of the logic window
    // the patched view: what a replayed in-between binds = the patch run on the logic bytes (patch_begin/patch_end).
    // Read live, the logic slots would also show what the verified full in-between itself wrote over them (its
    // command-pool slice and uniform rings reuse logic memory), which a replay never does.
    std::vector<uint8_t> patchPre, patchedBytes;
    bool patchedValid = false;
    std::vector<uint64_t> logicHash, logicPre;  // per logic block, at the close of its window (whole, first 64 B)
    // counters per class: [op kind: 0 j3ddesc, 1 packet, 2 other][class]
    // classes: 0 unchanged (full = logic = patched), 1 fixed (full != logic, patched = full), 2 missed (full != logic,
    // patched = logic), 3 wrong (patched differs from both), 4 broken (full = logic, patched differs)
    uint64_t cls[4][3][5] = {}, slotCls[64][5] = {}, windows = 0, unaligned = 0;
    uint64_t firstDiff[64][9] = {};
    uint64_t pre[4][3][5] = {};  // as cls, comparing only each block's first 64 bytes
    std::unordered_map<uint32_t, uint64_t> sizes;
    std::unordered_map<uint32_t, uint64_t> spans;  // j3d: (slot << 24 | bound << 12 | declared) -> blocks compared
    uint64_t declared[2] = {};                     // blocks compared: declaration unknown (bound size), known  // (slot << 24 | size) -> blocks seen (j3d)  // per slot, j3d draws, patched != full: first differing word (0-7, 8-15, ... 56-63, >= 64)
    int detail = getenv("WWHD_CHEAPIB_VERIFY_DETAIL") ? atoi(getenv("WWHD_CHEAPIB_VERIFY_DETAIL")) : 0;
};
inline V& v() { static V x; return x; }
inline uint64_t hash_at(uint32_t a, uint32_t n) { return a && n && n < (1u << 24) ? hash_bytes((const uint8_t*)mem::ptr(a), n) : 0; }
// the bytes of a block the draw can read: the program's declared size within the bound range (the bound range when the
// declaration is unknown; bound ranges can run into the next object's bytes, which the program never reads)
inline uint32_t span(const State::Block& b) { return b.declared ? std::min(b.declared, b.size) : b.size; }
inline void close() {
    V& x = v();
    State& s = vst();
    std::lock_guard<std::mutex> lk(x.mu);
    if (x.kind < 0) return;
    if (x.kind == 0) {
        x.logic = s.draws;
        x.logicBlocks = s.blocks;
        x.logicHash.resize(x.logicBlocks.size());
        x.logicPre.resize(x.logicBlocks.size());
        {
            x.logicBytes.clear();
            x.logicBytesAt.resize(x.logicBlocks.size());
            for (size_t i = 0; i < x.logicBlocks.size(); ++i) {
                x.logicBytesAt[i] = (uint32_t)x.logicBytes.size();
                const uint32_t n = std::min<uint32_t>(span(x.logicBlocks[i]), 4096);
                const uint8_t* b = (const uint8_t*)mem::ptr(x.logicBlocks[i].addr);
                x.logicBytes.insert(x.logicBytes.end(), b, b + n);
            }
            x.patchedValid = false;
        }
        for (size_t i = 0; i < x.logicBlocks.size(); ++i) {
            x.logicHash[i] = hash_at(x.logicBlocks[i].addr, span(x.logicBlocks[i]));
            x.logicPre[i] = hash_at(x.logicBlocks[i].addr, std::min<uint32_t>(x.logicBlocks[i].size, 64));
        }
    } else if (!x.logic.empty()) {
        x.windows++;
        bool same = s.draws.size() == x.logic.size();
        for (size_t i = 0; same && i < s.draws.size(); ++i)
            same = s.draws[i].op == x.logic[i].op && s.draws[i].count == x.logic[i].count;
        if (!same) x.unaligned++;
        for (size_t i = 0; same && i < s.draws.size(); ++i) {
            const State::Draw& f = s.draws[i];
            const State::Draw& l = x.logic[i];
            const int ok = f.op == 0x50 ? 0 : f.op == 0x41 ? 1 : 2;
            int worst = 0, worstPre = 0;
            for (uint32_t k = 0; k < f.count; ++k) {
                const State::Block& fb = s.blocks[f.first + k];
                const State::Block& lb = x.logicBlocks[l.first + k];
                if (fb.slot != lb.slot || fb.size != lb.size || span(fb) != span(lb)) { worst = std::max(worst, 3); continue; }
                const uint32_t sp = span(fb);
                x.declared[fb.declared ? 1 : 0]++;
                if (f.op == 0x50) x.spans[(uint32_t(fb.slot) << 24) | (std::min<uint32_t>(fb.size, 0xFFF) << 12) | std::min<uint32_t>(fb.declared, 0xFFF)]++;
                // the patched view (patch_end); without it (no patch ran) the logic bytes themselves
                const uint32_t at = x.logicBytesAt[l.first + k], sn = std::min<uint32_t>(sp, 4096);
                const uint8_t* pv = (x.patchedValid ? x.patchedBytes.data() : x.logicBytes.data()) + at;
                const uint64_t hf = hash_at(fb.addr, sn), hl = hash_bytes(x.logicBytes.data() + at, sn), hp = hash_bytes(pv, sn);
                const int c = hf == hl ? (hp == hf ? 0 : 4) : hp == hf ? 1 : hp == hl ? 2 : 3;
                {
                    const uint32_t n64 = std::min<uint32_t>(fb.size, 64);
                    const uint64_t pf = hash_at(fb.addr, n64), pl = x.logicPre[l.first + k], pp = hash_bytes(pv, std::min(n64, sn));
                    const int pc = pf == pl ? (pp == pf ? 0 : 4) : pp == pf ? 1 : pp == pl ? 2 : 3;
                    worstPre = pc == 1 && worstPre == 0 ? 1 : std::max(worstPre, pc);
                    if (f.op == 0x50) x.sizes[(uint32_t(fb.slot) << 24) | fb.size]++;
                }
                x.slotCls[fb.slot & 63][c]++;
                if (c >= 2 && f.op == 0x50) {
                    const uint32_t* wf = (const uint32_t*)mem::ptr(fb.addr);
                    const uint32_t* wp = (const uint32_t*)pv;
                    uint32_t w = 0;
                    while (w < sn / 4 && wf[w] == wp[w]) ++w;
                    x.firstDiff[fb.slot & 63][std::min<uint32_t>(w / 8, 8)]++;
                }
                // (debug WWHD_CHEAPIB_VERIFY_DETAIL_SLOTS=mask: only those j3d slots; their words in one line, no dump)
                static const uint64_t dslots = getenv("WWHD_CHEAPIB_VERIFY_DETAIL_SLOTS") ? strtoull(getenv("WWHD_CHEAPIB_VERIFY_DETAIL_SLOTS"), nullptr, 0) : 0;
                // (packet draws too: "packet"; each word full/logic/patched)
                if (dslots && c >= 2 && x.detail > 0 && (f.op == 0x50 || f.op == 0x41)) {
                    if (dslots >> (fb.slot & 63) & 1) {
                        x.detail--;
                        const uint32_t* wf = (const uint32_t*)mem::ptr(fb.addr);
                        const uint32_t* wp = (const uint32_t*)pv;
                        const uint32_t* wl = (const uint32_t*)(x.logicBytes.data() + at);
                        char ln[900];
                        int q = 0, nd = 0;
                        for (uint32_t w = 0; w < sn / 4; ++w)
                            if (wf[w] != wp[w] && nd++ < 12 && q < 780)
                                q += snprintf(ln + q, sizeof ln - q, " w%u %08X/%08X/%08X", w, __builtin_bswap32(wf[w]),
                                              __builtin_bswap32(wl[w]), __builtin_bswap32(wp[w]));
                        LOG("[cheapib] verify slotdetail: in-between %d %s slot %u full %08X logic %08X %s %d words (full/logic/patched):%s",
                            x.kind, f.op == 0x50 ? "j3d" : "packet", fb.slot, fb.addr, lb.addr,
                            c == 2 ? "missed" : c == 3 ? "wrong" : "broken", nd, q ? ln : "");
                    }
                } else if (c >= 2 && x.detail > 0 && f.op == 0x50) {
                    x.detail--;
                    // the words the patch got wrong: count, first and last index, the first one's three values
                    const uint32_t* wf = (const uint32_t*)mem::ptr(fb.addr);
                    const uint32_t* wp = (const uint32_t*)pv;
                    int nd = 0, first = -1, last = -1;
                    for (uint32_t w = 0; w < sn / 4; ++w)
                        if (wf[w] != wp[w]) { nd++; if (first < 0) first = w; last = w; }
                    LOG("[cheapib] verify: in-between %d draw %zu (j3d) slot %u (%u of %u B, full %08X, logic %08X): %s; %d words differ "
                        "(%d..%d), first: full %08X patched %08X", x.kind, i, fb.slot, sp, fb.size, fb.addr, lb.addr,
                        c == 2 ? "missed" : c == 3 ? "wrong" : "broken", nd, first, last, first >= 0 ? __builtin_bswap32(wf[first]) : 0,
                        first >= 0 ? __builtin_bswap32(wp[first]) : 0);
                    if (!x.logicBytesAt.empty()) {
                        const uint32_t* wl = (const uint32_t*)(x.logicBytes.data() + x.logicBytesAt[l.first + k]);
                        for (uint32_t w = 0; w < std::min<uint32_t>(sn / 4, 64); w += 4) {
                            char ln[256];
                            int q = snprintf(ln, sizeof ln, "  w%2u", w);
                            for (const uint32_t* src : {wl, wf, wp}) {
                                q += snprintf(ln + q, sizeof ln - q, " |");
                                for (int e = 0; e < 4; ++e) q += snprintf(ln + q, sizeof ln - q, " %08X", __builtin_bswap32(src[w + e]));
                            }
                            LOG("[cheapib] verify words (logic | full | patched)%s", ln);
                        }
                    }
                }
                worst = c == 1 && worst == 0 ? 1 : std::max(worst, c);
            }
            x.cls[x.kind & 3][ok][worst]++;
            x.pre[x.kind & 3][ok][worstPre]++;
        }
        if (x.windows % 30 == 0) {
            (void)0;
            static const char* on[3] = {"j3d", "packet", "other"};
            char b[1024];
            int k = snprintf(b, sizeof b, "[cheapib] verify (strict: declared block sizes; %llu blocks with the bound size, declaration "
                             "unknown, %llu declared) %llu in-betweens (%llu unaligned), draws unchanged/fixed/missed/wrong/broken:",
                             (unsigned long long)x.declared[0], (unsigned long long)x.declared[1],
                             (unsigned long long)x.windows, (unsigned long long)x.unaligned);
            for (int kd = 1; kd < 4; ++kd)
                for (int o = 0; o < 2; ++o)
                    k += snprintf(b + k, sizeof b - k, " %d:%s %llu/%llu/%llu/%llu/%llu;", kd, on[o], (unsigned long long)x.cls[kd][o][0],
                                  (unsigned long long)x.cls[kd][o][1], (unsigned long long)x.cls[kd][o][2], (unsigned long long)x.cls[kd][o][3],
                                  (unsigned long long)x.cls[kd][o][4]);
            k += snprintf(b + k, sizeof b - k, " blocks by slot (fixed/missed/wrong/broken):");
            for (int sl = 0; sl < 64 && k < (int)sizeof b - 60; ++sl)
                if (x.slotCls[sl][1] + x.slotCls[sl][2] + x.slotCls[sl][3] + x.slotCls[sl][4])
                    k += snprintf(b + k, sizeof b - k, " u%d %llu/%llu/%llu/%llu", sl, (unsigned long long)x.slotCls[sl][1],
                                  (unsigned long long)x.slotCls[sl][2], (unsigned long long)x.slotCls[sl][3],
                                  (unsigned long long)x.slotCls[sl][4]);
            LOG("%s", b);
            {
                char h[1024];
                int hk = snprintf(h, sizeof h, "[cheapib] verify, first 64 B of each block only:");
                for (int kd = 1; kd < 4; ++kd)
                    for (int o = 0; o < 2; ++o)
                        hk += snprintf(h + hk, sizeof h - hk, " %d:%s %llu/%llu/%llu/%llu/%llu;", kd, on[o], (unsigned long long)x.pre[kd][o][0],
                                       (unsigned long long)x.pre[kd][o][1], (unsigned long long)x.pre[kd][o][2], (unsigned long long)x.pre[kd][o][3],
                                       (unsigned long long)x.pre[kd][o][4]);
                hk += snprintf(h + hk, sizeof h - hk, " j3d block sizes:");
                for (auto& z : x.sizes) if (hk < (int)sizeof h - 40) hk += snprintf(h + hk, sizeof h - hk, " u%u:%u x%llu", z.first >> 24, z.first & 0xFFFFFF, (unsigned long long)z.second);
                LOG("%s", h);
            }
            {
                char h[1536];
                int hk = snprintf(h, sizeof h, "[cheapib] verify: j3d blocks compared, slot:bound:declared x count:");
                for (auto& z : x.spans)
                    if (hk < (int)sizeof h - 40)
                        hk += snprintf(h + hk, sizeof h - hk, " u%u:%u:%u x%llu", z.first >> 24, (z.first >> 12) & 0xFFF, z.first & 0xFFF,
                                       (unsigned long long)z.second);
                LOG("%s", h);
            }
            for (int sl = 0; sl < 64; ++sl) {
                uint64_t t = 0;
                for (int q = 0; q < 9; ++q) t += x.firstDiff[sl][q];
                if (!t) continue;
                char h[256];
                int hk = 0;
                for (int q = 0; q < 9; ++q) hk += snprintf(h + hk, sizeof h - hk, " %llu", (unsigned long long)x.firstDiff[sl][q]);
                LOG("[cheapib] verify: j3d slot u%d, patched != full, first differing word by 8s (0-7 ... 56-63, 64+):%s", sl, h);
            }
        }
    }
    if (x.kind > 0) x.patchedValid = false;
    x.kind = -1;
    s.draws.clear();
    s.blocks.clear();
}
// cheapib_models.h replay() around the patch of a verified in-between (after its full frame). A replay would patch the
// logic pass's blocks as the logic pass left them; the verified full in-between has drawn over some of that memory (its
// command-pool slice and uniform rings reuse it). So: the logic bytes go back into place, the patch runs, its result is
// the patched view, and the memory as the full in-between left it is restored (its lists are still to be painted)
inline void patch_begin() {
    V& x = v();
    std::lock_guard<std::mutex> lk(x.mu);
    x.patchPre.resize(x.logicBytes.size());
    for (size_t i = 0; i < x.logicBlocks.size(); ++i) {
        const uint32_t n = std::min<uint32_t>(span(x.logicBlocks[i]), 4096);
        memcpy(x.patchPre.data() + x.logicBytesAt[i], mem::ptr(x.logicBlocks[i].addr), n);
    }
    for (size_t i = 0; i < x.logicBlocks.size(); ++i) {
        const uint32_t n = std::min<uint32_t>(span(x.logicBlocks[i]), 4096);
        memcpy(mem::ptr(x.logicBlocks[i].addr), x.logicBytes.data() + x.logicBytesAt[i], n);
    }
}
inline void patch_end() {
    V& x = v();
    std::lock_guard<std::mutex> lk(x.mu);
    if (x.patchPre.size() != x.logicBytes.size()) return;
    x.patchedBytes.resize(x.logicBytes.size());
    for (size_t i = 0; i < x.logicBlocks.size(); ++i) {
        const uint32_t n = std::min<uint32_t>(span(x.logicBlocks[i]), 4096);
        memcpy(x.patchedBytes.data() + x.logicBytesAt[i], mem::ptr(x.logicBlocks[i].addr), n);
    }
    for (size_t i = 0; i < x.logicBlocks.size(); ++i) {
        const uint32_t n = std::min<uint32_t>(span(x.logicBlocks[i]), 4096);
        memcpy(mem::ptr(x.logicBlocks[i].addr), x.patchPre.data() + x.logicBytesAt[i], n);
    }
    x.patchedValid = true;
}
inline const bool kRegistered = (cheapib::set_verify_close(&close), cheapib::set_verify_patch(&patch_begin, &patch_end), true);
}  // namespace verify

template <class Readable>
inline void top(uint32_t op, const uint32_t* p, uint32_t n, Readable readable) {
    if (cheapib::verify_on()) {
        verify::V& x = verify::v();
        State& vs = vst();
        std::lock_guard<std::mutex> lk(x.mu);
        if (op == OP_SWAP) {
            x.kind = cheapib::verify_kind();
            vs.draws.clear();
            vs.blocks.clear();
        } else if (x.kind >= 0) {
            walk(vs, op, p, n, 0, readable);
        }
    }
    init();
    State& s = st();
    if (!s.on) return;
    std::lock_guard<std::mutex> lk(s.mu);
    const uint64_t pass = interp::pass_count();
    if (!s.active) {
        if (pass < s.from || s.done >= s.count || op != OP_SWAP) return;
        s.active = true;  // from the swap that ends the pass before the first one
        s.buf.clear();
        s.memo.clear();
        return;
    }
    s.topWords += 1 + n;
    walk(s, op, p, n, 0, readable);
    if (op == OP_SWAP) {
        end_pass<Readable>(s);
        if (++s.done >= s.count) { s.active = false; s.on = false; fclose(s.out); s.out = nullptr; }
    }
}
template <class Readable>
inline void top_regs(uint32_t first, const uint32_t* v, uint32_t count, Readable readable) {
    if (cheapib::verify_on()) {
        std::lock_guard<std::mutex> lk(verify::v().mu);
        if (verify::v().kind >= 0) regs(vst(), first, v, count);
    }
    State& s = st();
    if (!s.on || !s.active) return;
    std::lock_guard<std::mutex> lk(s.mu);
    if (!s.active) return;
    s.topWords += 2 + count;
    s.words += 2 + count;
    regs(s, first, v, count);
    (void)readable;
}

}  // namespace ib_census
}  // namespace gx2
