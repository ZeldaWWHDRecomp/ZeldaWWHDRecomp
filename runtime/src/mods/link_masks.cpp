// SPDX-License-Identifier: MPL-2.0
// Link's colour masks, the pure parts (link_masks.h): classification, endpoint labels, tint, reading
// Link.bfres (Wii U BFRES version 3, big-endian, story_resident_walk_clip.h's reader), the cache format.
#include "link_masks.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <unordered_map>

#include "item_icons.h"
#include "lyt_build.h"
#include "story_resident_walk_clip.h"

namespace link_masks {
namespace wc = story_resident_walk_clip;

const char* part_name(Part p) {
    static constexpr const char* kNames[] = {"none", "hair", "skin", "tunic", "undershirt", "pants", "boots", "emblem", "soles", "buckle", "eyes"};
    return unsigned(p) < unsigned(kParts) ? kNames[unsigned(p)] : "?";
}

uint32_t Mask::count(Part p) const {
    return uint32_t(std::count(texels.begin(), texels.end(), uint8_t(p)));
}

uint64_t hash_bytes(const uint8_t* a, size_t na, const uint8_t* b, size_t nb) {
    uint64_t h = 0xcbf29ce484222325ull;  // FNV-1a, 8 bytes at a time folded (fast enough for 200 KB: ~0.1 ms)
    auto run = [&](const uint8_t* p, size_t n) {
        size_t i = 0;
        for (; i + 8 <= n; i += 8) {
            uint64_t v;
            memcpy(&v, p + i, 8);
            h = (h ^ v) * 0x100000001b3ull;
            h ^= h >> 29;
        }
        for (; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    };
    if (a) run(a, na);
    if (b) run(b, nb);
    return h;
}

namespace {

// ---- colour ----
struct Lab { float l, a, b; };
float srgb_to_linear(int c) {
    static const std::array<float, 256> lut = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; i++) {
            const double v = i / 255.0;
            t[size_t(i)] = float(v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4));
        }
        return t;
    }();
    return lut[size_t(c & 255)];
}
Lab lab(int r8, int g8, int b8) {
    const float r = srgb_to_linear(r8), g = srgb_to_linear(g8), b = srgb_to_linear(b8);
    const float x = (0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.9505f, y = 0.2126f * r + 0.7152f * g + 0.0722f * b,
                z = (0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.089f;
    auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };
    const float fx = f(x), fy = f(y), fz = f(z);
    return {116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)};
}
Lab lab(uint32_t rgb) { return lab(int(rgb >> 16 & 255), int(rgb >> 8 & 255), int(rgb & 255)); }
float de2(const Lab& p, const Lab& q, float lw = 1) {
    const float dl = (p.l - q.l) * lw, da = p.a - q.a, db = p.b - q.b;
    return dl * dl + da * da + db * db;
}
struct Hsv { double h, s, v; };
Hsv hsv(double r, double g, double b) {  // 0..1 each
    const double mx = std::max({r, g, b}), d = mx - std::min({r, g, b});
    Hsv o{0, mx > 0 ? d / mx : 0, mx};
    if (d < 1e-9) return o;
    o.h = 60 * (r >= mx ? (g - b) / d : g >= mx ? 2 + (b - r) / d : 4 + (r - g) / d);
    if (o.h < 0) o.h += 360;
    return o;
}
void rgb_of(const Hsv& c, double& r, double& g, double& b) {
    r = g = b = c.v;
    if (c.s <= 0) return;
    double hh = std::fmod(c.h, 360.0);
    if (hh < 0) hh += 360;
    hh /= 60;
    const int i = int(hh);
    const double f = hh - i, p = c.v * (1 - c.s), q = c.v * (1 - c.s * f), t = c.v * (1 - c.s * (1 - f));
    switch (i) {
    case 0: r = c.v, g = t, b = p; break;
    case 1: r = q, g = c.v, b = p; break;
    case 2: r = p, g = c.v, b = t; break;
    case 3: r = p, g = q, b = c.v; break;
    case 4: r = t, g = p, b = c.v; break;
    default: r = c.v, g = p, b = q; break;
    }
}
double hue_gap(double a, double b) {
    const double d = std::fabs(a - b);
    return std::min(d, 360 - d);
}
void rgb565_to8(uint16_t c, int& r, int& g, int& b) {
    r = (c >> 11) * 255 / 31, g = (c >> 5 & 63) * 255 / 63, b = (c & 31) * 255 / 31;
}

// ---- union-find over texels ----
struct Sets {
    std::vector<uint32_t> up;
    explicit Sets(size_t n) : up(n) {
        for (size_t i = 0; i < n; i++) up[i] = uint32_t(i);
    }
    uint32_t root(uint32_t x) {
        while (up[x] != x) x = up[x] = up[up[x]];
        return x;
    }
};

// BC colour block (8 bytes) to 4 RGBA colours; four: the 4-colour mode
void palette4(const uint8_t* b, bool force_four, uint8_t col[4][4]) {
    const uint16_t c0 = uint16_t(b[0] | b[1] << 8), c1 = uint16_t(b[2] | b[3] << 8);
    int r0, g0, b0, r1, g1, b1;
    rgb565_to8(c0, r0, g0, b0), rgb565_to8(c1, r1, g1, b1);
    auto set = [&](int i, int r, int g, int bl, int a) { col[i][0] = uint8_t(r), col[i][1] = uint8_t(g), col[i][2] = uint8_t(bl), col[i][3] = uint8_t(a); };
    set(0, r0, g0, b0, 255), set(1, r1, g1, b1, 255);
    if (force_four || c0 > c1) {
        set(2, (2 * r0 + r1) / 3, (2 * g0 + g1) / 3, (2 * b0 + b1) / 3, 255);
        set(3, (r0 + 2 * r1) / 3, (g0 + 2 * g1) / 3, (b0 + 2 * b1) / 3, 255);
    } else {
        set(2, (r0 + r1) / 2, (g0 + g1) / 2, (b0 + b1) / 2, 255);
        set(3, 0, 0, 0, 0);
    }
}
// how much texel index i draws on endpoint 0 and 1
void index_weights(unsigned i, bool four, float& w0, float& w1) {
    static constexpr float k4[4][2] = {{1, 0}, {0, 1}, {2.f / 3, 1.f / 3}, {1.f / 3, 2.f / 3}};
    static constexpr float k3[4][2] = {{1, 0}, {0, 1}, {0.5f, 0.5f}, {0, 0}};
    w0 = four ? k4[i][0] : k3[i][0], w1 = four ? k4[i][1] : k3[i][1];
}

}  // namespace

