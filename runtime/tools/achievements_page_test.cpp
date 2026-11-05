// SPDX-License-Identifier: MPL-2.0
// The Achievements page (runtime/src/mods/achievements_page.h): the scopes, the open-only filter, scrolling, what pauses
// them, B, and the buttons held at the opening.
#include <cassert>
#include <cstdio>
#include <cstring>

#include "mods/achievements_page.h"

using namespace achievements_page;
namespace gs = game_settings_page;

namespace {
Input press(uint32_t b) {
    Input in;
    in.buttons = b, in.dt = 1 / 30.0f;
    return in;
}
Action tap(View& v, uint32_t b) {
    Action a = v.input(press(b));
    v.input(press(0));
    return a;
}
size_t texts(const std::vector<Command>& cmds, const std::string& s) {
    size_t n = 0;
    for (const Command& c : cmds) n += c.kind == Command::Text && c.text.find(s) != std::string::npos;
    return n;
}
}  // namespace

int main() {
    achievements::View v;
    v.file = "1:Link";
    for (const achievements::Goal& g : achievements::kAll) {
        achievements::View::Row r;
        r.key = g.key, r.title = g.title, r.desc = g.desc, r.group = g.group, r.points = g.points;
        if (!strcmp(g.key, "wind_waker")) r.all = r.file = 1000;
        if (!strcmp(g.key, "bombs")) r.all = 2000;  // (another save game earned it)
        v.rows.push_back(r);
    }
    v.pausing = {"Infinite health (on)"};
    View p(v);
    assert(p.input(press(gs::kPadB)).closed == false);  // (held at the opening)
    p.input(press(0));
    assert(p.scope() == 1);  // a save game is in play: its own list first
    std::vector<Command> c = p.draw(nullptr);
    assert(texts(c, "1 of 99 earned") == 1 && texts(c, "Paused: Infinite health (on)") == 1);
    tap(p, gs::kPadR);
    assert(p.scope() == 0);
    c = p.draw(nullptr);
    assert(texts(c, "2 of 99 earned") == 1);
    // only the open ones: 97 rows; scrolling stops at the end
    tap(p, View::kPadY);
    assert(p.open_only() && p.rows().size() == 97);
    for (int i = 0; i < 200; i++) tap(p, gs::kPadDown);
    assert(p.top() == 97 - View::kShown);
    tap(p, gs::kPadUp);
    assert(p.top() == 97 - View::kShown - 1);
    assert(tap(p, gs::kPadB).closed);
    // no save game in play: one scope, no tabs to switch
    achievements::View none = v;
    none.file.clear();
    View q(none);
    q.input(press(0));
    assert(q.scope() == 0);
    tap(q, gs::kPadR);
    assert(q.scope() == 0);
    puts("achievements_page: ok");
    return 0;
}
