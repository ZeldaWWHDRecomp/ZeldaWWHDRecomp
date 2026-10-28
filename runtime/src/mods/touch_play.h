// SPDX-License-Identifier: MPL-2.0
// Touch play (controls plan P3): what the on-screen controls do beyond pressing buttons, decided on the
// game's pad reads from one PlayContext (play_context.h). Pure, so touch_play_test checks every rule
// without the game; context_touch.cpp runs it on the touch pad's input (only while the player plays by
// touch: with a controller every button stays exactly as in the game).
//
// - Items by tap: a tap on X, Y or R with an aimed item (bow, boomerang, hookshot, grappling hook) takes
//   it out and keeps it aimed (the button stays held for the game); each further tap shoots (the button
//   lets go for a moment, the game fires, then it is held again); B, or A when the game offers "Back" /
//   "Put away" on it (the grappling hook), puts it away. The latch lets go by
//   itself when the item never got aimed, the aim ended, the item on the button changed or Link no
//   longer plays (a menu, an event, a fade).
// - Aim-only buttons: while an item is latched only its button (shoot), B (put away), A when it puts away
//   ("Back" / "Put away"), ZL and ZR show.
// - Auto put-away: the sword goes back 5 s after the last swing, walk or lock-on; the Deku Leaf once
//   Link stands still on the ground (a short A press, only while the game offers A "Put away").
// - ZL latch: a tap on ZL with a target in reach (or locked) keeps ZL held until the next tap; when the
//   locked enemy is defeated it lets go, or (Next target) locks the next enemy in reach. A tap with no
//   target is the game's plain ZL (the camera goes behind Link).
// - Golden rings: a counter that grows when A gains a new action in play (not "Put away" / "Back" / "Roll" (0x0C), not in talks,
//   shops or minigames, not when the same action just flickered off and on).
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0),
// ControlsView latchAim / putAwayStep / targetDefeated / its new-action rings; reimplemented in C++.
#pragma once
#include <cmath>
#include <cstdint>
#include <initializer_list>

#include "camera_mouse.h"
#include "context_buttons.h"
#include "play_context.h"

