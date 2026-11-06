/* SPDX-License-Identifier: MPL-2.0
 * From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (v0.9.9, MPL-2.0), runtime/src/gcmodel/, taken over
 * as it is; only the glue to this port changed. */
#include "gcm_j3d.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace gcmodel {

Mat mat_identity() { return Mat{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}}; }

Mat mat_mul(const Mat& a, const Mat& b) {
    Mat r{};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++) {
            double s = 0;
            for (int k = 0; k < 3; k++) s += a[i][k] * b[k][j];
            r[i][j] = s + (j == 3 ? a[i][3] : 0);
        }
    return r;
}

Vec3 mat_apply(const Mat& m, const Vec3& v) {
    Vec3 r;
    for (int i = 0; i < 3; i++) r[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] + m[i][3];
    return r;
}

Vec3 rot_apply(const Mat& m, const Vec3& v) {
    Vec3 r;
    for (int i = 0; i < 3; i++) r[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
    return r;
}

Mat mat_inv(const Mat& m) {
    // A^-1 of the 3x3 part (regularised when it is singular), then the translation
    double a[3][3], inv[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) a[i][j] = m[i][j];
    auto det3 = [](double x[3][3]) {
        return x[0][0] * (x[1][1] * x[2][2] - x[1][2] * x[2][1]) - x[0][1] * (x[1][0] * x[2][2] - x[1][2] * x[2][0]) +
               x[0][2] * (x[1][0] * x[2][1] - x[1][1] * x[2][0]);
    };
    double d = det3(a);
    if (std::fabs(d) < 1e-12) {
        // (A^T A + e I)^-1 A^T: near the pseudo-inverse
        double ata[3][3];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                ata[i][j] = (i == j ? 1e-9 : 0);
                for (int k = 0; k < 3; k++) ata[i][j] += a[k][i] * a[k][j];
            }
        double dd = det3(ata), ai[3][3];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                int i1 = (j + 1) % 3, i2 = (j + 2) % 3, j1 = (i + 1) % 3, j2 = (i + 2) % 3;
                ai[i][j] = (ata[i1][j1] * ata[i2][j2] - ata[i1][j2] * ata[i2][j1]) / dd;
            }
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                inv[i][j] = 0;
                for (int k = 0; k < 3; k++) inv[i][j] += ai[i][k] * a[j][k];
            }
    } else {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                int i1 = (j + 1) % 3, i2 = (j + 2) % 3, j1 = (i + 1) % 3, j2 = (i + 2) % 3;
                inv[i][j] = (a[i1][j1] * a[i2][j2] - a[i1][j2] * a[i2][j1]) / d;
            }
    }
    Mat r{};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) r[i][j] = inv[i][j];
        r[i][3] = -(inv[i][0] * m[0][3] + inv[i][1] * m[1][3] + inv[i][2] * m[2][3]);
    }
    return r;
}

Mat mat_srt(const Vec3& s, const Vec3& r, const Vec3& t) {
    const double cx = std::cos(r[0]), sx = std::sin(r[0]), cy = std::cos(r[1]), sy = std::sin(r[1]), cz = std::cos(r[2]),
                 sz = std::sin(r[2]);
    const double rot[3][3] = {{cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz},
                              {cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz},
                              {-sy, sx * cy, cx * cy}};
    Mat m{};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) m[i][j] = rot[i][j] * s[j];
        m[i][3] = t[i];
    }
    return m;
}

// ---- GameCube textures

