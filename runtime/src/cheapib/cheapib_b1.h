// cheapib b1: the render side of a replayed in-between (decomp build, fast mode; cheapib/DESIGN.md §2.2 b1).
// Included once, at the end of gfx/vulkan/backend.cpp (the renderer's flags and headers; game-thread part too). Switch WWHD_CHEAPIB_B1=1, with WWHD_CHEAPIB=1 (stage 1) and the record worker
// (WWHD_VK_RECORD_WORKER=1: every command goes through its POD stream); otherwise off.
//
// A replayed in-between's painter calls the logic pass's display lists again; today the render thread executes them
// again in full (register decode, draw decisions, uploads, descriptor sets, the commands). b1 executes them once:
// - logic pass (render thread): each top-level call is captured as a segment: the POD commands appended to the
//   producer stream while it executes (pod_commands.cpp), the uniform upload slices filled from the call's submitted
//   blocks (draw.cpp snapshot(): slice, block address, offset in the block), and the renderer's pass state after it.
//   Pending packet batches are drawn at the end of each call, so a segment holds all of its commands;
// - replayed in-between: a top-level call whose segment matches (same step, index, list and size; the logic pass's
//   submission slot not reused yet) is not executed: the segment's uniform blocks are uploaded again from this call's
//   snapshot (the patched model and camera blocks of this pass) into new slices of the logic slot's upload blocks (the
//   logic pass's descriptor sets bind those buffers), and its commands are appended again: the command buffer is this
//   frame's, the dynamic uniform offsets moved to the new slices; descriptor-set definitions (tokens of the logic
//   slot, still defined) and timestamp queries are left out. The pass state is set to the logic pass's after it.
// - anything else executes as today (counted): a segment that submitted, bound a pipeline future, or changed slot.
// Game thread: cheapib::drop_call tags every top-level call (kind of pass, logic step, index in the painter window);
// the render thread takes the tags in the same order.
// Counter line "[cheapib] b1 ..." every 1200 frames (render thread).
#pragma once
#include "cheapib.h"
#include "render_replay.h"
#include "../interp.h"
#include "../runtime.h"
#if defined(WWHD_HAS_VULKAN) && defined(WWHD_NATIVE_DRAW)
#include "../gfx/vulkan/backend.h"
#include "../gfx/vulkan/pod_commands.h"
#include "../gfx/vulkan/record_worker.h"
#include "../gfx/vulkan/record_mt.h"
#include <algorithm>
#include <unordered_set>
#include <memory>
#include <stdexcept>
#include "../gx2/submitted_uniforms.h"
#include "../gx2/gx2_cmd.h"  // (gx2::regs, apply_alu_regs)
#include "../drawoff/vertex_source.h"  // (drawoff c2: a call's vertex copies, WWHD_VERTEX_PTR)
#include "../drawoff/drawoff.h"
#include "../render_prof.h"
#include <cstdlib>
#include <cstring>
#include <bitset>
#include <deque>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
namespace gx2 { const submitted::Snapshot* active_snapshot(); }
namespace native_draw { void flush_mdi(); void flush_batch(); }  // (gx2/native_draw.h)
namespace gfxvk { void cheapib_b1_binds_dirty(); }
#define CHEAPIB_B1_RENDER 1
#endif

