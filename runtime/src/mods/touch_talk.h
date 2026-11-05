// SPDX-License-Identifier: MPL-2.0
// Talks, answers, number entry, shops, the wind compass and the pause menu's pages by touch or mouse
// (controls plan P5), and "hold to go on" in talks for a finger and for a controller's A. Pure: the rules
// decide, from one PlayContext (play_context.h), the fingers on the TV picture and what the shop shows,
// which pad input the game gets; touch_talk_test checks every rule without the game. touch_talk.cpp runs it
// on the game's pad reads and gets the fingers from Android (TouchControls) and the desktop mouse.
//
// Positions are layout units: the TV picture as the game's 2D layouts see it, 1280 x 720 around the centre,
// +y up (a wider picture extends x).
//
// A finger on the picture is the talk's (claims()) while a talk, a choice, a number entry, a shop's item
// choice or the wind compass is up, and in a game menu shown on the TV. Then:
// - Talk: a tap goes on (A). A finger held there goes on by itself each time the message waits for it (its
//   "go on" icon has stayed up a moment), never at a choice, and, optionally, fast forwards while held.
// - Choice: a tap on a line moves the cursor there (stick pushes, one line each) and takes it (A). In a
//   shop's question a tap off the lines answers its last line ("no"), and so does a controller's B.
// - Number entry (the auction's bid): the finger moves like the stick, one push per 60 units (up / down: the
//   digit, left / right: which digit); a tap takes the number (A).
// - Shop: a tap on an item brings the cursor to it (stick pushes towards it, one at a time, re-aimed after
//   each); a tap on the chosen item asks to buy it (A), and its price text goes on by itself to the question.
//   Nothing else ever presses A in a shop: buying needs a tap on the question's "yes" line.
// - Wind compass: a finger points the stick from the compass's centre; a tap takes the direction (A).
// - A system question (the save question after a game over): a tap on one of its buttons moves the cursor there
//   and takes it (A); a tap elsewhere does nothing.
// - Letters, stone tablets and signs (their scroll windows count as the message window): a tap goes on, as in talks.
// - Game menu on the TV: a swipe sideways turns the page between Items and Map (L: in the TV menu L switches
//   them, measured; R and ZL / ZR do nothing there).
// Hold to go on with a controller (option): A held in a talk goes on once as usual; held on, the game no
// longer sees it and A is pressed for it each time the message waits, until it is let go. A question that
// opens under the held A gets no press (A stays up for the game until it is let go).
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0),
// mods/minigames.cpp (talks, choices, number entry, shops) and turbo.cpp (hold to go on); reimplemented for
// our design: the rules are pure and decided natively, the fingers come from the same path on every platform.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

#include "play_context.h"

