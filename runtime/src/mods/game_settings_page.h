// The game-style settings page of the item menu (prototype, p9-gamesettings; idea from the
// GreenNaugahyde Android fork 0.9.6+, whose options page slides in left of the item page; this is
// our own implementation, drawn from C++ on every platform). Pure code: input in, draw commands
// out; the driver (game_settings_page.cpp) reads the game's state, plays the menu sounds, applies
// the changes (game_settings.h) and hands the commands to the HUD layer, which draws them with the
// game's font and window art (story_notebook_font.h / story_notebook_style.h), read at run time from
// the player's game.
//
//   Closed  over the item menu's items page: a page arrow at the left edge (the mirror of the game's
//           own arrow to the right page). A tap / click on it, or ZL, slides the page in from the left.
//   Open    the tabs (L / R), one row per setting (Up / Down, the left stick too, with repeat), Left /
//           Right change the value (a toggle follows the direction, a number steps), A toggles or steps
//           up, B, + or a tap on the right page arrow slide the page back out to the items. Touch:
//           a tap on a row selects it, a tap on its value changes it (left half of a number: down).
// Coordinates: a 1280 x 720 page (fit_to_screen scales it to the GamePad's 854 x 480).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "guest_hud.h"
#include "story_notebook_font.h"
#include "story_notebook_style.h"

struct Cpu;
namespace input { struct PadState; }

namespace game_settings_page {

using guestmods::hud::Command;

inline constexpr uint32_t kPadA = 0x8000, kPadB = 0x4000, kPadUp = 0x0200, kPadDown = 0x0100, kPadLeft = 0x0800,
                          kPadRight = 0x0400, kPadL = 0x0020, kPadR = 0x0010, kPadZL = 0x0080, kPadPlus = 0x0008;

// geometry (1280 x 720)
inline constexpr float kWinX = 70, kWinY = 34, kWinW = 1060, kWinH = 652;
inline constexpr float kRowX = kWinX + 60, kRowY = kWinY + 122, kRowW = kWinW - 120, kRowH = 56;
inline constexpr unsigned kRowsShown = 7;
inline constexpr float kValueW = 300, kValueX = kRowX + kRowW - kValueW - 10;
// The page arrows: the game's own right arrow (its place and size; the driver reads them from the item
// screen, these are the USA layout's), ours its mirror at the left edge (closed), and while the page is
// open the arrow back to the items where the game's right arrow is (which is hidden then).
struct ArrowPlace { float x = 1230, y = 360, w = 80, h = 160; };
inline constexpr float kArrowSlop = 24;
inline constexpr float kTapSlop = 24;

// the game's menu ink and paper (as the notebook's window)
inline constexpr uint32_t kInk = 0x003800FF, kInkSoft = 0x3C6B2EFF, kInkDim = 0x7C8A6CFF, kWhite = 0xFFFFFFFF,
                          kRule = 0xA9D08AFF, kArrowFill = 0x2F7A16FF, kArrowRim = 0xF4FFE4FF;

inline const std::string kBold = story_notebook::font::kBold;
inline std::string bold(const std::string& s) { return kBold + s; }

// One row as the driver gives it (from game_settings.h)
struct Row {
    std::string id, label, value, help;
    bool number = false, toggle = false, choice = false, on = false, enabled = true;
    bool stepped() const { return number || choice; }  // Left / Right step it (chevrons on its plate)
};

struct Input {
    uint32_t buttons = 0;  // VPAD bits (the Wii U's A / B / ...: the face layout is applied before)
    float lx = 0, ly = 0;
    bool touch = false;
    float x = 0, y = 0;    // page coordinates
    float dt = 0;          // seconds since the last input
    bool cursor_left = false;  // the game's cursor is in the items page's left column (Left from there opens the page)
};

enum class Sound : uint8_t { None, Cursor, Change, Decide, Cancel, Tab, Open, Close, Blocked };

struct Action {
    Sound sound = Sound::None;
    std::string id;     // the row to change
    int direction = 0;  // -1 / +1, 0: toggle (A)
    bool closed = false;
};

inline float ease_out(float t) { t = std::clamp(t, 0.0f, 1.0f); return 1 - (1 - t) * (1 - t) * (1 - t); }
inline bool inside(float x, float y, float bx, float by, float bw, float bh) { return x >= bx && x < bx + bw && y >= by && y < by + bh; }
inline bool near(float x, float y, float cx, float cy, float r) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r; }

