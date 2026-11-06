// SPDX-License-Identifier: MPL-2.0
// Online co-op: emotes (GAPS C22, DESIGN.md 2.2; Q10 provisional: puppet-only plus an icon over your own Link).
//
// An emote is a short animation of Link's own (LkAnm.bfres, read from the player's game files at run time, as the
// pose icons are: link_poses.h), played on a player's puppet on the others' screens. Nothing drives the player's own
// Link: on their own screen a small bubble over his head names the emote while it plays for the others.
//
// On the puppet the clip's pose of Link's body ("cl", the model daPy_lk_c's mpCLModel draws) is blended over the pose
// the player sent, in and out; every other model of the pose (his hair, hands, sword and shield, what he holds) follows
// the body joint it hangs nearest to, so it moves with the body. Only a puppet standing still plays one: once its
// player moves away the emote ends.
//
// The pure part here (the catalogue, the matrices, the blend) is checked by coop_emotes_test without the game.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "coop_pose.h"

namespace mods::coop::emotes {

struct Emote {
    uint8_t id;        // on the network (net::kEmote, the server's EMOTE)
    const char* clip;  // an animation of LkAnm.bfres
    const char* name;  // on the Online page and in the bubble
    uint8_t plays;     // times the clip plays
    float hold;        // seconds its last frame is held after that
};
inline constexpr Emote kEmotes[] = {
    {1, "yobu", "Wave", 2, 0.2f},
    {2, "glad", "Cheer", 1, 0.3f},
    {3, "seyyes", "Yes", 1, 0.2f},
    {4, "surprised", "Surprised", 1, 1.0f},
    {5, "who", "Look around", 1, 0.0f},
};
inline constexpr int kCount = int(sizeof kEmotes / sizeof kEmotes[0]);
inline constexpr float kFps = 30;        // LkAnm's frames a second (the game's logic rate)
inline constexpr float kBlendIn = 0.2f, kBlendOut = 0.25f;  // seconds
inline constexpr float kMoveAway = 40;   // units a puppet may move from where its emote began
inline constexpr uint32_t kMinGapMs = 1000;  // one a second at most (the server's limit too)

inline const Emote* find(uint8_t id) {
    for (const Emote& e : kEmotes)
        if (e.id == id) return &e;
    return nullptr;
}
// seconds an emote lasts, for a clip of `frames` frames (the last is the loop's first again)
inline float length(const Emote& e, unsigned frames) {
    const float once = frames > 1 ? float(frames - 1) / kFps : 0.f;
    return once * e.plays + e.hold;
}
// the clip's frame at `t` seconds (played `plays` times, then held)
inline float frame_at(const Emote& e, unsigned frames, float t) {
    if (frames <= 1) return 0;
    const float once = float(frames - 1) / kFps;
    if (t >= once * e.plays) return float(frames - 1);
    const float in = std::fmod(std::max(t, 0.f), once);
    return in * kFps;
}
// the emote's weight over the pose the player sent at `t` of `len` seconds (eased in and out)
inline float weight(float t, float len) {
    if (!(t >= 0) || !(t < len)) return 0;
    const float x = std::clamp(std::min(t / kBlendIn, (len - t) / kBlendOut), 0.f, 1.f);
    return x * x * (3 - 2 * x);
}

// ---- 3 x 4 matrices (rows; the 4th column the translation), rotation with a uniform scale
struct Mx {
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
};
inline Mx mul(const Mx& a, const Mx& b) {
    Mx o;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
            o.m[4 * r + c] = a.m[4 * r] * b.m[c] + a.m[4 * r + 1] * b.m[4 + c] + a.m[4 * r + 2] * b.m[8 + c] + (c == 3 ? a.m[4 * r + 3] : 0);
    return o;
}
inline float scale_of(const Mx& a) {
    float s = 0;
    for (int c = 0; c < 3; c++) s += std::sqrt(a.m[c] * a.m[c] + a.m[4 + c] * a.m[4 + c] + a.m[8 + c] * a.m[8 + c]);
    return s / 3;
}
// the inverse of a rotation with a uniform scale and a translation
inline Mx inverse(const Mx& a) {
    const float s = scale_of(a), k = s > 1e-6f ? 1 / (s * s) : 0;
    Mx o;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) o.m[4 * r + c] = a.m[4 * c + r] * k;
    for (int r = 0; r < 3; r++) o.m[4 * r + 3] = -(o.m[4 * r] * a.m[3] + o.m[4 * r + 1] * a.m[7] + o.m[4 * r + 2] * a.m[11]);
    return o;
}
// a pose's joint (coop_pose.h: rotation x32767, translation x8, scale x64) as a matrix, and back (false: out of range)
inline Mx from_joint(const Joint& j) {
    float w = j.q[0] / 32767.f, x = j.q[1] / 32767.f, y = j.q[2] / 32767.f, z = j.q[3] / 32767.f;
    const float n = 1 / std::sqrt(std::max(1e-9f, w * w + x * x + y * y + z * z));
    w *= n, x *= n, y *= n, z *= n;
    const float s = j.s / 64.f;
    const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                           {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                           {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
    Mx o;
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 3; k++) o.m[4 * i + k] = r[i][k] * s;
        o.m[4 * i + 3] = j.t[i] / 8.f;
    }
    return o;
}
// the rotation of `a` (scale taken out) as a quaternion w, x, y, z
inline void quat_of(const Mx& a, float q[4]) {
    const float s = scale_of(a), k = s > 1e-6f ? 1 / s : 0;
    const float m00 = a.m[0] * k, m01 = a.m[1] * k, m02 = a.m[2] * k, m10 = a.m[4] * k, m11 = a.m[5] * k, m12 = a.m[6] * k,
                m20 = a.m[8] * k, m21 = a.m[9] * k, m22 = a.m[10] * k;
    const float tr = m00 + m11 + m22;
    if (tr > 0) {
        const float t = std::sqrt(tr + 1) * 2;
        q[0] = t / 4, q[1] = (m21 - m12) / t, q[2] = (m02 - m20) / t, q[3] = (m10 - m01) / t;
    } else if (m00 > m11 && m00 > m22) {
        const float t = std::sqrt(1 + m00 - m11 - m22) * 2;
        q[0] = (m21 - m12) / t, q[1] = t / 4, q[2] = (m01 + m10) / t, q[3] = (m02 + m20) / t;
    } else if (m11 > m22) {
        const float t = std::sqrt(1 + m11 - m00 - m22) * 2;
        q[0] = (m02 - m20) / t, q[1] = (m01 + m10) / t, q[2] = t / 4, q[3] = (m12 + m21) / t;
    } else {
        const float t = std::sqrt(1 + m22 - m00 - m11) * 2;
        q[0] = (m10 - m01) / t, q[1] = (m02 + m20) / t, q[2] = (m12 + m21) / t, q[3] = t / 4;
    }
}
inline bool to_joint(const Mx& a, Joint& j) {
    float q[4];
    quat_of(a, q);
    for (int e = 0; e < 3; e++) {
        const float t = a.m[4 * e + 3] * 8;
        if (!std::isfinite(t) || std::fabs(t) > 32767) return false;
        j.t[e] = (int16_t)std::lround(t);
    }
    for (int e = 0; e < 4; e++) {
        if (!std::isfinite(q[e])) return false;
        j.q[e] = (int16_t)std::lround(std::clamp(q[e], -1.f, 1.f) * 32767);
    }
    j.s = (uint8_t)std::clamp<long>(std::lround(scale_of(a) * 64), 0, 255);
    return true;
}
// a between b at t 0..1: the rotation the short way, translation and scale in a line
inline Mx blend(const Mx& a, const Mx& b, float t) {
    float qa[4], qb[4];
    quat_of(a, qa), quat_of(b, qb);
    const float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    float q[4];
    for (int e = 0; e < 4; e++) q[e] = qa[e] * (1 - t) + (d < 0 ? -qb[e] : qb[e]) * t;
    const float n = 1 / std::sqrt(std::max(1e-12f, q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]));
    const float w = q[0] * n, x = q[1] * n, y = q[2] * n, z = q[3] * n;
    const float s = scale_of(a) * (1 - t) + scale_of(b) * t;
    const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                           {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                           {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
    Mx o;
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 3; k++) o.m[4 * i + k] = r[i][k] * s;
        o.m[4 * i + 3] = a.m[4 * i + 3] * (1 - t) + b.m[4 * i + 3] * t;
    }
    return o;
}

