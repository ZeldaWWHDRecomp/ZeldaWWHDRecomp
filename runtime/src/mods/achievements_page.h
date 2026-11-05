// SPDX-License-Identifier: MPL-2.0
// The Achievements page (built-in mod "Achievements", achievements.cpp): opened from its row on the Game tab of the
// item menu's settings page (game_settings_page.h Detail). Pure: achievements_page_test.
//   L / R: all save games or this save game; Up / Down: the goals by group (earned ones ticked, with their points);
//   Y: only the ones still open; B: back. A line at the top names what pauses them now (cheats), if anything.
// Idea and texts from the GreenNaugahyde Android fork (MPL-2.0, db65d60: Achievements.java and its strings).
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "achievements_host.h"
#include "game_settings_page.h"

namespace achievements_page {

using game_settings_page::Action;
using game_settings_page::Command;
using game_settings_page::Input;
using game_settings_page::Page;
using game_settings_page::Sound;
namespace gs = game_settings_page;

class View final : public gs::Detail {
public:
    explicit View(achievements::View v) : v_(std::move(v)) { scope_ = v_.file.empty() ? 0 : 1; }

    static constexpr float kX = gs::kWinX, kY = gs::kWinY, kW = gs::kWinW, kH = gs::kWinH;
    static constexpr float kListX = kX + 50, kListY = kY + 150, kListW = kW - 100, kRow = 50;
    static constexpr unsigned kShown = 8;
    static constexpr uint32_t kPadY = 0x1000;

    int scope() const { return scope_; }
    bool open_only() const { return open_only_; }
    unsigned top() const { return top_; }
    // the rows shown: indices into v_.rows, by group
    std::vector<unsigned> rows() const {
        std::vector<unsigned> out;
        for (int g = 0; g < achievements::kGroups; g++)
            for (unsigned i = 0; i < v_.rows.size(); i++)
                if (v_.rows[i].group == g && (!open_only_ || !earned(v_.rows[i]))) out.push_back(i);
        return out;
    }

    Action input(const Input& in) override {
        Action act;
        held_ &= in.buttons;
        const uint32_t live = in.buttons & ~held_, pressed = live & ~prev_;
        prev_ = live;
        uint32_t dirs = live & (gs::kPadUp | gs::kPadDown);
        if (in.ly > 0.5f) dirs |= gs::kPadUp;
        if (in.ly < -0.5f) dirs |= gs::kPadDown;
        uint32_t fire = dirs & ~dirs_;
        if (dirs && dirs == dirs_) {
            repeat_ += in.dt;
            if (repeat_ >= 0.35f) fire = dirs, repeat_ -= 0.08f;
        } else {
            repeat_ = 0;
        }
        dirs_ = dirs;
        if (pressed & (gs::kPadB | gs::kPadPlus)) return act.closed = true, act.sound = Sound::Close, act;
        if ((pressed & (gs::kPadL | gs::kPadR)) && !v_.file.empty()) scope_ ^= 1, top_ = 0, act.sound = Sound::Tab;
        if (pressed & kPadY) open_only_ = !open_only_, top_ = 0, act.sound = Sound::Change;
        const unsigned n = unsigned(rows().size()), last = n > kShown ? n - kShown : 0;
        if ((fire & gs::kPadDown) && top_ < last) top_++, act.sound = Sound::Cursor;
        if ((fire & gs::kPadUp) && top_ > 0) top_--, act.sound = Sound::Cursor;
        // touch: a tap on the lower half of the list scrolls down a page, on the upper half up; the tabs; Back
        const bool tap = !in.touch && touch_ && std::fabs(in_x_ - down_x_) < gs::kTapSlop && std::fabs(in_y_ - down_y_) < gs::kTapSlop;
        if (in.touch && !touch_) down_x_ = in.x, down_y_ = in.y;
        if (in.touch) in_x_ = in.x, in_y_ = in.y;
        touch_ = in.touch;
        if (tap) {
            if (in_y_ >= kY + kH - 76 && in_x_ > kX + kW - 260) return act.closed = true, act.sound = Sound::Close, act;
            if (in_y_ >= kY + kH - 76 && in_x_ < kX + 400) open_only_ = !open_only_, top_ = 0, act.sound = Sound::Change;
            else if (gs::inside(in_x_, in_y_, kListX, kListY, kListW, kRow * kShown)) {
                const bool down = in_y_ > kListY + kRow * kShown / 2;
                top_ = down ? std::min(last, top_ + kShown) : (top_ > kShown ? top_ - kShown : 0), act.sound = Sound::Cursor;
            } else if (in_y_ < kY + 90 && in_x_ > kX + kW / 2 && !v_.file.empty()) {
                scope_ ^= 1, top_ = 0, act.sound = Sound::Tab;
            }
        }
        return act;
    }
    void tick(float dt) override { time_ += dt; }

