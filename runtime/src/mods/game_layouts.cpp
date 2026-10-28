// The game's own layout art for guest mods (game_layouts.h).
#include "game_layouts.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <list>
#include <mutex>
#include <stdexcept>

#include "item_icons.h"
#include "lyt_build.h"

namespace game_layouts {
namespace {

[[noreturn]] void fail(const std::string& why) { throw std::runtime_error(why); }
uint32_t be16(const Bytes& d, size_t o) {
    if (o + 2 > d.size()) fail("truncated");
    return uint32_t(d[o]) << 8 | d[o + 1];
}
uint32_t be32(const Bytes& d, size_t o) {
    if (o + 4 > d.size()) fail("truncated");
    return uint32_t(d[o]) << 24 | uint32_t(d[o + 1]) << 16 | uint32_t(d[o + 2]) << 8 | d[o + 3];
}
float bef(const Bytes& d, size_t o) {
    const uint32_t v = be32(d, o);
    float f;
    memcpy(&f, &v, 4);
    return std::isfinite(f) ? f : 0.0f;
}
std::string cstr(const Bytes& d, size_t o, size_t limit) {
    std::string s;
    for (size_t i = o; i < d.size() && i < o + limit && d[i]; i++) s += char(d[i]);
    return s;
}

// ---- BC blocks (little-endian, as GX2 stores them) ----
void colour565(uint32_t v, int out[3]) {
    const int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    out[0] = (r << 3) | (r >> 2), out[1] = (g << 2) | (g >> 4), out[2] = (b << 3) | (b >> 2);
}
// BC1 colour block into rgba[16][4]; four_colour: the BC2/BC3 rule (never the 3-colour + transparent mode)
void colour_block(const uint8_t* b, bool four_colour, uint8_t rgba[16][4]) {
    const uint32_t c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8;
    int pal[4][4] = {};
    colour565(c0, pal[0]), colour565(c1, pal[1]);
    pal[0][3] = pal[1][3] = pal[2][3] = pal[3][3] = 255;
    if (four_colour || c0 > c1) {
        for (int c = 0; c < 3; c++) pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3, pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
    } else {
        for (int c = 0; c < 3; c++) pal[2][c] = (pal[0][c] + pal[1][c]) / 2, pal[3][c] = 0;
        pal[3][3] = 0;
    }
    const uint32_t bits = b[4] | b[5] << 8 | b[6] << 16 | uint32_t(b[7]) << 24;
    for (int i = 0; i < 16; i++)
        for (int c = 0; c < 4; c++) rgba[i][c] = uint8_t(pal[(bits >> (2 * i)) & 3][c]);
}
void channel_block(const uint8_t* b, uint8_t out[16]) {
    const int a0 = b[0], a1 = b[1];
    int pal[8] = {a0, a1};
    if (a0 > a1) {
        for (int j = 1; j < 7; j++) pal[j + 1] = ((7 - j) * a0 + j * a1) / 7;
    } else {
        for (int j = 1; j < 5; j++) pal[j + 1] = ((5 - j) * a0 + j * a1) / 5;
        pal[6] = 0, pal[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; i++) out[i] = uint8_t(pal[(bits >> (3 * i)) & 7]);
}

const char* kAllowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_^+-.";
bool name_ok(const std::string& s) {
    return !s.empty() && s.size() <= 48 && s.find_first_not_of(kAllowed) == std::string::npos && s.find("..") == std::string::npos &&
           s[0] != '.';
}

}  // namespace

const Pane* Layout::find(const std::string& name) const {
    for (const Pane& p : panes)
        if (p.name == name) return &p;
    return nullptr;
}

Bytes decode_bc(int kind, const Bytes& blocks, uint32_t w, uint32_t h) {
    const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4, size = (kind == 1 || kind == 4) ? 8 : 16;
    if (blocks.size() < size_t(bw) * bh * size) fail("short BC data");
    Bytes out(size_t(w) * h * 4);
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            const uint8_t* b = &blocks[(size_t(by) * bw + bx) * size];
            uint8_t px[16][4];
            if (kind == 1) {
                colour_block(b, false, px);
            } else if (kind == 2) {
                colour_block(b + 8, true, px);
                for (int i = 0; i < 16; i++) {
                    const int v = (b[i / 2] >> (4 * (i & 1))) & 15;
                    px[i][3] = uint8_t(v * 17);
                }
            } else if (kind == 3) {
                uint8_t a[16];
                channel_block(b, a);
                colour_block(b + 8, true, px);
                for (int i = 0; i < 16; i++) px[i][3] = a[i];
            } else if (kind == 4) {
                uint8_t v[16];
                channel_block(b, v);
                for (int i = 0; i < 16; i++) px[i][0] = px[i][1] = px[i][2] = v[i], px[i][3] = 255;
            } else if (kind == 5) {
                uint8_t r[16], g[16];
                channel_block(b, r), channel_block(b + 8, g);
                for (int i = 0; i < 16; i++) px[i][0] = r[i], px[i][1] = g[i], px[i][2] = 0, px[i][3] = 255;
            } else {
                fail("unknown BC kind");
            }
            for (int i = 0; i < 16; i++) {
                const uint32_t x = bx * 4 + uint32_t(i % 4), y = by * 4 + uint32_t(i / 4);
                if (x < w && y < h) memcpy(&out[(size_t(y) * w + x) * 4], px[i], 4);
            }
        }
    return out;
}

Image decode_bflim(const Bytes& d) {
    if (d.size() < 0x28 || memcmp(&d[d.size() - 0x28], "FLIM", 4) || memcmp(&d[d.size() - 0x14], "imag", 4)) fail("not a texture");
    const size_t f = d.size() - 0x28;
    const uint32_t w = be16(d, f + 0x1C), h = be16(d, f + 0x1E), code = d[f + 0x22], tile = d[f + 0x23], size = be32(d, f + 0x24);
    if (!w || !h || w > 2048 || h > 2048) fail("texture size out of range");
    if (size > f) fail("truncated texture");
    // FLIM format codes (bflim.py): the channel meaning is the game's
    int bc = 0;  // 0: RGBA8
    enum { RGBA, ALPHA, LUMINANCE, LUMINANCE_ALPHA } use = RGBA;
    switch (code) {
        case 0x09: case 0x14: break;
        case 0x0C: case 0x15: bc = 1; break;
        case 0x0D: case 0x16: bc = 2; break;
        case 0x0E: case 0x17: bc = 3; break;
        case 0x0F: bc = 4, use = LUMINANCE; break;
        case 0x10: bc = 4, use = ALPHA; break;
        case 0x11: bc = 5, use = LUMINANCE_ALPHA; break;
        default: fail("texture format " + std::to_string(code) + " is not supported");
    }
    const uint32_t bpp = bc == 0 ? 32 : (bc == 1 || bc == 4) ? 64 : 128;
    const uint32_t ew = bc ? (w + 3) / 4 : w, eh = bc ? (h + 3) / 4 : h;
    const Bytes lin = lyt_build::untile(Bytes(d.begin(), d.begin() + size), ew, eh, bpp, int(tile & 15), uint32_t(tile >> 5));
    if (lin.empty()) fail("unsupported texture tiling");
    Image img;
    img.width = w, img.height = h;
    img.rgba = bc ? decode_bc(bc, lin, w, h) : lin;
    img.srgb = code >= 0x14 && code <= 0x17;
    if (use != RGBA)
        for (size_t i = 0; i < img.rgba.size(); i += 4) {
            uint8_t* p = &img.rgba[i];
            const uint8_t r = p[0], g = p[1];
            if (use == ALPHA) p[0] = p[1] = p[2] = 255, p[3] = r;
            else if (use == LUMINANCE) p[0] = p[1] = p[2] = r, p[3] = 255;
            else p[0] = p[1] = p[2] = r, p[3] = g;
        }
    return img;
}

Layout parse_bflyt(const Bytes& d) {
    if (d.size() < 0x14 || memcmp(d.data(), "FLYT", 4)) fail("not a layout");
    Layout out;
    std::vector<std::string> textures;
    std::vector<std::string> material_names;
    struct Frame { float x, y, sx, sy, w, h; bool visible; uint8_t alpha; };  // origin point, scale, scaled size
    std::vector<Frame> stack{{0, 0, 1, 1, 0, 0, true, 255}};
    Frame last = stack.back();
    size_t o = be16(d, 6);
    const uint32_t sections = be16(d, 0x10);
    for (uint32_t i = 0; i < sections && o + 8 <= d.size(); i++) {
        const std::string magic(reinterpret_cast<const char*>(&d[o]), 4);
        const uint32_t size = be32(d, o + 4);
        if (size < 8 || o + size > d.size()) fail("bad layout section");
        if (magic == "txl1") {
            const uint32_t n = be16(d, o + 8);
            for (uint32_t k = 0; k < n; k++) textures.push_back(cstr(d, o + 12 + be32(d, o + 12 + 4 * k), 64));
        } else if (magic == "mat1") {
            const uint32_t n = be16(d, o + 8);
            for (uint32_t k = 0; k < n; k++) {
                const size_t m = o + be32(d, o + 12 + 4 * k);
                Material mat;
                std::string name = cstr(d, m, 28);
                // NintendoWare pads some authored material names with spaces.
                // Public layout specs contain identifiers, so expose the unpadded name.
                while (!name.empty() && name.back() == ' ') name.pop_back();
                mat.black = {d.at(m + 0x1C), d.at(m + 0x1D), d.at(m + 0x1E), d.at(m + 0x1F)};
                mat.white = {d.at(m + 0x20), d.at(m + 0x21), d.at(m + 0x22), d.at(m + 0x23)};
                const uint32_t flags = be32(d, m + 0x24);
                const uint32_t maps = flags & 3;
                // Text materials have no texture maps; their optional shadow
                // colour block follows the flags (black RGB, white RGBA, pad).
                if (!maps && (flags & 0x20000))
                    mat.shadow_white = {d.at(m + 0x2B), d.at(m + 0x2C), d.at(m + 0x2D), d.at(m + 0x2E)};
                for (uint32_t t = 0; t < maps; t++) {
                    const uint32_t index = be16(d, m + 0x28 + 4 * t);
                    if (index < textures.size()) mat.textures.push_back(textures[index]);
                }
                material_names.push_back(name);
                out.materials[name] = mat;
            }
        } else if (magic == "pan1" || magic == "pic1" || magic == "txt1" || magic == "wnd1" || magic == "prt1" || magic == "bnd1") {
            if (size < 0x54) fail("bad pane");
            const Frame& parent = stack.back();
            Pane p;
            p.kind = magic;
            p.name = cstr(d, o + 0x0C, 24);
            const bool own_visible = d[o + 8] & 1;
            const uint32_t base = d[o + 9];
            p.alpha = uint8_t(d[o + 0x0A] * parent.alpha / 255);
            const float tx = bef(d, o + 0x2C), ty = bef(d, o + 0x30), rz = bef(d, o + 0x40), sx = bef(d, o + 0x44), sy = bef(d, o + 0x48);
            const float w = bef(d, o + 0x4C), h = bef(d, o + 0x50);
            // origin byte: bits 0-1 the pane's own horizontal origin (0 centre, 1 left, 2 right), bits 2-3 vertical
            // (0 centre, 1 top, 2 bottom); bits 4-7 the same for the point of the parent it is placed from
            const uint32_t bh = base & 3, bv = (base >> 2) & 3, ph = (base >> 4) & 3, pv = (base >> 6) & 3;
            const float px = parent.x + (ph == 1 ? -parent.w / 2 : ph == 2 ? parent.w / 2 : 0);
            const float py = parent.y + (pv == 1 ? parent.h / 2 : pv == 2 ? -parent.h / 2 : 0);
            const float ox = px + parent.sx * tx, oy = py + parent.sy * ty;
            p.scale_x = parent.sx * sx, p.scale_y = parent.sy * sy, p.rotation = rz;
            p.width = std::fabs(w * p.scale_x), p.height = std::fabs(h * p.scale_y);
            p.x = ox + (bh == 1 ? p.width / 2 : bh == 2 ? -p.width / 2 : 0);
            p.y = oy + (bv == 1 ? -p.height / 2 : bv == 2 ? p.height / 2 : 0);
            p.visible = parent.visible && own_visible;
            if (magic == "pic1" && size >= 0x66) {
                for (size_t vertex = 0; vertex < 4; ++vertex) {
                    const size_t at = o + 0x54 + vertex * 4;
                    p.colours[vertex] = {d[at], d[at + 1], d[at + 2], d[at + 3]};
                }
                const uint32_t mi = be16(d, o + 0x64);
                if (mi < material_names.size()) {
                    p.material = material_names[mi];
                    const Material& mat = out.materials[p.material];
                    if (!mat.textures.empty()) p.texture = mat.textures[0];
                    if (p.texture.size() > 6 && p.texture.compare(p.texture.size() - 6, 6, ".bflim") == 0) p.texture.resize(p.texture.size() - 6);
                }
            }
            if (magic == "txt1" && size >= 0xA0) {
                p.text.width = bef(d, o + 0x70) * std::fabs(p.scale_x);
                p.text.height = bef(d, o + 0x74) * std::fabs(p.scale_y);
                p.text.shadow = d[o + 0x5E] & 1;
                p.text.shadow_x = bef(d, o + 0x84) * p.scale_x;
                p.text.shadow_y = -bef(d, o + 0x88) * p.scale_y;
                const uint32_t mi = be16(d, o + 0x58);
                if (mi < material_names.size()) {
                    p.material = material_names[mi];
                    p.text.ink = out.materials[p.material].white;
                    const Colour vertex{d[o + 0x94], d[o + 0x95], d[o + 0x96], d[o + 0x97]};
                    const auto& shade = out.materials[p.material].shadow_white;
                    p.text.shade = {uint8_t(vertex.r * shade.r / 255), uint8_t(vertex.g * shade.g / 255),
                                    uint8_t(vertex.b * shade.b / 255), uint8_t(vertex.a * shade.a / 255)};
                }
            }
            last = {ox, oy, p.scale_x, p.scale_y, p.width, p.height, p.visible, p.alpha};
            out.panes.push_back(p);
        } else if (magic == "pas1") {
            stack.push_back(last);
        } else if (magic == "pae1") {
            if (stack.size() > 1) stack.pop_back();
            last = stack.back();
        }
        o += size;
    }
    return out;
}

void apply_material(Image& image, const Material& m) {
    auto to_linear = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    auto to_srgb = [](float v) { return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f; };
    float decode[256];
    for (int v = 0; v < 256; v++) decode[v] = image.srgb ? to_linear(v / 255.0f) : v / 255.0f;
    const float lo[4] = {m.black.r / 255.0f, m.black.g / 255.0f, m.black.b / 255.0f, m.black.a / 255.0f};
    const float hi[4] = {m.white.r / 255.0f, m.white.g / 255.0f, m.white.b / 255.0f, m.white.a / 255.0f};
    for (size_t i = 0; i < image.rgba.size(); i += 4)
        for (int c = 0; c < 4; c++) {
            const float t = c == 3 ? image.rgba[i + c] / 255.0f : decode[image.rgba[i + c]];
            float v = std::clamp(lo[c] + (hi[c] - lo[c]) * t, 0.0f, 1.0f);
            if (c < 3) v = to_srgb(v);
            image.rgba[i + c] = uint8_t(std::lround(v * 255.0f));
        }
    image.srgb = true;
}

Image upscale(const Image& in, uint32_t factor) {
    if (factor < 2 || factor > 4 || !in.width || !in.height) return in;
    const uint32_t w = in.width * factor, h = in.height * factor;
    if (w > 2048 || h > 2048) return in;
    auto lanczos = [](double x) {
        x = std::fabs(x);
        if (x < 1e-9) return 1.0;
        if (x >= 3) return 0.0;
        const double px = 3.14159265358979323846 * x;
        return 3 * std::sin(px) * std::sin(px / 3) / (px * px);
    };
    // premultiplied, linear floats; edges clamped
    std::vector<float> src(size_t(in.width) * in.height * 4);
    for (size_t i = 0; i < src.size(); i += 4) {
        const float a = in.rgba[i + 3] / 255.0f;
        for (int c = 0; c < 3; c++) src[i + c] = in.rgba[i + c] / 255.0f * a;
        src[i + 3] = a;
    }
    auto pass = [&](const std::vector<float>& from, uint32_t fw, uint32_t fh, bool horizontal) {
        const uint32_t tw = horizontal ? fw * factor : fw, th = horizontal ? fh : fh * factor;
        const uint32_t n = horizontal ? fw : fh;
        std::vector<float> to(size_t(tw) * th * 4, 0.0f);
        const uint32_t outer = horizontal ? th : tw, inner = horizontal ? tw : th;
        for (uint32_t o = 0; o < outer; o++)
            for (uint32_t i = 0; i < inner; i++) {
                const double centre = (i + 0.5) / factor - 0.5;
                const int first = int(std::floor(centre)) - 2;
                double acc[4] = {0, 0, 0, 0}, sum = 0;
                for (int k = first; k < first + 6; k++) {
                    const double wgt = lanczos(centre - k);
                    const uint32_t kk = uint32_t(std::clamp(k, 0, int(n) - 1));
                    const size_t at = horizontal ? (size_t(o) * fw + kk) * 4 : (size_t(kk) * fw + o) * 4;
                    for (int c = 0; c < 4; c++) acc[c] += wgt * from[at + c];
                    sum += wgt;
                }
                const size_t out = horizontal ? (size_t(o) * tw + i) * 4 : (size_t(i) * tw + o) * 4;
                for (int c = 0; c < 4; c++) to[out + c] = float(acc[c] / sum);
            }
        return to;
    };
    const std::vector<float> wide = pass(src, in.width, in.height, true);
    const std::vector<float> full = pass(wide, w, in.height, false);
    // straight colour again, then sharpened as an enlargement needs: an unsharp mask on the colour (blur:
    // two box passes of the factor's radius, amount 1.2) and a steeper alpha ramp (x1.8 about one half) for
    // crisp outlines, as the soft source edges would otherwise stay blurred at the larger size
    std::vector<float> rgb(size_t(w) * h * 3), alpha(size_t(w) * h);
    for (size_t p = 0; p < alpha.size(); p++) {
        const float a = std::clamp(full[p * 4 + 3], 0.0f, 1.0f);
        alpha[p] = a;
        for (int c = 0; c < 3; c++) rgb[p * 3 + c] = a > 1e-4f ? std::clamp(full[p * 4 + c] / a, 0.0f, 1.0f) : 0.0f;
    }
    // the blur is alpha-weighted (premultiplied colour and alpha blurred, then divided), so invisible
    // texels never pull the colour at an outline
    std::vector<float> blur(size_t(w) * h * 4), tmp(blur.size());
    for (size_t p = 0; p < alpha.size(); p++) {
        for (int c = 0; c < 3; c++) blur[p * 4 + c] = rgb[p * 3 + c] * alpha[p];
        blur[p * 4 + 3] = alpha[p];
    }
    const int r = int(factor);
    for (int round = 0; round < 2; round++) {
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++)
                for (int c = 0; c < 4; c++) {
                    float sum = 0;
                    for (int k = -r; k <= r; k++) sum += blur[(size_t(y) * w + uint32_t(std::clamp(int(x) + k, 0, int(w) - 1))) * 4 + c];
                    tmp[(size_t(y) * w + x) * 4 + c] = sum / float(2 * r + 1);
                }
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++)
                for (int c = 0; c < 4; c++) {
                    float sum = 0;
                    for (int k = -r; k <= r; k++) sum += tmp[(size_t(std::clamp(int(y) + k, 0, int(h) - 1)) * w + x) * 4 + c];
                    blur[(size_t(y) * w + x) * 4 + c] = sum / float(2 * r + 1);
                }
    }
    Image out;
    out.width = w, out.height = h, out.srgb = in.srgb;
    out.rgba.resize(size_t(w) * h * 4);
    for (size_t p = 0; p < alpha.size(); p++) {
        for (int c = 0; c < 3; c++) {
            const float b = blur[p * 4 + 3] > 1e-4f ? blur[p * 4 + c] / blur[p * 4 + 3] : rgb[p * 3 + c];
            const float v = rgb[p * 3 + c] + 1.2f * (rgb[p * 3 + c] - b);
            out.rgba[p * 4 + c] = uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
        }
        out.rgba[p * 4 + 3] = uint8_t(std::lround(std::clamp((alpha[p] - 0.5f) * 1.8f + 0.5f, 0.0f, 1.0f) * 255.0f));
    }
    return out;
}

