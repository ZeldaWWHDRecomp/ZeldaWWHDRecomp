// True 60 Hz game logic: the step driver and the timebase of the decomp build (t60.h). Decomp build only.
//
// What is reused from the retired recompiled-code True 60 (2026-10-01..11-05) and what
// is not:
// - kept: the pass structure (frame interpolation's logic pass = the 30 Hz step, in-between passes = the extra steps),
//   so every hold interpolation keeps for logic in the draw path (world block, particles, HUD, timers, HD UI screens,
//   sound starts from draws, process creation and deletion) holds on the extra steps too; the per-process rate gate at
//   the process executor (now in the decompiled fpcEx_Execute, with a side table of converted processes instead of
//   per-procedure lists of Link); the pad read once per step with buttons on the 30 Hz grid;
// - not kept: half-pass "previews" with snapshots and rollbacks of Link, the camera, the random seeds and the draw
//   state, per-instruction ratio hooks, NaN fallbacks. Here a process is either converted in its source (it runs every
//   step with g_dt = 1/N) or it runs exactly as in the 30 Hz game on the tick30 step, and a whole 30 Hz frame is either
//   at N or at 1 (the fallback), never mixed.
//
// Switches (environment): WWHD_T60_STEPS=N (steps per 30 Hz frame: 1 default, 2, 4; frame interpolation is switched on
// at 30 N fps); WWHD_T60_AT_STEP=k (N from the k-th 30 Hz step on, 1 before); WWHD_T60_TEST_CONVERT=all|PROFNAME[,...]
// (tests: those processes count as converted although they are not, so they run every step: proves the gate, plays
// them N times too fast; a comma list converts several together, e.g. "168,0x1DC" Link and the camera: riders and
// carriers, readers and the read, switch together); WWHD_T60_TEST_CONVERT_FROM=seconds (scenario time, WWHD_TEST_*:
// the test conversion starts with the first 30 Hz frame at or after it; before, those processes run unconverted, as in
// the 30 Hz game: a pair can reach a place at 30 Hz in both runs and compare N steps from there); WWHD_T60_STEPLOG=path (one line per logic step: `step full dt`, step = the 30 Hz frame the
// step ends, full = it is the tick30 step); WWHD_T60_TRACE=path (one line per pass: pass, 30 Hz step, tick30, sub, N,
// the fallback flag).
#include "t60/t60.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "runtime.h"

