// cheapib b2: secondary command buffers for replayed rendering instances (decomp build, fast mode; cheapib/DESIGN.md
// §2.2 b2). Included by cheapib_b1.h inside namespace cheapib::b1 (render part) and at its end (the emitter part).
// Switch WWHD_CHEAPIB_B2=1, with WWHD_CHEAPIB_B1=1 (and what that needs); otherwise off.
//
// b1 re-appends a replayed segment's commands to the frame's primary, so the emitter ("Vulkan record") replays every
// command again on every in-between. b2 lets the emitter record each rendering instance once per step:
// - logic pass: every rendering instance (vkCmdBeginRendering / vkCmdBeginRenderPass to its end, possibly spanning
//   several calls) is noted: where it begins and ends in the segments, the state bound at its begin and end (a shadow
//   of the producer stream's last state commands), its attachment formats (dynamic rendering) or render pass and
//   framebuffer (render pass objects, Android). An instance qualifies when it closes within the pass, binds no
//   pipeline future, its segments are all replayable and nothing outside a call was appended while it was open;
// - uniforms in place: on an in-between, a segment's uniform slices are refreshed at the logic pass's own offsets (the
//   offsets a secondary bakes stay valid): this call's bytes go to a staging slice of the recording slot at once, the
//   copies wait ("pending") until the next point outside rendering: right before an instance's begin is emitted, or
//   at the end of a call that ends outside rendering. One barrier (shader reads, transfers -> transfer), the copies,
//   one barrier (transfer -> shader reads). Queue order and the first barrier keep earlier reads before the overwrite;
// - a qualifying instance's commands are withheld while its calls replay; when its end comes, the pending copies are
//   emitted, then the begin (contents: secondary command buffers), one execute, the end, and the state at its end
//   (a primary's state is undefined after an execute). At the step's first in-between the emitter records the
//   instance into a secondary (SIMULTANEOUS_USE, RENDER_PASS_CONTINUE, inheritance = render pass + framebuffer, or the
//   rendering formats) from a command pool of that step; later in-betweens execute it again;
// - an instance that does not qualify is emitted inline; segments that begin inside one take b1's path (fresh slices,
//   offsets moved), and an instance whose begin lies in such a segment, or whose draws bind a slice such a segment
//   uploads, does not qualify either (fixpoint at the step's first in-between);
// - if anything else is appended while an instance is withheld (a call not replayed, a layout fix-up, renderer work
//   outside calls), the withheld part is emitted inline first ("aborted": pending copies, the original begin, the
//   commands so far); the rest of that instance's segments take b1's path in that frame;
// - the step's pool is destroyed by the emitter after the hold's release (its replays retired).
// MoltenVK re-encodes secondaries when they are executed, so the Mac understates the emitter's saving; Turnip runs
// them as IB2s.
// (no #pragma once: included twice, once per part)

