// The hookshot chain's link count on blended passes (interp_fx.cpp, issue #304).
//
// daHookshot_shape_draw (021768B0) fills the matrices of the first `count` chain links of the shape
// (+0x980, 0xA8 apart); the shape's GX2 draw (021783F4), when the pass's lists are painted, draws as
// many links as the hookshot's count (+0xD504) says then. On a blended pass the matrices are filled
// for the blended count; the GX2 draw must draw that many too. With the step's exact count instead,
// links beyond the blended count were drawn from matrices left over from the last pass that filled
// them, often the previous use of the hookshot: while the chain flew out, each new link showed up
// where it was last seen.
//
// The chain's direction (the hookshot's shape angle) only means something on a step with a chain.
// On the step a shot starts, the step before had none (the angle there is 0, 0 or what the last shot
// left), so blending from it turned the new chain through that direction on the in-between frames
// (the first shot's direction: the chain appeared where the last one had been). The angle is
// blended only between two steps that both have a chain; otherwise the step with a chain gives it.
#pragma once
#include <cstdint>

#include "interp_pacing.h"

namespace interp {
struct ChainCount {
    uint32_t shape = 0;
    uint64_t pass = ~0ull;
    int32_t count = 0;
    // the shape draw filled `count` links of `shape` on `pass`
    void filled(uint32_t s, uint64_t p, int32_t n) { shape = s, pass = p, count = n; }
    // the number of links the GX2 draw of `shape` on `pass` draws: the filled count of the same
    // pass, else the game's own (exact) count
    int32_t drawn(uint32_t s, uint64_t p, int32_t exact) const { return s == shape && p == pass ? count : exact; }
};
// the shape angle (x or y) a blended pass draws the chain with, at fraction t from the step before
// (count_before links, angle before) to this one
inline int16_t chain_angle(int32_t count_before, int16_t before, int32_t count, int16_t angle, float t) {
    if (count_before > 0 && count > 0) return pacing::lerp_s16(before, angle, t);
    return count > 0 ? angle : before;
}
}  // namespace interp
