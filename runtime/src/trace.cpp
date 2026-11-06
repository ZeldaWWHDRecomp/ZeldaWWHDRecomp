#include <atomic>
// Per-thread ring buffer of guest function entries, for debugging.
// Enable with WWHD_TRACE_FUNCS=1; dump with `kill -USR1 <pid>` or at fatal errors.
#include "platform/host.h"
#include <signal.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "runtime.h"

int g_ppc_trace = 0;

namespace {
constexpr uint32_t kRing = 1 << 16;
struct Ring {
    uint32_t buf[kRing];
    uint32_t pos = 0;
    char name[64] = {};
};
std::mutex g_rings_mutex;
std::vector<Ring*> g_rings;
thread_local Ring* t_ring = nullptr;
}  // namespace

// WWHD_WATCH=<hex addr>: log arguments whenever that guest function is entered
static uint32_t g_watch = 0;
static int g_watch_count = 0, g_watch_limit = 200;
static int g_watch_reg = -1;         // WWHD_WATCH_IF=reg=value: only log when r<reg> == value
static uint32_t g_watch_val = 0;

// WWHD_WATCH_R3=<hex>: log every function entered with r3 == value (object/list tracing), with thread
static uint32_t g_watch_r3 = 0;
static std::atomic<int> g_watch_r3_count{0};

void guest_capture_enter(uint32_t addr);  // guest_capture.cpp (WWHD_GUEST_CAPTURE, debug only)
#if WWHD_DECOMP
void scalable_font_guard_enter(uint32_t addr);  // scalable_font_hooks.cpp (WWHD_FONT_GUARD, decomp build)
static bool g_font_guard = false;
#endif
#ifdef WWHD_WRITE_WATCH
void wwhd_ww_enter(uint32_t addr);  // guest_write_watch.cpp (diagnostic build)
#endif
#if WWHD_DECOMP
// WWHD_HOSTRT_TRACE=<file> (decomp build only): entry counts (and up to 8 distinct return addresses) of the port-owned guest
// runtime (GHS C/C++ runtime, nn static wrappers; tools/host_runtime/inventory.py), rewritten every 2 s
// because test runs leave with _Exit. Only these ranges are counted; nothing else is traced.
namespace hostrt {
struct Range { uint32_t lo, hi; };
constexpr Range kRanges[] = {{0x0286C590u, 0x0286DB50u}, {0x028EA038u, 0x028F626Cu}};
constexpr uint32_t kSlots0 = (0x0286DB50u - 0x0286C590u) / 4, kSlots1 = (0x028F626Cu - 0x028EA038u) / 4;
std::atomic<uint64_t> g_count[kSlots0 + kSlots1];
std::atomic<uint32_t> g_lr[kSlots0 + kSlots1][8];
bool g_on = false, g_only = false;
static inline int slot(uint32_t a) {
    if (a - kRanges[0].lo < kRanges[0].hi - kRanges[0].lo) return (int)((a - kRanges[0].lo) / 4);
    if (a - kRanges[1].lo < kRanges[1].hi - kRanges[1].lo) return (int)(kSlots0 + (a - kRanges[1].lo) / 4);
    return -1;
}
static uint32_t addr_of(int i) { return i < (int)kSlots0 ? kRanges[0].lo + 4 * i : kRanges[1].lo + 4 * (i - kSlots0); }
static void enter(uint32_t a) {
    int i = slot(a);
    if (i < 0) return;
    g_count[i].fetch_add(1, std::memory_order_relaxed);
    Cpu* c = threads::current();
    uint32_t lr = c ? c->lr : 0;
    for (auto& e : g_lr[i]) {
        uint32_t v = e.load(std::memory_order_relaxed);
        if (v == lr) break;
        if (v == 0) { uint32_t z = 0; if (e.compare_exchange_strong(z, lr) || z == lr) break; }
    }
}
static void writer(std::string path) {
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::string tmp = path + ".tmp";
        FILE* f = fopen(tmp.c_str(), "w");
        if (!f) continue;
        for (int i = 0; i < (int)(kSlots0 + kSlots1); i++) {
            uint64_t n = g_count[i].load(std::memory_order_relaxed);
            if (!n) continue;
            fprintf(f, "%08X\t%llu\t", addr_of(i), (unsigned long long)n);
            for (auto& e : g_lr[i]) if (uint32_t v = e.load()) fprintf(f, "%08X,", v);
            fputc('\n', f);
        }
        fclose(f);
        rename(tmp.c_str(), path.c_str());
    }
}
}  // namespace hostrt
#endif

