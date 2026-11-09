// SPDX-License-Identifier: MPL-2.0
// Link's appearance: the colour page (pure: input in, draw commands out; link_appearance_page_test). The
// driver (link_appearance_port.cpp) holds Link still in a close camera on the left of the screen, plays the
// menu sounds, shows each change on Link at once and draws the commands with the game's menu font and window
// art (game_look.h), as the settings page of the item menu does.
//
//   A window on the right side of the screen: the title, one row per area (hair, skin, eyes, tunic and hat,
//   undershirt, trousers, boots, belt buckle) with its colour (a swatch and the colour's name), then
//   "Original look" and "Done".
//   Up / Down choose a row (with the menus' repeat), Left / Right step through the area's colours (the first is
//   "Original": the game's own colour), A steps forward on an area, resets on "Original look" and closes on
//   "Done" (the look is kept). B closes and puts back the look the page was opened with. The right stick or
//   L / R turn the camera around Link (front and back). Touch: a tap on a row selects it, on its colour's left or
//   right half steps it; the prompts can be tapped.
// Coordinates: a 1280 x 720 page.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "game_settings_page.h"
#include "link_appearance.h"

namespace link_appearance::page {

using game_settings_page::Command;
using game_settings_page::Input;
using game_settings_page::Sound;
namespace gsp = game_settings_page;

inline constexpr float kWinX = 800, kWinY = 30, kWinW = 450, kWinH = 660;
inline constexpr float kRowX = kWinX + 30, kRowY = kWinY + 92, kRowW = kWinW - 60, kRowH = 46;
inline constexpr float kValueW = 220, kValueX = kRowX + kRowW - kValueW;
inline constexpr int kAreaRows = kSlots, kResetRow = kSlots, kDoneRow = kSlots + 1, kRows = kSlots + 2;

struct Action {
    Sound sound = Sound::None;
    bool changed = false;  // the look changed (show it on Link)
    bool closed = false;
    bool keep = false;     // closed with Done: the look is kept (else the opening look is back)
    float turn = 0;        // camera turn this read, -1..1 (left / right)
};

class Page {
public:
    float (*measure)(const std::string& text, float size) = nullptr;
    struct Texts {
        std::string title = "Link's look", original = "Original", custom = "Custom", reset = "Original look", done = "Done",
                    back = "Back", choose = "Choose", turn = "Right stick or L / R: turn";
    } texts;
    // the area names and colour names (translated by the driver); empty: the English ones
    std::vector<std::string> area_names;
    std::vector<std::vector<std::string>> colour_names;

    void open(const Look& now) {
        before_ = look_ = now, open_ = true, selected_ = 0, held_ = ~0u, prev_ = 0, touch_ = false, time_ = 0;
    }
    bool is_open() const { return open_; }
    const Look& look() const { return look_; }
    const Look& before() const { return before_; }
    int selected() const { return selected_; }

    void tick(float dt) { time_ += dt; }

