// Context buttons (mods/context_buttons.h): which buttons show, with which word or item, in each
// situation the touch controls meet. No game files.
#include "mods/context_buttons.h"

#include <cassert>
#include <cstring>
#include <initializer_list>

using namespace mods::playctx;
using namespace mods::ctxbtn;

namespace {
// Link on foot at Outset, nothing in reach: the sword on B, crouch on ZR, bow / Picto Box / telescope on X / Y / R
PlayContext walking() {
    PlayContext c;
    c.flags = kInGame | kLink | kInPlay | kHasWindWaker;
    c.b_action = kActSword;
    c.zr_action = kActCrouch;
    c.x_item = kItemBow;
    c.y_item = kItemPictoBox;
    c.r_item = kItemTelescope;
    c.item_in_hand = kHandNone;
    c.proc = 0x04;
    return c;
}
// exactly these buttons show
void only(const Buttons& b, std::initializer_list<Button> shown) {
    for (int i = 0; i < kCount; i++) {
        bool want = false;
        for (Button s : shown) want |= s == i;
        assert(b.b[i].shown == want);
    }
}
void action(const Buttons& b, Button i, uint8_t code) { assert(b[i].shown && b[i].kind == Kind::kAction && b[i].label == code); }
void item(const Buttons& b, Button i, uint8_t number) { assert(b[i].shown && b[i].kind == Kind::kItem && b[i].label == number); }
}  // namespace

static void off_and_menus() {
    // no Quest Log yet (title screen, file select): the full pad, no context rules
    PlayContext c;
    Buttons b = context_buttons(c);
    assert(!b.active);
    for (const ButtonView& v : b.b) assert(v.shown);
    // the pause menu and the item screen use every button
    c = walking();
    c.flags |= kMenu;
    c.flags &= ~kInPlay;
    b = context_buttons(c);
    assert(!b.active);
    for (const ButtonView& v : b.b) assert(v.shown);
}

static void walking_around() {
    PlayContext c = walking();
    Buttons b = context_buttons(c);
    assert(b.active && !b.tap_continues);
    // nothing to do with A: no A
    only(b, {kB, kX, kY, kR, kZR, kZL, kPlus, kStickR, kDpadUp});
    action(b, kB, kActSword);
    action(b, kZR, kActCrouch);
    item(b, kX, kItemBow);
    item(b, kY, kItemPictoBox);
    item(b, kR, kItemTelescope);
    // facing a person: A says Speak; facing a door: Open
    c.a_action = kActSpeak;
    c.attention_type = kAttnTalk;
    action(context_buttons(c), kA, kActSpeak);
    c.a_action = kActOpen;
    c.attention_type = kAttnDoor;
    action(context_buttons(c), kA, kActOpen);
    // no Wind Waker yet: no d-pad up; an empty item button: hidden; B hidden by the game: hidden
    c.flags &= ~kHasWindWaker;
    c.y_item = kItemNone;
    c.b_action = kActHidden;
    b = context_buttons(c);
    assert(!b[kDpadUp].shown && !b[kY].shown && !b[kB].shown && b[kA].shown);
    // the album's "nothing" on A is no action
    c.a_action = kActAlbumNothing;
    assert(!context_buttons(c)[kA].shown);
}

static void carrying_a_pot() {
    PlayContext c = walking();
    c.a_action = kActLift;
    c.attention_type = kAttnCarry;
    action(context_buttons(c), kA, kActLift);
    // lifted: A throws, ZR sets it down, and B (the game says sword) throws too
    c.a_action = kActThrow;
    c.zr_action = kActDrop;
    assert(carrying(c));
    const Buttons b = context_buttons(c);
    action(b, kA, kActThrow);
    action(b, kB, kActThrow);
    action(b, kZR, kActDrop);
    // a Bokoblin's club is no pot: B keeps the club
    c.b_action = kActBokoStick;
    assert(!carrying(c));
    action(context_buttons(c), kB, kActBokoStick);
}

static void swimming_around() {
    PlayContext c = walking();
    c.player_status0 = kSt0Swim;
    c.mode_flags = kModeSwim;
    c.zr_action = kActNone;
    const Buttons b = context_buttons(c);
    // no sword, no items, no first person and no Wind Waker in water; targeting and the menu stay
    only(b, {kZL, kPlus});
    // an action the water offers (climbing out) still shows on A
    c.a_action = kActClimb;
    action(context_buttons(c), kA, kActClimb);
}

