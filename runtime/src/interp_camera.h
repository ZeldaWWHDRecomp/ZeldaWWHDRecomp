// Frame interpolation of the camera without game state: the blended camera between two logic steps
// and the snap (cut) decisions. interp.cpp (camera_draw hook) uses it; runtime/tools/
// interp_camera_test.cpp tests it (ctest interp_camera).
//
// Orbits around the look-at point blend the eye's direction and distance separately, so the blended
// eye stays on the arc instead of cutting the corner towards the target. A step is drawn exactly
// (not blended) when it is a cut:
//   - cam_snap(): the eye or centre moved further than an absolute limit, far more than the camera's
//     recent motion, or the field of view jumped (doors, warps, mode changes, cutscene cuts);
//   - blend(): the view turned more than 45 degrees in one step;
//   - blend(): the eye-centre distance changed by more than 10 % in one step on a step where that
//     rule applies (distance_rule_applies()).
//
// The 10 % distance rule. The camera's collision check (dCamera_c::bumpCheck, 024FD11C) pulls the
// eye in front of walls, trees and bushes abruptly; a blended distance there could put the
// in-between frames inside the obstacle, so such steps are drawn exactly. The rule used to apply to
// every step, which also cut every fast but smooth zoom: the subject camera closes the radius in a
// straight line over 7 or 10 steps when going into first person, the telescope or item aiming (13 %
// per step and more), the follow camera eases back out from radius 20 at 5 % per step when leaving
// them, and the eye recovers outwards after a pull-in. Those were drawn at 30 fps (issue #251). The
// rule now applies only on steps where the collision check reported a wall or corner hit (bumpCheck
// sets mEventFlags 0x80 / 0x80000, cleared by Run just before it), and to event and demo cameras,
// where it is part of what catches cutscene cuts (a cut to a closer shot along the same line of
// sight while the camera was already moving).
// debug: WWHD_INTERP_CAM_DIST_RULE=0 turns the rule off, =2 applies it to every step (the old rule).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "interp_pacing.h"

