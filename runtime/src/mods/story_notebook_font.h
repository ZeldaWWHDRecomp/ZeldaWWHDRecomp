// The story notebook's text in the game's own menu font (story_notebook_page.h draws Text commands;
// this turns them into pictures of the game's glyphs). Pure code: the glyphs come from the caller
// (game_glyph::read_glyph on the fonts the game loaded, story_notebook.cpp), the result is a host
// image plus picture commands; unit-tested in runtime/tools/story_notebook_test.cpp.
//
// Each distinct line is composed once into a line atlas (an RGBA sheet: the letters in their colours,
// the icon font's own colours for the button pictures); a Text command becomes one picture of its
// line (one per segment of a line wider than a page), scaled to the command's size. Only the lines the
// atlas doesn't hold yet are composed, onto new pages.
//
// Sized lines (sized_key) are composed at the pixel height the screen draws them at (the TV window's
// or the GamePad window's real pixels, pixel_height), so the screen samples them about 1:1: a line
// shrunk on the GPU skips texels, and the 1-2 pixel border of the game's font smears into the
// letters. A shrunk glyph is averaged over the texels each pixel covers, and a bold line keeps a
// border of at least kMinBorder pixels.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../game_glyph.h"
#include "../button_glyph_art.h"
#include "../button_glyphs.h"
#include "guest_hud.h"

namespace story_notebook::font {

using guestmods::hud::Command;

// the private-use characters of the game's button pictures (the icon glyphs of its message font)
inline constexpr uint32_t kButtonA = 0xE000, kButtonB = 0xE001;
inline const std::string kGlyphA = "\xEE\x80\x80", kGlyphB = "\xEE\x80\x81";  // U+E000, U+E001 in UTF-8

// The next code point of a UTF-8 string (the HUD store only takes valid UTF-8); 0xFFFD on a bad byte
inline uint32_t next_code(const std::string& s, size_t& i) {
    const unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = c & (0x3F >> more);
    if (!more) return 0xFFFD;
    for (; more && i < s.size(); more--) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return more ? 0xFFFD : cp;
}
inline std::vector<uint32_t> codes(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) out.push_back(next_code(s, i));
    return out;
}

// How the button pictures look (input_map::prompt_style, as the game's own prompts are restyled by
// button_glyphs.cpp): the game's icon, the icon in the Xbox colours, or the PlayStation shapes.
struct Prompts {
    enum Face : uint8_t { Original, Xbox, PlayStation, GameCube } face = Original;
    bool colours = false;
    std::array<int, 4> buttons = {0, 1, 2, 3};
    bool operator==(const Prompts&) const = default;
};

// A font as the page uses it: its glyphs (filled on demand by the caller), its cell height.
struct Face {
    int cell_height = 0;
    std::unordered_map<uint32_t, game_glyph::Glyph> glyphs;
    std::unordered_map<uint32_t, bool> missing;  // asked for, not in the font
    bool ready() const { return cell_height > 0 && !glyphs.empty(); }
    const game_glyph::Glyph* find(uint32_t cp) const {
        auto it = glyphs.find(cp);
        if (it != glyphs.end()) return &it->second;
        if (cp != '?') {  // the font's own stand-in, as the game shows an unknown character
            it = glyphs.find('?');
            if (it != glyphs.end()) return &it->second;
        }
        return nullptr;
    }
    // the characters of `text` the caller still has to ask the game for
    void wanted(const std::string& text, std::vector<uint32_t>& out) const {
        for (uint32_t cp : codes(text))
            if (!glyphs.count(cp) && !missing.count(cp) && std::find(out.begin(), out.end(), cp) == out.end()) out.push_back(cp);
    }
};

// The width of a line in the font's own pixels
inline int line_pixels(const Face& f, const std::string& text) {
    int w = 0;
    for (uint32_t cp : codes(text))
        if (cp >= 0xE0F0 && cp <= 0xE0FF) continue;  // a style code (kBold)
        else
        if (const game_glyph::Glyph* g = f.find(cp)) w += g->advance;
    return w;
}
// The line's width on the page at a text size (the page's sizes are the line height in page units)
inline float measure(const Face& f, const std::string& text, float size) {
    return f.cell_height ? float(line_pixels(f, text)) * size / float(f.cell_height) : 0.0f;
}