namespace mods::touchplay {
using namespace mods::playctx;

// VPAD bits (input.h; context_touch.cpp checks they agree)
constexpr uint32_t kBtnA = 0x8000, kBtnB = 0x4000, kBtnX = 0x2000, kBtnY = 0x1000, kBtnR = 0x0010, kBtnZL = 0x0080;
constexpr uint32_t kItemBits[3] = {kBtnX, kBtnY, kBtnR};

enum ZlLatch : int { kLatchOff = 0, kLatchLetGo = 1, kLatchNext = 2 };
struct Options {
    bool item_tap = true;       // items by tap
    bool aim_only = true;       // only the aiming buttons while an item is latched
    bool auto_put_away = true;  // the sword and the Deku Leaf go away by themselves
    int zl_latch = kLatchLetGo;
};

// timings in logic steps (30 per second)
constexpr uint64_t kShootSteps = 3;         // the button lets go this long for a shot
constexpr uint64_t kAimStartSteps = 45;     // a latch whose item never aims within this lets go
constexpr uint64_t kAimLostSteps = 30;      // ... and one whose aim ended this long ago
constexpr uint64_t kPutAwayHoldSteps = 30;  // a put-away holds the item's button at most this long
constexpr uint64_t kSwordIdleSteps = 150;   // 5 s
constexpr uint64_t kLeafStillSteps = 30;    // 1 s
constexpr uint64_t kPutPressSteps = 2, kPutCooldownSteps = 30;
constexpr uint64_t kRetargetSteps = 3;      // ZL lets go this long to lock the next enemy
constexpr uint64_t kRingRepeatSteps = 15;   // an action back within this is no new action
constexpr float kStickActive = 0.2f;

// the items the game aims while their button is held
inline bool aim_item(uint8_t item) {
    return item == 0x27 || item == 0x35 || item == 0x36 ||  // the Hero's Bow (plain, Fire & Ice, Light)
           item == 0x2D || item == 0x2F || item == 0x25;    // the boomerang, the hookshot, the grappling hook
}
inline bool aiming_now(const PlayContext& c) { return aiming(c) || (c.proc >= 0 && camera_mouse::aiming_item(uint32_t(c.proc))); }
inline bool plays(const PlayContext& c) { return c.has(kInPlay) && c.has(kLink); }
inline uint8_t item_on(const PlayContext& c, int i) { return i == 0 ? c.x_item : i == 1 ? c.y_item : c.r_item; }

struct In {
    uint32_t buttons = 0;  // the pad as the game would read it (after the item ring's gate)
    float lx = 0, ly = 0;
    bool touch = false;    // the player plays by touch now
    uint64_t step = 0;     // logic step
};

class Machine {
public:
    // one pad read (full logic passes only): the buttons the game gets
    uint32_t filter(const In& in, const PlayContext& c, const Options& o) {
        const uint32_t raw = in.buttons;
        const uint32_t down = raw & ~prev_;
        prev_ = raw;
        if (!in.touch) {  // a controller: nothing changes, nothing stays latched
            reset_latches();
            return raw;
        }
        uint32_t out = raw;
        const bool play = plays(c);
        const uint64_t now = in.step;
        if (!started_) sword_active_ = still_ = now, started_ = true;  // (idle time counts from touch play's start)
        // ---- items by tap
        suppress_ &= raw;  // a button let go by a latch stays up until its finger lifts
        if (!o.item_tap && latched_ >= 0) unlatch(raw);
        if (latched_ >= 0) {
            const uint32_t bit = kItemBits[latched_];
            const bool aim = aiming_now(c);
            if (aim) aimed_ = true, aim_step_ = now;
            const bool put_away_a = (down & kBtnA) && (c.a_action == kActReturn || c.a_action == kActPutAway);
            const bool end = (down & kBtnB) || put_away_a || !play || item_on(c, latched_) != item_ ||
                             (!aimed_ && now - latch_step_ > kAimStartSteps) ||
                             (aimed_ && !aim && now - aim_step_ > kAimLostSteps);
            if (end) {
                // put away by A / B: the item's button stays held until the aim ended (letting go of it while
                // aimed would shoot: the grappling hook throws on release)
                if (put_away_a || (down & kBtnB)) putting_ = latched_, putting_until_ = now + kPutAwayHoldSteps;
                unlatch(raw);
            } else {
                if ((down & bit) && aimed_) shoot_until_ = now + kShootSteps;
                if (now < shoot_until_) out &= ~bit;
                else out |= bit;
            }
        } else if (o.item_tap && play) {
            for (int i = 0; i < 3; i++) {
                const uint8_t item = item_on(c, i);
                if (!(down & kItemBits[i]) || !aim_item(item) || !ctxbtn::item_usable(c, item)) continue;
                latched_ = i, item_ = item, latch_step_ = now, aimed_ = false, shoot_until_ = 0;
                out |= kItemBits[i];
                break;
            }
        }
        out &= ~suppress_;
        if (putting_ >= 0) {
            if (!play || !aiming_now(c) || now >= putting_until_) putting_ = -1;
            else out |= kItemBits[putting_];
        }
        // ---- auto put-away
        const bool moving = std::fabs(in.lx) > kStickActive || std::fabs(in.ly) > kStickActive;
        if (!o.auto_put_away || !play || c.item_in_hand != kHandSword || (raw & kBtnB) || moving || c.has(kLocked))
            sword_active_ = now;
        const bool leaf = c.item_in_hand == kItemDekuLeaf;
        if (!o.auto_put_away || !play || !leaf || leaf_flying(c) || (c.mode_flags & kModeMidair) || moving || raw)
            still_ = now;
        if (play && c.a_action == kActPutAway && now >= put_cooldown_ && !(raw & kBtnA) &&
            ((c.item_in_hand == kHandSword && now - sword_active_ >= kSwordIdleSteps) ||
             (leaf && now - still_ >= kLeafStillSteps))) {
            put_until_ = now + kPutPressSteps;
            put_cooldown_ = now + kPutCooldownSteps;
            sword_active_ = still_ = now;
            puts_++;
        }
        if (now < put_until_) out |= kBtnA;
        // ---- ZL latch
        if (o.zl_latch == kLatchOff || !play) zl_ = false, retarget_until_ = 0;
        if (!(raw & kBtnZL)) zl_suppress_ = false;
        if (o.zl_latch != kLatchOff && play && (down & kBtnZL)) {
            if (zl_) {
                zl_ = false, zl_suppress_ = true;
            } else if (c.has(kLocked) || c.lock_reach > 0) {
                zl_ = true, defeats_ = c.defeats;
            }
        }
        if (zl_ && c.defeats != defeats_) {
            defeats_ = c.defeats;
            if (o.zl_latch == kLatchNext && c.lock_reach > 0) retarget_until_ = now + kRetargetSteps;
            else zl_ = false;
        }
        const bool zl = (zl_ && now >= retarget_until_) || ((raw & kBtnZL) && !zl_suppress_);
        out = (out & ~kBtnZL) | (zl ? kBtnZL : 0);
        return out;
    }

