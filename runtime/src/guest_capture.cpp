// Debug tooling, off unless WWHD_GUEST_CAPTURE is set: a snapshot of guest state at the entry of one guest function, for
// checking decompiled functions against the state they really run from. At the first matching entry it freezes every other
// guest thread (the save-state quiesce), writes the calling thread's CPU (pre-prologue: PPC_ENTER runs before the function's
// first instruction) and all guest memory regions a full save state holds, then ends the process. Not on Windows.
// The output contains game code and data, like a full save state: never share or commit it.
//
//   WWHD_GUEST_CAPTURE=FUNC            hex guest address of the function (canonical USA address)
//   WWHD_GUEST_CAPTURE_DIR=DIR         output directory (required)
//   WWHD_GUEST_CAPTURE_STAGE=NAME      only while the current stage (dStage_startStage_c) is NAME
//   WWHD_GUEST_CAPTURE_LR=LO[-HI]      only when the caller's LR is LO (or in LO..HI)
//   WWHD_GUEST_CAPTURE_IF=rN=VAL       only when register rN == VAL (hex)
//   WWHD_GUEST_CAPTURE_SKIP=N          skip the first N matching calls (default 0: the first one)
//   WWHD_GUEST_CAPTURE_LOG=N           log the first N matching calls (default 64)
//   WWHD_GUEST_CAPTURE_DRY=1           log only, never capture
//   WWHD_GUEST_CAPTURE_NOMEM=1         no mem file (a write-watch run: its report holds the window bytes)
//   WWHD_GUEST_CAPTURE_J3DLIGHT=1      027F53CC only: log the model's lighting count, light slots 0/1 and fog kind, and
//                                      match only calls with count >= 2 (or r5 >= 2), a nonnull slot 0 or 1 and fog kind 2
//   WWHD_GUEST_CAPTURE_LIGHTS=nearfar  with J3DLIGHT: count >= 1, and the fog's far == near (the far == near arm)
//   WWHD_GUEST_CAPTURE_LIGHTS=scales   with J3DLIGHT: count >= 1, and a light in slot 0/1 with signed scale != colour scale
//   WWHD_GUEST_CAPTURE_LIGHTS=any      with J3DLIGHT: count >= 1 (a survey; the log shows the values)
#ifndef _WIN32
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "build_info.h"
#include "guest_addr.h"
#include "platform/host.h"
#include "runtime.h"
#include "gfx/renderer.h"

extern int g_ppc_trace;
#ifdef WWHD_WRITE_WATCH
void wwhd_ww_report(const std::string& dir);  // guest_write_watch.cpp (diagnostic build)
#endif

namespace {
struct Cfg {
    uint32_t func = 0, lr_lo = 0, lr_hi = 0;
    int if_reg = -1;
    uint32_t if_val = 0;
    long skip = 0, log = 64;
    bool dry = false, j3dlight = false;
    std::string lights;
    std::string dir, stage;
};
Cfg g_cfg;
std::atomic<long> g_matches{0};
std::atomic<bool> g_done{false};
std::mutex g_cap_m;

std::string cur_stage() {
    const char* p = (const char*)mem::ptr(GD(0x1046F0B0) + 0x5134);  // dStage_startStage_c (as savestate.cpp)
    size_t n = strnlen(p, 8);
    for (size_t i = 0; i < n; i++)
        if (p[i] < 0x20 || p[i] > 0x7E) return "";
    return std::string(p, n);
}

void write_u32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }

// mem.bin: "WWCAPMEM" u32 version(1) u32 page(0x1000), then records {u32 addr, bytes[0x1000]} for every
// nonzero page of every region, host byte order of the guest's big-endian memory (raw bytes).
size_t dump_region(FILE* f, uint32_t base, uint32_t size, FILE* idx) {
    const uint32_t kPage = 0x1000, kChunk = 0x10000;
    size_t pages = 0;
    for (uint32_t off = 0; off < size; off += kChunk) {
        uint8_t* c = mem::ptr(base + off);
        if (!host::memory_touched(c, kChunk)) continue;
        for (uint32_t po = 0; po < kChunk && off + po < size; po += kPage) {
            const uint64_t* q = (const uint64_t*)(c + po);
            bool zero = true;
            for (size_t i = 0; i < kPage / 8 && zero; i++) zero = q[i] == 0;
            if (zero) continue;
            write_u32(f, base + off + po);
            fwrite(c + po, 1, kPage, f);
            pages++;
        }
    }
    fprintf(idx, "region %08X %08X pages %zu\n", base, size, pages);
    return pages;
}

void capture(Cpu* c, uint32_t addr, const std::string& stage, long nth) {
    std::lock_guard<std::mutex> lk(g_cap_m);
    if (g_done.exchange(true)) return;
    std::string busy;
    bool quiet = threads::quiesce(5000, busy, 1, 100);  // other guest threads park; never thawed (we exit)
    mkdir(g_cfg.dir.c_str(), 0700);
    std::string d = g_cfg.dir;
    FILE* meta = fopen((d + "/capture.txt").c_str(), "w");
    FILE* cpu = fopen((d + "/cpu.bin").c_str(), "wb");
    // mem.bin.zst through the zstd tool (captures are ~0.5 GB raw, ~0.2 GB compressed); WWHD_GUEST_CAPTURE_RAW=1: mem.bin
    const bool nomem = getenv("WWHD_GUEST_CAPTURE_NOMEM") != nullptr;
    const bool raw = nomem || getenv("WWHD_GUEST_CAPTURE_RAW") != nullptr;
    FILE* m = nomem ? fopen("/dev/null", "wb") : raw ? fopen((d + "/mem.bin").c_str(), "wb") : popen(("zstd -q -3 -T4 -o '" + d + "/mem.bin.zst'").c_str(), "w");
    if (!meta || !cpu || !m) { LOG("[capture] cannot write to %s", d.c_str()); _exit(3); }
    char tn[64] = {};
    host::get_thread_name(tn, sizeof tn);
    uint32_t sb = 0, st = 0;
    bool have_sb = threads::current_stack_bounds(c, sb, st);
    fprintf(meta, "format wwhd-guest-capture 1\n");
    fprintf(meta, "runtime %s %s\n", build::version(), build::commit());
    fprintf(meta, "func %08X\nlr %08X\nstage %s\nmatch %ld\nthread %s\nguest_thread %08X\ncore %u\nmain_thread %u\n", addr,
            c->lr, stage.c_str(), nth, tn, threads::current_thread(), c->core, c->main_thread);
    fprintf(meta, "frame %llu\n", (unsigned long long)render::frame_count());
    if (have_sb) fprintf(meta, "stack %08X %08X\n", sb, st);
    fprintf(meta, "quiesce %s%s\n", quiet ? "ok" : "FAILED busy:", quiet ? "" : busy.c_str());
    for (int i = 0; i < 32; i++) fprintf(meta, "r%d %08X\n", i, c->r[i]);
    fprintf(meta, "ctr %08X\ncr", c->ctr);
    for (int i = 0; i < 32; i++) fprintf(meta, "%u", c->cr[i]);
    fprintf(meta, "\nxer so=%u ov=%u ca=%u bc=%u\nfpscr %08X\n", c->xer_so, c->xer_ov, c->xer_ca, c->xer_bc, c->fpscr);
    for (int i = 0; i < 32; i++) {
        uint64_t a, b;
        memcpy(&a, &c->f[i].ps0, 8);
        memcpy(&b, &c->f[i].ps1, 8);
        fprintf(meta, "f%d %016llX %016llX\n", i, (unsigned long long)a, (unsigned long long)b);
    }
    for (int i = 0; i < 8; i++) fprintf(meta, "gqr%d %08X\n", i, c->gqr[i]);
    fwrite(c, sizeof(Cpu), 1, cpu);
    fclose(cpu);
    fwrite("WWCAPMEM", 1, 8, m);
    write_u32(m, 1);
    write_u32(m, 0x1000);
    size_t total = 0;
    uint32_t top = (mem::runtime_top() + 0xFFFF) & ~0xFFFFu;
    total += dump_region(m, mem::kMem2Start, mem::kMem2End - mem::kMem2Start, meta);
    total += dump_region(m, mem::kRuntimeStart, top - mem::kRuntimeStart, meta);
    total += dump_region(m, mem::kFixedStart, mem::kFixedSize, meta);
    total += dump_region(m, mem::kFgBucket, mem::kFgBucketSize, meta);
    total += dump_region(m, mem::kMem1, mem::kMem1Size, meta);
    if (raw ? fclose(m) != 0 : pclose(m) != 0) fprintf(meta, "mem_write FAILED\n");
    // runtime-owned guest objects (port allocations in 0x6xxxxxxx): which bytes there are the port's own
    for (const auto& a : mem::runtime_alloc_log())
        fprintf(meta, "runtime_alloc %08X %08X %016llX\n", a.addr, a.size, (unsigned long long)a.tag);
    fprintf(meta, "pages %zu\nend\n", total);
    fclose(meta);
    LOG("[capture] %08X lr=%08X stage=%s match=%ld thread=%s quiesce=%s: %zu pages -> %s", addr, c->lr, stage.c_str(), nth, tn,
        quiet ? "ok" : busy.c_str(), total, d.c_str());
#ifdef WWHD_WRITE_WATCH
    wwhd_ww_report(d);
#endif
    fflush(nullptr);
    _exit(0);
}
}  // namespace

