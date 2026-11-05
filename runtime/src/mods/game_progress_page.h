// SPDX-License-Identifier: MPL-2.0
// The Game progress page (built-in mod "Game progress", game_progress.cpp): opened from its row on the Game tab
// of the item menu's settings page (game_settings_page.h Detail), in the same window and look. Pure: a snapshot of
// the count in, draw commands out (game_progress_page_test).
//
//   Checklist  the ten parts on the left (Up / Down), each with its count and a bar; the selected part's things
//              on the right, collected ones ticked, missing ones dim (Left / Right: more of them when they don't fit).
//   Map        the sea chart's 7 x 7 squares (the island's picture once the fish man has filled the square in),
//              with how many of the square's things are found; the cursor (the cross, or a tap) picks a square and
//              the right side lists its things and how each is found.
//   L / R the two views; Y: only what is still missing; X (map): which things (all, hearts, charts, items,
//   dungeons); B back to the settings.
//
// Idea and layout of the counts from the GreenNaugahyde Android fork (MPL-2.0, db65d60: GameProgress.java,
// ProgressMapView.java), drawn here in the settings page's own look.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "game_progress.h"
#include "game_progress_places.h"
#include "game_settings_page.h"

namespace game_progress_page {

using game_settings_page::Action;
using game_settings_page::Command;
using game_settings_page::Input;
using game_settings_page::Page;
using game_settings_page::Sound;
namespace gs = game_settings_page;

// The picture sheets the driver draws from (game_settings_page.cpp "gs:sheet:"): the icons (one row of kIcon
// squares, by Icon) and the map (7 x 7 squares of kIsland, by sea square - 1)
inline const std::string kIconSheet = "progress-icons", kMapSheet = "progress-map";
inline constexpr int kIconCells = 16;
enum Icon { kIconDungeon, kIconHeart, kIconItems, kIconEquipment, kIconUpgrades, kIconSongs, kIconQuest, kIconCharts,
            kIconSea, kIconGallery, kIconChest, kIconSunken, kIconFound, kIconCount };

struct Snapshot {
    bool valid = false;
    int percent = 0;
    gp_result r{};
    std::vector<std::string> entry;                 // r.e[i]'s name
    std::array<std::string, GP_PARTS> part;         // the parts' names
    std::array<std::string, 50> square;             // the squares' names (1..49)
    std::array<bool, 50> filled{};                  // the fish man filled it in (its picture shows)
    std::array<unsigned char, GP_PLACES> state{};   // gp_place_states
    std::vector<std::string> place;                 // gp_places[i]'s name
    std::array<std::string, GP_S_UNKNOWN + 1> way;  // how it is found ("Chest", "Sunken treasure"...)
    bool icons = false, map_art = false;            // the sheets were made
    struct Texts {
        std::string title = "Game progress", checklist = "Checklist", map = "Map", back = "Back", missing = "Only missing",
                    everything = "Everything", kinds = "Show", none = "Nothing here", found = "found", maybe = "found (not sure which)",
                    all_found = "All found";
        std::array<std::string, 5> groups = {"All", "Hearts", "Charts", "Items", "Dungeons"};
    } texts;
};

// the square's things in a group, in the table's order
inline std::vector<unsigned> square_places(int square, int group) {
    std::vector<unsigned> v;
    for (unsigned i = 0; i < GP_PLACES; i++)
        if (gp_places[i].square == square && gp_place_in_group(&gp_places[i], group)) v.push_back(i);
    return v;
}

class View final : public gs::Detail {
public:
    explicit View(Snapshot s) : s_(std::move(s)) {}

    // the view's state, for tests and the driver's log
    int view() const { return view_; }
    int part() const { return part_; }
    int cell() const { return cell_; }
    int group() const { return group_; }
    bool missing_only() const { return missing_; }
    int entry_page() const { return epage_; }
    void set_snapshot(Snapshot s) { s_ = std::move(s); }

