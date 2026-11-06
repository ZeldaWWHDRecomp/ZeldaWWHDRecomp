// Local diagnostic (CMake -DWWHD_WRITE_WATCH=ON, never in releases): which code wrote each byte of a few guest memory
// windows, from the first guest function entry to the guest capture (guest_capture.cpp): is a byte in such a window the game's
// own output, or the port's? Output holds bytes of guest memory: never share or commit it.
//
//   WWHD_WW_WINDOWS=FILE   lines "ADDR SIZE" (hex), any byte ranges
//
// Two sources, both exact about the value:
//  - every st8/16/32/64, stfs/stfd (through them), psq store, dcbz and successful stwcx. in generated code AND in host code
//    goes through wwhd_ww_store/note when it touches a window: the return address is the writer. Generated code: f_XXXXXXXX,
//    the game; anything else: host code, with a backtrace kept the first time a writer is seen.
//  - the block imports report themselves with the guest LR (wwhd_ww_block).
//  - other host code that writes guest memory without those primitives (memcpy/memset through mem::ptr, kernel reads) is found by
//    comparing the windows with a shadow copy at every guest function entry. Such a "raw" write is attributed to the interval
//    since the last entry (the thread that noticed it and what each core last entered), not to one instruction.
//    Comparing happens before the entered function runs, so a raw write is never mistaken for the entered function's.
// Store + shadow update are one critical section, so a noticing thread never sees a primitive store as raw.
#ifdef WWHD_WRITE_WATCH
#include <dlfcn.h>
#include <execinfo.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "guest_addr.h"
#include "platform/host.h"
#include "runtime.h"
#include "gfx/renderer.h"

extern "C" { uint8_t wwhd_ww_bm[1u << 21]; }