// called from ppc_trace_enter (trace.cpp) for every guest function entry while g_ppc_trace is on
void guest_capture_enter(uint32_t addr) {
    if (addr != g_cfg.func || g_done.load(std::memory_order_relaxed)) return;
    Cpu* c = threads::current();
    if (!c) return;
    if (g_cfg.lr_lo && (c->lr < g_cfg.lr_lo || c->lr > g_cfg.lr_hi)) return;
    if (g_cfg.if_reg >= 0 && c->r[g_cfg.if_reg] != g_cfg.if_val) return;
    std::string stage = cur_stage();
    if (!g_cfg.stage.empty() && stage != g_cfg.stage) return;
    char extra[300] = "";
    if (g_cfg.j3dlight) {  // ModelPipeline_l (r3): +0x70 lighting count, +0xAC model data -> +0x10 material 0
        auto ok = [](uint32_t a) { return a >= 0x10000000u && a < 0x50000000u; };
        uint32_t m = c->r[3], data = ok(m) ? ld32(m + 0xAC) : 0, mat = ok(data) ? ld32(data + 0x10) : 0;
        uint32_t prov = ok(mat) ? ld32(mat + 0x10) : 0, fprov = ok(mat) ? ld32(mat + 0x20) : 0;
        uint32_t vt = ok(prov) ? ld32(prov) : 0, get = ok(vt) ? ld32(vt + 0x44) : 0;
        uint32_t fvt = ok(fprov) ? ld32(fprov) : 0, fget = ok(fvt) ? ld32(fvt + 0x14) : 0;
        uint32_t l0 = ok(prov) ? ld32(prov + 0x14) : 0, l1 = ok(prov) ? ld32(prov + 0x18) : 0;
        uint32_t fog = ok(fprov) ? ld32(fprov + 4) : 0;
        int kind = ok(fog) ? (int)ld8(fog) : -1;
        int32_t count = (int32_t)c->r[5] == -1 && ok(m) ? (int32_t)ld32(m + 0x70) : (int32_t)c->r[5];
        auto fl = [&](uint32_t a) { uint32_t v = ok(a) ? ld32(a) : 0; float f; memcpy(&f, &v, 4); return f; };
        float nearv = ok(fog) ? fl(fog + 4) : 0, farv = ok(fog) ? fl(fog + 8) : 0;
        float c0 = l0 ? fl(l0 + 0x24) : 0, s0 = l0 ? fl(l0 + 0x28) : 0, c1 = l1 ? fl(l1 + 0x24) : 0, s1 = l1 ? fl(l1 + 0x28) : 0;
        snprintf(extra, sizeof extra, " count=%d lightget=%08X l0=%08X l1=%08X fogget=%08X fog=%08X kind=%d near=%g far=%g scales0=%g/%g scales1=%g/%g", count, get, l0, l1, fget, fog, kind, nearv, farv, c0, s0, c1, s1);
        const int mincount = g_cfg.lights.empty() ? 2 : 1;
        bool want = count >= mincount && get == GC(0x027F75A0) && fget == GC(0x027F763C) && (l0 || l1) && kind == 2;
        if (g_cfg.lights == "nearfar") want = want && farv == nearv;
        if (g_cfg.lights == "scales") want = want && ((l0 && c0 != s0) || (l1 && c1 != s1));
        if (!want) {
            static std::atomic<int> nlog{0};
            if (nlog++ < 40) LOG("[capture] skip %08X lr=%08X r3=%08X%s", addr, c->lr, c->r[3], extra);
            return;
        }
    }
    long n = g_matches.fetch_add(1);
    if (n < g_cfg.log) {
        char tn[32] = {};
        host::get_thread_name(tn, sizeof tn);
        LOG("[capture] match %ld %08X stage=%s thread=%s frame=%llu lr=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X%s", n, addr,
            stage.c_str(), tn, (unsigned long long)render::frame_count(), c->lr, c->r[3], c->r[4], c->r[5], c->r[6], c->r[7], extra);
    }
    if (g_cfg.dry || n < g_cfg.skip) return;
    capture(c, addr, stage, n);
}

