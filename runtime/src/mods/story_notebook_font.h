// The story notebook's text in the game's own menu font (story_notebook_page.h draws Text commands;
// this turns them into pictures of the game's glyphs). Pure code: the glyphs come from the caller
// (game_glyph::read_glyph on the fonts the game loaded, story_notebook.cpp), the result is a host
// image plus picture commands; unit-tested in runtime/tools/story_notebook_test.cpp.
//
// Each distinct line is composed once into a line atlas (an RGBA sheet: the letters in their colours,
// the icon font's own colours for the button pictures); a Text command becomes one picture of its
// line, scaled to the command's size. The atlas is rebuilt only when the page shows a line it doesn't
// hold yet.
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

// The line atlas: every line the page shows, composed once.
class Atlas {
public:
    static constexpr int kWidth = 2048, kMaxHeight = 2048, kPad = 2;
    static constexpr float kMinBorder = 1.0f;  // a bold line's border in pixels, at least
    struct Cell {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        int width = 0, height = 0;  // pixels
        int margin = 0;  // pixels left and right of the line in the cell (a sized line's border room)
    };
    // true if every line of `lines` (line_key) is in the atlas, composed with this look and these fonts
    bool holds(const std::vector<std::string>& lines, const Prompts& look, uint64_t fonts) const {
        if (look != look_ || fonts != fonts_ || !image_) return false;
        for (const std::string& l : lines)
            if (!cells_.count(l)) return false;
        return true;
    }
    // Compose the lines (line_key: the main font, and `fallback` for characters the main font lacks;
    // the icon font for the button pictures) at `scale` of the font's own size, sized lines
    // (sized_key) at their own height. Lines that don't fit any more are left out (drawn with the
    // overlay font).
    void build(const std::vector<std::string>& lines, const Face& main, const Face* fallback, const Face* icons, const Prompts& look,
               uint64_t fonts, float scale = 1.0f) {
        cells_.clear();
        look_ = look, fonts_ = fonts;
        const int base = std::max(1, int(std::lround(main.cell_height * scale)));
        auto image = std::make_shared<guestmods::hud::Image>();
        image->width = kWidth;
        struct Place {
            const std::string* key;
            int x, y, w, h, m;
        };
        std::vector<Place> places;
        for (const std::string& key : lines) places.push_back({&key, 0, 0, 0, height_of(key, base), sized(key) ? int(std::ceil(kMinBorder)) : 0});
        // unique lines, the tallest first (rows of about one height)
        std::sort(places.begin(), places.end(), [](const Place& a, const Place& b) { return a.h != b.h ? a.h > b.h : *a.key < *b.key; });
        places.erase(std::unique(places.begin(), places.end(), [](const Place& a, const Place& b) { return *a.key == *b.key; }), places.end());
        if (main.cell_height <= 0) places.clear();
        // layout: rows left to right, each as tall as its first (tallest) line
        int x = kPad, y = kPad, row = 0, used = 0;
        std::vector<Place> kept;
        for (Place p : places) {
            const std::string text = p.key->substr(0, p.key->rfind('\x1F'));
            p.w = std::max(1, line_width(text, main, fallback, p.h)) + 2 * p.m;
            if (p.w + 2 * kPad > kWidth) continue;
            if (x + p.w + kPad > kWidth) x = kPad, y += row + kPad, row = 0;
            if (y + p.h + kPad > kMaxHeight) break;
            p.x = x, p.y = y;
            row = std::max(row, p.h);
            kept.push_back(p);
            x += p.w + kPad;
            used = y + row + kPad;
        }
        image->height = std::max(4, used);
        image->rgba.assign(size_t(image->width) * image->height * 4, 0);
        for (const Place& p : kept) {
            Cell c;
            c.width = p.w, c.height = p.h, c.margin = p.m;
            c.u0 = float(p.x) / kWidth, c.v0 = float(p.y) / image->height;
            c.u1 = float(p.x + p.w) / kWidth, c.v1 = float(p.y + p.h) / image->height;
            const size_t sep = p.key->rfind('\x1F');
            const uint32_t rgb = uint32_t(std::strtoul(p.key->c_str() + sep + 1, nullptr, 16));
            compose(*image, p.key->substr(0, sep), rgb, main, fallback, icons, look, p.x + p.m, p.y, p.w - 2 * p.m, p.h, p.m);
            cells_[*p.key] = c;
        }
        image_ = image;
        generation_++;
    }
    const Cell* cell(const std::string& key) const {
        auto it = cells_.find(key);
        return it == cells_.end() ? nullptr : &it->second;
    }
    std::shared_ptr<const guestmods::hud::Image> image() const { return image_; }
    uint64_t generation() const { return generation_; }
    void clear() {
        cells_.clear();
        image_.reset();
        generation_++;
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

    std::map<std::string, Cell> cells_;
    std::shared_ptr<guestmods::hud::Image> image_;
    Prompts look_;
    uint64_t fonts_ = 0, generation_ = 0;
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

// The page's Text commands as pictures of their atlas lines (kind Picture, u/v set; the caller hands
// them to the HUD store with the atlas image). `font_scale`: the drawn line height per text size.
// Lines not in the atlas stay Text (the overlay font); a button without the atlas becomes a disc
// with its letter.
inline std::vector<Command> to_pictures(const std::vector<Command>& in, const Atlas* atlas, float font_scale = 1.0f, float pixels = 0.0f) {
    std::vector<Command> out;
    out.reserve(in.size());
    for (const Command& c : in) {
        if (c.kind != Command::Text) {
            out.push_back(c);
            continue;
        }
        const Atlas::Cell* cell = atlas ? atlas->cell(key_of(c, font_scale, pixels)) : nullptr;
        if (!cell) {
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
        Command p;
        p.kind = Command::Picture;
        const float h = c.size * font_scale, k = h / float(cell->height);
        p.x = c.x - float(cell->margin) * k, p.y = c.y - (h - c.size) * 0.5f, p.w = float(cell->width) * k, p.h = h;
        p.u0 = cell->u0, p.v0 = cell->v0, p.u1 = cell->u1, p.v1 = cell->v1;
        p.rgba = 0xFFFFFF00u | (c.rgba & 0xFF);  // the colours are in the atlas; the alpha fades it
        p.anchor = c.anchor, p.blend = c.blend;
        out.push_back(p);
    }
    return out;
}

}  // namespace story_notebook::font
