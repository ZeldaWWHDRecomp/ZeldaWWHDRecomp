// cheapib: cheap in-between frames (decomp build, fast mode only). See cheapib.h.
#include "cheapib/cheapib.h"
#include "cheapib/render_replay.h"

#include <atomic>
#include <pthread.h>
#include <time.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif
#include <cstdio>
#include <ctime>
#include <algorithm>
#include <deque>
#include <mutex>
#include <string>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "gx2/gx2_cmd.h"
#include "guest_addr.h"
#include "interp.h"
#include "ppc.h"
#include "runtime.h"

namespace cheapib {
namespace {

// HD's agl::RenderQueueManager (hd_graphics_task.cpp sGraphicsRenderer, data 101F95D0): its u16 flags at +0x1078.
// 0x200: RenderQueueManager::update (027C106C) runs no layer jobs, drawView (027C1AD8) calls the submitted commands
// again (submitDisplayLists(rebuild=0)); update clears it (& 0xFDEF).
constexpr uint32_t kRendererPtr = 0x101F95D0;
constexpr uint32_t kManagerFlags = 0x1078;
constexpr uint16_t kReplayFlag = 0x200;

int read_mode() {
#ifdef WWHD_DECOMP_FAST
    const char* e = render_replay::env("WWHD_CHEAPIB");  // (the setting's set when not set: render_replay.h)
    const int m = e ? atoi(e) : 0;
    if (m == 1) {
        LOG("[cheapib] WWHD_CHEAPIB=1 (stage 1): in-betweens replay the logic pass's display lists with the moving "
            "models' blocks patched (camera and dynamic sites not yet: frozen at the logic pass)");
        return m;
    }
    if (m == 8 || m == 9) {
        LOG("[cheapib] WWHD_CHEAPIB=%d (%s): test-only ceiling, in-betweens repeat the logic frame", m,
            m == 8 ? "S1: replayed display lists" : "S2: replayed lists, nothing sent to the render thread");
        return m;
    }
    if (m) LOG("[cheapib] WWHD_CHEAPIB=%d: unknown mode, off", m);
#endif
    return 0;
}

uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h *= 0xFF51AFD7ED558CCDull;
    return h ^ (h >> 33);
}
uint64_t hash_bytes(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull ^ n;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) { uint64_t v; memcpy(&v, p + i, 8); h = mix(h, v); }
    uint64_t t = 0;
    for (size_t k = 0; i < n; ++i, ++k) t |= uint64_t(p[i]) << (8 * k);
    return mix(h, t);
}

enum Window { kNone, kLogic, kReplay };
struct Call { uint32_t list, size; uint64_t hash; };

struct State {
    bool in_frame = false;    // inside the per-frame function
    bool replay = false;      // the current (or last) frame is a replayed hold pass
    bool have_logic = false;  // a logic pass drew since start: its submitted lists exist
    bool flag_set = false;    // we set the manager's 0x200 after the frame (cleared at the next pass entry)
    bool interp_pass = false; // the current (or last) frame ran with interpolation on
    uint32_t calls = 0;       // top-level display-list calls since the frame began (b1's tags)
    int last_kind = -1;       // 0 logic, 1 hold full, 2 hold replayed: the pass the recorded bytes belong to
    Window window = kNone;
    bool mismatch = false;
    size_t index = 0;
    std::vector<Call> logic_calls;
    uint64_t recorded_at_entry = 0, recorded_at_end = 0;
    // counters (per report)
    uint64_t passes[3] = {}, recorded[3] = {}, skipped_draws = 0;
    uint64_t logic_calls_n = 0, replay_windows = 0, dropped_calls = 0, dropped_ops = 0, kept_ops = 0, mismatches = 0,
             short_windows = 0, frame_top_ops = 0;
    uint64_t logic_steps = 0;
};
State g;
std::atomic<uint64_t> g_recorded{0};
// debug WWHD_CHEAPIB_STATS=1 (any mode): render-thread CPU per pass kind (swap_dump_label)
bool stats() {
    static const bool s = getenv("WWHD_CHEAPIB_STATS") && atoi(getenv("WWHD_CHEAPIB_STATS"));
    return s;
}

