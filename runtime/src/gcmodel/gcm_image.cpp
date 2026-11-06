/* SPDX-License-Identifier: MPL-2.0
 * From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (v0.9.9, MPL-2.0), runtime/src/gcmodel/, taken over
 * as it is; only the glue to this port changed. */
#include "gcm_image.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.h"
#include "gfx/texture_levels.h"

namespace gcmodel {

// ---- resizing (as Pillow: premultiplied alpha, separable filters)

namespace {
double lanczos3(double x) {
    if (x == 0) return 1.0;
    if (x <= -3.0 || x >= 3.0) return 0.0;
    const double px = M_PI * x;
    return 3.0 * std::sin(px) * std::sin(px / 3.0) / (px * px);
}

struct Taps {
    std::vector<int> first;
    std::vector<std::vector<double>> w;
};

Taps taps(int in, int out, bool box) {
    Taps t;
    const double scale = (double)in / out, fs = std::max(scale, 1.0), support = (box ? 0.5 : 3.0) * fs;
    for (int i = 0; i < out; i++) {
        const double center = (i + 0.5) * scale;
        const int x0 = std::max(0, (int)std::floor(center - support)), x1 = std::min(in, (int)std::ceil(center + support));
        std::vector<double> w;
        double sum = 0;
        for (int j = x0; j < x1; j++) {
            const double x = (j - center + 0.5) / fs;
            const double v = box ? (x >= -0.5 && x < 0.5 ? 1.0 : 0.0) : lanczos3(x);
            w.push_back(v);
            sum += v;
        }
        if (sum != 0)
            for (double& v : w) v /= sum;
        t.first.push_back(x0);
        t.w.push_back(w);
    }
    return t;
}

Image resize(const Image& src, int w, int h, bool box) {
    if (src.w == w && src.h == h) return src;
    // premultiplied, as floats
    std::vector<double> a((size_t)src.w * src.h * 4);
    for (size_t i = 0; i < (size_t)src.w * src.h; i++) {
        const double al = src.px[i * 4 + 3] / 255.0;
        for (int c = 0; c < 3; c++) a[i * 4 + c] = src.px[i * 4 + c] * al;
        a[i * 4 + 3] = src.px[i * 4 + 3];
    }
    const Taps tx = taps(src.w, w, box), ty = taps(src.h, h, box);
    std::vector<double> mid((size_t)w * src.h * 4, 0);
    for (int y = 0; y < src.h; y++)
        for (int x = 0; x < w; x++) {
            double acc[4] = {};
            for (size_t k = 0; k < tx.w[x].size(); k++) {
                const double* s = &a[((size_t)y * src.w + tx.first[x] + k) * 4];
                for (int c = 0; c < 4; c++) acc[c] += s[c] * tx.w[x][k];
            }
            for (int c = 0; c < 4; c++) mid[((size_t)y * w + x) * 4 + c] = acc[c];
        }
    Image out(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double acc[4] = {};
            for (size_t k = 0; k < ty.w[y].size(); k++) {
                const double* s = &mid[((size_t)(ty.first[y] + k) * w + x) * 4];
                for (int c = 0; c < 4; c++) acc[c] += s[c] * ty.w[y][k];
            }
            const double al = std::clamp(acc[3], 0.0, 255.0);
            uint8_t* d = out.at(x, y);
            for (int c = 0; c < 3; c++) d[c] = (uint8_t)std::clamp(std::lround(al > 0 ? acc[c] * 255.0 / al : 0.0), 0L, 255L);
            d[3] = (uint8_t)std::lround(al);
        }
    return out;
}

// k x k minimum or maximum of one channel (edges clamped)
std::vector<uint8_t> rank_filter(const std::vector<uint8_t>& ch, int w, int h, int k, bool mx) {
    const int r = k / 2;
    std::vector<uint8_t> tmp(ch.size()), out(ch.size());
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t v = mx ? 0 : 255;
            for (int d = -r; d <= r; d++) {
                const uint8_t s = ch[(size_t)y * w + std::clamp(x + d, 0, w - 1)];
                v = mx ? std::max(v, s) : std::min(v, s);
            }
            tmp[(size_t)y * w + x] = v;
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t v = mx ? 0 : 255;
            for (int d = -r; d <= r; d++) {
                const uint8_t s = tmp[(size_t)std::clamp(y + d, 0, h - 1) * w + x];
                v = mx ? std::max(v, s) : std::min(v, s);
            }
            out[(size_t)y * w + x] = v;
        }
    return out;
}
}  // namespace