    static constexpr float kX = gs::kWinX, kY = gs::kWinY, kW = gs::kWinW, kH = gs::kWinH;
    // the checklist's columns, the map's grid
    static constexpr float kListX = kX + 50, kListY = kY + 118, kListW = 360, kRow = 44;
    static constexpr float kItemsX = kX + 440, kItemsY = kY + 118, kItemsW = kW - 490, kItemRow = 34;
    static constexpr unsigned kItemsShown = 13;  // per column
    static constexpr float kGridX = kX + 50, kGridY = kY + 112, kCell = 64;
    static constexpr float kSideX = kGridX + 7 * kCell + 30, kSideW = kX + kW - 50 - kSideX;

    Action input(const Input& in) override {
        Action act;
        held_ &= in.buttons;
        const uint32_t live = in.buttons & ~held_;
        const uint32_t pressed = live & ~prev_;
        prev_ = live;
        uint32_t dirs = live & (gs::kPadUp | gs::kPadDown | gs::kPadLeft | gs::kPadRight);
        if (in.ly > 0.5f) dirs |= gs::kPadUp;
        if (in.ly < -0.5f) dirs |= gs::kPadDown;
        if (in.lx < -0.5f) dirs |= gs::kPadLeft;
        if (in.lx > 0.5f) dirs |= gs::kPadRight;
        uint32_t fire = dirs & ~dirs_;
        if (dirs && dirs == dirs_) {
            repeat_ += in.dt;
            if (repeat_ >= 0.35f) fire = dirs, repeat_ -= 0.12f;
        } else {
            repeat_ = 0;
        }
        dirs_ = dirs;
        if (pressed & (gs::kPadB | gs::kPadPlus)) {
            act.closed = true, act.sound = Sound::Close;
            return act;
        }
        if (pressed & (gs::kPadL | gs::kPadR)) {
            view_ ^= 1, act.sound = Sound::Tab, act.id = "view";
            return act;
        }
        if (pressed & kPadY) missing_ = !missing_, epage_ = 0, act.sound = Sound::Change, act.id = "missing";
        if ((pressed & kPadX) && view_ == 1) group_ = (group_ + 1) % 5, act.sound = Sound::Change, act.id = "group";
        if (view_ == 0) {
            const int pages = entry_pages();
            if (fire & gs::kPadUp) step_part(-1, act);
            else if (fire & gs::kPadDown) step_part(+1, act);
            else if ((fire & gs::kPadRight) && epage_ + 1 < pages) epage_++, act.sound = Sound::Cursor;
            else if ((fire & gs::kPadLeft) && epage_ > 0) epage_--, act.sound = Sound::Cursor;
        } else {
            int x = (cell_ - 1) % 7, y = (cell_ - 1) / 7;
            const int ox = x, oy = y;
            if (fire & gs::kPadUp) y = std::max(0, y - 1);
            if (fire & gs::kPadDown) y = std::min(6, y + 1);
            if (fire & gs::kPadLeft) x = std::max(0, x - 1);
            if (fire & gs::kPadRight) x = std::min(6, x + 1);
            if (x != ox || y != oy) cell_ = 1 + y * 7 + x, act.sound = Sound::Cursor;
        }
        // touch: a tap on a tab, a part, a square or the prompts
        const bool tap = !in.touch && touch_ && std::fabs(in_x_ - down_x_) < gs::kTapSlop && std::fabs(in_y_ - down_y_) < gs::kTapSlop;
        if (in.touch && !touch_) down_x_ = in.x, down_y_ = in.y;
        if (in.touch) in_x_ = in.x, in_y_ = in.y;
        touch_ = in.touch;
        if (tap) tapped(in_x_, in_y_, act);
        return act;
    }

    void tick(float dt) override { time_ += dt; }