namespace mods::talktouch {
using namespace mods::playctx;

// VPAD bits (input.h; touch_talk.cpp checks they agree)
constexpr uint32_t kBtnA = 0x8000, kBtnB = 0x4000, kBtnL = 0x0020, kBtnR = 0x0010;

struct Options {
    bool touch = true;      // talks, answers, number entry, shops, wind compass, menu pages by touch / mouse
    bool hold_ff = false;   // fast forward while a finger (or the held A) holds a talk
    bool pad_hold = false;  // hold A on a controller to go on in talks
};

enum Mode : int { kModeNone, kModeTalk, kModeChoice, kModeNumber, kModeShop, kModeWind, kModeMenu, kModeQuestion };

// ---- geometry and timing (layout units; logic steps, 30 per second) ----
constexpr float kChoiceLine = 50;         // between a choice's lines (measured: Beedle's question, cursor at -121 / -170)
constexpr float kChoiceHalfWidth = 200;   // a tap this far left or right of the box's centre still hits a line (box: 316 wide)
constexpr float kTapMove = 40;            // a tap moves less than this ...
constexpr uint64_t kTapSteps = 15;        // ... and lasts at most this long
constexpr uint64_t kHoldSteps = 12;       // a finger down this long without moving far holds the talk
constexpr float kHoldMove = 60;
constexpr uint64_t kIconSteps = 10;       // the "go on" icon stays up this long before a hold presses A
constexpr uint64_t kChoiceQuietSteps = 10;  // ... and no choice was up for this long
constexpr uint64_t kPadHoldSteps = 12;    // a controller's A held this long in a talk holds it
constexpr float kNumberStep = 60;         // number entry: a push per this much finger movement
constexpr float kShopReach = 90;          // a tap this close to an item's centre is on it
constexpr int kShopPushes = 10;           // at most this many pushes towards one item
constexpr uint64_t kAskSteps = 15;        // a shop's price text goes on after its icon was up this long
constexpr int kAskPresses = 2;            // ... at most this many times after a buy tap
constexpr float kWindDead = 30;           // wind compass: no direction this close to its centre
constexpr float kSwipe = 160;             // menu pages: a swipe moves at least this far sideways ...
constexpr uint64_t kSwipeSteps = 20;      // ... within this many steps
constexpr float kViewHalfW = 600, kViewHalfH = 330;  // the picture's safe area (a shop item beyond is out of view)
constexpr float kQuestionHalfW = 160, kQuestionHalfH = 55;  // a system question's button: a tap this close to its centre

// A finger on the TV picture, as it changed (layout units)
struct Finger {
    enum Kind : uint8_t { kDown, kMove, kUp, kCancel };
    Kind kind = kDown;
    float x = 0, y = 0;
};

// the shop's item choice as the runtime sees it (dShop_now_triggercheck's ShopItems_c and the items' actors)
struct Shop {
    bool open = false;   // the item choice ran within the last steps
    int selected = -1;   // mSelectedItemIdx (-1: none yet)
    bool seen[8] = {};   // the item's actor exists and is in front of the camera
    float x[8] = {}, y[8] = {};
};

struct In {
    uint64_t step = 0;           // logic step of this read
    const Finger* fingers = nullptr;
    int nfingers = 0;            // the fingers' changes since the last read, in order
    bool pad_a = false, pad_b = false;  // A / B held on the controller in this read (before this filter)
    bool menu_on_tv = false;     // a game menu shows on the TV (Pro Controller mode): swipes turn its pages
    float wind_x = 0, wind_y = 0;  // the wind compass's centre (as drawn)
    float choice_x = 0;          // the choice box's centre x (as drawn)
    Shop shop;
};

struct Out {
    uint32_t press = 0;    // buttons held for the game in this read
    uint32_t release = 0;  // buttons kept from the game in this read
    bool stick = false;    // the left stick is (lx, ly) in this read
    float lx = 0, ly = 0;
    bool fast_forward = false;  // the talk is fast forwarded now (hold with the option on)
};

inline Mode mode_of(const PlayContext& c, bool menu_on_tv) {
    if (!c.has(kInGame)) return kModeNone;
    if (c.has(kMenu)) return menu_on_tv ? kModeMenu : kModeNone;
    if (c.has(kWindCompass)) return kModeWind;
    if (c.has(kNumberEntry)) return kModeNumber;
    if (c.has(kChoice)) return kModeChoice;
    if (c.has(kQuestion) && c.question_shown) return kModeQuestion;
    if (c.has(kShop)) return kModeShop;
    if (talking(c)) return kModeTalk;
    return kModeNone;
}

// a finger landing on the picture is the talk's now (else the touch controls' or the game's)
inline bool claims(const PlayContext& c, const Options& o, bool menu_on_tv) {
    return o.touch && mode_of(c, menu_on_tv) != kModeNone;
}

// the choice's line under y (0 the top), or -1; lines of a box of n centred on box_y, kChoiceLine apart
inline int choice_line_at(float box_y, int n, float y) {
    if (n < 2) return -1;
    const float first = box_y + kChoiceLine * float(n - 1) / 2;
    const int line = int(std::lround((first - y) / kChoiceLine));
    if (line < 0 || line >= n) return -1;
    const float line_y = first - kChoiceLine * float(line);
    return std::fabs(y - line_y) <= kChoiceLine * 0.6f ? line : -1;
}
// the line the cursor is on (clamped)
inline int choice_cursor_line(float box_y, int n, float cursor_y) {
    const float first = box_y + kChoiceLine * float(n - 1) / 2;
    return std::clamp(int(std::lround((first - cursor_y) / kChoiceLine)), 0, std::max(n - 1, 0));
}

// A system question (SequenceWindow_00: the save question after a game over and the like; the Wii U took it by
// touch on the GamePad, the TV shows the same buttons): the button under (x, y), -1 none; and whether the cursor
// is on it already (the larger one, or the only one of its row: the centre button)
inline int question_button_at(const PlayContext& c, float x, float y) {
    int to = -1;
    for (int i = 0; i < 3; i++)
        if ((c.question_shown >> i & 1) && std::fabs(x - c.question_x[i]) < kQuestionHalfW && std::fabs(y - c.question_y[i]) < kQuestionHalfH)
            to = i;
    return to;
}
inline bool question_cursor_on(const PlayContext& c, int to) {
    const int other = to == 0 ? 1 : 0;
    return to == 2 || !(c.question_shown >> other & 1) || c.question_scale[to] > c.question_scale[other] + 5;
}

// the shop item nearest to (x, y) within kShopReach, -1 none
inline int shop_item_at(const Shop& s, float x, float y) {
    int best = -1;
    float bd = kShopReach;
    for (int i = 0; i < 8; i++) {
        if (!s.seen[i]) continue;
        const float d = std::hypot(s.x[i] - x, s.y[i] - y);
        if (d < bd) bd = d, best = i;
    }
    return best;
}

class Machine {
public:
    // One pad read on a logic step (repeated reads between steps use held()). `pad_a` / `pad_b` in `in` are
    // the controller's, as this read has them.
    Out step(const PlayContext& c, const In& in, const Options& o) {
        Out out;
        const uint64_t now = in.step;
        const Mode mode = o.touch || o.pad_hold ? mode_of(c, in.menu_on_tv) : kModeNone;
        // the talk's state: a shop talk lasts until the talk ends; the key icon's and the choice's ages
        if (!talking(c) && !c.has(kEvent)) shop_talk_ = false, asks_ = 0;
        if (mode == kModeShop) shop_talk_ = true;
        if (c.has(kChoice) || c.has(kNumberEntry) || c.has(kShop)) choice_seen_ = now;
        if (c.has(kKeyIcon) && mode == kModeTalk) {
            if (!icon_) icon_ = true, icon_since_ = now;
        } else {
            icon_ = false;
        }
        if (mode != mode_) {  // a new screen: what was queued for the old one goes
            pushes_.clear();
            target_ = -1, target_pushes_ = 0;
            if (mode == kModeShop && mode_ != kModeShop) peeked_ = false;
            mode_ = mode;
        }
        // ---- the fingers
        for (int i = 0; i < in.nfingers; i++) finger(in.fingers[i], c, in, o, now);
        if (!o.touch) grip_ = {};
        // a finger held on a talk
        if (grip_.down && !grip_.holding && !grip_.moved && now - grip_.t0 >= kHoldSteps && mode == kModeTalk)
            grip_.holding = true;
        if (grip_.holding && grip_.moved) grip_.holding = false;  // slid away: no longer a hold
        const bool finger_hold = grip_.down && grip_.holding && mode == kModeTalk;
        // ---- a controller's A held (hold to go on with a controller)
        if (!in.pad_a) pad_hold_ = false, pad_down_ = 0;
        else if (!pad_down_) pad_down_ = now + 1;  // (+1: 0 means up)
        if (o.pad_hold && in.pad_a && !pad_hold_ && mode == kModeTalk && now + 1 - pad_down_ >= kPadHoldSteps) pad_hold_ = true;
        if (!o.pad_hold) pad_hold_ = false;
        if (pad_hold_) out.release |= kBtnA;  // the game waits for a new press: the hold presses for it
        // ---- go on by itself: a held finger or A, each time the message waits (never in a shop's talk, never
        // right after a choice was up, never while something is queued)
        if ((finger_hold || pad_hold_) && mode == kModeTalk && !shop_talk_ && icon_ && now - icon_since_ >= kIconSteps &&
            now - choice_seen_ >= kChoiceQuietSteps && pushes_.empty() && now >= next_hold_) {
            press(kBtnA);
            next_hold_ = now + 4 + kIconSteps;  // (the icon goes and comes back before the next one)
            icon_ = false;
            holds_++;
        }
        // a shop's price text after a buy tap goes on to the question by itself
        if (asks_ > 0 && shop_talk_ && mode == kModeTalk && icon_ && now - icon_since_ >= kAskSteps && pushes_.empty() &&
            now - choice_seen_ >= kChoiceQuietSteps) {
            asks_--;
            press(kBtnA);
            icon_ = false;
        }
        if (mode == kModeChoice) asks_ = 0;
        // a controller's B at a shop's question answers its last line ("no")
        const bool b_down = in.pad_b && !pad_b_;
        pad_b_ = in.pad_b;
        if (mode == kModeChoice && shop_talk_ && o.touch && b_down && pushes_.empty() && c.choice_count >= 2) {
            take_line(c, c.choice_count - 1);
            out.release |= kBtnB;
        }
        if (mode == kModeChoice && shop_talk_ && in.pad_b) out.release |= kBtnB;
        // ---- the shop: steering towards the tapped item, the first look at items out of view
        if (mode == kModeShop && o.touch) shop_step(in.shop);
        // ---- the wind compass: the held finger points the stick
        if (mode == kModeWind && grip_.down && o.touch) {
            const float dx = grip_.x - in.wind_x, dy = grip_.y - in.wind_y, d = std::hypot(dx, dy);
            if (d > kWindDead) out.stick = true, out.lx = dx / d, out.ly = dy / d;
        }
        // ---- the queue: one push per read until its steps are spent
        if (!pushes_.empty()) {
            Push& p = pushes_.front();
            out.press |= p.buttons;
            if (p.stick) out.stick = true, out.lx = p.lx, out.ly = p.ly;
            if (--p.steps <= 0) pushes_.pop_front();
        }
        out.fast_forward = o.hold_ff && (finger_hold || pad_hold_) && mode == kModeTalk && !shop_talk_;
        last_ = out;
        return out;
    }
    // a repeated read between logic steps: the same as the step's read (the queue doesn't advance)
    const Out& held() const { return last_; }