namespace {
struct Win { uint32_t lo, n; std::vector<uint8_t> shadow; std::vector<uint32_t> last; };
struct Writer {
    uint64_t count = 0;
    bool raw = false, import = false;
    void* pc = nullptr;           // store writers: the return address
    std::vector<void*> bt;        // first time seen
    std::string note;             // raw writers: thread + last entries
};
std::vector<Win> g_win;
std::vector<Writer> g_writers;              // index 0: "not written since arming" (the value at the first guest entry)
std::map<void*, uint32_t> g_by_pc;
std::map<std::string, uint32_t> g_by_note;
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
std::atomic<bool> g_armed{false};
std::atomic<uint32_t> g_last_entry[4];      // per core, last guest function entered
thread_local uint32_t t_last_entry = 0;
struct Lock {
    Lock() { while (g_lock.test_and_set(std::memory_order_acquire)) {} }
    ~Lock() { g_lock.clear(std::memory_order_release); }
};

std::string cur_stage() {
    const char* p = (const char*)mem::ptr(GD(0x1046F0B0) + 0x5134);  // dStage_startStage_c (as savestate.cpp)
    size_t n = strnlen(p, 8);
    for (size_t i = 0; i < n; i++)
        if (p[i] < 0x20 || p[i] > 0x7E) return "?";
    return std::string(p, n);
}

uint32_t writer_for_note(const std::string& note, bool raw) {
    auto it = g_by_note.find(note);
    if (it != g_by_note.end()) return it->second;
    Writer wr;
    wr.raw = raw;
    wr.import = !raw;
    wr.note = note;
    g_writers.push_back(std::move(wr));
    return g_by_note[note] = (uint32_t)g_writers.size() - 1;
}

uint32_t writer_for_pc(void* pc) {
    auto it = g_by_pc.find(pc);
    if (it != g_by_pc.end()) return it->second;
    Writer w;
    w.pc = pc;
    void* fr[16];
    int n = backtrace(fr, 16);
    w.bt.assign(fr, fr + n);
    g_writers.push_back(std::move(w));
    return g_by_pc[pc] = (uint32_t)g_writers.size() - 1;
}

// the [ea, ea+n) bytes that are in a window now hold what memory holds: writer w owns them
void take(uint32_t ea, unsigned n, uint32_t w) {
    g_writers[w].count++;
    // WWHD_WW_TRACE=ADDR:LEN (hex): every store into that range, in order, with the render frame and the writer
    static uint32_t tlo = 0, thi = 0;
    static FILE* tf = [] {
        const char* e = getenv("WWHD_WW_TRACE");
        const char* d = getenv("WWHD_GUEST_CAPTURE_DIR");
        unsigned a = 0, l = 0;
        if (!e || !d || sscanf(e, "%x:%x", &a, &l) != 2) return (FILE*)nullptr;
        tlo = a; thi = a + l;
        return fopen((std::string(d) + ".trace.txt").c_str(), "w");
    }();
    if (tf && ea < thi && tlo < ea + n) {
        fprintf(tf, "frame=%llu stage=%s W%u %08X", (unsigned long long)render::frame_count(), cur_stage().c_str(), w, ea);
        for (unsigned i = 0; i < n; i++) fprintf(tf, " %02X", *mem::ptr(ea + i));
        fputc('\n', tf);
        fflush(tf);
    }
    for (Win& x : g_win) {
        uint32_t lo = std::max(ea, x.lo), hi = std::min(ea + n, x.lo + x.n);
        for (uint32_t a = lo; a < hi; a++) {
            x.shadow[a - x.lo] = *mem::ptr(a);
            x.last[a - x.lo] = w;
        }
    }
}

[[gnu::noinline]] void scan_raw(uint32_t addr) {
    Lock l;
    for (Win& x : g_win) {
        const uint8_t* m = mem::ptr(x.lo);
        if (!memcmp(m, x.shadow.data(), x.n)) continue;
        char tn[32] = {};
        host::get_thread_name(tn, sizeof tn);
        Cpu* c = threads::current();
        char note[240];
        snprintf(note, sizeof note, "stage=%s raw seen by thread %s at entry of %08X; its previous entry %08X lr=%08X; cores last entered %08X %08X %08X", cur_stage().c_str(), tn, addr,
                 t_last_entry, c ? c->lr : 0, g_last_entry[0].load(), g_last_entry[1].load(), g_last_entry[2].load());
        uint32_t w = writer_for_note(note, true);
        g_writers[w].count++;
        static FILE* rawlog = [] { const char* d = getenv("WWHD_GUEST_CAPTURE_DIR"); return d ? fopen((std::string(d) + ".rawlog.txt").c_str(), "w") : nullptr; }();
        for (uint32_t i = 0; i < x.n; i++)
            if (m[i] != x.shadow[i]) {
                if (rawlog) fprintf(rawlog, "%08X %02X->%02X W%u %s\n", x.lo + i, x.shadow[i], m[i], w, note);
                x.shadow[i] = m[i];
                x.last[i] = w;
            }
        if (rawlog) fflush(rawlog);
    }
}

std::string sym(void* pc) {
    Dl_info di;
    char b[300];
    if (dladdr(pc, &di) && di.dli_sname) snprintf(b, sizeof b, "%s+0x%lx", di.dli_sname, (unsigned long)((char*)pc - (char*)di.dli_saddr));
    else if (dladdr(pc, &di) && di.dli_fname) snprintf(b, sizeof b, "%s+0x%lx", strrchr(di.dli_fname, '/') ? strrchr(di.dli_fname, '/') + 1 : di.dli_fname, (unsigned long)((char*)pc - (char*)di.dli_fbase));
    else snprintf(b, sizeof b, "%p", pc);
    return b;
}

__attribute__((constructor)) void ww_init() {
    const char* f = getenv("WWHD_WW_WINDOWS");
    FILE* fp = f ? fopen(f, "r") : nullptr;
    if (f && !fp) { fprintf(stderr, "[ww] cannot read %s\n", f); _exit(2); }
    g_writers.emplace_back();
    unsigned a, n;
    while (fp && fscanf(fp, "%x %x", &a, &n) == 2) {
        Win w;
        w.lo = a;
        w.n = n;
        g_win.push_back(std::move(w));
        for (uint32_t b = a & ~0xFFu; b < a + n; b += 0x100) wwhd_ww_bm[b >> 11] |= (uint8_t)(1u << ((b >> 8) & 7));
    }
    if (fp) fclose(fp);
    if (!g_win.empty()) g_ppc_trace = 1;
}
}  // namespace

extern "C" [[gnu::noinline]] void wwhd_ww_store(uint32_t ea, const void* bytes, unsigned n) {
    void* pc = __builtin_return_address(0);
    Lock l;
    memcpy(mem::ptr(ea), bytes, n);
    if (g_armed.load(std::memory_order_relaxed)) take(ea, n, writer_for_pc(pc));
}
extern "C" [[gnu::noinline]] void wwhd_ww_note(uint32_t ea, unsigned n) {
    void* pc = __builtin_return_address(0);
    Lock l;
    if (g_armed.load(std::memory_order_relaxed)) take(ea, n, writer_for_pc(pc));
}

// the block imports (OSBlockMove, OSBlockSet, memcpy, memmove, memset): host code doing what original guest code asked for,
// reported with the guest LR so such a write is exact, not "raw". The writer is the import plus its calling site.
void wwhd_ww_block(const char* name, uint32_t lr, uint32_t dst, uint32_t n, const std::function<void()>& op) {
    bool hit = false;
    if (n) for (uint64_t b = dst & ~0xFFull; b < (uint64_t)dst + n && !hit; b += 0x100) hit = wwhd_ww_bm[(uint32_t)b >> 11] >> (((uint32_t)b >> 8) & 7) & 1;
    if (!hit) { op(); return; }
    Lock l;
    op();
    if (!g_armed.load(std::memory_order_relaxed)) return;
    char note[120];
    snprintf(note, sizeof note, "stage=%s import %s called from guest lr=%08X", cur_stage().c_str(), name, lr);
    uint32_t w = writer_for_note(note, false);
    take(dst, n, w);
}