// One button picture, 64 x 64 cells scaled to the font's height: the game's icon glyph (in the
// Xbox colours if asked), or the PlayStation shape the port draws for that button.
inline game_glyph::Bitmap button_picture(const game_glyph::Glyph* icon, int face, const Prompts& look, int height) {
    game_glyph::Bitmap out;
    if (look.face == Prompts::GameCube || look.face == Prompts::PlayStation || !icon) {
        // the port's own shape on a disc (button_glyph_art.h), or a plain disc when the font has no icon
        out.width = out.height = 64;
        out.rgba.resize(64 * 64 * 4);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                const uint32_t c = look.face == Prompts::GameCube ? button_glyphs::gamecube_icon(face, true, x, y)
                                 : button_glyphs::face_shape(face, look.colours, true, x, y);
                uint8_t* p = &out.rgba[(size_t(y) * 64 + x) * 4];
                p[0] = c >> 24, p[1] = c >> 16, p[2] = c >> 8, p[3] = c;
            }
        (void)height;
        return out;
    }
    out = icon->bitmap;
    if (look.face == Prompts::Xbox && look.colours)  // multiplied like the game's vertex colours (button_glyphs::tint)
        for (size_t i = 0; i + 3 < out.rgba.size(); i += 4) {
            const uint32_t t = button_glyphs::tint(uint32_t(out.rgba[i]) << 24 | uint32_t(out.rgba[i + 1]) << 16 |
                                                       uint32_t(out.rgba[i + 2]) << 8 | out.rgba[i + 3], face);
            out.rgba[i] = t >> 24, out.rgba[i + 1] = t >> 16, out.rgba[i + 2] = t >> 8;
        }
    return out;
}

// Text styles: a line that starts with kBold is drawn with the font's border (the game's menu
// labels: a light border around dark letters, a dark one around light letters); other lines with
// the letters only (the game's message text).
// A line that starts with kMessage is drawn in the game's message font (CKingMsg, the font of its
// talks and of the item menu's Info box) instead of its menu font.
inline const std::string kBold = "\xEE\x83\xB1", kMessage = "\xEE\x83\xB2";  // U+E0F1, U+E0F2
inline bool style_code(uint32_t cp) { return cp >= 0xE0F0 && cp <= 0xE0FF; }
inline std::string plain_text(std::string text) {
    while (text.size() >= 3 && (text.rfind(kBold, 0) == 0 || text.rfind(kMessage, 0) == 0)) text = text.substr(3);
    return text;
}
inline bool has_style(const std::string& text, const std::string& code) {
    for (size_t i = 0; i + 3 <= text.size() && (text.compare(i, 3, kBold) == 0 || text.compare(i, 3, kMessage) == 0); i += 3)
        if (text.compare(i, 3, code) == 0) return true;
    return false;
}

// A line as the atlas keeps it: its text and its colour (the colours are composed in)
inline std::string line_key(const std::string& text, uint32_t rgba) {
    char colour[8];
    std::snprintf(colour, sizeof colour, "%06X", unsigned(rgba >> 8));
    return text + '\x1F' + colour;
}

// A line at the pixel height the screen draws it at: line_key plus its height (pixel_height)
inline std::string sized_key(const std::string& text, uint32_t rgba, int pixels) {
    return line_key(text, rgba) + '\x1E' + std::to_string(pixels);
}
// The height a line is composed at for `pixels` on the screen: whole pixels up to 16, then steps of
// 8 % (never below what is drawn, at most 8 % above it), so the opening animation's scale and a
// window resized by a few pixels reuse the lines already composed
inline int pixel_height(float pixels) {
    if (!(pixels > 0)) return 1;
    if (pixels <= 16.0f) return std::max(1, int(std::ceil(pixels - 0.01f)));
    const int n = int(std::ceil(std::log(pixels / 16.0f) / std::log(1.08f) - 1e-4));
    return int(std::ceil(16.0f * std::pow(1.08f, float(n)) - 0.01f));
}