Bytes decode(int kind, const uint8_t* blocks, uint32_t w, uint32_t h) {
    const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4, size = kind == 1 ? 8 : 16;
    Bytes out(size_t(w) * h * 4);
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            const uint8_t* b = blocks + (size_t(by) * bw + bx) * size + (size - 8);
            uint8_t col[4][4];
            palette4(b, kind != 1, col);
            const uint32_t idx = uint32_t(b[4] | b[5] << 8 | b[6] << 16) | uint32_t(b[7]) << 24;
            for (uint32_t t = 0; t < 16; t++) {
                const uint32_t x = bx * 4 + t % 4, y = by * 4 + t / 4;
                if (x < w && y < h) memcpy(&out[(size_t(y) * w + x) * 4], col[idx >> (2 * t) & 3], 4);
            }
        }
    return out;
}

std::vector<uint32_t> block_order(uint32_t bw, uint32_t bh, uint32_t bpp, int tile_mode, uint32_t swizzle, uint32_t raw_blocks) {
    const size_t n = size_t(bw) * bh, es = bpp / 8, raw = std::max<size_t>(raw_blocks, n);
    Bytes surface(raw * es);
    for (size_t i = 0; i < raw; i++) memcpy(&surface[i * es], &i, 4);  // raw element i holds i (little-endian)
    const Bytes lin = lyt_build::untile(surface, bw, bh, bpp, tile_mode, swizzle);
    std::vector<uint32_t> order(raw, ~0u);
    if (lin.size() < n * es) {  // an unsupported mode: taken as linear
        for (size_t i = 0; i < n; i++) order[i] = uint32_t(i);
        return order;
    }
    for (size_t p = 0; p < n; p++) {
        uint32_t i;
        memcpy(&i, &lin[p * es], 4);
        if (i < raw) order[i] = uint32_t(p);
    }
    return order;
}