// The emote on a pose: the body model (Link's model `body_k`) takes the clip's joints `e` (Link's model space: his
// place and heading, as a pose's joints are) at weight w; every other model moves with the body joint nearest its first
// joint. False (the pose unchanged) when the pose has no such body or its joints are not the clip skeleton's.
inline bool apply(Pose& p, int body_k, const std::vector<Mx>& e, float w) {
    if (body_k < 0 || w <= 0) return false;
    Model* body = nullptr;
    for (Model& m : p.models)
        if (m.k == body_k && m.k != kHeld) body = &m;
    if (!body || body->j.size() != e.size() || e.empty()) return false;
    const size_t n = e.size();
    std::vector<Mx> was(n), now(n);
    for (size_t j = 0; j < n; j++) was[j] = from_joint(body->j[j]), now[j] = blend(was[j], e[j], std::min(w, 1.f));
    for (Model& m : p.models) {
        if (&m == body || m.j.empty()) continue;
        const Mx root = from_joint(m.j[0]);
        size_t near = 0;
        float best = 1e30f;
        for (size_t j = 0; j < n; j++) {
            const float dx = was[j].m[3] - root.m[3], dy = was[j].m[7] - root.m[7], dz = was[j].m[11] - root.m[11];
            if (const float d = dx * dx + dy * dy + dz * dz; d < best) best = d, near = j;
        }
        const Mx move = mul(now[near], inverse(was[near]));
        for (Joint& jt : m.j) {
            Joint out = jt;
            if (to_joint(mul(move, from_joint(jt)), out)) jt = out;
        }
    }
    for (size_t j = 0; j < n; j++) {
        Joint out = body->j[j];
        if (to_joint(now[j], out)) body->j[j] = out;
    }
    return true;
}