// debug (every mode, also 0): WWHD_CHEAPIB_DUMP=STEP:N writes the TV image of every pass of logic steps STEP..STEP+N-1
// (as interp counts them) to ib_<step>_<k>.png, k = 0 the logic pass, 1..3 its in-betweens: in-between frames, which
// WWHD_DUMP_FRAMES cannot name. The label is decided on the game thread and travels with the pass's swap.
// The swap that shows a pass's picture is sent during the NEXT pass (after its frame_begin): the label waits one pass
// (`pending`). (Until 2026-11-17 the swap took the next pass's label, so ib_S_k showed pass k-1 and ib_S_0 the previous
// step's record pass: with WWHD_CHEAPIB=8, where a step's 4 passes are identical, ib_S_1..3 = ib_(S+1)_0.)
struct Dump {
    uint64_t from = 0, n = 0;
    bool on = false;
    uint64_t step = 0;
    int phase = 0;
    std::string label;    // the label the next swap takes: the previous pass's (empty: none)
    std::string pending;  // the current pass's (its swap comes in the next pass)
    std::mutex mu;
    std::deque<std::pair<std::string, int>> swaps;  // one per swap the game sent, oldest first: label, pass kind
    // render thread: its CPU time between swaps, by the kind of pass whose lists it executed (0 logic, 1 hold full,
    // 2 hold replayed); WWHD_CHEAPIB on, every 1200 swaps
    int last_kind = -1;
    double last_cpu = 0, cpu[3] = {}, cnt[3] = {};
    uint64_t frames = 0;
};
Dump& dump() {
    static Dump d;
    static const bool init = [] {
        if (const char* e = getenv("WWHD_CHEAPIB_DUMP")) {
            d.from = strtoull(e, nullptr, 10);
            const char* c = strchr(e, ':');
            d.n = c ? strtoull(c + 1, nullptr, 10) : 1;
            d.on = d.n > 0;
        }
        return true;
    }();
    (void)init;
    return d;
}
void dump_pass(bool hold) {
    Dump& d = dump();
    if (!hold) {
        d.step = interp::logic_steps();
        d.phase = 0;
    } else {
        d.phase++;
    }
    {
        std::lock_guard<std::mutex> lk(d.mu);
        d.label = std::move(d.pending);
    }
    d.pending.clear();
    if (d.step >= d.from && d.step < d.from + d.n)
        d.pending = "ib_" + std::to_string(d.step) + "_" + std::to_string(d.phase) + ".png";
}

uint32_t manager() {
    const uint32_t m = ld32(GD(kRendererPtr));
    return m >= 0x10000000 && m < 0x50000000 ? m : 0;
}
int g_trace = getenv("WWHD_CHEAPIB_TRACE") ? atoi(getenv("WWHD_CHEAPIB_TRACE")) : 0;  // debug: n lines
void set_replay_flag(const char* where) {
    const uint32_t m = manager();
    if (g_trace > 0) {
        g_trace--;
        LOG("[cheapib] %s: manager %08X flags %04X", where, m, m ? ld16(m + kManagerFlags) : 0);
    }
    if (m) st16(m + kManagerFlags, ld16(m + kManagerFlags) | kReplayFlag);
}
void clear_replay_flag() {
    if (const uint32_t m = manager()) st16(m + kManagerFlags, ld16(m + kManagerFlags) & ~kReplayFlag);
}