// test aid WWHD_T60_EVLOG (softlock sweep) (t60_evlog.cpp sets it when the env var is set; the t60_test unit test links without it)
void (*t60_evlog_step)(unsigned long long frame, int tick, int sub) = nullptr;
// test aid WWHD_EVENT_TIMELINE (test_aids.cpp sets it when the env var is set): called as each 30 Hz frame's first logic step
// begins (its event-end save-info dumps at the 30 Hz frame boundary)
void (*t60_frame_first_hook)() = nullptr;
#define T60_EXPORT extern "C" __attribute__((visibility("default")))
T60_EXPORT float wwhd_t60_dt = 1.0f;
T60_EXPORT uint32_t wwhd_t60_tick30 = 1;
T60_EXPORT uint32_t wwhd_t60_steps = 1;
// the first step of a 30 Hz frame: the frame's one input read (always 1 at N = 1)
T60_EXPORT uint32_t wwhd_t60_first = 1;
// the step's index within its 30 Hz frame, 1..N (N on the tick30 step; always 1 at N = 1): decomp gabi_t60.h t60::step()
// (particles: an emitter's first calc runs at the k / N of the frame already elapsed)
T60_EXPORT uint32_t wwhd_t60_stepk = 1;
T60_EXPORT int wwhd_t60_world60 = 0;
// The frame-level work (process handlers, the play scene's execute)
// runs on the frame's first step (decomp f_pc_t60.h); the play scene then sets wwhd_t60_world60 on that step, so the flag
// is cleared when the frame's first step begins (after the decision for the frame), not on its logic pass. ON by default
// (the intended production form; off, scene-level systems like the attention lock run after Link's
// first step); WWHD_T60_SCENE_FIRST=0 turns it off.
int t60_scene_first_from_env(const char* e) { return e ? atoi(e) : 1; }
T60_EXPORT int wwhd_t60_scene_first = t60_scene_first_from_env(getenv("WWHD_T60_SCENE_FIRST"));
// Events (decomp d_event_t60.h): WWHD_T60_EVENTS60 lifts the 30 Hz fallback for events and messages.
// Default 1 (cutscenes and talks at true 60 too; softlock sweep on FG
// with EVENTS60=1: kalle / et / molg no softlock, end saves equal); WWHD_T60_EVENTS60=0 restores the fallback.
T60_EXPORT int wwhd_t60_events60 = getenv("WWHD_T60_EVENTS60") ? atoi(getenv("WWHD_T60_EVENTS60")) : 1;
// Cutscene timeline (decomp JST_timeline_t60.h): the cutscene timeline's evaluate-only pass on the frame's later steps;
// WWHD_T60_JST=0 turns it off (the 30 Hz timeline values hold over the frame: the control arm)
T60_EXPORT int wwhd_t60_jst = getenv("WWHD_T60_JST") ? atoi(getenv("WWHD_T60_JST")) : 1;
T60_EXPORT int wwhd_t60_test_convert = 0;
// the profiles of WWHD_T60_TEST_CONVERT (0-terminated; wwhd_t60_test_convert -1: all)
static int g_test_convert_list[9] = {0};
// WWHD_T60_TEST_CONVERT_FROM: the scenario time the test conversion starts (< 0: from the start); the scenario's time
// (input_sdl apply_scenario, -1 before its origin) and the conversion of the running frame, latched at its first step
static const double g_convert_from = getenv("WWHD_T60_TEST_CONVERT_FROM") ? atof(getenv("WWHD_T60_TEST_CONVERT_FROM")) : -1.0;
static double g_scenario_time = -1.0;
static bool g_convert_on = !getenv("WWHD_T60_TEST_CONVERT_FROM");
T60_EXPORT float wwhd_t60_ran = -1.0f;
T60_EXPORT T60Pad wwhd_t60_pad = {0, 0, {0, 0, 0, 0}};
// True 60 fps input: the decompiled sead::ControllerManager::calc reads the GamePad on
// every step and says so here (t60.h); the per-read edges of each controller state, for converted code (true60.h
// t60_in_step_word: a press is consumed by exactly one step)
T60_EXPORT uint32_t wwhd_t60_in_read = 0;
namespace {
struct InEdges { uint32_t state, word[3]; };
InEdges g_in_edges[16];
int g_in_count = 0;
}  // namespace
T60_EXPORT void wwhd_t60_in_edges(uint32_t state, uint32_t press, uint32_t release, uint32_t repeat) {
    int i = 0;
    while (i < g_in_count && g_in_edges[i].state != state) i++;
    if (i == g_in_count) {
        if (g_in_count == 16) return;  // (16 controller states: the game has 9)
        g_in_count++;
    }
    g_in_edges[i] = {state, {press, release, repeat}};
}
T60_EXPORT int wwhd_t60_in_step_word(uint32_t state, uint32_t offset, uint32_t* word) {
    if (offset > 8 || offset % 4 != 0) return 0;
    for (int i = 0; i < g_in_count; i++)
        if (g_in_edges[i].state == state) {
            *word = g_in_edges[i].word[offset / 4];
            return 1;
        }
    return 0;
}
// Step counter (true60.h t60_step_counter): +1 per logic step (fapGm_Execute); here so that every game unit reads one counter
T60_EXPORT uint32_t wwhd_t60_step_counter = 0;
T60_EXPORT uint32_t wwhd_t60_frame = 0;
// test aid WWHD_TEST_RNG_KEYED (t60_test_rng.cpp): the process whose execute runs (its guest address) (decomp f_pc_executor.cpp
// fpcEx_Execute sets it around fpcBs_Execute in the game build; 0 outside an execute)
T60_EXPORT uint32_t wwhd_t60_exec_proc = 0;
// test aid WWHD_TEST_RNG_KEYED: 1 inside an execute (decomp fpcEx_Execute T60_EXEC_LOGIC), else 0
T60_EXPORT uint32_t wwhd_t60_exec_logic = 0;
// test aid WWHD_TEST_PROC_ID_BASE (savestate.cpp; decomp fpcBs_MakeOfId takes it once)
T60_EXPORT uint32_t wwhd_test_proc_id_base = 0;
// Integer smoothing (decomp true60.h): the c_lib integer-smoothing table's use, set by the game; logged by pass_begin when it grows
T60_EXPORT uint32_t wwhd_t60_ismooth_peak = 0;
T60_EXPORT uint32_t wwhd_t60_ismooth_overflow = 0;
// Damage-reaction set latch (decomp true60.h / c_damagereaction.h t60dr_mark_set): key -> the 30 Hz frame tag of the set
static std::unordered_map<uint32_t, uint32_t> g_latch;
T60_EXPORT void wwhd_t60_latch_set(uint32_t key, uint32_t frame) { g_latch[key] = frame; }
T60_EXPORT int wwhd_t60_latch_pending(uint32_t key, uint32_t frame) {
    if (g_latch.empty()) return 0;
    auto it = g_latch.find(key);
    if (it == g_latch.end()) return 0;
    if (it->second == frame) return 1;
    g_latch.erase(it);  // (an older frame's: done)
    return 0;
}

