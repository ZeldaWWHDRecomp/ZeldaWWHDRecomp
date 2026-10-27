#pragma once
// Where each level of a sampled texture lives in guest memory, shared by both renderers.
//
// The texture resource holds two pointers: BASE_ADDRESS (word 2, level 0) and MIP_ADDRESS (word 3,
// levels 1 and up). For macro-tiled (2D/3D tiled) surfaces GX2 puts the bank/pipe swizzle into bits
// 8-10 of both pointers (GX2SetPixelTexture, gx2_resources.cpp set_texture: the mip pointer gets them
// while level 1 is still macro tiled), so those bits are the swizzle, not an offset. The base
// pointer's bits are the swizzle of level 0, the mip pointer's bits that of the mip chain. Level 1 does
// not start at MIP_ADDRESS << 8 but at that address without the swizzle bits (as
// LatteAddrLib::CalculateMipAndSliceAddr, i.e. Cemu, computes every level from the raw mip pointer).
// Reading level 1 at the raw pointer shifts it by up to 0x700 bytes within the macro tiles: blocky,
// scrambled texels on every mipmapped, swizzled texture once the sampler reaches level 1 (issue #297,
// the glow under the Triforce chests: a 512x512 BC5 texture with 10 levels, swizzle 0x100).
#include <cstdint>
#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.h"

namespace gfx::texture_levels {
struct Level {
    uint32_t address = 0;  // first byte of the level (slice 0)
    uint32_t swizzle = 0;  // bank/pipe swizzle (bits 8-10) used by the level's macro tiling
};

// addr: BASE_ADDRESS << 8 with the swizzle bits already removed (baseSwizzle holds them);
// mipAddr: MIP_ADDRESS << 8 as the resource holds it (swizzle bits included).
inline Level level(uint32_t addr, uint32_t mipAddr, uint32_t format, uint32_t width, uint32_t height, uint32_t slices,
                   uint32_t dim, uint32_t tileMode, uint32_t baseSwizzle, uint32_t level) {
    if (level == 0) return {addr, baseSwizzle};
    uint32_t address = 0, size = 0;
    sint32 sub = 0;
    LatteAddrLib::CalculateMipAndSliceAddr(addr, mipAddr, (Latte::E_GX2SURFFMT)format, width, height, slices,
                                           (Latte::E_DIM)dim, (Latte::E_HWTILEMODE)tileMode, baseSwizzle, 0, level, 0,
                                           &address, &size, &sub);
    // the mip chain's swizzle comes from the mip pointer; it only matters while the level is macro tiled
    // (CalculateMipAndSliceAddr takes it from there once level 1 is macro tiled)
    LatteAddrLib::AddrSurfaceInfo_OUT info{};
    LatteAddrLib::GX2CalculateSurfaceInfo((Latte::E_GX2SURFFMT)format, width, height, slices, (Latte::E_DIM)dim,
                                          Latte::MakeGX2TileMode((Latte::E_HWTILEMODE)tileMode), 0, level, &info);
    uint32_t swizzle = Latte::TM_IsMacroTiled((Latte::E_HWTILEMODE)info.hwTileMode) ? (mipAddr & 0x700) : 0;
    return {address, swizzle};
}
}  // namespace gfx::texture_levels
