// Quick doors and fast scene changes: while a door event or a scene change is in progress, the game
// runs extra logic steps per frame (game time passes faster), so nothing is skipped: every
// animation, event cut, flag and sound of the door or the transition happens exactly as before,
// only in fewer frames.
//
// Quick doors: door actors ask the event manager for their current cut through
// dDoor_info_c::getDemoAction (0252A684; knob doors, shutter doors 10/12, kddoor) or
// daMbdoor_c::getDemoAction (021C0078) while their door event runs. While a door was in such a cut
// during the step just run (not a TALK cut: locked-door messages run at normal speed), the frame gets
// kDoorExtra more logic steps: the steps of fpcM_Management between two executes (priority,
// creation, cCt_Counter) and fpcEx_Handler (every process: Link, door, camera, event manager in the
// play scene). Drawing, process deletion (fpcDt_Handler: its timers count drawn frames, see
// turbo_steps.h), scene management (fapGm_After) and the pad read stay once per frame. Button
// presses ("trigger" bits) are cleared for the extra steps, so a press is seen once.
//
// Fast scene changes: while an overlap (fade/wipe) process exists (l_fopOvlpM_overlap[0],
// 101F36CC), the frame gets kSceneExtra more runs of the transition machinery only: process
// creation (fpcCt_Handler: the new scene's loading phases), the overlap process's execute (its fade
// timers) and fapGm_After (scene and overlap request phases). The scenes themselves (actors,
// events, cutscenes) keep running at normal speed, so no story event is shortened.
//
// Both act on full logic passes only (not on the 60 fps in-between passes) and in all 60 fps modes.
#include <atomic>
#include <cstdlib>
#include <cstring>

#include "guest_addr.h"
#include "mods.h"
#include "../interp.h"
#include "turbo_steps.h"
#include "runtime.h"

extern "C" {
void f_025DE788_orig(Cpu* c);  // fpcEx_Handler
void f_025DE024_orig(Cpu* c);  // fpcDt_Handler
void f_025E0EE4_orig(Cpu* c);  // fpcPi_Handler
void f_025DDCEC_orig(Cpu* c);  // fpcCt_Handler
void f_025D42C4_orig(Cpu* c);  // fapGm_After
void f_0200E6EC_orig(Cpu* c);  // cCt_Counter
void f_025DF940(Cpu* c);       // fpcM_Execute (true60's per-process gate runs too)
void f_0252A684_orig(Cpu* c);  // dDoor_info_c::getDemoAction
void f_021C0078_orig(Cpu* c);  // daMbdoor_c::getDemoAction
void f_025DC86C_orig(Cpu* c);  // fopScnM_ChangeReq
void f_025D57E0(Cpu* c);       // fopAcM_delete
}

namespace interp { bool hold_pass(); }

