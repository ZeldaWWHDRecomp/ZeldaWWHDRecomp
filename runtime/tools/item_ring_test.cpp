// The item ring (mods/item_ring.h): equipping and swapping on fixture save memory, the bags, the Wind
// Waker's conduct (the baton never stays on a button), the cannon / salvage arm, the empty entry, the
// X / Y / R gate (tap vs hold), the picking by stick and finger, and the pace schedule. No game files.
#include "mods/item_ring.h"

#include <cassert>
#include <cstdio>
#include <map>

using namespace mods::item_ring;
namespace pc = mods::playctx;

namespace {
// guest memory: a sparse byte map, 0 where never written
struct Mem {
    std::map<uint32_t, uint8_t> b;
    uint8_t u8(uint32_t a) const {
        auto it = b.find(a);
        return it == b.end() ? 0 : it->second;
    }
    void w8(uint32_t a, uint8_t v) { b[a] = v; }
};
constexpr uint32_t kSave = 0x20000000, kPlaySelect = 0x30000000;

// a save in the middle of the game: telescope, sail, Wind Waker, grappling hook, spoils bag (2 kinds),
// Deku Leaf, Picto Box, bait bag (empty), bow, bombs, a bottle, delivery bag (a letter), hookshot;
// bow on X, Picto Box on Y, telescope on R
Mem fixture() {
    Mem m;
    const uint8_t inv[kInventorySlots] = {0x20, 0x78, 0x22, 0x25, 0x24, 0xFF, 0x34, 0xFF, 0x23, 0xFF, 0xFF,
                                          0x2C, 0x27, 0x31, 0x50, 0xFF, 0xFF, 0xFF, 0x30, 0x2F, 0xFF};
    for (int i = 0; i < kInventorySlots; i++) m.w8(kSave + kSaveInventory + i, inv[i]);
    for (int i = 0; i < kBags * kBagSize; i++) m.w8(kSave + kSaveBags + i, 0xFF);
    m.w8(kSave + kSaveBags + 0, 0x48);       // spoils: Knight's Crest
    m.w8(kSave + kSaveBags + 1, 0x49);       //         Red Chu Jelly
    m.w8(kSave + kSaveBags + 16 + 0, 0xA5);  // delivery: a letter
    const uint8_t sel[3] = {12, 8, 0};
    for (int i = 0; i < 3; i++) {
        m.w8(kSave + kSaveSelect + i, sel[i]);
        m.w8(kPlaySelect + i, inv[sel[i]]);
    }
    return m;
}
void buttons_are(const Mem& m, uint8_t x, uint8_t y, uint8_t r) {
    const uint8_t want[3] = {x, y, r};
    for (int i = 0; i < 3; i++) {
        assert(m.u8(kSave + kSaveSelect + i) == want[i]);
        const Save s = read_save(m, kSave);
        assert(m.u8(kPlaySelect + i) == (want[i] == kNone ? kNone : item_at(s, want[i])));
    }
}
int find(const Entries& l, Kind k, uint8_t slot = kNone) {
    for (int i = 0; i < l.n; i++)
        if (l.e[i].kind == k && (slot == kNone || l.e[i].slot == slot)) return i;
    return -1;
}
// a stick vector that picks entry i of n
void towards(int i, int n, float& x, float& y) { direction(i, n, x, y); }

struct Driver {  // one pad read every 1/30 s, as the game's full logic steps
    Ring ring;
    Env env;
    double t = 0;
    Action last;
    uint32_t read(uint32_t buttons, float lx = 0, float ly = 0, bool touch = false, Finger f = {}) {
        PadIn in;
        in.buttons = buttons, in.lx = lx, in.ly = ly, in.now = t, in.touch = touch, in.finger = f;
        const uint32_t out = ring.filter(in, env, last);
        t += 1.0 / 30;
        if (ring.open() && !ring.by_touch()) assert(in.lx == 0 && in.ly == 0);  // the stick picks, Link stays
        return out;
    }
    // hold `button` with the stick at (x, y) for `seconds`
    void hold(uint32_t bit, double seconds, float x = 0, float y = 0) {
        for (double end = t + seconds; t < end - 1e-9;) {
            const uint32_t out = read(bit, x, y);
            assert(!(out & bit) || !env.eligible);
        }
    }
};
Env env_from(const Mem& m, int mode = kAll, bool on_ship = false) {
    Env e;
    e.mode = mode;
    e.eligible = true;
    e.on_ship = on_ship;
    e.save = read_save(m, kSave);
    return e;
}
}  // namespace