    bool busy() const { return !pushes_.empty(); }
    bool holding() const { return grip_.down && grip_.holding; }
    bool pad_holding() const { return pad_hold_; }
    bool shop_talk() const { return shop_talk_; }
    int shop_target() const { return target_; }
    uint32_t holds() const { return holds_; }  // presses made by a hold so far (traces, tests)
    void reset() { *this = Machine{}; }

private:
    struct Push {
        uint32_t buttons = 0;
        bool stick = false;
        float lx = 0, ly = 0;
        int steps = 0;
    };
    struct Grip {
        bool down = false, moved = false, holding = false;
        float x0 = 0, y0 = 0, x = 0, y = 0, ax = 0, ay = 0;  // landed, now, the number entry's anchor
        uint64_t t0 = 0;
    };
    void press(uint32_t buttons) {
        pushes_.push_back({buttons, false, 0, 0, 2});
        pushes_.push_back({0, false, 0, 0, 2});
    }
    void stick(float lx, float ly) {
        pushes_.push_back({0, true, lx, ly, 2});
        pushes_.push_back({0, true, 0, 0, 3});
    }
    // the cursor to `to`, then A
    void take_line(const PlayContext& c, int to) {
        const int n = c.choice_count;
        int from = choice_cursor_line(c.choice_y, n, c.cursor_y);
        for (; from != to; from += to > from ? 1 : -1) stick(0, to > from ? -1.0f : 1.0f);
        press(kBtnA);
    }