    std::vector<Command> draw(float (*measure)(const std::string&, float)) const override {
        measure_ = measure;
        std::vector<Command> out;
        out.push_back(Page::rect(0, 0, 1280, 720, 0x0A140A70));
        out.push_back(Page::box("nb:window", kX, kY, kW, kH));
        out.push_back(Page::text(kX + 56, kY + 30, 42, gs::kInk, gs::bold(s_.texts.title)));
        if (s_.valid) {
            const std::string pc = std::to_string(s_.percent) + " %";
            out.push_back(Page::text(kX + 56 + width(gs::bold(s_.texts.title), 42) + 24, kY + 30, 42, gs::kInkSoft, gs::bold(pc)));
        }
        // the two tabs
        const std::string names[2] = {s_.texts.checklist, s_.texts.map};
        float tx = kX + kW - 56;
        for (int i = 1; i >= 0; i--) {
            const float w = std::max(130.0f, width(gs::bold(names[i]), 26) + 40);
            tx -= w;
            tab_x_[i] = tx, tab_w_[i] = w;
            out.push_back(Page::box(i == view_ ? "nb:tab-on" : "nb:tab", tx, kY + 30, w, 50));
            out.push_back(Page::text(tx + (w - width(gs::bold(names[i]), 26)) / 2, kY + 42, 26, i == view_ ? gs::kInk : gs::kInkSoft, gs::bold(names[i])));
            tx -= 10;
        }
        out.push_back(Page::text(kX + 56, kY + 82, 19, gs::kInkSoft, "L / R"));
        if (!s_.valid) {
            out.push_back(Page::text(kX + 60, kY + 140, 28, gs::kInkDim, s_.texts.none));
        } else if (view_ == 0) {
            checklist(out);
        } else {
            map(out);
        }
        prompts(out);
        return out;
    }

    // the number of entry pages of the selected part (two columns of kItemsShown)
    int entry_pages() const {
        const size_t n = entries().size(), per = 2 * kItemsShown;
        return n ? int((n + per - 1) / per) : 1;
    }
    // the shown entries of the selected part (indices into r.e)
    std::vector<unsigned> entries() const {
        std::vector<unsigned> v;
        if (!s_.valid) return v;
        for (unsigned i = s_.r.first[part_]; i < s_.r.first[part_ + 1]; i++)
            if (!missing_ || s_.r.e[i].done < s_.r.e[i].total) v.push_back(i);
        return v;
    }

    static constexpr uint32_t kPadX = 0x2000 /* VPAD X */, kPadY = 0x1000 /* VPAD Y */;

private:
    Snapshot s_;
    int view_ = 0, part_ = 0, cell_ = 11, group_ = 0, epage_ = 0;  // (the map starts on Windfall)
    bool missing_ = false;
    uint32_t prev_ = 0, held_ = ~0u, dirs_ = 0;  // buttons held at the opening (the A of the row) act after release
    float repeat_ = 0, time_ = 0;
    bool touch_ = false;
    float down_x_ = 0, down_y_ = 0, in_x_ = 0, in_y_ = 0;
    mutable float (*measure_)(const std::string&, float) = nullptr;
    mutable float tab_x_[2] = {}, tab_w_[2] = {};

    float width(const std::string& s, float size) const {
        if (measure_) return measure_(s, size);
        size_t n = 0;
        for (char c : s) n += (c & 0xC0) != 0x80;
        return float(n) * size * 0.45f;
    }
    // `s` cut with "..." to `w` page units
    std::string fit(std::string s, float size, float w) const {
        if (width(s, size) <= w) return s;
        while (!s.empty() && width(s + "...", size) > w) {
            unsigned char c;
            do c = static_cast<unsigned char>(s.back()), s.pop_back();
            while (!s.empty() && (c & 0xC0) == 0x80);
        }
        return s + "...";
    }
    static Command sheet(const std::string& name, int cell, int cells_x, int cells_y, float x, float y, float w, float h,
                         uint32_t rgba = 0xFFFFFFFF) {
        Command c = Page::box("", x, y, w, h);
        c.text = "gs:sheet:" + name;
        c.u0 = float(cell % cells_x) / float(cells_x), c.v0 = float(cell / cells_x) / float(cells_y);
        c.u1 = c.u0 + 1.0f / float(cells_x), c.v1 = c.v0 + 1.0f / float(cells_y);
        c.rgba = rgba;
        return c;
    }
    Command icon(int which, float x, float y, float size, bool on = true) const {
        return sheet(kIconSheet, which, kIconCells, 1, x, y, size, size, on ? 0xFFFFFFFF : 0xFFFFFF70);
    }
    static std::string count(unsigned done, unsigned total) { return std::to_string(done) + " / " + std::to_string(total); }