static void equip_and_swap() {
    Mem m = fixture();
    // the hookshot (slot 19) onto Y: the Picto Box goes off
    assert(equip(m, kSave, kPlaySelect, 1, 19));
    buttons_are(m, 12, 19, 0);
    assert(m.u8(kPlaySelect + 1) == 0x2F);
    // the bow (on X) onto R: swaps, the telescope goes to X
    assert(equip(m, kSave, kPlaySelect, 2, 12));
    buttons_are(m, 0, 19, 12);
    // empty Y
    assert(equip(m, kSave, kPlaySelect, 1, kNone));
    buttons_are(m, 0, kNone, 12);
    // a slot without an item, a bad button: nothing written
    const Mem before = m;
    assert(!equip(m, kSave, kPlaySelect, 0, 5));
    assert(!equip(m, kSave, kPlaySelect, 3, 12));
    assert(m.b == before.b);
    // a bag's item (spoils slot 25: Red Chu Jelly) onto X
    assert(equip(m, kSave, kPlaySelect, 0, 25));
    assert(m.u8(kPlaySelect + 0) == 0x49);
    buttons_are(m, 25, kNone, 12);
    std::puts("equip and swap ok");
}

static void entries_and_bags() {
    const Mem m = fixture();
    const Save s = read_save(m, kSave);
    Entries l = entries(s, 0, -1, false);
    // 14 items owned: telescope, sail, baton, hook, spoils, leaf, picto, bait, bow, bombs, bottle, delivery, hookshot = 13 + empty
    assert(l.n == 14);
    assert(l.e[0].kind == kEntryItem && l.e[0].item == 0x20);
    assert(find(l, kEntryConduct) >= 0 && l.e[find(l, kEntryConduct)].item == pc::kItemWindWaker);
    assert(find(l, kEntryBag, 4) >= 0 && find(l, kEntryBag, 11) >= 0 && find(l, kEntryBag, 18) >= 0);
    assert(find(l, kEntryEmpty) == l.n - 1);  // X has the bow
    assert(find(l, kEntryCannon) < 0 && find(l, kEntrySalvage) < 0);
    // on the boat: the cannon (bombs owned) and the salvage arm (grappling hook owned)
    l = entries(s, 0, -1, true);
    assert(find(l, kEntryCannon) >= 0 && find(l, kEntrySalvage) >= 0 && l.n == 16);
    // a bag's ring: its contents and Back
    l = entries(s, 0, 0, false);
    assert(l.n == 3 && l.e[0].slot == 24 && l.e[0].item == 0x48 && l.e[1].slot == 25 && l.e[2].kind == kEntryBack);
    l = entries(s, 0, 2, false);
    assert(l.n == 2 && l.e[0].slot == 48 && l.e[0].item == 0xA5);
    // a button without an item has no empty entry
    Save t = s;
    t.select[1] = kNone;
    assert(find(entries(t, 1, -1, false), kEntryEmpty) < 0);
    std::puts("entries ok");
}

static void pick_geometry() {
    assert(pick(0, 0, 0.5f, 8) == -1);
    assert(pick(0, 0.4f, 0.5f, 8) == -1);
    assert(pick(0, 1, 0.5f, 8) == 0);     // up: the first
    assert(pick(1, 0, 0.5f, 8) == 2);     // right: a quarter round
    assert(pick(0, -1, 0.5f, 8) == 4);    // down
    assert(pick(-1, 0, 0.5f, 8) == 6);    // left
    assert(pick(-0.2f, 1, 0.5f, 8) == 0); // just left of up: still the first (wraps)
    for (int n : {3, 7, 14, 16})
        for (int i = 0; i < n; i++) {
            float x, y;
            direction(i, n, x, y);
            assert(pick(x, y, 0.5f, n) == i);
        }
    std::puts("pick ok");
}

