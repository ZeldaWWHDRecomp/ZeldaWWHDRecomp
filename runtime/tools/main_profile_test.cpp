// Main-thread profile report maths (runtime/src/main_profile.h): per step / per second units,
// sample shares turned into ms, categories, the top list, and the marker on/off.
#include "main_profile.h"

#include <cassert>
#include <cmath>
#include <cstdio>

static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

int main() {
    using namespace mainprof;
    Window w;
    w.seconds = 5.0;
    w.steps = 150;  // 30 steps a second: 33.33 ms each
    w.cpu_ns = 1500000000;  // 1.5 s
    w.held_ns = 3000000000;
    w.dispatches = 300000;
    w.sends = 1500;
    w.send_full = 3;
    w.send_full_ns = 4500000;
    w.samples = 5000;
    w.where = {{"", 2000}, {"wait: event", 1500}, {"core wait", 250}, {"GX2DrawIndexedEx", 500}, {"GX2SetShaderMode", 100},
               {"FSReadFile", 150}, {"OSLockMutex", 500}, {"GX2WaitForVsync", 300}, {"GX2DrawDone", 200}};
    w.samples = 5500;
    w.seconds = 5.5;
    Report r = summarize(w, 3);
    assert(r.per_step && near(r.unit_div, 150));
    assert(near(r.wall_ms, 5500.0 / 150));
    assert(near(r.cpu_ms, 10.0) && near(r.held_ms, 20.0));
    assert(near(r.dispatches, 2000) && near(r.sends, 10) && near(r.send_full, 0.02) && near(r.send_full_ms, 0.03));
    const double ms = (5500.0 / 150) / 5500;  // one sample, ms per step
    assert(near(r.guest_ms, 2000 * ms));
    assert(near(r.wait_ms, 2250 * ms));  // + GX2WaitForVsync, GX2DrawDone
    assert(near(r.gx2_ms, 600 * ms));
    assert(near(r.file_ms, 150 * ms));
    assert(near(r.other_ms, 500 * ms));
    assert(near(r.guest_ms + r.wait_ms + r.gx2_ms + r.file_ms + r.other_ms, r.wall_ms));
    assert(is_wait("GX2WaitForVsync") && is_wait("GX2DrawDone") && !is_wait("GX2DrawIndexedEx") && !is_wait("OSWaitEvent"));
    assert(r.top.size() == 3 && r.top[0].name == "wait: event");
    assert(r.top[1].name == "GX2DrawIndexedEx" && r.top[2].name == "OSLockMutex");  // ties: by name
    // no step (paused, loading): per second
    w.steps = 0;
    r = summarize(w);
    assert(!r.per_step && near(r.wall_ms, 1000.0) && near(r.cpu_ms, 1500.0 / 5.5));
    // no samples: counters only
    w.samples = 0;
    w.where.clear();
    r = summarize(w);
    assert(r.top.empty() && r.guest_ms == 0);
    // the marker: only the main thread, only when on
    Cpu main{}, other{};
    main.main_thread = 1;
    {
        Mark m(&main, "OSLockMutex");
        assert(!g_where.load());  // off
    }
    g_on = true;
    {
        Mark a(&main, "OSWaitEvent");
        assert(!strcmp(g_where.load(), "OSWaitEvent"));
        {
            Mark b(&main, "wait: event");
            Mark c(&other, "OSLockMutex");  // another thread: no change
            assert(!strcmp(g_where.load(), "wait: event"));
        }
        assert(!strcmp(g_where.load(), "OSWaitEvent"));
    }
    assert(!g_where.load());
    puts("main_profile: ok");
}