    std::vector<Command> draw(float (*measure)(const std::string&, float)) const override {
        measure_ = measure;
        std::vector<Command> out;
        out.push_back(Page::rect(0, 0, 1280, 720, 0x0A140A70));
        out.push_back(Page::box("nb:window", kX, kY, kW, kH));
        out.push_back(Page::text(kX + 56, kY + 30, 42, gs::kInk, gs::bold("Achievements")));
        // the scopes as tabs
        const std::string names[2] = {"All save games", "This save game"};
        float tx = kX + kW - 56;
        for (int i = 1; i >= 0; i--) {
            if (i == 1 && v_.file.empty()) continue;
            const float w = std::max(150.0f, width(gs::bold(names[i]), 24) + 36);
            tx -= w;
            out.push_back(Page::box(i == scope_ ? "nb:tab-on" : "nb:tab", tx, kY + 30, w, 50));
            out.push_back(Page::text(tx + (w - width(gs::bold(names[i]), 24)) / 2, kY + 43, 24, i == scope_ ? gs::kInk : gs::kInkSoft, gs::bold(names[i])));
            tx -= 10;
        }
        // the totals, and what pauses them
        unsigned earned_n = 0, points = 0, all_points = 0;
        for (const auto& r : v_.rows) {
            all_points += r.points;
            if (earned(r)) earned_n++, points += r.points;
        }
        out.push_back(Page::text(kX + 56, kY + 84, 22, gs::kInkSoft,
                                 std::to_string(earned_n) + " of " + std::to_string(v_.rows.size()) + " earned \xC2\xB7 " + std::to_string(points) +
                                     " of " + std::to_string(all_points) + " points"));
        if (!v_.pausing.empty()) {
            std::string p = "Paused: ";
            for (size_t i = 0; i < v_.pausing.size(); i++) p += (i ? ", " : "") + v_.pausing[i];
            out.push_back(Page::text(kX + 56, kY + 112, 20, 0xA02818FF, fit(p, 20, kW - 120)));
        }
        out.push_back(Page::box("nb:panel", kListX - 14, kListY - 12, kListW + 28, kRow * kShown + 24));
        const std::vector<unsigned> list = rows();
        int last_group = -1;
        for (unsigned k = top_; k < list.size() && k < top_ + kShown; k++) {
            const auto& r = v_.rows[list[k]];
            const float y = kListY + kRow * float(k - top_);
            const bool got = earned(r);
            if (k > top_) out.push_back(Page::line(kListX + 10, y, kListX + kListW - 10, y, gs::kRule, 2));
            if (got) out.push_back(Page::circle(kListX + 18, y + 25, 10, gs::kArrowFill, true));
            else out.push_back(Page::circle(kListX + 18, y + 25, 10, gs::kInkDim, false, 2));
            out.push_back(Page::text(kListX + 42, y + 4, 24, got ? gs::kInk : gs::kInkSoft, fit(gs::bold(r.title), 24, 360)));
            out.push_back(Page::text(kListX + 42, y + 28, 18, got ? gs::kInkSoft : gs::kInkDim, fit(r.desc, 18, kListW - 200)));
            const std::string pts = std::to_string(r.points);
            out.push_back(Page::text(kListX + kListW - 20 - width(gs::bold(pts), 22), y + 12, 22, got ? gs::kArrowFill : gs::kInkDim, gs::bold(pts)));
            if (int(r.group) != last_group) {  // the group's name at the right of its first row
                last_group = r.group;
                const std::string g = achievements::kGroupEn[r.group];
                out.push_back(Page::text(kListX + kListW - 80 - width(g, 18), y + 6, 18, gs::kInkDim, g));
            }
        }
        if (list.empty()) out.push_back(Page::text(kListX + 10, kListY + 10, 24, gs::kInkSoft, "All earned."));
        if (list.size() > kShown) {  // a scroll bar
            const float x = kListX + kListW + 8, h = kRow * kShown - 8, y = kListY + 4;
            out.push_back(Page::rect(x - 1, y, 9, h, 0x3C6B2E40));
            const float th = std::max(24.0f, h * float(kShown) / float(list.size()));
            out.push_back(Page::rect(x, y + (h - th) * float(top_) / float(list.size() - kShown), 7, th, gs::kInk));
        }
        // the prompts
        namespace font = story_notebook::font;
        const float py = kY + kH - 66;
        out.push_back(Page::circle(kX + 56 + 16, py + 20, 16, gs::kInk, true));
        out.push_back(Page::text(kX + 56 + 16 - width(gs::bold("Y"), 22) / 2, py + 8, 22, gs::kWhite, gs::bold("Y")));
        out.push_back(Page::text(kX + 56 + 40, py + 6, 26, gs::kInk, gs::bold(open_only_ ? "Show all" : "Only open ones")));
        const float bw = 42 + width(gs::bold("Back"), 28);
        out.push_back(Page::text(kX + kW - 60 - bw, py, 36, gs::kWhite, font::kGlyphB));
        out.push_back(Page::text(kX + kW - 60 - bw + 42, py + 4, 28, gs::kInk, gs::bold("Back")));
        return out;
    }

private:
    achievements::View v_;
    int scope_ = 0;
    bool open_only_ = false;
    unsigned top_ = 0;
    uint32_t prev_ = 0, held_ = ~0u, dirs_ = 0;
    float repeat_ = 0, time_ = 0;
    bool touch_ = false;
    float down_x_ = 0, down_y_ = 0, in_x_ = 0, in_y_ = 0;
    mutable float (*measure_)(const std::string&, float) = nullptr;

    bool earned(const achievements::View::Row& r) const { return (scope_ == 0 ? r.all : r.file) != 0; }
    float width(const std::string& s, float size) const {
        if (measure_) return measure_(s, size);
        size_t n = 0;
        for (char c : s) n += (c & 0xC0) != 0x80;
        return float(n) * size * 0.45f;
    }
    std::string fit(std::string s, float size, float w) const {
        if (width(s, size) <= w) return s;
        while (!s.empty() && width(s + "...", size) > w) {
            unsigned char c;
            do c = static_cast<unsigned char>(s.back()), s.pop_back();
            while (!s.empty() && (c & 0xC0) == 0x80);
        }
        return s + "...";
    }
};

}  // namespace achievements_page
