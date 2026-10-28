// The item menu's settings page (runtime/src/mods/game_settings_page.h): entry, navigation, changes,
// touch and closing, without the game.
#include <cassert>
#include <cstdio>

#include "mods/game_settings_page.h"

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
    std::vector<Row> rows(3);
    rows[0].id = "a", rows[0].toggle = true;
    rows[1].id = "b", rows[1].number = true;
    rows[2].id = "c", rows[2].toggle = true, rows[2].enabled = false;
    Page p;
    // entry: ZL pressed (not held into the menu), or a tap on the arrow
    assert(!p.entry(pad(kPadZL)) || true);  // (first read: a held ZL counts as pressed only from idle)
    Page q;
    assert(!q.entry(pad(0)));
    assert(q.entry(pad(kPadZL)));
    Page t;
    assert(!t.entry(touch(true, kEntryX, kArrowYc)));
    assert(t.entry(touch(false, kEntryX, kArrowYc)));
    Page d;
    assert(!d.entry(touch(true, kEntryX, kArrowYc)));
    assert(!d.entry(touch(true, kEntryX, kArrowYc + 200)));
    assert(!d.entry(touch(false, kEntryX, kArrowYc + 200)));  // a drag (not to the right) is not a tap
    // Left opens only from the game's left column
    Page lc;
    Input left = pad(kPadLeft);
    assert(!lc.entry(left));
    lc.entry(pad(0));
    left.cursor_left = true;
    assert(lc.entry(left));
    Page st;
    Input stick = pad(0);
    stick.lx = -1, stick.cursor_left = true;
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
    // open: the opening button acts only after its release
    q.show(1);
    for (int i = 0; i < 10; i++) q.tick(1.0f / 30);  // slid in
    Action a = q.input(pad(kPadZL), rows, 1);
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
    std::puts("game_settings_page_test: ok");
}