__attribute__((constructor)) static void guest_capture_init() {
    const char* f = getenv("WWHD_GUEST_CAPTURE");
    if (!f) return;
    g_cfg.func = (uint32_t)strtoul(f, nullptr, 16);
    if (const char* e = getenv("WWHD_GUEST_CAPTURE_DIR")) g_cfg.dir = e;
    if (const char* e = getenv("WWHD_GUEST_CAPTURE_STAGE")) g_cfg.stage = e;
    if (const char* e = getenv("WWHD_GUEST_CAPTURE_LR")) {
        unsigned lo = 0, hi = 0;
        int k = sscanf(e, "%x-%x", &lo, &hi);
        g_cfg.lr_lo = lo;
        g_cfg.lr_hi = k == 2 ? hi : lo;
    }
    if (const char* e = getenv("WWHD_GUEST_CAPTURE_IF")) {
        unsigned reg = 0;
        char val[32];
        if (sscanf(e, "r%u=%31s", &reg, val) == 2 && reg < 32) { g_cfg.if_reg = (int)reg; g_cfg.if_val = (uint32_t)strtoul(val, nullptr, 16); }
    }
    if (const char* e = getenv("WWHD_GUEST_CAPTURE_SKIP")) g_cfg.skip = atol(e);
    if (const char* e = getenv("WWHD_GUEST_CAPTURE_LOG")) g_cfg.log = atol(e);
    g_cfg.dry = getenv("WWHD_GUEST_CAPTURE_DRY") != nullptr;
    g_cfg.j3dlight = getenv("WWHD_GUEST_CAPTURE_J3DLIGHT") != nullptr;
    if (const char* e = getenv("WWHD_GUEST_CAPTURE_LIGHTS")) g_cfg.lights = e;
    if (g_cfg.dir.empty() && !g_cfg.dry) {
        fprintf(stderr, "[capture] WWHD_GUEST_CAPTURE needs WWHD_GUEST_CAPTURE_DIR\n");
        _exit(2);
    }
    g_ppc_trace = 1;
}
#else
#include <cstdint>
void guest_capture_enter(uint32_t) {}
#endif
