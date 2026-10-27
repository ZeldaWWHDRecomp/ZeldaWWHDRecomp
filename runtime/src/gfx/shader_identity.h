// SPDX-License-Identifier: MPL-2.0
// Identify special rendering passes by their program, wherever the game loads it.
#pragma once
#include "area_sample.h"
#include "target_size.h"

namespace gfx {
// BloomChain: the bloom extract (PS 44F91200) and the chain's blur (PS 44FD6E00, 44FD7600); the
// targets they draw keep the console's size at any internal resolution (issue #307).
// BloomHelper: the copy passes that also draw the chain's levels (PS 44FD7E00, and 44FD8500, which
// draws the screen buffers too); they keep the chain's claim but never make one.
enum class ProgramKind { Other, DepthDownsample, OcclusionPixel, OcclusionVertex, SunShadowMask, BloomChain, BloomHelper };
inline ProgramKind program_kind(const void* bytes, uint32_t size, bool vertex) {
    if (!bytes) return ProgramKind::Other;
    if (vertex && size == 384 && area_sample::program_hash(bytes, size) == 0xd279c909832a6f45ull)
        return ProgramKind::OcclusionVertex;
    if (!vertex && size == 416) {
        const uint64_t hash = area_sample::program_hash(bytes, size);
        if (hash == 0xf49059587d7d9669ull) return ProgramKind::DepthDownsample;
        if (hash == 0x686be3682830bd88ull || hash == 0x960d3ef6661fc3c7ull) return ProgramKind::BloomHelper;
    }
    if (!vertex && size == 1584 && area_sample::program_hash(bytes, size) == 0xc2cae30e906b6255ull)
        return ProgramKind::OcclusionPixel;
    if (!vertex && size == 2112 && area_sample::program_hash(bytes, size) == 0x62ae9ab4cb625546ull)
        return ProgramKind::SunShadowMask;
    if (!vertex && size == 656 && area_sample::program_hash(bytes, size) == 0x3a8d0f3808f1501bull)
        return ProgramKind::BloomChain;
    if (!vertex && size == 768) {
        const uint64_t hash = area_sample::program_hash(bytes, size);
        if (hash == 0x4657565560d951b7ull || hash == 0x4102408f288aaa94ull) return ProgramKind::BloomChain;
    }
    return ProgramKind::Other;
}
// what a pass's draws ask of their render targets' size (target_size.h)
inline SizeRequest size_request(ProgramKind kind) {
    SizeRequest r;
    if (kind == ProgramKind::BloomChain) r.claim = TargetSize::Console;
    if (kind == ProgramKind::BloomHelper) r.renews = TargetSize::Console;
    return r;
}
} // namespace gfx