std::vector<uint8_t> classify(const uint8_t* rgba, uint32_t w, uint32_t h, const std::vector<PaletteEntry>& palette,
                              const Params& prm) {
    const size_t n = size_t(w) * h;
    std::vector<uint8_t> out(n, uint8_t(Part::kNone));
    if (!n || palette.empty()) return out;
    std::vector<Lab> L(n);
    for (size_t i = 0; i < n; i++) L[i] = lab(rgba[4 * i], rgba[4 * i + 1], rgba[4 * i + 2]);
    // 2. regions of near colours
    Sets s(n);
    const float join2 = prm.join * prm.join;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            const uint32_t i = y * w + x;
            if (x + 1 < w && de2(L[i], L[i + 1]) < join2) s.up[s.root(i)] = s.root(i + 1);
            if (y + 1 < h && de2(L[i], L[i + w]) < join2) s.up[s.root(i)] = s.root(i + w);
        }
    // region sums (kept at each root)
    std::vector<double> sl(n), sa(n), sb(n), sr(n), sg(n), sbl(n);
    std::vector<uint32_t> size(n, 0);
    auto add_stats = [&](uint32_t to, uint32_t from) {
        sl[to] += sl[from], sa[to] += sa[from], sb[to] += sb[from], sr[to] += sr[from], sg[to] += sg[from], sbl[to] += sbl[from];
        size[to] += size[from];
    };
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t r = s.root(i);
        sl[r] += L[i].l, sa[r] += L[i].a, sb[r] += L[i].b, sr[r] += rgba[4 * i], sg[r] += rgba[4 * i + 1], sbl[r] += rgba[4 * i + 2];
        size[r]++;
    }
    auto mean = [&](uint32_t r) { return Lab{float(sl[r] / size[r]), float(sa[r] / size[r]), float(sb[r] / size[r])}; };
    // each 4-neighbour edge between two regions, visited by fn(a, b) (roots)
    auto edges = [&](auto&& fn) {
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) {
                const uint32_t i = y * w + x;
                if (x + 1 < w) {
                    const uint32_t a = s.root(i), b = s.root(i + 1);
                    if (a != b) fn(a, b);
                }
                if (y + 1 < h) {
                    const uint32_t a = s.root(i), b = s.root(i + w);
                    if (a != b) fn(a, b);
                }
            }
    };
    // 3. small regions into their most similar neighbour that is no smaller
    for (int round = 0; round < 20; round++) {
        std::unordered_map<uint32_t, std::pair<float, uint32_t>> best;
        auto offer = [&](uint32_t a, uint32_t b) {
            if (size[a] >= prm.small || size[b] < size[a]) return;
            const float d = de2(mean(a), mean(b));
            auto it = best.find(a);
            if (it == best.end() || d < it->second.first || (d == it->second.first && b < it->second.second)) best[a] = {d, b};
        };
        edges([&](uint32_t a, uint32_t b) { offer(a, b), offer(b, a); });
        if (best.empty()) break;
        std::vector<std::pair<uint32_t, uint32_t>> merges;
        for (const auto& [a, v] : best) merges.push_back({a, v.second});
        std::sort(merges.begin(), merges.end());
        for (const auto& [a, b] : merges) {
            const uint32_t ra = s.root(a), rb = s.root(b);
            if (ra == rb) continue;
            s.up[ra] = rb;
            add_stats(rb, ra);
        }
    }
    // 4. name each region by the nearest palette colour
    std::vector<Lab> pal;
    for (const PaletteEntry& e : palette) pal.push_back(lab(e.rgb));
    std::vector<uint32_t> roots;
    for (uint32_t i = 0; i < n; i++)
        if (s.root(i) == i) roots.push_back(i);
    std::unordered_map<uint32_t, uint8_t> part;
    std::unordered_map<uint32_t, double> hue;
    for (uint32_t r : roots) {
        const Lab m = mean(r);
        size_t bi = 0;
        float bd = 1e30f;
        for (size_t k = 0; k < pal.size(); k++) {
            const float d = de2(m, pal[k], prm.lightness);
            if (d < bd) bd = d, bi = k;
        }
        part[r] = uint8_t(palette[bi].part);
        hue[r] = hsv(sr[r] / size[r] / 255, sg[r] / size[r] / 255, sbl[r] / size[r] / 255).h;
    }
    // 5. enclosed regions of a near hue take the encloser's part
    std::unordered_map<uint32_t, std::map<uint32_t, uint32_t>> border;  // region -> neighbour region -> edges (~0u: the image edge)
    edges([&](uint32_t a, uint32_t b) { border[a][b]++, border[b][a]++; });
    for (uint32_t x = 0; x < w; x++) border[s.root(x)][~0u]++, border[s.root((h - 1) * w + x)][~0u]++;
    for (uint32_t y = 0; y < h; y++) border[s.root(y * w)][~0u]++, border[s.root(y * w + w - 1)][~0u]++;
    for (int round = 0; round < 4; round++) {
        bool changed = false;
        for (uint32_t r : roots) {
            const auto it = border.find(r);
            if (it == border.end()) continue;
            uint32_t total = 0;
            std::map<int, uint32_t> by_part;
            std::map<int, std::pair<uint32_t, uint32_t>> largest;  // part -> (size, region)
            for (const auto& [nbr, edges_n] : it->second) {
                const int p = nbr == ~0u ? -1 : part[nbr];
                if (p == part[r]) continue;  // (a neighbour of its own part: the same mark, split in two)
                total += edges_n;
                by_part[p] += edges_n;
                if (nbr != ~0u && size[nbr] > largest[p].first) largest[p] = {size[nbr], nbr};
            }
            int bp = -1;
            uint32_t bc = 0;
            for (const auto& [p, c] : by_part)
                if (c > bc) bc = c, bp = p;
            if (bp < 0 || bp == part[r] || bc < prm.enclosed * total || largest[bp].first < 4 * size[r]) continue;
            if (hue_gap(hue[r], hue[largest[bp].second]) < prm.hue) part[r] = uint8_t(bp), changed = true;
        }
        if (!changed) break;
    }
    for (uint32_t i = 0; i < n; i++) out[i] = part[s.root(i)];
    return out;
}

std::vector<uint8_t> label_endpoints(int kind, const uint8_t* image, uint32_t image_size, const uint8_t* mips, uint32_t mip_size,
                                     const std::vector<uint32_t>& order, uint32_t w, uint32_t h, const std::vector<uint8_t>& level0,
                                     const Params& prm) {
    const uint32_t bs = kind == 1 ? 8 : 16, bw = (w + 3) / 4;
    const uint32_t n0 = image_size / bs, nm = mip_size / bs;
    std::vector<uint8_t> out(2 * size_t(n0 + nm), uint8_t(Part::kNone));
    // level 0: the parts of the texels drawing on each endpoint
    std::unordered_map<uint16_t, std::array<float, kParts>> votes;  // endpoint colour -> part weights (for the mips)
    for (uint32_t i = 0; i < n0 && i < order.size(); i++) {  // (order: raw block -> row-major block, ~0u padding)
        const uint8_t* b = image + size_t(i) * bs + (bs - 8);
        const uint16_t c[2] = {uint16_t(b[0] | b[1] << 8), uint16_t(b[2] | b[3] << 8)};
        const bool four = kind != 1 || c[0] > c[1];
        const uint32_t idx = uint32_t(b[4] | b[5] << 8 | b[6] << 16) | uint32_t(b[7]) << 24;
        if (order[i] == ~0u) continue;  // the surface's padding
        const uint32_t bx = order[i] % bw, by = order[i] / bw;
        std::array<float, kParts> wsum[2] = {};
        for (uint32_t t = 0; t < 16; t++) {
            const uint32_t x = bx * 4 + t % 4, y = by * 4 + t / 4;
            if (x >= w || y >= h) continue;
            float w0, w1;
            index_weights(idx >> (2 * t) & 3, four, w0, w1);
            const uint8_t p = level0[size_t(y) * w + x];
            wsum[0][p] += w0, wsum[1][p] += w1;
        }
        for (int e = 0; e < 2; e++) {
            int bp = 0;
            float bv = 0;
            for (int p = 0; p < kParts; p++)
                if (wsum[e][p] > bv) bv = wsum[e][p], bp = p;
            if (bv <= 0) {  // an endpoint no texel uses: the other's part (an empty block colour) or none
                const std::array<float, kParts>& o = wsum[1 - e];
                for (int p = 0; p < kParts; p++)
                    if (o[p] > bv) bv = o[p], bp = p;
            }
            out[2 * size_t(i) + e] = uint8_t(bp);
            if (bp) {
                auto& v = votes[c[e]];
                for (int p = 0; p < kParts; p++) v[size_t(p)] += wsum[e][p];
            }
        }
    }
    // mips: the part of the nearest level-0 endpoint colour
    std::vector<std::pair<Lab, uint8_t>> known;
    for (const auto& [c, v] : votes) {
        int r, g, b;
        rgb565_to8(c, r, g, b);
        const int bp = int(std::max_element(v.begin() + 1, v.end()) - v.begin());
        known.push_back({lab(r, g, b), uint8_t(bp)});
    }
    std::unordered_map<uint16_t, uint8_t> memo;
    auto nearest = [&](uint16_t c) {
        const auto it = memo.find(c);
        if (it != memo.end()) return it->second;
        int r, g, b;
        rgb565_to8(c, r, g, b);
        const Lab q = lab(r, g, b);
        float bd = 1e30f;
        uint8_t bp = 0;
        for (const auto& [kl, kp] : known) {
            const float d = de2(q, kl, prm.lightness);
            if (d < bd) bd = d, bp = kp;
        }
        // a colour far from every level-0 endpoint is no part (the mip surfaces' padding, zero blocks)
        return memo[c] = bd <= prm.mip_reach * prm.mip_reach ? bp : uint8_t(Part::kNone);
    };
    static constexpr uint8_t kZero[8] = {};
    for (uint32_t i = 0; i < nm; i++) {
        const uint8_t* b = mips + size_t(i) * bs + (bs - 8);
        if (!memcmp(b, kZero, 8)) continue;  // padding
        out[2 * size_t(n0 + i)] = nearest(uint16_t(b[0] | b[1] << 8));
        out[2 * size_t(n0 + i) + 1] = nearest(uint16_t(b[2] | b[3] << 8));
    }
    return out;
}

