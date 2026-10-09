#include "countdown.h"
#include "guest_addr.h"
#include "interp.h"
#include "runtime.h"
#include <array>

namespace interp { bool hold_pass(); uint64_t logic_steps(); float pass_t(); bool enabled(); }
extern "C" void f_025C5A8C_orig(Cpu*);

namespace {
countdown::Tick tick;
struct Layout { uint32_t object = 0; uint64_t step = 0; bool race = false; };
std::array<Layout, 16> layouts;
void forget(uint32_t object) {
    for (auto& layout : layouts) if (layout.object == object) layout = {};
}
constexpr uint32_t kRemaining = 0x10C, kStopped = 0x122, kStatus = 0x124;

void display(Cpu* c, const char* surface) {
    // Only layouts actually formatted during this game step are eligible for refresh.
    // Their destructors and state-load reset remove the pointers before reuse.
    const uint32_t object = c->r[31];
    if (object >= mem::kMem2Start && object < mem::kMem2End - 0x68) {
        for (auto& layout : layouts) {
            if (layout.object == object || !layout.object || layout.step != interp::logic_steps()) {
                layout = {object, interp::logic_steps(), surface[0] == 'r'};
                break;
            }
        }
    }
    const uint32_t play = GD(0x1046F0B0);
    const uint32_t timer = ld32(play + 0x5CF0);
    if (timer < mem::kMem2Start || timer >= mem::kMem2End - 0x136) return;
    const bool running = ld8(timer + kStatus) == 2 && ld8(timer + kStopped) == 0;
    const uint32_t native = c->r[7];
    c->r[7] = tick.display_ms(int32_t(native), timer, ld32(timer + kRemaining),
                            interp::logic_steps(), interp::pass_t(), running, interp::enabled());
    static FILE* trace = getenv("WWHD_COUNTDOWN_DISPLAY_TRACE") ? fopen(getenv("WWHD_COUNTDOWN_DISPLAY_TRACE"), "w") : nullptr;
    if (trace) {
        fprintf(trace, "%llu %.6f %u %u %s\n", (unsigned long long)interp::logic_steps(),
                interp::pass_t(), native, c->r[7], surface);
        fflush(trace);
    }
}
}  // namespace

// HD's TIMER method table installs this update in its DRAW slot (101EE73C + 0x10),
// reached through fpcLf_Draw. Execute-queue gating therefore cannot hold this logic back.
// This is the shared timer service: mail sorting, auctions, races and timed challenges.
extern "C" void hook_025C5A8C(Cpu* c) {
    if (interp::enabled() && interp::hold_pass()) {
        c->r[3] = 1;
        return;
    }
    const uint32_t timer = c->r[3], before = ld32(timer + kRemaining);
    f_025C5A8C_orig(c);
    const uint32_t after = ld32(timer + kRemaining);
    tick = {timer, after, interp::logic_steps(), before > 0 && before - 1 == after};
}

// Shared HUD and boat-race timer formatting, after native frames -> milliseconds and before
// digit extraction. Interpolate the register only; gameplay deadlines and sounds stay exact.
extern "C" void site_026277A8(Cpu* c) { display(c, "race"); }
extern "C" void site_026E9C24(Cpu* c) { display(c, "hud"); }

extern "C" void f_026E9BC4(Cpu*);
extern "C" void f_02627734(Cpu*);
extern "C" void f_026E9A38_orig(Cpu*);
extern "C" void f_0262769C_orig(Cpu*);
extern "C" void hook_026E9A38(Cpu* c) { forget(c->r[3]); f_026E9A38_orig(c); }
extern "C" void hook_0262769C(Cpu* c) { forget(c->r[3]); f_0262769C_orig(c); }

namespace countdown {
void reset() { tick = {}; layouts = {}; }
// Called on the main thread before HD layout render jobs are launched. Updating text
// from a worker draw callback would race other surfaces drawing the same layout.
void refresh(Cpu* c) {
    for (const auto layout : layouts) {
        if (!layout.object || layout.step != interp::logic_steps()) continue;
        const uint32_t vtable = ld32(layout.object + 4);
        if (vtable != GD(layout.race ? 0x100E5A58 : 0x10107D10) ||
            !ld32(layout.object + 0x44)) continue; // layout resources may close before destruction
        Cpu saved = *c;
        c->r[3] = layout.object;
        if (layout.race) f_02627734(c); else f_026E9BC4(c);
        *c = saved;
    }
}
} // namespace countdown
