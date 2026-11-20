// cheapib stage 2, material animations (WWHD_CHEAPIB_S2 bit 32; decomp build, fast mode; cheapib/stage2/PLAN.md §4).
//
// A material animation (btk texture SRT, brk/bpk colours, ...) is a key animator (material_layout::MaterialWork: +0 its
// time block: +0 frame, +4 start, +8 end, +0x10 the frame mapping; +0x34 its methods) that an actor's Draw evaluates
// (materialColorCalc 027DF40C; interp_fx's hook blends the frame at the pass's fraction) and attaches to the model data
// (+0x38 .. +0x48). The model's viewCalc (ModelPipeline_l::advanceFrame 027F55FC) applies each attached animator's results
// into the model runtime's material uniforms (model +0x14; methods +0x3C, AnimationRuntime::applyChannels: the values
// and their dirty bits), and the UBO update writes the dirty words into the ring slot the logic list binds
// (ModelRuntime::updateUniforms 027DE824). Stage 1's replayed in-between runs no Draw, so the animated blocks keep the
// logic pass's frame.
//
// Here, on the logic pass, the draw-time evaluations and the model calls are noted in their order (one animator may be
// evaluated for several models at different frames: each model's apply follows its own evaluation). An animator whose
// frame moves in the step (its record from the last record pass differs from the step's frame) is "moving". On a
// replayed in-between, the noted sequence of the moving animators runs again: each evaluation at this pass's frame (the
// same blend as interp_fx's hook: anm_mid at pass_t(); the step's exact frame on the record pass), each model call that
// had a moving animator attached applies its noted animators and runs updateUniforms into the slot its logic call
// selected (advanceFrame's flip of the index before the call). Only the dirty words are written (agl's per-slot dirty
// bits), so a model's other material words stay the logic pass's bytes.
// Two calcs apply an animator straight to one model instead of attaching it to the model data:
// mDoExt_btkAnm::calc 025E8048 and mDoExt_brkAnm::calc 025E8480 evaluate (027DF40C) and apply at once (027DF774:
// animator, the model's material target = model runtime); the model's viewCalc then only writes the uniforms (Ice Ring
// Isle's ice wall mist: two btks on daObjIceisland's model). The decomp port point PORT_CHEAPIB_ANM_APPLY tells the
// port after such an apply: the evaluation just noted gets the model; a replayed in-between applies it again right after
// its evaluation, and the model's call is kept (and flagged when the animator moves) even without animators on its data.
// The replayed record pass also takes the records the next step blends from (interp_fx's g_anm, as a drawn record pass
// does), so the next logic pass draws its animations blended, not at the step's frame.
#include "cheapib/cheapib_s2.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cheapib/cheapib.h"
#include "guest_addr.h"
#include "ppc.h"
#include "runtime.h"

#ifdef WWHD_DECOMP_FAST
extern "C" void f_027DF40C_orig(Cpu* c);
namespace interp {
bool record_pass();
uint64_t logic_steps();
float pass_t();
float fx_anm_mid(float a, float b, float start, float end, bool loop, float t);  // interp_fx.cpp (no counters)
bool fx_anm_loop(uint32_t ts);
void fx_anm_record(uint32_t anm, uint32_t ts, float frame);
void fx_anm_blended(uint32_t anm);
}  // namespace interp