// Per-frame holds (true60.h t60_hold30_request / t60_held): converted processes asked to run as unconverted ones (tick30
// step only) from the next 30 Hz frame on, e.g. Link on a carrier that moves him once per frame. The requests made
// during a frame become the holds of the next one at its first step; within a frame the holds stay fixed.
namespace {
constexpr int kHolds = 16;
uint32_t g_hold_req[kHolds], g_hold_cur[kHolds];
int g_hold_nreq = 0, g_hold_ncur = 0;
uint32_t g_hold_frame = ~0u;  // the frame whose holds g_hold_cur are
void hold_latch(uint32_t frame) {
    if (frame == g_hold_frame) return;
    g_hold_frame = frame;
    g_hold_ncur = g_hold_nreq;
    std::copy(g_hold_req, g_hold_req + g_hold_nreq, g_hold_cur);
    g_hold_nreq = 0;
}
}  // namespace
T60_EXPORT void wwhd_t60_hold30_request(unsigned id) {
    hold_latch(wwhd_t60_frame);
    for (int i = 0; i < g_hold_nreq; i++)
        if (g_hold_req[i] == id) return;
    if (g_hold_nreq < kHolds) g_hold_req[g_hold_nreq++] = id;
}
T60_EXPORT int wwhd_t60_held(unsigned id) {
    hold_latch(wwhd_t60_frame);
    for (int i = 0; i < g_hold_ncur; i++)
        if (g_hold_cur[i] == id) return 1;
    return 0;
}