// The line atlas: every line the page shows, composed once, on one or more pages (each its own RGBA
// image, at most kWidth x kMaxHeight, sized to what it holds). A line wider than a page is cut into
// segments, each its own cell, drawn side by side; every segment keeps a column of its neighbours'
// pixels on either side (outside its cell), so the screen's filtering finds the same pixels at a cut
// as inside the line, and the cuts don't show. Pages are never changed once composed: new lines go
// onto new pages, so the pages the screen already has stay as they are.
class Atlas {
public:
    static constexpr int kWidth = 2048, kMaxHeight = 2048, kPad = 2;
    static constexpr float kMinBorder = 1.0f;  // a bold line's border in pixels, at least
    struct Cell {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // on its page
        int width = 0, height = 0;  // pixels
        int margin = 0;  // pixels left and right of the line (a sized line's border room)
        int page = 0;    // the page it is on
        int offset = 0;  // pixels from the line's left edge (its margin included) to the cell: a segment's place
    };
    // true if every line of `lines` (line_key) is in the atlas, composed with this look and these fonts
    bool holds(const std::vector<std::string>& lines, const Prompts& look, uint64_t fonts) const {
        if (look != look_ || fonts != fonts_ || pages_.empty()) return false;
        for (const std::string& l : lines)
            if (!cells_.count(l)) return false;
        return true;
    }
    // Compose the lines (line_key: the main font, and `fallback` for characters the main font lacks;
    // the icon font for the button pictures) at `scale` of the font's own size, sized lines
    // (sized_key) at their own height, on new pages.
    void build(const std::vector<std::string>& lines, const Face& main, const Face* fallback, const Face* icons, const Prompts& look,
               uint64_t fonts, float scale = 1.0f) {
        clear();
        update(lines, main, fallback, icons, look, fonts, scale);
    }
    // The lines a page shows in this read: the ones the atlas doesn't hold yet are composed onto new pages (all of
    // them again with another look, other fonts or another scale). Then, while the pages take more than
    // `budget` bytes, the page unused for the longest is dropped (none of these lines on it), or else,
    // in a read that composed lines anyway, the current lines of the emptiest page under half used are
    // moved onto a new page and that page is dropped. So without a new line nothing is composed, and
    // the lines that are shown stay, however many bytes they need.
    void update(const std::vector<std::string>& lines, const Face& main, const Face* fallback, const Face* icons, const Prompts& look,
                uint64_t fonts, float scale = 1.0f, size_t budget = SIZE_MAX) {
        if (look != look_ || fonts != fonts_ || scale != scale_) clear();
        look_ = look, fonts_ = fonts, scale_ = scale;
        reads_++;
        std::vector<const std::string*> shown, missing;  // (each line once)
        for (const std::string& l : lines) shown.push_back(&l);
        std::sort(shown.begin(), shown.end(), [](const std::string* a, const std::string* b) { return *a < *b; });
        shown.erase(std::unique(shown.begin(), shown.end(), [](const std::string* a, const std::string* b) { return *a == *b; }), shown.end());
        for (const std::string* l : shown)
            if (!cells_.count(*l)) missing.push_back(l);
        const bool added_lines = !missing.empty();
        std::vector<int> added = place(missing, main, fallback, icons, look);
        // the pages these lines are on, and the area they take there
        std::vector<size_t> use;
        auto measure = [&] {
            use.assign(pages_.size(), 0);
            for (const std::string* l : shown)
                if (auto it = cells_.find(*l); it != cells_.end())
                    for (const Cell& c : it->second) use[size_t(c.page)] += size_t(c.width + kPad) * size_t(c.height + kPad);
            for (size_t p = 0; p < pages_.size(); p++)
                if (use[p]) pages_[p].last_used = reads_;
        };
        measure();
        std::vector<bool> moved(pages_.size(), false);  // pages composed or emptied in this read are not moved again
        for (int p : added) moved[size_t(p)] = true;
        while (bytes() > budget) {
            int drop = -1, sparse = -1;
            for (size_t p = 0; p < pages_.size(); p++) {
                if (!pages_[p].image) continue;
                if (!use[p]) {
                    if (drop < 0 || pages_[p].last_used < pages_[size_t(drop)].last_used) drop = int(p);
                } else if (added_lines && !moved[p] && use[p] * 2 < area(p) && (sparse < 0 || use[p] * area(size_t(sparse)) < use[size_t(sparse)] * area(p))) {
                    sparse = int(p);
                }
            }
            if (drop >= 0) {
                drop_page(drop);
                continue;
            }
            if (sparse < 0) break;
            std::vector<const std::string*> keep;
            for (const std::string* l : shown)
                if (auto it = cells_.find(*l); it != cells_.end())
                    for (const Cell& c : it->second)
                        if (c.page == sparse) {
                            keep.push_back(l);
                            cells_.erase(it);
                            break;
                        }
            added = place(keep, main, fallback, icons, look);
            moved.resize(pages_.size(), true);
            for (int p : added) moved[size_t(p)] = true;
            moved[size_t(sparse)] = true;
            measure();
        }
    }
    // the line's first cell (its only one unless it is wider than a page)
    const Cell* cell(const std::string& key) const {
        auto it = cells_.find(key);
        return it == cells_.end() ? nullptr : &it->second.front();
    }
    // the line's cells left to right (null if it isn't in the atlas)
    const std::vector<Cell>* segments(const std::string& key) const {
        auto it = cells_.find(key);
        return it == cells_.end() ? nullptr : &it->second;
    }
    // the pages (an index of a dropped page stays null until a new page takes it)
    size_t pages() const { return pages_.size(); }
    std::shared_ptr<const guestmods::hud::Image> image(size_t page = 0) const { return page < pages_.size() ? pages_[page].image : nullptr; }
    size_t bytes() const {
        size_t n = 0;
        for (const Page& p : pages_)
            if (p.image) n += p.image->rgba.size();
        return n;
    }
    // lines taller than a page, composed at a page's height (drawn scaled up), since the last clear
    int too_tall() const { return too_tall_; }
    void clear() {
        cells_.clear();
        pages_.clear();
        too_tall_ = 0;
    }