static void tap_and_hold() {
    const Mem m = fixture();
    Driver d;
    d.env = env_from(m);
    // a tap shorter than the hold time: held back while down, then a short press of kTapReads reads
    assert(!(d.read(kButtonBits[0]) & kButtonBits[0]));
    assert(!(d.read(kButtonBits[0]) & kButtonBits[0]));
    assert(d.ring.gated(0));
    assert(d.read(0) & kButtonBits[0]);   // let go: the press reaches the game now
    assert(d.read(0) & kButtonBits[0]);
    assert(!(d.read(0) & kButtonBits[0]));
    assert(!d.ring.open());
    // held: the ring opens after kHoldSeconds; nothing reaches the game
    d.hold(kButtonBits[1], kHoldSeconds + 0.05);
    assert(d.ring.open() && d.ring.button() == 1 && d.ring.bag() < 0);
    // let go without picking: closed, nothing done, no press
    assert(!(d.read(0) & kButtonBits[1]));
    assert(!d.ring.open() && d.last.kind == Action::kNoAction);
    assert(!(d.read(0) & kButtonBits[1]));
    // not eligible (Link aims): the press goes through at once
    d.env.eligible = false;
    assert(d.read(kButtonBits[0]) & kButtonBits[0]);
    assert(d.read(kButtonBits[0]) & kButtonBits[0]);
    d.read(0);
    d.env.eligible = true;
    // the ring off for controllers (touch only) and a controller press: through at once
    d.env.mode = kTouchOnly;
    assert(d.read(kButtonBits[2]) & kButtonBits[2]);
    d.read(0);
    // ... a touch press is held back
    assert(!(d.read(kButtonBits[2], 0, 0, true) & kButtonBits[2]));
    d.read(0, 0, 0, true);
    d.read(0, 0, 0, true);
    d.env.mode = kOff;
    assert(d.read(kButtonBits[2], 0, 0, true) & kButtonBits[2]);
    d.read(0);
    std::puts("tap and hold ok");
}

static void equip_through_the_ring() {
    Mem m = fixture();
    Driver d;
    d.env = env_from(m);
    // hold R, tilt the stick to the hookshot, let go: the action puts slot 19 on R
    const Entries l = entries(d.env.save, 2, -1, false);
    const int hookshot = find(l, kEntryItem, 19);
    float x, y;
    towards(hookshot, l.n, x, y);
    d.hold(kButtonBits[2], kHoldSeconds + 0.1, x, y);
    assert(d.ring.open() && d.ring.sel() == hookshot);
    d.read(0, x, y);
    assert(d.last.kind == Action::kEquip && d.last.button == 2 && d.last.slot == 19);
    assert(equip(m, kSave, kPlaySelect, d.last.button, d.last.slot));
    buttons_are(m, 12, 8, 19);
    // the item already on the button: nothing to do
    d.env = env_from(m);
    d.hold(kButtonBits[2], kHoldSeconds + 0.1, x, y);
    d.read(0, x, y);
    assert(d.last.kind == Action::kNoAction);
    // the empty entry
    const Entries l2 = entries(d.env.save, 0, -1, false);
    towards(find(l2, kEntryEmpty), l2.n, x, y);
    d.hold(kButtonBits[0], kHoldSeconds + 0.1, x, y);
    d.read(0, x, y);
    assert(d.last.kind == Action::kEquip && d.last.button == 0 && d.last.slot == kNone);
    std::puts("equip through the ring ok");
}

