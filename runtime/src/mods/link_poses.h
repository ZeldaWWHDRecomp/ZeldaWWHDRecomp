// SPDX-License-Identifier: MPL-2.0
// Link pose icons (controls plan P8, H4): small pictures of Link in the pose of the action on a touch
// button (the jump on Jump, a sword swing on the sword, crouching on Crouch...), drawn once from the
// player's own game files at run time: his model "cl" of Link.bfres posed by a skeletal animation of
// LkAnm.bfres (content/Common/Pack/permanent_3d.pack). Nothing of the game ships or is stored in the repo:
// the pictures are written next to the other exported touch-button art (context_touch.cpp's export, the
// app cache on Android) and drawn by Java in place of the action's word (the word stays, smaller).
//
// Pure parts here (unit-tested in link_poses_test without the game): which pose each action code gets,
// the small rasteriser (z-buffer, texture, two-band toon light, an outline as the game's item icons have)
// and the skinning maths. link_poses.cpp reads the files with our own readers (story_resident_walk_clip.h
// for the skeleton and the animation curves, item_icons for SARC / Yaz0, lyt_build / game_layouts for
// the GX2 textures). Rendering on the CPU: the port's GPU renderer draws only the game's own GX2 command
// streams and has no host mesh path; a few 128 x 128 pictures once per pack are cheaper in a few hundred
// lines of C++ here than an offscreen pass with read-back in both GPU backends.
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0), its
// link_icons.cpp "Link in the button icons"; reimplemented on our readers.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace link_poses {

using Bytes = std::vector<uint8_t>;
inline constexpr int kIconSize = 128;
inline constexpr int kSupersample = 3;  // drawn at 3 x 3 samples per pixel, then averaged

// what Link holds in a pose (the sword in his left hand as in the game, the shield on his right arm)
enum class Gear : uint8_t { kNone, kSword, kSwordShield };

struct PoseSpec {
    uint8_t action;     // the action code (play_context.h kAct*) whose button shows it
    const char* anim;   // an animation of LkAnm.bfres
    float frame;        // the frame shown (clamped to the animation)
    float yaw;          // camera: degrees around Link (0 his front, 90 his left side)
    float pitch;        // ... and down from above
    Gear gear;
};

// The poses, by action code (A / B / ZR words). Codes without one keep the word only.
inline constexpr PoseSpec kPoses[] = {
    {0x12, "jmpst", 6, -60, 10, Gear::kNone},              // Jump: the leap
    {0x0F, "crouch", 19, -60, 10, Gear::kNone},            // Crouch
    {0x0C, "rollf", 9, -90, 10, Gear::kNone},              // Attack / Roll on A: the forward roll (A "Attack" while running is the roll)
    {0x35, "cutf", 8, -50, 10, Gear::kSword},              // Sword (B): a swing, the blade out in front
    {0x36, "difence", 5, -20, 10, Gear::kSwordShield},     // Defend: the shield up
    {0x04, "grabup", 7, -60, 10, Gear::kNone},             // Lift: the arms up
    {0x0E, "grabthrow", 3, -60, 10, Gear::kNone},          // Throw
    {0x0B, "boxopenshortlink", 12, -60, 15, Gear::kNone},  // Open
    {0x02, "talka", 8, -40, 10, Gear::kNone},              // Speak
    {0x01, "waitq", 25, -45, 10, Gear::kNone},             // Look
    {0x0A, "waitq", 25, -45, 10, Gear::kNone},             // Check
    {0x10, "wallwl", 6, 25, 15, Gear::kNone},              // Sidle: back to the wall
    {0x11, "walkpush", 10, -90, 10, Gear::kNone},          // Grab: pushing
    {0x1B, "grabp", 5, -70, 20, Gear::kNone},              // Pick up
    {0x05, "ladderupst", 10, 150, 10, Gear::kNone},        // Climb
};
inline const PoseSpec* pose_for_action(uint8_t action) {
    for (const PoseSpec& p : kPoses)
        if (p.action == action) return &p;
    return nullptr;
}
// the exported picture of an action's pose: "pose_HH.png" (HH the action code in hex, as item_HH.png)
inline std::string file_for(uint8_t action) {
    char name[16];
    snprintf(name, sizeof name, "pose_%02X.png", action);
    return name;
}

