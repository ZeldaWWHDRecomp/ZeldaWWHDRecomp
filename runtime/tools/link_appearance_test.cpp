// SPDX-License-Identifier: MPL-2.0
// Link's appearance (runtime/src/mods/link_appearance.h): the record, the presets, the save's story block
// records, each save file's look (load, change, save, a new game, the page's preview), the colour page's
// input, and the irises' endpoint labels on synthetic BC blocks. Nothing of the game is used.
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "link_appearance.h"
#include "link_appearance_page.h"
#include "link_masks.h"
#include "story_block.h"
#include "story_mail_book.h"

void log_msg(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vfprintf(stdout, fmt, a);
    va_end(a);
    fputc('\n', stdout);
}

namespace la = link_appearance;
using la::Look;
using la::Slot;
using game_settings_page::Sound;

static int g_checks = 0;
#define CHECK(x) (g_checks++, (x) ? (void)0 : (fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x), std::abort()))

static void record() {
    Look empty;
    auto e = la::encode(empty);
    CHECK(e.size() == 3 && e[0] == 1 && e[1] == 0 && e[2] == 0);
    Look back;
    CHECK(la::decode(e.data(), e.size(), back) && back.empty());

    Look l;
    l.put(Slot::kHair, 0x2B2A30), l.put(Slot::kTunic, 0xE52B36), l.put(Slot::kBuckle, 0xC8CCD4);
    auto r = la::encode(l);
    CHECK(r.size() == 3 + 9);
    CHECK(la::decode(r.data(), r.size(), back) && back == l);
    // every slot: the largest record
    Look all;
    for (int i = 0; i < la::kSlots; i++) all.put(Slot(i), 0x010203u * uint32_t(i + 1));
    r = la::encode(all);
    CHECK(r.size() == la::kMaxRecord);
    CHECK(la::decode(r.data(), r.size(), back) && back == all);
    // refused: another version, an unknown slot bit, short or long records, null
    std::vector<uint8_t> bad = la::encode(l);
    bad[0] = 2;
    CHECK(!la::decode(bad.data(), bad.size(), back) && back.empty());
    bad = la::encode(l), bad[1] |= 0x80;
    CHECK(!la::decode(bad.data(), bad.size(), back));
    bad = la::encode(l), bad.pop_back();
    CHECK(!la::decode(bad.data(), bad.size(), back));
    bad = la::encode(l), bad.push_back(0);
    CHECK(!la::decode(bad.data(), bad.size(), back));
    CHECK(!la::decode(nullptr, 0, back));
    // equality ignores the colours of slots that are not set
    Look a, b;
    a.put(Slot::kSkin, 0x123456), a.clear(Slot::kSkin);
    CHECK(a == b && a.empty());
    // text
    CHECK(la::to_text(l) == "hair=2B2A30,tunic=E52B36,buckle=C8CCD4");
    Look t;
    CHECK(la::from_text("tunic=e52b36, hair=2B2A30", t) == false);  // (no spaces)
    CHECK(la::from_text("tunic=e52b36,hair=2B2A30,buckle=C8CCD4", t) && t == l);
    CHECK(!la::from_text("cape=FFFFFF", t));
    CHECK(!la::from_text("hair=FFF", t));
    CHECK(la::from_text("", t) && t.empty());
}

static void presets() {
    std::set<std::string> ids;
    for (int i = 0; i < la::kSlots; i++) {
        const Slot s = Slot(i);
        Slot back;
        CHECK(la::slot_of(la::slot_id(s), back) && back == s);
        ids.insert(la::slot_id(s));
        const auto& p = la::presets(s);
        CHECK(p.size() >= 5 && std::string(p[0].name) == "Original" && p[0].rgb == 0);
        std::set<uint32_t> colours;
        for (size_t k = 1; k < p.size(); k++) {
            CHECK(p[k].rgb && p[k].rgb <= 0xFFFFFF);
            CHECK(colours.insert(p[k].rgb).second);  // each colour once per area
            Look l;
            l.put(s, p[k].rgb);
            CHECK(la::preset_index(l, s) == int(k));
        }
        Look custom;
        custom.put(s, 0x010203);
        CHECK(la::preset_index(custom, s) == -1 && la::preset_index(Look{}, s) == 0);
        CHECK(!la::parts_of(s).empty());
        for (uint8_t part : la::parts_of(s)) CHECK(part > 0 && part < link_masks::kParts);
    }
    CHECK(ids.size() == size_t(la::kSlots));
}