namespace cheapib {
bool g_b1_capturing = false;

namespace b1 {
struct Tag {
    int kind = -1;  // 0 logic pass (interpolation on), 2 replayed in-between, -1 other
    uint64_t step = 0;
    uint32_t index = 0, list = 0, size = 0;
};
std::mutex g_tag_mu;
std::deque<Tag> g_tags;

bool requested() {
    static const bool on = [] {
        const char* e = render_replay::env("WWHD_CHEAPIB_B1");  // (render_replay.h)
        if (!e || !atoi(e)) return false;
#ifdef CHEAPIB_B1_RENDER
        // second-thread recording as this start has it (WWHD_VK_RECORD_WORKER, else the saved setting or the device's
        // default: record_worker.h), not only the variable
        if (mode() != 1 || !gfxvk::record_worker_requested()) {
            LOG("[cheapib] WWHD_CHEAPIB_B1=1 needs WWHD_CHEAPIB=1 and second-thread recording (WWHD_VK_RECORD_WORKER=1 or "
                "\"Record graphics on a second thread\"): off");
            return false;
        }
        LOG("[cheapib] b1: replayed in-betweens re-emit the logic pass's recorded commands with the patched uniform blocks");
        return true;
#else
        LOG("[cheapib] WWHD_CHEAPIB_B1=1: not in this build (Vulkan native draw only): off");
        return false;
#endif
    }();
    return on;
}

bool check_on() {
    static const bool on = getenv("WWHD_CHEAPIB_B1_CHECK") && atoi(getenv("WWHD_CHEAPIB_B1_CHECK"));
    return on;
}
#ifdef CHEAPIB_B1_RENDER
using namespace gfxvk;
// a uniform slice filled from a submitted block; or (addr 0, n > 0) a host-packed slice (support uniforms) whose n
// 16-byte entries at Seg::ssrc[at .. at + 3 * n) (offset in the slice, block address, offset in the block) were copied
// from blocks: a replay keeps the logic pass's bytes and rebuilds those entries
struct Rec {
    uint64_t buffer, offset, slice;
    uint32_t size, addr, delta;
    uint32_t at = 0, n = 0;
    uint32_t rat = 0, rn = 0;  // (host-packed: its inherited register entries, Seg::rsrc[rat .. rat + 3 * rn))
    bool hp() const { return n || rn; }
};
// a batch's per-instance entries (draw.cpp packet_flush: 16 bytes each, bound as a vertex stream), each from a block
// (stage 2: refreshed on replays, as the uniform slices are); its sources at Seg::isrc[at .. at + 2 * entries)
struct IRec {
    uint64_t buffer, offset, slice;
    uint32_t entries;
    size_t at;
};
// stage 2 (cheapib_s2_vertex.cpp): a vertex slice holding bytes copied from a CPU-written vertex site's guest range
// (the sea grid); its segments at Seg::vsrc[at .. at + 3 * n) (offset in the slice, guest address, size). A replayed
// pass's vertex writes reach guest memory before its calls (OP_CHEAPIB_VTX), so a replay rebuilds the segments from
// guest memory, the rest of the slice from the logic pass's bytes. With drawoff c2 the segments are read where the
// renderers read them (WWHD_VERTEX_PTR: the copy c2 took at this call, which holds the pass's replayed writes,
// cheapib::c2_vtx_patch): the game thread runs a pass ahead and writes the free grid side, the one an earlier
// step's replays read, before the render thread has executed them (the sea went black)
struct VRec {
    uint64_t buffer, offset, slice;
    uint32_t at, n;
};
// the pass state after a call
struct PassState {
    bool rendering, passTracked;
    std::array<Surface*, 8> colors;
    std::array<uint32_t, 8> slices;
    Surface* depth;
    uint32_t depthSlice, width, height;
};
// a surface a call transitions (or checks): its image, layout and use before and after the call
struct Img {
    Surface* s;
    VkImage image, imageAfter;  // (a call may swap retained images between surfaces: feedback copies, draw.cpp)
    VkImageLayout before, after;
    ResourceUse useBefore, useAfter;
};
struct Seg {
    uint32_t list = 0, size = 0;
    PassState begin{};
    std::vector<Img> imgs;
    std::vector<std::byte> cmds;  // (opcode u32, bytes u32, payload) each
    uint32_t ncmds = 0;
    std::vector<Rec> recs;
    std::vector<IRec> irecs;
    std::vector<uint32_t> isrc;  // (block address, offset in the block) per instance entry
    std::vector<VRec> vrecs;     // (stage 2) vertex slices with segments from CPU-written vertex sites
    std::vector<uint32_t> vsrc;  // (offset in the slice, guest address, size) per segment
    std::vector<uint32_t> ssrc;  // (offset in the slice, block address, offset in the block) per host-packed entry
    std::vector<uint32_t> rsrc;  // (offset in the slice, register, words) per inherited register entry (host-packed)
    std::vector<uint32_t> aluNet;  // the ALU constants the call wrote, as they were after it: runs (register, count, values)
    PassState end{};
    uint64_t draws = 0, passes = 0;
    const char* bad = nullptr;  // why it cannot be replayed
    size_t slot0 = 0, slot1 = 0;
    uint64_t gen0 = 0, gen1 = 0;
    std::string subs;  // (debug) where begins/submits fell: command index
    bool renderPass = false;
    // b2 (cheapib_b2.h): the rendering instance open at the segment's start; on b1's path (offsets moved)
    int startInst = -1;
    bool moved = false;
    std::vector<Surface*> written;  // surfaces the call marked GPU-written (in order; repeats folded)
    // WWHD_CHEAPIB_B1_CHECK=1: every upload slice the call allocated (in order) and, for a logic pass's, their bytes
    struct Alloc {
        uint64_t buffer, offset, size;
        void* mapped;
        int kind;  // (render_prof.h Upload)
        size_t at;  // in abytes
    };
    std::vector<Alloc> allocs;
    std::vector<uint8_t> abytes;
    // (check only) per host-packed upload: its noted entries (ssrc[sat .. +3*sn)) and the entries read from guest memory
    // instead of a snapshot block (gsrc[gat .. +2*gn): offset in the slice, guest address)
    struct GNote {
        uint64_t buffer, offset;
        uint32_t sat, sn, gat, gn, rat, rn;
        int who;
    };
    std::vector<GNote> gnotes;
    std::vector<uint32_t> gsrc;
};
struct Cap {
    uint64_t step = ~0ull;
    bool valid = false;
    size_t slot = 0;
    uint64_t gen = 0;              // R.submissionGeneration while the logic pass's slot records (its activation)
    std::vector<Seg> segs;
    int cur = -1;                  // the segment being captured
    uint64_t draws0 = 0, passes0 = 0;
};
Cap g_cap;
Seg* g_into = nullptr;  // the segment the notes go to: the captured one, or the checked call's (WWHD_CHEAPIB_B1_CHECK)
Renderer::Submission* g_held = nullptr;  // the submission slot held for the replays (&R.submissions[g_cap.slot])
// a held slot (a logic pass's) and the slots its replays were appended to (with lastSubmissionSerial at the replay:
// the user's own sealing gives it a higher serial). back() is g_held's while g_held is set; older ones wait for their
// release (their activation or retirement, after their users' retirement)
struct Hold {
    Renderer::Submission* slot;
    uint64_t step;  // (b2: its secondaries' pool)
    std::vector<std::pair<Renderer::Submission*, uint64_t>> users;
};
std::vector<Hold> g_holds;
Tag g_tag;  // the current top-level call's
// host buffers being packed (b1_src_arm .. their upload): base, (offset, block address, offset in the block) per entry
struct Armed {
    const void* base = nullptr;
    std::vector<uint32_t> src;
    std::vector<uint32_t> gsrc;  // (check only: offset in the slice, guest address) of guest-memory reads
    std::vector<uint32_t> rsrc;  // (offset in the slice, register, words) of inherited register reads
    int who = 0;
};
std::array<Armed, 4> g_armed;
std::bitset<kB1AluWords> g_aluWritten;
struct Kept {
    std::vector<uint32_t> src, rsrc;
};
#include "cheapib_b1_fast.h"  // (WWHD_CHEAPIB_B1_FAST: cheaper bookkeeping, same results)
std::unordered_map<const void*, Kept> g_kept;  // supBytes -> the source lists of the packing they were copied from  // the ALU constants the captured (or checked) call has written so far
PtrTable<Kept> g_fkept;  // (fast 8: g_kept, flat)
uint32_t g_armNext = 0;
bool g_frame_replayed = false;
struct Stats {
    uint64_t frames = 0, logicSegs = 0, badSegs = 0, replayed = 0, executed = 0, refused[8] = {}, recs = 0, refreshed = 0,
             fromLogic = 0, remapped = 0, unmapped = 0, ambiguous = 0, cmds = 0, skipped = 0, bytes = 0, replayFrames = 0, activations[8] = {}, badSub = 0, badExt = 0, badSlot = 0, released = 0,
             refused2[4] = {}, srecs = 0, sentries = 0, rentries = 0, rdropped = 0, aluWords = 0, srefreshed = 0, skept = 0, sdropped = 0, irecs = 0, ientries = 0, irefreshed = 0, ikept = 0, iremapped = 0, vrecs = 0, vsegs = 0, vbytes = 0, vnoted = 0, fixups = 0, holdsReleased = 0, userWaits = 0, userRetired = 0, unsealedAtActivation = 0, keptForUser = 0, adoptedSlots = 0, adoptedObjects = 0, skips = 0, badImg = 0, swapped = 0;
    double userWaitNs = 0;
    double ns = 0;
} g_st;
uint64_t g_guest[2][5] = {};  // (debug count: blocks read from guest memory) [logic call, checked call][path]
uint32_t g_guestAddr[2][5][4] = {}, g_guestSize[2][5][4] = {}, g_guestHeld[2][5][4] = {};
uint64_t g_guestWhy[2][3] = {};  // [logic, checked][no snapshot for the call, address not in it, in it but shorter]
const char* kRefused[12] = {"no capture", "other step", "index", "list", "slot reused", "bad segment", "upload space", "stream",
                            "pass state", "image replaced", "layout in a pass", "-"};

// the snapshot's bytes of block ADDR when it holds at least NEED of them (else nullptr): find + block_size, or (fast 1)
// one lookup
// the block a pointer into the snapshot belongs to: locate, or (fast 1) locate_cached
__attribute__((always_inline)) inline bool snap_locate(const gx2::submitted::Snapshot* snap, const void* p, uint32_t& addr, uint32_t& delta) {
    if (!fast(1)) return snap->locate(p, addr, delta);
    const bool r = snap->locate_cached(p, addr, delta);
    if (fast_verify()) {
        uint32_t a2 = 0, d2 = 0;
        const bool r2 = snap->locate(p, a2, d2);
        fv(r == r2 && (!r || (a2 == addr && d2 == delta)), "locate");
    }
    return r;
}
__attribute__((always_inline)) inline const uint8_t* snap_block(const gx2::submitted::Snapshot* snap, uint32_t addr, uint32_t need) {
    if (!snap) return nullptr;
    if (fast(1)) {
        uint32_t held;
        const uint8_t* p = snap->find_held(addr, held);
        p = p && held >= need ? p : nullptr;
        if (fast_verify()) {
            const uint8_t* o = snap->find(addr, 0);
            fv(p == (o && snap->block_size(addr) >= need ? o : nullptr), "snapshot lookup");
        }
        return p;
    }
    const uint8_t* p = snap->find(addr, 0);
    return p && snap->block_size(addr) >= need ? p : nullptr;
}
PassState pass_state() {  // (devel #608: the open pass is gfxvk::pass_state(), R.pass on the render thread)
    const ::gfxvk::PassState& q = ::gfxvk::pass_state();
    return {q.rendering, q.tracked, q.colors, q.slices, q.depth, q.depthSlice, q.width, q.height};
}
void set_pass_state(const PassState& p) {
    ::gfxvk::PassState& q = ::gfxvk::pass_state();
    q.rendering = p.rendering;
    q.tracked = p.passTracked;
    q.colors = p.colors;
    q.slices = p.slices;
    q.depth = p.depth;
    q.depthSlice = p.depthSlice;
    q.width = p.width;
    q.height = p.height;
}
Renderer::UploadBlock* logic_block(uint64_t buffer) {
    if (g_cap.slot >= R.submissions.size()) return nullptr;
    for (auto& b : R.submissions[g_cap.slot].uploadBlocks)
        if (uint64_t(b.buffer.buffer) == buffer) return &b;
    return nullptr;
}
// an instance record's bytes for this pass: each entry from this call's snapshot of its block, else the logic pass's
// bytes (OLD)
void fill_instances(const Seg& s, const IRec& r, const gx2::submitted::Snapshot* snap, const uint8_t* old, uint8_t* dst) {
    const uint32_t* src = s.isrc.data() + r.at;
    for (uint32_t i = 0; i < r.entries; i++) {
        const uint32_t addr = src[2 * i], off = src[2 * i + 1];
        const uint8_t* p = addr ? snap_block(snap, addr, off + 16) : nullptr;
        if (p) {
            memcpy(dst + 16 * i, p + off, 16);
            g_st.irefreshed++;
            static int tr = 40;  // (debug: entries whose bytes change)
            if (tr > 0 && memcmp(dst + 16 * i, old + 16 * i, 16) != 0) {
                tr--;
                uint32_t a4[4], b4[4];
                memcpy(a4, old + 16 * i, 16);
                memcpy(b4, dst + 16 * i, 16);
                LOG("[cheapib] b1 instances: entry %u of %u (block %08X +%u, size %u): logic %08X %08X %08X %08X -> %08X %08X "
                    "%08X %08X", i, r.entries, addr, off, (unsigned)snap->block_size(addr), a4[0], a4[1], a4[2], a4[3], b4[0],
                    b4[1], b4[2], b4[3]);
            }
        } else {
            memcpy(dst + 16 * i, old + 16 * i, 16);
            g_st.ikept++;
        }
    }
    g_st.irecs++;
    g_st.ientries += r.entries;
}
// a vertex record's bytes for this pass: the logic pass's slice (OLD), each segment again from guest memory
void fill_vertices(const Seg& s, const VRec& r, const uint8_t* old, uint8_t* dst) {
    memcpy(dst, old, r.slice);
    const uint32_t* q = s.vsrc.data() + r.at;
    for (uint32_t i = 0; i < r.n; i++, q += 3) {
        memcpy(dst + q[0], WWHD_VERTEX_PTR(q[1], q[2]), q[2]);
        g_st.vbytes += q[2];
    }
    g_st.vrecs++;
    g_st.vsegs += r.n;
}
// render thread, while a call is captured: bytes a batch copied from a vertex site's guest range into a host buffer
// (CONTAINER at OFF); its upload (snapshot -> b1_note_upload) turns them into a VRec's segments
struct VPend {
    const std::vector<uint8_t>* container;
    size_t off;
    uint32_t addr, size;
};
std::vector<VPend> g_vpend;
// a host-packed slice's entries for this pass (DST: the slice, already holding the logic pass's bytes): each from this
// call's snapshot of its block; an entry whose block is not in the snapshot keeps the logic pass's bytes
void fill_sources(const Seg& s, const Rec& r, const gx2::submitted::Snapshot* snap, uint8_t* dst) {
    const uint32_t* q = s.ssrc.data() + r.at;
    for (uint32_t i = 0; i < r.n; i++, q += 3) {
        const uint8_t* p = snap_block(snap, q[1], q[2] + 16);
        if (p) {
            memcpy(dst + q[0], p + q[2], 16);
            g_st.srefreshed++;
        } else {
            g_st.skept++;
        }
    }
    const uint32_t* regs = gx2::regs();
    q = s.rsrc.data() + r.rat;
    for (uint32_t i = 0; i < r.rn; i++, q += 3) memcpy(dst + q[0], regs + q[1], 4ull * q[2]);
    g_st.rentries += r.rn;
    g_st.srecs++;
    g_st.sentries += r.n;
}
// a replayed call's own ALU-constant writes (as they were after its logic execution), applied as executing it would
void apply_alu_net(const Seg& s) {
    for (size_t i = 0; i + 2 <= s.aluNet.size();) {
        const uint32_t first = s.aluNet[i], n = s.aluNet[i + 1];
        gx2::apply_alu_regs(first, s.aluNet.data() + i + 2, n);
        i += 2 + n;
        g_st.aluWords += n;
    }
}
void check_report();             // (WWHD_CHEAPIB_B1_CHECK, below)
void keep_alloc_bytes(Seg& s);
#include "cheapib_b2.h"  // (b2: the render-thread part)
void report() {
    if (g_frame_replayed) g_st.replayFrames++;
    g_frame_replayed = false;
    if (++g_st.frames % 1200) return;
    if (fast_verify())
        LOG("[cheapib] b1 fast verify: %llu compared, %llu differ (mask %u)", (unsigned long long)g_fvChecks,
            (unsigned long long)g_fvBad, fast_mask());
    const double r = g_st.replayed ? double(g_st.replayed) : 1.0;
    std::string refused;
    for (int i = 0; i < 8; i++)
        if (g_st.refused[i]) refused += std::string(", ") + kRefused[i] + " " + std::to_string(g_st.refused[i]);
    for (int i = 0; i < 4; i++)
        if (g_st.refused2[i]) refused += std::string(", ") + kRefused[8 + i] + " " + std::to_string(g_st.refused2[i]);
    LOG("[cheapib] b1 per 1200 frames: logic segments %llu (not replayable %llu); replayed segments %llu, executed %llu%s; "
        "per replayed segment: uniform slices %.1f (refreshed %.1f, kept %.1f), offsets moved %.1f, unmapped %.1f, "
        "ambiguous %.2f, commands %.1f (left out %.1f), %.1f KB; replay CPU %.3f ms per replayed frame",
        (unsigned long long)g_st.logicSegs, (unsigned long long)g_st.badSegs, (unsigned long long)g_st.replayed,
        (unsigned long long)g_st.executed, refused.c_str(), g_st.recs / r, g_st.refreshed / r, g_st.fromLogic / r,
        g_st.remapped / r, g_st.unmapped / r, g_st.ambiguous / r, g_st.cmds / r, g_st.skipped / r, g_st.bytes / r / 1024,
        g_st.ns / 1e6 / std::max<double>(1.0, double(g_st.replayFrames)));
    std::string act;
    for (int i = 0; i < 8; i++)
        if (g_st.activations[i]) act += " " + std::to_string(i) + (i == 7 ? "+" : "") + ":" + std::to_string(g_st.activations[i]);
    if (g_st.srecs || g_st.sdropped || g_st.aluWords)
        LOG("[cheapib] b1 per 1200 frames: host-packed slices rebuilt %llu (%.1f entries each: from this pass's blocks %llu, "
            "the logic pass's bytes %llu); entries not noted (changed after the copy) %llu; inherited register entries %llu "
            "(not noted %llu); ALU constant words applied after replayed calls %llu", (unsigned long long)g_st.srecs,
            g_st.srecs ? double(g_st.sentries) / double(g_st.srecs) : 0.0, (unsigned long long)g_st.srefreshed,
            (unsigned long long)g_st.skept, (unsigned long long)g_st.sdropped, (unsigned long long)g_st.rentries,
            (unsigned long long)g_st.rdropped, (unsigned long long)g_st.aluWords);
    if (g_st.vrecs || g_st.vnoted)
        LOG("[cheapib] b1 per 1200 frames: vertex slices noted %llu, refreshed %llu (%llu segments, %.1f KB)",
            (unsigned long long)g_st.vnoted, (unsigned long long)g_st.vrecs, (unsigned long long)g_st.vsegs,
            double(g_st.vbytes) / 1024.0);
    if (g_st.irecs)
        LOG("[cheapib] b1 per 1200 frames: instance batches refreshed %llu (%.1f entries each: from this pass's blocks %llu, "
            "the logic pass's bytes %llu), stream binds moved %llu", (unsigned long long)g_st.irecs,
            double(g_st.ientries) / double(g_st.irecs), (unsigned long long)g_st.irefreshed, (unsigned long long)g_st.ikept,
            (unsigned long long)g_st.iremapped);
    LOG("[cheapib] b1: slot activations since the logic pass's, at in-between calls (ring %zu):%s; segments not replayable: "
        "submission %llu, extension %llu, slot %llu, image replaced %llu (swapped, kept %llu); held slot activated again %llu", R.submissions.size(), act.c_str(),
        (unsigned long long)g_st.badSub, (unsigned long long)g_st.badExt, (unsigned long long)g_st.badSlot, (unsigned long long)g_st.badImg, (unsigned long long)g_st.swapped,
        (unsigned long long)g_st.released);
    LOG("[cheapib] b1 hold: layout fix-ups %llu; holds released %llu (users retired %llu, of them waited > 0.1 ms %llu, %.3f ms in all; "
        "unsealed user at activation %llu; kept for a recording user %llu); deferred objects given to a held slot %llu "
        "(from %llu slots); holds now %zu; held slot passed over %llu", (unsigned long long)g_st.fixups, (unsigned long long)g_st.holdsReleased,
        (unsigned long long)g_st.userRetired, (unsigned long long)g_st.userWaits, g_st.userWaitNs / 1e6,
        (unsigned long long)g_st.unsealedAtActivation, (unsigned long long)g_st.keptForUser,
        (unsigned long long)g_st.adoptedObjects, (unsigned long long)g_st.adoptedSlots, g_holds.size(),
        (unsigned long long)g_st.skips);
    b2_report();
    check_report();
    for (int w = 0; w < 2; w++) {
        const uint64_t* c = g_guest[w];
        if (!(c[0] | c[1] | c[2] | c[3] | c[4])) continue;
        std::string ad;
        for (int p = 0; p < 5; p++)
            for (int i = 0; i < 4 && i < int(c[p]); i++) {
                char t[48];
                snprintf(t, sizeof t, " %d:%08X(%u/%u)", p, g_guestAddr[w][p][i], g_guestSize[w][p][i], g_guestHeld[w][p][i]);
                ad += t;
            }
        LOG("[cheapib] b1 per 1200 frames: blocks read from guest memory at render time (not in the snapshot), %s calls: "
            "instance entries %llu, packet blocks %llu, pack_uniforms %llu, packet support %llu, J3D support %llu (no snapshot %llu, "
            "address not in it %llu, shorter in it %llu); first:%s",
            w ? "checked" : "logic", (unsigned long long)c[0], (unsigned long long)c[1], (unsigned long long)c[2],
            (unsigned long long)c[3], (unsigned long long)c[4], (unsigned long long)g_guestWhy[w][0],
            (unsigned long long)g_guestWhy[w][1], (unsigned long long)g_guestWhy[w][2], ad.c_str());
    }
    memset(g_guest, 0, sizeof g_guest);
    memset(g_guestWhy, 0, sizeof g_guestWhy);
    const uint64_t f = g_st.frames;
    g_st = Stats{};
    g_st.frames = f;
}

// (fast 4) the previous logic pass's segments, their vectors kept for the next pass's
std::vector<Seg> g_segPool;
void recycle_segs() {
    for (Seg& s : g_cap.segs) g_segPool.push_back(std::move(s));
    g_cap.segs.clear();
}
// a fresh segment (as Seg{}) whose vectors reuse a recycled one's storage
void emplace_seg() {
    g_cap.segs.emplace_back();
    if (g_segPool.empty()) return;
    Seg& o = g_segPool.back();
    Seg& n = g_cap.segs.back();
    auto take = [](auto& to, auto& from) {
        from.clear();
        to.swap(from);
    };
    take(n.imgs, o.imgs);
    take(n.cmds, o.cmds);
    take(n.recs, o.recs);
    take(n.irecs, o.irecs);
    take(n.isrc, o.isrc);
    take(n.vrecs, o.vrecs);
    take(n.vsrc, o.vsrc);
    take(n.ssrc, o.ssrc);
    take(n.rsrc, o.rsrc);
    take(n.aluNet, o.aluNet);
    take(n.written, o.written);
    take(n.allocs, o.allocs);
    take(n.abytes, o.abytes);
    take(n.gnotes, o.gnotes);
    take(n.gsrc, o.gsrc);
    take(n.subs, o.subs);
    g_segPool.pop_back();
    if (fast_verify())
        fv(n.imgs.empty() && n.cmds.empty() && n.recs.empty() && n.irecs.empty() && n.isrc.empty() && n.vrecs.empty() &&
               n.vsrc.empty() && n.ssrc.empty() && n.rsrc.empty() && n.aluNet.empty() && n.written.empty() &&
               n.allocs.empty() && n.abytes.empty() && n.gnotes.empty() && n.gsrc.empty() && n.subs.empty() &&
               n.ncmds == 0 && !n.bad && n.startInst == -1 && !n.moved && !n.renderPass,
           "recycled segment");
}
void capture_begin(uint32_t list, uint32_t size) {
    if (g_tag.step != g_cap.step) {  // the logic pass's first call
        if (fast(4)) recycle_segs();
        else g_cap.segs.clear();
        g_cap.step = g_tag.step;
        g_cap.slot = R.activeSubmission;
        g_cap.gen = R.submissionGeneration;
        g_cap.valid = true;
        g_held = &R.submissions[g_cap.slot];  // (the previous step's hold is released at its slot's next retirement)
        bool known = false;
        for (const Hold& h : g_holds) known = known || h.slot == g_held;
        if (!known) g_holds.push_back({g_held, g_tag.step, {}});
        else g_cap.valid = false;  // (cannot happen: a held slot's activation releases it)
    }
    if (g_tag.index != g_cap.segs.size()) g_cap.valid = false;
    ::native_draw::flush_mdi();  // (earlier commands' batches: not this call's)
    ::native_draw::flush_batch();
    if (fast(4)) emplace_seg();
    else g_cap.segs.emplace_back();
    Seg& s = g_cap.segs.back();
    s.list = list;
    s.size = size;
    g_cap.cur = int(g_cap.segs.size() - 1);
    g_cap.draws0 = R.drawCount;
    g_cap.passes0 = R.renderPassCount;
    s.begin = pass_state();
    b2_capture_edge(s, false);
    if (R.activeSubmission != g_cap.slot) s.bad = "slot";
    s.slot0 = R.activeSubmission;
    s.gen0 = R.submissionGeneration;
    g_into = &s;
    g_vpend.clear();
    g_aluWritten.reset();
    g_b1_capturing = true;
}
// the ALU constants the call wrote, with their values after it (gx2's register file at the call's end)
void keep_alu_net(Seg& s) {
    s.aluNet.clear();
    const uint32_t* regs = gx2::regs();
    for (uint32_t i = 0; i < kB1AluWords;) {
        if (!g_aluWritten[i]) { i++; continue; }
        uint32_t j = i;
        while (j < kB1AluWords && g_aluWritten[j]) j++;
        s.aluNet.push_back(kB1Alu0 + i);
        s.aluNet.push_back(j - i);
        s.aluNet.insert(s.aluNet.end(), regs + kB1Alu0 + i, regs + kB1Alu0 + j);
        i = j;
    }
}
void capture_end() {
    if (g_cap.cur < 0) return;
    ::native_draw::flush_mdi();  // (this call's batches into its segment)
    ::native_draw::flush_batch();
    g_b1_capturing = false;
    g_into = nullptr;
    Seg& s = g_cap.segs[g_cap.cur];
    keep_alu_net(s);
    if (check_on()) keep_alloc_bytes(s);
    s.end = pass_state();
    for (Img& m : s.imgs) {
        m.imageAfter = m.s->image;
        m.after = m.s->layout;
        m.useAfter = m.s->use;
    }
    for (Img& m : s.imgs) {
        if (m.s->image == m.image) continue;
        // swapped images (feedback copy slots): fine when the image in, the image out and the surface's former image
        // (if this call noted its new holder) are all shader-read at both ends; the replay then needs the image out
        // in place (checked) and changes no layout
        bool ok = m.before == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && m.after == m.before;
        for (const Img& n : s.imgs)
            if (&n != &m && n.imageAfter == m.image) ok = ok && n.after == m.before;
        if (ok) { g_st.swapped++; continue; }
        {
            s.bad = "image";
            static int imgLog = 8;
            if (imgLog > 0) {
                imgLog--;
                LOG("[cheapib] b1 capture: step %llu segment %d: surface %08X %ux%u format %X depth %d mips %u replaced "
                    "its image in the call (image %p -> %p, layout %d -> %d, evicted %d)", (unsigned long long)g_cap.step,
                    g_cap.cur, m.s->addr, m.s->width, m.s->height, m.s->format, int(m.s->isDepth), m.s->mips,
                    (void*)m.image, (void*)m.s->image, int(m.before), int(m.s->layout), int(m.s->evicted));
            }
        }
    }
    s.draws = R.drawCount - g_cap.draws0;
    s.passes = R.renderPassCount - g_cap.passes0;
    if (R.activeSubmission != g_cap.slot) s.bad = "slot";
    s.slot1 = R.activeSubmission;
    s.gen1 = R.submissionGeneration;
    b2_capture_edge(s, true);
    static int trace = getenv("WWHD_CHEAPIB_B1_TRACE") ? atoi(getenv("WWHD_CHEAPIB_B1_TRACE")) : 0;
    if (trace > 0) {
        trace--;
        LOG("[cheapib] b1 capture: step %llu segment %d list %08X: %u commands, %zu recs, slot %zu->%zu, generation "
            "%llu->%llu, bad %s, begins/submits at:%s; draws %llu passes %llu, rendering after %d",
            (unsigned long long)g_cap.step, g_cap.cur, s.list, s.ncmds, s.recs.size(), s.slot0, s.slot1,
            (unsigned long long)s.gen0, (unsigned long long)s.gen1, s.bad ? s.bad : "-", s.subs.c_str(),
            (unsigned long long)s.draws, (unsigned long long)s.passes, int(s.end.rendering));
    }
    g_st.logicSegs++;
    if (s.bad) {
        g_st.badSegs++;
        (s.bad[0] == 's' && s.bad[1] == 'u' ? g_st.badSub : s.bad[0] == 'e' ? g_st.badExt : s.bad[0] == 'i' ? g_st.badImg : g_st.badSlot)++;
    }
    g_cap.cur = -1;
}

// WWHD_CHEAPIB_B1_CHECK=1 (debug, Mac): no in-between call is replayed; each call b1 would replay executes, and the
// slices it uploads are compared, in order, with the bytes b1's replay would have bound (the logic pass's slices, the
// noted ones rebuilt from this call's blocks as replay() does). Counted by upload kind; the first mismatches logged
struct Check {
    bool active = false;
    uint32_t index = 0;
    Seg seg;                      // the executed call's notes
    std::vector<uint8_t> expect;  // the logic segment's slices as b1 would bind them
};
Check g_chk;
struct CheckStats {
    uint64_t segs = 0, layout = 0, order = 0, slices[8] = {}, differ[8] = {}, bytes[8] = {}, noted[8] = {}, notedDiffer[8] = {}, inNoted = 0, inGuest = 0, inOther = 0, inReg = 0, sReg = 0, sNoted = 0, sGuest = 0, sOther = 0, blockDiffer = 0;
} g_ck;
int g_ckLog = 24;
int g_ckLogN = 24;  // (mismatches in noted slices)
int g_ckLogO = 24;  // (host-packed slices differing only outside their noted entries)
int g_ckLogL = 24;  // (slice lists that differ in length or in kind/size order)
// a slice's description for the check log: kind, size, and where its bytes came from (block, host-packed packer, -)
std::string ck_slice(const Seg& s, size_t j) {
    if (j >= s.allocs.size()) return "(none)";
    const auto& a = s.allocs[j];
    char buf[96];
    const char* how = "-";
    uint32_t addr = 0;
    int who = -1;
    for (const Rec& r : s.recs) if (r.buffer == a.buffer && r.offset == a.offset) { how = r.hp() ? "host-packed" : "block"; addr = r.addr; }
    for (const IRec& r : s.irecs) if (r.buffer == a.buffer && r.offset == a.offset) how = "instances";
    for (const VRec& r : s.vrecs) if (r.buffer == a.buffer && r.offset == a.offset) how = "vertices";
    for (const auto& g : s.gnotes) if (g.buffer == a.buffer && g.offset == a.offset) who = g.who;
    snprintf(buf, sizeof buf, "%d/%llu/%s %08X p%d", a.kind, (unsigned long long)a.size, how, addr, who);
    return buf;
}
// the first index where the two slice lists differ in kind or size (n = both lengths' minimum if none)
size_t ck_first_order(const Seg& c, const Seg& k) {
    const size_t n = std::min(c.allocs.size(), k.allocs.size());
    for (size_t j = 0; j < n; j++)
        if (c.allocs[j].kind != k.allocs[j].kind || c.allocs[j].size != k.allocs[j].size) return j;
    return n;
}
void ck_log_order(const Seg& c, const Seg& k, size_t j, const char* what) {
    if (g_ckLogL <= 0) return;
    g_ckLogL--;
    std::string lg, ex;
    for (size_t i = j ? j - 1 : 0; i < j + 3; i++) { lg += " [" + ck_slice(c, i) + "]"; ex += " [" + ck_slice(k, i) + "]"; }
    LOG("[cheapib] b1 check: step %llu segment %u: %s (logic %zu slices, executed %zu) at slice %zu: logic%s; executed%s",
        (unsigned long long)g_tag.step, g_chk.index, what, c.allocs.size(), k.allocs.size(), j, lg.c_str(), ex.c_str());
}
void keep_alloc_bytes(Seg& s) {
    size_t n = 0;
    for (const auto& a : s.allocs) n += a.size;
    s.abytes.resize(n);
    size_t at = 0;
    for (auto& a : s.allocs) {
        a.at = at;
        memcpy(s.abytes.data() + at, a.mapped, a.size);
        at += a.size;
    }
}
void check_begin(const Seg& c) {
    g_chk.active = true;
    g_chk.index = g_tag.index;
    Seg& k = g_chk.seg;
    k.recs.clear(); k.irecs.clear(); k.isrc.clear(); k.vrecs.clear(); k.vsrc.clear(); k.ssrc.clear(); k.allocs.clear(); k.gnotes.clear(); k.gsrc.clear(); k.rsrc.clear();
    // what b1 would bind: the logic slices, the noted ones rebuilt from this call's snapshot
    g_chk.expect = c.abytes;
    const gx2::submitted::Snapshot* snap = gx2::active_snapshot();
    auto slot = [&](uint64_t buffer, uint64_t offset) -> const Seg::Alloc* {
        for (const auto& a : c.allocs)
            if (a.buffer == buffer && a.offset == offset) return &a;
        return nullptr;
    };
    for (const Rec& r : c.recs) {
        const Seg::Alloc* a = slot(r.buffer, r.offset);
        if (!a) continue;
        uint8_t* dst = g_chk.expect.data() + a->at;
        const uint8_t* src = snap ? snap->find(r.addr, 0) : nullptr;
        if (r.hp()) fill_sources(c, r, snap, dst);
        else if (src && snap->block_size(r.addr) >= r.delta + r.size) {
            memcpy(dst, src + r.delta, r.size);
            if (a->size > r.size) memset(dst + r.size, 0, a->size - r.size);
        }
    }
    for (const IRec& r : c.irecs) {
        const Seg::Alloc* a = slot(r.buffer, r.offset);
        if (!a) continue;
        static std::vector<uint8_t> old;
        old.assign(c.abytes.begin() + a->at, c.abytes.begin() + a->at + a->size);
        fill_instances(c, r, snap, old.data(), g_chk.expect.data() + a->at);
    }
    for (const VRec& r : c.vrecs) {
        const Seg::Alloc* a = slot(r.buffer, r.offset);
        if (!a || a->size < r.slice) continue;
        static std::vector<uint8_t> old;
        old.assign(c.abytes.begin() + a->at, c.abytes.begin() + a->at + a->size);
        fill_vertices(c, r, old.data(), g_chk.expect.data() + a->at);
    }
    g_vpend.clear();
    ::native_draw::flush_mdi();
    ::native_draw::flush_batch();
    g_into = &k;
    g_aluWritten.reset();
    g_b1_capturing = true;
}
void check_end() {
    ::native_draw::flush_mdi();
    ::native_draw::flush_batch();
    g_b1_capturing = false;
    g_into = nullptr;
    g_chk.active = false;
    if (g_chk.index >= g_cap.segs.size()) return;
    const Seg& c = g_cap.segs[g_chk.index];
    const Seg& k = g_chk.seg;
    g_ck.segs++;
    if (k.allocs.size() != c.allocs.size()) {
        g_ck.layout++;
        ck_log_order(c, k, ck_first_order(c, k), "slice lists differ in length");
        return;
    }
    // (same length, but a slice inserted early and one dropped later: the contents would be compared shifted)
    if (const size_t j = ck_first_order(c, k); j < c.allocs.size()) {
        g_ck.order++;
        ck_log_order(c, k, j, "slice lists differ in order");
        return;
    }
    for (size_t j = 0; j < c.allocs.size(); j++) {
        const auto& a = c.allocs[j];
        const auto& b = k.allocs[j];
        const int kind = std::clamp(a.kind, 0, 7);
        bool noted = false;
        const char* how = "-";
        for (const Rec& r : c.recs) if (r.buffer == a.buffer && r.offset == a.offset) { noted = true; how = r.hp() ? "host-packed" : "block"; }
        for (const IRec& r : c.irecs) if (r.buffer == a.buffer && r.offset == a.offset) { noted = true; how = "instances"; }
        for (const VRec& r : c.vrecs) if (r.buffer == a.buffer && r.offset == a.offset) { noted = true; how = "vertices"; }
        g_ck.slices[kind]++;
        g_ck.noted[kind] += noted;
        const uint8_t* e = g_chk.expect.data() + a.at;
        const uint8_t* x = static_cast<const uint8_t*>(b.mapped);
        const size_t n = std::min(a.size, b.size);
        if (a.size == b.size && memcmp(e, x, n) == 0) continue;
        size_t first = 0, diff = 0;
        while (first < n && e[first] == x[first]) first++;
        for (size_t i = 0; i < n; i++) diff += e[i] != x[i];
        g_ck.differ[kind]++;
        g_ck.bytes[kind] += diff;
        g_ck.notedDiffer[kind] += noted;
        // where the differing bytes lie: in a noted (rebuilt) entry, in an entry read from guest memory, elsewhere
        const Seg::GNote* gn = nullptr;
        for (const auto& g : c.gnotes) if (g.buffer == a.buffer && g.offset == a.offset) gn = &g;
        if (gn) {
            uint64_t bn = 0, bg = 0, bo = 0, br = 0;
            uint32_t gaddr = 0, nentry = ~0u, ofirst = ~0u;
            for (size_t i = 0; i < n; i++) {
                if (e[i] == x[i]) continue;
                bool hit = false;
                for (uint32_t m = 0; m < gn->sn && !hit; m++) {
                    const uint32_t d = c.ssrc[gn->sat + 3 * m];
                    if (i >= d && i < d + 16) { hit = true; bn++; if (nentry == ~0u) nentry = d; }
                }
                for (uint32_t m = 0; m < gn->rn && !hit; m++) {
                    const uint32_t d = c.rsrc[gn->rat + 3 * m];
                    if (i >= d && i < d + 4 * c.rsrc[gn->rat + 3 * m + 2]) { hit = true; br++; }
                }
                for (uint32_t m = 0; m < gn->gn && !hit; m++) {
                    const uint32_t d = c.gsrc[gn->gat + 2 * m];
                    if (i >= d && i < d + 16) { hit = true; bg++; if (!gaddr) gaddr = c.gsrc[gn->gat + 2 * m + 1]; }
                }
                if (!hit) { bo++; if (ofirst == ~0u) ofirst = uint32_t(i); }
            }
            g_ck.inNoted += bn; g_ck.inGuest += bg; g_ck.inOther += bo; g_ck.inReg += br; g_ck.sReg += br != 0;
            g_ck.sNoted += bn != 0; g_ck.sGuest += bg != 0; g_ck.sOther += bo != 0;
            if (bo && !bn && !bg && !br && g_ckLogO > 0) {  // (only elsewhere: where, and the packer)
                g_ckLogO--;
                uint32_t ew[4] = {}, xw[4] = {};
                const size_t f16 = ofirst & ~15u;
                if (f16 + 16 <= n) { memcpy(ew, e + f16, 16); memcpy(xw, x + f16, 16); }
                LOG("[cheapib] b1 check: step %llu segment %u slice %zu (kind %d, %llu bytes, packer %d, %u noted, %u registers): %llu bytes "
                    "differ elsewhere, first at %u: b1 %08X %08X %08X %08X, executed %08X %08X %08X %08X", (unsigned long long)g_tag.step,
                    g_chk.index, j, a.kind, (unsigned long long)a.size, gn->who, gn->sn, gn->rn, (unsigned long long)bo, ofirst,
                    ew[0], ew[1], ew[2], ew[3], xw[0], xw[1], xw[2], xw[3]);
            }
            if ((bn || bg || br) && g_ckLogN > 0) {
                g_ckLogN--;
                LOG("[cheapib] b1 check: step %llu segment %u slice %zu (kind %d, %llu bytes, %u noted entries, %u guest reads): "
                    "differing bytes in noted entries %llu (first entry at %d), in register entries %llu, in guest-read entries %llu (first guest address %08X), elsewhere %llu",
                    (unsigned long long)g_tag.step, g_chk.index, j, a.kind, (unsigned long long)a.size, gn->sn, gn->gn,
                    (unsigned long long)bn, nentry == ~0u ? -1 : int(nentry), (unsigned long long)br, (unsigned long long)bg, gaddr, (unsigned long long)bo);
            }
        } else if (noted) {
            g_ck.blockDiffer++;
            if (g_ckLogN > 0) {
                g_ckLogN--;
                uint32_t addr = 0, delta = 0, size = 0, xaddr = 0, xdelta = 0;
                for (const Rec& r : c.recs) if (r.buffer == a.buffer && r.offset == a.offset) { addr = r.addr; delta = r.delta; size = r.size; }
                for (const Rec& r : k.recs) if (r.buffer == b.buffer && r.offset == b.offset) { xaddr = r.addr; xdelta = r.delta; }
                LOG("[cheapib] b1 check: step %llu segment %u slice %zu (kind %d, %llu bytes, noted %s, block %08X + %u, %u bytes; executed "
                    "from %08X + %u): %zu bytes differ, first at %zu",
                    (unsigned long long)g_tag.step, g_chk.index, j, a.kind, (unsigned long long)a.size, how, addr, delta, size, xaddr,
                    xdelta, diff, first);
            }
        }
        if (g_ckLog > 0 && !noted) {
            g_ckLog--;
            uint32_t ew[4] = {}, xw[4] = {};
            const size_t f16 = first & ~size_t(15);
            if (f16 + 16 <= n) { memcpy(ew, e + f16, 16); memcpy(xw, x + f16, 16); }
            LOG("[cheapib] b1 check: step %llu segment %u slice %zu (kind %d, %llu bytes, noted %s): %zu bytes differ, first at %zu: b1 "
                "%08X %08X %08X %08X, executed %08X %08X %08X %08X", (unsigned long long)g_tag.step, g_chk.index, j, a.kind,
                (unsigned long long)a.size, how, diff, first, ew[0], ew[1], ew[2], ew[3], xw[0], xw[1], xw[2], xw[3]);
        }
    }
}
void check_report() {
    if (!g_ck.segs) return;
    static const char* kKind[8] = {"other", "vertex", "index", "ubo", "support", "texture", "?", "?"};
    std::string per;
    for (int i = 0; i < 8; i++)
        if (g_ck.slices[i])
            per += std::string("; ") + kKind[i] + " " + std::to_string(g_ck.slices[i]) + " (noted " + std::to_string(g_ck.noted[i]) +
                   "), differ " + std::to_string(g_ck.differ[i]) + " (noted " + std::to_string(g_ck.notedDiffer[i]) + ", " +
                   std::to_string(g_ck.bytes[i]) + " bytes)";
    LOG("[cheapib] b1 check per 1200 frames: calls checked %llu (slice lists differ %llu, in kind/size order %llu)%s",
        (unsigned long long)g_ck.segs, (unsigned long long)g_ck.layout, (unsigned long long)g_ck.order, per.c_str());
    LOG("[cheapib] b1 check per 1200 frames: host-packed differing bytes: in noted entries %llu (%llu slices), in register entries %llu "
        "(%llu slices), in guest-read entries %llu (%llu slices), elsewhere %llu (%llu slices); noted block slices differing %llu",
        (unsigned long long)g_ck.inNoted, (unsigned long long)g_ck.sNoted, (unsigned long long)g_ck.inReg,
        (unsigned long long)g_ck.sReg, (unsigned long long)g_ck.inGuest, (unsigned long long)g_ck.sGuest,
        (unsigned long long)g_ck.inOther, (unsigned long long)g_ck.sOther, (unsigned long long)g_ck.blockDiffer);
    g_ck = CheckStats{};
    g_ckLogN = 24;
    g_ckLogO = 24;
    g_ckLogL = 24;
}

// debug: WWHD_CHEAPIB_B1_SEGS=a:b replays only segments a..b-1 (the others execute); WWHD_CHEAPIB_B1_LOG=n logs the
// opcodes of the first n replayed segments
uint32_t seg_lo() { static const uint32_t v = getenv("WWHD_CHEAPIB_B1_SEGS") ? (uint32_t)atoi(getenv("WWHD_CHEAPIB_B1_SEGS")) : 0; return v; }
uint32_t seg_hi() {
    static const uint32_t v = [] {
        const char* e = getenv("WWHD_CHEAPIB_B1_SEGS");
        const char* c = e ? strchr(e, ':') : nullptr;
        return c ? (uint32_t)atoi(c + 1) : ~0u;
    }();
    return v;
}
int g_oplog = getenv("WWHD_CHEAPIB_B1_LOG") ? atoi(getenv("WWHD_CHEAPIB_B1_LOG")) : 0;
// the replay of segment g_tag.index; false: executed instead (counted)
bool replay(uint32_t list, uint32_t size) {
    int why = -1;
    if (g_tag.index < seg_lo() || g_tag.index >= seg_hi()) {
        g_st.executed++;
        return false;
    }
    if (!g_cap.valid || g_cap.segs.empty()) why = 0;
    else if (g_tag.step != g_cap.step) why = 1;
    else if (g_tag.index >= g_cap.segs.size()) why = 2;
    else if (g_cap.segs[g_tag.index].list != list || g_cap.segs[g_tag.index].size != size) why = 3;
    else if (R.activeSubmission == g_cap.slot || g_cap.slot >= R.submissions.size() ||
             R.submissions[g_cap.slot].uploadBlocks.empty() || g_held != &R.submissions[g_cap.slot])
        why = 4;  // (the logic slot was activated again: released)
    else if (g_cap.segs[g_tag.index].bad) why = 5;
    else {  // the renderer's state must be the logic pass's at this call: its pass, its images
        const Seg& c = g_cap.segs[g_tag.index];
        const PassState now = pass_state();
        if (now.rendering != c.begin.rendering ||
            (now.rendering && (now.colors != c.begin.colors || now.slices != c.begin.slices || now.depth != c.begin.depth ||
                               now.depthSlice != c.begin.depthSlice)))
            why = 8;
        for (size_t i = 0; i < c.imgs.size() && why < 0; i++) {
            const Img& m = c.imgs[i];
            if (m.s->image != m.image && (m.imageAfter == m.image || m.s->image != m.imageAfter)) why = 9;
            else if (m.s->image != m.image) { if (m.s->layout != m.after) why = 9; }
            else if (m.s->layout != m.before && (now.rendering || m.before == VK_IMAGE_LAYOUT_UNDEFINED)) why = 10;
        }
    }
    if (why < 0 || why == 4 || why == 5 || why >= 8) {
        const uint64_t d = R.submissionGeneration - g_cap.gen;
        g_st.activations[std::min<uint64_t>(d, 7)]++;
    }
    if (why < 0 && check_on()) {  // (WWHD_CHEAPIB_B1_CHECK: executed and compared instead)
        check_begin(g_cap.segs[g_tag.index]);
        g_st.executed++;
        return false;
    }
    PodCommands* stream = why < 0 ? record_worker_commands() : nullptr;
    if (why < 0 && !stream) why = 7;
    static const Seg kNone;
    const Seg& s = why < 0 ? g_cap.segs[g_tag.index] : kNone;
    // room for the uniform slices in the logic slot's blocks
    const VkDeviceSize align = std::max<VkDeviceSize>(R.properties.limits.minUniformBufferOffsetAlignment, 16);
    const bool inplace = why < 0 && b2_requested() && b2_inplace(g_tag.index);  // (b2: slices refreshed in place)
    if (why < 0 && !inplace && fast(2)) {  // (the same check: a few buffers, summed in a reused list)
        static std::vector<std::pair<uint64_t, VkDeviceSize>> need;
        need.clear();
        auto add = [](uint64_t buf, VkDeviceSize n) {
            for (auto& [b, m] : need)
                if (b == buf) { m += n; return; }
            need.push_back({buf, n});
        };
        for (const Rec& r : s.recs) add(r.buffer, r.slice + align);
        for (const IRec& r : s.irecs) add(r.buffer, r.slice + align);
        for (const VRec& r : s.vrecs) add(r.buffer, r.slice + align);
        for (auto& [buf, n] : need) {
            Renderer::UploadBlock* b = logic_block(buf);
            if (!b || b->used + n > b->buffer.size) { why = 6; break; }
        }
        if (fast_verify()) {
            std::unordered_map<uint64_t, VkDeviceSize> need2;
            for (const Rec& r : s.recs) need2[r.buffer] += r.slice + align;
            for (const IRec& r : s.irecs) need2[r.buffer] += r.slice + align;
            for (const VRec& r : s.vrecs) need2[r.buffer] += r.slice + align;
            int why2 = -1;
            for (auto& [buf, n] : need2) {
                Renderer::UploadBlock* b = logic_block(buf);
                if (!b || b->used + n > b->buffer.size) { why2 = 6; break; }
            }
            fv(why == why2, "room check");
        }
    } else if (why < 0 && !inplace) {
        std::unordered_map<uint64_t, VkDeviceSize> need;
        for (const Rec& r : s.recs) need[r.buffer] += r.slice + align;
        for (const IRec& r : s.irecs) need[r.buffer] += r.slice + align;
        for (const VRec& r : s.vrecs) need[r.buffer] += r.slice + align;
        for (auto& [buf, n] : need) {
            Renderer::UploadBlock* b = logic_block(buf);
            if (!b || b->used + n > b->buffer.size) { why = 6; break; }
        }
    }
    if (why >= 0) {
        (why < 8 ? g_st.refused[why] : g_st.refused2[why - 8])++;
        g_st.executed++;
        return false;
    }
    const uint64_t t0 = rprof::now_ns();
    // the images in the layouts the commands expect (another call of this in-between left them otherwise)
    for (const Img& m : s.imgs)
        if (m.s->layout != m.before) {
            transition_image(m.s, m.before, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
            g_st.fixups++;
        }
    {  // the slot the commands go to uses the held slot's sets and blocks until it is retired
        auto& users = g_holds.back().users;
        Renderer::Submission* u = &R.submissions[R.activeSubmission];
        if (users.empty() || users.back().first != u) users.push_back({u, lastSubmissionSerial});
    }
    const VkCommandBuffer cmd = command_buffer();
    if (inplace) {  // (cheapib_b2.h: slices refreshed in place, rendering instances as secondaries)
        if (!b2_replay(g_cap.segs[g_tag.index], g_tag.index, stream, cmd)) {
            g_st.executed++;
            return false;
        }
        goto replayed;
    }
    if (b2_requested()) b2_note_b1path();
    {
    // uniform slices again, from this call's snapshot (the patched blocks of this pass)
    // (for the whole frame: a later call's draws may bind a slice an earlier call uploaded)
    static std::unordered_map<uint64_t, uint64_t> moved;  // old offset -> new offset (0 = ambiguous)
    static std::unordered_map<uint64_t, uint64_t> owner;  // old offset -> its buffer
    static uint64_t movedFrame = ~0ull;
    // (fast 2) the same as moved + owner in one flat table: key old offset, a = new offset (0 ambiguous), b = buffer
    static GenMap fmoved;
    const bool f2 = fast(2);
    if (movedFrame != g_st.frames) {
        if (f2) fmoved.reset();
        if (!f2 || fast_verify()) {
            moved.clear();
            owner.clear();
        }
        movedFrame = g_st.frames;
    }
    const gx2::submitted::Snapshot* snap = gx2::active_snapshot();
    for (const Rec& r : s.recs) {
        Renderer::UploadBlock* b = logic_block(r.buffer);
        const VkDeviceSize at = ((b->used + align - 1) / align) * align;
        b->used = at + r.slice;
        uint8_t* dst = static_cast<uint8_t*>(b->buffer.mapped) + at;
        const uint8_t* old = static_cast<const uint8_t*>(b->buffer.mapped) + r.offset;
        const uint8_t* src = r.hp() ? nullptr : snap_block(snap, r.addr, r.delta + r.size);
        if (r.hp()) {  // (host-packed: the logic pass's bytes, the entries from this pass's blocks and registers)
            memcpy(dst, old, r.size);
            fill_sources(s, r, snap, dst);
            g_st.refreshed++;
        } else if (src) {
            memcpy(dst, src + r.delta, r.size);
            g_st.refreshed++;
        } else {
            memcpy(dst, old, r.size);
            g_st.fromLogic++;
        }
        if (r.slice > r.size) memset(dst + r.size, 0, r.slice - r.size);
        g_st.recs++;
        if (f2) {
            bool isNew;
            GenMap::E& e = fmoved.get(r.offset, isNew);
            if (!isNew && e.b != r.buffer) {
                e.a = 0;  // (two buffers' slices at this offset: left as they are)
                g_st.ambiguous++;
            } else {
                e.b = r.buffer;
                e.a = at;
            }
            if (!fast_verify()) continue;
            const uint64_t amb = g_st.ambiguous;
            auto o = owner.find(r.offset);
            if (o != owner.end() && o->second != r.buffer) moved[r.offset] = 0;
            else {
                owner[r.offset] = r.buffer;
                moved[r.offset] = at;
            }
            g_st.ambiguous = amb;
            continue;
        }
        auto o = owner.find(r.offset);
        if (o != owner.end() && o->second != r.buffer) {
            moved[r.offset] = 0;  // (two buffers' slices at this offset: left as they are)
            g_st.ambiguous++;
        } else {
            owner[r.offset] = r.buffer;
            moved[r.offset] = at;
        }
    }
    // the batches' instance entries again, into new slices (the stream binds below moved to them)
    std::unordered_map<uint64_t, uint64_t> imoved;  // (buffer ^ old offset << 1) -> new offset
    static GenMap fimoved;  // (fast 2: the same, flat, reset per call)
    if (f2) fimoved.reset();
    auto ikey = [](uint64_t buffer, uint64_t offset) { return buffer * 1000003ull ^ offset; };
    auto iput = [&](uint64_t k, uint64_t at) {
        if (f2) {
            bool isNew;
            fimoved.get(k, isNew).a = at;
            if (fast_verify()) imoved[k] = at;
        } else imoved[k] = at;
    };
    for (const IRec& r : s.irecs) {
        Renderer::UploadBlock* b = logic_block(r.buffer);
        const VkDeviceSize at = ((b->used + align - 1) / align) * align;
        b->used = at + r.slice;
        uint8_t* dst = static_cast<uint8_t*>(b->buffer.mapped) + at;
        fill_instances(s, r, snap, static_cast<const uint8_t*>(b->buffer.mapped) + r.offset, dst);
        if (r.slice > 16ull * r.entries) memset(dst + 16ull * r.entries, 0, r.slice - 16ull * r.entries);
        iput(ikey(r.buffer, r.offset), at);
    }
    // (stage 2) vertex slices with CPU-written vertex sites' bytes, into new slices (the vertex binds moved below)
    for (const VRec& r : s.vrecs) {
        Renderer::UploadBlock* b = logic_block(r.buffer);
        const VkDeviceSize at = ((b->used + align - 1) / align) * align;
        b->used = at + r.slice;
        fill_vertices(s, r, static_cast<const uint8_t*>(b->buffer.mapped) + r.offset,
                      static_cast<uint8_t*>(b->buffer.mapped) + at);
        iput(ikey(r.buffer, r.offset), at);
    }
    // the commands again
    static std::vector<std::byte> scratch;
    const std::byte* p = s.cmds.data();
    const std::byte* e = p + s.cmds.size();
    bool ok = true;
    std::string ops;
    while (p < e) {
        uint32_t op, n;
        memcpy(&op, p, 4);
        memcpy(&n, p + 4, 4);
        if (g_oplog > 0) ops += " " + std::to_string(op);
        const std::byte* pl = p + 8;
        p = pl + n;
        if (op == 9 || op == 10 || op == 28 || op == 29) { g_st.skipped++; continue; }  // (queries; never a begin/submit)  // timestamp queries (the logic slot's pool)
        if (op == 31) {  // extensions: descriptor-set definitions are the logic slot's, still defined
            g_st.skipped++;
            continue;
        }
        scratch.assign(pl, pl + n);
        if (n >= sizeof(VkCommandBuffer)) memcpy(scratch.data(), &cmd, sizeof cmd);
        if (op == 19) {  // vkCmdBindDescriptorSets: cmd, point, layout, first, count, sets[count], offsetCount, offsets[]
            size_t q = sizeof(VkCommandBuffer) + sizeof(VkPipelineBindPoint) + sizeof(VkPipelineLayout) + 4;
            uint32_t count, offsetCount;
            memcpy(&count, scratch.data() + q, 4);
            q += 4 + size_t(count) * sizeof(VkDescriptorSet);
            memcpy(&offsetCount, scratch.data() + q, 4);
            q += 4;
            for (uint32_t i = 0; i < offsetCount && q + 4 <= n; i++, q += 4) {
                uint32_t off;
                memcpy(&off, scratch.data() + q, 4);
                uint64_t mv = 0;
                if (f2) {
                    const GenMap::E* fe = fmoved.find(off);
                    mv = fe ? fe->a : 0;
                    if (fast_verify()) {
                        auto it = moved.find(off);
                        fv(mv == (it == moved.end() ? 0 : it->second), "moved slice");
                    }
                } else {
                    auto it = moved.find(off);
                    mv = it == moved.end() ? 0 : it->second;
                }
                if (mv == 0) { g_st.unmapped++; continue; }
                const uint32_t to = uint32_t(mv);
                memcpy(scratch.data() + q, &to, 4);
                g_st.remapped++;
            }
        }
        if (f2 && fast_verify() && op == 18) fv((fimoved.live != 0) == !imoved.empty(), "moved instances (empty)");
        if (op == 18 && (f2 ? fimoved.live != 0 : !imoved.empty())) {  // vkCmdBindVertexBuffers: cmd, first, count, buffers[count], offsets[count]
            size_t q = sizeof(VkCommandBuffer) + 4;
            uint32_t count;
            memcpy(&count, scratch.data() + q, 4);
            q += 4;
            const size_t qo = q + size_t(count) * sizeof(VkBuffer);
            for (uint32_t i = 0; i < count && qo + 8 * (i + 1) <= n; i++) {
                uint64_t buf, off;
                memcpy(&buf, scratch.data() + q + 8 * i, 8);
                memcpy(&off, scratch.data() + qo + 8 * i, 8);
                uint64_t to;
                if (f2) {
                    const GenMap::E* fe = fimoved.find(ikey(buf, off));
                    if (fast_verify()) {
                        auto it = imoved.find(ikey(buf, off));
                        fv((fe != nullptr) == (it != imoved.end()) && (!fe || fe->a == it->second), "moved instances");
                    }
                    if (!fe) continue;
                    to = fe->a;
                } else {
                    auto it = imoved.find(ikey(buf, off));
                    if (it == imoved.end()) continue;
                    to = it->second;
                }
                memcpy(scratch.data() + qo + 8 * i, &to, 8);
                g_st.iremapped++;
            }
        }
        if (!stream->append_raw(op, {scratch.data(), scratch.size()})) { ok = false; break; }
        g_st.cmds++;
        g_st.bytes += n + 8;
    }
    if (!ok) LOG("[cheapib] b1: the producer stream refused a replayed command (%d)", int(stream->failure_reason()));
    if (g_oplog > 0) {
        g_oplog--;
        LOG("[cheapib] b1: replayed segment %u (step %llu, list %08X, %u recs, rendering after %d; now slot %zu generation "
            "%llu):%s", g_tag.index, (unsigned long long)g_tag.step, list, (unsigned)s.recs.size(), int(s.end.rendering),
            R.activeSubmission, (unsigned long long)R.submissionGeneration, ops.substr(0, 200).c_str());
    }
    }
replayed:
    apply_alu_net(s);  // (after the slices read the registers the call inherited)
    set_pass_state(s.end);
    for (Surface* w : s.written) mark_draw_target_written(w);  // (writeSeq; linear targets written back at draw done)
    for (const Img& m : s.imgs) {
        m.s->layout = m.after;
        m.s->use = m.useAfter;
    }
    R.drawCount += s.draws;
    R.renderPassCount += s.passes;
    cheapib_b1_binds_dirty();
    publish_record_commands_at_draw_boundary();
    g_st.replayed++;
    g_frame_replayed = true;
    g_st.ns += double(rprof::now_ns() - t0);
    return true;
}
#endif  // CHEAPIB_B1_RENDER
}  // namespace b1

bool b1_on() { return b1::requested(); }

// game thread, cheapib::drop_call: the tag of a top-level call
void b1_tag(uint32_t list, uint32_t size, int kind, uint64_t step, uint32_t index) {
    std::lock_guard<std::mutex> lk(b1::g_tag_mu);
    b1::g_tags.push_back({kind, step, index, list, size});
}

bool b1_call_begin(uint32_t list, uint32_t size) {
    if (!b1::requested()) return false;
    {
        std::lock_guard<std::mutex> lk(b1::g_tag_mu);
        if (b1::g_tags.empty()) b1::g_tag = {};
        else {
            b1::g_tag = b1::g_tags.front();
            b1::g_tags.pop_front();
        }
    }
#ifdef CHEAPIB_B1_RENDER
    static const bool b2 = b1::b2_requested();
    g_b2_shadow = b2;  // (pod_commands.cpp: the state at segment edges)
    b1::Tag& t = b1::g_tag;
    if (t.list != list || t.size != size) t.kind = -1;  // (out of step with the game thread's tags)
    if (t.kind == 0) b1::capture_begin(list, size);
    else if (t.kind == 2) return b1::replay(list, size);
#endif
    return false;
}
void b1_call_end() {
#ifdef CHEAPIB_B1_RENDER
    if (b1::g_chk.active) b1::check_end();
    if (b1::requested()) b1::capture_end();
#endif
}
void b1_capture(const void* stream, uint32_t opcode, const void* payload, size_t bytes) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (g_cap.cur < 0 || stream != gfxvk::record_worker_commands()) return;
    Seg& s = g_cap.segs[g_cap.cur];
    if (opcode == 29) s.bad = "submission";
    if (opcode == 26 || opcode == 27) s.renderPass = true;
    b2_capture_cmd(s, opcode, static_cast<const std::byte*>(payload), bytes);  // (b2: rendering instances)
    if (opcode == 28 || opcode == 29) {
        s.subs += " " + std::to_string(opcode) + "@" + std::to_string(s.ncmds);
    }
    if (opcode == 31 && bytes >= 4) {
        uint32_t code;
        memcpy(&code, payload, 4);
        if (code != 2) s.bad = "extension";  // (pipeline futures: their reference is the emitter's to release)
    }
    const uint32_t h[2] = {opcode, uint32_t(bytes)};
    const std::byte* hb = reinterpret_cast<const std::byte*>(h);
    if (fast(16)) {  // (the same bytes: one resize, two copies)
        const size_t at = s.cmds.size();
        s.cmds.resize(at + 8 + bytes);
        memcpy(s.cmds.data() + at, hb, 8);
        if (bytes) memcpy(s.cmds.data() + at + 8, payload, bytes);
    } else {
        s.cmds.insert(s.cmds.end(), hb, hb + 8);
        s.cmds.insert(s.cmds.end(), static_cast<const std::byte*>(payload), static_cast<const std::byte*>(payload) + bytes);
    }
    s.ncmds++;
#endif
}
void b1_note_upload(uint64_t buffer, uint64_t offset, uint64_t sliceSize, const void* data, size_t size) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (!g_into) return;
    const gx2::submitted::Snapshot* snap = gx2::active_snapshot();
    uint32_t addr, delta;
    if (snap && snap_locate(snap, data, addr, delta)) {
        g_into->recs.push_back({buffer, offset, sliceSize, uint32_t(size), addr, delta});
        return;
    }
    // (stage 2) bytes of a CPU-written vertex site: straight from its guest range, or a batch's copies of it
    {
        const uint8_t* d = static_cast<const uint8_t*>(data);
        Seg& s = *g_into;
        const uint32_t at = uint32_t(s.vsrc.size());
        uint32_t a = 0;
        if (d >= PPC_MEM_BASE && d < PPC_MEM_BASE + 0x100000000ull) {
            a = uint32_t(d - PPC_MEM_BASE);
            if (vtx_site(a, uint32_t(size))) put3(s.vsrc, 0u, a, uint32_t(size));
        } else if (drawoff::g_vertex_on && drawoff::vertex_addr_of(d, uint32_t(size), a)) {  // (c2: read from its copy)
            if (vtx_site(a, uint32_t(size))) put3(s.vsrc, 0u, a, uint32_t(size));
        } else if (!g_vpend.empty()) {
            size_t kept = 0;
            for (const VPend& v : g_vpend) {
                if (v.container->data() != d) { g_vpend[kept++] = v; continue; }
                // (the bytes must still be the guest range's, as the renderers read it (c2: the call's copy): a batch
                // dropped without its upload left stale notes)
                if (v.off + v.size <= size && memcmp(d + v.off, WWHD_VERTEX_PTR(v.addr, v.size), v.size) == 0)
                    put3(s.vsrc, uint32_t(v.off), v.addr, v.size);
            }
            g_vpend.resize(kept);
        }
        const uint32_t n = uint32_t((s.vsrc.size() - at) / 3);
        if (n) {
            s.vrecs.push_back({buffer, offset, sliceSize, at, n});
            g_st.vnoted++;
            return;
        }
    }
    // a host-packed slice: its source list (b1_src), entries whose bytes still equal their source's
    for (Armed& a : g_armed) {
        if (a.base != data) continue;
        Seg& s = *g_into;
        const size_t at = s.ssrc.size();
        const uint8_t* d = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i + 3 <= a.src.size(); i += 3) {
            const uint32_t dst = a.src[i], ba = a.src[i + 1], bo = a.src[i + 2];
            const uint8_t* p = snap_block(snap, ba, bo + 16);
            // (a later step that changed the entry after the copy, e.g. a scale: its bytes stay the logic pass's)
            if (dst + 16ull > size || !p || memcmp(d + dst, p + bo, 16) != 0) {
                g_st.sdropped++;
                continue;
            }
            put3(s.ssrc, dst, ba, bo);
        }
        const uint32_t n = uint32_t((s.ssrc.size() - at) / 3);
        // inherited register entries whose bytes still equal the registers (a later scale or eye shift: dropped)
        const size_t rat = s.rsrc.size();
        const uint32_t* regs = gx2::regs();
        for (size_t i = 0; i + 3 <= a.rsrc.size(); i += 3) {
            const uint32_t dst = a.rsrc[i], reg = a.rsrc[i + 1], words = a.rsrc[i + 2];
            if (dst + 4ull * words > size || memcmp(d + dst, regs + reg, 4ull * words) != 0) {
                g_st.rdropped++;
                continue;
            }
            put3(s.rsrc, dst, reg, words);
        }
        const uint32_t rn = uint32_t((s.rsrc.size() - rat) / 3);
        if (n || rn) s.recs.push_back({buffer, offset, sliceSize, uint32_t(size), 0, 0, uint32_t(at), n, uint32_t(rat), rn});
        if (check_on()) {
            const uint32_t gat = uint32_t(s.gsrc.size());
            s.gsrc.insert(s.gsrc.end(), a.gsrc.begin(), a.gsrc.end());
            s.gnotes.push_back({buffer, offset, uint32_t(at), n, gat, uint32_t(a.gsrc.size() / 2), uint32_t(rat), rn, a.who});
        }
        a.base = nullptr;
        a.src.clear();
        a.gsrc.clear();
        a.rsrc.clear();
        return;
    }