#ifndef CHEAPIB_B2_EMITTER
// ---------------------------------------------------------------------------------------------- render thread part
bool b2_requested() {
    static const bool on = [] {
        const char* e = getenv("WWHD_CHEAPIB_B2");
        if (!e || !atoi(e)) return false;
        if (!requested()) {
            LOG("[cheapib] WWHD_CHEAPIB_B2=1 needs WWHD_CHEAPIB_B1=1: off");
            return false;
        }
        LOG("[cheapib] b2: replayed rendering instances run as secondary command buffers recorded once per step; "
            "uniform slices refreshed in place");
        return true;
    }();
    return on;
}
constexpr bool is_begin(uint32_t op) { return op == 24 || op == 26; }
constexpr bool is_end(uint32_t op) { return op == 6 || op == 27; }
constexpr bool left_out(uint32_t op) { return op == 9 || op == 10 || op == 28 || op == 29 || op == 31; }
// the producer stream's state commands, latest per kind (header + payload each), in their order
struct Shadow {
    uint64_t seq;
    std::vector<std::byte> cmd;
};
std::unordered_map<uint64_t, Shadow> g_shadow;
uint64_t g_shadowSeq = 0;
bool g_shadowFuture = false;  // a pipeline future was bound since the last pipeline bind (not replayable)
std::vector<std::byte> shadow_bytes() {
    std::vector<const Shadow*> v;
    v.reserve(g_shadow.size());
    for (auto& [k, e] : g_shadow) v.push_back(&e);
    std::sort(v.begin(), v.end(), [](const Shadow* a, const Shadow* b) { return a->seq < b->seq; });
    std::vector<std::byte> out;
    for (const Shadow* e : v) out.insert(out.end(), e->cmd.begin(), e->cmd.end());
    return out;
}
void shadow(uint32_t op, const std::byte* p, size_t n) {
    auto u32 = [&](size_t at) {
        uint32_t v = 0;
        if (at + 4 <= n) memcpy(&v, p + at, 4);
        return v;
    };
    const size_t c = sizeof(VkCommandBuffer);
    uint64_t key;
    switch (op) {
    case 28: g_shadow.clear(); g_shadowFuture = false; return;  // (a new command buffer: no state)
    case 31:
        if (u32(0) == uint32_t(gfxvk::record_mt::Ext::BindFuture)) g_shadowFuture = true;
        return;
    case 1: key = u32(c); if (key == VK_PIPELINE_BIND_POINT_GRAPHICS) g_shadowFuture = false; break;
    case 2: case 21: key = 0; break;
    case 7: case 8: case 11: case 12: case 18: key = u32(c); break;
    case 19: key = (uint64_t(u32(c)) << 32) | u32(c + 4 + sizeof(VkPipelineLayout)); break;
    case 20: key = (uint64_t(u32(c + sizeof(VkPipelineLayout))) << 32) | u32(c + sizeof(VkPipelineLayout) + 4); break;
    default: return;
    }
    Shadow& e = g_shadow[(uint64_t(op) << 56) ^ key];
    e.seq = ++g_shadowSeq;
    const uint32_t h[2] = {op, uint32_t(n)};
    e.cmd.resize(8 + n);
    memcpy(e.cmd.data(), h, 8);
    memcpy(e.cmd.data() + 8, p, n);
}
// the extension payloads
struct B2Exec {
    VkCommandBuffer primary;
    uint64_t id, step;
    std::vector<std::byte>* build;  // (first execute of the step: the bytes to record; the emitter deletes them)
    // inheritance: a render pass object (renderPass set) or dynamic rendering (the formats)
    VkRenderPass renderPass;
    VkFramebuffer framebuffer;
    uint32_t colorCount;
    VkFormat colors[8], depth, stencil;
};
struct B2Free {
    uint64_t step;
};
std::vector<uint64_t> g_b2_free;  // steps whose holds were released (their pools to destroy)
// a rendering instance of the logic pass
struct Inst {
    int seg0 = -1, seg1 = -1;           // segments of its begin and end
    uint32_t at0 = 0, at1 = 0;          // byte offsets of the begin and end commands in those segments' cmds
    std::vector<std::byte> pre, post;   // the state bound at its begin and at its end
    bool legacy = false, future = false, foreign = false, fmt = false, ok = false, built = false;
    bool endOutside = false;            // ended by the renderer outside a call (the frame's last pass): seg1 = the last
                                        // call in it, at1 = the end of its commands
    const char* why = nullptr;          // (not qualifying: the first reason)
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    uint32_t colorCount = 0;
    VkFormat colors[8] = {}, depth = VK_FORMAT_UNDEFINED, stencil = VK_FORMAT_UNDEFINED;
};
std::vector<Inst> g_insts;
int g_open = -1;                    // (capture) the instance open now
uint64_t g_finalStep = ~0ull;       // the step whose instances were judged
struct B2Stats {
    uint64_t insts = 0, qualified = 0, notClosed = 0, future = 0, foreign = 0, badSeg = 0, beginMoved = 0, cross = 0,
             noFormats = 0, executed = 0, built = 0, aborted = 0, inPlace = 0, moved = 0, copies = 0, regions = 0,
             copyBytes = 0, refused = 0, foreignReplay = 0, endedOutside = 0;
} g_b2;
void b2_flush_frees(PodCommands* stream) {
    while (stream && !g_b2_free.empty()) {
        const B2Free f{g_b2_free.back()};
        if (!gfxvk::record_mt::append(*stream, gfxvk::record_mt::Ext::CheapibB2Free,
                                      {reinterpret_cast<const std::byte*>(&f), sizeof f}))
            return;
        g_b2_free.pop_back();
    }
}
// capture: the segment's edges (cheapib_b1.h capture_begin / capture_end)
void b2_capture_edge(Seg& s, bool end) {
    if (!b2_requested() || end) return;
    if (&s == &g_cap.segs[0]) {  // (the logic pass's first call)
        g_insts.clear();
        g_open = -1;
    }
}
void inst_formats(Inst& in) {
    const ::gfxvk::PassState& ps = ::gfxvk::pass_state();
    if (in.fmt || in.legacy || !ps.rendering || !ps.tracked) return;
    for (uint32_t i = 0; i < in.colorCount && i < 8; i++)
        if (in.colors[i] != VK_FORMAT_UNDEFINED) {  // (marked: a view is bound)
            Surface* c = ps.colors[i];
            if (!c) return;
            in.colors[i] = c->fmt.pixel;
        }
    if (in.depth != VK_FORMAT_UNDEFINED) {
        if (!ps.depth) return;
        in.depth = ps.depth->fmt.pixel;
    }
    if (in.stencil != VK_FORMAT_UNDEFINED) in.stencil = in.depth;
    in.fmt = true;
}
// capture: each command of the producer stream appended during a call (cheapib_b1.h b1_capture), before it is stored
void b2_capture_cmd(Seg& s, uint32_t op, const std::byte* p, size_t n) {
    if (!b2_requested()) return;
    if (g_open >= 0) inst_formats(g_insts[size_t(g_open)]);
    if (is_begin(op)) {
        Inst& in = g_insts.emplace_back();
        g_open = int(g_insts.size() - 1);
        in.seg0 = g_cap.cur;
        in.at0 = uint32_t(s.cmds.size());
        in.pre = shadow_bytes();
        in.future = g_shadowFuture;
        const size_t c = sizeof(VkCommandBuffer);
        if (op == 26 && n >= c + sizeof(VkRenderPassBeginInfo)) {
            VkRenderPassBeginInfo bi;
            memcpy(&bi, p + c, sizeof bi);
            in.legacy = true;
            in.renderPass = bi.renderPass;
            in.framebuffer = bi.framebuffer;
            in.fmt = true;
        } else if (op == 24 && n >= c + sizeof(VkRenderingInfo) + 4) {
            VkRenderingInfo ri;
            uint32_t mask;
            memcpy(&ri, p + c, sizeof ri);
            memcpy(&mask, p + c + sizeof ri, 4);
            in.colorCount = std::min<uint32_t>(ri.colorAttachmentCount, 8);
            const std::byte* a = p + c + sizeof ri + 4;
            for (uint32_t i = 0; i < in.colorCount; i++) {
                VkRenderingAttachmentInfo ai;
                if (a + (i + 1) * sizeof ai > p + n) break;
                memcpy(&ai, a + i * sizeof ai, sizeof ai);
                in.colors[i] = ai.imageView ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_UNDEFINED;  // (marks; inst_formats)
            }
            if (mask & 1) in.depth = VK_FORMAT_D32_SFLOAT;
            if (mask & 6) in.stencil = VK_FORMAT_D32_SFLOAT;
        }
        return;
    }
    if (g_open < 0) return;
    Inst& in = g_insts[size_t(g_open)];
    if (op == 31 && n >= 4) {
        uint32_t code;
        memcpy(&code, p, 4);
        if (code == uint32_t(gfxvk::record_mt::Ext::BindFuture)) in.future = true;
    }
    if (is_end(op)) {
        in.seg1 = g_cap.cur;
        in.at1 = uint32_t(s.cmds.size());
        in.post = shadow_bytes();
        g_open = -1;
    }
}
// capture (b2_shadow, every append to the producer stream): appended outside a call while an instance is open. Its
// end, as the first such command, still qualifies (endOutside); anything else does not.
void b2_capture_foreign(uint32_t op) {
    if (g_open < 0 || g_b1_capturing || g_finalStep == g_cap.step || g_cap.segs.empty()) return;
    Inst& in = g_insts[size_t(g_open)];
    if (is_end(op) && !in.foreign) {
        in.endOutside = true;
        in.seg1 = int(g_cap.segs.size() - 1);
        in.at1 = uint32_t(g_cap.segs.back().cmds.size());
        in.post = shadow_bytes();
        g_open = -1;
        return;
    }
    in.foreign = true;
}
// the dynamic offsets of each vkCmdBindDescriptorSets (op 19: cmd, point, layout, first, count, sets[count], offsetCount,
// offsets[]) in BYTES [from, to) (header + payload each)
template <class F> void each_dynamic_offset(const std::byte* p, const std::byte* e, F&& f) {
    while (p < e) {
        uint32_t op, n;
        memcpy(&op, p, 4);
        memcpy(&n, p + 4, 4);
        const std::byte* pl = p + 8;
        p = pl + n;
        if (op != 19) continue;
        size_t q = sizeof(VkCommandBuffer) + sizeof(VkPipelineBindPoint) + sizeof(VkPipelineLayout) + 4;
        if (q + 4 > n) continue;
        uint32_t count, offsetCount;
        memcpy(&count, pl + q, 4);
        q += 4 + size_t(count) * sizeof(VkDescriptorSet);
        if (q + 4 > n) continue;
        memcpy(&offsetCount, pl + q, 4);
        q += 4;
        for (uint32_t i = 0; i < offsetCount && q + 4 <= n; i++, q += 4) {
            uint32_t off;
            memcpy(&off, pl + q, 4);
            f(off);
        }
    }
}
// the step's first in-between: which instances qualify, which segments take b1's path (Seg::startInst / moved)
void b2_finalize() {
    if (g_finalStep == g_cap.step) return;
    g_finalStep = g_cap.step;
    g_open = -1;
    // the instance open at each segment's start
    {
        int open = -1;
        size_t next = 0;  // instances in begin order
        for (size_t k = 0; k < g_cap.segs.size(); k++) {
            Seg& s = g_cap.segs[k];
            s.startInst = open;
            if (open >= 0 && g_insts[size_t(open)].seg1 == int(k)) open = -1;
            while (next < g_insts.size() && g_insts[next].seg0 == int(k)) {
                open = g_insts[next].seg1 == int(k) ? -1 : int(next);
                next++;
            }
        }
    }
    for (Inst& in : g_insts) {
        in.why = nullptr;
        if (in.seg1 < 0) in.why = "not closed";
        else if (in.future) in.why = "future";
        else if (in.foreign) in.why = "foreign";
        else if (!in.fmt) in.why = "formats";
        else
            for (int k = in.seg0; k <= in.seg1; k++)
                if (g_cap.segs[size_t(k)].bad) in.why = "segment";
        in.ok = !in.why;
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (Seg& s : g_cap.segs)  // (inside rendering at its start, not in a qualifying instance)
            s.moved = s.begin.rendering && (s.startInst < 0 || !g_insts[size_t(s.startInst)].ok);
        std::unordered_set<uint64_t> moving;
        for (const Seg& s : g_cap.segs)
            if (s.moved)
                for (const Rec& r : s.recs) moving.insert(r.offset);
        for (Inst& in : g_insts) {
            if (!in.ok) continue;
            if (g_cap.segs[size_t(in.seg0)].moved) in.why = "begin moved";
            else if (!moving.empty()) {
                bool hit = false;
                for (int k = in.seg0; k <= in.seg1 && !hit; k++) {
                    const Seg& s = g_cap.segs[size_t(k)];
                    const std::byte* b = s.cmds.data() + (k == in.seg0 ? in.at0 : 0);
                    const std::byte* e = s.cmds.data() + (k == in.seg1 ? in.at1 : s.cmds.size());
                    each_dynamic_offset(b, e, [&](uint32_t off) { hit = hit || moving.count(off); });
                }
                if (hit) in.why = "cross";
            }
            if (in.why) {
                in.ok = false;
                changed = true;
            }
        }
    }
    for (const Inst& in : g_insts) {
        g_b2.insts++;
        if (in.ok) { g_b2.qualified++; continue; }
        const char* w = in.why;
        (w[0] == 'n' ? g_b2.notClosed : w[0] == 'f' && w[1] == 'u' ? g_b2.future : w[0] == 'f' && w[1] == 'o' ? g_b2.foreign
         : w[0] == 's' ? g_b2.badSeg : w[0] == 'b' ? g_b2.beginMoved : w[0] == 'c' ? g_b2.cross : g_b2.noFormats)++;
    }
}
// replay-time state (one in-between frame)
struct Pending {
    uint64_t src, dst;
    VkBufferCopy c;
};
std::vector<Pending> g_pending;
VkDeviceSize g_pendingBytes = 0;
int g_buf = -1;              // the instance withheld now
int g_bufLast = -1;          // its last segment replayed so far
bool g_inReplay = false;     // (b2_guard: our own appends)
uint64_t g_abortFrame = ~0ull;
int g_aborted = -1;          // the instance aborted in frame g_abortFrame
bool append_cmd(PodCommands* stream, uint32_t op, const std::byte* pl, uint32_t n, VkCommandBuffer cmd) {
    static std::vector<std::byte> scratch;
    scratch.assign(pl, pl + n);
    if (n >= sizeof(VkCommandBuffer)) memcpy(scratch.data(), &cmd, sizeof cmd);
    return stream->append_raw(op, {scratch.data(), scratch.size()});
}
// commands of [p, e) (header + payload each) appended with this frame's command buffer (queries and extensions left out)
bool append_range(PodCommands* stream, const std::byte* p, const std::byte* e, VkCommandBuffer cmd) {
    while (p < e) {
        uint32_t op, n;
        memcpy(&op, p, 4);
        memcpy(&n, p + 4, 4);
        const std::byte* pl = p + 8;
        p = pl + n;
        if (left_out(op)) {
            g_st.skipped++;
            continue;
        }
        if (!append_cmd(stream, op, pl, n, cmd)) return false;
        g_st.cmds++;
        g_st.bytes += n + 8;
    }
    return true;
}
bool flush_copies(PodCommands* stream, VkCommandBuffer cmd) {
    if (g_pending.empty()) return true;
    VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                           VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                               VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_ACCESS_TRANSFER_WRITE_BIT};
    // (stage 2: batches' instance entries are vertex streams)
    VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT};
    bool ok = stream->vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1,
                                           &before, 0, nullptr, 0, nullptr);
    std::sort(g_pending.begin(), g_pending.end(),
              [](const Pending& a, const Pending& b) { return a.src != b.src ? a.src < b.src : a.dst < b.dst; });
    static std::vector<VkBufferCopy> copies;
    for (size_t i = 0; ok && i < g_pending.size();) {
        size_t j = i;
        copies.clear();
        while (j < g_pending.size() && g_pending[j].src == g_pending[i].src && g_pending[j].dst == g_pending[i].dst)
            copies.push_back(g_pending[j++].c);
        ok = stream->vkCmdCopyBuffer(cmd, reinterpret_cast<VkBuffer>(g_pending[i].src),
                                     reinterpret_cast<VkBuffer>(g_pending[i].dst), uint32_t(copies.size()), copies.data());
        g_b2.copies++;
        i = j;
    }
    ok = ok && stream->vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
                                            &after, 0, nullptr, 0, nullptr);
    g_b2.regions += g_pending.size();
    g_b2.copyBytes += g_pendingBytes;
    g_pending.clear();
    g_pendingBytes = 0;
    if (!ok) LOG("[cheapib] b2: the producer stream refused the refresh (%d)", int(stream->failure_reason()));
    return ok;
}
// the withheld instance emitted inline (pending copies, its begin, its commands so far)
void b2_abort() {
    if (g_buf < 0) return;
    const bool was = g_inReplay;
    g_inReplay = true;
    g_b2_guard = false;
    PodCommands* stream = record_worker_commands();
    const VkCommandBuffer cmd = command_buffer();
    const Inst& in = g_insts[size_t(g_buf)];
    bool ok = stream && flush_copies(stream, cmd);
    for (int k = in.seg0; ok && k <= g_bufLast; k++) {
        const Seg& s = g_cap.segs[size_t(k)];
        ok = append_range(stream, s.cmds.data() + (k == in.seg0 ? in.at0 : 0), s.cmds.data() + s.cmds.size(), cmd);
    }
    if (!ok) LOG("[cheapib] b2: the producer stream refused an aborted instance's commands");
    g_b2.aborted++;
    g_aborted = g_buf;
    g_abortFrame = g_st.frames;
    g_buf = -1;
    g_inReplay = was;
}
// does segment IDX replay in place (b2) or on b1's path (moved offsets)? (cheapib_b1.h replay, before its room check)
bool b2_inplace(uint32_t idx) {
    b2_finalize();
    const Seg& s = g_cap.segs[idx];
    if (s.moved) return false;
    if (s.startInst >= 0 && g_abortFrame == g_st.frames && g_aborted == s.startInst) return false;
    return true;
}
// the withheld instance: pending copies, its begin (contents: secondary command buffers), one execute (the step's
// first in-between: the emitter records the secondary first). The caller appends the end (and the state after it).
bool emit_withheld(PodCommands* stream, VkCommandBuffer cmd) {
    bool ok = true;
    Inst& in = g_insts[size_t(g_buf)];
    ok = flush_copies(stream, cmd);
    const Seg& s0 = g_cap.segs[size_t(in.seg0)];
    uint32_t bop, bn;
    memcpy(&bop, s0.cmds.data() + in.at0, 4);
    memcpy(&bn, s0.cmds.data() + in.at0 + 4, 4);
    static std::vector<std::byte> begin;
    begin.assign(s0.cmds.data() + in.at0 + 8, s0.cmds.data() + in.at0 + 8 + bn);
    memcpy(begin.data(), &cmd, sizeof cmd);
    if (bop == 24) {
        VkRenderingFlags f;
        const size_t fo = sizeof(VkCommandBuffer) + offsetof(VkRenderingInfo, flags);
        memcpy(&f, begin.data() + fo, sizeof f);
        f |= VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
        memcpy(begin.data() + fo, &f, sizeof f);
    } else {
        const VkSubpassContents c = VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS;
        memcpy(begin.data() + begin.size() - sizeof c, &c, sizeof c);
    }
    ok = ok && stream->append_raw(bop, {begin.data(), begin.size()});
    B2Exec x{cmd, (g_cap.step << 12) | uint64_t(g_buf), g_cap.step, nullptr, in.renderPass, in.framebuffer,
             in.colorCount, {}, in.depth, in.stencil};
    memcpy(x.colors, in.colors, sizeof x.colors);
    if (!in.built) {
        x.build = new std::vector<std::byte>(in.pre);
        for (int k = in.seg0; k <= in.seg1; k++) {
            const Seg& sk = g_cap.segs[size_t(k)];
            const std::byte* q = sk.cmds.data() + (k == in.seg0 ? in.at0 : 0);
            const std::byte* qe = sk.cmds.data() + (k == in.seg1 ? in.at1 : sk.cmds.size());
            if (k == in.seg0) {  // (past the begin)
                uint32_t qn;
                memcpy(&qn, q + 4, 4);
                q += 8 + qn;
            }
            while (q < qe) {
                uint32_t qop, qn;
                memcpy(&qop, q, 4);
                memcpy(&qn, q + 4, 4);
                if (!left_out(qop)) x.build->insert(x.build->end(), q, q + 8 + qn);
                q += 8 + qn;
            }
        }
    }
    if (ok && gfxvk::record_mt::append(*stream, gfxvk::record_mt::Ext::CheapibB2Exec,
                                       {reinterpret_cast<const std::byte*>(&x), sizeof x})) {
        if (x.build) {
            in.built = true;
            g_b2.built++;
        }
        g_b2.executed++;
        g_st.cmds++;
    } else {
        delete x.build;
        ok = false;
    }
    g_buf = -1;
    g_b2_guard = false;
    return ok;
}
// a replayed segment on b2's path (cheapib_b1.h replay, after the checks, image fix-ups and the hold's user);
// false: refused (executed instead)
bool b2_replay(Seg& s, uint32_t index, PodCommands* stream, VkCommandBuffer cmd) {
    g_inReplay = true;
    b2_flush_frees(stream);
    g_b2.inPlace++;
    // this call's uniform bytes to staging now; the copies into the logic pass's slices wait for a point outside
    // rendering
    const gx2::submitted::Snapshot* snap = gx2::active_snapshot();
    VkDeviceSize total = 0;
    for (const Rec& r : s.recs)
        if (r.hp()) total += 16ull * (r.n + r.rn);  // (host-packed: its entries, each copied in place)
        else if (snap && snap->find(r.addr, 0) && snap->block_size(r.addr) >= r.delta + r.size)
            total += (VkDeviceSize(r.size) + 15) & ~VkDeviceSize(15);
    for (const IRec& r : s.irecs) total += 16ull * r.entries;  // (stage 2: instance entries, refreshed as a whole)
    for (const VRec& r : s.vrecs)  // (stage 2: vertex segments from guest memory, each in place)
        for (uint32_t i = 0; i < r.n; i++) total += (VkDeviceSize(s.vsrc[r.at + 3 * i + 2]) + 15) & ~VkDeviceSize(15);
    g_st.recs += s.recs.size();
    if (total) {
        const UploadSlice st = allocate_upload(total, 16);
        if (!st.buffer || !st.mapped) {
            g_b2.refused++;
            g_inReplay = false;
            return false;
        }
        VkDeviceSize at = 0;
        for (const Rec& r : s.recs) {
            if (r.hp()) {  // host-packed: each entry from this pass's block (or register) into its place in the logic slice
                const uint32_t* q = s.ssrc.data() + r.at;
                for (uint32_t i = 0; i < r.n; i++, q += 3) {
                    const uint8_t* p = snap ? snap->find(q[1], 0) : nullptr;
                    if (!p || snap->block_size(q[1]) < q[2] + 16) { g_st.skept++; continue; }
                    memcpy(static_cast<uint8_t*>(st.mapped) + at, p + q[2], 16);
                    g_pending.push_back({uint64_t(st.buffer), r.buffer, {st.offset + at, r.offset + q[0], 16}});
                    at += 16;
                    g_st.srefreshed++;
                }
                const uint32_t* regs = gx2::regs();
                q = s.rsrc.data() + r.rat;
                for (uint32_t i = 0; i < r.rn; i++, q += 3) {
                    memcpy(static_cast<uint8_t*>(st.mapped) + at, regs + q[1], 4ull * q[2]);
                    g_pending.push_back({uint64_t(st.buffer), r.buffer, {st.offset + at, r.offset + q[0], 4ull * q[2]}});
                    at += 16;
                }
                g_st.rentries += r.rn;
                g_st.srecs++;
                g_st.sentries += r.n;
                g_st.refreshed++;
                continue;
            }
            const uint8_t* src = snap->find(r.addr, 0);
            if (src && snap->block_size(r.addr) >= r.delta + r.size) {
                memcpy(static_cast<uint8_t*>(st.mapped) + at, src + r.delta, r.size);
                g_pending.push_back({uint64_t(st.buffer), r.buffer, {st.offset + at, r.offset, r.size}});
                at += (VkDeviceSize(r.size) + 15) & ~VkDeviceSize(15);
                g_st.refreshed++;
            } else {
                g_st.fromLogic++;
            }
        }
        for (const IRec& r : s.irecs) {
            Renderer::UploadBlock* b = logic_block(r.buffer);
            if (!b) continue;
            fill_instances(s, r, snap, static_cast<const uint8_t*>(b->buffer.mapped) + r.offset,
                           static_cast<uint8_t*>(st.mapped) + at);
            g_pending.push_back({uint64_t(st.buffer), r.buffer, {st.offset + at, r.offset, 16ull * r.entries}});
            at += 16ull * r.entries;
        }
        for (const VRec& r : s.vrecs) {
            const uint32_t* q = s.vsrc.data() + r.at;
            for (uint32_t i = 0; i < r.n; i++, q += 3) {
                memcpy(static_cast<uint8_t*>(st.mapped) + at, PPC_MEM_BASE + q[1], q[2]);
                g_pending.push_back({uint64_t(st.buffer), r.buffer, {st.offset + at, r.offset + q[0], q[2]}});
                at += (VkDeviceSize(q[2]) + 15) & ~VkDeviceSize(15);
                g_st.vbytes += q[2];
            }
            g_st.vrecs++;
            g_st.vsegs += r.n;
        }
        g_pendingBytes += total;
    } else {
        g_st.fromLogic += s.recs.size();
    }
    // the commands: an instance that qualifies is withheld from its begin to its end
    bool ok = true;
    const std::byte* p = s.cmds.data();
    const std::byte* e = p + s.cmds.size();
    const std::byte* run = g_buf >= 0 ? e : p;  // (the start of commands still to append inline; none while withheld)
    while (ok && p < e) {
        const std::byte* at = p;
        uint32_t op, n;
        memcpy(&op, p, 4);
        memcpy(&n, p + 4, 4);
        const std::byte* pl = p + 8;
        p = pl + n;
        if (is_begin(op)) {
            int ii = -1;
            for (size_t i = 0; i < g_insts.size() && ii < 0; i++)
                if (g_insts[i].seg0 == int(index) && g_insts[i].at0 == uint32_t(at - s.cmds.data())) ii = int(i);
            if (run < at) ok = append_range(stream, run, at, cmd);
            if (ii >= 0 && g_insts[size_t(ii)].ok) {  // withheld
                g_buf = ii;
                g_bufLast = int(index);
                g_b2_guard = true;
                run = e;
            } else {
                ok = ok && flush_copies(stream, cmd);
                run = at;
            }
            continue;
        }
        if (!is_end(op) || g_buf < 0) continue;
        // the withheld instance's end: copies, begin, execute, end, the state at its end
        Inst& in = g_insts[size_t(g_buf)];
        ok = emit_withheld(stream, cmd);
        ok = ok && append_cmd(stream, op, pl, n, cmd);                                          // the end
        ok = ok && append_range(stream, in.post.data(), in.post.data() + in.post.size(), cmd);  // its state
        run = p;
    }
    if (g_buf >= 0) g_bufLast = int(index);
    if (ok && run < e) ok = append_range(stream, run, e, cmd);
    if (ok && g_buf < 0 && !s.end.rendering) ok = flush_copies(stream, cmd);
    if (!ok) LOG("[cheapib] b2: the producer stream refused a replayed command (%d)", int(stream->failure_reason()));
    g_inReplay = false;
    return true;
}
void b2_note_b1path() { g_b2.moved++; }
void b2_report() {
    if (!b2_requested()) return;
    const double f = 1200.0;
    LOG("[cheapib] b2 per 1200 frames: rendering instances %llu (as secondaries %llu; not: open at the end %llu, pipeline "
        "future %llu, foreign commands %llu, segment not replayable %llu, begin on b1's path %llu, binds a moved slice "
        "%llu, formats %llu); in-betweens: secondaries executed %llu (recorded %llu; ended by the renderer %llu), aborted %llu (foreign appends %llu); "
        "segments in place %llu, on b1's path %llu, refused %llu; refresh %.1f copies/frame, %.1f regions/frame, %.1f "
        "KB/frame",
        (unsigned long long)g_b2.insts, (unsigned long long)g_b2.qualified, (unsigned long long)g_b2.notClosed,
        (unsigned long long)g_b2.future, (unsigned long long)g_b2.foreign, (unsigned long long)g_b2.badSeg,
        (unsigned long long)g_b2.beginMoved, (unsigned long long)g_b2.cross, (unsigned long long)g_b2.noFormats,
        (unsigned long long)g_b2.executed, (unsigned long long)g_b2.built, (unsigned long long)g_b2.endedOutside,
        (unsigned long long)g_b2.aborted,
        (unsigned long long)g_b2.foreignReplay, (unsigned long long)g_b2.inPlace, (unsigned long long)g_b2.moved,
        (unsigned long long)g_b2.refused, g_b2.copies / f, g_b2.regions / f, g_b2.copyBytes / f / 1024);
    g_b2 = B2Stats{};
}