Image resize_lanczos(const Image& src, int w, int h) { return resize(src, w, h, false); }
Image resize_box(const Image& src, int w, int h) { return resize(src, w, h, true); }

Image upscale_sharp(const Image& src, int w, int h) {
    const double scale = std::max((double)w / src.w, (double)h / src.h);
    Image up = resize_lanczos(src, w, h);
    const int k = std::max(3, (int)std::lround(scale) | 1);
    for (int c = 0; c < 4; c++) {
        std::vector<uint8_t> ch((size_t)w * h);
        for (size_t i = 0; i < ch.size(); i++) ch[i] = up.px[i * 4 + c];
        const auto lo = rank_filter(ch, w, h, k, false), hi = rank_filter(ch, w, h, k, true);
        for (size_t i = 0; i < ch.size(); i++) {
            const double f = ch[i], span = (double)hi[i] - lo[i], mid = ((double)hi[i] + lo[i]) / 2;
            if (span <= 24) continue;
            double t = std::clamp((f - mid) / std::max(span, 1.0) * 2.2 + 0.5, 0.0, 1.0);
            t = t * t * (3 - 2 * t);
            up.px[i * 4 + c] = (uint8_t)std::clamp(lo[i] + span * t, 0.0, 255.0);
        }
    }
    return up;
}

// ---- BC

namespace {
using Block = std::array<std::array<uint8_t, 4>, 16>;

std::vector<Block> blocks(const Image& img, int& bw, int& bh) {
    bw = (img.w + 3) / 4, bh = (img.h + 3) / 4;
    std::vector<Block> out((size_t)bw * bh);
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++)
            for (int i = 0; i < 16; i++) {
                const int x = std::min(bx * 4 + i % 4, img.w - 1), y = std::min(by * 4 + i / 4, img.h - 1);
                memcpy(out[(size_t)by * bw + bx][i].data(), img.at(x, y), 4);
            }
    return out;
}

std::array<int, 3> rgb565(int v) { return {(v >> 11 & 31) * 255 / 31, (v >> 5 & 63) * 255 / 63, (v & 31) * 255 / 31}; }

void enc_color(const Block& px, bool bc1Alpha, uint8_t* out) {
    double rgb[16][3], mean[3] = {};
    for (int i = 0; i < 16; i++)
        for (int c = 0; c < 3; c++) rgb[i][c] = px[i][c], mean[c] += px[i][c] / 16.0;
    double cov[3][3] = {};
    for (int i = 0; i < 16; i++)
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++) cov[a][b] += (rgb[i][a] - mean[a]) * (rgb[i][b] - mean[b]);
    double axis[3] = {1, 1, 1};
    for (int it = 0; it < 8; it++) {
        double n[3];
        for (int a = 0; a < 3; a++) n[a] = cov[a][0] * axis[0] + cov[a][1] * axis[1] + cov[a][2] * axis[2];
        const double len = std::max(std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), 1e-6);
        for (int a = 0; a < 3; a++) axis[a] = n[a] / len;
    }
    double tmin = 1e30, tmax = -1e30;
    for (int i = 0; i < 16; i++) {
        const double t = (rgb[i][0] - mean[0]) * axis[0] + (rgb[i][1] - mean[1]) * axis[1] + (rgb[i][2] - mean[2]) * axis[2];
        tmin = std::min(tmin, t), tmax = std::max(tmax, t);
    }
    auto q565 = [](const double c[3]) {
        auto cl = [](double v) { return std::clamp(v, 0.0, 255.0); };
        const int r = (int)std::nearbyint(cl(c[0]) * 31 / 255), g = (int)std::nearbyint(cl(c[1]) * 63 / 255),
                  b = (int)std::nearbyint(cl(c[2]) * 31 / 255);
        return r << 11 | g << 5 | b;
    };
    double lo[3], hi[3];
    for (int a = 0; a < 3; a++) lo[a] = mean[a] + axis[a] * tmin, hi[a] = mean[a] + axis[a] * tmax;
    int c0 = q565(hi), c1 = q565(lo);
    bool hasAlpha = false;
    if (bc1Alpha)
        for (int i = 0; i < 16; i++) hasAlpha |= px[i][3] < 128;
    const bool swap = hasAlpha ? c0 > c1 : c0 < c1;
    if (swap) std::swap(c0, c1);
    if (c0 == c1 && !hasAlpha && c0 < 0xFFFF) c0++;
    const auto a = rgb565(c0), b = rgb565(c1);
    const bool four = !hasAlpha;
    double pal[4][3];
    for (int k = 0; k < 3; k++) {
        pal[0][k] = a[k], pal[1][k] = b[k];
        pal[2][k] = four ? (2.0 * a[k] + b[k]) / 3 : (a[k] + b[k]) / 2.0;
        pal[3][k] = four ? (a[k] + 2.0 * b[k]) / 3 : 1e9;
    }
    uint32_t bits = 0;
    for (int i = 0; i < 16; i++) {
        int best = 0;
        double bd = 1e300;
        for (int p = 0; p < 4; p++) {
            double d = 0;
            for (int k = 0; k < 3; k++) d += (rgb[i][k] - pal[p][k]) * (rgb[i][k] - pal[p][k]);
            if (d < bd) bd = d, best = p;
        }
        if (bc1Alpha) {
            if (hasAlpha && px[i][3] < 128) best = 3;
            else if (best == 3) best = 2;
        }
        bits |= (uint32_t)best << (2 * i);
    }
    out[0] = (uint8_t)(c0 & 255), out[1] = (uint8_t)(c0 >> 8), out[2] = (uint8_t)(c1 & 255), out[3] = (uint8_t)(c1 >> 8);
    for (int i = 0; i < 4; i++) out[4 + i] = (uint8_t)(bits >> (8 * i));
}