#endif
}
void b1_src_arm(const void* base, int who) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (!g_into) return;
    Armed* slot = nullptr;
    for (Armed& a : g_armed)
        if (a.base == base) slot = &a;
    if (!slot) slot = &g_armed[g_armNext++ % g_armed.size()];
    slot->base = base;
    slot->who = who;
    slot->src.clear();
    slot->gsrc.clear();
    slot->rsrc.clear();
#else
    (void)base; (void)who;
#endif
}
void b1_src(const void* base, uint32_t dst, const void* src) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (!g_into) return;
    const gx2::submitted::Snapshot* snap = gx2::active_snapshot();
    uint32_t addr, delta;
    if (!snap || !snap_locate(snap, src, addr, delta)) {
        if (check_on()) {  // (check only: a guest-memory read, which a replay cannot rebuild)
            const uint8_t* q = static_cast<const uint8_t*>(src);
            if (q >= PPC_MEM_BASE && q < PPC_MEM_BASE + 0x100000000ull)
                for (Armed& a : g_armed)
                    if (a.base == base) { a.gsrc.insert(a.gsrc.end(), {dst, uint32_t(q - PPC_MEM_BASE)}); break; }
        }
        return;
    }
    for (Armed& a : g_armed)
        if (a.base == base) {
            put3(a.src, dst, addr, delta);
            return;
        }