namespace {
enum { I4 = 0, I8 = 1, IA4 = 2, IA8 = 3, RGB565 = 4, RGB5A3 = 5, RGBA8 = 6, C4 = 8, C8 = 9, C14X2 = 10, CMPR = 14 };

using RGBA = std::array<uint8_t, 4>;
RGBA c565(uint16_t v) {
    return {(uint8_t)((v >> 11) * 255 / 31), (uint8_t)((v >> 5 & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31), 255};
}
RGBA c5a3(uint16_t v) {
    if (v & 0x8000) return {(uint8_t)((v >> 10 & 31) * 255 / 31), (uint8_t)((v >> 5 & 31) * 255 / 31), (uint8_t)((v & 31) * 255 / 31), 255};
    return {(uint8_t)((v >> 8 & 15) * 17), (uint8_t)((v >> 4 & 15) * 17), (uint8_t)((v & 15) * 17), (uint8_t)((v >> 12 & 7) * 255 / 7)};
}
RGBA cia8(uint16_t v) { return {(uint8_t)(v & 255), (uint8_t)(v & 255), (uint8_t)(v & 255), (uint8_t)(v >> 8)}; }

void block_size(int fmt, int& bw, int& bh, int& bits) {
    switch (fmt) {
        case I4: case C4: case CMPR: bw = 8, bh = 8, bits = 4; break;
        case I8: case IA4: case C8: bw = 8, bh = 4, bits = 8; break;
        case RGBA8: bw = 4, bh = 4, bits = 32; break;
        default: bw = 4, bh = 4, bits = 16; break;
    }
}

size_t tex_size(int fmt, int w, int h) {
    int bw, bh, bits;
    block_size(fmt, bw, bh, bits);
    return (size_t)((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bw * bh * bits / 8;
}

Texture decode_gx(int fmt, int w, int h, const uint8_t* data, size_t avail, const std::vector<RGBA>& pal) {
    Texture t;
    t.format = fmt, t.width = w, t.height = h;
    if (tex_size(fmt, w, h) > avail) throw Error("texture data out of range");
    t.rgba.assign((size_t)w * h * 4, 0);
    int bw, bh, bits;
    block_size(fmt, bw, bh, bits);
    size_t p = 0;
    auto put = [&](int x, int y, const RGBA& c) {
        if (x < w && y < h) memcpy(&t.rgba[((size_t)y * w + x) * 4], c.data(), 4);
    };
    for (int by = 0; by < h; by += bh)
        for (int bx = 0; bx < w; bx += bw) {
            if (fmt == CMPR) {
                for (int sub = 0; sub < 4; sub++) {
                    const int sx = bx + (sub & 1) * 4, sy = by + (sub >> 1) * 4;
                    const uint16_t c0 = be16(data + p), c1 = be16(data + p + 2);
                    const uint32_t bitsv = be32(data + p + 4);
                    p += 8;
                    RGBA a = c565(c0), b = c565(c1), cs[4];
                    cs[0] = a, cs[1] = b;
                    if (c0 > c1) {
                        for (int i = 0; i < 3; i++) cs[2][i] = (uint8_t)((2 * a[i] + b[i]) / 3), cs[3][i] = (uint8_t)((a[i] + 2 * b[i]) / 3);
                        cs[2][3] = cs[3][3] = 255;
                    } else {
                        for (int i = 0; i < 3; i++) cs[2][i] = (uint8_t)((a[i] + b[i]) / 2);
                        cs[2][3] = 255;
                        cs[3] = {0, 0, 0, 0};
                    }
                    for (int i = 0; i < 16; i++) put(sx + i % 4, sy + i / 4, cs[(bitsv >> (30 - 2 * i)) & 3]);
                }
                continue;
            }
            if (fmt == RGBA8) {
                const uint8_t *ar = data + p, *gb = data + p + 32;
                p += 64;
                for (int i = 0; i < 16; i++) put(bx + i % 4, by + i / 4, {ar[2 * i + 1], gb[2 * i], gb[2 * i + 1], ar[2 * i]});
                continue;
            }
            for (int i = 0; i < bw * bh; i++) {
                const int x = bx + i % bw, y = by + i / bw;
                uint32_t v;
                if (bits == 4) v = data[p + i / 2] >> (i % 2 == 0 ? 4 : 0) & 15;
                else if (bits == 8) v = data[p + i];
                else v = be16(data + p + 2 * i);
                RGBA c;
                switch (fmt) {
                    case I4: c = {(uint8_t)(v * 17), (uint8_t)(v * 17), (uint8_t)(v * 17), (uint8_t)(v * 17)}; break;
                    case I8: c = {(uint8_t)v, (uint8_t)v, (uint8_t)v, (uint8_t)v}; break;
                    case IA4: c = {(uint8_t)((v & 15) * 17), (uint8_t)((v & 15) * 17), (uint8_t)((v & 15) * 17), (uint8_t)((v >> 4) * 17)}; break;
                    case IA8: c = cia8((uint16_t)v); break;
                    case RGB565: c = c565((uint16_t)v); break;
                    case RGB5A3: c = c5a3((uint16_t)v); break;
                    default:
                        c = pal.empty() ? RGBA{(uint8_t)v, (uint8_t)v, (uint8_t)v, 255} : pal[(v & 0x3FFF) % pal.size()];
                        break;
                }
                put(x, y, c);
            }
            p += (size_t)bw * bh * bits / 8;
        }
    return t;
}
}  // namespace

Texture decode_bti(const uint8_t* base, size_t size, size_t h) {
    if (h + 0x20 > size) throw Error("texture header out of range");
    const uint8_t* b = base + h;
    const int fmt = b[0], w = be16(b + 2), ht = be16(b + 4), palfmt = b[9], npal = be16(b + 0xA);
    const uint32_t paloff = be32(b + 0xC), doff = be32(b + 0x1C);
    std::vector<RGBA> pal;
    if (fmt == C4 || fmt == C8 || fmt == C14X2) {
        if (h + paloff + 2 * (size_t)npal > size) throw Error("palette out of range");
        for (int i = 0; i < npal; i++) {
            uint16_t v = be16(b + paloff + 2 * i);
            pal.push_back(palfmt == 0 ? cia8(v) : palfmt == 1 ? c565(v) : c5a3(v));
        }
    }
    if (h + doff > size) throw Error("texture data out of range");
    return decode_gx(fmt, w, ht, b + doff, size - h - doff, pal);
}

Texture decode_bti_file(const Bytes& bti) { return decode_bti(bti.data(), bti.size(), 0); }

// ---- the model

namespace {
std::vector<std::string> strtab(const Bytes& c, size_t o) {
    std::vector<std::string> r;
    const uint16_t n = be16(at(c, o, 2));
    for (int i = 0; i < n; i++) {
        size_t so = o + be16(at(c, o + 4 + i * 4 + 2, 2));
        std::string s;
        while (so < c.size() && c[so]) s += (char)c[so++];
        r.push_back(s);
    }
    return r;
}
}  // namespace

Model::Model(const Bytes& d) {
    if (d.size() < 0x20) throw Error("model too small");
    const uint32_t n = be32(&d[0xC]);
    size_t p = 0x20;
    for (uint32_t i = 0; i < n && p + 8 <= d.size(); i++) {
        std::string t(d.begin() + p, d.begin() + p + 4);
        const uint32_t sz = be32(&d[p + 4]);
        if (p + sz > d.size() || sz < 8) throw Error("model chunk " + t + " out of range");
        chunks[t] = Bytes(d.begin() + p, d.begin() + p + sz);
        p += sz;
    }
    for (const char* need : {"INF1", "VTX1", "DRW1", "JNT1", "SHP1", "MAT3"})
        if (!chunks.count(need)) throw Error(std::string("model without ") + need);

    // JNT1
    {
        const Bytes& c = chunks["JNT1"];
        const uint16_t nj = be16(at(c, 8, 2));
        if (nj > 1024) throw Error("model with too many joints");  // (a player model has about 50; the parent walk recurses)
        const uint32_t eo = be32(at(c, 0xC, 4)), ro = be32(at(c, 0x10, 4)), so = be32(at(c, 0x14, 4));
        auto names = strtab(c, so);
        for (int i = 0; i < nj; i++) {
            const size_t e = eo + (size_t)be16(at(c, ro + 2 * i, 2)) * 0x40;
            Vec3 s{bef32(at(c, e + 4, 4)), bef32(at(c, e + 8, 4)), bef32(at(c, e + 0xC, 4))};
            Vec3 r{bes16(at(c, e + 0x10, 2)) * M_PI / 32768.0, bes16(at(c, e + 0x12, 2)) * M_PI / 32768.0, bes16(at(c, e + 0x14, 2)) * M_PI / 32768.0};
            Vec3 t{bef32(at(c, e + 0x18, 4)), bef32(at(c, e + 0x1C, 4)), bef32(at(c, e + 0x20, 4))};
            Joint j;
            j.name = i < (int)names.size() ? names[i] : "";
            j.local = mat_srt(s, r, t);
            joints.push_back(j);
        }
    }
    // INF1: parents, shapes' materials
    std::map<int, int> shapeMaterial;
    {
        const Bytes& c = chunks["INF1"];
        size_t p2 = be32(at(c, 0x14, 4));
        std::vector<std::pair<char, int>> stack;
        std::pair<char, int> last{0, -1};
        int mat = -1;
        while (p2 + 4 <= c.size()) {
            const uint16_t t = be16(at(c, p2, 2)), i = be16(at(c, p2 + 2, 2));
            p2 += 4;
            if (t == 0) break;
            if (t == 1) stack.push_back(last);
            else if (t == 2) {
                if (!stack.empty()) stack.pop_back();
            } else if (t == 0x10) {
                int up = -1;
                for (auto& s : stack)
                    if (s.first == 'j') up = s.second;
                if (i < joints.size()) joints[i].parent = up;
                last = {'j', i};
            } else if (t == 0x11) {
                mat = i;
                last = {'m', i};
            } else if (t == 0x12) {
                int ms = mat;
                for (auto& s : stack)
                    if (s.first == 'm') ms = s.second;
                shapeMaterial[i] = ms;
                last = {'s', i};
            }
        }
        std::vector<int> done(joints.size(), 0);
        std::function<void(int)> world = [&](int k) {
            if (done[k]) return;
            done[k] = 1;
            Joint& j = joints[k];
            if (j.parent >= 0 && j.parent < (int)joints.size() && j.parent != k) {
                world(j.parent);
                j.world = mat_mul(joints[j.parent].world, j.local);
            } else {
                j.world = j.local;
            }
        };
        for (size_t k = 0; k < joints.size(); k++) world((int)k);
    }
    // EVP1
    if (chunks.count("EVP1")) {
        const Bytes& c = chunks["EVP1"];
        const uint16_t ne = be16(at(c, 8, 2));
        const uint32_t co = be32(at(c, 0xC, 4)), io = be32(at(c, 0x10, 4)), wo = be32(at(c, 0x14, 4)), mo = be32(at(c, 0x18, 4));
        size_t pi = io, pw = wo;
        int maxj = 0;
        for (int i = 0; i < ne; i++) {
            const int k = at(c, co + i, 1)[0];
            std::vector<std::pair<int, double>> env;
            for (int q = 0; q < k; q++) {
                env.push_back({be16(at(c, pi + 2 * q, 2)), bef32(at(c, pw + 4 * q, 4))});
                maxj = std::max(maxj, (int)be16(at(c, pi + 2 * q, 2)) + 1);
            }
            pi += 2 * k, pw += 4 * k;
            envelopes.push_back(env);
        }
        for (int j = 0; j < maxj; j++) {
            Mat m{};
            for (int r = 0; r < 3; r++)
                for (int q = 0; q < 4; q++) m[r][q] = bef32(at(c, mo + j * 48 + (r * 4 + q) * 4, 4));
            invBind[j] = m;
        }
    }
    // DRW1
    {
        const Bytes& c = chunks["DRW1"];
        const uint16_t nd = be16(at(c, 8, 2));
        const uint32_t wo = be32(at(c, 0xC, 4)), dof = be32(at(c, 0x10, 4));
        for (int i = 0; i < nd; i++) {
            const bool w = at(c, wo + i, 1)[0] != 0;
            const uint16_t idx = be16(at(c, dof + 2 * i, 2));
            drawWeighted.push_back(w);
            if (w && idx < envelopes.size() && !envelopes[idx].empty()) draw.push_back(envelopes[idx]);
            else draw.push_back({{idx, 1.0}});
        }
    }
    // VTX1
    {
        const Bytes& c = chunks["VTX1"];
        const uint32_t fo = be32(at(c, 8, 4));
        uint32_t offs[13];
        for (int i = 0; i < 13; i++) offs[i] = be32(at(c, 0xC + 4 * i, 4));
        auto slot = [](int attr) { return attr == POS ? 0 : attr == NRM ? 1 : attr == CLR0 ? 3 : attr == CLR1 ? 4 : 5 + (attr - TEX0); };
        for (size_t p2 = fo; p2 + 16 <= c.size(); p2 += 16) {
            const uint32_t attr = be32(at(c, p2, 4)), cnt = be32(at(c, p2 + 4, 4)), typ = be32(at(c, p2 + 8, 4));
            const uint8_t shift = at(c, p2 + 12, 1)[0];
            if (attr == 0xFF) break;
            if (attr < POS || attr > TEX0 + 7) continue;
            const uint32_t start = offs[slot((int)attr)];
            uint32_t end = (uint32_t)c.size();
            for (uint32_t o : offs)
                if (o > start && o < end) end = o;
            if (start > c.size()) throw Error("model data out of range");
            const uint8_t* raw = c.data() + start;
            const size_t len = end - start;
            if (attr == CLR0) {
                if (typ == 5)
                    for (size_t i = 0; i + 3 < len; i += 4) clr.push_back({raw[i], raw[i + 1], raw[i + 2], raw[i + 3]});
                else if (typ == 1 || typ == 2) {
                    const size_t st = typ == 1 ? 3 : 4;
                    for (size_t i = 0; i + st <= len; i += st) clr.push_back({raw[i], raw[i + 1], raw[i + 2], 255});
                } else if (typ == 0)
                    for (size_t i = 0; i + 1 < len; i += 2) clr.push_back(c565(be16(raw + i)));
                else if (typ == 3)
                    for (size_t i = 0; i + 1 < len; i += 2) {
                        uint16_t v = be16(raw + i);
                        clr.push_back({(uint8_t)((v >> 12 & 15) * 17), (uint8_t)((v >> 8 & 15) * 17), (uint8_t)((v >> 4 & 15) * 17), (uint8_t)((v & 15) * 17)});
                    }
                else if (typ == 4)
                    for (size_t i = 0; i + 2 < len; i += 3) {
                        uint32_t v = raw[i] << 16 | raw[i + 1] << 8 | raw[i + 2];
                        clr.push_back({(uint8_t)((v >> 18 & 63) * 255 / 63), (uint8_t)((v >> 12 & 63) * 255 / 63), (uint8_t)((v >> 6 & 63) * 255 / 63), (uint8_t)((v & 63) * 255 / 63)});
                    }
                continue;
            }
            if (attr == CLR1 || (attr > TEX0 && attr <= TEX0 + 7)) continue;  // (only TEX0 is used)
            int ncomp = attr == POS ? (cnt ? 3 : 2) : attr == NRM ? (cnt ? 9 : 3) : (cnt ? 2 : 1);
            const int size = typ == 4 ? 4 : typ >= 2 ? 2 : 1;
            const double scale = typ == 4 ? 1.0 : 1.0 / (double)(1 << shift);
            const size_t count = len / (size * ncomp);
            for (size_t i = 0; i < count; i++) {
                double v[9] = {};
                for (int k = 0; k < ncomp; k++) {
                    const uint8_t* q = raw + (i * ncomp + k) * size;
                    double x;
                    switch (typ) {
                        case 0: x = q[0]; break;
                        case 1: x = (int8_t)q[0]; break;
                        case 2: x = be16(q); break;
                        case 3: x = bes16(q); break;
                        default: x = bef32(q); break;
                    }
                    v[k] = x * scale;
                }
                if (attr == POS) pos.push_back({v[0], v[1], ncomp == 3 ? v[2] : 0.0});
                else if (attr == NRM) nrm.push_back({v[0], v[1], v[2]});
                else uv.push_back({v[0], ncomp == 2 ? v[1] : 0.0});
            }
        }
    }
    // SHP1
    {
        const Bytes& c = chunks["SHP1"];
        const uint16_t ns = be16(at(c, 8, 2));
        const uint32_t so = be32(at(c, 0xC, 4)), ro = be32(at(c, 0x10, 4)), ao = be32(at(c, 0x18, 4)), mo = be32(at(c, 0x1C, 4)), po = be32(at(c, 0x20, 4)),
                       mdo = be32(at(c, 0x24, 4)), plo = be32(at(c, 0x28, 4));
        for (int si = 0; si < ns; si++) {
            const size_t e = so + (size_t)be16(at(c, ro + 2 * si, 2)) * 0x28;
            const uint16_t npk = be16(at(c, e + 2, 2)), aoff = be16(at(c, e + 4, 2)), fmi = be16(at(c, e + 6, 2)), fpi = be16(at(c, e + 8, 2));
            std::vector<std::pair<uint32_t, uint32_t>> attrs;
            for (size_t q = ao + aoff; q + 8 <= c.size(); q += 8) {
                const uint32_t a = be32(at(c, q, 4)), t = be32(at(c, q + 4, 4));
                if (a == 0xFF) break;
                attrs.push_back({a, t});
            }
            Shape shape;
            auto it = shapeMaterial.find(si);
            shape.material = it == shapeMaterial.end() ? -1 : it->second;
            std::vector<uint16_t> lastTable;
            for (int k = 0; k < npk; k++) {
                const size_t pl = plo + (size_t)(fpi + k) * 8, md = mdo + (size_t)(fmi + k) * 8;
                const uint32_t size = be32(at(c, pl, 4)), off = be32(at(c, pl + 4, 4));
                const uint16_t cnt = be16(at(c, md + 2, 2));
                const uint32_t first = be32(at(c, md + 4, 4));
                std::vector<uint16_t> table(cnt);
                for (int q = 0; q < cnt; q++) {
                    table[q] = be16(at(c, mo + (first + q) * 2, 2));
                    if (table[q] == 0xFFFF && q < (int)lastTable.size()) table[q] = lastTable[q];
                }
                lastTable = table;
                size_t q = po + off;
                const size_t end = std::min(c.size(), (size_t)po + off + size);
                while (q < end) {
                    const uint8_t op = at(c, q++, 1)[0];
                    if (op == 0) continue;
                    if (op != 0x90 && op != 0x98 && op != 0xA0) break;
                    if (q + 2 > end) break;
                    const uint16_t nv = be16(at(c, q, 2));
                    q += 2;
                    std::vector<Vertex> verts;
                    for (int vi = 0; vi < nv; vi++) {
                        Vertex v;
                        int pnm = -1;
                        for (auto& [a, t] : attrs) {
                            int val;
                            if (t == 1 || t == 2) val = at(c, q++, 1)[0];
                            else if (t == 3) val = be16(at(c, q, 2)), q += 2;
                            else continue;
                            if (a == PNMTXIDX) pnm = val;
                            else if (a == POS) v.pos = val;
                            else if (a == NRM) v.nrm = val;
                            else if (a == CLR0) v.clr = val;
                            else if (a == TEX0) v.tex = val;
                        }
                        const int ti = pnm >= 0 ? pnm / 3 : 0;
                        v.mtx = ti < (int)table.size() ? table[ti] : (table.empty() ? 0 : table[0]);
                        verts.push_back(v);
                    }
                    if (op == 0x90) {
                        for (int i = 0; i + 2 < nv; i += 3) shape.tris.push_back({verts[i], verts[i + 1], verts[i + 2]});
                    } else if (op == 0x98) {
                        for (int i = 0; i + 2 < nv; i++) {
                            if (i & 1) shape.tris.push_back({verts[i + 1], verts[i], verts[i + 2]});
                            else shape.tris.push_back({verts[i], verts[i + 1], verts[i + 2]});
                        }
                    } else {
                        for (int i = 0; i + 2 < nv; i++) shape.tris.push_back({verts[0], verts[i + 1], verts[i + 2]});
                    }
                }
            }
            shapes.push_back(std::move(shape));
        }
    }
    // (indices past their arrays, from a damaged file: as if the vertex had none)
    for (auto& sh : shapes)
        for (auto& t : sh.tris)
            for (auto& v : t) {
                if (v.pos >= (int)pos.size()) v.pos = -1;
                if (v.nrm >= (int)nrm.size()) v.nrm = -1;
                if (v.tex >= (int)uv.size()) v.tex = -1;
                if (v.clr >= (int)clr.size()) v.clr = -1;
            }
    // MAT3: names, textures
    {
        const Bytes& c = chunks["MAT3"];
        const uint16_t nm = be16(at(c, 8, 2));
        uint32_t offs[30];
        for (int i = 0; i < 30; i++) offs[i] = be32(at(c, 0xC + 4 * i, 4));
        materialNames = strtab(c, offs[2]);
        for (int i = 0; i < nm; i++) {
            const size_t e = offs[0] + (size_t)be16(at(c, offs[1] + 2 * i, 2)) * 0x14C;
            std::vector<int> ts;
            for (int s = 0; s < 8; s++) {
                const uint16_t slot = be16(at(c, e + 0x84 + 2 * s, 2));
                if (slot != 0xFFFF) ts.push_back(be16(at(c, offs[15] + 2 * slot, 2)));
            }
            materialTextures.push_back(ts);
        }
    }
    // TEX1
    if (chunks.count("TEX1")) {
        const Bytes& c = chunks["TEX1"];
        const uint16_t nt = be16(at(c, 8, 2));
        const uint32_t ho = be32(at(c, 0xC, 4)), so = be32(at(c, 0x10, 4));
        textureNames = strtab(c, so);
        for (int i = 0; i < nt && i < (int)textureNames.size(); i++)
            if (!textures.count(textureNames[i])) textures[textureNames[i]] = decode_bti(c.data(), c.size(), ho + (size_t)i * 0x20);
    }
}

Vec3 Model::position(const Vertex& v) const {
    if (v.pos < 0 || v.pos >= (int)pos.size()) return {0, 0, 0};
    const Vec3 p = pos[v.pos];
    const int mi = v.mtx < (int)draw.size() ? v.mtx : 0;
    if (draw.empty()) return p;
    if (!drawWeighted[mi]) {  // rigid: in its joint's space
        const int j = draw[mi][0].first;
        return j < (int)joints.size() ? mat_apply(joints[j].world, p) : p;
    }
    Vec3 out{0, 0, 0};
    for (auto& [j, w] : draw[mi]) {
        if (j >= (int)joints.size()) continue;
        auto ib = invBind.find(j);
        const Mat m = ib == invBind.end() ? mat_identity() : mat_mul(joints[j].world, ib->second);
        const Vec3 q = mat_apply(m, p);
        for (int k = 0; k < 3; k++) out[k] += w * q[k];
    }
    return out;
}

Vec3 Model::normal(const Vertex& v) const {
    const Vec3 n = v.nrm >= 0 && v.nrm < (int)nrm.size() ? nrm[v.nrm] : Vec3{0, 1, 0};
    const int mi = v.mtx < (int)draw.size() ? v.mtx : 0;
    if (draw.empty()) return n;
    if (!drawWeighted[mi]) {
        const int j = draw[mi][0].first;
        return j < (int)joints.size() ? rot_apply(joints[j].world, n) : n;
    }
    Vec3 out{0, 0, 0};
    for (auto& [j, w] : draw[mi]) {
        if (j >= (int)joints.size()) continue;
        auto ib = invBind.find(j);
        const Mat m = ib == invBind.end() ? mat_identity() : mat_mul(joints[j].world, ib->second);
        const Vec3 q = rot_apply(m, n);
        for (int k = 0; k < 3; k++) out[k] += w * q[k];
    }
    return out;
}

}  // namespace gcmodel