bool iris_colour(int r, int g, int b) {
    const Hsv c = hsv(r / 255.0, g / 255.0, b / 255.0);
    return c.s >= 0.25 && c.v >= 0.05 && c.v <= 0.85;
}

std::vector<uint8_t> label_endpoints_by_colour(int kind, const uint8_t* image, uint32_t image_size, const uint8_t* mips, uint32_t mip_size,
                                               Part part) {
    const uint32_t bs = kind == 1 ? 8 : 16, n0 = image_size / bs, n = n0 + (mips ? mip_size / bs : 0);
    std::vector<uint8_t> out(2 * size_t(n), uint8_t(Part::kNone));
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* b = (i < n0 ? image + size_t(i) * bs : mips + size_t(i - n0) * bs) + (bs - 8);
        for (int e = 0; e < 2; e++) {
            int r, g, bb;
            rgb565_to8(uint16_t(b[2 * e] | b[2 * e + 1] << 8), r, g, bb);
            if (iris_colour(r, g, bb)) out[2 * size_t(i) + size_t(e)] = uint8_t(part);
        }
    }
    return out;
}

uint16_t exchange(uint16_t cur, uint32_t base_rgb, uint32_t want_rgb) {
    int r, g, b;
    rgb565_to8(cur, r, g, b);
    const Hsv c = hsv(r / 255.0, g / 255.0, b / 255.0);
    const Hsv base = hsv((base_rgb >> 16 & 255) / 255.0, (base_rgb >> 8 & 255) / 255.0, (base_rgb & 255) / 255.0);
    const Hsv want = hsv((want_rgb >> 16 & 255) / 255.0, (want_rgb >> 8 & 255) / 255.0, (want_rgb & 255) / 255.0);
    // a value (saturation or brightness) moved as the base moves to the wanted one: scaled down towards 0, or
    // up towards 1, so the shading's ratios stay (black hair keeps its highlights, white keeps its shadows)
    auto move = [](double v, double from, double to) {
        if (to <= from) return from > 1e-6 ? v * to / from : to;
        return from < 1 - 1e-6 ? 1 - (1 - v) * (1 - to) / (1 - from) : to;
    };
    Hsv o;
    const bool grey_base = base.s < 0.05;
    o.h = grey_base || c.s < 0.02 ? want.h : c.h + (want.h - base.h);
    o.s = std::clamp(move(c.s < 0.02 && !grey_base ? base.s : c.s, base.s, want.s), 0.0, 1.0);
    o.v = std::clamp(move(c.v, base.v, want.v), 0.0, 1.0);
    double rr, gg, bb;
    rgb_of(o, rr, gg, bb);
    return uint16_t(std::lround(rr * 31) << 11 | std::lround(gg * 63) << 5 | std::lround(bb * 31));
}

