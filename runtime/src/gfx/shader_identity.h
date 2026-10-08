// SPDX-License-Identifier: MPL-2.0
// Identify special rendering passes by their program, wherever the game loads it.
#pragma once
#include "area_sample.h"

namespace gfx {
enum class ProgramKind { Other, DepthDownsample, OcclusionPixel, OcclusionVertex };
inline ProgramKind program_kind(const void* bytes, uint32_t size, bool vertex) {
    if (!bytes) return ProgramKind::Other;
    if (vertex && size == 384 && area_sample::program_hash(bytes, size) == 0xd279c909832a6f45ull)
        return ProgramKind::OcclusionVertex;
    if (!vertex && size == 416 && area_sample::program_hash(bytes, size) == 0xf49059587d7d9669ull)
        return ProgramKind::DepthDownsample;
    if (!vertex && size == 1584 && area_sample::program_hash(bytes, size) == 0xc2cae30e906b6255ull)
        return ProgramKind::OcclusionPixel;
    return ProgramKind::Other;
}
} // namespace gfx
