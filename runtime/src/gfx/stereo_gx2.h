#pragma once
// Stereo 3D (stereo.h): whether a guest clear or copy writes a target both eyes share (eye_shared_target), from
// its GX2 structures; the renderers then run it for eye 0 alone (their stereo_pair).
#include <algorithm>
#include <cstdint>

#include "gx2_texture_regs.h"
#include "runtime.h"
#include "stereo.h"

namespace stereo {
inline bool shared_color_buffer(uint32_t gx2ColorBuffer) {
    auto* cb = (GX2::GX2ColorBuffer*)mem::ptr(gx2ColorBuffer);
    const uint32_t layers = cb->surface.dim.value() == Latte::E_DIM::DIM_2D_ARRAY ? std::max<uint32_t>(cb->surface.depth, 1) : 1;
    return eye_shared_target(std::max<uint32_t>(cb->surface.width >> cb->viewMip, 1), std::max<uint32_t>(cb->surface.height >> cb->viewMip, 1),
                             layers, false);
}
inline bool shared_depth_buffer(uint32_t gx2DepthBuffer) {
    auto* db = (GX2::GX2DepthBuffer*)mem::ptr(gx2DepthBuffer);
    const uint32_t layers = db->surface.dim.value() == Latte::E_DIM::DIM_2D_ARRAY ? std::max<uint32_t>(db->surface.depth, 1) : 1;
    return eye_shared_target(db->surface.width, db->surface.height, layers, true);
}
inline bool shared_copy_destination(uint32_t gx2Surface, uint32_t mip) {
    auto* d = (GX2Surface*)mem::ptr(gx2Surface);
    return eye_shared_target(std::max<uint32_t>(d->width >> mip, 1), std::max<uint32_t>(d->height >> mip, 1), 1, false);
}
}  // namespace stereo