#else
    (void)base; (void)dst; (void)src;
#endif
}
void b1_src_reg(const void* base, uint32_t dst, uint32_t reg, uint32_t words) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (!g_into) return;
    Armed* arm = nullptr;
    for (Armed& a : g_armed)
        if (a.base == base) arm = &a;
    if (!arm) return;
    for (uint32_t k = 0; k < words; k += 4) {  // (per 16 bytes: each judged on its own)
        const uint32_t r = reg + k, w = std::min<uint32_t>(4, words - k);
        if (r < kB1Alu0 || r + w > kB1Alu0 + kB1AluWords) continue;
        bool written = false;
        for (uint32_t i = 0; i < w; i++) written = written || g_aluWritten[r - kB1Alu0 + i];
        if (written) continue;  // (set by the call itself: the same on every pass, the logic bytes stay)
        put3(arm->rsrc, dst + 4 * k, r, w);
    }
#else
    (void)base; (void)dst; (void)reg; (void)words;
#endif
}
void b1_guest_read(int path, uint32_t addr, uint32_t size) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (!g_into || path < 0 || path > 4) return;
    const int w = g_chk.active ? 1 : 0;
    const gx2::submitted::Snapshot* snap = gx2::active_snapshot();
    g_guestWhy[w][!snap ? 0 : snap->find(addr, 0) ? 2 : 1]++;
    uint32_t* a = g_guestAddr[w][path];
    if (g_guest[w][path] < 4) {
        a[g_guest[w][path]] = addr;
        g_guestSize[w][path][g_guest[w][path]] = size;
        g_guestHeld[w][path][g_guest[w][path]] = snap ? snap->block_size(addr) : 0;
    }
    g_guest[w][path]++;
