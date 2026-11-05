// 60 fps (generated): the generated camera (runtime/src/gfx/vulkan/reproj_camera.h) and the warp's
// mapping (reproj.cpp kWarp toGenCam, here in C++): a point of the real picture lands where the camera
// moved on by half its last step (half the turn, half the eye's step) sees it; cuts and still cameras
// make no in-between frame.
#include "../src/gfx/vulkan/reproj_camera.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace gfxvk::reproj;

namespace {
struct Cam {
    float eye[3], m[9];
};
Cam cam(float ex, float ey, float ez, float cx, float cy, float cz) {
    Cam c{{ex, ey, ez}, {}};
    const float center[3] = {cx, cy, cz}, up[3] = {0, 1, 0};
    basis(c.eye, center, up, c.m);
    return c;
}
// camera space of world point x (the camera looks along -z)
void view(const Cam& c, const float x[3], float v[3]) {
    for (int j = 0; j < 3; j++) {
        v[j] = 0;
        for (int k = 0; k < 3; k++) v[j] += c.m[k * 3 + j] * (x[k] - c.eye[k]);
    }
}
const float tanX = 0.7f, tanY = 0.4f;
void project(const float v[3], float uv[2]) {
    uv[0] = v[0] / -v[2] / tanX * 0.5f + 0.5f;
    uv[1] = 0.5f - v[1] / -v[2] / tanY * 0.5f;
}
// kWarp toGenCam: the real picture's pixel q at view depth z, in the generated picture
void to_gen(const float q[2], float z, const float half[9], const float move[3], float out[2]) {
    const float P[3] = {(q[0] * 2 - 1) * tanX * z - move[0], (1 - q[1] * 2) * tanY * z - move[1], -z - move[2]};
    float g[3];
    for (int j = 0; j < 3; j++) g[j] = half[0 * 3 + j] * P[0] + half[1 * 3 + j] * P[1] + half[2 * 3 + j] * P[2];
    out[0] = g[0] / -g[2] / tanX * 0.5f + 0.5f;
    out[1] = 0.5f - g[1] / -g[2] / tanY * 0.5f;
}
// the camera moved on by half a step: eye + step / 2, axes Ccur * H
Cam extrapolated(const Cam& prev, const Cam& cur, const float half[9]) {
    Cam g{};
    for (int k = 0; k < 3; k++) g.eye[k] = cur.eye[k] + 0.5f * (cur.eye[k] - prev.eye[k]);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            float s = 0;
            for (int k = 0; k < 3; k++) s += cur.m[r * 3 + k] * half[k * 3 + c];
            g.m[r * 3 + c] = s;
        }
    return g;
}
bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// every point of a few depths lands where the extrapolated camera sees it
void check_mapping(const Cam& prev, const Cam& cur, const char* what) {
    float half[9], move[3];
    Step s = half_step(prev.m, prev.eye, cur.m, cur.eye, half, move);
    assert(s == Step::Moved);
    // H * H is the whole step's turn D = Cprev^T Ccur
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) {
            float hh = 0, d = 0;
            for (int k = 0; k < 3; k++) {
                hh += half[a * 3 + k] * half[k * 3 + b];
                d += prev.m[k * 3 + a] * cur.m[k * 3 + b];
            }
            assert(near(hh, d, 1e-4f));
        }
    const Cam gen = extrapolated(prev, cur, half);
    int checked = 0;
    for (float z : {50.0f, 300.0f, 2000.0f})
        for (float u = 0.1f; u < 1.0f; u += 0.2f)
            for (float w = 0.1f; w < 1.0f; w += 0.2f) {
                // the world point at pixel (u, w) of the real picture, z away
                const float vc[3] = {(u * 2 - 1) * tanX * z, (1 - w * 2) * tanY * z, -z};
                float x[3];
                for (int k = 0; k < 3; k++) x[k] = cur.eye[k] + cur.m[k * 3 + 0] * vc[0] + cur.m[k * 3 + 1] * vc[1] + cur.m[k * 3 + 2] * vc[2];
                float vg[3], expect[2], got[2];
                view(gen, x, vg);
                if (vg[2] > -1.0f) continue;
                project(vg, expect);
                const float q[2] = {u, w};
                to_gen(q, z, half, move, got);
                assert(near(got[0], expect[0], 2e-4f) && near(got[1], expect[1], 2e-4f));
                checked++;
            }
    assert(checked > 50);
    printf("%s: %d points land where the camera half a step on sees them\n", what, checked);
}
}  // namespace