// ---- maths ----
struct V3 {
    float x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 normalize(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-12f ? a * (1.0f / l) : V3{0, 0, 1};
}
// a rotation and a translation (rows)
struct M34 {
    float m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
    V3 point(V3 p) const {
        return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3], m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
                m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]};
    }
    V3 dir(V3 p) const {
        return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z, m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z, m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z};
    }
    M34 operator*(const M34& o) const {
        M34 r;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 4; j++) {
                float s = j == 3 ? m[i][3] : 0;
                for (int k = 0; k < 3; k++) s += m[i][k] * o.m[k][j];
                r.m[i][j] = s;
            }
        return r;
    }
    // the inverse of a rigid transform (rotation + translation)
    M34 rigid_inverse() const {
        M34 r;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) r.m[i][j] = m[j][i];
        for (int i = 0; i < 3; i++) r.m[i][3] = -(r.m[i][0] * m[0][3] + r.m[i][1] * m[1][3] + r.m[i][2] * m[2][3]);
        return r;
    }
    // m * s + o * t (a skinning blend)
    static M34 blend(const M34& a, float wa, const M34& b, float wb) {
        M34 r;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 4; j++) r.m[i][j] = a.m[i][j] * wa + b.m[i][j] * wb;
        return r;
    }
};
// the icon's camera: yaw degrees around +y (0: Link's front, +z, faces the camera), then pitch degrees
// looking down; x right, y up, z towards the viewer
inline M34 camera(float yaw_deg, float pitch_deg) {
    const float y = yaw_deg * 3.14159265f / 180, p = pitch_deg * 3.14159265f / 180;
    const float cy = std::cos(y), sy = std::sin(y), cp = std::cos(p), sp = std::sin(p);
    M34 yawm, pitchm;
    yawm.m[0][0] = cy, yawm.m[0][2] = -sy, yawm.m[2][0] = sy, yawm.m[2][2] = cy;
    pitchm.m[1][1] = cp, pitchm.m[1][2] = -sp, pitchm.m[2][1] = sp, pitchm.m[2][2] = cp;
    return pitchm * yawm;
}

