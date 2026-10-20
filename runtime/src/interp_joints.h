// Frame interpolation of a model's joint (world) matrices, joint by joint (interp.cpp, J3DModel
// viewCalc hook), with joints whose recorded matrix is stale drawn at the current step.
//
// Some models keep joints that draw nothing most of the time and are only switched on now and then.
// Link's hands model (daPy_lk_c::mpHandsModel) has one joint per hand pose (open hand, fist, sword
// grip, ...): daPy_lk_c::setDrawHandModel (023D8624) shows the shape of the pose in use and copies
// Link's hand matrix into that joint only. The other joints keep the matrix they had when their pose
// was last used. When a pose comes back (standing to running, sheathing, untargeting), its recorded
// "before" matrix is where the hand was back then, and the in-between frames drew the fist flying
// from there to Link's hand (issue #255).
// A joint's recorded matrix is stale when, on the record pass, the joint drew nothing (it has shapes
// of its own and all of them were hidden) and its matrix had not been written since the record pass
// before. Such a joint is drawn at the current step on the next step's blended frames instead of
// blended from the stale matrix. Joints that are drawn, joints without shapes of their own (bones
// that only move skinned shapes), and hidden joints that are still being animated keep blending.
#pragma once
#include <cstdint>
#include <cstring>

namespace interp::joints {

constexpr uint32_t kWords = 12;  // 3x4 matrix, guest (big-endian) words

// record pass: is the joint's matrix `rec` stale? `hidden`: the joint has shapes of its own and all
// are hidden; `before`: its matrix as recorded on the previous record pass (nullptr: none recorded)
inline bool stale(bool hidden, const uint32_t* rec, const uint32_t* before) {
    return hidden && (!before || std::memcmp(rec, before, 4 * kWords) == 0);
}

inline float word_f(uint32_t w) {
    w = __builtin_bswap32(w);
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}
inline uint32_t f_word(float f) {
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return __builtin_bswap32(w);
}

struct Result {
    bool cut = false;     // a drawn joint moved further than `cut_dist` (teleport, NaN): show the new step
    uint32_t shown = 0;   // stale joints drawn at the current step
};

// blended frame at `t` between the recorded step `prev` and the current one `cur` (n matrices, guest
// words) into `out`; `stale_flags` (n entries, may be nullptr) marks the joints to draw at `cur`.
// `blend(a, b, out, t)` blends two 3x4 float matrices. When the result is a cut, `out` is incomplete.
template <class Blend>
Result blend(const uint32_t* prev, const uint32_t* cur, const uint8_t* stale_flags, uint32_t n, float cut_dist, float t,
             uint32_t* out, Blend&& blend_mtx) {
    Result r;
    for (uint32_t m = 0; m < n; m++) {
        const uint32_t* pa = prev + kWords * m;
        const uint32_t* pb = cur + kWords * m;
        uint32_t* po = out + kWords * m;
        if (stale_flags && stale_flags[m]) {
            if (std::memcmp(pa, pb, 4 * kWords) != 0) r.shown++;
            std::memcpy(po, pb, 4 * kWords);
            continue;
        }
        float a[kWords], b[kWords], o[kWords];
        for (uint32_t k = 0; k < kWords; k++) { a[k] = word_f(pa[k]); b[k] = word_f(pb[k]); }
        const float dx = a[3] - b[3], dy = a[7] - b[7], dz = a[11] - b[11];
        if (!(dx * dx + dy * dy + dz * dz <= cut_dist * cut_dist)) {
            r.cut = true;
            return r;
        }
        blend_mtx(a, b, o, t);
        for (uint32_t k = 0; k < kWords; k++) po[k] = f_word(o[k]);
    }
    return r;
}

}  // namespace interp::joints