#else
    (void)path; (void)addr; (void)size;
#endif
}
void b1_src_keep(const void* from, const void* to) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (fast(8)) {  // (the same in a flat table)
        const bool v = fast_verify();
        if (!g_into) {
            if (!g_fkept.empty()) g_fkept.erase(to);
            if (v && !g_kept.empty()) g_kept.erase(to);
            return;
        }
        for (Armed& a : g_armed)
            if (a.base == from) {
                Kept& k = g_fkept.get(to);
                k.src = a.src;
                k.rsrc = a.rsrc;
                if (v) g_kept[to] = k;
                return;
            }
        g_fkept.erase(to);
        if (v) g_kept.erase(to);
        return;
    }
    if (!g_into) {
        if (!g_kept.empty()) g_kept.erase(to);
        return;
    }
    for (Armed& a : g_armed)
        if (a.base == from) {
            Kept& k = g_kept[to];
            k.src = a.src;
            k.rsrc = a.rsrc;
            return;
        }
    g_kept.erase(to);
#else
    (void)from; (void)to;
#endif
}
void b1_src_inherit(const void* key, const void* base) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (!g_into) return;
    const Kept* kept;
    if (fast(8)) {
        kept = g_fkept.find(key);
        if (fast_verify()) {
            auto it = g_kept.find(key);
            fv((kept != nullptr) == (it != g_kept.end()) &&
                   (!kept || (kept->src == it->second.src && kept->rsrc == it->second.rsrc)), "kept lists");
        }
    } else {
        auto it = g_kept.find(key);
        kept = it == g_kept.end() ? nullptr : &it->second;
    }
    if (!kept) return;
    for (Armed& a : g_armed)
        if (a.base == base) {
            a.src.insert(a.src.end(), kept->src.begin(), kept->src.end());
            const auto& rs = kept->rsrc;
            for (size_t i = 0; i + 3 <= rs.size(); i += 3) {  // (registers the call wrote since then: its own now)
                bool written = false;
                for (uint32_t w = 0; w < rs[i + 2]; w++) written = written || g_aluWritten[rs[i + 1] - kB1Alu0 + w];
                if (!written) put3(a.rsrc, rs[i], rs[i + 1], rs[i + 2]);
            }
            return;
        }
