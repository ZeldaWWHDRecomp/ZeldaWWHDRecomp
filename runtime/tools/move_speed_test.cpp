// Unit tests for the run/swim speed mod maths (runtime/src/mods/move_speed.h). Pure functions only:
// the live state (button, mode, stamina, ramp) is mods.cpp and needs no game either.
//   make -C build/cmake move_speed_test && ./build/cmake/move_speed_test
#include "mods/move_speed.h"
#include "mods/hud_visibility.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <string>

using namespace mods;

static void test_distance() {
    // The site multiplies the horizontal speed by the factor, composed with true60's dt. One second
    // of movement must be exactly `factor` times the unboosted distance at any rate and logic mode.
    for (auto proc : {kProcMove, kProcSwimMove})
        for (float factor : {1.25f, 1.5f, 2.f, 4.f})
            for (int rate : {30, 60, 120, 240})
                for (bool true60 : {false, true}) {
                    float distance = 0;
                    const int logic = true60 ? 60 : 30;
                    for (int frame = 0; frame < rate; ++frame)
                        for (int n = frame * logic / rate; n < (frame + 1) * logic / rate; ++n)
                            distance += 10.f * (true60 ? .5f : 1.f) * move_target(true, proc, factor, factor);
                    assert(std::abs(distance / 300.f - factor) < 1e-6f);
                }
}

static void test_target() {
    // only the two movement procedures boost, and only while boosting
    assert(move_target(true, kProcMove, 2.f, 3.f) == 2.f);
    assert(move_target(true, kProcSwimMove, 2.f, 3.f) == 3.f);
    assert(move_target(false, kProcMove, 2.f, 3.f) == 1.f);
    assert(move_target(false, kProcSwimMove, 2.f, 3.f) == 1.f);
    for (uint32_t proc = 0; proc < 256; ++proc)
        if (!is_move_proc(proc)) assert(move_target(true, proc, 4.f, 4.f) == 1.f);
    // a dial of 1 means "no boost in this state": the site then keeps its stock path
    assert(move_target(true, kProcMove, 1.f, 4.f) == 1.f);
    assert(move_target(true, kProcSwimMove, 4.f, 1.f) == 1.f);
}

static void test_clamp() {
    assert(clamp_factor(std::numeric_limits<float>::quiet_NaN()) == 1.50f);
    assert(clamp_factor(std::numeric_limits<float>::infinity()) == 1.50f);
    assert(clamp_factor(0.f) == 1.f);
    assert(clamp_factor(100.f) == 4.f);
    assert(clamp_factor(.5f) == 1.f);
    assert(clamp_stamina_seconds(std::numeric_limits<float>::quiet_NaN()) == 5.f);
    assert(clamp_stamina_seconds(-1.f) == 0.f);
    assert(clamp_stamina_seconds(1000.f) == 60.f);
    assert(clamp_cooldown_seconds(std::numeric_limits<float>::quiet_NaN()) == 3.f);
    assert(clamp_cooldown_seconds(-1.f) == 0.f);
    assert(clamp_cooldown_seconds(1000.f) == 60.f);
    // the defaults a fresh settings file gets
    assert(kDefaultLandFactor == 1.50f && kDefaultSwimFactor == 1.25f);
    assert(kDefaultStaminaSeconds == 5.f && kDefaultCooldownSeconds == 3.f);
}

static void test_ramp() {
    const float dt = 1.f / 30.f;
    // reaches the target, monotonically, from either side
    float up = 1.f, down = 4.f;
    for (int i = 0; i < 300; ++i) {
        float next = ramp_towards(up, 4.f, .15f, dt);
        assert(next >= up && next <= 4.f);
        up = next;
        next = ramp_towards(down, 1.f, .15f, dt);
        assert(next <= down && next >= 1.f);
        down = next;
    }
    assert(up == 4.f && down == 1.f);  // snapped, so "off" is exactly 1
    // ~63% of the way after one time constant
    assert(std::abs(ramp_towards(1.f, 4.f, .15f, .15f) - (1.f + 3.f * (1.f - std::exp(-1.f)))) < 1e-6f);
    // half steps advance half as far, so the curve is rate independent
    const float full = ramp_towards(1.f, 4.f, .15f, dt);
    const float half = ramp_towards(ramp_towards(1.f, 4.f, .15f, dt / 2), 4.f, .15f, dt / 2);
    assert(std::abs(full - half) < 1e-3f);
    // a zero time constant jumps
    assert(ramp_towards(1.f, 4.f, 0.f, dt) == 4.f);
    assert(ramp_towards(1.f, 4.f, .15f, 0.f) == 4.f);
}