    void step_part(int d, Action& act) {
        const int next = std::clamp(part_ + d, 0, GP_PARTS - 1);
        if (next != part_) part_ = next, epage_ = 0, act.sound = Sound::Cursor;
    }

    void tapped(float x, float y, Action& act) {
        for (int i = 0; i < 2; i++)
            if (gs::inside(x, y, tab_x_[i], kY + 30, tab_w_[i], 50) && view_ != i) {
                view_ = i, act.sound = Sound::Tab;
                return;
            }
        const float py = kY + kH - 66;
        if (y >= py - 10 && x > kX + kW - 260) {  // (the Back prompt)
            act.closed = true, act.sound = Sound::Close;
            return;
        }
        if (view_ == 0) {
            for (int i = 0; i < GP_PARTS; i++)
                if (gs::inside(x, y, kListX, kListY + kRow * float(i), kListW, kRow)) {
                    if (part_ != i) part_ = i, epage_ = 0, act.sound = Sound::Cursor;
                    return;
                }
            if (gs::inside(x, y, kItemsX, kItemsY, kItemsW, kItemRow * kItemsShown) && entry_pages() > 1) {
                epage_ = (epage_ + 1) % entry_pages(), act.sound = Sound::Cursor;
                return;
            }
        } else {
            if (gs::inside(x, y, kGridX, kGridY, 7 * kCell, 7 * kCell)) {
                const int cx = int((x - kGridX) / kCell), cy = int((y - kGridY) / kCell);
                const int c = 1 + std::clamp(cy, 0, 6) * 7 + std::clamp(cx, 0, 6);
                if (c != cell_) cell_ = c, act.sound = Sound::Cursor;
                return;
            }
        }
        if (gs::inside(x, y, kX + 56, py - 4, 330, 48)) missing_ = !missing_, epage_ = 0, act.sound = Sound::Change;
    }

