// Input-to-screen latency test: see latency_test.h.
#include "latency_test.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

#include "game_hooks.h"
#include "runtime.h"

namespace interp { uint64_t logic_steps(); }

namespace latency_test {
namespace {
int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct Config {
    uint32_t bits = 0, count = 0;
    int64_t period = 0, hold = 0;
    uint64_t start_steps = 150;
    uint64_t start_at = 0;  // WWHD_LATENCY_START_AT: start at this logic step (since boot) instead
    FILE* out = nullptr;
};
Config read_config() {
    Config c;
    const char* e = getenv("WWHD_LATENCY_TEST");
    unsigned bits = 0, period = 0, hold = 0, count = 0;
    if (!e || sscanf(e, "%x:%u:%u:%u", &bits, &period, &hold, &count) != 4 || !bits || !period || !hold || !count) {
        if (e) LOG("[latency] WWHD_LATENCY_TEST=%s: expected bits:period_ms:hold_ms:count (hex bits), off", e);
        return c;
    }
    c.bits = bits;
    c.count = count;
    c.period = int64_t(period) * 1'000'000;
    c.hold = int64_t(hold) * 1'000'000;
    if (const char* s = getenv("WWHD_LATENCY_START_STEPS")) c.start_steps = strtoull(s, nullptr, 10);
    if (const char* s = getenv("WWHD_LATENCY_START_AT")) c.start_at = strtoull(s, nullptr, 10);
    if (const char* p = getenv("WWHD_LATENCY_LOG")) c.out = fopen(p, "w");
    LOG("[latency] test on: buttons %X every %u ms (+0..40 ms) held %u ms, %u presses, from %llu of Link's steps after "
        "the load; log %s", bits, period, hold, count, (unsigned long long)c.start_steps, getenv("WWHD_LATENCY_LOG") ? getenv("WWHD_LATENCY_LOG") : "-");
    return c;
}
Config& cfg() {
    static Config c = read_config();
    return c;
}

enum Stage { kIdle, kWaitRead, kWaitReact, kWaitSwap, kWaitPresent };
struct Press {
    uint32_t k = 0;
    int64_t press = 0, read = 0, react = 0, swap_ns = 0, present = 0;
    uint64_t read_step = 0, react_step = 0, swap = 0, frame = 0;
    uint32_t proc_from = 0, proc_to = 0;
};
struct State {
    std::mutex mu;
    Stage stage = kIdle;
    bool started = false;
    int64_t t0 = 0, next = 0;  // the next press's time
    uint32_t k = 0, done = 0, missed = 0, busy = 0;
    uint64_t lcg = 0x2545F4914F6CDD1Dull;
    uint32_t base_proc = 0xFFFFFFFFu;  // Link's procedure before the first press
    uint32_t proc = 0xFFFFFFFFu;       // his procedure after his last execute
    bool react_seen = false;           // (the reacting execute ran; its pass's swap is next)
    Press cur;
    std::deque<Press> waiting;         // waiting for their frame's present (render side)
    std::deque<std::pair<uint64_t, uint64_t>> frames;  // swap -> renderer frame (render thread)
    std::vector<double> lat, to_read, to_react, to_swap, to_present;
};
State& st() {
    static State s;
    return s;
}
int64_t jitter(State& s) {
    s.lcg = s.lcg * 6364136223846793005ull + 1442695040888963407ull;
    return int64_t((s.lcg >> 33) % 40'000'000ull);
}
double pct(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(q * double(v.size() - 1) + 0.5))];
}
void summary(State& s, bool final) {
    LOG("[latency]%s %zu presses (missed %u, busy %u): press->present median %.2f p90 %.2f min %.2f max %.2f ms; "
        "medians press->read %.2f, read->react %.2f, react->swap %.2f, swap->present %.2f ms",
        final ? " done:" : "", s.lat.size(), s.missed, s.busy, pct(s.lat, 0.5), pct(s.lat, 0.9),
        s.lat.empty() ? 0 : *std::min_element(s.lat.begin(), s.lat.end()), s.lat.empty() ? 0 : *std::max_element(s.lat.begin(), s.lat.end()),
        pct(s.to_read, 0.5), pct(s.to_react, 0.5), pct(s.to_swap, 0.5), pct(s.to_present, 0.5));
}
void finish(State& s, const Press& p) {  // mu held
    const double ms = 1e-6;
    s.lat.push_back((p.present - p.press) * ms);
    s.to_read.push_back((p.read - p.press) * ms);
    s.to_react.push_back((p.react - p.read) * ms);
    s.to_swap.push_back((p.swap_ns - p.react) * ms);
    s.to_present.push_back((p.present - p.swap_ns) * ms);
    if (FILE* f = cfg().out) {
        fprintf(f, "press %u %lld %lld %llu %lld %llu %llu %lld %lld %X %X\n", p.k, (long long)p.press, (long long)p.read,
                (unsigned long long)p.read_step, (long long)p.react, (unsigned long long)p.react_step,
                (unsigned long long)p.swap, (long long)p.swap_ns, (long long)p.present, p.proc_from, p.proc_to);
        fflush(f);
    }
    s.done++;
    if (s.done % 10 == 0 && s.done < cfg().count) summary(s, false);
    if (s.done + s.missed >= cfg().count) {
        summary(s, true);
        if (FILE* f = cfg().out) { fprintf(f, "done\n"); fflush(f); }
    }
}
}  // namespace