    // the button characters' width in a line: as wide as tall (the game's icons are square cells)
    static bool button(uint32_t cp) { return cp == kButtonA || cp == kButtonB; }
    // a line's width in pixels at line height h (each glyph scaled from its own font's cell height)
    static int line_width(const std::string& text, const Face& main, const Face* fallback, int h) {
        const bool message = has_style(text, kMessage) && fallback && fallback->ready();
        const Face& first = message ? *fallback : main;
        const Face* second = message ? &main : fallback;
        float w = 0;
        for (uint32_t cp : codes(text)) {
            if (style_code(cp)) continue;
            if (button(cp)) {
                w += h;
                continue;
            }
            const game_glyph::Glyph* g = glyph(cp, first, second);
            if (g) w += std::max(g->advance, g->left + g->bitmap.width) * float(h) / float(std::max(1, g->bitmap.height));
        }
        return int(std::ceil(w));
    }

private:
    // a key's line height: a sized line's own, else the atlas's
    static bool sized(const std::string& key) {
        const size_t mark = key.rfind('\x1E');
        return mark != std::string::npos && mark > key.rfind('\x1F');
    }
    static int height_of(const std::string& key, int base) {
        const size_t mark = key.rfind('\x1E');
        if (!sized(key)) return base;
        return std::clamp(std::atoi(key.c_str() + mark + 1), 1, kMaxHeight - 2 * kPad);
    }
    static const game_glyph::Glyph* glyph(uint32_t cp, const Face& main, const Face* fallback) {
        auto it = main.glyphs.find(cp);
        if (it != main.glyphs.end()) return &it->second;
        if (fallback) {
            auto f = fallback->glyphs.find(cp);
            if (f != fallback->glyphs.end()) return &f->second;
        }
        return main.find(cp);
    }
    // a bitmap drawn into the atlas at (x, y), scaled to w x h (bilinear), its pixels through `paint`
    // (straight RGBA in, straight RGBA out), over what is already there
    template <class Paint>
    static void blit(guestmods::hud::Image& img, const game_glyph::Bitmap& b, float x, int y, int w, int h, Paint&& paint) {
        if (b.width <= 0 || b.height <= 0 || w <= 0 || h <= 0) return;
        const int x0 = int(std::floor(x));
        for (int ty = 0; ty < h; ty++)
            for (int tx = 0; tx <= w; tx++) {
                const int ax = x0 + tx, ay = y + ty;
                if (ax < 0 || ay < 0 || ax >= int(img.width) || ay >= int(img.height)) continue;
                const float sx = (ax + 0.5f - x) * b.width / w - 0.5f, sy = (ty + 0.5f) * b.height / h - 0.5f;
                if (sx < -1 || sx > b.width) continue;
                const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
                const float fx = sx - ix, fy = sy - iy;
                float acc[4] = {0, 0, 0, 0};
                for (int k = 0; k < 4; k++) {
                    const int px = ix + (k & 1), py = std::clamp(iy + (k >> 1), 0, b.height - 1);
                    if (px < 0 || px >= b.width) continue;  // outside the glyph: transparent
                    const float wgt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
                    const uint8_t* s = &b.rgba[(size_t(py) * b.width + px) * 4];
                    const float a = s[3] / 255.0f;  // premultiplied, so transparent texels don't bleed their colour
                    for (int c = 0; c < 3; c++) acc[c] += s[c] * a * wgt;
                    acc[3] += s[3] * wgt;
                }
                if (acc[3] <= 0.0f) continue;
                float src[4] = {acc[0] * 255.0f / acc[3], acc[1] * 255.0f / acc[3], acc[2] * 255.0f / acc[3], acc[3]};
                paint(src);
                if (src[3] <= 0.0f) continue;
                uint8_t* d = &img.rgba[(size_t(ay) * img.width + ax) * 4];
                const float da = d[3] / 255.0f, sa = src[3] / 255.0f, oa = sa + da * (1 - sa);
                for (int c = 0; c < 3; c++) d[c] = uint8_t(std::clamp((src[c] * sa + d[c] * da * (1 - sa)) / std::max(oa, 1e-4f), 0.0f, 255.0f));
                d[3] = uint8_t(std::clamp(oa * 255.0f + 0.5f, 0.0f, 255.0f));
            }
    }
    static float ramp(float t, float from, float to) { return std::clamp((t - from) / (to - from), 0.0f, 1.0f); }
    // a glyph's coverage alpha (0..1) at a bitmap position, bilinear (outside its width: none)
    static float alpha_at(const game_glyph::Bitmap& b, float sx, float sy) {
        const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
        const float fx = sx - ix, fy = sy - iy;
        float a = 0;
        for (int k = 0; k < 4; k++) {
            const int px = ix + (k & 1), py = std::clamp(iy + (k >> 1), 0, b.height - 1);
            if (px < 0 || px >= b.width) continue;
            a += b.rgba[(size_t(py) * b.width + px) * 4 + 3] * ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
        }
        return a / 255.0f;
    }
    // A glyph drawn at (x, 0) of a line w x h pixels into the line's letters (`fill`) and border
    // (`edge`) coverage (stride `stride`, `margin` columns left of the line). The game's fonts keep
    // their letters at full coverage and their border at about half; each pixel is the average of
    // n x n samples spread over the texels it covers (one sample when the glyph isn't shrunk), so a
    // small line keeps every stroke.
    static void coverage(const game_glyph::Bitmap& b, float x, int w, int h, bool bold, std::vector<float>& fill, std::vector<float>& edge,
                         int stride, int margin) {
        if (b.width <= 0 || b.height <= 0 || w <= 0 || h <= 0) return;
        const int x0 = int(std::floor(x));
        const int nx = std::clamp(int(std::ceil(float(b.width) / float(w) - 0.01f)), 1, 8);
        const int ny = std::clamp(int(std::ceil(float(b.height) / float(h) - 0.01f)), 1, 8);
        const float inv = 1.0f / float(nx * ny);
        for (int ty = 0; ty < h; ty++)
            for (int tx = 0; tx <= w; tx++) {
                const int ax = x0 + tx, col = ax + margin;
                if (col < 0 || col >= stride) continue;
                float f = 0, e = 0;
                for (int j = 0; j < ny; j++)
                    for (int i = 0; i < nx; i++) {
                        const float px = float(ax) + (float(i) + 0.5f) / float(nx), py = float(ty) + (float(j) + 0.5f) / float(ny);
                        const float sx = (px - x) * float(b.width) / float(w) - 0.5f, sy = py * float(b.height) / float(h) - 0.5f;
                        if (sx < -1 || sx > b.width) continue;
                        const float t = alpha_at(b, sx, sy);
                        if (bold) f += ramp(t, 0.62f, 0.9f), e += ramp(t, 0.05f, 0.4f);
                        else f += ramp(t, 0.42f, 0.85f);
                    }
                const size_t at = size_t(ty) * stride + col;
                fill[at] = std::max(fill[at], f * inv), edge[at] = std::max(edge[at], e * inv);
            }
    }
    // one line in its colour (0xRRGGBB), w x h pixels at (x, y), with `room` pixels on either side for its border
    static void compose(guestmods::hud::Image& img, std::string text, uint32_t rgb, const Face& main, const Face* fallback, const Face* icons,
                        const Prompts& look, int x, int y, int w, int h, int room = 0) {
        const bool bold = has_style(text, kBold), message = has_style(text, kMessage) && fallback && fallback->ready();
        const Face& first = message ? *fallback : main;
        const Face* second = message ? &main : fallback;
        const float fill[3] = {float(rgb >> 16 & 255), float(rgb >> 8 & 255), float(rgb & 255)};
        const bool light = fill[0] * 0.3f + fill[1] * 0.59f + fill[2] * 0.11f > 140.0f;
        const float border[3] = {light ? 44.0f : 255.0f, light ? 58.0f : 255.0f, light ? 46.0f : 255.0f};
        // plain text keeps the letters, bold text draws the border in the border colour around them
        const int margin = 2, stride = w + 2 * margin + 2;
        std::vector<float> letters(size_t(stride) * h, 0.0f), edge(letters.size(), 0.0f);
        auto as_is = [](float*) {};
        float pen = float(x);
        for (uint32_t cp : codes(text)) {
            if (style_code(cp)) continue;
            if (button(cp)) {
                const int face = look.buttons[cp == kButtonA ? 0 : 1];
                const uint32_t picture = kButtonA + face;
                const game_glyph::Glyph* icon = icons ? icons->find(picture) : nullptr;
                if (icon && icons->glyphs.find(picture) == icons->glyphs.end()) icon = nullptr;  // not the stand-in
                const game_glyph::Bitmap b = button_picture(icon, face, look, h);
                blit(img, b, pen, y, h, h, as_is);
                pen += h;
                continue;
            }
            const game_glyph::Glyph* g = glyph(cp, first, second);
            if (!g) continue;
            const float k = float(h) / float(std::max(1, g->bitmap.height));  // its font's cell to the line
            if (g->bitmap.width > 0)
                coverage(g->bitmap, pen - float(x) + std::max(0, g->left) * k, std::max(1, int(std::lround(g->bitmap.width * k))), h, bold, letters,
                         edge, stride, margin);
            pen += std::max(g->advance, g->left + g->bitmap.width) * k;
        }
        // a bold line's border at least kMinBorder pixels wide: the letters grown by that much (a
        // shrunk line's own border can be thinner than a pixel and would vanish into the letters)
        if (bold) {
            std::vector<float> grown = edge;
            const int r = int(std::ceil(kMinBorder));
            for (int ty = 0; ty < h; ty++)
                for (int col = 0; col < stride; col++) {
                    float g = 0;
                    for (int dy = -r; dy <= r; dy++)
                        for (int dx = -r; dx <= r; dx++) {
                            const int yy = ty + dy, xx = col + dx;
                            if (yy < 0 || yy >= h || xx < 0 || xx >= stride) continue;
                            const float reach = std::clamp(kMinBorder + 1.0f - std::sqrt(float(dx * dx + dy * dy)), 0.0f, 1.0f);  // a neighbour: all of it
                            g = std::max(g, letters[size_t(yy) * stride + xx] * reach);
                        }
                    float& e = grown[size_t(ty) * stride + col];
                    e = std::max(e, g);
                }
            edge.swap(grown);
        }
        for (int ty = 0; ty < h; ty++)
            for (int col = 0; col < stride; col++) {
                const float f = letters[size_t(ty) * stride + col], e = edge[size_t(ty) * stride + col];
                const int ax = x + col - margin, ay = y + ty;
                if (ax < x - room - kPad / 2 || ax >= x + w + room + kPad / 2) continue;  // the pads keep the lines apart
                if (ax < 0 || ay < 0 || ax >= int(img.width) || ay >= int(img.height)) continue;
                float src[4];
                if (bold) {
                    for (int i = 0; i < 3; i++) src[i] = border[i] + (fill[i] - border[i]) * f;
                    src[3] = 255.0f * std::max(f, e * 0.95f);
                } else {
                    for (int i = 0; i < 3; i++) src[i] = fill[i];
                    src[3] = 255.0f * f;
                }
                if (src[3] <= 0.0f) continue;
                over(&img.rgba[(size_t(ay) * img.width + ax) * 4], src);
            }
    }
    // straight RGBA `src` (0..255 floats) over the pixel `d`
    static void over(uint8_t* d, const float* src) {
        const float da = d[3] / 255.0f, sa = src[3] / 255.0f, oa = sa + da * (1 - sa);
        for (int c = 0; c < 3; c++) d[c] = uint8_t(std::clamp((src[c] * sa + d[c] * da * (1 - sa)) / std::max(oa, 1e-4f), 0.0f, 255.0f));
        d[3] = uint8_t(std::clamp(oa * 255.0f + 0.5f, 0.0f, 255.0f));
    }