void tint(const Mask& m, Part part, uint32_t want_rgb, uint8_t* image, uint8_t* mips) {
    const uint32_t base = m.base_rgb[size_t(part)];
    if (!base || part == Part::kNone) return;
    const bool bc1 = (m.format & 0xFF) == 0x31;
    const uint32_t bs = bc1 ? 8 : 16, n0 = m.image_size / bs, n = n0 + m.mip_size / bs;
    std::unordered_map<uint16_t, uint16_t> memo;
    for (uint32_t i = 0; i < n && 2 * size_t(i) + 1 < m.endpoints.size(); i++) {
        const bool e0 = m.endpoints[2 * size_t(i)] == uint8_t(part), e1 = m.endpoints[2 * size_t(i) + 1] == uint8_t(part);
        if (!e0 && !e1) continue;
        if (!(i < n0 ? image : mips)) continue;
        uint8_t* b = (i < n0 ? image + size_t(i) * bs : mips + size_t(i - n0) * bs) + (bs - 8);
        uint16_t c[2] = {uint16_t(b[0] | b[1] << 8), uint16_t(b[2] | b[3] << 8)};
        const bool four = c[0] > c[1];
        for (int e = 0; e < 2; e++) {
            if (!(e ? e1 : e0)) continue;
            auto it = memo.find(c[e]);
            c[e] = it != memo.end() ? it->second : (memo[c[e]] = exchange(c[e], base, want_rgb));
        }
        uint32_t idx = uint32_t(b[4] | b[5] << 8 | b[6] << 16) | uint32_t(b[7]) << 24;
        if (bc1) {
            // keep the block's colour mode: 4 colours needs c0 > c1, 3 colours + transparent c0 <= c1
            if (four && c[0] < c[1]) {
                std::swap(c[0], c[1]);
                idx ^= 0x55555555u;  // 0<->1, 2<->3
            } else if (four && c[0] == c[1]) {
                for (int t = 0; t < 32; t += 2)  // one colour: index 3 would turn transparent
                    if ((idx >> t & 3) == 3) idx &= ~(3u << t);
            } else if (!four && c[0] > c[1]) {
                std::swap(c[0], c[1]);
                for (int t = 0; t < 32; t += 2) {  // 0<->1; 2 (the middle) and 3 (transparent) stay
                    const uint32_t v = idx >> t & 3;
                    if (v < 2) idx = (idx & ~(3u << t)) | ((v ^ 1u) << t);
                }
            }
        }  // (BC3's colour half always has 4 colours: nothing to keep)
        b[0] = uint8_t(c[0]), b[1] = uint8_t(c[0] >> 8), b[2] = uint8_t(c[1]), b[3] = uint8_t(c[1] >> 8);
        b[4] = uint8_t(idx), b[5] = uint8_t(idx >> 8), b[6] = uint8_t(idx >> 16), b[7] = uint8_t(idx >> 24);
    }
}

// ---- Link.bfres ----
namespace {

struct Known {
    const char* texture;
    std::vector<PaletteEntry> palette;
    // the hero's belt buckle: the same yellow as his hair, told apart by the shape drawing it (model "cl",
    // the shape of bone cl_back); the hair's own texels (another shape's) stay hair
    const char* buckle_shape = nullptr;
    bool by_colour = false;  // parts by iris_colour() alone (the irises), not by regions
};
// The parts' base colours (the default colours of Link's outfits; used only to name regions found in the
// texture, the regions' own colours are measured).
const std::vector<Known>& known() {
    static const std::vector<Known> k = {
        {"linktexS3TC",
         {{Part::kHair, 0xFFEF10}, {Part::kSkin, 0xF7DB9C}, {Part::kTunic, 0x5AB24A}, {Part::kUndershirt, 0xADE342},
          {Part::kPants, 0xFFFFFF}, {Part::kBoots, 0x944908}, {Part::kBoots, 0xD6B639}},
         "cl_back__"},
        {"linktexbci4",
         {{Part::kHair, 0xFFEF10}, {Part::kSkin, 0xF7DB9C}, {Part::kTunic, 0x4A75AD}, {Part::kEmblem, 0xFFFFD6},
          {Part::kBoots, 0x63696B}, {Part::kPants, 0xFFA608}, {Part::kSoles, 0xD6BA39}}},
        {"katsuraS3TC", {{Part::kHair, 0xFFEF10}}},
        {"hitomi", {{Part::kEyes, 0x1A5A4A}}, nullptr, true},
    };
    return k;
}

float f16(uint16_t v) {
    const uint32_t s = (v >> 15) & 1, e = (v >> 10) & 31, m = v & 1023;
    const float f = e == 0 ? std::ldexp(float(m), -24) : std::ldexp(float(m | 1024), int(e) - 25);
    return s ? -f : f;
}

// the texture a material samples first (its texture references: a count at +0x10, the names' table at +0x28)
std::string first_texture(wc::Reader& r, uint64_t mat) {
    const uint64_t refs = r.rel(mat + 0x28);
    return r.u8(mat + 0x10) && refs ? r.str(r.rel(refs)) : std::string();
}

// The texels (w x h, row-major) under the UVs of model `model`'s shapes that sample `texture` and whose name
// starts with `prefix` (others: the other shapes sampling it), their triangles' edges included, grown by
// `grow` texels (what the sampler's filter reaches).
std::vector<uint8_t> uv_cover(wc::Reader& r, const char* model, const char* texture, const char* prefix, bool others, uint32_t w,
                              uint32_t h, int grow) {
    std::vector<uint8_t> cover(size_t(w) * h, 0);
    const uint64_t fmdl = wc::find(r.group(0x20), model);
    if (!fmdl) return cover;
    const uint64_t vertex_array = r.rel(fmdl + 0x10);
    const auto mats = r.group(fmdl + 0x18);
    for (const auto& [sname, at] : r.group(fmdl + 0x14)) {
        if (!r.ok) break;
        if ((sname.rfind(prefix, 0) == 0) == others) continue;
        const unsigned mi = r.u16(at + 0xE);
        if (mi >= mats.size() || first_texture(r, mats[mi].second) != texture) continue;
        const uint64_t vtx = vertex_array + 0x20 * uint64_t(r.u16(at + 0x12));
        const unsigned nattr = r.u8(vtx + 4), nbuf = r.u8(vtx + 5);
        const uint32_t count = r.u32(vtx + 8);
        const uint64_t attrs = r.rel(vtx + 0x10), bufs = r.rel(vtx + 0x18);
        uint64_t data = 0;
        uint32_t stride = 0, offset = 0, fmt = 0;
        for (unsigned i = 0; i < nattr && r.ok; i++) {
            const uint64_t a = attrs + 12 * uint64_t(i);
            if (r.str(r.rel(a)) != "_u0" || r.u8(a + 4) >= nbuf) continue;
            const uint64_t buf = bufs + 0x18 * uint64_t(r.u8(a + 4));
            stride = r.u16(buf + 0xC), data = r.rel(buf + 0x14), offset = r.u16(a + 6), fmt = r.u32(a + 8);
        }
        if (!data || !stride || !count || count > 200000 || !r.in(data, uint64_t(stride) * count)) continue;
        auto uv = [&](uint32_t k, float& u, float& v) {
            const uint64_t p = data + uint64_t(stride) * k + offset;
            switch (fmt) {
            case 0x80D: u = float(r.f32(p)), v = float(r.f32(p + 4)); return true;
            case 0x808: u = f16(r.u16(p)), v = f16(r.u16(p + 2)); return true;
            case 0x007: u = r.u16(p) / 65535.0f, v = r.u16(p + 2) / 65535.0f; return true;
            case 0x207: u = int16_t(r.u16(p)) / 32767.0f, v = int16_t(r.u16(p + 2)) / 32767.0f; return true;
            default: return false;
            }
        };
        const uint64_t mesh = r.rel(at + 0x24);
        const uint32_t prim = r.u32(mesh), ifmt = r.u32(mesh + 4), ni = r.u32(mesh + 8), first = r.u32(mesh + 0x18);
        const uint64_t ib = r.rel(r.rel(mesh + 0x14) + 0x14);
        if (prim != 4 || (ifmt != 4 && ifmt != 9) || !ib || ni > 600000 || !r.in(ib, uint64_t(ni) * (ifmt == 9 ? 4 : 2))) continue;
        for (uint32_t i = 0; i + 2 < ni; i += 3) {
            float px[3], py[3];
            bool ok = true;
            for (int j = 0; j < 3 && ok; j++) {
                const uint32_t t = (ifmt == 9 ? r.u32(ib + 4 * uint64_t(i + j)) : r.u16(ib + 2 * uint64_t(i + j))) + first;
                float u = 0, v = 0;
                ok = t < count && uv(t, u, v);
                px[j] = u * float(w), py[j] = v * float(h);
            }
            if (!ok) continue;
            for (int j = 0; j < 3; j++) {  // the edges (a shape drawn from one point or a sliver has no texel centre inside)
                const float ax = px[j], ay = py[j], bx = px[(j + 1) % 3], by = py[(j + 1) % 3];
                const int steps = int(std::min(4096.0f, 2 * std::max(std::fabs(bx - ax), std::fabs(by - ay)))) + 1;
                for (int t = 0; t <= steps; t++) {
                    const float x = ax + (bx - ax) * float(t) / float(steps), y = ay + (by - ay) * float(t) / float(steps);
                    if (x >= 0 && y >= 0 && x < float(w) && y < float(h)) cover[size_t(y) * w + size_t(x)] = 1;
                }
            }
            const float area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
            if (std::fabs(area) < 1e-6f) continue;
            const int x0 = std::max(0, int(std::floor(std::min({px[0], px[1], px[2]})))), x1 = std::min(int(w) - 1, int(std::ceil(std::max({px[0], px[1], px[2]}))));
            const int y0 = std::max(0, int(std::floor(std::min({py[0], py[1], py[2]})))), y1 = std::min(int(h) - 1, int(std::ceil(std::max({py[0], py[1], py[2]}))));
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    const float cx = float(x) + 0.5f, cy = float(y) + 0.5f;
                    const float a = ((px[1] - cx) * (py[2] - cy) - (px[2] - cx) * (py[1] - cy)) / area;
                    const float b = ((px[2] - cx) * (py[0] - cy) - (px[0] - cx) * (py[2] - cy)) / area;
                    if (a >= -1e-4f && b >= -1e-4f && a + b <= 1 + 1e-4f) cover[size_t(y) * w + size_t(x)] = 1;
                }
        }
    }
    for (int pass = 0; pass < grow; pass++) {
        std::vector<uint8_t> grown = cover;
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++)
                if (cover[size_t(y) * w + x]) {
                    if (x > 0) grown[size_t(y) * w + x - 1] = 1;
                    if (x + 1 < w) grown[size_t(y) * w + x + 1] = 1;
                    if (y > 0) grown[size_t(y - 1) * w + x] = 1;
                    if (y + 1 < h) grown[size_t(y + 1) * w + x] = 1;
                }
        cover.swap(grown);
    }
    return cover;
}

}  // namespace

