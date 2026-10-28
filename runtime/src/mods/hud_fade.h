// SPDX-License-Identifier: MPL-2.0
// HUD fades (controls plan P3, all platforms): the game's hearts, rupee counter and magic bar fade out
// while they tell nothing new, each by its own option (Display > Screen (HUD); all off by default).
// Pure rules, so hud_fade_test checks them; hud_fade.cpp reads the game once per logic step and
// aspect.cpp applies the alpha to the panes (L_HeartAll_00, L_Rupy_00, L_MagicBar_00).
//
//   Hearts  shown with the sword in hand, while locked on, for 5 s after the life changed (a hit, a
//           heart picked up) and whenever life is low (a quarter of the hearts or less, at most 3 hearts)
//   Rupees  shown for 4 s after the count changed, and in shops
//   Magic   shown for 2 s after the magic changed (the Deku Leaf, Fire / Ice / Light Arrows, a refill)
// Anything but Link in play (title, menus, events, talks, fades) shows them as the game does. The first
// reading never counts as a change.
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0), its
// "hearts only in combat" options; reimplemented.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mods::hudfade {

enum Element : int { kHearts, kRupees, kMagic, kCount };

constexpr uint64_t kHeartsAfterChange = 150, kRupeesAfterChange = 120, kMagicAfterChange = 60;  // logic steps

struct In {
    bool play = false;       // Link is under the player's control (PlayContext kInPlay)
    bool save = false;       // the values below were read (a Quest Log is loaded)
    uint16_t life = 0, max_life = 0, rupees = 0;
    uint8_t magic = 0;
    bool sword = false;      // the sword in Link's hand
    bool locked = false;     // the lock-on holds
    bool shop = false;       // a shop's item choice
};

inline bool low_life(uint16_t life, uint16_t max_life) {  // life in quarter hearts
    return life <= 12 || life * 4 <= max_life;
}

class Rules {
public:
    // one logic step: whether each element should be visible now
    void step(const In& in, uint64_t now, bool out[kCount]) {
        if (in.save) {
            if (seen_ && in.life != life_) hearts_until_ = now + kHeartsAfterChange;
            if (seen_ && in.rupees != rupees_) rupees_until_ = now + kRupeesAfterChange;
            if (seen_ && in.magic != magic_) magic_until_ = now + kMagicAfterChange;
            life_ = in.life, rupees_ = in.rupees, magic_ = in.magic, seen_ = true;
        } else {
            seen_ = false;  // a new file: its first reading is no change
        }
        if (!in.play || !in.save) {
            for (int i = 0; i < kCount; i++) out[i] = true;
            return;
        }
        out[kHearts] = in.sword || in.locked || now < hearts_until_ || low_life(in.life, in.max_life);
        out[kRupees] = in.shop || now < rupees_until_;
        out[kMagic] = now < magic_until_;
    }

private:
    bool seen_ = false;
    uint16_t life_ = 0, rupees_ = 0;
    uint8_t magic_ = 0;
    uint64_t hearts_until_ = 0, rupees_until_ = 0, magic_until_ = 0;
};

// The element's opacity toward wanted: in over about a quarter second, out over about two thirds;
// snaps at the ends (0 = not drawn at all)
constexpr float kInRate = 4.f, kOutRate = 1.5f;  // per second
inline float fade(float alpha, bool wanted, float seconds) {
    if (!(seconds > 0.f)) return alpha;
    alpha += wanted ? seconds * kInRate : -seconds * kOutRate;
    return std::clamp(alpha, 0.f, 1.f);
}

}  // namespace mods::hudfade
