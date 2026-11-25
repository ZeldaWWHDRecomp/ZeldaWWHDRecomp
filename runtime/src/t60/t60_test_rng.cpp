// Test aid of the true 60 pairs (decomp build only; default off): WWHD_TEST_RNG_CONST makes the game's random numbers
// (cM_rnd / cM_rndF / cM_rndFX, the hooks in interp_fx.cpp) independent of how many draws ran before. After a converted
// process moves a draw onto another step, the Wichmann-Hill seeds of a 30-vs-N pair split and every later
// AI decision draws other values: the fights diverge and a strict pair gate ends at the first RNG difference. With this
// mode a decision's value depends only on the mode (and the 30 Hz frame / call site), never on the draw count, so a
// converted enemy's fight stays strictly comparable for the whole route. Both runs of a pair must use the same value.
//   WWHD_TEST_RNG_CONST=F         every draw returns the fraction F (0 <= F < 1): cM_rnd F, cM_rndF(m) F*m,
//                                 cM_rndFX(m) 2 (F - 0.5) m (the original float operations)
//   WWHD_TEST_RNG_CONST=frame:S   the fraction hashed from S and the 30 Hz scenario frame: one value per frame, so the AI
//                                 is not frozen into one branch for the whole route
//   WWHD_TEST_RNG_CONST=site:S    hashed from S, the 30 Hz scenario frame, the function and the guest call site (lr; for
//                                 native decompiled callers the lr can be stale, then several sites share a value)
// The scenario frame counts from the scenario origin (test_aids::origin_step, the origin compare60 and the pair tools
// use): the two runs of a pair reach the origin at different absolute steps (e.g. A 3508, B 3506), so a hash of the run's
// own wwhd_t60_frame gave each run other values. Before the origin is set the absolute frame is used.
// The seeds are not advanced (the original functions are not entered): WWHD_RNG_TRACE's seed columns stay constant.
//   WWHD_TEST_RNG_KEYED=S         (takes precedence over WWHD_TEST_RNG_CONST) each draw is hashed from S,
//                                 the 30 Hz scenario frame, the process whose execute draws (its process id, read from
//                                 wwhd_t60_exec_proc, the guest address the decomp's fpcEx_Execute sets; heap addresses
//                                 can differ between the runs; no process outside an execute) and its draw index within
//                                 the 30 Hz frame: a draw no longer depends on other processes' draw counts or on which
//                                 step of the frame draws, so a 30-vs-N pair stays comparable start to end while the AI
//                                 still sees varied values (different seeds, different branches). A converted process
//                                 that draws a different number of times per frame than at 30 Hz (or in another order)
//                                 gets other values from that draw on: a real difference the pair shows.
//                                 From the scenario origin on; before it the game's own draws (both runs alike there).
//   WWHD_TEST_RNG_KEYED_MODE=frame  (the default) the key above; its 30 Hz frame counts from the last draw-counter restart
//                                 (below) and a load epoch, not from the scenario origin (t60_test_rng_key.h, 2026-11-18:
//                                 a warp's load took 22 record steps in A and 21 in B with equal game clocks, so every keyed
//                                 draw after it was one frame apart: false failures)
//   WWHD_TEST_RNG_KEYED_MODE=seq    no frame number: each draw is hashed
//                                 from S, the process id, the draw's stream (the function and its bound max, e.g. a
//                                 lighting flicker's cM_rndF(1) apart from a wait timer's cM_rndF(60)) and its index in
//                                 that stream since the scenario origin. An event that legitimately happens one frame
//                                 apart in the two runs then draws the same value instead of a
//                                 frame-keyed other one. The stream split keeps a per-frame draw of the same process (the
//                                 lighting flicker) from shifting the event's index by the frame difference. Use it for
//                                 pairs where an accepted +-1 frame shift precedes a draw; keep the frame mode where the
//                                 per-frame draw counts are themselves under test (a converted actor drawing a different
//                                 number of times per frame still shows in the frame mode at once; in seq only once
//                                 that stream's later values are used).
//                                 The mode is logged at the first draw call ("[t60] test rng keyed mode: ..."); compare60
//                                 and the pair tools print it.
//                                 The draw counters restart wherever WWHD_TEST_FIXED_SEED re-seeds the original generator
//                                 (savestate.cpp apply_fixed_seed: first gameplay frame, every loading frame of a portable
//                                 load / stage change / test warp and the first frame after it, a full state load, the
//                                 origin reseed): the two runs of a pair boot and load in a different number of steps,
//                                 so counters carried over from before the load gave the first draws after it other
//                                 values (q6bk: the Fishman's createInit draw picked another hiding tag, 17700 units away).
//   WWHD_TEST_RNG_KEYED_LOG=path  (debug, keyed mode only) one line per keyed draw: logic step, wwhd_t60_frame, origin,
//                                 frame key, process, draw index, function, value; and a "restart" line per counter
//                                 restart (which key part made the Fishman's swim timer differ)
//                                 Draws on a hold pass outside an execute (interp::hold_pass and not wwhd_t60_exec_logic:
//                                 a converted process's execute on a step before the tick30 step is logic) (interp::hold_pass: an in-between pass with no game logic, where
//                                 the normal mode replays the logic pass's values, interp_fx.cpp rnd_call) count on
//                                 counters of their own (frame and seq modes), so they never move a process's logic draw
//                                 index: a run with hold passes ran each draw method (dScnKy_env_light_c::
//                                 settingTevStruct's draw) once more per frame, and the next execute draw took another
//                                 index (the Fishman's modeSwimInit swim timer
//                                 A 75 / B 104, jump at frame 16 / 45).
// Unset (the default): t60_test_rng returns false at once and the hooks run the original functions unchanged.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include "ppc.h"
#include "t60/t60.h"
#include "t60/t60_test_rng_key.h"