bool g_on = getenv("WWHD_LATENCY_TEST") != nullptr;

void pad(uint32_t& buttons, float& ly) {
    static const bool stick = getenv("WWHD_LATENCY_STICK") && atoi(getenv("WWHD_LATENCY_STICK"));
    Config& c = cfg();
    if (!c.bits) return;
    State& s = st();
    std::lock_guard<std::mutex> lk(s.mu);
    const int64_t now = now_ns();
    if (!s.started) {
        if (s.proc == 0xFFFFFFFFu) return;
        if (c.start_at ? interp::logic_steps() < c.start_at
                       : (!game_hooks::state_loaded() || game_hooks::link_steps() < c.start_steps)) return;
        s.started = true;
        s.t0 = now;
        s.base_proc = s.proc;
        s.next = now + jitter(s);
        LOG("[latency] start: Link's procedure %X", s.base_proc);
    }
    if (s.k >= c.count) return;
    if (s.stage == kIdle && now >= s.next) {
        if (s.proc != s.base_proc) {  // still busy with the last reaction: try again shortly
            s.busy++;
            s.next = now + 100'000'000;
            return;
        }
        s.cur = {};
        s.cur.k = s.k++;
        s.cur.press = s.next;  // (the reads see the bits from this time on)
        s.stage = kWaitRead;
    }
    const bool held = s.stage != kIdle && s.cur.press && now < s.cur.press + c.hold;
    if (held) {
        buttons |= c.bits;
        if (stick) ly = 1.0f;
    }
    if (s.stage == kWaitRead && held) {
        s.cur.read = now;
        s.cur.read_step = interp::logic_steps();
        s.cur.proc_from = s.proc;
        s.stage = kWaitReact;
    }
}
void link_execute(uint32_t, uint32_t after) {
    if (!cfg().bits) return;
    State& s = st();
    std::lock_guard<std::mutex> lk(s.mu);
    s.proc = after;
    if (s.stage != kWaitReact) return;
    if (after != s.cur.proc_from) {
        s.cur.react = now_ns();
        s.cur.react_step = interp::logic_steps();
        s.cur.proc_to = after;
        s.stage = kWaitSwap;
    } else if (interp::logic_steps() > s.cur.read_step + 45) {  // no reaction: Link could not act (an event, a menu)
        s.missed++;
        if (FILE* f = cfg().out) { fprintf(f, "miss %u %lld %lld %X\n", s.cur.k, (long long)s.cur.press, (long long)s.cur.read, s.cur.proc_from); fflush(f); }
        s.stage = kIdle;
        s.next = s.cur.press + cfg().period + jitter(s);
        if (s.done + s.missed >= cfg().count) summary(s, true);
    }
}
void game_swap(uint64_t swap) {
    if (!cfg().bits) return;
    State& s = st();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.stage != kWaitSwap) return;
    // the pass's display list is called at the next pass's start and scanned out by the swap after (pass N's picture
    // is swap N + 1's): that swap's present counts
    s.cur.swap = swap + 1;
    s.cur.swap_ns = now_ns();
    s.waiting.push_back(s.cur);
    s.stage = kIdle;
    s.next = s.cur.press + cfg().period + jitter(s);
}
void render_swap(uint64_t swap, uint64_t frame) {
    if (!cfg().bits) return;
    State& s = st();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.waiting.empty()) return;
    for (Press& p : s.waiting)
        if (p.swap == swap) p.frame = frame + 1;  // (frame + 1: the frame presented as this swap ends; 0: not yet)
}
static std::atomic<bool> g_presents{false};  // a TV present was seen (else the swap's end on the render thread counts)
void render_swapped(uint64_t swap) {
    if (!cfg().bits || g_presents.load(std::memory_order_relaxed)) return;
    State& s = st();
    std::lock_guard<std::mutex> lk(s.mu);
    while (!s.waiting.empty() && s.waiting.front().swap && s.waiting.front().swap <= swap) {
        Press p = s.waiting.front();
        s.waiting.pop_front();
        p.present = now_ns();
        finish(s, p);
    }
}
void presented(uint64_t frame) {
    if (!cfg().bits) return;
    g_presents.store(true, std::memory_order_relaxed);
    State& s = st();
    std::lock_guard<std::mutex> lk(s.mu);
    while (!s.waiting.empty() && s.waiting.front().frame && s.waiting.front().frame - 1 <= frame) {
        Press p = s.waiting.front();
        s.waiting.pop_front();
        p.present = now_ns();
        finish(s, p);
    }
}
}  // namespace latency_test