void enc_alpha(const int v[16], uint8_t* out) {
    int a0 = *std::max_element(v, v + 16), a1 = *std::min_element(v, v + 16);
    if (a0 == a1) a0 = std::min(a0 + 1, 255);
    if (a0 == a1) a1 = a0 - 1;
    int pal[8] = {a0, a1};
    for (int i = 1; i < 7; i++) pal[1 + i] = ((7 - i) * a0 + i * a1) / 7;
    uint64_t bits = 0;
    for (int i = 0; i < 16; i++) {
        int best = 0, bd = 1 << 30;
        for (int p = 0; p < 8; p++) {
            const int d = std::abs(v[i] - pal[p]);
            if (d < bd) bd = d, best = p;
        }
        bits |= (uint64_t)best << (3 * i);
    }
    out[0] = (uint8_t)a0, out[1] = (uint8_t)a1;
    for (int i = 0; i < 6; i++) out[2 + i] = (uint8_t)(bits >> (8 * i));
}

void dec_color(const uint8_t* b, bool bc1, uint8_t out[16][4]) {
    const int c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8;
    const uint32_t bits = b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24;
    const auto a = rgb565(c0), c = rgb565(c1);
    const bool four = c0 > c1 || !bc1;
    int pal[4][4];
    for (int k = 0; k < 3; k++) {
        pal[0][k] = a[k], pal[1][k] = c[k];
        pal[2][k] = four ? (2 * a[k] + c[k]) / 3 : (a[k] + c[k]) / 2;
        pal[3][k] = four ? (a[k] + 2 * c[k]) / 3 : 0;
    }
    pal[0][3] = pal[1][3] = pal[2][3] = 255;
    pal[3][3] = (bc1 && !four) ? 0 : 255;
    for (int i = 0; i < 16; i++) {
        const int idx = bits >> (2 * i) & 3;
        for (int k = 0; k < 4; k++) out[i][k] = (uint8_t)pal[idx][k];
    }
}

void dec_alpha(const uint8_t* b, uint8_t out[16]) {
    const int a0 = b[0], a1 = b[1];
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
    int pal[8] = {a0, a1};
    if (a0 > a1) {
        for (int i = 1; i < 7; i++) pal[1 + i] = ((7 - i) * a0 + i * a1) / 7;
    } else {
        for (int i = 1; i < 5; i++) pal[1 + i] = ((5 - i) * a0 + i * a1) / 5;
        pal[6] = 0, pal[7] = 255;
    }
    for (int i = 0; i < 16; i++) out[i] = (uint8_t)pal[bits >> (3 * i) & 7];
}
}  // namespace