    void finger(const Finger& f, const PlayContext& c, const In& in, const Options& o, uint64_t now) {
        if (f.kind == Finger::kDown) {
            grip_ = {true, false, false, f.x, f.y, f.x, f.y, f.x, f.y, now};
            return;
        }
        if (!grip_.down) return;
        grip_.x = f.x, grip_.y = f.y;
        const float travel = std::hypot(f.x - grip_.x0, f.y - grip_.y0);
        if (travel >= kTapMove && !grip_.holding) grip_.moved = true;  // (a hold survives a small slide)
        if (travel >= kHoldMove) grip_.moved = true;
        if (mode_ == kModeNumber && o.touch && pushes_.empty()) {  // number entry: the finger as the stick
            const float dx = f.x - grip_.ax, dy = f.y - grip_.ay;
            if (std::fabs(dx) >= kNumberStep || std::fabs(dy) >= kNumberStep) {
                if (std::fabs(dx) > std::fabs(dy)) stick(dx > 0 ? 1.0f : -1.0f, 0);
                else stick(0, dy > 0 ? 1.0f : -1.0f);
                grip_.ax = f.x, grip_.ay = f.y;
            }
        }
        if (f.kind == Finger::kMove) return;
        const Grip g = grip_;
        grip_ = {};
        if (f.kind == Finger::kCancel || !o.touch) return;
        const bool tap = !g.moved && now - g.t0 <= kTapSteps;
        switch (mode_) {
            case kModeTalk:
                if (tap && !g.holding && pushes_.empty()) press(kBtnA);
                return;
            case kModeChoice: {
                if (!tap || !pushes_.empty() || c.choice_count < 2) return;
                const bool on_box = std::fabs(f.x - in.choice_x) <= kChoiceHalfWidth;
                const int line = on_box ? choice_line_at(c.choice_y, c.choice_count, f.y) : -1;
                if (line >= 0) take_line(c, line);
                else if (shop_talk_) take_line(c, c.choice_count - 1);  // a shop's question: off the lines is "no"
                return;
            }
            case kModeNumber:
                if (tap && pushes_.empty()) press(kBtnA);
                return;
            case kModeShop: {
                if (!tap || !pushes_.empty()) return;
                const int item = shop_item_at(in.shop, f.x, f.y);
                if (item < 0) return;
                if (item == in.shop.selected) {
                    press(kBtnA);  // the game brings it up and names its price; the question needs a tap on "yes"
                    asks_ = kAskPresses;
                    target_ = -1;
                } else {
                    target_ = item, target_pushes_ = 0;
                }
                return;
            }
            case kModeWind:
                if (tap && pushes_.empty()) press(kBtnA);
                return;
            case kModeQuestion: {
                // a tap on a button moves the cursor there (a stick push, unless it is on it already) and takes it
                // (A). From the GreenNaugahyde fork's minigames.cpp sequence_step.
                if (!tap || !pushes_.empty()) return;
                const int to = question_button_at(c, f.x, f.y);
                if (to < 0) return;
                if (!question_cursor_on(c, to)) stick(to == 0 ? -1.0f : 1.0f, 0);
                press(kBtnA);
                return;
            }
            case kModeMenu: {
                const float dx = f.x - g.x0, dy = f.y - g.y0;
                if (std::fabs(dx) >= kSwipe && std::fabs(dx) > 2 * std::fabs(dy) && now - g.t0 <= kSwipeSteps && pushes_.empty())
                    press(kBtnL);  // (either way: L switches between the two pages)
                return;
            }
            default:
                return;
        }
    }