// ---- the rasteriser ----
struct Texture {
    int w = 0, h = 0;
    Bytes rgba;  // RGBA8, row 0 at the top
    // nearest texel, coordinates wrapped (repeat)
    std::array<uint8_t, 4> at(float u, float v) const {
        if (w <= 0 || h <= 0) return {255, 255, 255, 255};
        int x = int(std::floor(u * w)) % w, y = int(std::floor(v * h)) % h;
        if (x < 0) x += w;
        if (y < 0) y += h;
        const uint8_t* p = &rgba[(size_t(y) * w + x) * 4];
        return {p[0], p[1], p[2], p[3]};
    }
};
struct ScreenVertex {
    float x = 0, y = 0, z = 0;  // pixels (y down), depth (larger is nearer)
    float u = 0, v = 0;
    float light = 1;            // n . l in -1 .. 1
};
struct Canvas {
    int w = 0, h = 0;
    std::vector<float> depth;
    Bytes rgba;
    Canvas(int width, int height) : w(width), h(height), depth(size_t(width) * height, -1e30f), rgba(size_t(width) * height * 4, 0) {}
};
// two tones as the game's toon ramp gives Link: lit, or in shade (a soft step around n . l = 0)
inline float toon(float light) {
    const float t = std::clamp((light + 0.05f) / 0.1f, 0.0f, 1.0f);
    return 0.66f + 0.34f * t;
}
// one triangle: nearest depth wins; texels with alpha below one half are cut out (as the game's alpha test);
// `overlay` (the eyes' pupils) is drawn over the texture where it is opaque
inline void draw_triangle(Canvas& c, const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& d, const Texture* tex,
                          const std::array<uint8_t, 3>& flat = {200, 200, 200}, const Texture* overlay = nullptr) {
    const float area = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
    if (std::fabs(area) < 1e-8f) return;
    const int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, d.x})))), x1 = std::min(c.w - 1, int(std::ceil(std::max({a.x, b.x, d.x}))));
    const int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, d.y})))), y1 = std::min(c.h - 1, int(std::ceil(std::max({a.y, b.y, d.y}))));
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float w0 = ((b.x - px) * (d.y - py) - (b.y - py) * (d.x - px)) / area;
            const float w1 = ((d.x - px) * (a.y - py) - (d.y - py) * (a.x - px)) / area;
            const float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            const float z = w0 * a.z + w1 * b.z + w2 * d.z;
            const size_t i = size_t(y) * c.w + x;
            if (z <= c.depth[i]) continue;
            std::array<uint8_t, 4> t = {flat[0], flat[1], flat[2], 255};
            const float u = w0 * a.u + w1 * b.u + w2 * d.u, v = w0 * a.v + w1 * b.v + w2 * d.v;
            if (tex) t = tex->at(u, v);
            if (t[3] < 128) continue;
            if (overlay) {
                const auto o = overlay->at(u, v);
                if (o[3] >= 128) t = {o[0], o[1], o[2], 255};
            }
            const float s = toon(w0 * a.light + w1 * b.light + w2 * d.light);
            c.depth[i] = z;
            uint8_t* p = &c.rgba[i * 4];
            for (int k = 0; k < 3; k++) p[k] = uint8_t(std::clamp(t[k] * s + 0.5f, 0.0f, 255.0f));
            p[3] = 255;
        }
}
// the canvas averaged down by `factor` (premultiplied, so the edges stay clean)
inline Bytes downsample(const Canvas& c, int factor) {
    const int w = c.w / factor, h = c.h / factor;
    Bytes out(size_t(w) * h * 4, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float sum[4] = {};
            for (int j = 0; j < factor; j++)
                for (int i = 0; i < factor; i++) {
                    const uint8_t* p = &c.rgba[(size_t(y * factor + j) * c.w + x * factor + i) * 4];
                    const float a = p[3] / 255.0f;
                    for (int k = 0; k < 3; k++) sum[k] += p[k] * a;
                    sum[3] += a;
                }
            uint8_t* o = &out[(size_t(y) * w + x) * 4];
            const float n = float(factor * factor);
            if (sum[3] > 0)
                for (int k = 0; k < 3; k++) o[k] = uint8_t(std::clamp(sum[k] / sum[3] + 0.5f, 0.0f, 255.0f));
            o[3] = uint8_t(std::clamp(sum[3] / n * 255 + 0.5f, 0.0f, 255.0f));
        }
    return out;
}
// a dark outline `radius` pixels wide behind the picture (as the game's own item icons have), so it reads
// on any background
inline void outline(Bytes& rgba, int w, int h, float radius, std::array<uint8_t, 3> ink = {24, 20, 16}) {
    const Bytes src = rgba;
    const int r = int(std::ceil(radius));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float cover = 0;  // the outline's alpha: the strongest picture alpha within the radius, softened at its rim
            for (int j = -r; j <= r; j++)
                for (int i = -r; i <= r; i++) {
                    const int sx = x + i, sy = y + j;
                    if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
                    const float d = std::sqrt(float(i * i + j * j));
                    const float fall = std::clamp(radius + 0.5f - d, 0.0f, 1.0f);
                    cover = std::max(cover, src[(size_t(sy) * w + sx) * 4 + 3] / 255.0f * fall);
                }
            uint8_t* p = &rgba[(size_t(y) * w + x) * 4];
            const float a = p[3] / 255.0f, out_a = a + cover * (1 - a);
            if (out_a <= 0) continue;
            for (int k = 0; k < 3; k++) p[k] = uint8_t(std::clamp((p[k] * a + ink[k] * cover * (1 - a)) / out_a + 0.5f, 0.0f, 255.0f));
            p[3] = uint8_t(std::clamp(out_a * 255 + 0.5f, 0.0f, 255.0f));
        }
}

// ---- reading and rendering (link_poses.cpp) ----
struct Model;  // Link's model "cl" with his gear, ready to pose
// Reads Link.bfres (the model, its skeleton and textures). Null with `why` on failure.
std::shared_ptr<const Model> load(const Bytes& link_bfres, std::string& why);
// One pose as a kIconSize x kIconSize RGBA picture. False with `why` (no such animation...).
bool render(const Model& m, const Bytes& anim_bfres, const PoseSpec& pose, Bytes& rgba, std::string& why, int size = kIconSize);
// Link.bfres and LkAnm.bfres out of permanent_3d.pack (a SARC of Yaz0 SARCs). False with `why`.
bool read_pack(const Bytes& pack, Bytes& link_bfres, Bytes& anim_bfres, std::string& why);

// ---- the port ----
// the touch buttons show the poses (setting touchLinkPoses, WWHD_LINK_POSES; on by default)
bool enabled();
void set_enabled(bool on);
// the game opened a file (fs.cpp): remember the host path of its 3D pack
void note_open(const std::string& guest_path, const std::string& host_path);
// Writes every pose's picture into `dir` (file_for(action), RGBA PNG), with `encode` making the PNG.
// The number written; -1 when the files can't be read (logged once), 0 when the game hasn't opened its
// 3D pack yet.
int export_to(const std::string& dir, bool (*encode)(uint32_t w, uint32_t h, const uint8_t* rgba, Bytes& png));

}  // namespace link_poses