void reassign(std::vector<uint8_t>& texels, uint32_t w, uint32_t h, Part from, Part to, const std::vector<uint8_t>& under,
              const std::vector<uint8_t>& keep) {
    const size_t n = size_t(w) * h;
    if (texels.size() < n || under.size() < n || keep.size() < n) return;
    auto free_texel = [&](size_t i) { return texels[i] == uint8_t(from) && !keep[i]; };
    std::vector<uint8_t> seen(n, 0);
    std::vector<size_t> stack, area;
    for (size_t start = 0; start < n; start++) {
        if (seen[start] || !free_texel(start)) continue;
        // one 4-connected area of `from` outside `keep`; it goes to `to` if `under` reaches it anywhere
        bool reached = false;
        area.clear(), stack.assign(1, start), seen[start] = 1;
        while (!stack.empty()) {
            const size_t i = stack.back();
            stack.pop_back();
            area.push_back(i);
            reached = reached || under[i];
            const size_t x = i % w, y = i / w;
            const size_t next[4] = {x > 0 ? i - 1 : n, x + 1 < w ? i + 1 : n, y > 0 ? i - w : n, y + 1 < h ? i + w : n};
            for (size_t j : next)
                if (j < n && !seen[j] && free_texel(j)) seen[j] = 1, stack.push_back(j);
        }
        if (reached)
            for (size_t i : area) texels[i] = uint8_t(to);
    }
}