namespace cheapib {
namespace {

constexpr uint32_t kRuntime = 0x14, kCounts = 0x2C, kModelData = 0xAC, kAnimationOff = 0x78;
constexpr uint32_t kDataAnimators[5] = {0x38, 0x3C, 0x44, 0x48, 0x40};  // advanceFrame's order
constexpr uint32_t kMethods = 0x34, kUpdate = 0x3C;
constexpr uint32_t kUpdateUniforms = 0x027DE824;
constexpr uint32_t kDirectApply = 0x027DF774;  // (the per-model calcs' apply: animator, material target)

struct Ev {
    uint32_t anm, ts;
    float from, cur, start, end;
    bool loop;
    uint32_t model = 0, data = 0;  // a per-model calc's model (applied at once, kDirectApply) and its data
};
struct Call {
    uint32_t model, data;
    uint8_t slot, n;
    bool off;           // the model's animation disabled (advanceFrame applies none)
    bool flagged = false;  // a moving animator is attached
    uint32_t anm[5], methods[5];
};
struct Item {
    bool call;
    uint32_t index;
};
struct S {
    std::vector<Ev> evs;
    std::vector<Call> calls;
    std::vector<Item> items;
    std::unordered_set<uint32_t> moving;
    std::unordered_set<uint32_t> direct;  // models a per-model calc applied an animator to (this logic pass)
    uint64_t logic_direct = 0;
    bool open = false, valid = false;
    // counters per report
    uint64_t steps = 0, logic_evs = 0, logic_moving = 0, logic_calls = 0, logic_flagged = 0;
    uint64_t passes = 0, evaluated = 0, applied = 0, updated = 0, skipped = 0, records = 0, ns = 0;
} g;

void begin() {
    if (g.open) return;
    g.evs.clear();
    g.calls.clear();
    g.items.clear();
    g.moving.clear();
    g.direct.clear();
    g.open = true;
    g.valid = false;
}

// debug WWHD_CHEAPIB_S2_MATLOG=STEP: the logic pass of STEP logs every model call: its material uniform slices in the
// slot its list binds (to match the verify's WWHD_CHEAPIB_VERIFY_DETAIL block addresses)
void debug_map(uint32_t model, uint32_t data, uint8_t index_before) {
    static const uint64_t at = getenv("WWHD_CHEAPIB_S2_MATLOG") ? strtoull(getenv("WWHD_CHEAPIB_S2_MATLOG"), nullptr, 10) : 0;
    if (!at || interp::logic_steps() != at) return;
    const uint32_t counts = ld32(model + kCounts);
    const int32_t divisor = counts ? ld8(counts + 6) : 0;
    const int32_t next = int32_t(index_before) + 1;
    const uint32_t slot = uint32_t(divisor ? next - (next / divisor) * divisor : next);
    const uint32_t rt = model + kRuntime;
    const uint32_t n = ld16(rt + 0x16), u = ld32(rt + 0x20);
    char b[1600];
    int q = snprintf(b, sizeof b, "model %08X data %08X slot %u anm %08X %08X %08X %08X %08X off %u uniforms %u:", model, data,
                     slot, ld32(data + 0x38), ld32(data + 0x3C), ld32(data + 0x40), ld32(data + 0x44), ld32(data + 0x48),
                     ld8(model + kAnimationOff), n);
    for (uint32_t i = 0; i < n && i < 48 && q < (int)sizeof b - 40; i++) {
        const uint32_t ub = u + 0x3C * i;
        q += snprintf(b + q, sizeof b - q, " %08X+%X", ld32(ub + 0x14) + slot * ld32(ub + 0x18), ld32(ub + 0x18));
    }
    LOG("[cheapib] s2 material map: %s", b);
}

}  // namespace

bool s2_mat_on() { return (s2_mask() & 32) != 0; }

// interp_fx's hook_027DF40C on the logic pass (outside actor Execute): ANM evaluated at the step's frame CUR, drawn
// blended from FROM (= CUR: not blended)
void s2_mat_eval(uint32_t anm, uint32_t ts, float from, float cur) {
    if (!s2_mat_on()) return;
    begin();
    g.items.push_back({false, (uint32_t)g.evs.size()});
    g.evs.push_back({anm, ts, from, cur, (float)ldf32(ts + 4), (float)ldf32(ts + 8), interp::fx_anm_loop(ts)});
}

// decomp port point PORT_CHEAPIB_ANM_APPLY: a per-model calc applied BACKEND (just evaluated) to TARGET (model + 0x14)
void s2_mat_anm_apply(uint32_t backend, uint32_t target) {
    if (!s2_mat_on() || !g.open || g.evs.empty() || g.items.empty() || g.items.back().call) return;
    Ev& e = g.evs.back();
    if (e.anm != backend || target < kRuntime) return;  // (evaluated outside the noted window: not halfway / not logic)
    e.model = target - kRuntime;
    e.data = ld32(e.model + kModelData);
    g.direct.insert(e.model);
    g.logic_direct++;
}

// cheapib_models.h note(): a viewCalc of the logic pass (before it runs; INDEX_BEFORE its buffer index then)
void s2_mat_call(uint32_t model, uint8_t index_before) {
    if (!s2_mat_on()) return;
    const uint32_t data = ld32(model + kModelData);
    if (!data) return;
    debug_map(model, data, index_before);
    const bool direct = g.open && g.direct.count(model);
    Call k{};
    k.model = model;
    k.data = data;
    const uint32_t counts = ld32(model + kCounts);
    const int32_t divisor = counts ? ld8(counts + 6) : 0;
    const int32_t next = int32_t(index_before) + 1;
    k.slot = uint8_t(divisor ? next - (next / divisor) * divisor : next);  // (advanceFrame's flip)
    k.off = ld8(model + kAnimationOff) != 0;
    for (uint32_t off : kDataAnimators) {
        const uint32_t a = ld32(data + off);
        if (!a) continue;
        k.anm[k.n] = a;
        k.methods[k.n] = ld32(a + kMethods);
        k.n++;
    }
    if (!k.n && !direct) return;
    begin();
    g.items.push_back({true, (uint32_t)g.calls.size()});
    g.calls.push_back(k);
}

// the logic pass's per-frame function returned (cheapib_models.h logic_done)
void s2_mat_logic_done() {
    if (!s2_mat_on()) return;
    if (!g.open) {  // (nothing noted this logic pass)
        g.evs.clear();
        g.calls.clear();
        g.items.clear();
        g.moving.clear();
        g.direct.clear();
    }
    g.open = false;
    g.valid = true;
    for (const Ev& e : g.evs)
        if (e.from != e.cur && interp::fx_anm_mid(e.from, e.cur, e.start, e.end, e.loop, 0.5f) != e.cur) g.moving.insert(e.anm);
    std::unordered_set<uint32_t> direct_moving;  // models a moving animator was applied to directly
    for (const Ev& e : g.evs)
        if (e.model && g.moving.count(e.anm)) direct_moving.insert(e.model);
    for (Call& k : g.calls) {
        for (int i = 0; i < k.n && !k.flagged; i++) k.flagged = g.moving.count(k.anm[i]) != 0;
        if (!k.flagged) k.flagged = direct_moving.count(k.model) != 0;
        if (k.flagged) g.logic_flagged++;
    }
    static const uint64_t at = getenv("WWHD_CHEAPIB_S2_MATLOG") ? strtoull(getenv("WWHD_CHEAPIB_S2_MATLOG"), nullptr, 10) : 0;
    if (at && interp::logic_steps() == at)
        for (const Item& it : g.items) {
            if (it.call) {
                const Call& k = g.calls[it.index];
                LOG("[cheapib] s2 material timeline: call model %08X slot %u anm %u flagged %d", k.model, k.slot, k.n, k.flagged);
            } else {
                const Ev& e = g.evs[it.index];
                LOG("[cheapib] s2 material timeline: eval %08X ts %08X from %.3f cur %.3f (%.1f..%.1f %s) moving %d model %08X", e.anm,
                    e.ts, e.from, e.cur, e.start, e.end, e.loop ? "loop" : "clamp", (int)g.moving.count(e.anm), e.model);
            }
        }
    g.steps++;
    g.logic_evs += g.evs.size();
    g.logic_moving += g.moving.size();
    g.logic_calls += g.calls.size();
}

// a replayed hold pass (or a verified in-between), after stage 1's patch and its drain
void s2_mat_rerun(Cpu* c) {
    if (!s2_mat_on() || !(replaying() || verifying()) || g.open || !g.valid) return;
    const auto t0 = std::chrono::steady_clock::now();
    const bool record = interp::record_pass();
    const float t = interp::pass_t();
    g.passes++;
    if (!g.moving.empty()) {
        const Cpu saved = *c;
        for (const Item& it : g.items) {
            if (!it.call) {
                const Ev& e = g.evs[it.index];
                if (!g.moving.count(e.anm)) continue;
                if (ld32(e.anm) != e.ts) {
                    g.skipped++;
                    continue;
                }
                const float f = record ? e.cur : interp::fx_anm_mid(e.from, e.cur, e.start, e.end, e.loop, t);
                const uint32_t was = ld32(e.ts);
                stf32(e.ts, f);
                *c = saved;
                c->r[3] = e.anm;
                f_027DF40C_orig(c);
                st32(e.ts, was);
                g.evaluated++;
                if (e.model) {  // (a per-model calc: applied at once, as the calc does)
                    if (ld32(e.model + kModelData) != e.data) {
                        g.skipped++;
                    } else {
                        *c = saved;
                        guest_call(c, kDirectApply, {e.anm, e.model + kRuntime});
                        g.applied++;
                    }
                }
                if (!record && f != e.cur) interp::fx_anm_blended(e.anm);  // (exact again before the next logic)
                continue;
            }
            const Call& k = g.calls[it.index];
            if (!k.flagged) continue;
            if (ld32(k.model + kModelData) != k.data) {
                g.skipped++;
                continue;
            }
            if (!k.off) {
                for (int i = 0; i < k.n; i++) {
                    if (ld32(k.anm[i] + kMethods) != k.methods[i] || !k.methods[i]) continue;
                    const uint32_t fn = ld32(k.methods[i] + kUpdate);
                    if (!fn) continue;
                    *c = saved;
                    guest_call(c, fn, {k.anm[i], k.model + kRuntime});
                    g.applied++;
                }
            }
            *c = saved;
            guest_call(c, kUpdateUniforms, {k.model + kRuntime, uint32_t(k.slot)});
            g.updated++;
        }
        *c = saved;
    }
    // the replayed record pass: the records the next step's blended frames start from, as a drawn record pass takes them
    if (record && replaying())
        for (const Ev& e : g.evs)
            if (ld32(e.anm) == e.ts) {
                interp::fx_anm_record(e.anm, e.ts, e.cur);
                g.records++;
            }
    g.ns += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    if (g.passes % 900 == 0) {
        auto per = [](uint64_t v, uint64_t d) { return d ? double(v) / double(d) : 0.0; };
        LOG("[cheapib] s2 material: per replayed pass %.1f us, evaluations %.1f, applies %.1f, uniform updates %.1f, "
            "skipped %llu, records %.1f; logic: %.1f draw-time evaluations (%.1f applied per model), %.1f moving animators, "
            "%.1f model calls with animators (%.1f with a moving one) per step",
            per(g.ns, g.passes) / 1000.0, per(g.evaluated, g.passes), per(g.applied, g.passes), per(g.updated, g.passes),
            (unsigned long long)g.skipped, per(g.records, g.passes), per(g.logic_evs, g.steps), per(g.logic_direct, g.steps),
            per(g.logic_moving, g.steps),
            per(g.logic_calls, g.steps), per(g.logic_flagged, g.steps));
        g.steps = g.logic_evs = g.logic_moving = g.logic_calls = g.logic_flagged = g.logic_direct = 0;
        g.passes = g.evaluated = g.applied = g.updated = g.skipped = g.records = g.ns = 0;
    }
}

}  // namespace cheapib
#else
namespace cheapib {
bool s2_mat_on() { return false; }
void s2_mat_eval(uint32_t, uint32_t, float, float) {}
void s2_mat_anm_apply(uint32_t, uint32_t) {}
void s2_mat_call(uint32_t, uint8_t) {}
void s2_mat_logic_done() {}
void s2_mat_rerun(Cpu*) {}
}  // namespace cheapib
#endif

extern "C" void wwhd_cheapib_anm_apply(unsigned backend, unsigned target) { cheapib::s2_mat_anm_apply(backend, target); }
