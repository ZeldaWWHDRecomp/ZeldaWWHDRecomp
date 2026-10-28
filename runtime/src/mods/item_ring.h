// SPDX-License-Identifier: MPL-2.0
// The item ring (Controls > Item ring; P4 of the controls plan). Hold X, Y or R and a ring of the items
// Link owns opens; the left stick (a controller) or the finger's drag (touch) picks one, letting go of
// the button puts it on that button, as the game's item screen does (an item already on another button
// swaps with the old one). A bag opens as a ring of its contents (rest on it); the Wind Waker conducts
// at once (the game's own d-pad up) and takes no button; on the boat the cannon and the salvage arm
// press the d-pad's left / right; the empty entry leaves the button empty. A short tap of the button
// still reaches the game, as a short press when it is let go. While the ring is open the game runs
// normally, in slow motion or is paused (the game's own pause timer, as its hit stop uses it).
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0), its
// ItemRing.java and touch_state.cpp equip / conduct; reimplemented: the rules are here, in C++, the ring
// is drawn by the host HUD (item_ring.cpp) for controllers and touch alike, and Java only routes fingers.
//
// This header is pure (no game, no host): item_ring_test runs it on fixture save memory.
//
// The save data (dSv_player_c, *(save info pointer) + 0x20; play_context.h Addresses, cheats.cpp):
//   +0x09 mSelectItem[3]: the X / Y / R items as slot numbers (inventory 0..20, the bags' contents
//         24..31 spoils, 36..43 bait, 48..55 delivery; 0xFF none)
//   +0x3C the inventory u8[21] (item numbers; 0xFF empty), +0x76 the bags u8[8] x 3 (spoils, bait,
//         delivery: dSv_player_bag_item_c)
// The play's copy of the buttons' item numbers, mSelectItem (gameInfo + 0x5BBB, play_context.h
// kSelectItem), is what Link's item use and the HUD read; the game writes it from the save
// (dComIfGp_setSelectItem) when its item screen sets a button, and so does equip() below.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

#include "play_context.h"