#else
    (void)key; (void)base;
#endif
}
void b1_alu_written(uint32_t first, uint32_t n) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (!g_into) return;
    const uint32_t lo = std::max(first, kB1Alu0), hi = std::min(first + n, kB1Alu0 + kB1AluWords);
    for (uint32_t r = lo; r < hi; r++) g_aluWritten.set(r - kB1Alu0);
#else
    (void)first; (void)n;
#endif
}
void b1_note_instances(uint64_t buffer, uint64_t offset, uint64_t sliceSize, const uint32_t* sources, size_t entries) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (!g_into || !entries) return;
    Seg& s = *g_into;
    s.irecs.push_back({buffer, offset, sliceSize, uint32_t(entries), s.isrc.size()});
    s.isrc.insert(s.isrc.end(), sources, sources + 2 * entries);
#endif
}
void b1_vtx_src(const std::vector<uint8_t>* container, size_t off, uint32_t addr, uint32_t size) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (!g_into || !vtx_site(addr, size)) return;
    g_vpend.push_back({container, off, addr, size});
#else
    (void)container; (void)off; (void)addr; (void)size;
#endif
}
void b1_note_alloc(uint64_t buffer, uint64_t offset, uint64_t size, void* mapped) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (!g_into || !check_on()) return;
    g_into->allocs.push_back({buffer, offset, size, mapped, rprof::g_upload_kind, 0});