    void checklist(std::vector<Command>& out) const {
        static const int icons[GP_PARTS] = {kIconDungeon, kIconHeart, kIconItems, kIconEquipment, kIconUpgrades,
                                            kIconSongs, kIconQuest, kIconCharts, kIconSea, kIconGallery};
        out.push_back(Page::box("nb:panel", kListX - 14, kListY - 12, kListW + 28, kRow * GP_PARTS + 24));
        for (int i = 0; i < GP_PARTS; i++) {
            const float y = kListY + kRow * float(i);
            const bool sel = i == part_, full = s_.r.done[i] >= s_.r.total[i];
            if (s_.icons) out.push_back(icon(icons[i], kListX + 4, y + 4, 36));
            out.push_back(Page::text(kListX + 50, y + 9, 24, gs::kInk, fit(sel ? gs::bold(s_.part[size_t(i)]) : s_.part[size_t(i)], 24, 222)));
            const std::string c = count(s_.r.done[i], s_.r.total[i]);
            out.push_back(Page::text(kListX + kListW - 8 - width(gs::bold(c), 22), y + 10, 22, full ? gs::kArrowFill : gs::kInkSoft, gs::bold(c)));
            // the bar under the row
            const float bw = kListW - 60, f = s_.r.total[i] ? float(s_.r.done[i]) / float(s_.r.total[i]) : 1.0f;
            out.push_back(Page::rect(kListX + 50, y + kRow - 7, bw, 4, 0x3C6B2E30));
            out.push_back(Page::rect(kListX + 50, y + kRow - 7, bw * f, 4, full ? gs::kArrowFill : gs::kInkSoft));
        }
        // the cursor
        const float b = 5 + 2.0f * std::sin(time_ * 6.2831853f * 0.9f);
        out.push_back(Page::box("nb:cursor", kListX - b, kListY + kRow * float(part_) - b, kListW + 2 * b, kRow + 2 * b));
        // the selected part's things
        out.push_back(Page::box("nb:panel", kItemsX - 14, kItemsY - 12, kItemsW + 28, kItemRow * kItemsShown + 24));
        const std::vector<unsigned> list = entries();
        const size_t per = 2 * kItemsShown, from = size_t(epage_) * per;
        const float colw = list.size() <= kItemsShown ? kItemsW : kItemsW / 2;  // (one column when they fit: room for the names)
        if (list.empty())
            out.push_back(Page::text(kItemsX + 10, kItemsY + 6, 24, gs::kInkSoft, s_.texts.all_found));
        for (size_t k = from; k < list.size() && k < from + per; k++) {
            const unsigned i = list[k];
            const gp_entry& e = s_.r.e[i];
            const float x = kItemsX + colw * float((k - from) / kItemsShown), y = kItemsY + kItemRow * float((k - from) % kItemsShown);
            const bool got = e.done >= e.total;
            // a tick (filled) or a ring (missing); a count for the things with more than one
            if (got) out.push_back(Page::circle(x + 12, y + 16, 8, gs::kArrowFill, true));
            else out.push_back(Page::circle(x + 12, y + 16, 8, gs::kInkDim, false, 2));
            std::string name = i < s_.entry.size() ? s_.entry[i] : "";
            if (e.kind == GP_E_DUNGEON) {  // its five things as marks after the name
                std::string marks;
                for (int b = 0; b < 5; b++)
                    if (e.parts_mask & (1 << b)) marks += (e.got_mask & (1 << b)) ? "\xE2\x97\x8F" : "\xE2\x97\x8B";  // ● ○
                name += " " + marks;
            } else if (e.total > 1) {
                name += "  " + count(e.done, e.total);
            }
            out.push_back(Page::text(x + 28, y + 4, 22, got ? gs::kInk : gs::kInkDim, fit(name, 22, colw - 36)));
        }
        if (entry_pages() > 1) {
            const std::string pg = std::to_string(epage_ + 1) + " / " + std::to_string(entry_pages());
            out.push_back(Page::text(kItemsX + kItemsW - width(pg, 20), kItemsY + kItemRow * kItemsShown + 16, 20, gs::kInkSoft, pg));
        }
    }