void log_msg(const char* fmt, ...) __attribute__((format(printf, 1, 2)));  // (runtime.h)

namespace test_aids { uint64_t origin_step(); }  // test_aids.cpp: the scenario origin (0 before it)
namespace interp { bool hold_pass(); bool in_execute(); }  // interp.cpp: an in-between (hold) pass, no game logic; logic marked (g_in_execute)

namespace {
enum Mode { kOff, kConst, kFrame, kSite, kKeyed };
enum KeyedMode { kKeyFrame, kKeySeq };
struct Cfg {
    Mode mode = kOff;
    float frac = 0;
    uint64_t seed = 0;
    KeyedMode keyed = kKeyFrame;
    Cfg() {
        const char* k = getenv("WWHD_TEST_RNG_KEYED");
        if (k && *k) {
            mode = kKeyed, seed = strtoull(k, nullptr, 0);
            const char* km = getenv("WWHD_TEST_RNG_KEYED_MODE");
            if (km && !strcmp(km, "seq")) keyed = kKeySeq;
            else if (km && *km && strcmp(km, "frame"))
                log_msg("[t60] WWHD_TEST_RNG_KEYED_MODE=%s: not frame or seq, frame used", km);
            log_msg("[t60] test rng keyed mode: %s seed %llu drawstream %d", keyed == kKeySeq ? "seq" : "frame", (unsigned long long)seed, getenv("WWHD_TEST_RNG_DRAWSTREAM") && getenv("WWHD_TEST_RNG_DRAWSTREAM")[0] == '0' ? 0 : 1);
            return;
        }
        const char* e = getenv("WWHD_TEST_RNG_CONST");
        if (!e || !*e) return;
        if (!strncmp(e, "frame:", 6)) mode = kFrame, seed = strtoull(e + 6, nullptr, 0);
        else if (!strncmp(e, "site:", 5)) mode = kSite, seed = strtoull(e + 5, nullptr, 0);
        else {
            const double f = strtod(e, nullptr);
            if (!(f >= 0 && f < 1)) {
                log_msg("[t60] WWHD_TEST_RNG_CONST=%s: not a fraction in [0, 1), ignored", e);
                return;
            }
            mode = kConst, frac = (float)f;
        }
    }
};
const Cfg& cfg() {
    static const Cfg c;
    return c;
}
uint64_t mix(uint64_t x) {  // splitmix64 finaliser
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
// the 30 Hz frame relative to the scenario origin (wwhd_t60_frame and the origin are both record steps)
uint64_t scenario_frame() {
    const uint64_t o = test_aids::origin_step();
    return o ? (uint64_t)((int64_t)wwhd_t60_frame - (int64_t)o) : (uint64_t)wwhd_t60_frame;
}
// the keyed frame mode's frame key (t60_test_rng_key.h): frames since the last restart, per load epoch
T60KeyedFrame g_kf;
// the keyed mode's draw index per process within the 30 Hz scenario frame
struct DrawCount {
    uint64_t frame;
    uint32_t n;
};
std::unordered_map<uint32_t, DrawCount> g_draws;
std::unordered_map<uint32_t, DrawCount> g_hold_draws;  // hold passes' draws (never the logic index)
FILE* keyed_log() {
    static FILE* f = getenv("WWHD_TEST_RNG_KEYED_LOG") ? fopen(getenv("WWHD_TEST_RNG_KEYED_LOG"), "w") : nullptr;
    return f;
}
uint32_t draw_index(uint32_t proc, uint64_t frame, bool hold) {
    DrawCount& d = (hold ? g_hold_draws : g_draws)[proc];
    if (d.frame != frame || d.n == 0xFFFFFFFFu) d.frame = frame, d.n = 0;
    return d.n++;
}
// the seq mode's draw index per process and stream since the scenario origin (cleared when the origin changes)
std::unordered_map<uint64_t, uint32_t> g_seq;
std::unordered_map<uint64_t, uint32_t> g_hold_seq;  // hold passes' draws (never the logic index)
uint64_t g_seq_origin = 0;
uint32_t seq_index(uint32_t proc, uint64_t stream, bool hold) {
    const uint64_t o = test_aids::origin_step();
    if (o != g_seq_origin) g_seq_origin = o, g_seq.clear(), g_hold_seq.clear();
    return (hold ? g_hold_seq : g_seq)[mix(stream) ^ proc]++;
}
float hashed(uint64_t k) { return (float)(mix(k) >> 40) * (1.0f / 16777216.0f); }  // 24 bits: exact, in [0, 1)
}  // namespace

// the keyed mode's draw counters start anew (savestate.cpp apply_fixed_seed; no effect in the other modes)
void t60_test_rng_restart() {
    g_kf.restart(wwhd_t60_frame, test_aids::origin_step());
    g_draws.clear(); g_hold_draws.clear(); g_seq.clear(); g_hold_seq.clear();  // (the seq counters as well)
    if (FILE* lf = keyed_log()) fprintf(lf, "restart frame %u origin %llu\n", wwhd_t60_frame, (unsigned long long)test_aids::origin_step());
}

// fn ' ' cM_rnd, 'F' cM_rndF(max), 'X' cM_rndFX(max); true: *out is the call's result (the original is not entered)
bool t60_test_rng(char fn, uint32_t lr, float max, float* out) {
    const Cfg& c = cfg();
    if (c.mode == kOff) return false;
    // keyed: the game's own draws until the scenario origin (both runs execute at 30 Hz with the fixed, load-held seeds
    // there, so they draw alike; a pre-origin key gave a ChuChu other chase offsets in the two runs of cck3s1_4: the
    // draw index of an address counted the boot's draws, which differ with the loading steps)
    if (c.mode == kKeyed && !test_aids::origin_step()) return false;
    float f = c.frac;
    if (c.mode == kFrame) f = hashed(mix(c.seed) ^ scenario_frame());
    else if (c.mode == kSite) f = hashed(mix(mix(c.seed) ^ scenario_frame()) ^ ((uint64_t)(uint8_t)fn << 32) ^ lr);
    else if (c.mode == kKeyed) {
        const uint64_t fr = g_kf.key(wwhd_t60_frame, test_aids::origin_step());
        // keyed by the process id (base_process_class +0x4), not its address: the two runs of a pair can place the same
        // actor at other heap addresses (cck9s2_2: every ChuChu 0xC apart, so each drew other values from the origin on)
        const uint32_t proc = wwhd_t60_exec_proc;
        const uint32_t key = proc ? ld32(proc + 4) : 0xFFFFFFFFu;
        // only outside an execute: a converted process's execute on a step before the tick30 step runs on an in-between
        // pass and is logic (tool line ef58dc096)
        // (the frame-level work: the process handlers' create / delete methods on the frame-work hold pass run marked as logic,
        // g_in_execute, interp.cpp HandlerAsLogic b36a2cb7e: their draws are logic draws too, as at N = 1)
        // (the strict RNG-pair rule tooling, RNG-DRAWSTREAM, 2026-11-25) a draw method's draws (f_pc_draw.cpp's
        // fpcDw_Execute marks them: wwhd_t60_exec_draw) count on the hold counters on every pass too: the picture's
        // draws (a point light's flicker in settingTevStruct, ...) depend on how often and where the process was drawn
        // or culled, which differs between a 30-vs-N pair's pictures; on the logic stream they shifted a logic draw's
        // index (route ld: a seagull culled on B's frame pass at 430 / 431 drew 2 flicker values fewer; its sing gate at
        // 805 then read another value and sang: two extra cries). WWHD_TEST_RNG_DRAWSTREAM=0: the old keying.
        static const bool drawStream = [] { const char* e = getenv("WWHD_TEST_RNG_DRAWSTREAM"); return !(e && e[0] == '0'); }();
        const bool hold = (interp::hold_pass() && !wwhd_t60_exec_logic && !interp::in_execute()) ||
                          (drawStream && wwhd_t60_exec_draw && !wwhd_t60_exec_logic);
        const uint64_t htag = hold ? 0x8000000000000000ull : 0;
        uint32_t idx;
        if (c.keyed == kKeySeq) {
            uint32_t mb;
            memcpy(&mb, &max, 4);
            const uint64_t stream = ((uint64_t)(uint8_t)fn << 32) | mb;
            idx = seq_index(key, stream, hold);
            f = hashed(mix(mix(mix(mix(c.seed) ^ 0x5E9ull) ^ key) ^ stream) ^ idx ^ htag);
        } else {
            idx = draw_index(key, fr, hold);
            f = hashed(mix(mix(mix(c.seed) ^ fr) ^ key) ^ idx ^ htag);
        }
        if (FILE* lf = keyed_log())
            fprintf(lf, "draw frame %u origin %llu key %lld proc %08X id %08X idx %u%s fn %c lr %08X f %.7f\n", wwhd_t60_frame,
                    (unsigned long long)test_aids::origin_step(), (long long)fr, proc, key, idx, hold ? " hold" : "", fn, lr, f);
    }
    if (fn == 'F') f = f * max;  // (fmuls)
    else if (fn == 'X') {
        const float v = (f - 0.5f) * max;  // (fsubs, fmuls, fadds)
        f = v + v;
    }
    *out = f;
    static bool logged = false;
    if (!logged) {
        logged = true;
        log_msg("[t60] WWHD_TEST_RNG_%s=%s: test random numbers on (mode %d), first draw frame %u",
                c.mode == kKeyed ? "KEYED" : "CONST",
                getenv(c.mode == kKeyed ? "WWHD_TEST_RNG_KEYED" : "WWHD_TEST_RNG_CONST"), (int)c.mode, wwhd_t60_frame);
    }
    return true;
}