#else
    (void)buffer; (void)offset; (void)size; (void)mapped;
#endif
}
#ifdef CHEAPIB_B1_RENDER
namespace b1 {
int hold_of(const void* slot) {
    for (size_t i = 0; i < g_holds.size(); i++)
        if (g_holds[i].slot == slot) return int(i);
    return -1;
}
// a user still recording (the active slot, not sealed since the replay)
bool recording_user(const Hold& h) {
    const Renderer::Submission* active = &R.submissions[R.activeSubmission];
    for (const auto& [u, mark] : h.users)
        if (u == active && u->serial <= mark) return true;
    return false;
}
// the hold stays (not cleaned) at this retirement
bool keep(int hi, bool activating) {
    if (hi < 0) return false;
    const Hold& h = g_holds[size_t(hi)];
    if (activating) return false;
    if (h.slot == g_held) return true;
    if (recording_user(h)) { g_st.keptForUser++; return true; }
    return false;
}
}  // namespace b1
#endif
bool b1_hold(const void* slot) {
#ifdef CHEAPIB_B1_RENDER
    if (!slot || b1::g_holds.empty()) return false;
    return b1::keep(b1::hold_of(slot), false);
#else
    (void)slot;
    return false;
#endif
}
bool b1_skip(const void* slot) {
#ifdef CHEAPIB_B1_RENDER
    static const bool on = [] { const char* e = render_replay::env("WWHD_CHEAPIB_B1_SKIP"); return e && atoi(e); }();  // (render_replay.h)
    if (!on || !slot || slot != b1::g_held || !b1::g_cap.valid) return false;
    b1::g_st.skips++;
    return true;
#else
    (void)slot;
    return false;
#endif
}
bool b1_retire(void* slot, bool activating) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (!slot || g_holds.empty()) return false;
    auto* sub = static_cast<Renderer::Submission*>(slot);
    const int hi = hold_of(sub);
    if (hi >= 0) {
        if (keep(hi, activating)) return true;
        // released: the submissions that replayed from it first (their commands bind its sets, blocks, objects)
        if (activating && recording_user(g_holds[size_t(hi)])) g_st.unsealedAtActivation++;
        std::vector<std::pair<Renderer::Submission*, uint64_t>> users = std::move(g_holds[size_t(hi)].users);
        if (b2_requested()) g_b2_free.push_back(g_holds[size_t(hi)].step);  // (its secondaries: after these retire)
        g_holds.erase(g_holds.begin() + hi);
        if (sub == g_held) {
            g_held = nullptr;
            g_cap.valid = false;
            g_st.released++;
        }
        g_st.holdsReleased++;
        for (auto& [u, mark] : users) {
            if (u == sub || !u->pending || u->serial <= mark) continue;
            g_st.userRetired++;
            const uint64_t t0 = rprof::now_ns();  // (no fence query here: the emitter may still be submitting it)
            retire_submission(*u);
            const uint64_t dt = rprof::now_ns() - t0;
            if (dt > 100000) g_st.userWaits++;  // (waited over 0.1 ms)
            g_st.userWaitNs += double(dt);
        }
        return false;
    }
    // a retired slot submitted after a held one: its deferred objects go with the newest such hold (a replay after
    // this slot may still use them)
    if (!sub->pending) return false;
    Hold* to = nullptr;
    for (auto& h : g_holds)
        if (h.slot->pending && h.slot->serial < sub->serial) to = &h;
    if (!to) return false;
    const size_t n = sub->garbageBuffers.size() + sub->garbageFramebuffers.size() + sub->garbageImages.size() +
                     sub->garbageCacheRegions.size();
    if (!n) return false;
    auto move_into = [](auto& from, auto& into) {
        into.insert(into.end(), std::make_move_iterator(from.begin()), std::make_move_iterator(from.end()));
        from.clear();
    };
    move_into(sub->garbageBuffers, to->slot->garbageBuffers);
    move_into(sub->garbageFramebuffers, to->slot->garbageFramebuffers);
    move_into(sub->garbageImages, to->slot->garbageImages);
    move_into(sub->garbageCacheRegions, to->slot->garbageCacheRegions);
    g_st.adoptedSlots++;
    g_st.adoptedObjects += n;