    struct Page {
        std::shared_ptr<guestmods::hud::Image> image;  // null: dropped
        uint64_t last_used = 0;  // the read that last showed one of its lines
    };
    size_t area(size_t page) const { return size_t(pages_[page].image->width) * pages_[page].image->height; }
    void drop_page(int page) {
        std::erase_if(cells_, [&](const auto& line) {
            return std::any_of(line.second.begin(), line.second.end(), [&](const Cell& c) { return c.page == page; });
        });
        pages_[size_t(page)] = {};
    }
    // The lines onto new pages (the free indices first): rows left to right, each as tall as its first
    // (tallest) line; a line wider than a page as segments of at most a page's width; a page full: the
    // next one. Each line is composed once into a strip (with a column on either side for its border's
    // edge) and its segments copied from there, with a column of the strip on either side of each.
    // Returns the pages it composed.
    std::vector<int> place(const std::vector<const std::string*>& keys, const Face& main, const Face* fallback, const Face* icons,
                           const Prompts& look) {
        if (main.cell_height <= 0 || keys.empty()) return {};
        const int base = std::clamp(int(std::lround(main.cell_height * scale_)), 1, kMaxHeight - 2 * kPad);
        struct Line {
            const std::string* key;
            int w, h, m;
        };
        struct Piece {
            size_t line;
            int offset, w, x, y, page;
        };
        struct Sheet {
            int width = 0, height = 0;
        };
        std::vector<Line> todo;
        for (const std::string* key : keys) {
            todo.push_back({key, 0, height_of(*key, base), sized(*key) ? int(std::ceil(kMinBorder)) : 0});
            if (sized(*key) && std::atoi(key->c_str() + key->rfind('\x1E') + 1) > todo.back().h) too_tall_++;
        }
        // unique lines, the tallest first (rows of about one height)
        std::sort(todo.begin(), todo.end(), [](const Line& a, const Line& b) { return a.h != b.h ? a.h > b.h : *a.key < *b.key; });
        todo.erase(std::unique(todo.begin(), todo.end(), [](const Line& a, const Line& b) { return *a.key == *b.key; }), todo.end());
        std::vector<Piece> pieces;
        std::vector<Sheet> sheets;
        int x = kPad, y = kPad, row = 0;
        for (size_t i = 0; i < todo.size(); i++) {
            Line& l = todo[i];
            l.w = std::max(1, line_width(l.key->substr(0, l.key->rfind('\x1F')), main, fallback, l.h)) + 2 * l.m;
            const int widest = kWidth - 2 * kPad, n = (l.w + widest - 1) / widest;
            for (int s = 0; s < n; s++) {
                const int from = int(int64_t(l.w) * s / n), to = int(int64_t(l.w) * (s + 1) / n);
                if (x + (to - from) + kPad > kWidth) x = kPad, y += row + kPad, row = 0;
                if (sheets.empty() || y + l.h + kPad > kMaxHeight) sheets.emplace_back(), x = kPad, y = kPad, row = 0;
                pieces.push_back({i, from, to - from, x, y, int(sheets.size()) - 1});
                row = std::max(row, l.h);
                x += to - from + kPad;
                Sheet& sh = sheets.back();
                sh.width = std::max(sh.width, x), sh.height = std::max(sh.height, y + row + kPad);
            }
        }
        // the sheets as pages, at the free indices
        std::vector<int> index;
        size_t free = 0;
        for (const Sheet& sh : sheets) {
            while (free < pages_.size() && pages_[free].image) free++;
            if (free == pages_.size()) pages_.emplace_back();
            auto image = std::make_shared<guestmods::hud::Image>();
            image->width = uint32_t(std::max(4, sh.width)), image->height = uint32_t(std::max(4, sh.height));
            image->rgba.assign(size_t(image->width) * image->height * 4, 0);
            pages_[free] = {image, reads_};
            index.push_back(int(free));
        }
        size_t next = 0;
        for (size_t i = 0; i < todo.size(); i++) {
            const Line& l = todo[i];
            const size_t sep = l.key->rfind('\x1F');
            const uint32_t rgb = uint32_t(std::strtoul(l.key->c_str() + sep + 1, nullptr, 16));
            guestmods::hud::Image strip;
            strip.width = uint32_t(l.w + 2), strip.height = uint32_t(l.h);
            strip.rgba.assign(size_t(strip.width) * strip.height * 4, 0);
            compose(strip, l.key->substr(0, sep), rgb, main, fallback, icons, look, 1 + l.m, 0, l.w - 2 * l.m, l.h, l.m);
            std::vector<Cell>& cells = cells_[*l.key];
            cells.clear();
            for (; next < pieces.size() && pieces[next].line == i; next++) {
                const Piece& p = pieces[next];
                guestmods::hud::Image& page = *pages_[size_t(index[size_t(p.page)])].image;
                for (int ty = 0; ty < l.h; ty++)  // strip columns offset .. offset + w + 1: the cell and a column either side
                    std::copy_n(&strip.rgba[(size_t(ty) * strip.width + size_t(p.offset)) * 4], size_t(p.w + 2) * 4,
                                &page.rgba[(size_t(p.y + ty) * page.width + size_t(p.x - 1)) * 4]);
                Cell c;
                c.width = p.w, c.height = l.h, c.margin = l.m, c.page = index[size_t(p.page)], c.offset = p.offset;
                c.u0 = float(p.x) / float(page.width), c.v0 = float(p.y) / float(page.height);
                c.u1 = float(p.x + p.w) / float(page.width), c.v1 = float(p.y + l.h) / float(page.height);
                cells.push_back(c);
            }
        }
        return index;
    }

