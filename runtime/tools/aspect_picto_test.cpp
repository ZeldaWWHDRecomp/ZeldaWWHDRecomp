// The Picto Box at other aspect ratios (runtime/src/aspect_picto.h, issue #303): the photo's source
// rectangle and the subjects' projection at a wider (Hor+) or taller (Vert+) TV picture give the same
// part of the scene as at 16:9.
#include "aspect_picto.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
constexpr float kBase = 16.0f / 9.0f;
constexpr float kPi = 3.14159265358979f;

// aspect.cpp: factors() and the projection the port gives the TV camera at aspect a (Hor+ wider,
// Vert+ taller with the vertical field of view grown by ky)
void factors(float a, float& kx, float& ky) {
    kx = a >= kBase ? a / kBase : 1.0f;
    ky = a >= kBase ? 1.0f : kBase / a;
}
// GX style (w = -z), with off-centre terms m[0][2], m[1][2] (the Picto Box camera has m[1][2] = 0.35)
void perspective(float m[16], float fovyRad, float aspect, float ox, float oy) {
    std::memset(m, 0, 16 * sizeof(float));
    const float f = 1.0f / std::tan(fovyRad * 0.5f);
    m[0] = f / aspect;
    m[2] = ox;
    m[5] = f;
    m[6] = oy;
    m[10] = -1.0f;
    m[11] = -2.0f;
    m[14] = -1.0f;
}
void tv_projection(float m[16], float fovyRad, float a, float ox = 0.0f, float oy = 0.0f) {
    float kx, ky;
    factors(a, kx, ky);
    perspective(m, ky == 1.0f ? fovyRad : 2.0f * std::atan(std::tan(fovyRad * 0.5f) * ky), a, ox, oy);
}
// texture coordinates (0..1, v down) of a camera-space point through a projection
void to_uv(const float m[16], const float p[3], float& u, float& v) {
    const float x = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
    const float y = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
    const float w = m[12] * p[0] + m[13] * p[1] + m[14] * p[2] + m[15];
    u = 0.5f + 0.5f * x / w;
    v = 0.5f - 0.5f * y / w;
}
bool near(float a, float b, float eps = 2e-5f) { return std::fabs(a - b) <= eps * std::fmax(1.0f, std::fabs(b)); }

// the game's finder frame (dSnap_packet::draw at 16:9): 1120x630 of the 1280x720 screen from (80, 45.5)
constexpr float kPosX = 80.0f / 1280.0f, kPosY = 45.5f / 720.0f;
constexpr float kScaleX = 1120.0f / 1600.0f, kScaleY = 630.0f / 900.0f;  // per destination texel of the 1600x900 photo
constexpr float kExtentX = 1600.0f / 1280.0f, kExtentY = 900.0f / 720.0f;  // destination width, height in source units

void check_aspect(float a, float ox, float oy) {
    float kx, ky;
    factors(a, kx, ky);
    const float fovy = 50.0f * kPi / 180.0f;
    float m169[16], mtv[16];
    tv_projection(m169, fovy, kBase, ox, oy);
    tv_projection(mtv, fovy, a, ox, oy);

    // 1. the photo: a point at finder position t (0..1 over the photo) of the 16:9 picture is found at
    //    the same t through the moved rectangle in the wider/taller picture
    float px = kPosX, sx = kScaleX, py = kPosY, sy = kScaleY;
    aspect::picto::crop_to_16x9(px, sx, kx, -mtv[2]);
    aspect::picto::crop_to_16x9(py, sy, ky, mtv[6]);
    int seen = 0;
    for (int i = 0; i < 400; i++) {
        const float p[3] = {(float)((i * 37) % 41 - 20) * 13.0f, (float)((i * 53) % 29 - 14) * 9.0f, -300.0f - 7.0f * i};
        float u169, v169, utv, vtv;
        to_uv(m169, p, u169, v169);
        to_uv(mtv, p, utv, vtv);
        const float tx = (u169 - kPosX) / (kScaleX * kExtentX), ty = (v169 - kPosY) / (kScaleY * kExtentY);
        if (tx < 0 || tx > 1 || ty < 0 || ty > 1) continue;  // outside the finder at 16:9
        seen++;
        assert(near(px + tx * sx * kExtentX, utv));
        assert(near(py + ty * sy * kExtentY, vtv));
    }
    assert(seen > 50);

    // 2. the subjects: the TV projection narrowed to its 16:9 part is the 16:9 projection
    float n[16];
    std::memcpy(n, mtv, sizeof n);
    aspect::picto::narrow_to_16x9(n, kx, ky);
    for (int i = 0; i < 16; i++) assert(near(n[i], m169[i]));
    // ... also after the game's own narrowing to the finder (scales on the diagonal, offset m[1][2]),
    //     done before the hook
    float g169[16], gtv[16];
    std::memcpy(g169, m169, sizeof g169);
    std::memcpy(gtv, mtv, sizeof gtv);
    for (float* g : {g169, gtv}) {
        g[0] *= 0.5714286f;
        g[5] *= 0.5714286f;
        g[2] = 0.0f;
        g[6] = 0.39683f;
    }
    aspect::picto::narrow_to_16x9(gtv, kx, ky);
    for (int i = 0; i < 16; i++) assert(near(gtv[i], g169[i]));
    std::printf("aspect %.4f (x%.4f wide, x%.4f tall), off-centre %g %g: finder %.4f+%.4f, %.4f+%.4f; %d points\n", a, kx, ky,
                ox, oy, px, sx, py, sy, seen);
}
}  // namespace

int main() {
    // 16:9: nothing changes, bit for bit
    float p = kPosX, s = kScaleX, m[16];
    aspect::picto::crop_to_16x9(p, s, 1.0f, 0.35f);
    assert(std::memcmp(&p, &kPosX, 4) == 0 && std::memcmp(&s, &kScaleX, 4) == 0);
    tv_projection(m, 0.9f, kBase, 0.0f, 0.35f);
    float n[16];
    std::memcpy(n, m, sizeof n);
    aspect::picto::narrow_to_16x9(n, 1.0f, 1.0f);
    assert(std::memcmp(m, n, sizeof m) == 0);

    // wider (21:9, 32:9, a custom 2.5) and taller (16:10, 4:3, a 9:16 phone held upright: clamped to 1)
    // centred projections, the Picto Box camera's (above the centre) and one off-centre both ways
    for (float a : {64.0f / 27.0f, 32.0f / 9.0f, 2.5f, 16.0f / 10.0f, 4.0f / 3.0f, 1.0f}) {
        check_aspect(a, 0.0f, 0.0f);
        check_aspect(a, 0.0f, 0.35f);
        check_aspect(a, -0.2f, -0.1f);
    }

    // the centre stays the centre; the 16:9 picture's edges map to the centred 16:9 part's edges
    float c = 0.5f, cs = 1.0f;
    aspect::picto::crop_to_16x9(c, cs, 2.0f);
    assert(c == 0.5f && cs == 0.5f);
    float e = 0.0f, es = 1.0f;
    aspect::picto::crop_to_16x9(e, es, 2.0f);
    assert(e == 0.25f && e + es == 0.75f);
    std::printf("aspect_picto: ok\n");
    return 0;
}
