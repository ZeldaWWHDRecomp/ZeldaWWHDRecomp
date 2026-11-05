// SPDX-License-Identifier: MPL-2.0
// touch_talk.h: talks, answers, number entry, shops, the wind compass and menu pages by touch, and hold to
// go on (finger and controller), decided read by read on fixture PlayContexts.
#include "tv_pointer.h"
#include "mods/touch_talk.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace mods;
using namespace mods::playctx;
using namespace mods::talktouch;

namespace {

PlayContext talk(bool icon) {
    PlayContext c;
    c.flags = kInGame | kLink | kEvent | kMessage | kMsgWindow | (icon ? kKeyIcon : 0);
    c.mesg_status = icon ? 15 : 6;
    return c;
}
PlayContext choice(int n, float box_y, int cursor_line) {
    PlayContext c = talk(true);
    c.flags |= kChoice;
    c.choice_count = uint8_t(n);
    c.choice_y = int16_t(box_y);
    c.cursor_y = int16_t(box_y + kChoiceLine * (n - 1) / 2.0f - kChoiceLine * cursor_line);
    return c;
}
PlayContext shop_choice() {
    PlayContext c = talk(true);
    c.flags |= kShop;
    c.mesg_status = 14;
    c.a_action = kActChoose, c.b_action = kActCancel;
    return c;
}
PlayContext play() {
    PlayContext c;
    c.flags = kInGame | kLink | kInPlay;
    return c;
}

struct Driver {
    Machine m;
    Options o;
    In in;
    uint64_t step = 100;
    std::vector<Finger> fingers;
    Out read(const PlayContext& c) {
        in.step = step++;
        in.fingers = fingers.data();
        in.nfingers = int(fingers.size());
        Out out = m.step(c, in, o);
        fingers.clear();
        return out;
    }
    void down(float x, float y) { fingers.push_back({Finger::kDown, x, y}); }
    void move(float x, float y) { fingers.push_back({Finger::kMove, x, y}); }
    void up(float x, float y) { fingers.push_back({Finger::kUp, x, y}); }
    // reads until something is pressed or n reads passed; returns the reads' outputs
    std::vector<Out> run(const PlayContext& c, int n) {
        std::vector<Out> v;
        for (int i = 0; i < n; i++) v.push_back(read(c));
        return v;
    }
};

int count_press(const std::vector<Out>& v, uint32_t bit) {  // rising edges of bit
    int n = 0;
    bool was = false;
    for (const Out& o : v) {
        const bool is = (o.press & bit) != 0;
        if (is && !was) n++;
        was = is;
    }
    return n;
}
int count_stick(const std::vector<Out>& v, float lx, float ly) {  // pushes to (lx, ly)
    int n = 0;
    bool was = false;
    for (const Out& o : v) {
        const bool is = o.stick && o.lx == lx && o.ly == ly;
        if (is && !was) n++;
        was = is;
    }
    return n;
}

}  // namespace