    std::map<std::string, std::vector<Cell>> cells_;  // a line's cells, left to right
    std::vector<Page> pages_;
    Prompts look_;
    uint64_t fonts_ = 0, reads_ = 0;
    float scale_ = -1;
    int too_tall_ = 0;
};

// A Text command's atlas key: sized (pixel_height) when `pixels` (screen pixels per page unit) is set
inline std::string key_of(const Command& c, float font_scale, float pixels) {
    return pixels > 0 ? sized_key(c.text, c.rgba, pixel_height(c.size * font_scale * pixels)) : line_key(c.text, c.rgba);
}
// The lines a draw list needs from the atlas (line_key; sized_key when `pixels` is set)
inline std::vector<std::string> lines_of(const std::vector<Command>& cmds, float font_scale = 1.0f, float pixels = 0.0f) {
    std::vector<std::string> out;
    for (const Command& c : cmds)
        if (c.kind == Command::Text && !c.text.empty()) out.push_back(key_of(c, font_scale, pixels));
    return out;
}
// The text of the lines (for the glyphs the page needs)
inline std::vector<std::string> texts_of(const std::vector<Command>& cmds) {
    std::vector<std::string> out;
    for (const Command& c : cmds)
        if (c.kind == Command::Text && !c.text.empty()) out.push_back(c.text);
    return out;
}