    Action input(const Input& in) {
        Action act;
        if (!open_) return act;
        uint32_t stick = 0;
        if (in.ly > 0.5f) stick |= gsp::kPadUp;
        if (in.ly < -0.5f) stick |= gsp::kPadDown;
        if (in.lx < -0.5f) stick |= gsp::kPadLeft;
        if (in.lx > 0.5f) stick |= gsp::kPadRight;
        held_ &= in.buttons | stick;  // held at the opening (the talk's A): ignored until released
        const uint32_t live = in.buttons & ~held_;
        const uint32_t pressed = live & ~prev_;
        prev_ = live;
        uint32_t dirs = (live | (stick & ~held_)) & (gsp::kPadUp | gsp::kPadDown | gsp::kPadLeft | gsp::kPadRight);
        uint32_t fire = dirs & ~dirs_;
        if (dirs && dirs == dirs_) {
            repeat_ += in.dt;
            if (repeat_ >= 0.35f) fire = dirs, repeat_ -= 0.12f;
        } else {
            repeat_ = 0;
        }
        dirs_ = dirs;
        // turning: the right stick, or L / R held
        act.turn = std::clamp(rx_ + ((live & gsp::kPadL) ? -1.0f : 0.0f) + ((live & gsp::kPadR) ? 1.0f : 0.0f), -1.0f, 1.0f);
        if (pressed & (gsp::kPadB)) return close(act, false);
        if (pressed & gsp::kPadPlus) return close(act, true);
        if (fire & gsp::kPadUp) move(act, -1);
        else if (fire & gsp::kPadDown) move(act, +1);
        else if ((fire & (gsp::kPadLeft | gsp::kPadRight)) && selected_ < kAreaRows) step(act, (fire & gsp::kPadLeft) ? -1 : +1);
        else if (pressed & gsp::kPadA) {
            if (selected_ < kAreaRows) step(act, +1);
            else if (selected_ == kResetRow) reset(act);
            else return close(act, true);
        }
        // touch: a tap (released near where it began)
        if (in.touch && !touch_) down_x_ = in.x, down_y_ = in.y;
        if (in.touch) at_x_ = in.x, at_y_ = in.y;
        const bool tap = !in.touch && touch_ && gsp::near(at_x_, at_y_, down_x_, down_y_, gsp::kTapSlop);
        touch_ = in.touch;
        if (tap) {
            const float py = kWinY + kWinH - 60;
            if (gsp::inside(at_x_, at_y_, kWinX + 20, py - 6, kWinW / 2 - 20, 52)) return close(act, false);
            if (gsp::inside(at_x_, at_y_, kWinX + kWinW / 2, py - 6, kWinW / 2 - 20, 52)) return close(act, true);
            for (int r = 0; r < kRows; r++) {
                const float y = row_y(r);
                if (!gsp::inside(at_x_, at_y_, kRowX, y, kRowW, kRowH)) continue;
                if (r != selected_) selected_ = r, act.sound = Sound::Cursor;
                if (r < kAreaRows && at_x_ >= kValueX) step(act, at_x_ < kValueX + kValueW / 2 ? -1 : +1);
                else if (r == kResetRow) reset(act);
                else if (r == kDoneRow) return close(act, true);
            }
        }
        return act;
    }
    // the right stick's x for the turn (the driver reads it with the buttons)
    void set_right_stick(float rx) { rx_ = std::fabs(rx) > 0.2f ? rx : 0.0f; }

    // the colour name an area shows
    std::string colour_name(Slot s) const {
        const int i = preset_index(look_, s);
        if (i < 0) return texts.custom;
        if (size_t(s) < colour_names.size() && size_t(i) < colour_names[size_t(s)].size()) return colour_names[size_t(s)][size_t(i)];
        return i == 0 ? texts.original : presets(s)[size_t(i)].name;
    }
    std::string area_name(Slot s) const { return size_t(s) < area_names.size() ? area_names[size_t(s)] : slot_label(s); }

