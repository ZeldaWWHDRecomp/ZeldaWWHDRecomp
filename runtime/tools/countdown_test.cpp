// Exercise the production shared-timer hook without game code or assets.
#include "runtime.h"
#include "countdown.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

extern "C" void hook_025C5A8C(Cpu*);
extern "C" void hook_026E9A38(Cpu*);
extern "C" void hook_0262769C(Cpu*);
extern "C" void site_026277A8(Cpu*);
extern "C" void site_026E9C24(Cpu*);
namespace {
constexpr uint32_t timer = 0x15000000, play = 0x1046F0B0;
bool enabled = false, hold = false;
uint64_t step = 0;
float fraction = 1;
unsigned calls = 0, refreshes = 0;
void map(uint32_t address, size_t size) {
#ifdef _WIN32
    auto p = VirtualAlloc(mem::ptr(address), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto p = mmap(mem::ptr(address), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(p == mem::ptr(address));
}
}
namespace interp {
bool enabled() { return ::enabled; }
bool hold_pass() { return hold; }
uint64_t logic_steps() { return step; }
float pass_t() { return fraction; }
}
// A synthetic draw callback with a counter and an observable side effect. The production
// hook must gate the whole callback, including startup/end delays, not just one counter.
extern "C" void f_025C5A8C_orig(Cpu* c) {
    calls++;
    if (!ld8(c->r[3] + 0x122)) st32(c->r[3] + 0x10C, ld32(c->r[3] + 0x10C) - 1);
    c->r[3] = 1;
}
int main() {
    map(timer, 0x10000);
    map(0x10460000, 0x20000);
    st32(play + 0x5CF0, timer);
    st8(timer + 0x124, 2);
    Cpu c{};
    for (int fps : {30, 60, 120}) for (bool paced : {false, true}) {
        // Interpolation and true60 share this contract; live scenarios select each actual mode.
        enabled = fps != 30; calls = 0; st32(timer + 0x10C, 900);
        for (int tick = 0; tick < 30; tick++) {
            step++;
            const int passes = fps / 30;
            for (int phase = 0; phase < passes; phase++) {
                // A paced renderer may drop in-between passes; this cannot drop logic.
                if (paced && phase && tick % 3 == 0) continue;
                hold = phase != 0; fraction = float(phase + 1) / passes;
                c.r[3] = timer; hook_025C5A8C(&c);
                assert(c.r[3] == 1);
            }
        }
        assert(calls == 30 && ld32(timer + 0x10C) == 870);
    }
    // Digit formatting changes only the presentation register on both displays.
    enabled = true; hold = false; step++; st32(timer + 0x10C, 901);
    c.r[3] = timer; hook_025C5A8C(&c);
    int last = 30034;
    for (int phase = 1; phase <= 4; phase++) {
        fraction = phase / 4.f;
        c.r[7] = 30000; site_026277A8(&c);
        int shown = c.r[7]; assert(shown < last); last = shown;
        c.r[7] = 30000; site_026E9C24(&c); assert(int(c.r[7]) == shown);
        assert(ld32(timer + 0x10C) == 900);
    }
    assert(last == 30000);
    // Layout runs before the next timer draw: still monotonic across the boundary.
    step++; fraction = .25f; c.r[7] = 30000; site_026277A8(&c);
    assert(c.r[7] == 29991);
    // Pause, counter reset, object replacement, stale history and 30 fps remain native.
    st8(timer + 0x122, 1); c.r[7] = 30000; site_026277A8(&c); assert(c.r[7] == 30000);
    st8(timer + 0x122, 0); st32(timer + 0x10C, 1200);
    c.r[7] = 40000; site_026277A8(&c); assert(c.r[7] == 40000);
    st32(timer + 0x10C, 900);
    st32(play + 0x5CF0, timer + 0x1000);
    st8(timer + 0x1000 + 0x124, 2); st32(timer + 0x1000 + 0x10C, 900);
    c.r[7] = 30000; site_026277A8(&c); assert(c.r[7] == 30000);
    st32(play + 0x5CF0, timer); step += 2;
    c.r[7] = 30000; site_026277A8(&c); assert(c.r[7] == 30000);
    enabled = false; c.r[7] = 30000; site_026277A8(&c); assert(c.r[7] == 30000);
    // Refresh only layouts formatted in the current step; keep the caller's CPU intact.
    countdown::reset(); enabled = true; hold = true; c.r[31] = timer;
    st32(timer + 4, 0x10107D10); st32(timer + 0x44, 1); c.r[7] = 30000; site_026E9C24(&c);
    c.r[3] = 123; c.r[7] = 456; countdown::refresh(&c);
    assert(refreshes == 1 && c.r[3] == 123 && c.r[7] == 456);
    step++; countdown::refresh(&c); assert(refreshes == 1);
    c.r[31] = timer; site_026E9C24(&c);
    c.r[3] = timer; hook_026E9A38(&c); countdown::refresh(&c); assert(refreshes == 1);
    site_026E9C24(&c); countdown::reset(); countdown::refresh(&c); assert(refreshes == 1);
    st32(timer + 4, 0x100E5A58); site_026277A8(&c);
    countdown::refresh(&c); assert(refreshes == 2);
    c.r[3] = timer; hook_0262769C(&c); countdown::refresh(&c); assert(refreshes == 2);
    site_026277A8(&c); st32(timer + 0x44, 0);
    countdown::refresh(&c); assert(refreshes == 2);
    countdown::reset(); c.r[31] = 0xfffffff0; st32(play + 0x5CF0, 0xfffffff0);
    c.r[7] = 123; site_026E9C24(&c); countdown::refresh(&c);
    assert(c.r[7] == 123 && refreshes == 2);
    std::puts("countdown_test: 30/60/120 fps, paced drops, shared hold contract, display and pause/reset checks passed");
}

extern "C" void f_026E9A38_orig(Cpu*) {}
extern "C" void f_0262769C_orig(Cpu*) {}
extern "C" void f_026E9BC4(Cpu* c) { refreshes++; c->r[7] = 0; }
extern "C" void f_02627734(Cpu* c) { refreshes++; c->r[3] = 0; }