Bytes bc_encode(uint32_t fmt, const Image& img) {
    const uint32_t f = fmt & 0x3F;
    int bw, bh;
    const auto bl = blocks(img, bw, bh);
    const size_t bs = (f == 0x31 || f == 0x34) ? 8 : 16;
    Bytes out(bl.size() * bs);
    for (size_t i = 0; i < bl.size(); i++) {
        uint8_t* o = &out[i * bs];
        int ch[16];
        switch (f) {
            case 0x31: enc_color(bl[i], true, o); break;
            case 0x33:
                for (int k = 0; k < 16; k++) ch[k] = bl[i][k][3];
                enc_alpha(ch, o);
                enc_color(bl[i], false, o + 8);
                break;
            case 0x34:
                for (int k = 0; k < 16; k++) ch[k] = bl[i][k][0];
                enc_alpha(ch, o);
                break;
            case 0x35:
                for (int k = 0; k < 16; k++) ch[k] = bl[i][k][0];
                enc_alpha(ch, o);
                for (int k = 0; k < 16; k++) ch[k] = bl[i][k][1];
                enc_alpha(ch, o + 8);
                break;
            default: throw Error("texture format not handled");
        }
    }
    return out;
}

Image bc_decode(uint32_t fmt, int w, int h, const Bytes& data) {
    const uint32_t f = fmt & 0x3F;
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    const size_t bs = (f == 0x31 || f == 0x34) ? 8 : 16;
    if (data.size() < (size_t)bw * bh * bs) throw Error("texture data too short");
    Image img(w, h);
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++) {
            const uint8_t* b = &data[((size_t)by * bw + bx) * bs];
            uint8_t px[16][4], a[16], g[16];
            switch (f) {
                case 0x31: dec_color(b, true, px); break;
                case 0x33:
                    dec_color(b + 8, false, px);
                    dec_alpha(b, a);
                    for (int i = 0; i < 16; i++) px[i][3] = a[i];
                    break;
                case 0x34:
                    dec_alpha(b, a);
                    for (int i = 0; i < 16; i++) px[i][0] = px[i][1] = px[i][2] = a[i], px[i][3] = 255;
                    break;
                case 0x35:
                    dec_alpha(b, a);
                    dec_alpha(b + 8, g);
                    for (int i = 0; i < 16; i++) px[i][0] = a[i], px[i][1] = g[i], px[i][2] = 0, px[i][3] = 255;
                    break;
                default: throw Error("texture format not handled");
            }
            for (int i = 0; i < 16; i++) {
                const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
                if (x < w && y < h) memcpy(img.at(x, y), px[i], 4);
            }
        }
    return img;
}

// ---- GX2 tiling

namespace {
struct Level {
    uint32_t base, size, pitch, height, bw, bh;
    Latte::E_HWTILEMODE tm;
    uint32_t pipe, bank;
};

std::vector<Level> levels(uint32_t fmt, int w, int h, int mips, uint32_t tileMode, uint32_t swizzle) {
    const uint32_t imageBase = 0x10000000, mipBase = 0x20000000, swz = swizzle & 0x700;
    LatteAddrLib::AddrSurfaceInfo_OUT info0{};
    LatteAddrLib::GX2CalculateSurfaceInfo((Latte::E_GX2SURFFMT)fmt, w, h, 1, Latte::E_DIM::DIM_2D, (Latte::E_GX2TILEMODE)tileMode, 0, 0, &info0);
    const uint32_t hwTile = (uint32_t)info0.hwTileMode;
    const bool bc = (fmt & 0x3F) >= 0x31 && (fmt & 0x3F) <= 0x35;
    std::vector<Level> out;
    for (int level = 0; level < mips; level++) {
        LatteAddrLib::AddrSurfaceInfo_OUT info{};
        LatteAddrLib::GX2CalculateSurfaceInfo((Latte::E_GX2SURFFMT)fmt, w, h, 1, Latte::E_DIM::DIM_2D, (Latte::E_GX2TILEMODE)tileMode, 0,
                                              level, &info);
        const gfx::texture_levels::Level at = gfx::texture_levels::level(imageBase, mipBase | swz, fmt, w, h, 1, (uint32_t)Latte::E_DIM::DIM_2D, hwTile,
                                                               swz, level);
        const uint32_t lw = std::max(w >> level, 1), lh = std::max(h >> level, 1);
        out.push_back({level == 0 ? 0u : at.address - mipBase, (uint32_t)info.surfSize, info.pitch, info.height,
                       bc ? (lw + 3) / 4 : lw, bc ? (lh + 3) / 4 : lh, (Latte::E_HWTILEMODE)info.hwTileMode, (at.swizzle >> 8) & 1u,
                       (at.swizzle >> 9) & 3u});
    }
    return out;
}

uint32_t block_bytes(uint32_t fmt) {
    const uint32_t f = fmt & 0x3F;
    if (f >= 0x31 && f <= 0x35) return (f == 0x31 || f == 0x34) ? 8 : 16;
    return f == 0x01 ? 1 : (f == 0x05 || f == 0x07 || f == 0x08) ? 2 : 4;
}

template <typename Fn>
void each_block(const Level& L, uint32_t bpp, Fn fn) {
    LatteAddrLib::CachedSurfaceAddrInfo ci;
    const bool macro = Latte::TM_IsMacroTiled(L.tm);
    if (macro) LatteAddrLib::SetupCachedSurfaceAddrInfo(&ci, 0, 0, bpp, L.pitch, L.height, 1, 1, L.tm, false, L.pipe, L.bank);
    for (uint32_t y = 0; y < L.bh; y++)
        for (uint32_t x = 0; x < L.bw; x++) {
            uint32_t off;
            if (L.tm == Latte::E_HWTILEMODE::TM_LINEAR_GENERAL || L.tm == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED)
                off = LatteAddrLib::ComputeSurfaceAddrFromCoordLinear(x, y, 0, 0, bpp, L.pitch, L.height, 1);
            else if (!macro)
                off = LatteAddrLib::ComputeSurfaceAddrFromCoordMicroTiled(x, y, 0, bpp, L.pitch, L.height, L.tm, false);
            else
                off = LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached(x, y, &ci);
            fn(off);
        }
}
}  // namespace