std::vector<Mask> compute(const Bytes& bfres) {
    wc::Reader r{bfres};
    if (!wc::fres(r)) throw std::runtime_error("Link.bfres is not a BFRES file");
    std::vector<Mask> out;
    const auto files = r.group(0x24);
    for (const Known& k : known()) {
        const uint64_t ftex = wc::find(files, k.texture);
        if (!ftex || !r.in(ftex, 0xC0) || memcmp(&bfres[ftex], "FTEX", 4)) continue;
        const uint64_t s = ftex + 4;
        Mask m;
        m.texture = k.texture;
        m.width = r.u32(s + 4), m.height = r.u32(s + 8), m.format = r.u32(s + 0x14);
        m.image_size = r.u32(s + 0x20), m.mip_size = r.u32(s + 0x28);
        const uint32_t mode = r.u32(s + 0x30), swizzle = r.u32(s + 0x34);
        const int kind = (m.format & 0xFF) == 0x31 ? 1 : (m.format & 0xFF) == 0x33 ? 3 : 0;
        const uint64_t data = r.rel(ftex + 0xB0), mipdata = m.mip_size ? r.rel(ftex + 0xB4) : 0;
        if (!kind || !m.width || !m.height || m.width > 2048 || m.height > 2048 || !data || !r.in(data, m.image_size) ||
            (m.mip_size && (!mipdata || !r.in(mipdata, m.mip_size))))
            continue;
        const uint8_t* img = &bfres[data];
        const uint8_t* mip = m.mip_size ? &bfres[mipdata] : nullptr;
        m.hash = hash_bytes(img, m.image_size, mip, m.mip_size);
        const uint32_t bw = (m.width + 3) / 4, bh = (m.height + 3) / 4, bpp = kind == 1 ? 64 : 128;
        const Bytes lin = lyt_build::untile(Bytes(img, img + m.image_size), bw, bh, bpp, int(mode), (swizzle >> 8) & 7);
        if (lin.size() < size_t(bw) * bh * (bpp / 8)) continue;
        const Bytes rgba = decode(kind, lin.data(), m.width, m.height);
        m.ftex = uint32_t(ftex);
        memcpy(m.file_head, bfres.data(), std::min<size_t>(sizeof m.file_head, bfres.size()));
        if (k.by_colour) {  // the irises: by colour, texels and endpoints alike
            m.texels.assign(size_t(m.width) * m.height, uint8_t(Part::kNone));
            for (size_t i = 0; i < m.texels.size(); i++)
                if (iris_colour(rgba[4 * i], rgba[4 * i + 1], rgba[4 * i + 2])) m.texels[i] = uint8_t(k.palette[0].part);
            m.endpoints = label_endpoints_by_colour(kind, img, m.image_size, mip, m.mip_size, k.palette[0].part);
        } else
        m.texels = classify(rgba.data(), m.width, m.height, k.palette);
        if (k.buckle_shape) {
            // the buckle: the hair-coloured areas its shape draws, outside what the other shapes draw (with 2 texels
            // of filter reach, so the hair's flat patch inside the buckle's square stays hair)
            const auto buckle = uv_cover(r, "cl", k.texture, k.buckle_shape, false, m.width, m.height, 0);
            const auto rest = uv_cover(r, "cl", k.texture, k.buckle_shape, true, m.width, m.height, 2);
            reassign(m.texels, m.width, m.height, Part::kHair, Part::kBuckle, buckle, rest);
        }
        const std::vector<uint32_t> order = block_order(bw, bh, bpp, int(mode), (swizzle >> 8) & 7, m.image_size / (bpp / 8));
        if (!k.by_colour) m.endpoints = label_endpoints(kind, img, m.image_size, mip, m.mip_size, order, m.width, m.height, m.texels);
        // each part's own colour: its commonest texel colour
        std::vector<std::unordered_map<uint32_t, uint32_t>> counts(kParts);
        for (size_t i = 0; i < m.texels.size(); i++)
            counts[m.texels[i]][uint32_t(rgba[4 * i]) << 16 | uint32_t(rgba[4 * i + 1]) << 8 | rgba[4 * i + 2]]++;
        // each part's own colour: of its common texel colours (at least a quarter as many as the commonest),
        // the one nearest the part's palette colour (the hero's hair: the fringe's yellow, not the swirl's)
        for (int p = 1; p < kParts; p++) {
            uint32_t top = 0;
            for (const auto& [c, n] : counts[size_t(p)]) top = std::max(top, n);
            if (!top) continue;
            Lab want{};
            bool have = false;
            for (const PaletteEntry& e : k.palette)
                if (e.part == Part(p) && !have) want = lab(e.rgb), have = true;
            uint32_t best = 0;
            float bd = 1e30f;
            for (const auto& [c, n] : counts[size_t(p)]) {
                if (4 * n < top) continue;
                const float d = have ? de2(lab(c), want) : -float(n);
                if (d < bd || (d == bd && c < best)) bd = d, best = c;
            }
            m.base_rgb[p] = best ? best : 0x010101;
        }
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<Mask> compute_from_szs(const Bytes& link_szs) {
    auto sarc = item_icons::read_sarc(item_icons::yaz0_decode(link_szs));
    const auto f = sarc.find("Link.bfres");
    if (f == sarc.end()) throw std::runtime_error("Link.bfres not in Link.szs");
    return compute(f->second);
}

std::vector<Mask> compute_from_pack(const Bytes& pack3d) {
    const auto files = item_icons::read_sarc(pack3d);
    const auto it = files.find("Link.szs");
    if (it == files.end()) throw std::runtime_error("Link.szs not in the 3D pack");
    return compute_from_szs(it->second);
}

Bytes sarc_member(const std::string& path, const std::string& name) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error(path + " can't be read");
    auto be = [](const Bytes& d, size_t o, int n) {
        if (o + size_t(n) > d.size()) throw std::runtime_error("truncated SARC");
        uint32_t v = 0;
        for (int i = 0; i < n; i++) v = v << 8 | d[o + size_t(i)];
        return v;
    };
    Bytes head(0x20);
    if (!f.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size())) || memcmp(head.data(), "SARC", 4) || be(head, 6, 2) != 0xFEFF)
        throw std::runtime_error("not a big-endian SARC");
    const uint32_t hlen = be(head, 4, 2), start = be(head, 12, 4);
    if (start > (64u << 20) || start < hlen + 12) throw std::runtime_error("bad SARC header");
    Bytes tables(start);  // header, SFAT and SFNT: everything before the members' data
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(tables.data()), std::streamsize(start))) throw std::runtime_error("truncated SARC");
    const uint32_t n = be(tables, hlen + 6, 2);
    const size_t names = hlen + 12 + 16 * size_t(n) + 8;
    for (uint32_t i = 0; i < n; i++) {
        const size_t e = hlen + 12 + 16 * size_t(i);
        const uint32_t attr = be(tables, e + 4, 4), s = be(tables, e + 8, 4), en = be(tables, e + 12, 4);
        const size_t at = names + size_t(attr & 0xFFFF) * 4;
        if (at >= tables.size() || strncmp(reinterpret_cast<const char*>(&tables[at]), name.c_str(), tables.size() - at) != 0) continue;
        if (en < s) throw std::runtime_error("bad SARC entry");
        Bytes out(en - s);
        f.seekg(std::streamoff(start) + s);
        if (!f.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()))) throw std::runtime_error("truncated SARC member");
        return out;
    }
    throw std::runtime_error(name + " not in " + path);
}

