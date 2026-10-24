// Frame interpolation of swing trails (sword, boomerang) without game state (interp_fx.cpp;
// runtime/tools/interp_trail_test.cpp, ctest interp_trail).
//
// A trail is a history of points in world space, newest first: one or more lanes of up to 60
// points (the sword: blade tips and blade bases; the boomerang: two rings of tips and roots) and a
// count of drawn segments (segment i joins points i and i+1). Each logic step the game's logic
// (copySwBlur 02407310, copyBlur 020CE7E8) shifts the history back by the step's new samples
// (k: the sword's animation sub-frames, 5 for the boomerang) and writes them at the front; the
// count grows by k (at most 58), or shrinks by 10 a step once the swing is over. The draw (a
// J3DPacket, painted after the pass) reads the step's exact history on every drawn frame, while the
// blade itself is drawn blended: at 120 fps the trail's head ran ahead of the blade and the whole
// trail moved once every 4th frame.
// On a blended frame at t the trail is drawn as it was at step S+t: its head is (1-t) * k samples
// back in the new history (between two samples: blended), and its length blends between the two
// steps' counts, so the oldest point stays where it is while the head follows the blade. The new
// samples k come from matching the previous step's newest points in the new history; when they
// cannot be found (a new swing: the history was reset) the step is drawn as it is.
#pragma once
#include <cmath>
#include <cstdint>

#include "interp_pacing.h"

namespace interp::trail {

constexpr int kPoints = 60;
constexpr int kMaxLanes = 4;

struct Frame {
    int lanes = 0;
    float p[kMaxLanes][kPoints][3];
    int count = 0;  // drawn segments: points 0..count+1 are used
};

// new samples between a (step S) and b (step S+1): b's points k, k+1 are a's points 0, 1 in every
// lane; -1 if not found
inline int new_samples(const Frame& a, const Frame& b) {
    if (a.lanes != b.lanes || a.lanes <= 0) return -1;
    for (int k = 0; k + 1 < kPoints; k++) {
        bool same = true;
        for (int l = 0; l < a.lanes && same; l++)
            for (int j = 0; j < 2 && same; j++)
                for (int x = 0; x < 3; x++)
                    if (a.p[l][j][x] != b.p[l][k + j][x]) { same = false; break; }
        if (same) return k;
    }
    return -1;
}

// the trail at t between a and b into out (out.lanes = b.lanes); false: draw b as it is
inline bool at(const Frame& a, const Frame& b, float t, Frame& out) {
    if (b.lanes <= 0 || b.lanes > kMaxLanes || b.count < 0 || b.count > kPoints - 2) return false;
    int k;
    if (a.count <= 0) {
        k = b.count;  // a new trail: it grows from its first point (the blade at S) to the head
    } else {
        k = new_samples(a, b);
        if (k < 0) return false;
    }
    if (k == 0 && a.count == b.count) return false;  // nothing changed
    const float head = (1.0f - t) * (float)k;           // where the head is in b's history
    float len = pacing::lerp_f((float)a.count, (float)b.count, t);
    // (growing: head + len is b.count, the oldest point stays put; shrinking after the swing: b's
    // history is a's, and the points past b's count are still there)
    if (head + len > (float)(kPoints - 2)) len = (float)(kPoints - 2) - head;
    if (len < 0) len = 0;
    out.lanes = b.lanes;
    out.count = (int)std::lround(len);
    for (int i = 0; i < kPoints; i++) {
        float s = head + (float)i;
        if (s > (float)(kPoints - 1)) s = (float)(kPoints - 1);
        const int i0 = (int)s;
        const int i1 = i0 + 1 < kPoints ? i0 + 1 : i0;
        const float f = s - (float)i0;
        for (int l = 0; l < b.lanes; l++)
            for (int x = 0; x < 3; x++)
                out.p[l][i][x] = f == 0.0f ? b.p[l][i0][x] : (1.0f - f) * b.p[l][i0][x] + f * b.p[l][i1][x];
    }
    return true;
}

}  // namespace interp::trail