namespace interp::camera {

struct CamState {
    float eye[3], center[3], up[3], fovy;
    int16_t bank;
};

inline float dist(const float* a, const float* b) {
    float d = 0;
    for (int i = 0; i < 3; i++) d += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(d);
}

// what the step's camera did, for the distance rule (recorded by the bumpCheck hook on the logic
// pass, before the camera is drawn)
struct StepInfo {
    bool pulled = false;  // the collision check hit a wall or corner and placed the eye in front of it
    bool event = false;   // an event or demo (cutscene) camera
};
enum DistRule { kDistOff = 0, kDistCollision = 1, kDistAlways = 2 };
inline bool distance_rule_applies(const StepInfo& s, int rule) {
    return rule == kDistAlways || (rule == kDistCollision && (s.pulled || s.event));
}
// the distance changed abruptly (by more than 10 %) in one step
inline bool distance_jump(float la, float lb) { return std::fabs(la - lb) > 0.1f * std::max(la, lb); }

// Is the step a = S -> b = S+1 a cut? `last_step` is the camera's movement over the previous step
// (updated here). Decided once per step, on the logic pass.
inline bool cam_snap(const CamState& a, const CamState& b, float& last_step, float cut = 800.0f) {
    const float step = std::max(dist(a.eye, b.eye), dist(a.center, b.center));
    const float prev = last_step;
    last_step = step;
    return step > cut || std::fabs(a.fovy - b.fovy) > 20.0f || (step > 60.0f && step > 4.0f * prev + 20.0f);
}

// Blended camera at t between the last exact step a and the new one b. `dist_rule`: the 10 % distance
// rule applies on this step. Returns b (drawn exactly) when the step turned out to be a cut.
inline CamState blend(const CamState& a, const CamState& b, float t, bool dist_rule) {
    using pacing::lerp_f;
    CamState m;
    for (int i = 0; i < 3; i++) {
        m.center[i] = lerp_f(a.center[i], b.center[i], t);
        m.up[i] = lerp_f(a.up[i], b.up[i], t);
    }
    // eye = center + direction * distance, each blended on its own
    float da[3], db[3], la = 0, lb = 0;
    for (int i = 0; i < 3; i++) {
        da[i] = a.eye[i] - a.center[i];
        db[i] = b.eye[i] - b.center[i];
        la += da[i] * da[i];
        lb += db[i] * db[i];
    }
    la = std::sqrt(la);
    lb = std::sqrt(lb);
    if (la > 1e-3f && lb > 1e-3f) {
        float cosang = 0;
        for (int i = 0; i < 3; i++) cosang += (da[i] / la) * (db[i] / lb);
        if (cosang < 0.7071f) return b;  // view turned more than 45 degrees in one step: a snap
        if (dist_rule && distance_jump(la, lb)) return b;  // pulled in by the collision check, or an event cut
        float dir[3], ld = 0;
        // direction: halfway = normalised sum; other fractions slerp (constant angular speed over
        // the step's frames; nlerp would bunch them towards the middle)
        float wa, wb;
        pacing::slerp_weights(cosang, t, wa, wb);
        for (int i = 0; i < 3; i++) {
            dir[i] = wa * da[i] / la + wb * db[i] / lb;  // blended direction (normalised below)
            ld += dir[i] * dir[i];
        }
        ld = std::sqrt(ld);
        float len = lerp_f(la, lb, t);
        for (int i = 0; i < 3; i++) m.eye[i] = m.center[i] + (ld > 1e-3f ? dir[i] / ld : db[i] / lb) * len;
    } else {
        for (int i = 0; i < 3; i++) m.eye[i] = lerp_f(a.eye[i], b.eye[i], t);
    }
    // up: normalised and made perpendicular to the blended view direction, so the in-between frame
    // gets no extra roll (matters most when looking down from above, where small differences in up
    // turn into large twists)
    float fwd[3], lf = 0, lu = 0, d = 0;
    for (int i = 0; i < 3; i++) { fwd[i] = m.center[i] - m.eye[i]; lf += fwd[i] * fwd[i]; }
    lf = std::sqrt(lf);
    if (lf > 1e-3f) {
        for (int i = 0; i < 3; i++) { fwd[i] /= lf; d += m.up[i] * fwd[i]; }
        for (int i = 0; i < 3; i++) { m.up[i] -= d * fwd[i]; lu += m.up[i] * m.up[i]; }
        lu = std::sqrt(lu);
        if (lu > 0.1f) {
            for (int i = 0; i < 3; i++) m.up[i] /= lu;
        } else {
            for (int i = 0; i < 3; i++) m.up[i] = b.up[i];  // degenerate: keep the exact up vector
        }
    }
    m.fovy = lerp_f(a.fovy, b.fovy, t);
    m.bank = pacing::lerp_s16(a.bank, b.bank, t);  // shortest way round
    return m;
}

// Camera shake (dCamera_c::shakeCamera: explosions, boss hits, heavy landings). The camera's store
// (024F8430) draws the body's eye and centre plus a shake offset that flips its sign every step
// (mEyeShake, mCenterShake; and mFovYShake, mBankShake added to fovy and bank). Blending the drawn
// cameras of two steps averaged +a and -b towards zero: the in-between frames lost most of the
// shake. The base camera (without the shake) is blended instead, and the new step's shake is added
// on top, so every frame of a step shakes as the 30 fps frame of that step does.
struct Shake {
    float eye[3] = {0, 0, 0}, center[3] = {0, 0, 0}, fovy = 0;
    int16_t bank = 0;
    bool on = false;  // the store added it to the drawn camera (no demo camera, no event flag 1)
};
inline CamState unshake(const CamState& s, const Shake& k) {
    if (!k.on) return s;
    CamState b = s;
    for (int i = 0; i < 3; i++) {
        b.eye[i] -= k.eye[i];
        b.center[i] -= k.center[i];
    }
    b.fovy -= k.fovy;
    b.bank = (int16_t)(uint16_t)((uint16_t)b.bank - (uint16_t)k.bank);
    return b;
}
inline CamState add_shake(const CamState& s, const Shake& k) {
    if (!k.on) return s;
    CamState b = s;
    for (int i = 0; i < 3; i++) {
        b.eye[i] += k.eye[i];
        b.center[i] += k.center[i];
    }
    b.fovy += k.fovy;
    b.bank = (int16_t)(uint16_t)((uint16_t)b.bank + (uint16_t)k.bank);
    return b;
}
// a and b drawn with shakes ka and kb: the blended base camera with b's shake (a step drawn
// exactly by blend(), a cut, comes back as b itself)
inline CamState blend_shaken(const CamState& a, const Shake& ka, const CamState& b, const Shake& kb, float t, bool dist_rule) {
    if (!ka.on && !kb.on) return blend(a, b, t, dist_rule);
    return add_shake(blend(unshake(a, ka), unshake(b, kb), t, dist_rule), kb);
}

// The history of each camera process (interp.cpp Prev: the last exact step the blended frames start
// from, the step's snap decision, the camera's recent motion) lives in a small table of slots. A
// stage can have more than one camera process drawn on every pass (the Boating Course's stage has
// two), and camera processes are deleted with their stage without the table hearing of it. The table
// used to give a camera without a slot the first slot that had not recorded a step yet, else always
// slot 0: once four cameras had been seen (a few stage changes), the stage's two cameras took slot 0
// from each other on every draw, so each draw found no history and drew its camera exactly, at 30 Hz,
// while Link and the boat blended (issue #299). A camera without a slot now takes the slot used
// longest ago (an unused one first); a camera drawn on every pass keeps its slot. `Slot` needs `cam`
// and `used` (0 = unused), and value-initialises to an unused slot.
template <class Slot, size_t N>
Slot& history_slot(Slot (&slots)[N], uint32_t cam, uint64_t& clock) {
    Slot* pick = nullptr;
    for (auto& s : slots)
        if (s.cam == cam) { pick = &s; break; }
    if (!pick) {
        for (auto& s : slots)
            if (!pick || s.used < pick->used) pick = &s;
        *pick = Slot{};
        pick->cam = cam;
    }
    pick->used = ++clock;
    return *pick;
}

// Letterbox bars (dCamera_c mTrimHeight, the scissor camera_draw sets): CalcTrimSize eases the
// height 25 % of the way to its target per step (events, talk), so on blended frames it is drawn
// between the two steps' heights. Two sizes set it at once (and are drawn at once): 4 switches the
// bars off (0), 3 sets the cinema-scope height outside events. The camera's own cuts are left to
// the caller (not blended on a camera snap).
inline bool trim_blends(int32_t size, float prev, float cur, float cinema) {
    if (prev == cur) return false;
    if (size == 4 && cur == 0.0f) return false;
    if (size == 3 && cur == cinema) return false;
    return true;
}

// Sky sprites that the environment's logic places at a fixed offset from the camera's eye (the sun
// and moon: dKyr_sun_move puts them 8000 units from the eye along their direction; the lens flare's
// elements) are painted with the pass's camera. On a blended pass that eye is not the one the logic
// used, so they sat a varying part of a step's camera motion off their place (a 30 Hz judder while
// sailing, issue #320). Placed around the drawn eye, each keeps its offset from the eye it is seen
// from; on exact passes nothing moves.
inline void sky_follow_eye(float* pos, const float* drawn_eye, const float* exact_eye) {
    for (int i = 0; i < 3; i++) pos[i] += drawn_eye[i] - exact_eye[i];
}
// The offset itself (the logic recomputes it once per step: the sun's direction and the eye the logic
// saw) is drawn between the step drawn last (its record pass, `before`) and the new one at the pass's
// fraction t, so it does not move once per step either. A change of `cut` units or more (a new place:
// scene change, time jump) is drawn as it is.
inline void sky_offset_blend(float* offset, const float* before, float t, float cut = 500.0f) {
    float d2 = 0;
    for (int i = 0; i < 3; i++) d2 += (offset[i] - before[i]) * (offset[i] - before[i]);
    if (!(d2 < cut * cut)) return;
    for (int i = 0; i < 3; i++) offset[i] = before[i] + (offset[i] - before[i]) * t;
}

}  // namespace interp::camera