namespace mods::item_ring {

// ---- save layout ----
constexpr uint32_t kSaveSelect = 0x09, kSaveInventory = 0x3C, kSaveBags = 0x76;
constexpr int kInventorySlots = 21, kBagSize = 8, kBags = 3, kButtons = 3;
constexpr uint8_t kNone = 0xFF;
constexpr uint8_t kBagInventorySlot[kBags] = {4, 11, 18};  // the spoils, bait and delivery bag in the inventory
constexpr uint8_t kBagFirstSlot[kBags] = {24, 36, 48};      // their contents' slot numbers on a button
// item numbers (dItemNo)
constexpr uint8_t kItemBombs = 0x31;  // (the Wind Waker, the grappling hook: play_context.h)
// the item buttons (VPAD bits, as input.h): X, Y, R
constexpr uint32_t kButtonBits[kButtons] = {0x2000, 0x1000, 0x0010};
constexpr uint32_t kDpadUp = 0x0200, kDpadLeft = 0x0800, kDpadRight = 0x0400;

// The bag (0 spoils, 1 bait, 2 delivery) a button slot number belongs to, -1 none
inline int bag_of_slot(uint8_t slot) {
    for (int b = 0; b < kBags; b++)
        if (slot >= kBagFirstSlot[b] && slot < kBagFirstSlot[b] + kBagSize) return b;
    return -1;
}
// The bag whose inventory slot this is, -1 none
inline int bag_of_inventory_slot(uint8_t slot) {
    for (int b = 0; b < kBags; b++)
        if (slot == kBagInventorySlot[b]) return b;
    return -1;
}

// What the ring needs from the save, read once when it opens
struct Save {
    uint8_t inventory[kInventorySlots];
    uint8_t bags[kBags * kBagSize];
    uint8_t select[kButtons];
    Save() {
        memset(inventory, kNone, sizeof inventory);
        memset(bags, kNone, sizeof bags);
        memset(select, kNone, sizeof select);
    }
};
// the item in a button slot (dComIfGs_getItem: an inventory slot or a bag's), kNone for none
inline uint8_t item_at(const Save& s, uint8_t slot) {
    if (slot < kInventorySlots) return s.inventory[slot];
    const int b = bag_of_slot(slot);
    return b >= 0 ? s.bags[b * kBagSize + (slot - kBagFirstSlot[b])] : kNone;
}
inline bool owns(const Save& s, uint8_t item) {
    for (uint8_t v : s.inventory)
        if (v == item) return true;
    return false;
}
inline bool bag_empty(const Save& s, int bag) {
    for (int k = 0; k < kBagSize; k++)
        if (s.bags[bag * kBagSize + k] != kNone) return false;
    return true;
}
// Mem: u8(addr) (big-endian guest bytes)
template <class Mem>
Save read_save(const Mem& m, uint32_t s) {
    Save out;
    for (int i = 0; i < kInventorySlots; i++) out.inventory[i] = m.u8(s + kSaveInventory + i);
    for (int i = 0; i < kBags * kBagSize; i++) out.bags[i] = m.u8(s + kSaveBags + i);
    for (int i = 0; i < kButtons; i++) out.select[i] = m.u8(s + kSaveSelect + i);
    return out;
}

// ---- equipping, as the game's item screen does it ----
// Puts button slot `slot` (kNone: nothing) on `button` (0 X, 1 Y, 2 R): the save's select item, and the
// play's copy of the item number for each button that changed. An item already on another button gets
// the old item of this one (a swap). False (nothing written) for a bad button or a slot without an item.
// MemW: u8(addr) and w8(addr, value)
template <class MemW>
bool equip(MemW& m, uint32_t save, uint32_t play_select, int button, uint8_t slot) {
    if (button < 0 || button >= kButtons) return false;
    const Save s = read_save(m, save);
    if (slot != kNone && item_at(s, slot) == kNone) return false;
    const uint8_t old = s.select[button];
    uint8_t now[kButtons];
    for (int i = 0; i < kButtons; i++) now[i] = s.select[i];
    for (int i = 0; i < kButtons; i++)
        if (i != button && slot != kNone && now[i] == slot) now[i] = old;  // swap
    now[button] = slot;
    for (int i = 0; i < kButtons; i++) {
        if (now[i] == s.select[i]) continue;
        m.w8(save + kSaveSelect + i, now[i]);
        m.w8(play_select + i, now[i] == kNone ? kNone : item_at(s, now[i]));
    }
    return true;
}

// ---- the ring's entries ----
enum Kind : uint8_t {
    kEntryItem,     // an item: goes on the button
    kEntryBag,      // a bag: rest on it to open its contents
    kEntryConduct,  // the Wind Waker: conducts (d-pad up), no button
    kEntryCannon,   // on the boat: the cannon (d-pad left; needs the bombs)
    kEntrySalvage,  // on the boat: the salvage arm (d-pad right; needs the grappling hook)
    kEntryEmpty,    // leaves the button empty (while it has an item)
    kEntryBack,     // in a bag: back to the items (rest on it)
};
struct Entry {
    Kind kind = kEntryItem;
    uint8_t slot = kNone;  // the button slot number (items), the inventory slot (bags, the Wind Waker)
    uint8_t item = kNone;  // the item number, kNone for the d-pad entries, empty and back
};
constexpr int kMaxEntries = 32;
struct Entries {
    Entry e[kMaxEntries];
    int n = 0;
    void add(Kind k, uint8_t slot, uint8_t item) {
        if (n < kMaxEntries) e[n++] = Entry{k, slot, item};
    }
};
// The entries for `button`: the inventory in its order (bag < 0), or bag `bag`'s contents
inline Entries entries(const Save& s, int button, int bag, bool on_ship) {
    Entries out;
    if (bag >= 0 && bag < kBags) {
        for (int k = 0; k < kBagSize; k++) {
            const uint8_t item = s.bags[bag * kBagSize + k];
            if (item != kNone) out.add(kEntryItem, uint8_t(kBagFirstSlot[bag] + k), item);
        }
        out.add(kEntryBack, kNone, kNone);
        return out;
    }
    for (int k = 0; k < kInventorySlots; k++) {
        const uint8_t item = s.inventory[k];
        if (item == kNone) continue;
        if (item == playctx::kItemWindWaker) out.add(kEntryConduct, uint8_t(k), item);
        else if (bag_of_inventory_slot(uint8_t(k)) >= 0) out.add(kEntryBag, uint8_t(k), item);
        else out.add(kEntryItem, uint8_t(k), item);
    }
    if (on_ship && owns(s, kItemBombs)) out.add(kEntryCannon, kNone, kNone);
    if (on_ship && owns(s, playctx::kItemGrapplingHook)) out.add(kEntrySalvage, kNone, kNone);
    if (button >= 0 && button < kButtons && s.select[button] != kNone) out.add(kEntryEmpty, kNone, kNone);
    return out;
}

// The entry in direction (vx, vy) (+y up), -1 within the dead zone: entry 0 at the top, clockwise
inline int pick(float vx, float vy, float dead, int n) {
    if (n <= 0 || std::hypot(vx, vy) < dead) return -1;
    double a = std::atan2(double(vx), double(vy));  // 0 up, clockwise
    if (a < 0) a += 2 * M_PI;
    return int(std::lround(a / (2 * M_PI / n))) % n;
}
// where entry i of n sits: unit vector (+y up)
inline void direction(int i, int n, float& x, float& y) {
    const double a = n > 0 ? 2 * M_PI * i / n : 0;
    x = float(std::sin(a));
    y = float(std::cos(a));
}

// ---- when X / Y / R may open the ring ----
// In play (no event, talk, menu, fade, stage change), Link not aiming or using an item: bow, hookshot,
// grappling hook and boomerang aim, first person, the telescope, the Picto Box, conducting, the Deku
// Leaf, the sail. Then a press of the button goes to the game at once, so the press that shoots isn't
// held back. (An item only held, as a bomb, doesn't stop it.)
inline bool eligible(const playctx::PlayContext& c) {
    using namespace playctx;
    if (!c.has(kInPlay) || !c.has(kLink)) return false;
    if (c.has(kMessage) || c.has(kPictoFinder) || c.has(kAlbum) || c.has(kWindCompass) || c.has(kZoomBar)) return false;
    if (aiming(c) || first_person(c) || telescope(c) || picto_mode(c) || conducting(c) || leaf_flying(c)) return false;
    if (c.player_status1 & (kSt1LeafFan | kSt1Sail)) return false;
    return true;
}

// ---- the button gate and the ring's state (once per full pad read, on the game thread) ----
enum Mode : int { kOff = 0, kTouchOnly = 1, kAll = 2 };  // Controls > Item ring (default kTouchOnly)
enum Pace : int { kPaceNormal = 0, kPaceSlow = 1, kPacePaused = 2 };  // while it is open (default slow)
constexpr double kHoldSeconds = 0.2;   // a press held this long opens the ring (decision 3: ~0.2 s)
constexpr double kDwellSeconds = 0.35; // resting on a bag (or Back) this long opens it (goes back)
constexpr int kTapReads = 2;           // a short press reaches the game for this many pad reads
constexpr float kStickDead = 0.5f;     // the left stick's dead zone for picking
constexpr float kFingerDead = 0.6f;    // the finger's, in the button's radii

struct Finger {        // touch: the finger on an item button (Java, TouchControls)
    bool down = false;
    int button = -1;   // 0 X, 1 Y, 2 R
    float dx = 0, dy = 0;  // from where it landed, in the button's radii (+y down, as the screen)
};
struct PadIn {
    uint32_t buttons = 0;  // VPAD bits after the play rules
    float lx = 0, ly = 0;  // left stick, +y up
    double now = 0;        // seconds (any steady clock)
    bool touch = false;    // the player plays by touch (the on-screen controls)
    Finger finger;
};
struct Env {
    int mode = kTouchOnly;
    bool eligible = false;  // eligible(PlayContext) and the host's own conditions
    bool on_ship = false;
    Save save;              // the save now (read when the ring may open)
};
struct Action {
    enum Kind { kNoAction, kEquip, kPress } kind = kNoAction;
    int button = -1;
    uint8_t slot = kNone;    // kEquip: the slot (kNone: empty the button)
    uint32_t dpad = 0;       // kPress: the d-pad bit
};

class Ring {
public:
    // One pad read: the buttons for the game (X / Y / R held back or replayed as a short press, the
    // d-pad press of a picked entry added); `in.lx / ly` zeroed while a controller picks. `act`: what
    // the read decided (applied at the frame boundary by the caller).
    uint32_t filter(PadIn& in, const Env& env, Action& act) {
        act = Action{};
        uint32_t out = in.buttons;
        const bool on_for_input = env.mode == kAll || (env.mode == kTouchOnly && in.touch);
        if (open_ >= 0 && !env.eligible) cancel_open();  // an event, a hit, a menu...: closed, nothing done
        for (int i = 0; i < kButtons; i++) {
            const uint32_t bit = kButtonBits[i];
            const bool down = (in.buttons & bit) != 0;
            if (down && !prev_[i]) {
                if (open_ >= 0) swallow_[i] = true;  // other item buttons do nothing while the ring is open
                else if (on_for_input && env.eligible) {
                    gated_[i] = true;
                    down_at_[i] = in.now;
                }
            }
            if (gated_[i] && down && open_ < 0 && in.now - down_at_[i] >= kHoldSeconds - 1e-9) {
                Entries list = entries(env.save, i, -1, env.on_ship);
                if (env.eligible && list.n > 0) {
                    open_now(i, env, in.touch && in.finger.down && in.finger.button == i, in.now);
                } else {
                    gated_[i] = false;  // it can't open now: the press goes on as a held press
                }
            }
            if (!down && prev_[i]) {
                if (gated_[i]) {
                    gated_[i] = false;
                    if (open_ == i) act = confirm();
                    else if (!cancelled_[i]) tap_left_[i] = kTapReads;  // a short press, now
                }
                swallow_[i] = false;
                cancelled_[i] = false;
            }
            prev_[i] = down;
            if (gated_[i] || swallow_[i]) out &= ~bit;
            if (tap_left_[i] > 0) {
                out |= bit;
                tap_left_[i]--;
            }
        }
        if (open_ >= 0) {
            float vx = 0, vy = 0, dead = kStickDead;
            if (by_touch_) {
                if (in.finger.down && in.finger.button == open_) vx = in.finger.dx, vy = -in.finger.dy, dead = kFingerDead;
            } else {
                vx = in.lx, vy = in.ly;
                in.lx = in.ly = 0;  // the stick picks; Link doesn't walk meanwhile
            }
            set_sel(pick(vx, vy, dead, list_.n), in.now);
            dwell(env, in.now);
        }
        if (press_left_ > 0) {
            out |= press_bit_;
            press_left_--;
        }
        return out;
    }