// "168,0x1DC" -> {168, 0x1DC, 0}; at most max entries (later ones are logged and left out); returns the first (0: none)
int t60_parse_convert_list(const char* e, int* out, int max) {
    int n = 0;
    const char* p = e;
    while (*p) {
        char* end = nullptr;
        long v = strtol(p, &end, 0);
        if (end == p) break;  // not a number: the rest is ignored
        if (v != 0) {
            if (n < max)
                out[n++] = (int)v;
            else
                LOG("[t60] WWHD_T60_TEST_CONVERT: more than %d profiles, %ld left out", max, v);
        }
        p = end;
        while (*p == ',' || *p == ' ') p++;
    }
    out[n] = 0;
    return out[0];
}
// WWHD_T60_TEST_UNCONVERT (tests only, no-conversion controls): a comma list of profile names (decimal or 0x hex, at most
// 8) that count as NOT converted, the game's default list (decomp true60.h kT60Converted) and WWHD_T60_TEST_CONVERT
// included; read once at start; unset or empty: nothing excluded (normal play never sets it)
static int g_test_unconvert_list[9] = {0};
static int test_unconvert_from_env() {
    const char* e = getenv("WWHD_T60_TEST_UNCONVERT");
    if (!e || !*e) return 0;
    return t60_parse_convert_list(e, g_test_unconvert_list, 8);  // (logged on the first step: no LOG at static init)
}
T60_EXPORT int wwhd_t60_test_unconvert = test_unconvert_from_env();
T60_EXPORT int wwhd_t60_test_unconverted(int profName) {
    if (profName == 0) return 0;
    for (const int* p = g_test_unconvert_list; *p != 0; p++)
        if (*p == profName) return 1;
    return 0;
}
int t60_test_unconvert_set(const char* e) {  // (tests: re-read a list)
    g_test_unconvert_list[0] = 0;
    wwhd_t60_test_unconvert = (e && *e) ? t60_parse_convert_list(e, g_test_unconvert_list, 8) : 0;
    return wwhd_t60_test_unconvert;
}
T60_EXPORT int wwhd_t60_test_converted(int profName) {
    if (wwhd_t60_test_convert == -1) return 1;
    if (wwhd_t60_test_convert == kT60TestWindowClosed) return 0;
    if (profName == 0) return 0;
    for (const int* p = g_test_convert_list; *p != 0; p++)
        if (*p == profName) return 1;
    return 0;
}