#else
    (void)slot;
    (void)activating;
#endif
    return false;
}
void b1_note_written(void* surface) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (g_cap.cur < 0 || !surface) return;
    auto& w = g_cap.segs[size_t(g_cap.cur)].written;
    if (w.empty() || w.back() != surface) w.push_back(static_cast<Surface*>(surface));
#else
    (void)surface;
#endif
}
void b1_note_image(void* surface) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (g_cap.cur < 0 || !surface) return;
    Seg& s = g_cap.segs[size_t(g_cap.cur)];
    auto* sf = static_cast<Surface*>(surface);
    for (const Img& m : s.imgs)
        if (m.s == sf) return;
    s.imgs.push_back({sf, sf->image, sf->image, sf->layout, sf->layout, sf->use, sf->use});
#else
    (void)surface;
#endif
}
bool g_b2_shadow = false;
void b2_shadow(const void* stream, uint32_t opcode, const void* payload, size_t bytes) {
#ifdef CHEAPIB_B1_RENDER
    if (stream == gfxvk::record_worker_commands()) {
        b1::shadow(opcode, static_cast<const std::byte*>(payload), bytes);
        b1::b2_capture_foreign(opcode);
    }
#else
    (void)stream; (void)opcode; (void)payload; (void)bytes;
#endif
}
#define CHEAPIB_B2_EMITTER
#include "cheapib_b2.h"  // (b2: the emitter part)
#undef CHEAPIB_B2_EMITTER
// render thread, at each swap (swap_dump_label): the counters
void b1_frame() {
#ifdef CHEAPIB_B1_RENDER
    if (b1::requested()) b1::report();
#endif
}

}  // namespace cheapib