void maybe_report() {
    if (++g.logic_steps % 300) return;
    auto per = [](uint64_t v, uint64_t n) { return n ? double(v) / double(n) : 0.0; };
    LOG("[cheapib] mode %d per 300 steps: passes logic %llu, hold full %llu, hold replayed %llu (fpcDw skipped %llu); "
        "recorded KB per pass: logic %.1f, hold full %.1f, replayed %.1f; painter calls per logic pass %.1f; "
        "replayed windows %llu: calls dropped %.1f, ops dropped %.1f kept %.1f per window, mismatches %llu, short %llu",
        mode(), (unsigned long long)g.passes[0], (unsigned long long)g.passes[1], (unsigned long long)g.passes[2],
        (unsigned long long)g.skipped_draws, per(g.recorded[0], g.passes[0]) / 1024, per(g.recorded[1], g.passes[1]) / 1024,
        per(g.recorded[2], g.passes[2]) / 1024, per(g.logic_calls_n, g.passes[0]), (unsigned long long)g.replay_windows,
        per(g.dropped_calls, g.replay_windows), per(g.dropped_ops, g.replay_windows), per(g.kept_ops, g.replay_windows),
        (unsigned long long)g.mismatches, (unsigned long long)g.short_windows);
    memset(g.passes, 0, sizeof g.passes);
    memset(g.recorded, 0, sizeof g.recorded);
    g.skipped_draws = g.logic_calls_n = g.replay_windows = g.dropped_calls = g.dropped_ops = g.kept_ops = 0;
    g.mismatches = g.short_windows = g.frame_top_ops = 0;
}

}  // namespace

int mode() {
    static const int m = read_mode();
    return m;
}

// WWHD_CHEAPIB_VERIFY=STEP:N (stage 1, Mac): in logic steps STEP..STEP+N-1 one in-between (in turn) is drawn in full,
// and the patch is written into the logic pass's slots after its frame; ib_census.h compares, draw by draw, the blocks
// the full pass bound with the patched ones (logged every 30 in-betweens).
struct Verify {
    uint64_t from = 0, n = 0;
    bool on = false;
    int kind = -1;  // the current pass: 0 logic, 1..3 in-between of a verified step, -1 other
    int phase = 0;
    void (*close)() = nullptr;
    void (*patch_begin)() = nullptr, (*patch_end)() = nullptr;
};
Verify& ver() {
    static Verify v;
    static const bool init = [] {
        const char* e = getenv("WWHD_CHEAPIB_VERIFY");
        if (e && mode() == 1) {
            v.from = strtoull(e, nullptr, 10);
            const char* c = strchr(e, ':');
            v.n = c ? strtoull(c + 1, nullptr, 10) : 1;
            v.on = v.n > 0;
            if (v.on) LOG("[cheapib] verify: logic steps %llu..%llu drawn in full, the patch compared", (unsigned long long)v.from,
                          (unsigned long long)(v.from + v.n - 1));
        }
        return true;
    }();
    (void)init;
    return v;
}

void pass_entry() {
    qnote(0);
    if (!on()) return;
    if (ver().on && ver().close) ver().close();  // (the last pass's painter window, before this pass's patch)
    // the bytes recorded since the last entry belong to the pass whose frame ran then
    const uint64_t r = g_recorded.load(std::memory_order_relaxed);
    if (g.last_kind >= 0) g.recorded[g.last_kind] += r - g.recorded_at_entry;
    if (g.window == kReplay && !g.mismatch && g.index != g.logic_calls.size()) g.short_windows++;
    g.window = kNone;
    if (g_trace > 0 && g.last_kind == 2) {
        g_trace--;
        const uint32_t m = manager();
        LOG("[cheapib] pass entry after a replayed pass: flags %04X, recorded bytes: %llu in the frame, %llu after it", m ? ld16(m + kManagerFlags) : 0,
            (unsigned long long)(g.recorded_at_end - g.recorded_at_entry), (unsigned long long)(r - g.recorded_at_end));
    }
    if (g.flag_set) {
        clear_replay_flag();
        g.flag_set = false;
    }
    g.recorded_at_entry = r;
}

void frame_begin(bool hold, bool interp) {
    qnote(1);
    if (dump().on) dump_pass(hold && interp);
    if (!on()) {
        if (stats()) g.last_kind = hold && interp ? 1 : 0;  // (WWHD_CHEAPIB_STATS=1 with the switch off: today's passes)
        return;
    }
    if (ver().on) {
        Verify& v = ver();
        v.phase = hold ? v.phase + 1 : 0;
        const uint64_t step = interp::logic_steps();
        // one in-between per verified step is drawn in full, in turn (1, 2, 3): the others replay, so the logic pass's
        // command-pool slice (which holds blocks the lists bind) is not recorded over before the comparison
        const bool in = interp && step >= v.from && step < v.from + v.n;
        v.kind = !in ? -1 : !hold ? 0 : v.phase == int(step % 3) + 1 ? v.phase : -1;
    }
    g.replay = hold && interp && g.have_logic && !(ver().on && ver().kind > 0);
    g.interp_pass = interp;
    g.calls = 0;
    g.last_kind = !hold ? 0 : g.replay ? 2 : 1;
    g.passes[g.last_kind]++;
    if (g.replay) set_replay_flag("frame begin");  // (update: no layer jobs this frame)
    g.in_frame = true;
}