    // the ring
    bool open() const { return open_ >= 0; }
    int button() const { return open_; }
    int bag() const { return bag_; }
    int sel() const { return sel_; }
    bool by_touch() const { return by_touch_; }
    const Entries& list() const { return list_; }
    const Save& save() const { return save_; }
    double opened_at() const { return opened_at_; }
    // a button whose press is held back now (tests)
    bool gated(int i) const { return gated_[i]; }
    // everything forgotten: a state load, the overlay took the controller, the app went to the background
    void reset() { *this = Ring{}; }

private:
    void open_now(int i, const Env& env, bool touch, double now) {
        open_ = i;
        by_touch_ = touch;
        save_ = env.save;
        on_ship_ = env.on_ship;
        opened_at_ = now;
        fill(-1, now);
    }
    void fill(int bag, double now) {
        bag_ = bag;
        list_ = entries(save_, open_, bag, on_ship_);
        sel_ = -1;
        sel_since_ = now;
        dwelt_ = false;
    }
    void set_sel(int s, double now) {
        if (s == sel_) return;
        sel_ = s;
        sel_since_ = now;
        dwelt_ = false;
    }
    void dwell(const Env&, double now) {
        if (sel_ < 0 || dwelt_ || now - sel_since_ < kDwellSeconds - 1e-9) return;
        const Entry& e = list_.e[sel_];
        if (e.kind == kEntryBag) {
            const int b = bag_of_inventory_slot(e.slot);
            if (b >= 0 && !bag_empty(save_, b)) fill(b, now);
            else dwelt_ = true;
        } else if (e.kind == kEntryBack) {
            fill(-1, now);
        } else {
            dwelt_ = true;
        }
    }
    Action confirm() {
        Action a;
        const int b = open_;
        if (sel_ >= 0 && sel_ < list_.n) {
            const Entry& e = list_.e[sel_];
            switch (e.kind) {
                case kEntryItem:
                    if (save_.select[b] != e.slot) a = Action{Action::kEquip, b, e.slot, 0};
                    break;
                case kEntryEmpty: a = Action{Action::kEquip, b, kNone, 0}; break;
                case kEntryConduct: a = Action{Action::kPress, b, kNone, kDpadUp}; break;
                case kEntryCannon: a = Action{Action::kPress, b, kNone, kDpadLeft}; break;
                case kEntrySalvage: a = Action{Action::kPress, b, kNone, kDpadRight}; break;
                default: break;  // a bag itself, Back: nothing
            }
        }
        if (a.kind == Action::kPress) {
            press_bit_ = a.dpad;
            press_left_ = kTapReads;
        }
        close();
        return a;
    }
    void cancel_open() {
        gated_[open_] = false;  // still held: swallowed until let go, no short press then
        swallow_[open_] = true;
        cancelled_[open_] = true;
        close();
    }
    void close() {
        open_ = -1;
        bag_ = -1;
        sel_ = -1;
        list_ = Entries{};
    }