// every HLE import's return: what it (or another thread's host code, in the same instant) wrote without the primitives
void wwhd_ww_after_import(const char* name, uint32_t lr, uint32_t r3, uint32_t a0, uint32_t a1) {
    // WWHD_WW_RETLOG=FILE: what OSSendMessage answered at the call site 0276035C
    if (lr == GC(0x0276035C) && !strcmp(name, "OSSendMessage")) {
        static FILE* rl = [] { const char* e = getenv("WWHD_WW_RETLOG"); return e ? fopen(e, "w") : nullptr; }();
        if (rl) {
            char tn[32] = {};
            host::get_thread_name(tn, sizeof tn);
            fprintf(rl, "frame=%llu stage=%s thread=%s OSSendMessage q=%08X msg0=%08X lr=0276035C -> r3=%08X\n", (unsigned long long)render::frame_count(), cur_stage().c_str(), tn, a0, ld32(a1), r3);
            fflush(rl);
        }
    }
    if (g_win.empty() || !g_armed.load(std::memory_order_acquire)) return;
    bool any = false;
    for (const Win& x : g_win)
        if (memcmp(mem::ptr(x.lo), x.shadow.data(), x.n)) { any = true; break; }
    if (!any) return;
    Lock l;
    for (Win& x : g_win) {
        const uint8_t* m = mem::ptr(x.lo);
        if (!memcmp(m, x.shadow.data(), x.n)) continue;
        char note[120];
        snprintf(note, sizeof note, "stage=%s import %s (seen at its return) called from guest lr=%08X", cur_stage().c_str(), name, lr);
        uint32_t w = writer_for_note(note, false);
        g_writers[w].count++;
        for (uint32_t i = 0; i < x.n; i++)
            if (m[i] != x.shadow[i]) { x.shadow[i] = m[i]; x.last[i] = w; }
    }
}

// every guest function entry
void wwhd_ww_enter(uint32_t addr) {
    if (g_win.empty()) return;
    if (!g_armed.load(std::memory_order_acquire)) {
        Lock l;
        if (!g_armed.load(std::memory_order_relaxed)) {
            for (Win& x : g_win) {
                x.shadow.assign(mem::ptr(x.lo), mem::ptr(x.lo) + x.n);
                x.last.assign(x.n, 0);
            }
            g_armed.store(true, std::memory_order_release);
        }
    }
    for (const Win& x : g_win)
        if (memcmp(mem::ptr(x.lo), x.shadow.data(), x.n)) { scan_raw(addr); break; }
    t_last_entry = addr;
    if (Cpu* c = threads::current()) g_last_entry[c->core & 3].store(addr, std::memory_order_relaxed);
}

// writes.txt: "BYTES ADDR LEN HEXBYTES WRITER" runs of the final state; writers.txt: every writer with its class and count
void wwhd_ww_report(const std::string& dir) {
    Lock l;
    for (const Win& x : g_win)
        if (memcmp(mem::ptr(x.lo), x.shadow.data(), x.n)) {
            FILE* e = fopen((dir + "/ww-unsettled.txt").c_str(), "a");
            if (e) { fprintf(e, "window %08X differs from its shadow at the report\n", x.lo); fclose(e); }
        }
    FILE* o = fopen((dir + "/writes.txt").c_str(), "w");
    FILE* w = fopen((dir + "/writers.txt").c_str(), "w");
    if (!o || !w) return;
    for (const Win& x : g_win)
        for (uint32_t i = 0; i < x.n;) {
            uint32_t j = i;
            while (j < x.n && x.last[j] == x.last[i]) j++;
            fprintf(o, "%08X %X W%u ", x.lo + i, j - i, x.last[i]);
            for (uint32_t k = i; k < j; k++) fprintf(o, "%02X", x.shadow[k]);
            fputc('\n', o);
            i = j;
        }
    fclose(o);
    fprintf(w, "W0 none count=0 not written after the first guest function entry\n");
    for (size_t i = 1; i < g_writers.size(); i++) {
        const Writer& x = g_writers[i];
        if (x.raw || x.import) { fprintf(w, "W%zu %s count=%llu %s\n", i, x.raw ? "raw" : "import", (unsigned long long)x.count, x.note.c_str()); continue; }
        std::string s = sym(x.pc);
        fprintf(w, "W%zu %s count=%llu %s\n", i, s.rfind("f_", 0) == 0 ? "guest" : "host", (unsigned long long)x.count, s.c_str());
        if (s.rfind("f_", 0) != 0)
            for (void* p : x.bt) fprintf(w, "    %s\n", sym(p).c_str());
    }
    fclose(w);
}
#endif