namespace t60 {
namespace {
int parse_convert_list(const char* e, int* out, int max) { return t60_parse_convert_list(e, out, max); }
// the converted profiles, for compare60's strict gates for converted processes: "[t60] test conversion
// profiles: all" or a comma list of 4-digit hex profiles
void log_convert_list(int convert) {
    if (convert == 0) return;
    if (convert == -1) {
        LOG("[t60] test conversion profiles: all");
        return;
    }
    char b[96] = {0};
    size_t o = 0;
    for (int i = 0; i < 8 && g_test_convert_list[i] && o + 6 < sizeof b; i++)
        o += snprintf(b + o, sizeof b - o, "%s%04X", i ? "," : "", (unsigned)g_test_convert_list[i]);
    LOG("[t60] test conversion profiles: %s", b);
}
int env_n() {
    const char* e = getenv("WWHD_T60_STEPS");
    int n = e ? atoi(e) : 1;
    if (n != 1 && n != 2 && n != 4) {
        if (e) LOG("[t60] WWHD_T60_STEPS=%s: steps per 30 Hz frame must be 1, 2 or 4; 1", e);
        n = 1;
    }
    return n;
}
int g_selected = env_n();
// the settings' change (request_n) waiting for the next 30 Hz frame boundary (0: none)
std::atomic<int> g_requested{0};
// every logic step run so far, the true-60 steps before a frame's tick30 step included (steps_run: the overlay's rate)
std::atomic<uint64_t> g_steps_run{0};
const uint64_t g_at_step = getenv("WWHD_T60_AT_STEP") ? strtoull(getenv("WWHD_T60_AT_STEP"), nullptr, 10) : 0;
uint64_t g_frames = 0;   // logic passes so far (WWHD_T60_AT_STEP)
int g_build_n = 1;       // N of the 30 Hz frame whose steps run now: it ends with the next logic pass
int g_spacing = 1;       // passes per step of that frame
bool g_sub = false;      // the current pass is a step before the tick30 step
bool g_tick = true;      // the current pass is a tick30 step
bool g_world = false;    // the fallback flag of the last decision
double g_step_time = 0;  // step_time()
uint64_t g_passes = 0;
// why the last decision ran a frame at 1 although N > 1 was selected (logged when it changes)
enum Why { kRunning, kNotSelected, kNoPasses, kFallback } g_why = kNotSelected;
uint32_t g_latched = 0;  // presses seen by in-between polls, for the next live read
uint32_t g_raw_prev = 0;
FILE* g_trace = getenv("WWHD_T60_TRACE") ? fopen(getenv("WWHD_T60_TRACE"), "w") : nullptr;
FILE* g_steplog = getenv("WWHD_T60_STEPLOG") ? fopen(getenv("WWHD_T60_STEPLOG"), "w") : nullptr;

// WWHD_T60_TEST_CONVERT: "all" -> -1; else a comma list of profile names (decimal or 0x hex, at most 8) into
// g_test_convert_list; returns the first (0: none)
int test_convert() {
    const char* e = getenv("WWHD_T60_TEST_CONVERT");
    if (!e || !*e) return 0;
    if (!strcmp(e, "all")) return -1;
    return parse_convert_list(e, g_test_convert_list, 8);
}

void log_why(Why w, int passes) {
    if (w == g_why) return;
    g_why = w;
    static const char* const kText[] = {"running", "not selected", "frame interpolation off or too few passes per step",
                                        "30 Hz fallback (no play, or event / message / menu / pause / stage change)"};
    if (w == kNoPasses)
        LOG("[t60] %d steps per 30 Hz frame selected, 30 Hz now: %s (%d passes per step)", g_selected, kText[w], passes);
    else if (g_selected > 1 || w == kRunning)
        LOG("[t60] %d steps per 30 Hz frame selected, now %s", g_selected, w == kRunning ? "running" : kText[w]);
}
}  // namespace

int selected_n() { return g_at_step && g_frames < g_at_step ? 1 : g_selected; }
void set_n(int n) {
    if (n != 1 && n != 2 && n != 4) return;
    g_selected = n;
    LOG("[t60] steps per 30 Hz frame: %d", n);
}
void request_n(int n) {
    if (n != 1 && n != 2 && n != 4) return;
    g_requested.store(n, std::memory_order_relaxed);
}
uint64_t steps_run() { return g_steps_run.load(std::memory_order_relaxed); }
int requested_n() {
    const int r = g_requested.load(std::memory_order_relaxed);
    return r ? r : g_selected;
}
bool n_from_env() {
    static const bool set = getenv("WWHD_T60_STEPS") != nullptr;
    return set;
}
void load_saved_n(int n) {
    if (n_from_env() || (n != 1 && n != 2 && n != 4)) return;
    g_selected = n;
    if (n > 1) LOG("[t60] steps per 30 Hz frame: %d (the saved Frame rate setting)", n);
}

void pass_begin(bool interp_on, bool hold, int phase, int passes, uint64_t logic_steps) {
    static const int convert = (LOG("[t60] decomp build timebase: WWHD_T60_STEPS=%d SCENE_FIRST=%d EVENTS60=%d", g_selected, wwhd_t60_scene_first, wwhd_t60_events60), test_convert());
    static const bool conv_logged = (getenv("WWHD_T60_TEST_CONVERT_FROM") ? 0 : (log_convert_list(convert), 0), true);  // (compare60's strict gates)
    (void)conv_logged;
    {  // integer-smoothing table use: each new peak (grown by more than an eighth), each overflow count doubling
        static uint32_t peak_logged = 0, over_logged = 0;
        if (wwhd_t60_ismooth_peak > peak_logged + peak_logged / 8) {
            peak_logged = wwhd_t60_ismooth_peak;
            LOG("[t60] integer smoothing: peak %u values in one 30 Hz frame (table 2048 slots)", peak_logged);
        }
        if (wwhd_t60_ismooth_overflow > 0 && wwhd_t60_ismooth_overflow >= 2 * over_logged) {
            over_logged = wwhd_t60_ismooth_overflow;
            LOG("[t60] integer smoothing: %u table overflow(s) so far (exact overflow map; peak %u)", over_logged,
                wwhd_t60_ismooth_peak);
        }
    }
    g_passes++;
    wwhd_t60_dt = 1.0f;
    // (g_convert_on is false only with WWHD_T60_TEST_CONVERT_FROM before its time: kT60TestWindowClosed, so that the
    // decomp's t60_converted holds the default conversion list too and both runs of a pair run alike at 30 Hz until then)
    wwhd_t60_test_convert = g_convert_on ? convert : kT60TestWindowClosed;
    static bool unconvert_logged = false;
    if (!unconvert_logged && wwhd_t60_test_unconvert != 0) {  // (compare60 / the lane scripts read it from B's log)
        unconvert_logged = true;
        char b[96] = {0};
        size_t o = 0;
        for (int i = 0; i < 8 && g_test_unconvert_list[i] && o + 6 < sizeof b; i++)
            o += snprintf(b + o, sizeof b - o, "%s%04X", i ? "," : "", (unsigned)g_test_unconvert_list[i]);
        LOG("[t60] test unconversion profiles: %s", b);
    }
    int n = 1, sub = 0;
    if (!hold) {  // the logic pass: the tick30 step that ends the frame planned on the last frame's in-between passes
        if (const int r = g_requested.exchange(0, std::memory_order_relaxed); r && r != g_selected) {
            // (the settings' change: from the next frame on, whose N is decided on its first in-between pass)
            g_selected = r;
            LOG("[t60] steps per 30 Hz frame: %d (Frame rate setting)", r);
        }
        g_frames++;
        n = g_build_n;
        sub = n - 1;
        g_build_n = 1;  // (the next frame: decided on this frame's first in-between pass)
        g_spacing = 1;
        g_tick = true;
        g_sub = false;
        if (!wwhd_t60_scene_first || n == 1)
            wwhd_t60_world60 = 0;  // (set again by this step's play scene)
    } else {
        if (phase == 1) {  // the next 30 Hz frame: N steps when the play scene of this logic pass allowed it
            if (!g_convert_on && g_scenario_time >= 0 && g_scenario_time >= g_convert_from) {
                // (the frame's input read, on this pass before the step begins, set the scenario time: the whole frame
                // runs converted from its first step on)
                g_convert_on = true;
                wwhd_t60_test_convert = convert;
                LOG("[t60] WWHD_T60_TEST_CONVERT_FROM: test conversion on from scenario time %.3f s", g_scenario_time);
                log_convert_list(convert);
            }
            const int want = selected_n();
            g_world = wwhd_t60_world60 != 0;
            Why why = kRunning;
            if (want <= 1) why = kNotSelected;
            else if (!interp_on || passes < want || passes % want != 0) why = kNoPasses;
            else if (!g_world) why = kFallback;
            log_why(why, passes);
            g_build_n = why == kRunning ? want : 1;
            g_spacing = std::max(1, passes / g_build_n);
        }
        g_tick = false;
        g_sub = g_build_n > 1 && phase % g_spacing == 0 && phase / g_spacing < g_build_n;
        n = g_build_n;
        sub = g_sub ? phase / g_spacing - 1 : -1;
        if (wwhd_t60_scene_first && g_sub && sub == 0)
            wwhd_t60_world60 = 0;  // (the frame's first step: set again by its play scene)
    }
    g_step_time = (double)logic_steps + (g_sub ? (double)sub / n + 1.0 / n : 0.0);
    if (g_tick || g_sub) g_steps_run.fetch_add(1, std::memory_order_relaxed);
    wwhd_t60_tick30 = g_tick ? 1 : 0;
    wwhd_t60_first = (g_tick && n == 1) || (g_sub && sub == 0) ? 1 : 0;
    wwhd_t60_stepk = g_tick ? (uint32_t)n : g_sub ? (uint32_t)(sub + 1) : 1u;
    wwhd_t60_frame = (uint32_t)record_step(logic_steps);
    wwhd_t60_steps = (uint32_t)n;
    if (g_steplog && (g_tick || g_sub)) {
        fprintf(g_steplog, "%llu %d %g\n", (unsigned long long)record_step(logic_steps), g_tick ? 1 : 0, 1.0 / n);
        fflush(g_steplog);
    }
    if (t60_frame_first_hook && wwhd_t60_first) t60_frame_first_hook();
    if (t60_evlog_step && (g_tick || g_sub)) t60_evlog_step(record_step(logic_steps), g_tick ? 1 : 0, sub);
    if (g_trace) {
        fprintf(g_trace, "%llu %llu %d %d %d %d\n", (unsigned long long)g_passes, (unsigned long long)logic_steps, (int)g_tick,
                sub, n, (int)g_world);
        fflush(g_trace);
    }
}

void test_scenario_time(double t) { g_scenario_time = t; }
double test_scenario_time() { return g_scenario_time; }
bool active() { return wwhd_t60_steps > 1; }
bool sub_step() { return g_sub; }
bool tick30() { return g_tick; }
bool first_step() { return wwhd_t60_first != 0; }
// (the decomp's t60_frame_work_step, f_pc_t60.h: the first step with the switch on; at N > 1 it is a hold pass, g_sub)
bool frame_work_hold_pass() { return wwhd_t60_scene_first != 0 && g_sub && wwhd_t60_first != 0; }
bool next_pass_first(bool interp_on, bool hold_next, int next_phase, int passes) {
    if (!interp_on || !hold_next) return g_build_n <= 1;  // a logic pass: the frame's first step only at N = 1
    int n = g_build_n, spacing = g_spacing;
    if (next_phase == 1) {  // (the next frame's N, decided as pass_begin will decide it)
        const int want = selected_n();
        n = want > 1 && passes >= want && passes % want == 0 && wwhd_t60_world60 != 0 ? want : 1;
        spacing = std::max(1, passes / n);
    }
    return n > 1 && next_phase == spacing;
}
bool next_pass_step(bool interp_on, bool hold_next, int next_phase, int passes) {
    if (!interp_on || !hold_next) return true;  // a logic pass: always a step (the tick30 step)
    int n = g_build_n, spacing = g_spacing;
    if (next_phase == 1) {  // (the next frame's N, decided as pass_begin will decide it)
        const int want = selected_n();
        n = want > 1 && passes >= want && passes % want == 0 && wwhd_t60_world60 != 0 ? want : 1;
        spacing = std::max(1, passes / n);
    }
    return n > 1 && next_phase % spacing == 0 && next_phase / spacing < n;  // (pass_begin's g_sub)
}
double next_pass_step_offset(bool interp_on, bool hold_next, int next_phase, int passes) {
    if (!interp_on || !hold_next) return g_build_n > 1 ? (double)(g_build_n - 1) / g_build_n : 0.0;  // (the tick30 step)
    if (!next_pass_step(interp_on, hold_next, next_phase, passes)) return 0.0;
    int n = g_build_n, spacing = g_spacing;
    if (next_phase == 1) {  // (as next_pass_step)
        const int want = selected_n();
        n = want > 1 && passes >= want && passes % want == 0 && wwhd_t60_world60 != 0 ? want : 1;
        spacing = std::max(1, passes / n);
    }
    return (double)(next_phase / spacing - 1) / n;
}
// WWHD_T60_AT_STEP=k: before the k-th 30 Hz step the run is a 30 Hz run, frame interpolation included (switched on at
// 30 N fps from step k; before, a whole N 4 boot ran its title / file-select presses at 120 fps passes and did not start
// the game in time: a test pair)
bool wants_unpaced() { return selected_n() > 1; }
// (a pending settings change counts already: frame interpolation is switched to its rate, or no longer held at 30 N, on
// the passes before the frame boundary takes it; the step decision itself waits for that boundary)
int wants_fps() {
    const int n = requested_n();
    return (g_at_step && g_frames < g_at_step) || n <= 1 ? 0 : 30 * n;
}
uint64_t record_step(uint64_t logic_steps) { return g_tick ? logic_steps : logic_steps + 1; }
double step_time() { return g_step_time; }

bool poll_every_read() { return g_build_n > 1 || wwhd_t60_steps > 1; }
void pad_poll(uint32_t raw, const float sticks[4], bool live_read) {
    wwhd_t60_pad.trig = raw & ~g_raw_prev;
    wwhd_t60_pad.hold = raw;
    std::copy(sticks, sticks + 4, wwhd_t60_pad.stick);
    if (!live_read) g_latched |= raw & ~g_raw_prev;
    g_raw_prev = raw;
}
uint32_t game_buttons(uint32_t hold) {
    const uint32_t out = hold | g_latched;
    g_latched = 0;
    return out;
}
}  // namespace t60