class Page {
public:
    float (*measure)(const std::string& text, float size) = nullptr;  // the game's font, when loaded
    ArrowPlace game_arrow;  // the game's right page arrow
    // the page's own words (the driver translates them: game_settings_text.h)
    struct Texts { std::string title = "Settings", change = "Change", back = "Back", tabs = "L / R: tabs", more = "More in Settings (Esc / hold Select)"; } texts;

    float entry_x() const { return 1280 - game_arrow.x; }
    bool on_arrow(float x, float y, bool entry) const {
        return inside(x, y, (entry ? entry_x() : game_arrow.x) - game_arrow.w / 2 - kArrowSlop, game_arrow.y - game_arrow.h / 2 - kArrowSlop, game_arrow.w + 2 * kArrowSlop,
                      game_arrow.h + 2 * kArrowSlop);
    }

    bool open() const { return open_; }
    // the page, or its slide out, is on screen
    bool shown() const { return open_ || slide_ > 0.0f; }
    int tab() const { return tab_; }
    int selected() const { return selected_; }

    void show(int tabs) {
        open_ = true, tab_ = std::clamp(tab_, 0, std::max(0, tabs - 1)), held_ = ~0u;  // a button held now acts only after its release
        touch_ = false;
    }
    void hide() { open_ = false; }

    // The closed page's input: true when it should open (the entry arrow tapped, or ZL)
    // How the closed page opens: ZL; Left (the cross or the stick) while the game's cursor is in the left
    // column; a tap on the entry arrow; or a swipe that starts on the arrow and pulls the page in (it
    // follows the finger; let go past a quarter of the screen, or flicked, it opens, else it slides back).
    bool entry(const Input& in) {
        uint32_t buttons = in.buttons;
        if (in.lx < -0.5f) buttons |= kPadLeft;  // the stick as the cross
        const uint32_t pressed = buttons & ~prev_;
        prev_ = buttons;
        const bool was_touch = touch_;
        bool tap = false;
        track_touch(in, tap);
        if (in.touch && !was_touch) swipe_ = on_arrow(in.x, in.y, true), swipe_dx_ = 0, swipe_v_ = 0;
        if (swipe_ && in.touch) {
            const float dx = std::max(0.0f, in.x - down_x_);
            if (in.dt > 0) swipe_v_ = (dx - swipe_dx_) / in.dt;
            swipe_dx_ = dx;
            slide_ = std::clamp(dx / (kWinX + kWinW + 100), 0.0f, 1.0f);  // the page follows the finger
            return false;
        }
        if (swipe_ && !in.touch) {  // let go
            swipe_ = false;
            if (swipe_dx_ > 320 || (swipe_v_ > 1500 && swipe_dx_ > 40)) return true;
            if (tap && on_arrow(in_x_, in_y_, true)) return true;
            return false;  // (the page slides back: shown() until it is out)
        }
        return (pressed & kPadZL) || ((pressed & kPadLeft) && in.cursor_left);
    }
    bool swiping() const { return swipe_; }