static void areas() {
    std::vector<uint8_t> area(story_block::kAreaSize, 0);
    CHECK(la::read_area(area.data()).empty());
    // other records stay: a quest step and a long name record around ours
    story_mail::Book book(area.data(), story_block::kAreaRecords);
    CHECK(book.set(story_mail::kRecordQuest, 0x1234, 3));
    CHECK(book.set(story_mail::kRecordLongName, 0x100, 9));
    Look l;
    l.put(Slot::kEyes, 0x2F6FD8), l.put(Slot::kSkin, 0xE2B07A);
    CHECK(la::write_area(area.data(), l));
    CHECK(la::read_area(area.data()) == l);
    CHECK(book.get(story_mail::kRecordQuest, 0x1234) == 3 && book.get(story_mail::kRecordLongName, 0x100) == 9);
    // another look over it; then none: our records go, the others stay
    Look m;
    m.put(Slot::kTunic, 0x34343C);
    CHECK(la::write_area(area.data(), m) && la::read_area(area.data()) == m);
    CHECK(la::write_area(area.data(), Look{}) && la::read_area(area.data()).empty());
    unsigned ours = 0;
    for (unsigned r = 0; r < story_block::kAreaRecords; r++) ours += book.record_kind(r) == story_mail::kRecordAppearance;
    CHECK(ours == 0 && book.get(story_mail::kRecordQuest, 0x1234) == 3);
    // no room: false and nothing half written
    std::vector<uint8_t> full(story_block::kAreaSize, 0);
    story_mail::Book fb(full.data(), story_block::kAreaRecords);
    for (unsigned k = 0; fb.free_records() > 3; k++) CHECK(fb.set(story_mail::kRecordQuest, uint16_t(k + 1), 1));
    CHECK(!la::write_area(full.data(), l));
    CHECK(la::read_area(full.data()).empty());
    // data this port did not write (no header): left alone
    std::vector<uint8_t> foreign(story_block::kAreaSize, 0);
    foreign[0] = 0x77, foreign[9] = 1;
    CHECK(!la::write_area(foreign.data(), l) && foreign[0] == 0x77);
    // a damaged record (a byte missing): no look
    CHECK(la::write_area(area.data(), l));
    CHECK(book.erase(story_mail::kRecordAppearance, 4));
    CHECK(la::read_area(area.data()).empty());
}