int main() {
    // a turn on the spot (0.2 rad)
    check_mapping(cam(0, 0, 0, 0, 0, -100), cam(0, 0, 0, std::sin(0.2f) * 100, 0, -std::cos(0.2f) * 100), "turn");
    // a step sideways and forward
    check_mapping(cam(0, 10, 0, 0, 10, -100), cam(30, 12, -20, 30, 12, -120), "step");
    // an orbit around Link at the origin (the follow camera): Link stays near the middle of the picture
    {
        const float r = 500, a0 = 0.0f, a1 = 0.15f;
        const Cam prev = cam(r * std::sin(a0), 100, r * std::cos(a0), 0, 0, 0), cur = cam(r * std::sin(a1), 100, r * std::cos(a1), 0, 0, 0);
        check_mapping(prev, cur, "orbit");
        float half[9], move[3];
        half_step(prev.m, prev.eye, cur.m, cur.eye, half, move);
        const Cam gen = extrapolated(prev, cur, half);
        const float link[3] = {0, 0, 0};
        float v[3], uv[2];
        view(gen, link, v);
        project(v, uv);
        assert(near(uv[0], 0.5f, 0.01f) && near(uv[1], 0.5f, 0.01f));
        printf("orbit: Link at %.4f %.4f in the in-between picture\n", uv[0], uv[1]);
    }
    // cuts and still cameras: no in-between frame (identity, no movement)
    {
        float half[9], move[3];
        const Cam a = cam(0, 0, 0, 0, 0, -100);
        assert(half_step(a.m, a.eye, a.m, a.eye, half, move) == Step::Still);
        const Cam turned = cam(0, 0, 0, std::sin(0.6f) * 100, 0, -std::cos(0.6f) * 100);
        assert(half_step(a.m, a.eye, turned.m, turned.eye, half, move) == Step::Cut);
        assert(half[0] == 1 && half[4] == 1 && half[8] == 1 && move[0] == 0 && move[1] == 0 && move[2] == 0);
        const Cam far = cam(450, 0, 0, 450, 0, -100);
        assert(half_step(a.m, a.eye, far.m, far.eye, half, move) == Step::Cut);
        const Cam justBelow = cam(399, 0, 0, 399, 0, -100);
        assert(half_step(a.m, a.eye, justBelow.m, justBelow.eye, half, move) == Step::Moved);
        assert(near(move[0], 199.5f, 1e-3f));
        printf("cuts: a 0.6 rad turn and a 450 step cut, a 399 step moves; a still camera makes no frame\n");
    }
    // the objects' movement search (full_step): a point of the current camera's space, in the previous
    // camera's, is where the previous camera sees that world point
    {
        const Cam prev = cam(10, 20, 30, 0, 0, -100), cur = cam(40, 15, -10, 30, 5, -140);
        float full[9], fullMove[3];
        full_step(prev.m, prev.eye, cur.m, cur.eye, full, fullMove);
        int checked = 0;
        for (float z : {50.0f, 900.0f})
            for (float px : {-0.5f, 0.3f})
                for (float py : {-0.2f, 0.4f}) {
                    const float vc[3] = {px * z, py * z, -z};
                    float x[3], vp[3], got[3];
                    for (int k = 0; k < 3; k++)
                        x[k] = cur.eye[k] + cur.m[k * 3 + 0] * vc[0] + cur.m[k * 3 + 1] * vc[1] + cur.m[k * 3 + 2] * vc[2];
                    view(prev, x, vp);
                    for (int r = 0; r < 3; r++) got[r] = full[r * 3 + 0] * vc[0] + full[r * 3 + 1] * vc[1] + full[r * 3 + 2] * vc[2] + fullMove[r];
                    for (int r = 0; r < 3; r++) assert(near(got[r], vp[r], 2e-2f));
                    checked++;
                }
        printf("full_step: %d points of this camera's space land in the previous camera's\n", checked);
    }
    printf("reproj_camera: ok\n");
    return 0;
}