bool split_spec(const std::string& spec, std::string& layout, std::string& item, std::string& material, uint32_t& factor) {
    factor = 1;
    std::string rest = spec;
    if (rest.size() > 2 && rest[rest.size() - 2] == '@') {
        const char f = rest.back();
        if (f < '2' || f > '4') return false;
        factor = uint32_t(f - '0');
        rest.resize(rest.size() - 2);
    }
    return split_spec(rest, layout, item, material);
}

bool split_spec(const std::string& spec, std::string& layout, std::string& item, std::string& material) {
    layout.clear(), item.clear(), material.clear();
    if (spec.size() > 128) return false;
    const size_t slash = spec.find('/');
    if (slash == std::string::npos || spec.find('/', slash + 1) != std::string::npos) return false;
    layout = spec.substr(0, slash);
    item = spec.substr(slash + 1);
    if (const size_t hash = item.find('#'); hash != std::string::npos) {
        material = item.substr(hash + 1);
        item = item.substr(0, hash);
        if (!name_ok(material)) return false;
    }
    return name_ok(layout) && name_ok(item);
}

// ---- the port ----
namespace {
std::mutex g_mu;
std::string g_pack_path;
std::map<std::string, Bytes> g_pack;  // the pack's members, read once
bool g_pack_read = false;
struct Archive { std::string name; std::map<std::string, Bytes> files; Layout layout; bool has_layout = false; };
std::list<Archive> g_archives;  // most recently used first, at most 8
constexpr size_t kMaxArchives = 8;

bool lower_ends(const std::string& s, const std::string& prefix, const std::string& suffix) {
    std::string l = s;
    for (char& c : l) c = char(std::tolower(static_cast<unsigned char>(c)));
    return l.size() >= prefix.size() + suffix.size() && l.compare(0, prefix.size(), prefix) == 0 &&
           l.compare(l.size() - suffix.size(), suffix.size(), suffix) == 0;
}

Archive* archive(const std::string& layout, std::string& error) {
    for (auto it = g_archives.begin(); it != g_archives.end(); ++it)
        if (it->name == layout) {
            g_archives.splice(g_archives.begin(), g_archives, it);
            return &g_archives.front();
        }
    if (g_pack_path.empty()) {
        error = "the game's 2D pack is not open yet";
        return nullptr;
    }
    try {
        if (!g_pack_read) {
            std::ifstream f(g_pack_path, std::ios::binary);
            Bytes data((std::istreambuf_iterator<char>(f)), {});
            if (data.empty()) fail("can't read the game's 2D pack");
            g_pack = item_icons::read_sarc(data);
            g_pack_read = true;
        }
        const auto it = g_pack.find(layout + ".szs");
        if (it == g_pack.end()) fail("no layout " + layout);
        Archive a;
        a.name = layout;
        a.files = item_icons::read_sarc(item_icons::yaz0_decode(it->second));
        if (const auto l = a.files.find("blyt/" + layout + ".bflyt"); l != a.files.end()) a.layout = parse_bflyt(l->second), a.has_layout = true;
        g_archives.push_front(std::move(a));
        if (g_archives.size() > kMaxArchives) g_archives.pop_back();
        return &g_archives.front();
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}
}  // namespace

void note_open(const std::string& guest_path, const std::string& host_path) {
    const size_t slash = guest_path.find_last_of('/');
    const std::string name = slash == std::string::npos ? guest_path : guest_path.substr(slash + 1);
    if (!lower_ends(name, "permanent_2d_", ".pack")) return;
    std::lock_guard lock(g_mu);
    if (host_path == g_pack_path) return;
    g_pack_path = host_path, g_pack.clear(), g_pack_read = false, g_archives.clear();
}

bool texture(const std::string& spec, Image& out, std::string& error) {
    std::string layout, item, material;
    uint32_t factor = 1;
    if (!split_spec(spec, layout, item, material, factor)) {
        error = "invalid layout texture name";
        return false;
    }
    std::lock_guard lock(g_mu);
    Archive* a = archive(layout, error);
    if (!a) return false;
    const auto it = a->files.find("timg/" + item + ".bflim");
    if (it == a->files.end()) {
        error = "no texture " + item + " in " + layout;
        return false;
    }
    try {
        out = decode_bflim(it->second);
        if (!material.empty()) {
            const auto m = a->layout.materials.find(material);
            if (m == a->layout.materials.end()) {
                error = "no material " + material + " in " + layout;
                return false;
            }
            apply_material(out, m->second);
        }
        out = upscale(out, factor);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    return true;
}

bool texture_size(const std::string& spec, uint32_t& width, uint32_t& height) {
    std::string layout, item, material, error;
    if (!split_spec(spec, layout, item, material) || !material.empty()) return false;
    std::lock_guard lock(g_mu);
    Archive* a = archive(layout, error);
    if (!a) return false;
    const auto it = a->files.find("timg/" + item + ".bflim");
    if (it == a->files.end() || it->second.size() < 0x28 || memcmp(&it->second[it->second.size() - 0x28], "FLIM", 4)) return false;
    const size_t f = it->second.size() - 0x28;
    width = be16(it->second, f + 0x1C), height = be16(it->second, f + 0x1E);
    return true;
}

bool pane(const std::string& spec, Pane& out) {
    std::string layout, item, material, error;
    if (!split_spec(spec, layout, item, material) || !material.empty()) return false;
    std::lock_guard lock(g_mu);
    Archive* a = archive(layout, error);
    if (!a || !a->has_layout) return false;
    const Pane* p = a->layout.find(item);
    if (!p) return false;
    out = *p;
    return true;
}

bool file(const std::string& archive_name, const std::string& member, Bytes& out, std::string& error) {
    if (!name_ok(archive_name)) {
        error = "bad archive name";
        return false;
    }
    std::lock_guard lock(g_mu);
    Archive* a = archive(archive_name, error);
    if (!a) return false;
    const auto it = a->files.find(member);
    if (it == a->files.end()) {
        error = "no " + member + " in " + archive_name;
        return false;
    }
    out = it->second;
    return true;
}

}  // namespace game_layouts