// ---- the port (coop_emotes.cpp)
// a player's emote came (the network: net::kEmote or the server's EMOTE), for the puppet in `slot`
void play(int slot, uint8_t id);
// the puppet in `slot` is let go (its player left)
void stop(int slot);
// coop_pose's show(): the emote playing on the puppet in `slot` put on its pose (at its place `pos`); `body_k` Link's
// body model (his creation order)
void pose(int slot, Pose& p, const float pos[3], int body_k);
// this player's own emote (the Online page, any thread): sent at the next step, the bubble over Link meanwhile
void mine(uint8_t id);
// each logic step of a session (game thread): an emote asked for sent, the bubble's place over Link (0: none);
// the test aid WWHD_COOP_TEST_EMOTE=<s>:<id>[,<s>:<id>...] (seconds after the first step)
void step(uint32_t link);
// main (before the overlay seals): the bubble's layer
void register_overlay();

// (C13) a cheap puppet for a player at mid range: only Link's body, posed by one of his own clips by its speed
enum Gait : uint8_t { kWait, kWalk, kDash, kGaits };
inline constexpr const char* kGaitClips[kGaits] = {"wait", "walk", "dash"};
// the gait for a speed (units a second), with hysteresis around the steps (`was` the gait shown)
inline Gait gait_for(float speed, Gait was) {
    const float up[kGaits] = {0, 40, 330}, down[kGaits] = {0, 25, 290};
    Gait g = kWait;
    for (int i = 1; i < kGaits; i++) if (speed >= (int(was) >= i ? down[i] : up[i])) g = Gait(i);
    return g;
}
// p's models: the body (Link's model body_k) in that gait at `t` seconds (looping); false while the clips aren't read
bool cheap_pose(Gait g, float t, int body_k, Pose& p);

}  // namespace mods::coop::emotes