static void on_a_ladder() {
    PlayContext c = walking();
    c.mode_flags = kModeLadder;
    c.player_status0 = kSt0Ladder;
    c.a_action = kActLetGo;
    const Buttons b = context_buttons(c);
    action(b, kA, kActLetGo);
    assert(!b[kB].shown && !b[kX].shown && !b[kStickR].shown && !b[kDpadUp].shown);
    // hanging in the boat is getting ready to sail, not climbing
    c.mode_flags = kModeHang | kModeInShip;
    c.player_status0 = kSt0ShipRide;
    assert(!climbing(c));
    const Buttons s = context_buttons(c);
    assert(s[kB].shown && s[kDpadLeftRight].shown);
    // in the boat only the boat's items: the telescope yes, the Picto Box (also allowed) yes, the Skull Hammer no
    c.y_item = 0x33;
    assert(!context_buttons(c)[kY].shown && context_buttons(c)[kR].shown);
}

static void aiming_the_bow() {
    PlayContext c = walking();
    c.player_status0 = kSt0BowAim | kSt0FirstPerson;
    c.item_in_hand = kItemBow;
    c.ready_item_button = 0;
    c.a_action = kActPutAway;
    c.b_action = kActReturn;
    assert(aiming(c) && item_in_hand(c));
    const Buttons b = context_buttons(c);
    // shoot with the bow's own button, B / A put it away; no menu while aiming
    item(b, kX, kItemBow);
    action(b, kA, kActPutAway);
    action(b, kB, kActReturn);
    assert(!b[kPlus].shown && b[kZL].shown);
}

static void picto_box() {
    PlayContext c = walking();
    c.player_status1 = kSt1PictoAim;
    c.zr_action = kActPictoAim;  // ZR: to the album
    c.b_action = kActReturn;
    c.a_action = kActNone;
    c.flags |= kPictoFinder | kZoomBar | kMessage;  // (hud_gate's message: the scope status, seen live 11)
    c.scope_mesg_status = 11;
    c.zoom_kind = 1;
    Buttons b = context_buttons(c);
    // A takes the photo (the game's byte is empty there), B goes back, ZR the album; nothing else
    only(b, {kA, kB, kZR});
    action(b, kA, kActTakePhoto);
    action(b, kB, kActReturn);
    action(b, kZR, kActPictoAim);
    // its album: A chooses, the d-pad turns the photos
    c.player_status1 = 0;
    c.flags = (c.flags & ~(kPictoFinder | kZoomBar)) | kAlbum;
    c.zr_action = kActPictoAlbum;
    c.a_action = kActChoose;
    b = context_buttons(c);
    only(b, {kA, kB, kZR, kDpadLeftRight});
    action(b, kA, kActChoose);
    // the telescope: B and its own button put it away; no targeting, first person or menu
    c = walking();
    c.player_status0 = kSt0Telescope;
    c.b_action = kActReturn;
    b = context_buttons(c);
    only(b, {kB, kR});
    item(b, kR, kItemTelescope);
}

static void talking_to_someone() {
    PlayContext c = walking();
    c.flags = kInGame | kLink | kEvent | kMessage | kMsgWindow | kKeyIcon;
    c.a_action = c.b_action = c.zr_action = kActNone;  // the game clears its prompts in a talk
    Buttons b = context_buttons(c);
    // A goes on (Next; the game's byte is empty), nothing else; a tap goes on too
    only(b, {kA});
    action(b, kA, kActNext);
    assert(b.tap_continues);
    // with tap to continue (touch), A itself goes; with the event fast forward on, ZR fast-forwards
    b = context_buttons(c, {true, true});
    only(b, {kZR});
    assert(b[kZR].kind == Kind::kFastForward);
    // the text still being written (no key icon): A shows, but a tap is not yet "go on"
    c.flags &= ~kKeyIcon;
    b = context_buttons(c, {false, true});
    assert(!b.tap_continues && b[kA].shown);
    // a choice: A takes the line, no B (the game ignores it in yes / no)
    c.flags |= kChoice;
    c.choice_count = 2;
    b = context_buttons(c);
    only(b, {kA});
    action(b, kA, kActChoose);
    // the auction's number entry: A takes it, B cancels
    c.flags = (c.flags & ~kChoice) | kNumberEntry;
    b = context_buttons(c);
    only(b, {kA, kB});
    action(b, kB, kActCancel);
}

static void in_a_shop() {
    // Beedle's shop as traced live: the item choice prompts A Choose / B Cancel, message status 14
    PlayContext c = walking();
    c.flags = kInGame | kLink | kEvent | kMessage | kMsgWindow | kShop;
    c.mesg_status = 14;
    c.a_action = kActChoose;
    c.b_action = kActCancel;
    c.zr_action = kActNone;
    const Buttons b = context_buttons(c);
    // choosing an item: A takes it, B leaves the shop; no items, sword or menu
    only(b, {kA, kB});
    action(b, kA, kActChoose);
    action(b, kB, kActCancel);
}