// The page's Text commands as pictures of their atlas lines (kind Picture, u/v set, `image` the
// atlas page it is on: the caller hands them to the HUD store with that page's image), one per cell
// of the line, side by side. `font_scale`: the drawn line height per text size. Lines not in the atlas,
// or on a page `drawable` says the screen can't have, stay Text (the overlay font); a button without
// the atlas becomes a disc with its letter.
inline std::vector<Command> to_pictures(const std::vector<Command>& in, const Atlas* atlas, float font_scale = 1.0f, float pixels = 0.0f,
                                        const std::function<bool(int page)>& drawable = nullptr) {
    std::vector<Command> out;
    out.reserve(in.size());
    for (const Command& c : in) {
        if (c.kind != Command::Text) {
            out.push_back(c);
            continue;
        }
        const std::vector<Atlas::Cell>* cells = atlas ? atlas->segments(key_of(c, font_scale, pixels)) : nullptr;
        if (cells && drawable)
            for (const Atlas::Cell& cell : *cells)
                if (!drawable(cell.page)) cells = nullptr;
        if (!cells) {
            if (c.text == kGlyphA || c.text == kGlyphB) {  // no font: the button as a dark disc with its letter
                const float r = c.size * 0.5f;
                Command disc;
                disc.kind = Command::Circle, disc.x = c.x + r, disc.y = c.y + r, disc.size = r, disc.rgba = 0x3F2A14FF & (0xFFFFFF00u | (c.rgba & 0xFF));
                out.push_back(disc);
                Command letter = c;
                letter.text = c.text == kGlyphA ? "A" : "B";
                letter.x = c.x + r * 0.52f, letter.y = c.y + r * 0.22f, letter.size = c.size * 0.72f;
                letter.rgba = 0xF2E4C200u | (c.rgba & 0xFF);
                out.push_back(letter);
                continue;
            }
            Command t = c;
            t.text = plain_text(c.text);
            out.push_back(t);
            continue;
        }
        // each cell from the line's left edge, scaled alike; a cell ends where the next one starts
        const float h = c.size * font_scale, k = h / float(cells->front().height), left = c.x - float(cells->front().margin) * k;
        for (const Atlas::Cell& cell : *cells) {
            Command p;
            p.kind = Command::Picture;
            p.x = left + float(cell.offset) * k, p.y = c.y - (h - c.size) * 0.5f;
            p.w = left + float(cell.offset + cell.width) * k - p.x, p.h = h;
            p.u0 = cell.u0, p.v0 = cell.v0, p.u1 = cell.u1, p.v1 = cell.v1;
            p.rgba = 0xFFFFFF00u | (c.rgba & 0xFF);  // the colours are in the atlas; the alpha fades it
            p.anchor = c.anchor, p.blend = c.blend;
            p.image = atlas->image(size_t(cell.page));
            out.push_back(p);
        }
    }
    return out;
}

}  // namespace story_notebook::font