static void test_animation() {
    // the animation plays at the movement's factor, and 1 is the authored rate untouched
    assert(animation_factor(1.f) == 1.f);
    assert(animation_factor(1.5f) == 1.5f);
    assert(animation_factor(4.f) == 4.f);
    assert(animation_factor(100.f) == 4.f);
    assert(animation_factor(std::numeric_limits<float>::quiet_NaN()) == 1.50f);
    // the dash option only swaps the locomotion walk, and only while boosting
    assert(boost_anim(kAnmWalk, MoveAnim::kDash, true) == kAnmDash);
    assert(boost_anim(kAnmWalk, MoveAnim::kDash, false) == kAnmWalk);
    assert(boost_anim(kAnmWalk, MoveAnim::kNative, true) == kAnmWalk);
    assert(boost_anim(kAnmDash, MoveAnim::kDash, true) == kAnmDash);   // already the dash
    assert(boost_anim(0, MoveAnim::kDash, true) == 0);                 // ANM_WAITS is not touched
    assert(boost_anim(0x35, MoveAnim::kDash, true) == 0x35);           // nor any other clip
    assert(std::string(move_anim_label(MoveAnim::kNative)) == "native");
    assert(std::string(move_anim_label(MoveAnim::kDash)) == "dash");
}

static void test_stamina() {
    const float dt = 1.f / 30.f;
    // a full bar lasts its stated seconds of boosting
    float s = 1.f;
    for (int i = 0; i < 4 * 30; ++i) s = drain_stamina(s, 4.f, dt);
    assert(s == 0.f);
    s = 1.f;
    for (int i = 0; i < 4 * 30 - 1; ++i) s = drain_stamina(s, 4.f, dt);
    assert(s > 0.f && s <= dt / 4.f + 1e-6f);
    // clamps
    assert(drain_stamina(.01f, 4.f, 1.f) == 0.f);
    assert(refill_stamina(.99f, 4.f, 1.f) == 1.f);
    // 0 seconds = no limit: the bar stays full, so the boost never runs out
    assert(drain_stamina(.5f, 0.f, dt) == 1.f);
    assert(refill_stamina(.5f, 0.f, dt) == 1.f);
    // refills at the same rate
    s = 0.f;
    for (int i = 0; i < 4 * 30; ++i) s = refill_stamina(s, 4.f, dt);
    assert(s == 1.f);
}

static void test_cooldown() {
    const float dt = 1.f / 30.f;
    // the cooldown is the bar's refill time: an empty bar is full again after exactly that long
    float s = 0.f;
    for (int i = 0; i < 3 * 30 - 1; ++i) s = refill_stamina_over(s, 3.f, dt);
    assert(s < 1.f);
    s = refill_stamina_over(s, 3.f, dt);
    assert(s == 1.f);
    // 0 fills at once, so the boost is always ready
    assert(refill_stamina_over(0.f, 0.f, dt) == 1.f);
    assert(refill_stamina_over(.4f, 0.f, dt) == 1.f);
    // a full bar stays full
    assert(refill_stamina_over(1.f, 3.f, dt) == 1.f);
}

// The boost's bar and the game's own screens (issue #233).
static void test_bar_visibility() {
    // a normal limited boost shows the bar, and so does the refill after it
    assert(move_bar_wanted(true, 5.f, true, 1.f));
    assert(move_bar_wanted(true, 5.f, true, .4f));
    assert(move_bar_wanted(true, 5.f, false, .4f));
    assert(!move_bar_wanted(true, 5.f, false, 1.f));
    // no limit: nothing to run out, so no bar at all, boosting or not, whatever is left of a refill
    for (float stamina : {0.f, .5f, 1.f})
        for (bool boosting : {false, true}) {
            assert(!move_bar_wanted(true, 0.f, boosting, stamina));
            assert(!move_bar_wanted(true, -1.f, boosting, stamina));
            assert(!move_bar_wanted(true, std::numeric_limits<float>::quiet_NaN(), boosting, stamina));
            // "Show boost bar" off: never shown
            assert(!move_bar_wanted(false, 5.f, boosting, stamina));
        }
}

static void test_game_ui() {
    assert(!game_ui_hides_hud(GameUi{}));
    assert(game_ui_hides_hud(GameUi{true, false, false}));   // a cutscene or event
    assert(game_ui_hides_hud(GameUi{false, true, false}));   // a message or dialogue
    assert(game_ui_hides_hud(GameUi{false, false, true}));   // the pause menu, items, map, ...
    // the fade: gone in about a quarter of a second at any frame rate, back as quickly
    for (int rate : {30, 60, 144}) {
        const float dt = 1.f / rate;
        float alpha = 1.f;
        int frames = 0;
        while (alpha > 0.f) { alpha = game_ui_fade(alpha, true, dt); ++frames; assert(frames < rate); }
        assert(frames * dt <= .35f && alpha == 0.f);
        for (int i = 0; i < rate / 2; ++i) alpha = game_ui_fade(alpha, false, dt);
        assert(alpha == 1.f);
    }
    // monotonic, bounded, and a stalled clock or a missing frame time changes nothing
    float alpha = 1.f, previous = 1.f;
    for (int i = 0; i < 30; ++i) { alpha = game_ui_fade(alpha, true, 1.f / 60); assert(alpha <= previous && alpha >= 0.f); previous = alpha; }
    assert(game_ui_fade(.5f, true, 0.f) == .5f && game_ui_fade(.5f, false, -1.f) == .5f);
    assert(game_ui_fade(.5f, true, 10.f) == 0.f && game_ui_fade(.5f, false, 10.f) == 1.f);
    // the HUD is drawn with its own alpha times this: a menu over a visible bar leaves nothing
    const float bar = 1.f;
    float shown = 1.f;
    for (int i = 0; i < 30; ++i) shown = game_ui_fade(shown, game_ui_hides_hud(GameUi{false, false, true}), 1.f / 30);
    assert(bar * shown == 0.f);
}