void gx2_tile(uint32_t fmt, int w, int h, int mips, uint32_t tileMode, uint32_t swizzle, const Bytes& linear, Bytes& image,
              Bytes& mipData, size_t imageSize, size_t mipSize) {
    const uint32_t bb = block_bytes(fmt);
    image.assign(imageSize, 0);
    mipData.assign(mipSize, 0);
    size_t lin = 0;
    const auto ls = levels(fmt, w, h, mips, tileMode, swizzle);
    for (size_t li = 0; li < ls.size(); li++) {
        const Level& L = ls[li];
        Bytes& dst = li == 0 ? image : mipData;
        each_block(L, bb * 8, [&](uint32_t off) {
            const size_t at = (size_t)L.base + off;
            if (at + bb <= dst.size() && lin + bb <= linear.size()) memcpy(&dst[at], &linear[lin], bb);
            lin += bb;
        });
    }
}

size_t gx2_surface_size(uint32_t fmt, int w, int h, uint32_t tileMode) {
    LatteAddrLib::AddrSurfaceInfo_OUT info{};
    LatteAddrLib::GX2CalculateSurfaceInfo((Latte::E_GX2SURFFMT)fmt, w, h, 1, Latte::E_DIM::DIM_2D, (Latte::E_GX2TILEMODE)tileMode, 0, 0, &info);
    return info.surfSize;
}

Bytes gx2_untile_level0(uint32_t fmt, int w, int h, uint32_t tileMode, uint32_t swizzle, const uint8_t* image, size_t imageSize) {
    const uint32_t bb = block_bytes(fmt);
    Bytes out;
    const Level L = levels(fmt, w, h, 1, tileMode, swizzle)[0];
    out.reserve((size_t)L.bw * L.bh * bb);
    each_block(L, bb * 8, [&](uint32_t off) {
        const size_t n = out.size();
        out.resize(n + bb, 0);
        if (off + bb <= imageSize) memcpy(&out[n], image + off, bb);
    });
    return out;
}

// ---- see-through texels

std::vector<uint8_t> raster_uv(const std::vector<std::array<std::array<double, 2>, 3>>& tris, int w, int h) {
    std::vector<uint8_t> m((size_t)w * h, 0);
    auto mark = [&](int x, int y) {
        if (x >= 0 && y >= 0 && x < w && y < h) m[(size_t)y * w + x] = 1;
    };
    for (auto& t : tris) {
        double X[3], Y[3];
        for (int i = 0; i < 3; i++) X[i] = std::fmod(std::fmod(t[i][0], 1.0) + 1.0, 1.0) * w, Y[i] = std::fmod(std::fmod(t[i][1], 1.0) + 1.0, 1.0) * h;
        // (as Pillow's polygon: wrapped corners, filled and outlined)
        const int x0 = std::max(0, (int)std::floor(std::min({X[0], X[1], X[2]}))), x1 = std::min(w - 1, (int)std::ceil(std::max({X[0], X[1], X[2]})));
        const int y0 = std::max(0, (int)std::floor(std::min({Y[0], Y[1], Y[2]}))), y1 = std::min(h - 1, (int)std::ceil(std::max({Y[0], Y[1], Y[2]})));
        const double d = (Y[1] - Y[2]) * (X[0] - X[2]) + (X[2] - X[1]) * (Y[0] - Y[2]);
        if (std::fabs(d) > 1e-12)
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    const double px = x + 0.5, py = y + 0.5;
                    const double a = ((Y[1] - Y[2]) * (px - X[2]) + (X[2] - X[1]) * (py - Y[2])) / d;
                    const double b = ((Y[2] - Y[0]) * (px - X[2]) + (X[0] - X[2]) * (py - Y[2])) / d;
                    if (a >= 0 && b >= 0 && a + b <= 1) mark(x, y);
                }
        for (int e = 0; e < 3; e++) {  // the outline
            const double ax = X[e], ay = Y[e], bx = X[(e + 1) % 3], by = Y[(e + 1) % 3];
            const int steps = (int)std::ceil(std::max(std::fabs(bx - ax), std::fabs(by - ay))) + 1;
            for (int s = 0; s <= steps; s++) mark((int)(ax + (bx - ax) * s / steps), (int)(ay + (by - ay) * s / steps));
        }
    }
    return m;
}

