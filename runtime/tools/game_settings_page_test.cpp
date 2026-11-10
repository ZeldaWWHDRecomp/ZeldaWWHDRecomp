// The item menu's settings page (runtime/src/mods/game_settings_page.h): entry, navigation, changes,
// touch and closing, without the game.
#include <cassert>
#include <cstdio>

#include "mods/game_settings_graphics.h"
#include "mods/game_settings_page.h"
#include "mods/game_settings_entry.h"
#include "mods/game_settings_text.h"

using namespace game_settings_page;
static const float kEntryX = Page().entry_x(), kArrowYc = ArrowPlace().y, kBackXc = ArrowPlace().x;

static Input pad(uint32_t buttons, float dt = 1.0f / 30) {
    Input in;
    in.buttons = buttons, in.dt = dt;
    return in;
}
static Input touch(bool down, float x, float y) {
    Input in;
    in.touch = down, in.x = x, in.y = y, in.dt = 1.0f / 30;
    return in;
}

int main() {
    // The arrow requires the game's right target, with neither a left target nor Back.
    for (int mask = 0; mask < 16; ++mask)
        assert(entry_exists(mask & 1, mask & 2, mask & 4, mask & 8) == (mask == 3));
    {
        EntryPosition item{1, 2, 3, 4, 0, -462, 300};
        EntryFocus focus;
        assert(!focus.update(true, true, true, item, 10)); // live neighbour: game always wins
        assert(!focus.update(true, false, true, item, 20) && !focus.focused());
        assert(!focus.update(true, true, false, item, 30)); // a dead Left is first given to the game
        assert(!focus.update(true, true, false, item, 40) && !focus.focused()); // holding cannot turn it
        assert(focus.update(true, false, false, item, 41) && focus.focused());
        focus.clear();
        assert(!focus.update(true, true, false, item, 50));
        auto moved = item; moved.receiver++;
        assert(!focus.update(true, false, false, moved, 60) && !focus.focused());
        assert(!focus.update(true, true, false, item, 70));
        auto page = item; page.offset = -1280;
        assert(!focus.update(true, false, false, page, 80) && !focus.focused());
        assert(!focus.update(true, true, false, item, 90));
        assert(!focus.update(false, false, false, item, 100) && !focus.focused());
        assert(!focus.update(true, true, false, item, 110));
        assert(!focus.update(true, false, true, item, 120) && !focus.focused());
        // Duplicate reads in the same frame don't finish the probe.
        assert(!focus.update(true, true, false, item, 130));
        assert(!focus.update(true, false, false, item, 130));
        assert(!focus.update(true, false, false, item, 133));
        assert(focus.update(true, false, false, item, 134));
    }
    std::vector<Row> rows(3);
    rows[0].id = "a", rows[0].toggle = true;
    rows[1].id = "b", rows[1].number = true;
    rows[2].id = "c", rows[2].toggle = true, rows[2].enabled = false;
    // Entry: ZL belongs to the game; A / Left only turn a focused arrow.
    Page p;
    assert(!p.entry(pad(kPadZL)));
    Page q;
    assert(!q.entry(pad(0)));
    Input confirm = pad(kPadA); confirm.arrow_focused = true;
    assert(q.entry(confirm));
    Page held;
    assert(!held.entry(pad(kPadA)));
    assert(!held.entry(confirm)); // moving onto the arrow cannot activate an already held A
    held.entry(pad(0));
    assert(held.entry(confirm));
    Page t;
    assert(!t.entry(touch(true, kEntryX, kArrowYc)));
    assert(t.entry(touch(false, kEntryX, kArrowYc)));
    Page d;
    assert(!d.entry(touch(true, kEntryX, kArrowYc)));
    assert(!d.entry(touch(true, kEntryX, kArrowYc + 200)));
    assert(!d.entry(touch(false, kEntryX, kArrowYc + 200)));  // a drag (not to the right) is not a tap
    // Left only turns the page after the arrow has been reached
    Page lc;
    Input left = pad(kPadLeft);
    assert(!lc.entry(left));
    lc.entry(pad(0));
    left.arrow_focused = true;
    assert(lc.entry(left));
    Page st;
    Input stick = pad(0);
    stick.lx = -1, stick.arrow_focused = true;
    assert(st.entry(stick));
    // a swipe from the arrow: the page follows; far enough it opens, a short one slides back
    Page sw;
    assert(!sw.entry(touch(true, kEntryX, kArrowYc)));
    assert(!sw.entry(touch(true, kEntryX + 200, kArrowYc)) && sw.swiping() && sw.shown());
    assert(!sw.entry(touch(true, kEntryX + 400, kArrowYc)));
    assert(sw.entry(touch(false, kEntryX + 400, kArrowYc)));
    Page sb;
    sb.entry(touch(true, kEntryX, kArrowYc));
    Input slow = touch(true, kEntryX + 100, kArrowYc);
    slow.dt = 1;  // (slow: no flick)
    sb.entry(slow);
    Input up = touch(false, kEntryX + 100, kArrowYc);
    assert(!sb.entry(up) && sb.shown());
    for (int i = 0; i < 20; i++) sb.tick(1.0f / 30);
    assert(!sb.shown());
    Page disabled;
    disabled.entry(touch(true, kEntryX, kArrowYc));
    disabled.entry(touch(true, kEntryX + 200, kArrowYc));
    assert(disabled.shown() && disabled.swiping());
    disabled.cancel_entry();
    assert(!disabled.shown() && !disabled.swiping());
    assert(!disabled.entry(touch(false, kEntryX + 200, kArrowYc)));
    // open: the opening button acts only after its release
    q.show(1);
    for (int i = 0; i < 10; i++) q.tick(1.0f / 30);  // slid in
    Action a = q.input(pad(kPadA), rows, 1);
    assert(a.id.empty() && !a.closed);
    a = q.input(pad(0), rows, 1);
    a = q.input(pad(kPadA), rows, 1);
    assert(a.id == "a" && a.direction == 0 && a.sound == Sound::Change);
    a = q.input(pad(0), rows, 1);
    a = q.input(pad(kPadDown), rows, 1);
    assert(q.selected() == 1 && a.sound == Sound::Cursor);
    a = q.input(pad(0), rows, 1);
    a = q.input(pad(kPadLeft), rows, 1);
    assert(a.id == "b" && a.direction == -1);
    a = q.input(pad(0), rows, 1);
    a = q.input(pad(kPadA), rows, 1);
    assert(a.id == "b" && a.direction == +1);  // A steps a number up
    // the stick, with repeat
    a = q.input(pad(0), rows, 1);
    Input s = pad(0);
    s.ly = -1;
    a = q.input(s, rows, 1);
    assert(q.selected() == 2);
    a = q.input(pad(0), rows, 1);
    a = q.input(pad(kPadA), rows, 1);
    assert(a.id.empty() && a.sound == Sound::Blocked);  // a dimmed row does not change
    a = q.input(pad(0), rows, 1);
    // touch: a tap on the first row's value changes it
    const float y0 = kRowY + kRowH / 2;
    a = q.input(touch(true, kValueX + 20, y0), rows, 1);
    a = q.input(touch(false, kValueX + 20, y0), rows, 1);
    assert(q.selected() == 0 && a.id == "a");
    // a tap on the left half of a number's value steps down, the right half up
    a = q.input(touch(true, kValueX + 20, y0 + kRowH), rows, 1);
    a = q.input(touch(false, kValueX + 20, y0 + kRowH), rows, 1);
    assert(a.id == "b" && a.direction == -1);
    a = q.input(touch(true, kValueX + kValueW - 20, y0 + kRowH), rows, 1);
    a = q.input(touch(false, kValueX + kValueW - 20, y0 + kRowH), rows, 1);
    assert(a.id == "b" && a.direction == +1);
    // B closes; the page slides out
    a = q.input(pad(kPadB), rows, 1);
    assert(a.closed && !q.open() && q.shown());
    for (int i = 0; i < 20; i++) q.tick(1.0f / 30);
    assert(!q.shown());
    // the arrow back to the items closes too
    q.show(1);
    q.input(pad(0), rows, 1);
    a = q.input(touch(true, kBackXc, kArrowYc), rows, 1);
    a = q.input(touch(false, kBackXc, kArrowYc), rows, 1);
    assert(a.closed);
    // the Back prompt can be tapped
    q.show(1);
    for (int i = 0; i < 20; i++) q.tick(1.0f / 30);
    q.input(pad(0), rows, 1);
    {
        const float bx = kWinX + kWinW - 80, by = kWinY + kWinH - 50;
        q.input(touch(true, bx, by), rows, 1);
        a = q.input(touch(false, bx, by), rows, 1);
        assert(a.closed);
    }
    // drawing: the page and the entry arrow produce commands, the window first after the dim
    q.show(1);
    for (int i = 0; i < 20; i++) q.tick(1.0f / 30);
    const auto cmds = q.draw(rows, {"Camera"});
    assert(cmds.size() > 10 && cmds[1].text.rfind("nb:window@", 0) == 0);
    assert(!q.draw_entry().empty());
    // a stick held at the opening (it pushed Left onto the arrow) acts only after it centres, as held buttons;
    // another direction pushed meanwhile still acts
    {
        std::vector<Row> t(2);
        t[0].id = "invert", t[0].toggle = true;
        t[1].id = "other", t[1].toggle = true;
        Input left = pad(0), right = pad(0), down_left = pad(0);
        left.lx = -1, right.lx = 1, down_left.lx = -1, down_left.ly = -1;
        Page sp;
        sp.show(1);
        for (int i = 0; i < 20; i++) assert(sp.input(left, t, 1).id.empty());  // past the repeat delay too
        Action sa = sp.input(down_left, t, 1);
        assert(sa.id.empty() && sp.selected() == 1 && sa.sound == Sound::Cursor);
        sp.input(pad(0), t, 1);
        sa = sp.input(left, t, 1);
        assert(sa.id == "other" && sa.direction == -1);  // a fresh push acts
        // a row's page closed with the stick held: the row behind it does not change
        sp.hold_buttons();
        for (int i = 0; i < 20; i++) assert(sp.input(right, t, 1).id.empty());
        sp.input(pad(0), t, 1);
        assert(sp.input(right, t, 1).direction == +1);
    }
    // choices step like numbers; a long help is cut to two lines (valid UTF-8 kept)
    {
        std::vector<Row> c(1);
        c[0].id = "ch", c[0].choice = true;
        c[0].help = std::string(300, 'x') + " \xC3\xA9\xC3\xA9\xC3\xA9 end";
        Page cp;
        cp.show(1);
        cp.input(pad(0), c, 1);
        Action ca = cp.input(pad(kPadLeft), c, 1);
        assert(ca.id == "ch" && ca.direction == -1);
        for (int i = 0; i < 20; i++) cp.tick(1.0f / 30);
        for (const Command& k : cp.draw(c, {"A"}))
            if (k.kind == Command::Text) {
                for (size_t i = 0; i < k.text.size();) {  // well-formed UTF-8 (the HUD store drops a list otherwise)
                    const unsigned char b = (unsigned char)k.text[i];
                    const size_t n = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC2 ? 2 : 0;
                    assert(n && i + n <= k.text.size());
                    for (size_t j = 1; j < n; j++) assert(((unsigned char)k.text[i + j] & 0xC0) == 0x80);
                    i += n;
                }
            }
    }
    // the tab row: Up from the first row puts the cursor on the tabs, Left / Right there go from tab to tab
    // (no wrap), Down or A back to the shown tab's first row; L / R still turn the tabs from anywhere
    {
        std::vector<Row> r(3);
        r[0].id = "r0", r[0].toggle = true, r[0].label = "First";
        r[1].id = "r1", r[1].number = true, r[1].label = "Second";
        r[2].id = "r2", r[2].toggle = true, r[2].label = "Third";
        const std::vector<std::string> labels = {"Camera", "Controls", "Graphics"};
        auto push = [&](Page& pg, uint32_t b) {  // one push: down, then up
            Action got = pg.input(pad(b), r, 3);
            pg.input(pad(0), r, 3);
            return got;
        };
        auto cursor_on = [&](const Page& pg, float& x, float& y) {  // the brackets' place (the cursor box)
            for (const Command& k : pg.draw(r, labels))
                if (k.kind == Command::Picture && k.text.rfind("nb:cursor@", 0) == 0) x = k.x, y = k.y;
        };
        Page tp;
        tp.show(3);
        for (int i = 0; i < 20; i++) tp.tick(1.0f / 30);
        tp.input(pad(0), r, 3);
        push(tp, kPadDown);
        assert(tp.selected() == 1 && !tp.on_tabs());
        Action ta = push(tp, kPadUp);  // the second row: Up is the first row, as before
        assert(tp.selected() == 0 && !tp.on_tabs() && ta.sound == Sound::Cursor);
        ta = push(tp, kPadUp);  // the first row: the tab row
        assert(tp.on_tabs() && tp.tab() == 0 && ta.sound == Sound::Cursor && ta.id.empty());
        float cx = 0, cy = 0;
        cursor_on(tp, cx, cy);
        assert(cy < kWinY + 30 && cx > kWinX + kWinW / 2);  // the brackets around the shown tab (top right of the window)
        for (const Command& k : tp.draw(r, labels))  // no row is marked while the cursor is on the tabs
            assert(!(k.kind == Command::Text && k.text == bold("First")));
        {
            size_t marked = 0;  // (on the rows the selected row's label is bold)
            push(tp, kPadDown);
            for (const Command& k : tp.draw(r, labels)) marked += k.kind == Command::Text && k.text == bold("First");
            assert(marked == 1 && !tp.on_tabs());
            push(tp, kPadUp);
            assert(tp.on_tabs());
        }
        assert(push(tp, kPadUp).sound == Sound::None && tp.on_tabs());  // nothing above the tabs
        assert(push(tp, kPadLeft).sound == Sound::None && tp.tab() == 0);  // the first tab: no wrap
        ta = push(tp, kPadRight);
        assert(tp.on_tabs() && tp.tab() == 1 && ta.sound == Sound::Tab && ta.id.empty());  // Right: the next tab, no value changed
        push(tp, kPadRight);
        assert(tp.tab() == 2 && push(tp, kPadRight).sound == Sound::None && tp.tab() == 2);
        float nx = 0, ny = 0;
        cursor_on(tp, nx, ny);
        assert(nx > cx && ny == cy);  // (the last tab is the rightmost)
        ta = push(tp, kPadLeft);
        assert(tp.tab() == 1 && tp.on_tabs());
        ta = push(tp, kPadDown);  // Down: the tab's first row
        assert(!tp.on_tabs() && tp.tab() == 1 && tp.selected() == 0 && ta.sound == Sound::Cursor && ta.id.empty());
        push(tp, kPadUp);
        assert(tp.on_tabs());
        ta = push(tp, kPadA);  // A: the same, and no value changes
        assert(!tp.on_tabs() && tp.selected() == 0 && ta.sound == Sound::Decide && ta.id.empty());
        ta = push(tp, kPadA);  // then A changes the first row as before
        assert(ta.id == "r0");
        // L / R from the tab row and from the rows, as before
        push(tp, kPadUp);
        ta = push(tp, kPadR);
        assert(tp.tab() == 2 && ta.sound == Sound::Tab && tp.on_tabs());
        ta = push(tp, kPadR);
        assert(tp.tab() == 0);  // (L / R wrap, as before)
        push(tp, kPadDown);
        push(tp, kPadDown);
        ta = push(tp, kPadL);
        assert(tp.tab() == 2 && !tp.on_tabs() && tp.selected() == 0);
        // a held Up's repeat stops at the first row; a fresh push goes on to the tabs
        push(tp, kPadDown), push(tp, kPadDown);
        assert(tp.selected() == 2);
        for (int i = 0; i < 40; i++) tp.input(pad(kPadUp), r, 3);
        assert(tp.selected() == 0 && !tp.on_tabs());
        tp.input(pad(0), r, 3);
        Input stick_up = pad(0);
        stick_up.ly = 1;
        tp.input(stick_up, r, 3);
        assert(tp.on_tabs());  // (the stick too)
        tp.input(pad(0), r, 3);
        // touch: a tap on a tab shows it (from the rows or from the tab row), a tap on a row goes back to the rows
        float tx = 0, ty = 0;
        tp.draw(r, labels);
        cursor_on(tp, tx, ty);
        push(tp, kPadDown);
        assert(!tp.on_tabs());
        const auto tap = [&](float x, float y) {
            Action got = tp.input(touch(true, x, y), r, 3);
            Action up = tp.input(touch(false, x, y), r, 3);
            return up.sound != Sound::None ? up : got;
        };
        // the first tab's middle: the window's tab row starts right of the title; find it from the plates drawn
        float first_x = 0, first_w = 0;
        for (const Command& k : tp.draw(r, labels))
            if (k.kind == Command::Picture && (k.text.rfind("nb:tab@", 0) == 0 || k.text.rfind("nb:tab-on@", 0) == 0) && k.y < kWinY + 40 &&
                (first_w == 0 || k.x < first_x))
                first_x = k.x, first_w = k.w;
        ta = tap(first_x + first_w / 2, kWinY + 55);
        assert(tp.tab() == 0 && ta.sound == Sound::Tab && !tp.on_tabs());
        push(tp, kPadUp);
        assert(tp.on_tabs());
        tap(kRowX + 40, kRowY + kRowH * 1.5f);
        assert(!tp.on_tabs() && tp.selected() == 1);
        // one tab only: Up from the first row stays there
        Page one;
        one.show(1);
        one.input(pad(0), r, 1);
        one.input(pad(kPadUp), r, 1);
        assert(!one.on_tabs() && one.selected() == 0);
        // the page opened again: on the rows
        tp.input(pad(0), r, 3);
        push(tp, kPadUp), push(tp, kPadUp);
        assert(tp.on_tabs());
        tp.hide();
        tp.show(3);
        assert(!tp.on_tabs());
    }
    // the Graphics tab's choices map onto the Settings window's options (game_settings_graphics.h)
    {
        namespace g = game_settings_graphics;
        assert(g::scale_choice(1.0f) == 0 && g::scale_choice(1.5f) == 1 && g::scale_choice(2.0f) == 2 && g::scale_choice(3.0f) == 3);
        assert(g::scale_choice(1.2f) == 0 && g::scale_choice(2.6f) == 3 && g::scale_choice(0.5f) == 0);
        assert(g::rate_choice(0, 60) == 0 && g::rate_choice(1, 40) == 1 && g::rate_choice(1, 60) == 2);
        assert(g::rate_choice(1, 120) == 3 && g::rate_choice(1, 240) == 4 && g::kRateCount == 6);
        for (int c = 0; c < g::kRateCount; c++) {  // every choice round-trips
            const g::Rate r = g::rate_of(c);
            assert(g::rate_choice(r.mode, r.fps ? r.fps : 60, r.generated) == c);
        }
        assert(g::rate_of(0).mode == 0 && g::rate_of(2).mode == 1 && g::rate_of(2).fps == 60);  // (no True 60 choice)
        // 60 fps (generated): the last choice, at mode 0; every other choice turns it off; while another
        // 60 fps mode runs the page shows that one (frame generation does not run then)
        assert(g::kGenerated == g::kRateCount - 1 && g::rate_of(g::kGenerated).generated && g::rate_of(g::kGenerated).mode == 0);
        for (int c = 0; c < g::kGenerated; c++) assert(!g::rate_of(c).generated);
        assert(g::rate_choice(0, 60, true) == g::kGenerated && g::rate_choice(0, 60, false) == 0);
        assert(g::rate_choice(1, 60, true) == 2 && g::rate_of(g::kRateCount).mode == 0 && !g::rate_of(g::kRateCount).generated);
        assert(g::dyn_choice(0) == 0 && g::dyn_choice(0.75f) == 1 && g::dyn_choice(0.5f) == 2 && g::dyn_choice(0.55f) == 2);
        // its words are translated (French, German, Italian, Spanish), as the other tabs'
        const char* words[] = {"Graphics", "Internal resolution", "Frame rate", "Dynamic resolution", "Bloom", "Edge smoothing (FXAA)",
                               "Anisotropic filtering (16x)", "Clouds", "Cloud shadows", "Full-size torchlight", "Down to 75%",
                               "Down to 50%", "30 fps (original)", "60 fps (generated, experimental)", "Toon (Lite)", "Toon (High)",
                               "40 fps and more add in-between frames; the game logic keeps its 30 steps. Generated: "
                               "every other frame made on the graphics chip."};
        for (const char* w : words)
            for (uint32_t lang = 2; lang <= 5; lang++) assert(game_settings_text::table().count(w) && !game_settings_text::tr(w, lang).empty());
        assert(game_settings_text::tr("Graphics", 3) == "Grafik");
    }
    std::puts("game_settings_page_test: ok");
}
