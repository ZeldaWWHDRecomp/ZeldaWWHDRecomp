// SPDX-License-Identifier: MPL-2.0
// The Game progress page (runtime/src/mods/game_progress_page.h) and its names (game_progress_names.h): the views,
// the cursor, the filters, touch, B closing, the buttons held at the opening, and the names from message texts.
#include <cassert>
#include <cstdio>
#include <cstring>

#include "mods/game_progress_names.h"
#include "mods/game_progress_page.h"

using namespace game_progress_page;
namespace gs = game_settings_page;

namespace {
Input press(uint32_t b) {
    Input in;
    in.buttons = b, in.dt = 1 / 30.0f;
    return in;
}
// one press: down, then up
Action tap_button(View& v, uint32_t b) {
    Action a = v.input(press(b));
    v.input(press(0));
    return a;
}
size_t count_text(const std::vector<Command>& cmds, const std::string& s) {
    size_t n = 0;
    for (const Command& c : cmds) n += c.kind == Command::Text && c.text.find(s) != std::string::npos;
    return n;
}
}  // namespace

int main() {
    // a save: three pieces of heart, Outset's telescope, Windfall filled in, the bottle of the submarine
    static unsigned char d[GP_SAVE_LEN];
    memset(d + GP_ITEMS, 0xFF, 21);
    d[GP_MAX_LIFE + 1] = 15;
    d[GP_ITEMS + 0] = 0x20;
    d[GP_FMAP + 10] = 1;
    d[GP_STAGE_NO] = 0xFF;
    Snapshot s;
    s.valid = true;
    gp_count(d, &s.r);
    s.percent = gp_percent(&s.r);
    gp_place_states(d, s.state.data());
    game_progress_names::Names names;
    for (unsigned i = 0; i < s.r.count; i++) s.entry.push_back(names.entry(s.r.e[i]));
    for (const gp_place& p : gp_places) s.place.push_back(names.place(p));
    for (unsigned sq = 1; sq <= 49; sq++) s.square[sq] = names.square(sq), s.filled[sq] = d[GP_FMAP + sq - 1] & 1;
    for (int p = 0; p < GP_PARTS; p++) s.part[size_t(p)] = game_progress_names::kPartEn[p];

    View v(s);
    // the A that opened it is still held: nothing happens until it is let go
    assert(v.input(press(gs::kPadA | gs::kPadB)).closed == false);
    v.input(press(0));
    // the checklist: Down to Hearts, the cursor moves with a sound
    Action a = tap_button(v, gs::kPadDown);
    assert(v.part() == 1 && a.sound == Sound::Cursor);
    std::vector<Command> cmds = v.draw(nullptr);
    assert(count_text(cmds, "Piece of Heart") == 1);
    assert(count_text(cmds, "3 / 44") >= 1);
    // Items: the telescope is had; Y shows only the missing ones (the telescope goes)
    tap_button(v, gs::kPadDown);
    assert(v.part() == 2);
    cmds = v.draw(nullptr);
    assert(count_text(cmds, "Telescope") == 1);
    tap_button(v, View::kPadY);
    assert(v.missing_only());
    cmds = v.draw(nullptr);
    assert(count_text(cmds, "Telescope") == 0 && count_text(cmds, "Grappling Hook") == 1);
    tap_button(v, View::kPadY);
    // the gallery: 134 figurines in pages of 26; Right turns them
    for (int i = 0; i < 10; i++) tap_button(v, gs::kPadDown);
    assert(v.part() == GP_PART_GALLERY && v.entry_pages() == 6);
    tap_button(v, gs::kPadRight);
    assert(v.entry_page() == 1);
    tap_button(v, gs::kPadUp);
    assert(v.entry_page() == 0);  // (another part starts at its first page)
    // R: the map, on Windfall; its square lists its things
    a = tap_button(v, gs::kPadR);
    assert(v.view() == 1 && a.sound == Sound::Tab);
    assert(v.cell() == 11);
    cmds = v.draw(nullptr);
    assert(count_text(cmds, "Windfall Island") >= 1);
    size_t windfall_hearts = 0;
    for (unsigned i : square_places(11, GP_G_HEARTS)) windfall_hearts += gp_places[i].kind == GP_K_HEART;
    assert(windfall_hearts > 0);
    // the cross moves the cursor on the grid and stops at the edges
    tap_button(v, gs::kPadUp);
    assert(v.cell() == 4);
    tap_button(v, gs::kPadUp);
    assert(v.cell() == 4);
    tap_button(v, gs::kPadRight);
    assert(v.cell() == 5);
    // X: which things
    tap_button(v, View::kPadX);
    assert(v.group() == 1);
    // a tap on a square picks it (Outset: square 44, column 1, row 6)
    {
        Input t;
        t.dt = 1 / 30.0f, t.touch = true;
        t.x = View::kGridX + View::kCell * 1.5f, t.y = View::kGridY + View::kCell * 6.5f;
        v.input(t);
        t.touch = false;
        v.input(t);
        assert(v.cell() == 44);
    }
    // the sheet pictures carry their names and UVs (the driver resolves "gs:sheet:")
    s.map_art = true;
    v.set_snapshot(s);
    cmds = v.draw(nullptr);
    size_t island = 0;
    for (const Command& c : cmds)
        if (c.kind == Command::Picture && c.text == "gs:sheet:" + kMapSheet) island++, assert(c.u1 > c.u0 && c.v1 > c.v0);
    assert(island == 1);  // (only Windfall is filled in)
    // B closes it
    a = tap_button(v, gs::kPadB);
    assert(a.closed && a.sound == Sound::Close);
    // no save: a short note, no crash
    View none(Snapshot{});
    none.input(press(0));
    assert(count_text(none.draw(nullptr), "Nothing here") == 1);

    // names: the coloured part of a message, the English fallbacks, the charts' printed numbers
    assert(game_progress_names::Names::coloured("You got the *Telescope*!") == "Telescope");
    assert(game_progress_names::Names::coloured("no colour") == "");
    game_progress_names::Names n;
    n.t.message["00133"] = "You got a\n*telescope*!";
    assert(n.item(0x20) == "Telescope");  // (first letter as a capital)
    assert(n.item(0x24) == "Spoils Bag");
    assert(n.chart(25) == "Treasure Chart 29" && n.chart(3) == "Triforce Chart 2" && n.chart(2) == "Treasure Chart 42");
    n.t.places["Topic_Tag_place10"] = "Isla Taura";
    assert(n.square(11) == "Isla Taura" && n.square(44) == "Outset Island");
    n.t.message4["14301"] = "Orca\nBirthplace: Outset";
    assert(n.figurine(0) == "Orca" && n.figurine(1) == "Figurine 2");
    puts("game_progress_page: ok");
    return 0;
}