static void bag_sub_ring() {
    Mem m = fixture();
    Driver d;
    d.env = env_from(m);
    const Entries l = entries(d.env.save, 1, -1, false);
    const int spoils = find(l, kEntryBag, 4), bait = find(l, kEntryBag, 11);
    float x, y;
    // the bait bag is empty: resting on it opens nothing
    towards(bait, l.n, x, y);
    d.hold(kButtonBits[1], kHoldSeconds + kDwellSeconds + 0.2, x, y);
    assert(d.ring.open() && d.ring.bag() < 0);
    d.read(0, x, y);
    assert(d.last.kind == Action::kNoAction);  // a bag goes on no button
    // the spoils bag: rest on it, it opens; pick the Red Chu Jelly (slot 25)
    towards(spoils, l.n, x, y);
    d.hold(kButtonBits[1], kHoldSeconds + 0.05, x, y);
    d.hold(kButtonBits[1], kDwellSeconds + 0.05, x, y);
    assert(d.ring.bag() == 0 && d.ring.list().n == 3);
    float bx, by;
    towards(1, 3, bx, by);
    d.hold(kButtonBits[1], 0.1, bx, by);
    assert(d.ring.sel() == 1);
    d.read(0, bx, by);
    assert(d.last.kind == Action::kEquip && d.last.button == 1 && d.last.slot == 25);
    assert(equip(m, kSave, kPlaySelect, 1, 25));
    assert(m.u8(kPlaySelect + 1) == 0x49);
    // Back: resting on it returns to the items
    d.env = env_from(m);
    d.hold(kButtonBits[1], kHoldSeconds + 0.05, x, y);
    d.hold(kButtonBits[1], kDwellSeconds + 0.05, x, y);
    assert(d.ring.bag() == 0);
    towards(2, 3, bx, by);
    d.hold(kButtonBits[1], kDwellSeconds + 0.1, bx, by);
    assert(d.ring.open() && d.ring.bag() < 0);
    d.read(0);
    std::puts("bags ok");
}

static void conduct_and_boat() {
    Mem m = fixture();
    Driver d;
    d.env = env_from(m, kAll, true);
    const Mem before = m;
    const Entries l = entries(d.env.save, 0, -1, true);
    float x, y;
    // the Wind Waker: d-pad up for kTapReads reads after the ring closes; the buttons keep their items
    // (the baton never lands on X / Y / R, so nothing has to take it off again)
    towards(find(l, kEntryConduct), l.n, x, y);
    d.hold(kButtonBits[0], kHoldSeconds + 0.1, x, y);
    uint32_t out = d.read(0, x, y);
    assert(d.last.kind == Action::kPress && d.last.dpad == kDpadUp);
    assert(out & kDpadUp);
    assert(!(out & kButtonBits[0]));
    assert(d.read(0) & kDpadUp);
    assert(!(d.read(0) & kDpadUp));
    assert(m.b == before.b);
    // the cannon and the salvage arm: d-pad left / right
    towards(find(l, kEntryCannon), l.n, x, y);
    d.hold(kButtonBits[1], kHoldSeconds + 0.1, x, y);
    assert(d.read(0, x, y) & kDpadLeft);
    d.read(0);
    d.read(0);
    towards(find(l, kEntrySalvage), l.n, x, y);
    d.hold(kButtonBits[2], kHoldSeconds + 0.1, x, y);
    assert(d.read(0, x, y) & kDpadRight);
    std::puts("conduct and boat ok");
}

static void cancel_and_touch() {
    const Mem m = fixture();
    Driver d;
    d.env = env_from(m);
    const Entries l = entries(d.env.save, 0, -1, false);
    float x, y;
    towards(3, l.n, x, y);
    // an event starts while it is open: closed, nothing done, no press on letting go
    d.hold(kButtonBits[0], kHoldSeconds + 0.1, x, y);
    assert(d.ring.open());
    d.env.eligible = false;
    assert(!(d.read(kButtonBits[0], x, y) & kButtonBits[0]));
    assert(!d.ring.open());
    d.env.eligible = true;
    assert(!(d.read(0) & kButtonBits[0]) && d.last.kind == Action::kNoAction);
    assert(!(d.read(0) & kButtonBits[0]));
    // touch: the finger's drag picks (in button radii, +y down); the stick still walks
    Finger f;
    f.down = true, f.button = 0;
    f.dx = x * 2, f.dy = -y * 2;
    d.env.mode = kTouchOnly;
    for (int i = 0; i < 8; i++) {
        PadIn in;
        in.buttons = kButtonBits[0], in.lx = 0.7f, in.ly = 0.1f, in.now = d.t, in.touch = true, in.finger = f;
        d.ring.filter(in, d.env, d.last);
        d.t += 1.0 / 30;
        assert(in.lx == 0.7f);
    }
    assert(d.ring.open() && d.ring.by_touch() && d.ring.sel() == 3);
    f.dx = f.dy = 0.1f;  // back to the middle: nothing picked
    PadIn in;
    in.buttons = kButtonBits[0], in.now = d.t, in.touch = true, in.finger = f;
    d.ring.filter(in, d.env, d.last);
    assert(d.ring.sel() == -1);
    std::puts("cancel and touch ok");
}