void frame_end(bool hold, bool interp) {
    qnote(2);
    if (!on()) return;
    g.in_frame = false;
    g.recorded_at_end = g_recorded.load(std::memory_order_relaxed);
    if (!hold) {
        g.have_logic = interp;
        maybe_report();
    }
    if (g.replay) {
        set_replay_flag("frame end");  // (update cleared it: drawView calls the submitted commands again)
        g.flag_set = true;
    }
    if (mode() == 9) {
        if (!hold) {
            g.window = kLogic;
            g.logic_calls.clear();
        } else if (g.replay) {
            g.window = kReplay;
            g.index = 0;
            g.mismatch = false;
            g.replay_windows++;
        }
    }
}

bool painter_replay() { return g.flag_set; }

bool stage1() { return mode() == 1; }
bool verify_on() { return ver().on; }
int verify_kind() { return ver().on ? ver().kind : -1; }
bool verifying() { return ver().on && ver().kind > 0 && g.in_frame; }
void set_verify_close(void (*fn)()) { ver().close = fn; }
void set_verify_patch(void (*begin)(), void (*end)()) { ver().patch_begin = begin; ver().patch_end = end; }
void verify_patch(bool begin) {
    if (void (*fn)() = begin ? ver().patch_begin : ver().patch_end) fn();
}
bool replaying() { return g.replay && g.in_frame; }

// the emitter thread ("Vulkan record", record_mt.cpp emitter_started): its CPU time, for the stats line
namespace {
std::atomic<bool> g_em_have{false};
pthread_t g_em;
double emitter_cpu_ms() {
    if (!g_em_have.load(std::memory_order_acquire)) return -1;
#ifdef __APPLE__
    thread_basic_info_data_t info;
    mach_msg_type_number_t n = THREAD_BASIC_INFO_COUNT;
    if (thread_info(pthread_mach_thread_np(g_em), THREAD_BASIC_INFO, (thread_info_t)&info, &n) != KERN_SUCCESS) return -1;
    return (info.user_time.seconds + info.system_time.seconds) * 1e3 +
           (info.user_time.microseconds + info.system_time.microseconds) * 1e-3;
#else
    clockid_t id;
    timespec ts;
    if (pthread_getcpuclockid(g_em, &id) || clock_gettime(id, &ts)) return -1;
    return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
#endif
}
}  // namespace
void note_emitter_thread() {
    g_em = pthread_self();
    g_em_have.store(true, std::memory_order_release);
}
#ifdef WWHD_DECOMP_FAST
void b1_frame();  // (cheapib_b1.h, in the fast build's backend.cpp)
#endif
std::string swap_dump_label() {
#ifdef WWHD_DECOMP_FAST
    b1_frame();
#endif
    if (!dump().on && !stats()) return {};
    Dump& d = dump();
    std::pair<std::string, int> e{{}, -1};
    {
        std::lock_guard<std::mutex> lk(d.mu);
        if (!d.swaps.empty()) {
            e = std::move(d.swaps.front());
            d.swaps.pop_front();
        }
    }
    if (stats()) {
        timespec ts;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        const double now = ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
        if (d.last_kind >= 0 && d.last_kind < 3) { d.cpu[d.last_kind] += now - d.last_cpu; d.cnt[d.last_kind]++; }
        d.last_cpu = now;
        d.last_kind = e.second;
        if (++d.frames % 1200 == 0) {
            static double emLast = -1;
            const double em = emitter_cpu_ms();
            if (em >= 0 && emLast >= 0)
                LOG("[cheapib] emitter thread CPU per frame (ms, last 1200 frames): %.3f", (em - emLast) / 1200.0);
            emLast = em;
            auto avg = [&](int k) { return d.cnt[k] ? d.cpu[k] / d.cnt[k] : 0.0; };
            LOG("[cheapib] render thread CPU per frame (ms, from swap to swap): logic %.2f (%.0f), in-between full %.2f (%.0f), "
                "replayed %.2f (%.0f)", avg(0), d.cnt[0], avg(1), d.cnt[1], avg(2), d.cnt[2]);
            memset(d.cpu, 0, sizeof d.cpu);
            memset(d.cnt, 0, sizeof d.cnt);
        }
    }
    return std::move(e.first);
}

