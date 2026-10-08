#pragma once
// Rendered mip-chain reconstruction, based on GreenNaugahyde/ZeldaWWHDRecompAndroid
// commit 73b54e1bb00ae24fc242e50dc4220235d4f34fd5 (game_level / with_mip_chain).
#include <algorithm>
#include <cstdint>
#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.h"

namespace gfx::render_mips {
template<class Desc, class Surfaces>
auto game_level(const Desc& d, uint32_t level, Surfaces& surfaces) -> typename Surfaces::mapped_type::element_type* {
    uint32_t address = d.mipAddr;
    if (!address || !level) return nullptr;
    if (level > 1) {
        uint32_t size = 0;
        sint32 sub = 0;
        LatteAddrLib::CalculateMipAndSliceAddr(d.addr, d.mipAddr, (Latte::E_GX2SURFFMT)d.format,
            d.width, d.height, d.slices, (Latte::E_DIM)d.dim, (Latte::E_HWTILEMODE)d.tileMode,
            d.swizzle, 0, level, 0, &address, &size, &sub);
    }
    if (Latte::TM_IsMacroTiled((Latte::E_HWTILEMODE)d.tileMode)) address &= ~0x700u;
    typename Surfaces::mapped_type::element_type* best = nullptr;
    auto range = surfaces.equal_range(address);
    for (auto it = range.first; it != range.second; ++it) {
        auto* s = it->second.get();
        if (!s->gpuWritten || s->isDepth || s->mips != 1 || s->slices != 1 || s->dim != d.dim ||
            s->width != std::max(d.width >> level, 1u) || s->height != std::max(d.height >> level, 1u) ||
            s->format != d.format) continue;
        if (!best || s->writeSeq > best->writeSeq) best = s;
    }
    return best;
}
} // namespace gfx::render_mips