static void save_files() {
    // cking.sav's block: file 1 with a look, file 2 without
    std::vector<uint8_t> payload(story_block::kPayloadSize, 0);
    Look one;
    one.put(Slot::kHair, 0x6E4626);
    CHECK(la::write_area(payload.data(), one));
    la::loaded(payload.data());
    CHECK(la::store::frame(0, false) == one && la::current() == one && la::current_file() == 0);
    CHECK(la::store::frame(1, false).empty());
    CHECK(la::store::frame(-1, false).empty() && la::current().empty());
    std::string why;
    CHECK(!la::set_current(one, why) && !why.empty());
    // a change in file 2, kept in the save when the game writes it
    la::store::frame(1, false);
    Look two;
    two.put(Slot::kTunic, 0xE52B36), two.put(Slot::kEyes, 0x3FA34D);
    CHECK(la::set_current(two, why) && la::current() == two);
    std::vector<uint8_t> areas(story_block::kPayloadSize, 0);
    la::written(areas.data());
    CHECK(la::read_area(areas.data()) == one);
    CHECK(la::read_area(areas.data() + story_block::kAreaSize) == two);
    CHECK(la::read_area(areas.data() + 2 * story_block::kAreaSize).empty());
    // read back (another session)
    la::loaded(nullptr);
    CHECK(la::store::frame(1, false).empty());
    la::loaded(areas.data());
    CHECK(la::store::frame(1, false) == two);
    // the page's preview is shown, the file's look stays until the page keeps it
    Look pv;
    pv.put(Slot::kBoots, 0x2E2A28);
    la::store::preview(true, pv);
    CHECK(la::store::frame(1, false) == pv && la::shown() == pv && la::current() == two);
    la::store::preview(false, {});
    CHECK(la::store::frame(1, false) == two && la::shown() == two);
    // a new game in file 1 (its saved look): Link's own colours, once; a look chosen then stays
    CHECK(la::store::frame(0, true).empty());
    CHECK(la::set_current(pv, why));
    CHECK(la::store::frame(0, true) == pv);
    // the new game saved: written with its new look
    la::written(areas.data());
    CHECK(la::read_area(areas.data()) == pv);
    // no save file: nothing written that wasn't there; a file without a look keeps no record
    la::loaded(nullptr);
    std::vector<uint8_t> none(story_block::kPayloadSize, 0);
    la::written(none.data());
    CHECK(story_block::all_zero(none.data(), none.size()));
}

static void page() {
    namespace gsp = game_settings_page;
    namespace pg = la::page;
    pg::Page p;
    Look start;
    start.put(Slot::kTunic, 0xE52B36);  // a preset (Red)
    gsp::Input in;
    in.dt = 1.0f / 60;
    auto press = [&](uint32_t b) {
        in.buttons = b;
        pg::Action a = p.input(in);
        in.buttons = 0;
        pg::Action r = p.input(in);  // (released)
        if (r.closed) a = r;
        return a;
    };
    // A held from the talk: ignored until released
    in.buttons = gsp::kPadA;
    p.open(start);
    CHECK(p.is_open() && p.input(in).sound == Sound::None && p.look() == start);
    in.buttons = 0, p.input(in);
    // Right on hair: the first preset; Left twice: Original, then the last preset
    pg::Action a = press(gsp::kPadRight);
    CHECK(a.changed && p.look().colour(Slot::kHair) == la::presets(Slot::kHair)[1].rgb);
    press(gsp::kPadLeft);
    CHECK(!p.look().has(Slot::kHair));
    press(gsp::kPadLeft);
    CHECK(p.look().colour(Slot::kHair) == la::presets(Slot::kHair).back().rgb);
    // down to the tunic (row 3): A steps it from Red to the next preset
    press(gsp::kPadDown), press(gsp::kPadDown), press(gsp::kPadDown);
    CHECK(p.selected() == int(Slot::kTunic));
    press(gsp::kPadA);
    CHECK(la::preset_index(p.look(), Slot::kTunic) == 2);
    // B: back to the look the page was opened with
    a = press(gsp::kPadB);
    CHECK(a.closed && !a.keep && a.changed && p.look() == start && !p.is_open());
    // reset row then Done: kept, and nothing set
    p.open(start);
    in.buttons = 0, p.input(in);
    for (int i = 0; i < pg::kResetRow; i++) press(gsp::kPadDown);
    CHECK(p.selected() == pg::kResetRow);
    a = press(gsp::kPadA);
    CHECK(a.changed && p.look().empty());
    a = press(gsp::kPadA);
    CHECK(a.sound == Sound::Blocked);  // (nothing left to reset)
    press(gsp::kPadDown), press(gsp::kPadDown);  // (stops at Done)
    CHECK(p.selected() == pg::kDoneRow);
    a = press(gsp::kPadA);
    CHECK(a.closed && a.keep && p.look().empty());
    // turning: the right stick and L / R
    p.open(Look{});
    in.buttons = 0, p.input(in);
    p.set_right_stick(-0.9f);
    CHECK(p.input(in).turn < -0.8f);
    p.set_right_stick(0.1f);  // (dead zone)
    in.buttons = gsp::kPadR;
    CHECK(p.input(in).turn == 1.0f);
    in.buttons = 0;
    // touch: a tap on hair's colour, right half, steps it
    in.touch = true, in.x = pg::kValueX + pg::kValueW - 10, in.y = pg::Page::row_y(0) + 10;
    p.input(in);
    in.touch = false;
    a = p.input(in);
    CHECK(a.changed && p.look().has(Slot::kHair));
    // drawing: the window, a row per area, colour names, the cursor
    const auto cmds = p.draw();
    unsigned texts = 0;
    bool window = false;
    for (const auto& c : cmds) {
        texts += c.kind == gsp::Command::Text;
        window |= c.kind == gsp::Command::Picture && c.text.rfind("nb:window@", 0) == 0;
        CHECK(c.x >= pg::kWinX - 10 && c.x <= 1280);  // all on the right side
    }
    CHECK(window && texts >= unsigned(pg::kRows) + 3);
}

