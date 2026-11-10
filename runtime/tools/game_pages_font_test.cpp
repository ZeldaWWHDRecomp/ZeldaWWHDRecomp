// SPDX-License-Identifier: MPL-2.0
// The item menu's own pages (game_settings_page.h, game_progress_page.h) through the game's look
// (game_look.cpp) and the HUD store (guest_hud.h), as the driver (game_settings_page.cpp publish) hands
// them over every read: every line has to come out as a picture of the game's font. A line drawn as a
// Text command is the overlay font's stand-in (the HUD layer draws it with the host font), which the
// player saw when the page's images ran past the store's limits while scrolling.
//
// Scenario: the settings page with eight tabs, each tab's rows scrolled through (L / R and Up / Down),
// then the Game progress page opened from it: its checklist parts and pages, then the map, the cursor
// over all 49 squares in each group and with Y. The menu art is read (window, panels, tab plates), so
// the boxes take their images as in the game.
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mods/game_look.h"
#include "mods/game_progress_names.h"
#include "mods/game_progress_page.h"
#include "mods/game_settings_page.h"

void log_msg(const char* fmt, ...) {
    if (!std::getenv("TLOG")) return;
    va_list a;
    va_start(a, fmt);
    std::vfprintf(stderr, fmt, a);
    va_end(a);
    std::fputc('\n', stderr);
}

namespace {
namespace font = story_notebook::font;
namespace gs = game_settings_page;
using guestmods::hud::Command;
using guestmods::hud::Handle;

const std::string kOwner = "port:game-settings";

// a synthetic font: every character a w x h block, advance w + 2 (the button pictures come from `icons`)
font::Face block_font(const std::vector<uint32_t>& chars, int w, int h) {
    font::Face f;
    f.cell_height = h;
    for (uint32_t cp : chars) {
        game_glyph::Glyph g;
        g.advance = w + 2, g.bitmap.width = cp == ' ' ? 0 : w, g.bitmap.height = h;
        g.bitmap.rgba.assign(size_t(g.bitmap.width) * h * 4, 255);
        f.glyphs[cp] = g;
    }
    return f;
}
game_glyph::Bitmap solid(int w, int h, uint8_t v = 200) {
    game_glyph::Bitmap b;
    b.width = w, b.height = h;
    b.rgba.assign(size_t(w) * h * 4, v);
    return b;
}

struct Driver {
    game_look::Look look{kOwner, "test"};
    std::vector<Handle> sheets;  // the progress page's two sheets and the two page arrows (game_settings_page.cpp)
    size_t reads = 0, fallback_lines = 0;
    std::string first_fallback;

