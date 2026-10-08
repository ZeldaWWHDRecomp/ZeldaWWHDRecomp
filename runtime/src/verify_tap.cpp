// Recording taps for the verification harness (tools/verify, runtime/include/verify_tap.h).
//
// Off unless WWHD_TAP=<dir> is set and the build has taps (tools/verify/mktap.py). Then each
// tapped function records its first WWHD_TAP_N calls (default 100; WWHD_TAP_EVERY=k records
// every k-th call; WWHD_TAP_AFTER=n starts once the game's step counter g_Counter.mCounter0
// reaches n) to <dir>/<ADDR>/<n>.tap: entry registers, its own loads and stores, every call it
// makes with the registers before and after, exit registers.
#include <filesystem>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "guest_addr.h"
#include "verify_tap.h"  // not verify_tap_gen.h: the recorder's own accesses are not logged

extern "C" {
int tap_begin(Cpu* c, uint32_t func);
void tap_end(Cpu* c);
void tap_call(Cpu* c, PpcFunc fn, uint32_t target);
void tap_dispatch(Cpu* c);
void tap_access(int type, uint32_t ea, int size, uint64_t value);
}

namespace {
struct Recorder {
    uint32_t func, seq;
    std::vector<uint8_t> buf;
    void put(const void* p, size_t n) { buf.insert(buf.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
};
thread_local std::vector<Recorder*> t_stack;
std::mutex g_mu;
std::unordered_map<uint32_t, uint32_t> g_calls, g_saved;
const char* g_dir = nullptr;
uint32_t g_max = 100, g_every = 1, g_after = 0;
std::once_flag g_once;

void init() {
    g_dir = getenv("WWHD_TAP");
    if (const char* e = getenv("WWHD_TAP_N")) g_max = (uint32_t)atoi(e);
    if (const char* e = getenv("WWHD_TAP_EVERY")) g_every = std::max(1, atoi(e));
    if (const char* e = getenv("WWHD_TAP_AFTER")) g_after = (uint32_t)strtoul(e, nullptr, 10);
    if (g_dir) {
        std::error_code ec; std::filesystem::create_directories(g_dir,ec);
        fprintf(stderr, "[tap] recording to %s (%u calls per function, every %u)\n", g_dir, g_max, g_every);
    }
}

void event(Recorder* r, uint8_t type, uint8_t size, uint32_t ea, uint64_t value, const Cpu* c) {
    TapEvent e{type, size, 0, 0, ea, value};
    r->put(&e, sizeof e);
    if (c) {
        TapRegs t;
        tap_regs_from(&t, c);
        r->put(&t, sizeof t);
    }
}
}  // namespace

extern "C" int tap_begin(Cpu* c, uint32_t func) {
    std::call_once(g_once, init);
    if (!g_dir) return 0;
    if (g_after && ld32(GD(0x101FF558)) < g_after) return 0;  // g_Counter.mCounter0
    uint32_t seq;
    {
        std::lock_guard<std::mutex> l(g_mu);
        uint32_t n = g_calls[func]++;
        if (n % g_every || g_saved[func] >= g_max) return 0;
        seq = g_saved[func]++;
    }
    Recorder* r = new Recorder{func, seq, {}};
    TapHeader h{};
    h.magic = TAP_MAGIC;
    h.func = func;
    h.seq = seq;
    h.frame = ld32(GD(0x101FF558));  // g_Counter.mCounter0
    tap_regs_from(&h.entry, c);
    r->put(&h, sizeof h);
    t_stack.push_back(r);
    return 1;
}

extern "C" void tap_end(Cpu* c) {
    Recorder* r = t_stack.back();
    t_stack.pop_back();
    event(r, TAP_END, 0, 0, 0, c);
    char dir[512], path[600];
    snprintf(dir, sizeof dir, "%s/%08X", g_dir, r->func);
    std::error_code ec; std::filesystem::create_directories(dir,ec);
    snprintf(path, sizeof path, "%s/%05u.tap", dir, r->seq);
    if (FILE* f = fopen(path, "wb")) {
        fwrite(r->buf.data(), 1, r->buf.size(), f);
        fclose(f);
    }
    delete r;
}

extern "C" void tap_access(int type, uint32_t ea, int size, uint64_t value) {
    if (!t_stack.empty()) event(t_stack.back(), (uint8_t)type, (uint8_t)size, ea, value, nullptr);
}

extern "C" void tap_call(Cpu* c, PpcFunc fn, uint32_t target) {
    Recorder* r = t_stack.back();
    event(r, TAP_CALL, 0, target, 0, c);
    fn(c);
    event(r, TAP_RET, 0, target, 0, c);
}

extern "C" void tap_dispatch(Cpu* c) {
    Recorder* r = t_stack.back();
    uint32_t target = c->pc;
    event(r, TAP_CALL, 0, target, 1, c);
    ppc_dispatch(c);
    event(r, TAP_RET, 0, target, 1, c);
}