    Action input(const Input& in, const std::vector<Row>& rows, int tabs) {
        Action act;
        held_ &= in.buttons;  // buttons held at the opening are ignored until released
        const uint32_t live = in.buttons & ~held_;
        const uint32_t pressed = live & ~prev_;
        prev_ = live;
        const int count = int(rows.size());
        selected_ = count ? std::clamp(selected_, 0, count - 1) : 0;
        // the stick as the cross, with the menus' repeat (0.35 s, then every 0.12 s)
        uint32_t dirs = live & (kPadUp | kPadDown | kPadLeft | kPadRight);
        if (in.ly > 0.5f) dirs |= kPadUp;
        if (in.ly < -0.5f) dirs |= kPadDown;
        if (in.lx < -0.5f) dirs |= kPadLeft;
        if (in.lx > 0.5f) dirs |= kPadRight;
        uint32_t fire = dirs & ~dirs_;
        if (dirs && dirs == dirs_) {
            repeat_ += in.dt;
            if (repeat_ >= 0.35f) fire = dirs, repeat_ -= 0.12f;
        } else {
            repeat_ = 0;
        }
        dirs_ = dirs;
        if (pressed & (kPadB | kPadPlus)) return close(act);
        if ((pressed & (kPadL | kPadR)) && tabs > 1) {
            tab_ = (tab_ + ((pressed & kPadR) ? 1 : tabs - 1)) % tabs, selected_ = 0, top_ = 0, turn_ = 0;
            act.sound = Sound::Tab;
            return act;
        }
        auto move = [&](int d) {
            if (!count) return;
            const int next = std::clamp(selected_ + d, 0, count - 1);
            if (next != selected_) {
                glide_from_ = row_y(selected_), glide_ = 0, selected_ = next, act.sound = Sound::Cursor;
            }
        };
        if (fire & kPadUp) move(-1);
        else if (fire & kPadDown) move(+1);
        else if ((fire & (kPadLeft | kPadRight)) && count) change(act, rows[selected_], (fire & kPadLeft) ? -1 : +1);
        else if ((pressed & kPadA) && count) change(act, rows[selected_], rows[selected_].stepped() ? +1 : 0);
        if (selected_ < int(top_)) top_ = unsigned(selected_);
        if (selected_ >= int(top_ + kRowsShown)) top_ = unsigned(selected_) - kRowsShown + 1;
        // touch: a tap (released near where it began)
        bool tap = false;
        track_touch(in, tap);
        if (tap) {
            if (on_arrow(in_x_, in_y_, false)) return close(act);
            // the prompts can be tapped too (touch and mouse players): Back closes, Change acts as A
            const Prompts pr = prompt_places();
            const float py = kWinY + kWinH - 66 - 10, ph = 56;
            if (inside(in_x_, in_y_, pr.back_x - 10, py, pr.back_w + 20, ph)) return close(act);
            if (inside(in_x_, in_y_, pr.change_x - 10, py, pr.change_w + 20, ph) && !rows.empty()) {
                change(act, rows[selected_], rows[selected_].stepped() ? +1 : 0);
                return act;
            }
            for (unsigned i = 0; i < kRowsShown && top_ + i < rows.size(); i++) {
                const float y = kRowY + kRowH * float(i);
                if (!inside(in_x_, in_y_, kRowX, y, kRowW, kRowH)) continue;
                const int index = int(top_ + i);
                const bool on_value = in_x_ >= kValueX, was_selected = index == selected_;
                if (!was_selected) glide_from_ = row_y(selected_), glide_ = 0, selected_ = index, act.sound = Sound::Cursor;
                if (on_value || was_selected) {
                    const Row& r = rows[index];
                    change(act, r, r.stepped() ? (in_x_ < kValueX + kValueW / 2 ? -1 : +1) : 0);
                }
                break;
            }
        }
        return act;
    }

    void tick(float dt) {
        time_ += dt;
        slide_ = std::clamp(slide_ + (open_ ? dt : -dt) / 0.22f, 0.0f, 1.0f);
        glide_ = std::min(1.0f, glide_ + dt / 0.10f);
        bump_ = std::min(1.0f, bump_ + dt / 0.18f);
        turn_ = std::min(1.0f, turn_ + dt / 0.18f);
    }

    // the entry arrow over the items page (closed), in the game's page-arrow look
    std::vector<Command> draw_entry() const {
        std::vector<Command> out;
        const float pulse = 1.0f + 0.06f * std::sin(time_ * 6.2831853f * 0.8f);
        arrow(out, entry_x(), game_arrow.y, pulse, true);
        return out;
    }