    void map(std::vector<Command>& out) const {
        out.push_back(Page::box("nb:panel", kGridX - 10, kGridY - 10, 7 * kCell + 20, 7 * kCell + 20));
        for (int sq = 1; sq <= 49; sq++) {
            const float x = kGridX + kCell * float((sq - 1) % 7), y = kGridY + kCell * float((sq - 1) / 7);
            if (s_.map_art && s_.filled[size_t(sq)]) out.push_back(sheet(kMapSheet, sq - 1, 7, 7, x + 1, y + 1, kCell - 2, kCell - 2));
            else out.push_back(Page::rect(x + 1, y + 1, kCell - 2, kCell - 2, s_.filled[size_t(sq)] ? 0x6FA0C8FF : 0x9DB7C860));
            if (!s_.filled[size_t(sq)]) out.push_back(Page::text(x + kCell / 2 - 7, y + kCell / 2 - 14, 26, 0xFFFFFFC0, "?"));
            out.push_back(Page::line(x, y, x + kCell, y, 0x2F4F2F60, 1));
            out.push_back(Page::line(x, y, x, y + kCell, 0x2F4F2F60, 1));
            unsigned found = 0, total = 0;
            for (unsigned i : square_places(sq, group_)) total++, found += s_.state[i] != GP_MISSING;
            if (!total) continue;
            const bool all = found == total;
            if (missing_ && all) continue;
            const std::string c = missing_ ? std::to_string(total - found) : std::to_string(found) + "/" + std::to_string(total);
            const float w = width(gs::bold(c), 17) + 8;
            out.push_back(Page::rect(x + kCell - w - 2, y + kCell - 22, w, 20, all ? 0x2F7A16E0 : 0x1B2A4AC8));
            out.push_back(Page::text(x + kCell - w + 2, y + kCell - 21, 17, 0xFFFFFFFF, gs::bold(c)));
        }
        out.push_back(Page::line(kGridX + 7 * kCell, kGridY, kGridX + 7 * kCell, kGridY + 7 * kCell, 0x2F4F2F60, 1));
        out.push_back(Page::line(kGridX, kGridY + 7 * kCell, kGridX + 7 * kCell, kGridY + 7 * kCell, 0x2F4F2F60, 1));
        // the cursor on the square
        const float cx = kGridX + kCell * float((cell_ - 1) % 7), cy = kGridY + kCell * float((cell_ - 1) / 7);
        const float b = 3 + 2.0f * std::sin(time_ * 6.2831853f * 0.9f);
        out.push_back(Page::box("nb:cursor", cx - b, cy - b, kCell + 2 * b, kCell + 2 * b));
        // the square's things
        const float sy = kGridY - 10;
        out.push_back(Page::box("nb:panel", kSideX - 14, sy, kSideW + 28, 7 * kCell + 20));
        out.push_back(Page::text(kSideX, sy + 14, 26, gs::kInk, fit(gs::bold(s_.square[size_t(cell_)]), 26, kSideW)));
        const std::string grp = s_.texts.kinds + ": " + s_.texts.groups[size_t(group_)];
        out.push_back(Page::text(kSideX, sy + 48, 19, gs::kInkSoft, grp));
        float y = sy + 80;
        unsigned shown = 0;
        for (unsigned i : square_places(cell_, group_)) {
            if (missing_ && s_.state[i] != GP_MISSING) continue;
            if (y > sy + 7 * kCell - 20) {
                out.push_back(Page::text(kSideX, y, 20, gs::kInkSoft, "..."));
                break;
            }
            const bool got = s_.state[i] != GP_MISSING;
            if (s_.state[i] == GP_FOUND) out.push_back(Page::circle(kSideX + 10, y + 13, 7, gs::kArrowFill, true));
            else if (s_.state[i] == GP_MAYBE) out.push_back(Page::circle(kSideX + 10, y + 13, 7, gs::kArrowFill, false, 3));
            else out.push_back(Page::circle(kSideX + 10, y + 13, 7, gs::kInkDim, false, 2));
            const std::string& way = s_.way[gp_places[i].src < s_.way.size() ? gp_places[i].src : GP_S_UNKNOWN];
            std::string line = i < s_.place.size() ? s_.place[i] : "";
            if (!way.empty()) line += "  \xC2\xB7  " + way;  // ·
            out.push_back(Page::text(kSideX + 26, y + 2, 20, got ? gs::kInk : gs::kInkDim, fit(line, 20, kSideW - 30)));
            y += 28, shown++;
        }
        if (!shown) out.push_back(Page::text(kSideX, y, 20, gs::kInkDim, missing_ ? s_.texts.all_found : s_.texts.none));
    }

    void prompts(std::vector<Command>& out) const {
        namespace font = story_notebook::font;
        const float y = kY + kH - 66;
        // B as the game's button picture; X and Y as letters (the game's font has pictures for A and B only here)
        auto key = [&](float x, const char* letter, const std::string& what) {
            out.push_back(Page::circle(x + 16, y + 20, 16, gs::kInk, true));
            out.push_back(Page::text(x + 16 - width(gs::bold(letter), 22) / 2, y + 8, 22, gs::kWhite, gs::bold(letter)));
            out.push_back(Page::text(x + 40, y + 6, 26, gs::kInk, gs::bold(what)));
            return x + 40 + width(gs::bold(what), 26) + 30;
        };
        float x = kX + 56;
        x = key(x, "Y", missing_ ? s_.texts.everything : s_.texts.missing);
        if (view_ == 1) x = key(x, "X", s_.texts.kinds);
        const float bw = 42 + width(gs::bold(s_.texts.back), 28);
        out.push_back(Page::text(kX + kW - 60 - bw, y, 36, gs::kWhite, font::kGlyphB));
        out.push_back(Page::text(kX + kW - 60 - bw + 42, y + 4, 28, gs::kInk, gs::bold(s_.texts.back)));
    }
};

}  // namespace game_progress_page