static void cutscenes_and_fades() {
    PlayContext c = walking();
    c.flags = kInGame | kLink | kEvent;  // a cutscene without text
    only(context_buttons(c), {});
    const Buttons ff = context_buttons(c, {true, false});
    only(ff, {kZR});
    assert(ff[kZR].kind == Kind::kFastForward);
    // a fade or a stage change: nothing to press
    c.flags = kInGame | kLink | kFade;
    only(context_buttons(c), {});
    c.flags = kInGame | kLink | kStageChange;
    only(context_buttons(c), {});
    // ... but text over a fade still goes on
    c.flags = kInGame | kLink | kFade | kEvent | kMessage | kKeyIcon;
    only(context_buttons(c), {kA});
    // another actor controlled (the Seagull): nothing from Link's rules
    c.flags = kInGame;
    only(context_buttons(c), {});
}

static void wind_compass() {
    PlayContext c = walking();
    c.flags = kInGame | kLink | kEvent | kWindCompass;
    c.a_action = c.b_action = kActNone;
    const Buttons b = context_buttons(c);
    only(b, {kA, kB});
    action(b, kA, kActChoose);
    action(b, kB, kActReturn);
}

static void trace_format() {
    char buf[160];
    format(context_buttons(PlayContext{}), buf, sizeof buf);
    assert(!strcmp(buf, "all"));
    PlayContext c = walking();
    c.a_action = kActLift;
    format(context_buttons(c), buf, sizeof buf);
    assert(!strcmp(buf, "A:04 B:35 X:i27 Y:i23 R:i20 ZR:0F ZL + R3 Up"));
    c.flags = kInGame | kLink | kEvent | kKeyIcon;
    format(context_buttons(c, {true, true}), buf, sizeof buf);
    assert(!strcmp(buf, "ZR:ff tap"));
}

static void item_rules() {
    PlayContext c = walking();
    assert(item_usable(c, kItemBow) && !item_usable(c, kItemNone));
    c.player_status0 = kSt0Rope;  // on the grappling hook's rope: only the hook
    assert(item_usable(c, kItemGrapplingHook) && !item_usable(c, kItemBow));
    c.player_status0 = kSt0Crawl;
    assert(!item_usable(c, kItemGrapplingHook));
    c.player_status0 = 0;
    c.minigame = 1;  // the sailing minigame: sails and the telescope
    assert(item_usable(c, kItemSail) && item_usable(c, kItemTelescope) && !item_usable(c, kItemBow));
}

// P6: the on-screen Jump and Moon jump buttons
static void jump_buttons_shown() {
    PlayContext c = walking();
    assert(jump_buttons(c, false, false) == 0);
    assert(jump_buttons(c, true, false) == kShowJump);
    assert(jump_buttons(c, true, true) == (kShowJump | kShowMoonJump));
    assert(jump_buttons(c, false, true) == kShowMoonJump);
    // swimming, climbing, hanging: Jump anywhere does nothing there, the moon jump lifts him out
    PlayContext w = walking();
    w.player_status0 |= kSt0Swim;
    w.mode_flags |= kModeSwim;
    assert(jump_buttons(w, true, true) == kShowMoonJump);
    PlayContext l = walking();
    l.mode_flags |= kModeLadder;
    assert(jump_buttons(l, true, false) == 0);
    // the boat's own "hang" is no climbing: Jump stays
    PlayContext b = walking();
    b.mode_flags |= kModeHang | kModeInShip;
    assert(jump_buttons(b, true, false) == kShowJump);
    // cutscenes and talks (not in play), menus, fades, the title screen: none
    for (uint32_t f : {kEvent, kMenu, kFade, kStageChange}) {
        PlayContext e = walking();
        e.flags = (e.flags | f) & ~kInPlay;
        assert(jump_buttons(e, true, true) == 0);
    }
    PlayContext t;
    assert(jump_buttons(t, true, true) == 0);
    // the Picto Box (aiming, finder, album) and the telescope: none
    PlayContext p = walking();
    p.player_status1 |= kSt1PictoAim;
    assert(jump_buttons(p, true, true) == 0);
    PlayContext a = walking();
    a.flags |= kAlbum;
    assert(jump_buttons(a, true, true) == 0);
    PlayContext s = walking();
    s.player_status0 |= kSt0Telescope;
    assert(jump_buttons(s, true, true) == 0);
    // another actor controlled (no Link): none
    PlayContext n = walking();
    n.flags &= ~kLink;
    assert(jump_buttons(n, true, true) == 0);
}

int main() {
    jump_buttons_shown();
    off_and_menus();
    walking_around();
    carrying_a_pot();
    swimming_around();
    on_a_ladder();
    aiming_the_bow();
    picto_box();
    talking_to_someone();
    in_a_shop();
    cutscenes_and_fades();
    wind_compass();
    item_rules();
    trace_format();
    return 0;
}