bool plan_all() {
    static const bool on = (mode() == 1 || mode() >= 8) && getenv("WWHD_CHEAPIB_PLAN_ALL") && atoi(getenv("WWHD_CHEAPIB_PLAN_ALL"));
    return on;
}

bool skip_draw() {
    if (!on() || !g.in_frame || !g.replay) return false;
    g.skipped_draws++;
    return true;
}

#ifdef WWHD_DECOMP_FAST
void b1_tag(uint32_t list, uint32_t size, int kind, uint64_t step, uint32_t index);
#endif
bool drop_call(uint32_t list, uint32_t size) {
#ifdef WWHD_DECOMP_FAST  // (b1 is the fast build's: cheapib_b1.h)
    if (b1_on())  // (every top-level call, in the order the render thread executes them)
        b1_tag(list, size, g.last_kind == 0 && g.interp_pass ? 0 : g.last_kind == 2 ? 2 : -1, interp::logic_steps(), g.calls++);
#endif
    if (g.window == kNone) return false;
    const uint64_t h = list && size && size < (64u << 20) ? hash_bytes((const uint8_t*)ppc_ptr(list), size) : 0;
    if (g.window == kLogic) {
        g.logic_calls.push_back({list, size, h});
        g.logic_calls_n++;
        return false;
    }
    if (g.mismatch) return false;
    if (g.index < g.logic_calls.size()) {
        const Call& c = g.logic_calls[g.index];
        if (c.list == list && c.size == size && c.hash == h) {
            g.index++;
            g.dropped_calls++;
            return true;
        }
    }
    g.mismatch = true;
    g.mismatches++;
    return false;
}

bool drop_op(uint32_t op) {
    if ((op & 0xFF) == gx2::OP_SWAP && (dump().on || stats())) {
        Dump& d = dump();
        std::lock_guard<std::mutex> lk(d.mu);
        d.swaps.emplace_back(d.label, g.last_kind);
        d.label.clear();
    }
    if (g.window != kReplay || g.mismatch) return false;
    using namespace gx2;
    switch (op & 0xFF) {
    case OP_CALL: case OP_SUBMITTED_UNIFORMS: return false;  // (drop_call decides the call and its snapshot)
    // kept: state, synchronisation, presentation and memory coherence
    case OP_SET_REGS: case OP_SET_CONTEXT: case OP_SETUP_CONTEXT: case OP_SET_PROJ_REGS: case OP_INVALIDATE:
    case OP_FLUSH: case OP_DRAW_DONE: case OP_SWAP: case OP_FENCE: case OP_PEEK_Z: case OP_CHEAPIB_VTX:
        g.kept_ops++;
        return false;
    default:
        g.dropped_ops++;
        return true;
    }
}

void note_recorded(uint32_t bytes) { g_recorded.fetch_add(bytes, std::memory_order_relaxed); }