static void irises() {
    // BC3 blocks (16 bytes, colour half at +8): white, iris teal, black pupil, half iris half white
    auto block = [](uint16_t c0, uint16_t c1) {
        std::vector<uint8_t> b(16, 0);
        b[0] = 255, b[1] = 255;
        b[8] = uint8_t(c0), b[9] = uint8_t(c0 >> 8), b[10] = uint8_t(c1), b[11] = uint8_t(c1 >> 8);
        return b;
    };
    auto rgb565 = [](int r, int g, int b) { return uint16_t((r * 31 / 255) << 11 | (g * 63 / 255) << 5 | (b * 31 / 255)); };
    const uint16_t white = rgb565(250, 250, 250), teal = rgb565(26, 90, 74), black = rgb565(0, 0, 0);
    std::vector<uint8_t> img;
    for (auto b : {block(white, white), block(teal, teal), block(teal, black), block(white, teal)}) img.insert(img.end(), b.begin(), b.end());
    const auto e = link_masks::label_endpoints_by_colour(3, img.data(), uint32_t(img.size()), nullptr, 0, link_masks::Part::kEyes);
    const uint8_t E = uint8_t(link_masks::Part::kEyes), N = 0;
    CHECK((e == std::vector<uint8_t>{N, N, E, E, E, N, N, E}));
    CHECK(link_masks::iris_colour(26, 90, 74) && !link_masks::iris_colour(250, 250, 250) && !link_masks::iris_colour(0, 0, 0) &&
          !link_masks::iris_colour(128, 128, 128));
    // tint moves the iris endpoints only: the pupil's black and the white stay
    link_masks::Mask m;
    m.texture = "hitomi", m.width = 8, m.height = 8, m.format = 0x433, m.image_size = uint32_t(img.size());
    m.endpoints = e;
    m.base_rgb[E] = 0x1A5A4A;
    std::vector<uint8_t> out = img;
    link_masks::tint(m, link_masks::Part::kEyes, 0x2F6FD8, out.data(), nullptr);
    auto c = [&](int blk, int k) { return uint16_t(out[16 * blk + 8 + 2 * k] | out[16 * blk + 9 + 2 * k] << 8); };
    CHECK(c(0, 0) == white && c(0, 1) == white);
    CHECK(c(1, 0) != teal && (c(1, 0) & 31) > ((c(1, 0) >> 11) & 31));  // blue now
    CHECK(c(2, 1) == black);
    CHECK(c(3, 0) == white && c(3, 1) != teal);
}

int main() {
    record();
    presets();
    areas();
    save_files();
    page();
    irises();
    printf("link_appearance_test: %d checks passed\n", g_checks);
    return 0;
}