Image bleed(const Image& src, const std::vector<std::vector<uint8_t>>& regions, const Image* under) {
    const int w = src.w, h = src.h;
    const size_t n = (size_t)w * h;
    std::vector<double> rgb(n * 3);
    std::vector<uint8_t> known(n);
    for (size_t i = 0; i < n; i++) {
        for (int c = 0; c < 3; c++) rgb[i * 3 + c] = src.px[i * 4 + c];
        known[i] = src.px[i * 4 + 3] >= 128;
    }
    if (under && under->w == w && under->h == h)
        for (size_t i = 0; i < n; i++)
            if (!known[i] && under->px[i * 4 + 3] >= 128) {
                for (int c = 0; c < 3; c++) rgb[i * 3 + c] = under->px[i * 4 + c];
                known[i] = 1;
            }
    bool any = false;
    for (uint8_t k : known) any |= k != 0;
    if (!any) return src;
    // grows the known texels into `allowed` (neighbours wrap, as numpy's roll)
    auto fill = [&](std::vector<double>& col, std::vector<uint8_t>& kn, const std::vector<uint8_t>* allowed) {
        for (int it = 0; it < std::max(w, h); it++) {
            std::vector<size_t> grow;
            std::vector<double> val;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    const size_t i = (size_t)y * w + x;
                    if (kn[i] || (allowed && !(*allowed)[i])) continue;
                    double acc[3] = {};
                    int cnt = 0;
                    const size_t nb[4] = {(size_t)((y + h - 1) % h) * w + x, (size_t)((y + 1) % h) * w + x, (size_t)y * w + (x + w - 1) % w,
                                          (size_t)y * w + (x + 1) % w};
                    for (size_t j : nb)
                        if (kn[j] && (!allowed || (*allowed)[j])) {
                            for (int c = 0; c < 3; c++) acc[c] += col[j * 3 + c];
                            cnt++;
                        }
                    if (cnt) {
                        grow.push_back(i);
                        for (int c = 0; c < 3; c++) val.push_back(acc[c] / cnt);
                    }
                }
            if (grow.empty()) break;
            for (size_t g = 0; g < grow.size(); g++) {
                for (int c = 0; c < 3; c++) col[grow[g] * 3 + c] = val[g * 3 + c];
                kn[grow[g]] = 1;
            }
        }
    };
    std::vector<uint8_t> done = known;
    std::vector<double> outRgb = rgb;
    for (auto& r : regions) {
        bool has = false;
        for (size_t i = 0; i < n; i++) has |= known[i] && r[i];
        if (!has) continue;
        std::vector<double> col = rgb;
        std::vector<uint8_t> kn = known;
        fill(col, kn, &r);
        for (size_t i = 0; i < n; i++)
            if (kn[i] && !done[i] && r[i]) {
                for (int c = 0; c < 3; c++) outRgb[i * 3 + c] = col[i * 3 + c];
                done[i] = 1;
            }
    }
    std::vector<double> col = outRgb;
    std::vector<uint8_t> kn = done;
    fill(col, kn, nullptr);
    Image out = src;
    for (size_t i = 0; i < n; i++) {
        const double* c = done[i] ? &outRgb[i * 3] : &col[i * 3];
        for (int k = 0; k < 3; k++) out.px[i * 4 + k] = (uint8_t)std::clamp(std::lround(c[k]), 0L, 255L);
        out.px[i * 4 + 3] = 255;
    }
    return out;
}

}  // namespace gcmodel