namespace mods {
namespace {
const uint32_t kOverlap = GD(0x101F36CC);  // l_fopOvlpM_overlap[0] (fopOvlpM_IsPeek 025DBE00)
constexpr uint32_t kOvlpTask = 0x20;       // overlap_request_class::mpTask (fopOvlpM_SceneIsStart)
const uint32_t kPadPtr = GD(0x101F5088);   // the game's pad state (pad accessors 0200763C...)
constexpr uint32_t kCurProc = 0x65F0;      // daPy_lk_c::mCurProc
constexpr int kDoorTalk = 16;              // dDoor_info_c action table: "TALK"

int env_i(const char* n, int d) {
    const char* e = getenv(n);
    return e ? atoi(e) : d;
}
const int kDoorExtra = env_i("WWHD_MOD_DOOR_EXTRA", 3);    // 4 logic steps per frame
const int kSceneExtra = env_i("WWHD_MOD_SCENE_EXTRA", 3);  // 4 transition steps per frame
// debug: WWHD_MOD_DOOR_DELETE=1 runs process deletion in the extra steps too (the behaviour before
// the fix of issue #61, for A/B runs: it can delete a process whose packets are still listed)
const StepParts kDoorStep = [] {
    StepParts p = kDoorExtraStep;
    if (env_i("WWHD_MOD_DOOR_DELETE", 0)) p.deletion = true;
    return p;
}();

// traces (WWHD_MODS_TRACE): the processes in the delete queue whose timer has run out, which the
// next fpcDt_Handler deletes (g_fpcDtTg_Queue 101F3A1C, see fpcDt_Handler 025DE024; delete tag:
// +8 next node, +0xC process, +0x18 timer, fpcDtTg_Do 025DDE44)
const uint32_t kDeleteQueue = GD(0x101F3A1C);
void trace_due_deletes(const char* when) {
    if (!trace_on()) return;
    uint32_t n = ld32(kDeleteQueue);
    for (int k = 0; n && k < 512; k++, n = ld32(n + 8)) {
        if (n < 0x10000000 || n >= 0x50000000) break;
        int timer = (int16_t)ld16(n + 0x18);
        if (timer > 0) continue;
        uint32_t proc = ld32(n + 0xC);
        uint32_t sub = proc ? ld32(proc + 0xF0) : 0;  // fopAc_ac_c::sub_method (true60.cpp kSubMethod)
        uint32_t exec = sub >= 0x10000000 && sub < 0x50000000 ? ld32(sub + 8) : 0;
        trace("%s: process %08X (execute %08X) due for deletion", when, proc, exec);
    }
}

bool g_door_cut = false;  // a door asked for its cut during the logic step just run
int g_door_action = -1;
bool g_door_event = false;  // for traces: door event in progress
uint64_t g_extra_door = 0, g_extra_scene = 0;

// "pressed this frame" / "released this frame" words of the game's pad state (sead controller:
// +0x124 held, +0x18 pressed, +0x1C released, +0x40 hold counter; measured with A presses), cleared
// for the extra steps. WWHD_MODS_PADLOG=1 logs the pad state words when they change.
const uint32_t kTrigWords[] = {0x18, 0x1C};

void padlog() {
    static const bool on = getenv("WWHD_MODS_PADLOG") != nullptr;
    if (!on) return;
    uint32_t p = ld32(kPadPtr);
    if (!p) return;
    static uint32_t last[0x60];
    for (uint32_t i = 0; i < 0x60; i++) {
        uint32_t v = ld32(p + 4 * i);
        if (v != last[i]) LOG("[mods] pad +%03X: %08X -> %08X (step %llu)", 4 * i, last[i], v, (unsigned long long)step());
        last[i] = v;
    }
}

bool overlap_active() { return ld32(kOverlap) != 0; }

// debug: WWHD_MODS_OVLPLOG=1 traces the overlap request words (and its task's first words) on change
void ovlplog() {
    static const bool on = getenv("WWHD_MODS_OVLPLOG") != nullptr;
    uint32_t r = ld32(kOverlap);
    if (!on || !r) return;
    static uint32_t last[16], lastt[8];
    char buf[400];
    int n = 0;
    bool ch = false;
    for (int i = 0; i < 16; i++) {
        uint32_t v = ld32(r + 4 * i);
        ch |= v != last[i];
        last[i] = v;
        n += snprintf(buf + n, sizeof buf - n, " %08X", v);
    }
    uint32_t t = ld32(r + kOvlpTask);
    if (t) {
        n += snprintf(buf + n, sizeof buf - n, " | task");
        for (int i = 0; i < 8; i++) {
            uint32_t v = ld32(t + 0xC0 + 4 * i);
            ch |= v != lastt[i];
            lastt[i] = v;
            n += snprintf(buf + n, sizeof buf - n, " %08X", v);
        }
    }
    if (ch) trace("ovlp%s", buf);
}

void call(Cpu* c, void (*f)(Cpu*), uint32_t r3) {
    c->r[3] = r3;
    f(c);
}

// test aid (issue #61): WWHD_TEST_DOOR_DELETE=FN (hex) queues the first actor whose execute
// function is FN for deletion (fopAcM_delete) right after the first logic step of each door event,
// as an actor deleting itself in that step would (a rat going into its hole, a pot breaking, Tingle
// leaving). FN=list traces the actors (execute function, position) instead.
const uint32_t kActorQueue = GD(0x101F3328);  // g_fopAcTg_Queue (fopAcIt_Executor 025D51DC)
uint32_t actor_execute(uint32_t a) {
    uint32_t sub = ld32(a + 0xF0);  // fopAc_ac_c::sub_method (true60.cpp kSubMethod)
    return sub >= 0x10000000 && sub < 0x50000000 ? ld32(sub + 8) : 0;
}
void test_door_delete(Cpu* c) {
    static const char* e = getenv("WWHD_TEST_DOOR_DELETE");
    if (!e) return;
    const bool list = !strcmp(e, "list");
    const uint32_t fn = (uint32_t)strtoul(e, nullptr, 16);
    uint32_t n = ld32(kActorQueue);
    for (int k = 0; n && k < 2048; k++, n = ld32(n + 8)) {
        if (n < 0x10000000 || n >= 0x50000000) break;
        uint32_t a = ld32(n + 0xC);
        if (!a) continue;
        uint32_t x = actor_execute(a);
        if (list) {
            trace("actor %08X execute %08X at (%.0f, %.0f, %.0f)", a, x, ldf32(a + 0x314), ldf32(a + 0x318), ldf32(a + 0x31C));
        } else if (x == fn) {
            trace("test: queueing actor %08X (execute %08X) for deletion in the door event", a, x);
            uint32_t lr = c->lr, r3 = c->r[3];
            call(c, f_025D57E0, a);  // fopAcM_delete
            c->lr = lr;
            c->r[3] = r3;
            return;
        }
    }
}
}  // namespace

// interp.cpp's fpcEx_Handler hook, after the step's own execute
void after_execute(Cpu* c, uint32_t execute_fn) {
    // in-between passes of the 60 fps modes: no 30 Hz logic ran (doors, scenes), nothing to do
    if (interp::hold_pass()) return;
    padlog();
    ovlplog();
    bool door = g_door_cut;
    g_door_cut = false;
    if (door != g_door_event) {
        g_door_event = door;
        trace("door event %s", door ? "running" : "done");
        if (door) test_door_delete(c);
    }
    static bool ovl = false;
    if (overlap_active() != ovl) {
        ovl = !ovl;
        trace("overlap (fade) %s", ovl ? "starts" : "ends");
    }
    uint32_t lr = c->lr, r3 = c->r[3];
    // quick doors: whole logic steps
    if (quick_doors()) {
        uint32_t pad = ld32(kPadPtr);
        uint32_t saved[sizeof kTrigWords / sizeof *kTrigWords];
        bool cleared = false;
        for (int i = 0; i < kDoorExtra && door; i++) {
            if (pad && !cleared) {
                for (size_t k = 0; k < sizeof kTrigWords / sizeof *kTrigWords; k++) {
                    saved[k] = ld32(pad + kTrigWords[k]);
                    st32(pad + kTrigWords[k], 0);
                }
                cleared = true;
            }
            // (no fpcDt_Handler: process deletion stays once per frame, see turbo_steps.h, issue #61)
            const StepParts& kStep = kDoorStep;
            trace_due_deletes(kStep.deletion ? "extra step deletes" : "extra step leaves");
            if (kStep.counter) call(c, f_0200E6EC_orig, 0);  // cCt_Counter(0) (end of the previous step)
            if (kStep.deletion) call(c, f_025DE024_orig, 0);  // fpcDt_Handler
            if (kStep.priority) call(c, f_025E0EE4_orig, 0);  // fpcPi_Handler
            if (kStep.creation) call(c, f_025DDCEC_orig, 0);  // fpcCt_Handler
            g_door_cut = false;
            if (kStep.execute) { interp::record_executed_step(); call(c, f_025DE788_orig, execute_fn); }
            g_extra_door++;
            door = g_door_cut;  // continue only while the door event still runs
            g_door_cut = false;
        }
        if (cleared)
            for (size_t k = 0; k < sizeof kTrigWords / sizeof *kTrigWords; k++) st32(pad + kTrigWords[k], saved[k]);
    }
    // fast scene changes: transition machinery only
    if (fast_scenes()) {
        for (int i = 0; i < kSceneExtra && overlap_active(); i++) {
            call(c, f_025DDCEC_orig, 0);  // fpcCt_Handler: loading phases of the new scene
            uint32_t ovl_req = ld32(kOverlap);
            uint32_t task = ovl_req ? ld32(ovl_req + kOvlpTask) : 0;
            if (task) call(c, f_025DF940, task);  // the overlap process: fade timers
            call(c, f_025D42C4_orig, 0);  // fapGm_After: scene / overlap request phases
            g_extra_scene++;
        }
    }
    c->lr = lr;
    c->r[3] = r3;
}

// Link executed (camera.cpp's daPy_Execute hook): control traces
void link_executed(uint32_t link) {
    if (!trace_on()) return;
    static uint32_t last = 0xFFFFFFFF;
    uint32_t p = ld32(link + kCurProc);
    if (p != last) {
        trace("link proc %u -> %u", last, p);
        last = p;
    }
}

}  // namespace mods

using namespace mods;

// door cuts
extern "C" void hook_0252A684(Cpu* c) {
    f_0252A684_orig(c);
    int a = (int)c->r[3];
    if (a != kDoorTalk) g_door_cut = true;
    if (a != g_door_action) {
        trace("door cut %d", a);
        g_door_action = a;
    }
}
extern "C" void hook_021C0078(Cpu* c) {
    f_021C0078_orig(c);
    g_door_cut = true;
}

// fopScnM_ChangeReq: scene change trigger (traces only)
extern "C" void hook_025DC86C(Cpu* c) {
    uint32_t r3 = c->r[3];
    f_025DC86C_orig(c);
    if (c->r[3]) trace("scene change request (proc %u)", r3 & 0xFFFF);
}
