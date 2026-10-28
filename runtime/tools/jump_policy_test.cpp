// Jump anywhere: the per-step decision (jump_policy.h). No game files.
#include "mods/jump_policy.h"
#include <cassert>

using namespace mods::jump_policy;

int main() {
    constexpr uint32_t kWait = 0x04, kMove = 0x06, kLadder = 0x3C, kBackJump = 0x22, kSwimWait = 0x2D;
    State s;
    auto run = [&](uint32_t proc, bool button, bool blocked = false, bool dbl = false) {
        return step(s, {proc, button, blocked, dbl});
    };
    // a press jumps from the ground; holding does not jump again
    assert(run(kWait, false) == Action::kNone);
    assert(run(kWait, true) == Action::kJump && s.own);
    assert(!after_step(s, kProcAutoJump, true) && s.own);  // our own jump is never turned into a fall
    assert(run(kProcAutoJump, true) == Action::kNone);
    // no double jump unless the option is on; with it, exactly one per airtime
    assert(run(kProcAutoJump, false) == Action::kNone);
    assert(run(kProcAutoJump, true) == Action::kNone);
    assert(run(kProcAutoJump, false, false, true) == Action::kNone);
    assert(run(kProcAutoJump, true, false, true) == Action::kDoubleJump);
    assert(run(kProcAutoJump, false, false, true) == Action::kNone);
    assert(run(kProcFall, true, false, true) == Action::kNone);
    // landing resets the double jump
    assert(!after_step(s, kWait, true) && !s.own);
    assert(run(kWait, false, false, true) == Action::kNone);
    assert(run(kMove, true, false, true) == Action::kJump);
    assert(run(kProcAutoJump, false, false, true) == Action::kNone);
    assert(run(kBackJump, true, false, true) == Action::kDoubleJump);
    // blocked (event, swimming, menus) and procedures that are neither ground nor air: nothing
    s = {};
    assert(run(kWait, true, true) == Action::kNone);
    assert(run(kWait, false) == Action::kNone);
    assert(run(kLadder, true) == Action::kNone);
    assert(run(kSwimWait, false) == Action::kNone);
    assert(run(kSwimWait, true, false, true) == Action::kNone);
    // the game's ledge jump: a fall with "no automatic ledge jumps", untouched without
    s = {};
    run(kMove, false);
    assert(after_step(s, kProcAutoJump, true));
    assert(!after_step(s, kProcAutoJump, false));
    // grace window after running off an edge: counts as the ground jump, double jump stays
    s = {};
    run(kMove, false);
    for (int i = 0; i < kGraceSteps - 1; i++) assert(run(kProcFall, false, false, true) == Action::kNone);
    assert(run(kProcFall, true, false, true) == Action::kGraceJump);
    assert(run(kProcAutoJump, false, false, true) == Action::kNone);
    assert(run(kProcAutoJump, true, false, true) == Action::kDoubleJump);
    // ... but not after the window (then it is the double jump)
    s = {};
    run(kMove, false);
    for (int i = 0; i < kGraceSteps; i++) run(kProcFall, false, false, true);
    assert(run(kProcFall, true, false, true) == Action::kDoubleJump);
    // and not at all without double jump once the window is over
    s = {};
    run(kMove, false);
    for (int i = 0; i < kGraceSteps; i++) run(kProcFall, false);
    assert(run(kProcFall, true) == Action::kNone);
    // True 60: the controller is read on full and half steps; jump decisions run on full steps only.
    // A press that begins on a half step is neither lost (also when let go before the next full
    // step) nor doubled (when held on through several full steps).
    {
        constexpr uint32_t kJ = 1;  // input::kVirtJump
        struct Read { bool full; bool held; };
        auto jumps = [&](std::initializer_list<Read> reads) {
            State st;
            Latch latch;
            int n = 0;
            step(st, {kWait, false, false, false, 0});
            for (const Read& r : reads) {
                latch.see(r.held ? kJ : 0);
                if (!r.full) continue;  // a half step: the game only previews it
                const uint32_t press = latch.take();
                const Action a = step(st, {kWait, r.held, false, false, (press & kJ) ? 1 : 0});
                if (a == Action::kJump) {
                    ++n;
                    after_step(st, kWait, false);  // landed at once (a test without the game)
                }
            }
            return n;
        };
        // pressed on a half step, let go before the next full step: still one jump
        assert(jumps({{true, false}, {false, true}, {true, false}, {false, false}, {true, false}}) == 1);
        // pressed on a half step and held through three full steps: one jump
        assert(jumps({{true, false}, {false, true}, {true, true}, {false, true}, {true, true}, {false, true}, {true, true}}) == 1);
        // pressed on a full step, held over the half step: one jump
        assert(jumps({{true, true}, {false, true}, {true, true}, {false, false}, {true, false}}) == 1);
        // let go on the half step and pressed again on the next full step: a second press, a second jump
        assert(jumps({{true, true}, {false, false}, {true, true}, {false, true}, {true, false}}) == 2);
        // two separate half-step taps in two steps: two jumps
        assert(jumps({{true, false}, {false, true}, {true, false}, {false, true}, {true, false}}) == 2);
        // nothing pressed: nothing
        assert(jumps({{true, false}, {false, false}, {true, false}}) == 0);
        // without the latch (press = -1) the old edge rule still holds
        State st;
        assert(step(st, {kWait, true, false, false}) == Action::kJump);
        Latch l;
        l.see(kJ); l.see(kJ);
        assert(l.take() == kJ && l.take() == 0);
    }
    // the ground jump is a little higher than the game's, at least the minimum
    assert(ground_speed_y(12.5f) == kGroundMin);
    assert(ground_speed_y(23.7f) > 23.7f);
    return 0;
}
