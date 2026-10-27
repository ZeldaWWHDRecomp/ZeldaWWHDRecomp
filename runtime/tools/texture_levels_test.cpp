// Guest addresses and swizzles of sampled texture levels (runtime/src/gfx/texture_levels.h, issue #297).
// Synthetic descriptors only; no game files or GPU needed.
#include "gfx/texture_levels.h"

#include <cassert>
#include <cstdio>

namespace tl = gfx::texture_levels;

static LatteAddrLib::AddrSurfaceInfo_OUT info(uint32_t format, uint32_t w, uint32_t h, uint32_t tileMode, uint32_t level) {
    LatteAddrLib::AddrSurfaceInfo_OUT out{};
    LatteAddrLib::GX2CalculateSurfaceInfo((Latte::E_GX2SURFFMT)format, w, h, 1, Latte::E_DIM::DIM_2D,
                                          Latte::MakeGX2TileMode((Latte::E_HWTILEMODE)tileMode), 0, level, &out);
    return out;
}

int main() {
    constexpr uint32_t kBC5 = 0x35, kBC1 = 0x31, kTiled2DThin1 = 4, kDim2D = 1;
    // the issue's texture: 512x512 BC5, 10 levels, 2D tiled, bank/pipe swizzle 0x100 in both pointers
    const uint32_t base = 0x10000000, mipRaw = 0x10040100, swz = 0x100;
    tl::Level l0 = tl::level(base, mipRaw, kBC5, 512, 512, 1, kDim2D, kTiled2DThin1, swz, 0);
    assert(l0.address == base && l0.swizzle == swz);
    tl::Level l1 = tl::level(base, mipRaw, kBC5, 512, 512, 1, kDim2D, kTiled2DThin1, swz, 1);
    // level 1 starts at the mip pointer without its swizzle bits (before: read 0x100 bytes late)
    assert(l1.address == 0x10040000);
    assert(l1.swizzle == 0x100);
    assert(Latte::TM_IsMacroTiled((Latte::E_HWTILEMODE)info(kBC5, 512, 512, kTiled2DThin1, 1).hwTileMode));
    // level 2 follows level 1 (aligned), its address free of swizzle bits, tiled with the mip swizzle
    auto i1 = info(kBC5, 512, 512, kTiled2DThin1, 1), i2 = info(kBC5, 512, 512, kTiled2DThin1, 2);
    tl::Level l2 = tl::level(base, mipRaw, kBC5, 512, 512, 1, kDim2D, kTiled2DThin1, swz, 2);
    uint32_t expect2 = l1.address + (uint32_t)i1.surfSize;
    expect2 = (expect2 + i2.baseAlign - 1) / i2.baseAlign * i2.baseAlign;
    assert(l2.address == expect2);
    assert((l2.address & 0x700) == 0);
    assert(l2.swizzle == 0x100);
    // every level of the chain lies after level 1 and in ascending order
    uint32_t prev = l1.address;
    for (uint32_t level = 2; level < 10; level++) {
        tl::Level l = tl::level(base, mipRaw, kBC5, 512, 512, 1, kDim2D, kTiled2DThin1, swz, level);
        assert(l.address > prev);
        prev = l.address;
        bool macro = Latte::TM_IsMacroTiled((Latte::E_HWTILEMODE)info(kBC5, 512, 512, kTiled2DThin1, level).hwTileMode);
        assert(l.swizzle == (macro ? 0x100u : 0u));
    }
    // the mip chain's swizzle is the mip pointer's, which may differ from the base's
    tl::Level other = tl::level(base, 0x10040600, kBC5, 512, 512, 1, kDim2D, kTiled2DThin1, swz, 1);
    assert(other.address == 0x10040000 && other.swizzle == 0x600);
    // no swizzle: unchanged (level 1 at the mip pointer)
    tl::Level plain = tl::level(base, 0x10040000, kBC5, 512, 512, 1, kDim2D, kTiled2DThin1, 0, 1);
    assert(plain.address == 0x10040000 && plain.swizzle == 0);
    // linear textures: the pointers are plain addresses
    tl::Level linear = tl::level(base, 0x10040000, kBC1, 64, 64, 1, kDim2D, 1, 0, 1);
    assert(linear.address == 0x10040000 && linear.swizzle == 0);
    // a small texture whose level 1 is no longer macro tiled has no swizzle there
    auto s1 = info(kBC1, 32, 32, kTiled2DThin1, 1);
    if (!Latte::TM_IsMacroTiled((Latte::E_HWTILEMODE)s1.hwTileMode)) {
        tl::Level small = tl::level(base, 0x10001000, kBC1, 32, 32, 1, kDim2D, kTiled2DThin1, 0x200, 1);
        assert(small.swizzle == 0);
    }
    puts("texture_levels: ok");
    return 0;
}