#else
// ---------------------------------------------------------------------------------------------- emitter part
bool g_b2_guard = false;
// pod_commands.cpp append/append_scalars, while an instance is withheld: anything appended outside the replay emits
// the withheld part first
void b2_guard(const void* stream, uint32_t opcode) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b1;
    if (g_inReplay || stream != gfxvk::record_worker_commands() || g_buf < 0) return;
    const Inst& in = g_insts[size_t(g_buf)];
    if (is_end(opcode) && in.endOutside && g_bufLast == in.seg1) {  // (the renderer ends it: emitted before that end)
        g_inReplay = true;
        PodCommands* s = record_worker_commands();
        if (!emit_withheld(s, command_buffer()))
            LOG("[cheapib] b2: the producer stream refused a withheld instance (%d)", int(s->failure_reason()));
        g_b2.endedOutside++;
        g_inReplay = false;
        return;
    }
    g_b2.foreignReplay++;
    b2_abort();
#else
    (void)stream; (void)opcode;
#endif
}
#ifdef CHEAPIB_B1_RENDER
namespace b2em {
std::unordered_map<uint64_t, VkCommandBuffer> g_sec;   // id -> secondary
std::unordered_map<uint64_t, VkCommandPool> g_pools;   // step -> pool
PFN_vkCmdExecuteCommands g_execute = nullptr;
}  // namespace b2em
#endif
void b2_ext(uint32_t code, const std::byte* bytes, size_t size, bool execute) {
#ifdef CHEAPIB_B1_RENDER
    using namespace b2em;
    using namespace gfxvk;
    if (code == uint32_t(record_mt::Ext::CheapibB2Free)) {
        if (size != sizeof(b1::B2Free)) throw std::runtime_error("bad cheapib b2 free");
        if (!execute) return;
        b1::B2Free f;
        memcpy(&f, bytes, sizeof f);
        if (auto it = g_pools.find(f.step); it != g_pools.end()) {
            vkDestroyCommandPool(R.device, it->second, nullptr);
            g_pools.erase(it);
        }
        for (auto it = g_sec.begin(); it != g_sec.end();)
            it = (it->first >> 12) == f.step ? g_sec.erase(it) : std::next(it);
        return;
    }
    if (size != sizeof(b1::B2Exec)) throw std::runtime_error("bad cheapib b2 exec");
    if (!execute) return;
    b1::B2Exec x;
    memcpy(&x, bytes, sizeof x);
    if (!g_execute) g_execute = reinterpret_cast<PFN_vkCmdExecuteCommands>(vkGetDeviceProcAddr(R.device, "vkCmdExecuteCommands"));
    if (x.build) {
        std::unique_ptr<std::vector<std::byte>> build(x.build);
        VkCommandPool& pool = g_pools[x.step];
        if (!pool) {
            VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pi.queueFamilyIndex = R.queueFamily;
            vk_check(vkCreateCommandPool(R.device, &pi, nullptr, &pool), "create command pool (cheapib b2)");
        }
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer sec = VK_NULL_HANDLE;
        vk_check(vkAllocateCommandBuffers(R.device, &ai, &sec), "allocate secondary (cheapib b2)");
        VkCommandBufferInheritanceRenderingInfo ri{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO};
        VkCommandBufferInheritanceInfo ii{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
        if (x.renderPass) {
            ii.renderPass = x.renderPass;
            ii.subpass = 0;
            ii.framebuffer = x.framebuffer;
        } else {
            ri.colorAttachmentCount = x.colorCount;
            ri.pColorAttachmentFormats = x.colors;
            ri.depthAttachmentFormat = x.depth;
            ri.stencilAttachmentFormat = x.stencil;
            ri.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            ii.pNext = &ri;
        }
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        bi.pInheritanceInfo = &ii;
        vk_check(vkBeginCommandBuffer(sec, &bi), "begin secondary (cheapib b2)");
        for (size_t at = 0; at + 8 <= build->size();) {  // (each command's buffer: the secondary)
            uint32_t n;
            memcpy(&n, build->data() + at + 4, 4);
            if (n >= sizeof sec) memcpy(build->data() + at + 8, &sec, sizeof sec);
            at += 8 + n;
        }
        PodCommands::replay(*build);
        vk_check(vkEndCommandBuffer(sec), "end secondary (cheapib b2)");
        g_sec[x.id] = sec;
    }
    auto it = g_sec.find(x.id);
    if (it == g_sec.end()) throw std::runtime_error("cheapib b2: no secondary for an execute");
    g_execute(x.primary, 1, &it->second);
#else
    (void)code; (void)bytes; (void)size; (void)execute;
#endif
}
#endif