    std::vector<Command> draw(const std::vector<Row>& rows, const std::vector<std::string>& tab_labels) const {
        std::vector<Command> out;
        const float dx = -(1.0f - ease_out(slide_)) * (kWinX + kWinW + 100);
        out.push_back(rect(0, 0, 1280, 720, 0x0A140A00u | uint32_t(std::lround(0x70 * slide_))));  // the items page dims
        const size_t from = out.size();
        out.push_back(box("nb:window", kWinX, kWinY, kWinW, kWinH));
        out.push_back(text(kWinX + 56, kWinY + 30, 42, kInk, bold(texts.title)));
        // tabs (the game's tab plates)
        float tx = kWinX + kWinW - 56;
        // the tabs right of the title: as wide as their names (at least 110), narrower when many
        const float room = kWinW - 112 - width(bold(texts.title), 42) - 24;
        const float most = tab_labels.empty() ? room : room / float(tab_labels.size()) - 10;
        for (int i = int(tab_labels.size()) - 1; i >= 0; i--) {
            const float w = std::min(most, std::max(110.0f, width(bold(tab_labels[i]), 26) + 40));
            tx -= w;
            const bool on = i == tab_;
            out.push_back(box(on ? "nb:tab-on" : "nb:tab", tx, kWinY + 30, w, 50));
            out.push_back(text(tx + (w - width(bold(tab_labels[i]), 26)) / 2, kWinY + 42, 26, on ? kInk : kInkSoft, bold(tab_labels[i])));
            tx -= 10;
        }
        if (tab_labels.size() > 1) out.push_back(text(kWinX + 56, kWinY + 82, 19, kInkSoft, texts.tabs));
        out.push_back(box("nb:panel", kRowX - 20, kRowY - 16, kRowW + 40, kRowH * kRowsShown + 30));
        for (unsigned i = 0; i < kRowsShown && top_ + i < rows.size(); i++) {
            const Row& r = rows[top_ + i];
            const float y = kRowY + kRowH * float(i);
            const bool sel = int(top_ + i) == selected_;
            if (i && !sel && int(top_ + i) != selected_ + 1) out.push_back(line(kRowX + 10, y, kRowX + kRowW - 10, y, kRule, 2));
            const uint32_t ink = r.enabled ? kInk : kInkDim;
            out.push_back(text(kRowX + 20, y + 15, 28, ink, sel ? bold(r.label) : r.label));
            // the value: a plate (lit when on), with step marks on numbers
            // On: the game's strong green plate with white letters; Off and dimmed rows: the muted tab plate;
            // numbers and choices: the light plate
            const bool on = r.toggle && r.on && r.enabled, muted = !r.enabled || (r.toggle && !r.on);
            out.push_back(box(on ? "nb:plate" : muted ? "nb:tab" : "nb:plate-on", kValueX, y + 6, kValueW, kRowH - 12));
            const float vw = width(bold(r.value), 26);
            out.push_back(text(kValueX + (kValueW - vw) / 2, y + 16, 26, on ? kWhite : !r.enabled ? kInkDim : muted ? kInkSoft : kInk, bold(r.value)));
            if (r.stepped() && r.enabled) {
                chevron(out, kValueX + 26, y + kRowH / 2, 9, false, kInk);
                chevron(out, kValueX + kValueW - 26, y + kRowH / 2, 9, true, kInk);
            }
        }
        if (!rows.empty()) {  // the cursor: the game's four brackets around the selected row, gliding there
            float y = row_y(selected_);
            if (glide_ < 1.0f) y = glide_from_ + (y - glide_from_) * ease_out(glide_);
            const float b = 4 + 2.0f + 2.0f * std::sin(time_ * 6.2831853f * 0.9f);
            out.push_back(box("nb:cursor", kRowX - b, y - b, kRowW + 2 * b, kRowH + 2 * b));
        }
        if (rows.size() > kRowsShown) {  // a scroll bar at the list's right edge
            const float x = kRowX + kRowW + 8, h = kRowH * kRowsShown - 8, y = kRowY + 4;
            out.push_back(rect(x - 1, y, 9, h, 0x3C6B2E40));
            const float th = std::max(24.0f, h * float(kRowsShown) / float(rows.size()));
            const float ty = y + (h - th) * float(top_) / float(rows.size() - kRowsShown);
            out.push_back(rect(x, ty, 7, th, kInk));
        }
        // the selected row's help, and the prompts along the bottom
        const float hy = kWinY + kWinH - 66;
        if (!rows.empty()) {  // (two lines at most; a longer help ends with "...")
            const std::vector<std::string> lines = wrap(rows[size_t(selected_)].help, 21, kWinW - 120, 2);
            for (size_t i = 0; i < lines.size(); i++)
                out.push_back(text(kWinX + 60, hy - 44 + 24 * float(i) - (lines.size() > 1 ? 12 : 0), 21, kInkSoft, lines[i]));
        }
        const Prompts pr = prompt_places();
        prompt(out, pr.back_x, hy, false, texts.back.c_str());
        prompt(out, pr.change_x, hy, true, texts.change.c_str());
        const float x = pr.change_x;
        // the hint, cut to the room left of the prompts
        const std::vector<std::string> more = wrap(texts.more, 22, x - 30 - (kWinX + 60), 1);
        if (!more.empty()) out.push_back(text(kWinX + 60, hy + 6, 22, kInkSoft, more[0]));
        // the arrow back to the items, right of the page
        arrow(out, game_arrow.x, game_arrow.y, 1.0f, false);
        for (size_t i = from; i < out.size(); i++) out[i].x += dx;
        return out;
    }

private:
    bool open_ = false;
    int tab_ = 0, selected_ = 0;
    unsigned top_ = 0;
    uint32_t prev_ = 0, held_ = 0, dirs_ = 0;
    float repeat_ = 0, time_ = 0, slide_ = 0, glide_ = 1, glide_from_ = 0, bump_ = 1, turn_ = 1;
    bool touch_ = false, swipe_ = false;
    float swipe_dx_ = 0, swipe_v_ = 0;
    float down_x_ = 0, down_y_ = 0, in_x_ = 0, in_y_ = 0;