extern "C" void ppc_trace_enter(uint32_t addr) {
#if WWHD_DECOMP
    if (hostrt::g_on) {
        hostrt::enter(addr);
        if (hostrt::g_only) return;
    }
#endif
    static const bool guest_capture = getenv("WWHD_GUEST_CAPTURE") != nullptr;
#ifdef WWHD_WRITE_WATCH
    wwhd_ww_enter(addr);  // before the capture hook: its report must see the state up to this entry
#endif
    if (guest_capture) guest_capture_enter(addr);
#if WWHD_DECOMP
    if (g_font_guard) scalable_font_guard_enter(addr);
#endif
    if (g_watch_r3) {
        Cpu* c = threads::current();
        if (c && c->r[3] == g_watch_r3 && g_watch_r3_count++ < 3000) {
            char name[32] = {};
            host::get_thread_name(name, sizeof name);
            log_msg("[watch3] %-14s f_%08X lr=%08X r4=%08X r5=%08X", name, addr, c->lr, c->r[4], c->r[5]);
        }
    }
    if (addr == g_watch && g_watch_count < g_watch_limit) {
        Cpu* c = threads::current();
        if (c && (g_watch_reg < 0 || c->r[g_watch_reg] == g_watch_val)) {
            g_watch_count++;
            char tn[32] = {};
            host::get_thread_name(tn, sizeof tn);
            log_msg("[watch] %s %08X lr=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X | r28=%08X r29=%08X r30=%08X r31=%08X",
                    tn, addr, c->lr, c->r[3], c->r[4], c->r[5], c->r[6], c->r[7], c->r[8], c->r[28], c->r[29], c->r[30], c->r[31]);
            if (const char* e = getenv("WWHD_WATCH_DUMP")) {  // "reg": hex dump 0x100 bytes at r<reg>
                unsigned reg = (unsigned)atoi(e) & 31;
                uint32_t base = c->r[reg];
                for (uint32_t o = 0; o < 0x100; o += 32) {
                    char line[160];
                    int n = snprintf(line, sizeof line, "[watch]   %08X:", base + o);
                    for (uint32_t k = 0; k < 32; k += 4) n += snprintf(line + n, sizeof line - n, " %08X", ld32(base + o + k));
                    log_msg("%s", line);
                }
            }
            if (const char* e = getenv("WWHD_WATCH_STR")) {  // "reg": the C string at r<reg> (e.g. a sound's name)
                const uint32_t at = c->r[(unsigned)atoi(e) & 31];
                char text[64] = {};
                for (uint32_t k = 0; at && k + 1 < sizeof text; k++) {
                    const char ch = (char)ld8(at + k);
                    if (!ch || ch < 0x20 || ch > 0x7E) break;
                    text[k] = ch;
                }
                log_msg("[watch]   \"%s\"", text);
            }
            if (const char* e = getenv("WWHD_WATCH_EXPR")) {  // "reg+off": print ld32(r<reg> + off)
                unsigned reg = 0, off = 0;
                if (sscanf(e, "%u+%x", &reg, &off) == 2 && reg < 32) log_msg("[watch]   ld32(r%u+%X) = %08X", reg, off, ld32(c->r[reg] + off));
            }
        }
    }
    if (!t_ring) {
        t_ring = new Ring();
        host::get_thread_name(t_ring->name, sizeof t_ring->name);
        std::lock_guard<std::mutex> lk(g_rings_mutex);
        g_rings.push_back(t_ring);
    }
    t_ring->buf[t_ring->pos++ & (kRing - 1)] = addr;
}

void trace_dump(FILE* f, unsigned last) {
    std::lock_guard<std::mutex> lk(g_rings_mutex);
    for (Ring* r : g_rings) {
        uint32_t end = __atomic_load_n(&r->pos, __ATOMIC_RELAXED);  // other threads keep running
        fprintf(f, "=== thread '%s' (%u calls)\n", r->name, end);
        uint32_t n = std::min(last, std::min(end, kRing));
        for (uint32_t i = end - n; i != end; i++) fprintf(f, "%08X\n", r->buf[i & (kRing - 1)]);
    }
    fflush(f);
}

static void on_usr1(int) {
    FILE* f = fopen("trace_dump.txt", "w");
    if (f) { trace_dump(f, 4000); fclose(f); }
    fputs("[trace] wrote trace_dump.txt\n",stderr);
}

__attribute__((constructor)) static void trace_init() {
#if WWHD_DECOMP
    if (const char* p = getenv("WWHD_HOSTRT_TRACE")) {
        hostrt::g_on = true;
        g_ppc_trace = 1;
        hostrt::g_only = !getenv("WWHD_WATCH_R3") && !getenv("WWHD_WATCH") && !getenv("WWHD_NAN_PROBE") &&
                         !getenv("WWHD_TRACE_FUNCS") && !getenv("WWHD_GUEST_CAPTURE");
        std::thread(hostrt::writer, std::string(p)).detach();
    }
#endif
    if (const char* w = getenv("WWHD_WATCH_R3")) { g_watch_r3 = (uint32_t)strtoul(w, nullptr, 16); g_ppc_trace = 1; }
    if (const char* w = getenv("WWHD_WATCH")) {
        g_watch = (uint32_t)strtoul(w, nullptr, 16);
        if (const char* l = getenv("WWHD_WATCH_LIMIT")) g_watch_limit = atoi(l);
        if (const char* f = getenv("WWHD_WATCH_IF")) {
            unsigned reg; char val[32];
            if (sscanf(f, "%u=%31s", &reg, val) == 2) { g_watch_reg = (int)(reg & 31); g_watch_val = (uint32_t)strtoul(val, nullptr, 16); }
        }
        g_ppc_trace = 1;
    }
    if (getenv("WWHD_NAN_PROBE")) g_ppc_trace = 1;
    // traps when any function of the replaced scalable font module runs (scalable_font.h). The decomp build
    // only: the normal build runs the game's own font module, so there is nothing to guard or log there.
#if WWHD_DECOMP
    if (getenv("WWHD_FONT_GUARD")) { g_font_guard = true; g_ppc_trace = 1; }
#else
    if (getenv("WWHD_FONT_GUARD") || getenv("WWHD_FONT_LOG"))
        fputs("[font] WWHD_FONT_GUARD / WWHD_FONT_LOG only apply to the decomp build; this build runs the game's own font module\n", stderr);
#endif
    if (getenv("WWHD_TRACE_FUNCS")) {
        g_ppc_trace = 1;
#ifndef _WIN32
        signal(SIGUSR1, on_usr1);
#endif
    }
}