    std::vector<Command> draw() const {
        using P = gsp::Page;
        std::vector<Command> out;
        if (!open_) return out;
        out.push_back(P::box("nb:window", kWinX, kWinY, kWinW, kWinH));
        out.push_back(P::text(kWinX + 34, kWinY + 26, 36, gsp::kInk, gsp::bold(texts.title)));
        out.push_back(P::box("nb:panel", kRowX - 12, kRowY - 10, kRowW + 24, kRowH * kRows + 20));
        for (int r = 0; r < kRows; r++) {
            const float y = row_y(r);
            const bool sel = r == selected_;
            if (r && !sel && r != selected_ + 1) out.push_back(P::line(kRowX + 8, y, kRowX + kRowW - 8, y, gsp::kRule, 2));
            if (r < kAreaRows) {
                const Slot s = Slot(r);
                const std::string name = area_name(s);
                out.push_back(P::text(kRowX + 12, y + 13, 24, gsp::kInk, sel ? gsp::bold(name) : name));
                const bool set = look_.has(s);
                out.push_back(P::box(set ? "nb:plate-on" : "nb:tab", kValueX, y + 5, kValueW, kRowH - 10));
                // the swatch: the chosen colour, or a ring for the game's own
                const float cx = kValueX + 40, cy = y + kRowH / 2;
                if (set) {
                    out.push_back(P::circle(cx, cy, 13, 0x00000060, true));
                    out.push_back(P::circle(cx, cy, 11, (look_.colour(s) << 8) | 0xFF, true));
                } else {
                    out.push_back(P::circle(cx, cy, 11, gsp::kInkSoft, false, 2.5f));
                }
                const std::string value = colour_name(s);
                const float vw = width(gsp::bold(value), 21);
                const float vx = kValueX + 58 + (kValueW - 58 - 30 - vw) / 2;
                out.push_back(P::text(vx, y + 15, 21, set ? gsp::kInk : gsp::kInkSoft, gsp::bold(value)));
                chevron(out, kValueX + 16, cy, 7, false);
                chevron(out, kValueX + kValueW - 16, cy, 7, true);
            } else {
                const std::string& label = r == kResetRow ? texts.reset : texts.done;
                const float w = width(gsp::bold(label), 24);
                out.push_back(P::text(kRowX + (kRowW - w) / 2, y + 13, 24, gsp::kInk, sel ? gsp::bold(label) : label));
            }
        }
        {  // the cursor: the game's brackets around the selected row
            const float b = 4 + 2.0f + 2.0f * std::sin(time_ * 6.2831853f * 0.9f);
            out.push_back(P::box("nb:cursor", kRowX - b, row_y(selected_) - b, kRowW + 2 * b, kRowH + 2 * b));
        }
        const float hy = kWinY + kWinH - 60;
        out.push_back(P::text(kWinX + 34, hy - 30, 19, gsp::kInkSoft, texts.turn));
        prompt(out, kWinX + 34, hy, false, texts.back);
        prompt(out, kWinX + kWinW / 2 + 10, hy, true, selected_ < kAreaRows ? texts.choose : selected_ == kResetRow ? texts.reset : texts.done);
        return out;
    }

    static float row_y(int r) { return kRowY + kRowH * float(r); }

private:
    bool open_ = false;
    Look before_, look_;
    int selected_ = 0;
    uint32_t prev_ = 0, held_ = 0, dirs_ = 0;
    float repeat_ = 0, time_ = 0, rx_ = 0;
    bool touch_ = false;
    float down_x_ = 0, down_y_ = 0, at_x_ = 0, at_y_ = 0;

    Action& close(Action& act, bool keep) {
        open_ = false, act.closed = true, act.keep = keep, act.sound = keep ? Sound::Decide : Sound::Cancel;
        if (!keep && look_ != before_) look_ = before_, act.changed = true;
        return act;
    }
    void move(Action& act, int d) {
        const int next = std::clamp(selected_ + d, 0, kRows - 1);
        if (next != selected_) selected_ = next, act.sound = Sound::Cursor;
    }
    void step(Action& act, int d) {
        const Slot s = Slot(selected_);
        const auto& p = presets(s);
        int i = preset_index(look_, s);
        if (i < 0) i = 0;  // a custom colour: the next step goes from "Original"
        i = (i + d + int(p.size())) % int(p.size());
        if (i == 0) look_.clear(s);
        else look_.put(s, p[size_t(i)].rgb);
        act.changed = true, act.sound = Sound::Change;
    }
    void reset(Action& act) {
        if (look_.empty()) { act.sound = Sound::Blocked; return; }
        look_ = Look{}, act.changed = true, act.sound = Sound::Change;
    }
    float width(const std::string& s, float size) const {
        if (measure) return measure(s, size);
        size_t n = 0;
        for (char c : s) n += (c & 0xC0) != 0x80;
        return float(n) * size * 0.45f;
    }
    void prompt(std::vector<Command>& out, float x, float y, bool a, const std::string& what) const {
        out.push_back(gsp::Page::text(x, y, 32, gsp::kWhite, a ? story_notebook::font::kGlyphA : story_notebook::font::kGlyphB));
        out.push_back(gsp::Page::text(x + 38, y + 4, 24, gsp::kInk, gsp::bold(what)));
    }
    static void chevron(std::vector<Command>& out, float cx, float cy, float s, bool right) {
        const float d = right ? 1.0f : -1.0f;
        out.push_back(gsp::Page::line(cx - d * s * 0.5f, cy - s, cx + d * s * 0.5f, cy, gsp::kInk, 3.5f));
        out.push_back(gsp::Page::line(cx + d * s * 0.5f, cy, cx - d * s * 0.5f, cy + s, gsp::kInk, 3.5f));
    }
};

}  // namespace link_appearance::page