// GX2VertexShader: program +0xD4 (else its GX2RBuffer at +0x124: +0xC), uniform blocks count +0xDC, array +0xE0;
// GX2PixelShader: program +0xA8 (GX2RBuffer +0xD8), count +0xB0, array +0xB4. GX2UniformBlock: name, offset
// (the block's location), size (bytes).
namespace {
std::mutex g_shader_mu;
std::unordered_map<uint32_t, uint32_t> g_shaders;  // program register | pixel << 31 -> shader object
}  // namespace
void note_shader(uint32_t shader, bool pixel) {
    if (!shader || !ver().on) return;
    uint32_t prog = ld32(shader + (pixel ? 0xA8 : 0xD4));
    if (!prog) prog = ld32(shader + (pixel ? 0xD8 : 0x124) + 0xC);
    if (!prog) return;
    std::lock_guard<std::mutex> lk(g_shader_mu);
    g_shaders[(prog >> 8) | (pixel ? 0x80000000u : 0)] = shader;
}
uint32_t declared_block_size(uint32_t programReg, bool pixel, uint32_t location) {
    uint32_t shader = 0;
    {
        std::lock_guard<std::mutex> lk(g_shader_mu);
        auto it = g_shaders.find((programReg & 0x7FFFFFFF) | (pixel ? 0x80000000u : 0));
        if (it == g_shaders.end()) return 0;
        shader = it->second;
    }
    const uint32_t n = std::min<uint32_t>(ld32(shader + (pixel ? 0xB0 : 0xDC)), 16);
    const uint32_t a = ld32(shader + (pixel ? 0xB4 : 0xE0));
    if (!a) return 0;
    for (uint32_t i = 0; i < n; ++i)
        if (ld32(a + 12 * i + 4) == location) return ld32(a + 12 * i + 8);
    return 0;
}