    int latched() const { return latched_; }  // the latched item button (0 X, 1 Y, 2 R), -1 none
    bool aim_latched() const { return latched_ >= 0; }
    bool zl_latched() const { return zl_; }
    uint32_t put_aways() const { return puts_; }  // auto put-aways so far (traces, tests)
    void reset() { *this = Machine{}; }

private:
    void unlatch(uint32_t raw) {
        if (latched_ >= 0) suppress_ |= kItemBits[latched_] & raw;
        latched_ = -1, shoot_until_ = 0;
    }
    void reset_latches() {
        latched_ = -1, putting_ = -1, shoot_until_ = 0, suppress_ = 0, zl_ = false, zl_suppress_ = false, retarget_until_ = 0, put_until_ = 0;
        started_ = false;
    }
    uint32_t prev_ = 0, suppress_ = 0;
    int latched_ = -1, putting_ = -1;
    uint64_t putting_until_ = 0;
    uint8_t item_ = kItemNone;
    bool aimed_ = false;
    uint64_t latch_step_ = 0, aim_step_ = 0, shoot_until_ = 0;
    uint64_t sword_active_ = 0, still_ = 0, put_until_ = 0, put_cooldown_ = 0;
    uint32_t puts_ = 0;
    bool started_ = false;
    bool zl_ = false, zl_suppress_ = false;
    uint32_t defeats_ = 0;
    uint64_t retarget_until_ = 0;
};

// Aim-only buttons: while an item is latched only its button (it shoots), B (puts it away), ZL and ZR
inline void aim_only(ctxbtn::Buttons& b, const PlayContext& c, int latched) {
    using namespace ctxbtn;
    if (!b.active || latched < 0 || latched > 2) return;
    const bool a_puts_away = c.a_action == kActReturn || c.a_action == kActPutAway;
    for (Button i : {kA, kX, kY, kR, kPlus, kStickR, kDpadUp, kDpadLeftRight})
        if (i != kA || !a_puts_away) b.b[i].shown = false;
    b.b[kX + latched] = {true, Kind::kItem, item_on(c, latched)};
    b.b[kB] = {true, Kind::kAction, c.b_action && c.b_action != kActHidden && c.b_action != kActSword ? c.b_action : uint8_t(kActPutAway)};
    b.b[kZL].shown = true;
}

// Golden rings: count() grows each time A gains a new action in play
class RingTracker {
public:
    void step(const ctxbtn::Buttons& b, const PlayContext& c, uint64_t now) {
        const ctxbtn::ButtonView& v = b.b[ctxbtn::kA];
        const uint8_t a = v.shown && v.kind == ctxbtn::Kind::kAction ? v.label : 0;
        const bool eligible = b.active && plays(c) && !talking(c) && !c.has(kShop) && c.minigame == 0;
        if (a && a != shown_ && eligible && a != kActPutAway && a != kActReturn && a != kActAttack && !(a == prev_ && now - prev_step_ <= kRingRepeatSteps))
            count_++;
        if (shown_ && shown_ != a) prev_ = shown_, prev_step_ = now;
        shown_ = a;
    }
    int32_t count() const { return count_; }

private:
    uint8_t shown_ = 0, prev_ = 0;
    uint64_t prev_step_ = 0;
    int32_t count_ = 0;
};

}  // namespace mods::touchplay