    bool prev_[kButtons] = {}, gated_[kButtons] = {}, swallow_[kButtons] = {}, cancelled_[kButtons] = {};
    double down_at_[kButtons] = {};
    int tap_left_[kButtons] = {};
    int open_ = -1, bag_ = -1, sel_ = -1;
    bool by_touch_ = false, on_ship_ = false, dwelt_ = false;
    double opened_at_ = 0, sel_since_ = 0;
    Save save_;
    Entries list_;
    uint32_t press_bit_ = 0;
    int press_left_ = 0;
};

// ---- pace: the game's pause timer while the ring is open ----
// dScnPly_ply_c::nextPauseTimer (s8, USA 101EACB7): dScnPly_Execute (025B0314) moves it into pauseTimer
// and stops the scene's step while that is not 0 (calcPauseTimer 025B02E8: a new value counts this step,
// else the timer counts down); every actor's execute skips while it runs (fopAc_Execute 025D475C:
// dScnPly_isPause), the camera too: the world holds still, the frame is still drawn, the pad still read.
// The value to write at a pass of full logic step `step` (counted from the ring's opening), or 0 to
// leave the timer alone: 1 holds this step, and without a new write the next step runs. Slow motion
// runs one step in three.
inline int pause_value(int pace, uint64_t steps_open) {
    if (pace == kPacePaused) return 1;
    if (pace == kPaceSlow) return steps_open % 3 == 2 ? 0 : 1;
    return 0;
}

// ---- names ----
// The item buttons' names (Controls > Item button names): 0 X / Y / R as the game, 1 A1 / A2 / A3, 2 Auto
// (A1..A3 while playing by touch, else X / Y / R)
enum Names : int { kNamesGame = 0, kNamesA123 = 1, kNamesAuto = 2 };
inline const char* button_name(int button, int names, bool touch) {
    static const char* const game[kButtons] = {"X", "Y", "R"};
    static const char* const a123[kButtons] = {"A1", "A2", "A3"};
    if (button < 0 || button >= kButtons) return "?";
    const bool a = names == kNamesA123 || (names == kNamesAuto && touch);
    return a ? a123[button] : game[button];
}

// An item's short English name (the ring's label while it has no picture)
inline const char* item_name(uint8_t item) {
    switch (item) {
        case 0x20: return "Telescope";
        case 0x21: return "Tingle Bottle";
        case 0x22: return "Wind Waker";
        case 0x23: return "Picto Box";
        case 0x24: return "Spoils Bag";
        case 0x25: return "Grappling Hook";
        case 0x26: return "Deluxe Picto Box";
        case 0x27: return "Hero's Bow";
        case 0x28: return "Power Bracelets";
        case 0x29: return "Iron Boots";
        case 0x2A: return "Magic Armor";
        case 0x2C: return "Bait Bag";
        case 0x2D: return "Boomerang";
        case 0x2F: return "Hookshot";
        case 0x30: return "Delivery Bag";
        case 0x31: return "Bombs";
        case 0x33: return "Skull Hammer";
        case 0x34: return "Deku Leaf";
        case 0x35: return "Bow (Fire & Ice)";
        case 0x36: return "Bow (Light)";
        case 0x45: return "Skull Necklace";
        case 0x46: return "Boko Baba Seed";
        case 0x47: return "Golden Feather";
        case 0x48: return "Knight's Crest";
        case 0x49: return "Red Chu Jelly";
        case 0x4A: return "Green Chu Jelly";
        case 0x4B: return "Blue Chu Jelly";
        case 0x1F: return "Joy Pendant";
        case 0x50: return "Empty Bottle";
        case 0x51: return "Red Potion";
        case 0x52: return "Green Potion";
        case 0x53: return "Blue Potion";
        case 0x54: return "Elixir Soup (half)";
        case 0x55: return "Elixir Soup";
        case 0x56: return "Water";
        case 0x57: return "Fairy";
        case 0x58: return "Forest Firefly";
        case 0x59: return "Forest Water";
        case 0x77: return "Swift Sail";
        case 0x78: return "Sail";
        case 0x82: return "All-Purpose Bait";
        case 0x83: return "Hyoi Pear";
        default:
            if (item >= 0x8C && item <= 0xAF) return "Delivery";
            return "Item";
    }
}
inline const char* entry_name(const Entry& e) {
    switch (e.kind) {
        case kEntryCannon: return "Cannon";
        case kEntrySalvage: return "Salvage Arm";
        case kEntryEmpty: return "Empty";
        case kEntryBack: return "Back";
        default: return item_name(e.item);
    }
}

}  // namespace mods::item_ring