    Action& close(Action& act) {
        open_ = false, act.closed = true, act.sound = Sound::Close;
        return act;
    }
    void change(Action& act, const Row& r, int direction) {
        if (!r.enabled) { act.sound = Sound::Blocked; return; }
        act.id = r.id, act.direction = direction, act.sound = Sound::Change, bump_ = 0;
    }
    void track_touch(const Input& in, bool& tap) {
        if (in.touch && !touch_) down_x_ = in.x, down_y_ = in.y;
        if (in.touch) in_x_ = in.x, in_y_ = in.y;
        tap = !in.touch && touch_ && near(in_x_, in_y_, down_x_, down_y_, kTapSlop);
        touch_ = in.touch;
    }
    // `text` in lines of at most `w` page units (at most `max_lines`; the last one cut with "...")
    std::vector<std::string> wrap(const std::string& text, float size, float w, size_t max_lines) const {
        std::vector<std::string> lines;
        std::string line;
        size_t i = 0;
        while (i < text.size()) {
            size_t j = text.find(' ', i);
            if (j == std::string::npos) j = text.size();
            const std::string word = text.substr(i, j - i);
            const std::string next = line.empty() ? word : line + " " + word;
            if (!line.empty() && width(next, size) > w) {
                lines.push_back(line);
                line = word;
                if (lines.size() == max_lines) break;
            } else {
                line = next;
            }
            i = j + 1;
        }
        if (lines.size() < max_lines && !line.empty()) {
            lines.push_back(line);
            line.clear();
        }
        if (i < text.size() || !line.empty()) {  // cut: the last line ends with "..."
            std::string& last = lines.back();
            while (!last.empty() && width(last + "...", size) > w) {  // whole words (or whole UTF-8 characters)
                const size_t space = last.rfind(' ');
                if (space != std::string::npos && space > 0) last.resize(space);
                else {  // the last character: its continuation bytes, then its lead byte
                    unsigned char c;
                    do c = static_cast<unsigned char>(last.back()), last.pop_back();
                    while (!last.empty() && (c & 0xC0) == 0x80);
                }
            }
            last += "...";
        }
        return lines;
    }
    struct Prompts { float back_x, back_w, change_x, change_w; };
    Prompts prompt_places() const {
        Prompts p;
        p.back_w = prompt_width(texts.back.c_str()), p.change_w = prompt_width(texts.change.c_str());
        p.back_x = kWinX + kWinW - 60 - p.back_w;
        p.change_x = p.back_x - 36 - p.change_w;
        return p;
    }
    float row_y(int index) const { return kRowY + kRowH * float(index - int(top_)); }
    float width(const std::string& s, float size) const {
        if (measure) return measure(s, size);
        size_t n = 0;
        for (size_t i = 0; i < s.size(); i++) n += (s[i] & 0xC0) != 0x80;
        return float(n) * size * 0.45f;
    }
    float prompt_width(const char* what, float size = 36) const { return size + 6 + width(bold(what), size * 0.8f); }
    void prompt(std::vector<Command>& out, float x, float y, bool a, const char* what, float size = 36) const {
        out.push_back(text(x, y, size, kWhite, a ? story_notebook::font::kGlyphA : story_notebook::font::kGlyphB));
        out.push_back(text(x + size + 6, y + size * 0.1f, size * 0.8f, kInk, bold(what)));
    }
    static void chevron(std::vector<Command>& out, float cx, float cy, float s, bool right, uint32_t rgba, float t = 4) {
        const float d = right ? 1.0f : -1.0f;
        out.push_back(line(cx - d * s * 0.5f, cy - s, cx + d * s * 0.5f, cy, rgba, t));
        out.push_back(line(cx + d * s * 0.5f, cy, cx - d * s * 0.5f, cy + s, rgba, t));
    }
    // A page arrow at (cx, cy), scaled by `s`: the game's arrow picture ("gs:arrow-left/right", the driver
    // draws the game's own picture when it could read it) and, for when it couldn't, a round drawn arrow
    // (commands tagged "gs:drawn", left out then)
    void arrow(std::vector<Command>& out, float cx, float cy, float s, bool left) const {
        const float w = game_arrow.w * s, h = game_arrow.h * s;
        Command p = box("", cx - w / 2, cy - h / 2, w, h);
        p.text = left ? "gs:arrow-left" : "gs:arrow-right";
        out.push_back(p);
        const float r = std::min(game_arrow.w, game_arrow.h) * 0.4f * s;
        const size_t from = out.size();
        out.push_back(circle(cx + 2, cy + 4, r + 4, 0x00000050, true));
        out.push_back(circle(cx, cy, r + 4, kArrowRim, true));
        out.push_back(circle(cx, cy, r, kArrowFill, true));
        chevron(out, cx + (left ? -2.0f : 2.0f), cy, r * 0.42f, !left, kWhite, r * 0.22f);
        for (size_t i = from; i < out.size(); i++) out[i].text = "gs:drawn";
    }
    static Command rect(float x, float y, float w, float h, uint32_t rgba) {
        Command c;
        c.kind = Command::Rect, c.x = x, c.y = y, c.w = w, c.h = h, c.rgba = rgba;
        return c;
    }
    static Command text(float x, float y, float size, uint32_t rgba, std::string s) {
        Command c;
        c.kind = Command::Text, c.x = x, c.y = y, c.size = size, c.rgba = rgba, c.text = std::move(s);
        return c;
    }
    static Command circle(float x, float y, float r, uint32_t rgba, bool filled, float t = 2) {
        Command c;
        c.kind = filled ? Command::Circle : Command::CircleOutline, c.x = x, c.y = y, c.size = r, c.rgba = rgba, c.thickness = t;
        return c;
    }
    static Command line(float x0, float y0, float x1, float y1, uint32_t rgba, float t) {
        Command c;
        c.kind = Command::Line, c.x = x0, c.y = y0, c.w = x1 - x0, c.h = y1 - y0, c.rgba = rgba, c.thickness = t;
        return c;
    }
    static Command box(const char* name, float x, float y, float w, float h) {
        Command c;
        c.kind = Command::Picture, c.x = x, c.y = y, c.w = w, c.h = h, c.rgba = 0xFFFFFFFF;
        c.text = std::string(name) + "@" + std::to_string(int(std::lround(w))) + "x" + std::to_string(int(std::lround(h)));
        return c;
    }
};

// The driver (game_settings_page.cpp): pad() runs once per GamePad read (VPADRead, game thread, `c` the
// reading thread for the game's fonts and sounds); mute() on the Pro Controller's reads
void pad(input::PadState& p, Cpu* c);
void mute(input::PadState& p);
bool page_open();  // the page (or its slide out) has the controller

}  // namespace game_settings_page