// ---- cache format: "WWLM", u32 algorithm, u32 count, zlib of the masks ----
namespace {
void put32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (8 * i))); }
void put64(Bytes& b, uint64_t v) { put32(b, uint32_t(v)), put32(b, uint32_t(v >> 32)); }
void put_bytes(Bytes& b, const std::vector<uint8_t>& v) { put32(b, uint32_t(v.size())), b.insert(b.end(), v.begin(), v.end()); }
struct In {
    const Bytes& b;
    size_t at = 0;
    bool ok = true;
    uint32_t u32() {
        if (at + 4 > b.size()) return ok = false, 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= uint32_t(b[at + i]) << (8 * i);
        return at += 4, v;
    }
    uint64_t u64() { const uint64_t lo = u32(); return lo | uint64_t(u32()) << 32; }
    std::vector<uint8_t> bytes(size_t limit) {
        const uint32_t n = u32();
        if (!ok || n > limit || at + n > b.size()) return ok = false, std::vector<uint8_t>();
        std::vector<uint8_t> v(b.begin() + long(at), b.begin() + long(at + n));
        return at += n, v;
    }
};
}  // namespace

Bytes serialize(const std::vector<Mask>& masks) {
    Bytes body;
    put32(body, uint32_t(masks.size()));
    for (const Mask& m : masks) {
        put_bytes(body, std::vector<uint8_t>(m.texture.begin(), m.texture.end()));
        put32(body, m.width), put32(body, m.height), put32(body, m.format), put32(body, m.image_size), put32(body, m.mip_size);
        put64(body, m.hash);
        for (uint32_t c : m.base_rgb) put32(body, c);
        put32(body, m.ftex), put_bytes(body, std::vector<uint8_t>(std::begin(m.file_head), std::end(m.file_head)));
        put_bytes(body, m.texels), put_bytes(body, m.endpoints);
    }
    uLongf packed_n = compressBound(uLong(body.size()));
    Bytes packed(packed_n);
    if (compress2(packed.data(), &packed_n, body.data(), uLong(body.size()), 6) != Z_OK) return {};
    packed.resize(packed_n);
    Bytes out = {'W', 'W', 'L', 'M'};
    put32(out, kAlgorithm), put32(out, uint32_t(body.size()));
    out.insert(out.end(), packed.begin(), packed.end());
    return out;
}

bool deserialize(const Bytes& data, std::vector<Mask>& out) {
    out.clear();
    if (data.size() < 12 || memcmp(data.data(), "WWLM", 4)) return false;
    In head{data, 4};
    if (head.u32() != kAlgorithm) return false;
    const uint32_t n = head.u32();
    if (n > (64u << 20)) return false;
    Bytes body(n);
    uLongf got = n;
    if (uncompress(body.data(), &got, data.data() + 12, uLong(data.size() - 12)) != Z_OK || got != n) return false;
    In in{body};
    const uint32_t count = in.u32();
    if (count > 16) return false;
    for (uint32_t i = 0; i < count && in.ok; i++) {
        Mask m;
        const auto name = in.bytes(64);
        m.texture.assign(name.begin(), name.end());
        m.width = in.u32(), m.height = in.u32(), m.format = in.u32(), m.image_size = in.u32(), m.mip_size = in.u32();
        m.hash = in.u64();
        for (uint32_t& c : m.base_rgb) c = in.u32();
        m.ftex = in.u32();
        const auto head = in.bytes(sizeof m.file_head);
        if (head.size() != sizeof m.file_head) return false;
        memcpy(m.file_head, head.data(), sizeof m.file_head);
        m.texels = in.bytes(size_t(m.width) * m.height);
        m.endpoints = in.bytes(size_t(m.image_size + m.mip_size) / 4);
        if (!in.ok || m.width > 2048 || m.height > 2048 || m.texels.size() != size_t(m.width) * m.height) return out.clear(), false;
        for (uint8_t p : m.texels) if (p >= kParts) return out.clear(), false;
        for (uint8_t p : m.endpoints) if (p >= kParts) return out.clear(), false;
        out.push_back(std::move(m));
    }
    if (!in.ok) out.clear();
    return in.ok;
}

}  // namespace link_masks
