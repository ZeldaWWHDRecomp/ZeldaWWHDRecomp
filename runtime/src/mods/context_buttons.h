// SPDX-License-Identifier: MPL-2.0
// Context buttons: which virtual buttons the on-screen controls (and any host-drawn button hints)
// show right now, with which label and icon, decided from one PlayContext (play_context.h). Pure, so
// context_buttons_test checks every situation without the game; Java (TouchControls) only draws the
// result and routes fingers.
//
// The rules follow what the game itself offers: its button prompts (the A / B / ZR actions), the
// HUD's own rules for the item buttons (dMeter_xyAlpha in the decompilation: which items are allowed
// while swimming, on a rope, in the boat, in the Picto Box...), and what does nothing in a situation
// although the game still draws it (the sword in water or on a ladder, first person while climbing).
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0),
// ControlsView.shown() / aAction() / usable(); reimplemented in C++ for our design.
#pragma once
#include <cstdint>
#include <cstdio>

#include "play_context.h"

namespace mods::ctxbtn {
using namespace mods::playctx;

enum Button : uint8_t { kA, kB, kX, kY, kR, kZR, kZL, kPlus, kStickR, kDpadUp, kDpadLeftRight, kCount };

enum class Kind : uint8_t {
    kPlain,        // the button itself, no game word (ZL, +, R-stick, the d-pad)
    kAction,       // label = the game's action code (Action; its text: CommandGuide_00 T_ACT_Command_NN)
    kItem,         // label = the item number on the button (its icon: BtnItemIcon_00, item_icons.cpp)
    kFastForward,  // ZR fast-forwards the event (fast_forward.cpp, while that option is on)
};

struct ButtonView {
    bool shown = false;
    Kind kind = Kind::kPlain;
    uint8_t label = 0;  // Action code or item number by kind
};

struct Options {
    bool fast_forward = false;  // the event fast forward is on (ZR does it during events)
    bool tap_to_continue = false;  // a tap anywhere is A in talks: A itself goes (touch only)
};

struct Buttons {
    ButtonView b[kCount];
    bool active = false;          // the context rules apply (false: show the full pad, as without them)
    bool tap_continues = false;   // a tap anywhere on the picture goes on (the message's key icon is up)
    const ButtonView& operator[](Button i) const { return b[i]; }
};

// The item numbers the boat allows (dMeter_xyAlpha's ship list): sails, telescope, Wind Waker, the
// hooks, the boomerang, the bottles' contents, the Picto Boxes, armour, bait, bows, bombs
inline bool ship_item(uint8_t item) {
    static constexpr uint8_t kShip[] = {0x78, 0x77, 0x20, 0x22, 0x25, 0x2D, 0x21, 0x23, 0x26, 0x2A, 0x2C, 0x27, 0x35,
                                        0x36, 0x31, 0x2F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59};
    for (uint8_t i : kShip)
        if (i == item) return true;
    return false;
}

// whether the game lets Link use this item now (the HUD dims the others)
inline bool item_usable(const PlayContext& c, uint8_t item) {
    if (item == kItemNone) return false;
    const uint32_t st0 = c.player_status0, st1 = c.player_status1;
    if (st1 & kSt1PictoAim) return item == kItemPictoBox || item == kItemDeluxePictoBox;
    if (st0 & kSt0Telescope) return item == kItemTelescope;
    if (st0 & kSt0Rope) return item == kItemGrapplingHook;
    if (st0 & (kSt0Swim | kSt0Hang | kSt0Hang2 | kSt0Crawl | kSt0Ladder)) return false;
    if (climbing(c) || swimming(c)) return false;
    if (c.minigame == 1) return item == kItemSail || item == kItemSwiftSail || item == kItemTelescope;  // sailing minigame
    if (st0 & kSt0ShipRide) return ship_item(item);
    return true;
}

// A's word: the game's, except where its byte stays empty although A does something
inline uint8_t a_label(const PlayContext& c) {
    if (c.has(kWindCompass)) return kActChoose;                     // A takes the wind's direction
    if (c.zr_action == kActPictoAim && c.a_action == kActNone) return kActTakePhoto;
    if ((c.has(kChoice) || c.has(kNumberEntry) || c.has(kShop)) && c.a_action == kActNone) return kActChoose;
    if (c.has(kEvent) && c.a_action == kActNone) return kActNext;   // talks, events: A goes on
    return c.a_action == kActAlbumNothing ? kActNone : c.a_action;
}
// B's word: carrying something, B throws it too (the game says sword)
inline uint8_t b_label(const PlayContext& c) {
    if (c.has(kWindCompass)) return kActReturn;
    if (c.a_action == kActThrow && c.b_action == kActSword) return kActThrow;
    return c.b_action;
}

inline Buttons context_buttons(const PlayContext& c, const Options& o = {}) {
    Buttons r;
    auto show = [&](Button i, Kind k = Kind::kPlain, uint8_t label = 0) { r.b[i] = {true, k, label}; };
    // no Quest Log, or a game menu (the pause menu and the item screen use every button): the full pad
    if (!c.has(kInGame) || c.has(kMenu)) {
        for (auto& v : r.b) v.shown = true;
        return r;
    }
    r.active = true;
    const bool ff = o.fast_forward && c.has(kEvent);
    // a fade or a stage change without text: nothing to press
    if ((c.has(kFade) || c.has(kStageChange)) && !talking(c)) {
        if (ff) show(kZR, Kind::kFastForward);
        return r;
    }
    // a choice, a number entry, a shop's item choice: A takes it; B cancels the number entry and leaves the shop
    if (c.has(kChoice) || c.has(kNumberEntry) || c.has(kShop)) {
        show(kA, Kind::kAction, a_label(c));
        if (c.has(kNumberEntry) || c.has(kShop)) show(kB, Kind::kAction, c.b_action ? c.b_action : kActCancel);
        if (ff) show(kZR, Kind::kFastForward);
        return r;
    }
    // the wind compass after the Wind's Requiem: A takes the direction, B goes back
    if (c.has(kWindCompass)) {
        show(kA, Kind::kAction, a_label(c));
        show(kB, Kind::kAction, b_label(c));
        return r;
    }
    // a talk or a scene's text: A goes on (B does the same in the game's message box); with tap to
    // continue a tap anywhere is A, so A itself goes
    if (talking(c) || c.has(kEvent)) {
        r.tap_continues = c.has(kKeyIcon);
        if (talking(c) && !(o.tap_to_continue && r.tap_continues)) show(kA, Kind::kAction, a_label(c));
        if (ff) show(kZR, Kind::kFastForward);
        return r;
    }
    if (!c.has(kLink)) return r;  // another actor is controlled (the Seagull, a possessed Moblin): its own rules (later)
    // the Picto Box (aiming, its album): A takes the photo / chooses, B goes back, ZR switches album and finder,
    // the album pages by the d-pad; no targeting, first person or menu there
    if (picto_mode(c)) {
        const uint8_t a = a_label(c);
        if (a) show(kA, Kind::kAction, a);
        if (c.b_action && c.b_action != kActHidden) show(kB, Kind::kAction, c.b_action);
        if (c.zr_action) show(kZR, Kind::kAction, c.zr_action);
        if (album(c)) show(kDpadLeftRight);
        return r;
    }
    // the telescope: its zoom (left stick), B and its own item button put it away
    if (telescope(c)) {
        if (c.b_action && c.b_action != kActHidden) show(kB, Kind::kAction, c.b_action);
        const uint8_t items[3] = {c.x_item, c.y_item, c.r_item};
        for (int i = 0; i < 3; i++)
            if (items[i] == kItemTelescope) show(Button(kX + i), Kind::kItem, items[i]);
        return r;
    }
    // normal play
    const bool water_or_wall = swimming(c) || climbing(c);
    const uint8_t a = a_label(c);
    if (a) show(kA, Kind::kAction, a);
    const uint8_t b = b_label(c);
    if (b && b != kActHidden && !water_or_wall) show(kB, Kind::kAction, b);  // no sword in water, on a ladder
    const uint8_t items[3] = {c.x_item, c.y_item, c.r_item};
    for (int i = 0; i < 3; i++)
        if (item_usable(c, items[i])) show(Button(kX + i), Kind::kItem, items[i]);
    if (c.zr_action && c.zr_action != kActHidden) show(kZR, Kind::kAction, c.zr_action);
    show(kZL);
    if (!aiming(c) && !first_person(c)) show(kPlus);  // no menu while aiming or looking in first person
    if (!water_or_wall) show(kStickR);                 // no first person in water, on a ladder
    if (c.has(kHasWindWaker) && !water_or_wall) show(kDpadUp);  // the Wind Waker conducts
    if (in_ship(c)) show(kDpadLeftRight);
    return r;
}

// The on-screen Jump and Moon jump buttons (P6): bits as input::kVirtJump (1) and kVirtMoonJump (2),
// each only while its mod is on and Link plays (no event, talk, menu, fade or stage change), never
// in the Picto Box or the telescope. Jump anywhere doesn't jump out of water: its button also hides
// while Link swims, climbs or hangs, where it does nothing (jump_policy::ground_proc). The moon jump
// lifts him out of water, so its button stays. Idea from the GreenNaugahyde Android fork.
constexpr uint32_t kShowJump = 1, kShowMoonJump = 2;
inline uint32_t jump_buttons(const PlayContext& c, bool jump_on, bool moon_on) {
    if (!c.has(kInPlay) || !c.has(kLink) || picto_mode(c) || telescope(c)) return 0;
    uint32_t r = 0;
    if (jump_on && !swimming(c) && !climbing(c)) r |= kShowJump;
    if (moon_on) r |= kShowMoonJump;
    return r;
}

// for traces and tests: "A:02 B:35 X:i35 ZL -ZR..." (shown buttons; i = item, ff = fast forward; "all" without the rules)
inline int format(const Buttons& b, char* buf, int size) {
    static const char* const kNames[kCount] = {"A", "B", "X", "Y", "R", "ZR", "ZL", "+", "R3", "Up", "LR"};
    if (!b.active) return snprintf(buf, size, "all");
    int k = 0;
    buf[0] = 0;
    for (int i = 0; i < kCount && k < size; i++) {
        const ButtonView& v = b.b[i];
        if (!v.shown) continue;
        const char* sep = k ? " " : "";
        if (v.kind == Kind::kAction) k += snprintf(buf + k, size - k, "%s%s:%02X", sep, kNames[i], v.label);
        else if (v.kind == Kind::kItem) k += snprintf(buf + k, size - k, "%s%s:i%02X", sep, kNames[i], v.label);
        else if (v.kind == Kind::kFastForward) k += snprintf(buf + k, size - k, "%s%s:ff", sep, kNames[i]);
        else k += snprintf(buf + k, size - k, "%s%s", sep, kNames[i]);
    }
    if (b.tap_continues && k < size) k += snprintf(buf + k, size - k, "%stap", k ? " " : "");
    return k;
}

}  // namespace mods::ctxbtn