    void shop_step(const Shop& s) {
        if (!s.open || !pushes_.empty()) return;
        // the first look: an item out of view (a long shelf) comes in with one push towards it
        if (!peeked_) {
            peeked_ = true;
            float far = 1, fx = 0, fy = 0;
            for (int i = 0; i < 8; i++) {
                if (!s.seen[i]) continue;
                const float out = std::max(std::fabs(s.x[i]) / kViewHalfW, std::fabs(s.y[i]) / kViewHalfH);
                if (out > far) far = out, fx = s.x[i], fy = s.y[i];
            }
            if (far > 1) {
                if (std::fabs(fx) / kViewHalfW >= std::fabs(fy) / kViewHalfH) stick(fx > 0 ? 1.0f : -1.0f, 0);
                else stick(0, fy > 0 ? 1.0f : -1.0f);
                return;
            }
        }
        if (target_ < 0) return;
        if (s.selected == target_ || target_pushes_ >= kShopPushes || !s.seen[target_]) {
            target_ = -1;
            return;
        }
        const bool from_item = s.selected >= 0 && s.selected < 8 && s.seen[s.selected];
        const float sx = from_item ? s.x[s.selected] : 0, sy = from_item ? s.y[s.selected] : 0;
        const float dx = s.x[target_] - sx, dy = s.y[target_] - sy;
        if (std::fabs(dx) >= std::fabs(dy)) stick(dx > 0 ? 1.0f : -1.0f, 0);
        else stick(0, dy > 0 ? 1.0f : -1.0f);
        target_pushes_++;
    }

    std::deque<Push> pushes_;
    Grip grip_;
    Mode mode_ = kModeNone;
    Out last_;
    bool icon_ = false;
    uint64_t icon_since_ = 0, choice_seen_ = 0, next_hold_ = 0, pad_down_ = 0;
    bool pad_hold_ = false, pad_b_ = false;
    bool shop_talk_ = false, peeked_ = false;
    int asks_ = 0;
    int target_ = -1, target_pushes_ = 0;
    uint32_t holds_ = 0;
};

}  // namespace mods::talktouch