// "Roll, then hold A" (issue #233): the state machine, one call per logic step.
static void test_roll_hold_a() {
    constexpr uint32_t kWait = 0x00, kTalk = 0x5A;  // standing; any other procedure (a talk, a pick-up)
    auto run = [](RollHoldA& r, bool a, uint32_t proc, int steps) {
        bool out = false;
        for (int i = 0; i < steps; ++i) out = r.step(a, proc);
        return out;
    };
    {   // running, A pressed: the game rolls; A still held as the roll ends in running: boost
        RollHoldA r;
        assert(!run(r, false, kProcMove, 10));
        assert(!r.step(true, kProcMove));           // the press, the game has not rolled yet
        assert(!run(r, true, kProcFrontRoll, 15));  // no boost during the roll itself
        assert(r.rolling);
        assert(r.step(true, kProcMove));            // out of the roll, A held: the boost runs
        assert(run(r, true, kProcMove, 60));        // ... as long as A stays held
        assert(!r.step(false, kProcMove));          // releasing A ends it
        assert(!run(r, true, kProcMove, 5));        // holding A again does not restart it
    }
    {   // the press shows in the same step as the roll (the game reacts to the press at once)
        RollHoldA r;
        run(r, false, kProcMove, 3);
        assert(!run(r, true, kProcFrontRoll, 12));
        assert(r.step(true, kProcMove));
    }
    {   // roll, A released before the roll ends: no boost
        RollHoldA r;
        run(r, false, kProcMove, 3);
        run(r, true, kProcFrontRoll, 5);
        run(r, false, kProcFrontRoll, 5);
        assert(!r.step(false, kProcMove) && !run(r, true, kProcMove, 10));
        RollHoldA q;  // a tap that is let go and pressed again late in the roll does not count either
        run(q, false, kProcMove, 3); run(q, true, kProcFrontRoll, 3); run(q, false, kProcFrontRoll, 3);
        run(q, true, kProcFrontRoll, 3);
        assert(!q.step(true, kProcMove));
    }
    {   // A held from a talk (or any other use) before running: a roll can't come from it, no boost
        RollHoldA r;
        run(r, true, kTalk, 30);
        assert(!run(r, true, kProcMove, 30));
        assert(!run(r, true, kProcFrontRoll, 10));  // even if a roll began, the press is not fresh
        assert(!r.step(true, kProcMove));
    }
    {   // a roll from standing does not count
        RollHoldA r;
        run(r, false, kWait, 5);
        r.step(true, kWait);
        run(r, true, kProcFrontRoll, 10);
        assert(!r.step(true, kProcMove));
    }
    {   // the roll ends in something other than running (a wall bump, standing): no boost
        RollHoldA r;
        run(r, false, kProcMove, 3); run(r, true, kProcFrontRoll, 10);
        assert(!r.step(true, kWait) && !run(r, true, kProcMove, 5));
    }
    {   // stopping ends the boost; swimming never boosts with this choice
        RollHoldA r;
        run(r, false, kProcMove, 3); run(r, true, kProcFrontRoll, 10);
        assert(r.step(true, kProcMove));
        assert(!r.step(true, kWait) && !run(r, true, kProcMove, 5));
        RollHoldA s;
        run(s, false, kProcSwimMove, 3);
        assert(!run(s, true, kProcSwimMove, 30));
    }
    {   // a second roll while boosting needs a fresh press: A is held, so it starts a new cycle only after a release
        RollHoldA r;
        run(r, false, kProcMove, 3); run(r, true, kProcFrontRoll, 10); assert(r.step(true, kProcMove));
        run(r, false, kProcMove, 2); run(r, true, kProcFrontRoll, 10);
        assert(r.step(true, kProcMove));
    }
    // the choice by name (the test override) and its saved value
    assert(move_button_from_id("roll-a") == kMoveButtonRollA && move_button_from_id("L3") == 0x40000u);
    assert(move_button_from_id("ZR") == 0x40u && move_button_from_id("A") == 0 && move_button_from_id("") == 0);
    assert(kMoveButtonRollA && !(kMoveButtonRollA & (kMoveButtonRollA - 1)) && !(kMoveButtonRollA & 0x000FFFFFu));
}

int main() {
    test_distance();
    test_target();
    test_clamp();
    test_ramp();
    test_animation();
    test_stamina();
    test_cooldown();
    test_bar_visibility();
    test_game_ui();
    test_roll_hold_a();
    return 0;
}