    Driver() {
        std::vector<uint32_t> chars;
        for (uint32_t c = 0x20; c < 0x7F; c++) chars.push_back(c);
        for (uint32_t c : {0xB7u, 0x25CFu, 0x25CBu, 0xE9u}) chars.push_back(c);  // · ● ○ é
        look.main = block_font(chars, 9, 24);
        look.icons = block_font({font::kButtonA, font::kButtonB}, 24, 24);
        story_notebook::style::Art& a = look.art;  // the window, panel and plate art (as read from the item menu)
        for (auto& e : a.edge) e = solid(16, 16);
        a.fibre = solid(16, 16), a.paper = 0xE0D8B0FF;
        a.plate_rim = solid(16, 16), a.plate = solid(16, 16), a.gloss = solid(16, 16);
        auto& store = guestmods::hud::store();
        store.note_pixels(0, 1.5f);  // 1080p
        const std::vector<uint8_t> px(64 * 64 * 4, 255);
        for (int i = 0; i < 4; i++) sheets.push_back(store.create_image(kOwner, 64, 64, 256, px.data(), px.size()));
        look.picture_handle = [this](const std::string& name) -> Handle {
            if (name == "gs:sheet:" + game_progress_page::kIconSheet) return sheets[0];
            if (name == "gs:sheet:" + game_progress_page::kMapSheet) return sheets[1];
            return 0;
        };
    }
    // one read: the commands as publish() hands them to the HUD store
    void publish(std::vector<Command> cmds) {
        auto& store = guestmods::hud::store();
        std::vector<Handle> images;
        std::vector<Command> out = look.render(std::move(cmds), images, 0);
        reads++;
        assert(look.atlas.live_pages() <= look.atlas_pages);  // (each page is an image in the store)
        for (size_t i = 0; i < out.size(); i++) {
            const Command& c = out[i];
            if (c.kind == Command::Text) {  // (with the game's font loaded, only a line whose atlas page has no image)
                if (!fallback_lines) first_fallback = c.text + " (read " + std::to_string(reads) + ")";
                fallback_lines++;
            }
        }
        const Handle list = store.begin(kOwner, 0);
        assert(list);
        look.append(list, out, images);
        assert(store.commit(kOwner, list));
    }
    float measure(const std::string& s, float size) const { return look.measure(s, size); }
};
Driver* g_driver = nullptr;
float measure(const std::string& s, float size) { return g_driver->measure(s, size); }

gs::Input pad(uint32_t b) {
    gs::Input in;
    in.buttons = b, in.dt = 1 / 30.0f;
    return in;
}

// the settings page: eight tabs of a dozen rows, each tab's rows walked down and up
void settings_page(Driver& d) {
    gs::Page page;
    page.measure = measure;
    std::vector<std::string> tabs = {"Camera", "Controls", "Graphics", "Audio", "Game", "Mods", "Help", "Online"};
    auto rows_of = [](int tab) {
        std::vector<gs::Row> rows;
        for (int i = 0; i < 12; i++) {
            gs::Row r;
            r.id = "t" + std::to_string(tab) + "r" + std::to_string(i);
            r.label = "Setting " + std::to_string(i + 1) + " of tab " + std::to_string(tab + 1);
            r.value = i % 3 == 0 ? "On" : std::to_string(10 * i + tab);
            r.toggle = i % 3 == 0, r.on = i % 2, r.number = !r.toggle;
            r.help = "What setting " + std::to_string(i + 1) + " does (tab " + std::to_string(tab + 1) + ").";
            rows.push_back(r);
        }
        return rows;
    };
    page.show(int(tabs.size()));
    auto frame = [&](uint32_t b) {
        const auto rows = rows_of(page.tab());
        page.input(pad(b), rows, int(tabs.size()));
        page.tick(1 / 30.0f);
        d.publish(page.draw(rows, tabs));
    };
    frame(0);
    for (int round = 0; round < 2; round++)
        for (size_t t = 0; t < tabs.size(); t++) {
            for (int i = 0; i < 12; i++) frame(gs::kPadDown), frame(0);
            for (int i = 0; i < 12; i++) frame(gs::kPadUp), frame(0);
            frame(gs::kPadR), frame(0);
        }
}

// the Game progress page from a save with a few things found, every view, part, page and square
void progress_page(Driver& d) {
    using namespace game_progress_page;
    static unsigned char save[GP_SAVE_LEN];
    std::memset(save + GP_ITEMS, 0xFF, 21);
    save[GP_MAX_LIFE + 1] = 15, save[GP_ITEMS + 0] = 0x20, save[GP_FMAP + 10] = 1, save[GP_STAGE_NO] = 0xFF;
    Snapshot s;
    s.valid = true;
    gp_count(save, &s.r);
    s.percent = gp_percent(&s.r);
    gp_place_states(save, s.state.data());
    game_progress_names::Names names;
    for (unsigned i = 0; i < s.r.count; i++) s.entry.push_back(names.entry(s.r.e[i]));
    for (const gp_place& p : gp_places) s.place.push_back(names.place(p));
    for (unsigned sq = 1; sq <= 49; sq++) s.square[sq] = names.square(sq), s.filled[sq] = save[GP_FMAP + sq - 1] & 1;
    for (int p = 0; p < GP_PARTS; p++) s.part[size_t(p)] = game_progress_names::kPartEn[p];
    for (unsigned w = 0; w <= GP_S_UNKNOWN; w++) s.way[w] = "Way " + std::to_string(w) + " (found)";
    s.icons = s.map_art = true;
    View v(s);
    auto frame = [&](uint32_t b) {
        v.input(pad(b));
        v.tick(1 / 30.0f);
        d.publish(v.draw(measure));
    };
    frame(0);
    // the checklist: every part, every page of its things
    for (int part = 0; part < GP_PARTS; part++) {
        for (int p = 1; p < v.entry_pages(); p++) frame(gs::kPadRight), frame(0);
        frame(gs::kPadDown), frame(0);
    }
    frame(View::kPadY), frame(0);
    for (int part = 0; part < GP_PARTS; part++) frame(gs::kPadUp), frame(0);
    frame(View::kPadY), frame(0);
    // the map: each group, the cursor over every square (row by row, there and back)
    frame(gs::kPadR), frame(0);
    assert(v.view() == 1);
    for (int group = 0; group < 5; group++) {
        for (int i = 0; i < 6; i++) frame(gs::kPadUp), frame(0);
        for (int i = 0; i < 6; i++) frame(gs::kPadLeft), frame(0);
        for (int row = 0; row < 7; row++) {
            for (int i = 0; i < 6; i++) frame(row % 2 ? gs::kPadLeft : gs::kPadRight), frame(0);
            frame(gs::kPadDown), frame(0);
        }
        if (group == 2) {  // only the missing ones, once over the grid
            frame(View::kPadY), frame(0);
            for (int i = 0; i < 6; i++) frame(gs::kPadUp), frame(gs::kPadRight), frame(0);
            frame(View::kPadY), frame(0);
        }
        frame(View::kPadX), frame(0);
    }
}
}  // namespace

int main() {
    Driver d;
    g_driver = &d;
    settings_page(d);
    progress_page(d);
    progress_page(d);  // (a second visit: the page opened again)
    std::printf("game_pages_font: %zu reads, %zu atlas pages (%zu bytes), %zu boxes, %zu lines in the overlay font%s%s\n", d.reads,
                d.look.atlas.pages(), d.look.atlas.bytes(), d.look.boxes.size(), d.fallback_lines, d.fallback_lines ? ", first: " : "",
                d.first_fallback.c_str());
    assert(d.fallback_lines == 0);
    std::puts("game_pages_font_test: ok");
    return 0;
}