static void others_while_open() {
    const Mem m = fixture();
    Driver d;
    d.env = env_from(m);
    d.hold(kButtonBits[0], kHoldSeconds + 0.1);
    assert(d.ring.open());
    // Y pressed while X's ring is open: it does nothing (no item use mid-pick), also after the ring
    assert(!(d.read(kButtonBits[0] | kButtonBits[1]) & kButtonBits[1]));
    assert(!(d.read(kButtonBits[0] | kButtonBits[1]) & kButtonBits[1]));
    assert(!(d.read(kButtonBits[1]) & kButtonBits[1]));  // X let go: closed; Y still held, still swallowed
    assert(!(d.read(0) & kButtonBits[1]));
    // A, B and the rest pass
    d.hold(kButtonBits[0], kHoldSeconds + 0.1);
    assert(d.read(kButtonBits[0] | 0x8000) & 0x8000);
    d.read(0);
    std::puts("other buttons ok");
}

static void eligibility() {
    using namespace pc;
    PlayContext c;
    c.flags = kInGame | kLink | kInPlay;
    assert(eligible(c));
    PlayContext t = c;
    t.flags &= ~kInPlay;
    assert(!eligible(t));
    t = c;
    t.player_status0 = kSt0BowAim;
    assert(!eligible(t));
    t = c;
    t.player_status0 = kSt0Telescope;
    assert(!eligible(t));
    t = c;
    t.player_status1 = kSt1Conduct;
    assert(!eligible(t));
    t = c;
    t.player_status1 = kSt1LeafFly;
    assert(!eligible(t));
    t = c;
    t.player_status1 = kSt1PictoAim;
    assert(!eligible(t));
    t = c;
    t.player_status0 = kSt0ShipRide;  // on the boat, sail down: it opens (cannon, salvage arm)
    assert(eligible(t));
    t.player_status1 = kSt1Sail;
    assert(!eligible(t));
    t = c;
    t.item_in_hand = 0x31;  // a bomb only held
    assert(eligible(t));
    std::puts("eligibility ok");
}

static void pace() {
    for (uint64_t s = 0; s < 12; s++) {
        assert(pause_value(kPaceNormal, s) == 0);
        assert(pause_value(kPacePaused, s) == 1);
        assert(pause_value(kPaceSlow, s) == (s % 3 == 2 ? 0 : 1));
    }
    // the game's timer (calcPauseTimer): a new value holds this step, else it counts down; slow motion
    // runs one step in three, paused none, and a step after the ring closes runs again
    int timer = 0, ran = 0;
    for (uint64_t s = 0; s < 30; s++) {
        const int next = pause_value(kPaceSlow, s);
        bool paused;
        if (next) timer = next, paused = true;
        else paused = (timer = timer > 0 ? timer - 1 : 0) != 0;
        ran += !paused;
    }
    assert(ran == 10);
    std::puts("pace ok");
}

static void names() {
    assert(!strcmp(button_name(0, kNamesGame, true), "X"));
    assert(!strcmp(button_name(2, kNamesA123, false), "A3"));
    assert(!strcmp(button_name(1, kNamesAuto, true), "A2"));
    assert(!strcmp(button_name(1, kNamesAuto, false), "Y"));
    assert(!strcmp(item_name(0x27), "Hero's Bow"));
    std::puts("names ok");
}

int main() {
    equip_and_swap();
    entries_and_bags();
    pick_geometry();
    tap_and_hold();
    equip_through_the_ring();
    bag_sub_ring();
    conduct_and_boat();
    cancel_and_touch();
    others_while_open();
    eligibility();
    pace();
    names();
    std::puts("item_ring_test: all ok");
    return 0;
}