// debug (hang hunt, part 5): PipelineFrameQueue (data 101F997C; mFlag +0xD0, mWork +0xD8: mInputs[3] {count,
// capacity, items} +0, mQueues[3] (sead::JobQueue, 0x6C each: mCoreMask +0x14, mActive[3] +0x24, mCompleted[3] +0x30,
// mState +0x3C, mCount +0x64, mNext +0x68) +0x44, mWorkers (sead::WorkerManager) +0x188; mCompleted +0x388)
namespace {
constexpr uint32_t kPfq = 0x101F997C;
struct QNote {
    uint64_t pass, step;
    int8_t kind;
    uint8_t where, flag, completed;
    uint32_t cnt[3], next[3], done[3], inputs[3], qcount;
};
std::mutex g_qmu;
QNote g_qring[96];
uint64_t g_qn = 0, g_qpass = 0, g_qodd = 0;
uint32_t pfq() {
    const uint32_t q = ld32(GD(kPfq));
    return q >= 0x10000000 && q < 0x50000000 ? q : 0;
}
QNote qsnap(uint32_t q) {
    QNote n{};
    n.flag = ld8(q + 0xD0);
    n.completed = ld8(q + 0x388);
    const uint32_t w = q + 0xD8;
    for (int i = 0; i < 3; i++) {
        const uint32_t j = w + 0x44 + 0x6C * i;
        n.cnt[i] = ld32(j + 0x64);
        n.next[i] = ld32(j + 0x68);
        n.done[i] = ld32(j + 0x30) + ld32(j + 0x34) + ld32(j + 0x38);
        n.inputs[i] = ld32(w + 12 * i);
    }
    n.qcount = ld32(w + 0x188 + 0x50);
    return n;
}
}  // namespace
void qnote(int where) {
    static const bool on = getenv("WWHD_CHEAPIB_QNOTE") && atoi(getenv("WWHD_CHEAPIB_QNOTE"));  // (debug, off by default)
    if (!on) return;
    const uint32_t q = pfq();
    if (!q) return;
    QNote n = qsnap(q);
    n.where = (uint8_t)where;
    n.kind = (int8_t)g.last_kind;
    n.step = interp::logic_steps();
    std::lock_guard<std::mutex> lk(g_qmu);
    if (where == 0) g_qpass++;
    n.pass = g_qpass;
    // (at a pass entry the last pass's resetWork has cleared the queues: anything else is odd; logged 20 times)
    if (where == 0 && (n.cnt[0] || n.cnt[1] || n.cnt[2] || n.next[0] || n.next[1] || n.next[2]) && g_qodd++ < 20)
        LOG("[cheapib] qnote: pass %llu step %llu entry with queues not cleared: count %u/%u/%u next %u/%u/%u done %u/%u/%u",
            (unsigned long long)n.pass, (unsigned long long)n.step, n.cnt[0], n.cnt[1], n.cnt[2], n.next[0], n.next[1],
            n.next[2], n.done[0], n.done[1], n.done[2]);
    g_qring[g_qn++ % 96] = n;
}
void watchdog_dump() {
    const uint32_t q = pfq();
    if (!q) return;
    {
        std::lock_guard<std::mutex> lk(g_qmu);
        LOG("[cheapib] qnote ring (oldest first; where 0 entry 1 begin 2 end 3 pre-drain 4 post-drain; kind 0 logic 1 hold "
            "full 2 replayed): mode %d, in_frame %d replay %d flag_set %d", mode(), g.in_frame, g.replay, g.flag_set);
        for (uint64_t i = g_qn > 96 ? g_qn - 96 : 0; i < g_qn; i++) {
            const QNote& n = g_qring[i % 96];
            LOG("[cheapib] q pass %llu step %llu w%u k%d flag %u compl %u | count %u/%u/%u next %u/%u/%u done %u/%u/%u inputs "
                "%u/%u/%u mgr queues %u",
                (unsigned long long)n.pass, (unsigned long long)n.step, n.where, n.kind, n.flag, n.completed, n.cnt[0],
                n.cnt[1], n.cnt[2], n.next[0], n.next[1], n.next[2], n.done[0], n.done[1], n.done[2], n.inputs[0],
                n.inputs[1], n.inputs[2], n.qcount);
        }
    }
    const uint32_t w = q + 0xD8, m = w + 0x188;
    for (int i = 0; i < 3; i++) {
        const uint32_t j = w + 0x44 + 0x6C * i;
        LOG("[cheapib] queue %d @%08X: coremask %X waitmode %u active %u/%u/%u completed %u/%u/%u state %u count %u next %u "
            "capacity %u jobs %08X first job %08X lock %u",
            i, j, ld32(j + 0x14), ld32(j + 0xC), ld32(j + 0x24), ld32(j + 0x28), ld32(j + 0x2C), ld32(j + 0x30),
            ld32(j + 0x34), ld32(j + 0x38), ld32(j + 0x3C), ld32(j + 0x64), ld32(j + 0x68), ld32(j + 0x5C), ld32(j + 0x60),
            ld32(j + 0x60) >= 0x10000000 ? ld32(ld32(j + 0x60)) : 0, ld32(j + 0x10));
    }
    const uint32_t nw = ld32(m + 0x40), ws = ld32(m + 0x44), nq = ld32(m + 0x50), qs = ld32(m + 0x4C);
    LOG("[cheapib] workers @%08X: count %u, queues %u (capacity %u), single %u, active queue %08X, submission %u, submitted %u",
        m, nw, nq, ld32(m + 0x48), ld8(m + 0x54), ld32(m + 0x58), ld32(m + 0x5C), ld8(m + 0x68));
    for (uint32_t i = 0; i < nq && i < 8 && qs; i++) LOG("[cheapib]   queue[%u] %08X", i, ld32(qs + 4 * i));
    for (uint32_t i = 0; i < nw && i < 4 && ws; i++) {
        const uint32_t r = ld32(ws + 4 * i);
        if (r < 0x10000000) continue;
        LOG("[cheapib]   worker %u @%08X: enabled %u state %u; words 0x80..0xBF: %08X %08X %08X %08X %08X %08X %08X %08X "
            "%08X %08X %08X %08X %08X %08X %08X %08X", i, r, ld32(r + 0x94), ld32(r + 0x98), ld32(r + 0x80), ld32(r + 0x84),
            ld32(r + 0x88), ld32(r + 0x8C), ld32(r + 0x90), ld32(r + 0x94), ld32(r + 0x98), ld32(r + 0x9C), ld32(r + 0xA0),
            ld32(r + 0xA4), ld32(r + 0xA8), ld32(r + 0xAC), ld32(r + 0xB0), ld32(r + 0xB4), ld32(r + 0xB8), ld32(r + 0xBC));
    }
}

}  // namespace cheapib

extern "C" int wwhd_cheapib_skip_draw(void) { return cheapib::skip_draw() ? 1 : 0; }
extern "C" int wwhd_cheapib_replay(void) { return cheapib::painter_replay() ? 1 : 0; }