int main() {
    // issue #315: a left mouse button bound to a game button wins over the pages / talks on the TV picture
    // (with the mouse camera, which the bindings need) and over the GamePad picture's touch (captured)
    assert(tv_pointer::picture_takes_click(false, true) && tv_pointer::picture_takes_click(false, false));
    assert(tv_pointer::picture_takes_click(true, false) && !tv_pointer::picture_takes_click(true, true));
    assert(tv_pointer::gamepad_takes_click(false, true) && tv_pointer::gamepad_takes_click(true, false));
    assert(!tv_pointer::gamepad_takes_click(true, true));

    // ---- claims
    {
        Options o;
        assert(!claims(play(), o, false));
        assert(claims(talk(false), o, false));
        assert(claims(choice(2, 0, 0), o, false));
        PlayContext menu = play();
        menu.flags = kInGame | kMenu;
        assert(!claims(menu, o, false) && claims(menu, o, true));
        o.touch = false;
        assert(!claims(talk(true), o, false));
        PlayContext title;  // no Quest Log
        assert(!claims(title, Options{}, false));
    }
    // ---- a tap in a talk goes on (A for a moment); a slide doesn't
    {
        Driver d;
        d.down(0, 0);
        d.read(talk(false));
        d.up(5, 3);
        auto v = d.run(talk(false), 8);
        assert(count_press(v, kBtnA) == 1 && (v[0].press & kBtnA) && (v[1].press & kBtnA) && !(v[2].press & kBtnA));
        d.down(0, 0);
        d.move(100, 0);
        d.read(talk(false));
        d.up(100, 0);
        v = d.run(talk(false), 8);
        assert(count_press(v, kBtnA) == 0);
        // a long press without the hold's icon: no tap either
        d.down(0, 0);
        d.run(talk(false), int(kTapSteps) + 2);
        d.up(0, 0);
        v = d.run(talk(false), 6);
        assert(count_press(v, kBtnA) == 0);
    }
    // ---- a held finger goes on each time the icon has been up a moment, never at a choice
    {
        Driver d;
        d.o.hold_ff = true;
        d.down(0, 0);
        auto v = d.run(talk(false), int(kHoldSteps) + 1);
        assert(count_press(v, kBtnA) == 0 && d.m.holding());
        assert(v.back().fast_forward);
        v = d.run(talk(true), int(kIconSteps) - 1);
        assert(count_press(v, kBtnA) == 0);  // the icon only just came
        v = d.run(talk(true), 6);
        assert(count_press(v, kBtnA) == 1);
        v = d.run(talk(false), 20);  // the next page types
        assert(count_press(v, kBtnA) == 0);
        v = d.run(talk(true), int(kIconSteps) + 6);
        assert(count_press(v, kBtnA) == 1 && d.m.holds() == 2);
        // a choice: no press while it is up, nor right after it went
        v = d.run(choice(2, 0, 0), 30);
        assert(count_press(v, kBtnA) == 0 && !v.back().fast_forward);
        v = d.run(talk(true), int(kChoiceQuietSteps) - 2);
        assert(count_press(v, kBtnA) == 0);
        // lifting after a hold is no tap
        d.up(0, 0);
        v = d.run(talk(false), 6);
        assert(count_press(v, kBtnA) == 0 && !v.back().fast_forward);
        // without the option: no fast forward
        Driver e;
        e.down(0, 0);
        v = e.run(talk(false), int(kHoldSteps) + 2);
        assert(e.m.holding() && !v.back().fast_forward);
    }
    // ---- hold A on a controller
    {
        Driver d;
        d.o.pad_hold = true;
        d.in.pad_a = true;
        auto v = d.run(talk(false), int(kPadHoldSteps) - 1);
        for (const Out& o : v) assert(!(o.release & kBtnA));  // the first press is the game's
        v = d.run(talk(false), 2);
        assert((v.back().release & kBtnA) && d.m.pad_holding());
        v = d.run(talk(true), int(kIconSteps) + 4);
        assert(count_press(v, kBtnA) == 1);
        // a question opens under the held A: A stays up for the game
        v = d.run(choice(2, 0, 0), 20);
        for (const Out& o : v) assert((o.release & kBtnA) && !(o.press & kBtnA));
        // the talk ends with A still held: still kept from the game until it is let go
        v = d.run(play(), 5);
        for (const Out& o : v) assert(o.release & kBtnA);
        d.in.pad_a = false;
        v = d.run(play(), 2);
        assert(!(v.back().release & kBtnA) && !d.m.pad_holding());
        // off: A is the game's
        Driver e;
        e.in.pad_a = true;
        v = e.run(talk(true), 40);
        for (const Out& o : v) assert(!o.release && !o.press);
        // the A that started the talk ("Speak"), held on: the talk is held at once
        Driver f;
        f.o.pad_hold = true;
        f.in.pad_a = true;
        v = f.run(play(), 30);
        for (const Out& o : v) assert(!o.release);  // (in play A is the game's, however long)
        v = f.run(talk(true), 4);
        assert(f.m.pad_holding() && (v.back().release & kBtnA));
    }
    // ---- choices: a tap on a line moves the cursor there and takes it
    {
        assert(choice_line_at(0, 2, 26) == 0 && choice_line_at(0, 2, -26) == 1 && choice_line_at(0, 2, 90) == -1);
        assert(choice_line_at(10, 3, 62) == 0 && choice_line_at(10, 3, 10) == 1 && choice_line_at(10, 3, -42) == 2);
        assert(choice_cursor_line(0, 2, 26) == 0 && choice_cursor_line(0, 2, -26) == 1 && choice_cursor_line(0, 2, -400) == 1);
        Driver d;
        d.in.choice_x = 100;
        const PlayContext c = choice(3, 0, 0);
        d.down(120, -52);
        d.read(c);
        d.up(120, -52);
        auto v = d.run(c, 40);
        assert(count_stick(v, 0, -1) == 2 && count_press(v, kBtnA) == 1);  // two lines down, then A
        // the A comes after the pushes
        size_t last_push = 0, first_a = 0;
        for (size_t i = 0; i < v.size(); i++) {
            if (v[i].stick && v[i].ly == -1) last_push = i;
            if ((v[i].press & kBtnA) && !first_a) first_a = i;
        }
        assert(first_a > last_push);
        // a tap off the box: nothing
        d.down(600, 0);
        d.read(c);
        d.up(600, 0);
        v = d.run(c, 20);
        assert(count_press(v, kBtnA) == 0 && count_stick(v, 0, -1) == 0);
        // on the cursor's own line: just A
        d.down(100, 52);
        d.read(c);
        d.up(100, 52);
        v = d.run(c, 20);
        assert(count_press(v, kBtnA) == 1 && count_stick(v, 0, -1) == 0 && count_stick(v, 0, 1) == 0);
        // from the bottom line up
        const PlayContext c2 = choice(2, 0, 1);
        d.down(100, 26);
        d.read(c2);
        d.up(100, 26);
        v = d.run(c2, 20);
        assert(count_stick(v, 0, 1) == 1 && count_press(v, kBtnA) == 1);
    }
    // ---- a shop: tap an item, the cursor goes there; a tap on it asks to buy; the question needs "yes"
    {
        Driver d;
        Shop s;
        s.open = true;
        s.seen[0] = s.seen[1] = s.seen[2] = true;
        s.x[0] = -50, s.x[1] = 135, s.x[2] = 340;
        s.y[0] = s.y[1] = s.y[2] = 30;
        d.in.shop = s;
        const PlayContext sc = shop_choice();
        d.read(sc);
        // a tap next to nothing: nothing
        d.down(0, -300);
        d.read(sc);
        d.up(0, -300);
        auto v = d.run(sc, 10);
        assert(count_press(v, kBtnA) == 0 && d.m.shop_target() < 0 && !d.m.busy());
        // a tap on the heart piece: pushes right (nothing chosen yet: from the middle) until it is chosen
        d.down(140, 40);
        d.read(sc);
        d.up(140, 40);
        v = d.run(sc, 5);  // (a push: 2 reads, then 3 at rest)
        assert(count_stick(v, 1, 0) == 1 && d.m.shop_target() == 1);
        d.in.shop.selected = 0;  // the game chose the bottle: on to the right from there
        v = d.run(sc, 5);
        assert(count_stick(v, 1, 0) == 1);
        d.in.shop.selected = 1;
        v = d.run(sc, 10);
        assert(count_stick(v, 1, 0) == 0 && d.m.shop_target() < 0 && count_press(v, kBtnA) == 0);
        // a tap on the chosen item: A, once
        d.down(130, 25);
        d.read(sc);
        d.up(130, 25);
        v = d.run(sc, 6);
        assert(count_press(v, kBtnA) == 1);
        // its price text: goes on by itself (once the icon stayed up), then the question waits for a tap
        d.in.shop.open = false;
        v = d.run(talk(false), 10);
        v = d.run(talk(true), int(kAskSteps) + 6);
        assert(count_press(v, kBtnA) == 1);
        PlayContext q = choice(2, -100, 0);
        v = d.run(q, 40);
        assert(count_press(v, kBtnA) == 0 && count_stick(v, 0, -1) == 0);  // nobody says yes for the player
        // a tap off the lines is "no": to the last line, then A
        d.in.choice_x = 0;
        d.down(500, 300);
        d.read(q);
        d.up(500, 300);
        v = d.run(q, 30);
        assert(count_stick(v, 0, -1) == 1 && count_press(v, kBtnA) == 1);
        // a controller's B at the question: "no" too, and the game doesn't see that B
        d.in.pad_b = true;
        Out o = d.read(q);
        assert(o.release & kBtnB);
        d.in.pad_b = false;
        v = d.run(q, 30);
        assert(count_stick(v, 0, -1) == 1 && count_press(v, kBtnA) == 1);
        // the talk ends: no longer a shop talk; a later question elsewhere: B is the game's
        d.run(play(), 2);
        assert(!d.m.shop_talk());
        d.in.pad_b = true;
        o = d.read(choice(2, 0, 0));
        assert(!(o.release & kBtnB));
        d.in.pad_b = false;
        // a hold never presses in a shop's talk
        Driver h;
        h.in.shop = s;
        h.read(sc);
        h.down(0, 0);
        v = h.run(talk(true), 60);
        assert(count_press(v, kBtnA) == 0);
    }
    // ---- a shop whose shelf is wider than the picture: one push towards the item out of view
    {
        Driver d;
        Shop s;
        s.open = true;
        s.seen[0] = s.seen[1] = true;
        s.x[0] = 0, s.x[1] = 900;
        d.in.shop = s;
        auto v = d.run(shop_choice(), 10);
        assert(count_stick(v, 1, 0) == 1);
        v = d.run(shop_choice(), 10);
        assert(count_stick(v, 1, 0) == 0);  // once
    }
    // ---- number entry: the finger as the stick, a tap takes it
    {
        Driver d;
        PlayContext c = talk(true);
        c.flags |= kNumberEntry;
        d.down(0, 0);
        d.read(c);
        d.move(0, 70);
        auto v = d.run(c, 6);
        assert(count_stick(v, 0, 1) == 1);
        d.move(-70, 70);
        v = d.run(c, 6);
        assert(count_stick(v, -1, 0) == 1);
        d.up(-70, 70);
        v = d.run(c, 6);
        assert(count_press(v, kBtnA) == 0);  // (a drag is no tap)
        d.down(0, 0);
        d.read(c);
        d.up(0, 0);
        v = d.run(c, 6);
        assert(count_press(v, kBtnA) == 1);
    }
    // ---- the wind compass: the finger points the stick from its centre; a tap takes it
    {
        Driver d;
        PlayContext c = play();
        c.flags = kInGame | kLink | kEvent | kWindCompass;
        d.in.wind_x = 100, d.in.wind_y = 50;
        d.down(200, 50);
        Out o = d.read(c);
        assert(o.stick && std::fabs(o.lx - 1) < 1e-5f && std::fabs(o.ly) < 1e-5f);
        d.move(100, -50);
        o = d.read(c);
        assert(o.stick && std::fabs(o.lx) < 1e-5f && std::fabs(o.ly + 1) < 1e-5f);
        d.up(100, -50);
        o = d.read(c);
        assert(!o.stick && !(o.press & kBtnA));
        d.down(102, 52);
        d.read(c);
        d.up(102, 52);
        auto v = d.run(c, 6);
        assert(count_press(v, kBtnA) == 1);
    }
    // ---- a game menu on the TV: a swipe sideways turns the page (L, either way)
    {
        Driver d;
        d.in.menu_on_tv = true;
        PlayContext c;
        c.flags = kInGame | kMenu;
        d.down(300, 0);
        d.read(c);
        d.move(100, 10);
        d.read(c);
        d.up(50, 10);
        auto v = d.run(c, 6);
        assert(count_press(v, kBtnL) == 1 && count_press(v, kBtnR) == 0);
        d.down(-300, 0);
        d.read(c);
        d.up(0, 0);
        v = d.run(c, 6);
        assert(count_press(v, kBtnL) == 1);
        // too slow, or mostly up and down: nothing
        d.down(-300, 0);
        d.run(c, int(kSwipeSteps) + 2);
        d.up(0, 0);
        v = d.run(c, 6);
        assert(count_press(v, kBtnL) == 0);
        d.down(0, 200);
        d.read(c);
        d.up(170, -200);
        v = d.run(c, 6);
        assert(count_press(v, kBtnR) == 0 && count_press(v, kBtnL) == 0);
        // not on the TV (GamePad mode: the game's own touch screen has its menu): nothing is claimed
        assert(!claims(c, Options{}, false));
    }
    // ---- the option off: fingers do nothing
    {
        Driver d;
        d.o.touch = false;
        d.down(0, 0);
        d.read(talk(true));
        d.up(0, 0);
        auto v = d.run(talk(true), 8);
        assert(count_press(v, kBtnA) == 0);
    }
    // ---- a new screen drops what was queued for the old one
    {
        Driver d;
        d.in.choice_x = 0;
        const PlayContext c = choice(3, 0, 0);
        d.down(0, -52);
        d.read(c);
        d.up(0, -52);
        d.read(c);
        assert(d.m.busy());
        d.read(play());
        assert(!d.m.busy());
    }
    // ---- a system question (the save question after a game over): a tap on a button takes it
    {
        PlayContext q;
        q.flags = kInGame | kLink | kQuestion;
        q.question_shown = 3;
        q.question_x[0] = -200, q.question_y[0] = -150, q.question_x[1] = 200, q.question_y[1] = -150;
        q.question_scale[0] = 1100, q.question_scale[1] = 1000;  // the cursor is on the left one
        Options o;
        assert(claims(q, o, false) && mode_of(q, false) == kModeQuestion);
        assert(question_button_at(q, -180, -140) == 0 && question_button_at(q, 210, -170) == 1 && question_button_at(q, 0, 0) == -1);
        assert(question_cursor_on(q, 0) && !question_cursor_on(q, 1));
        // on the button the cursor is on: A only
        Driver d;
        d.down(-200, -150);
        d.read(q);
        d.up(-200, -150);
        std::vector<Out> v = d.run(q, 12);
        assert(count_press(v, kBtnA) == 1 && count_stick(v, 1, 0) == 0 && count_stick(v, -1, 0) == 0);
        // on the other one: the stick right first, then A
        Driver e;
        e.down(200, -150);
        e.read(q);
        e.up(200, -150);
        v = e.run(q, 16);
        assert(count_stick(v, 1, 0) == 1 && count_press(v, kBtnA) == 1);
        size_t first_stick = 0, first_a = 0;
        for (size_t i = 0; i < v.size(); i++) {
            if (!first_stick && v[i].stick && v[i].lx == 1) first_stick = i + 1;
            if (!first_a && (v[i].press & kBtnA)) first_a = i + 1;
        }
        assert(first_stick && first_a > first_stick);
        // off the buttons: nothing
        Driver g;
        g.down(0, 200);
        g.read(q);
        g.up(0, 200);
        v = g.run(q, 12);
        assert(count_press(v, kBtnA) == 0);
    }
    // ---- a letter / stone tablet / sign: its scroll window is the message window, so a tap goes on (A)
    {
        PlayContext letter;
        letter.flags = kInGame | kLink | kEvent | kMsgWindow;  // (no talk's message status, no key icon needed)
        Options o;
        assert(claims(letter, o, false) && mode_of(letter, false) == kModeTalk);
        Driver d;
        d.down(0, 0);
        d.read(letter);
        d.up(0, 0);
        assert(count_press(d.run(letter, 8), kBtnA) == 1);
    }
    printf("touch_talk_test: ok\n");
    return 0;
}
