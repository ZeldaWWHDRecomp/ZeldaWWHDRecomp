// GX2 surface formats -> Metal pixel formats, and texel conversion for
// formats Metal has no direct equivalent for.
#pragma once
#include <Metal/Metal.h>

#include <cstdint>

namespace gfx {

enum class Convert : uint8_t {
    NONE,        // copy texels as stored
    RGB565,      // Latte 5_6_5 -> RGBA8
    RGBA5551,    // Latte 1_5_5_5 (R in low bits) -> RGBA8
    ABGR1555,    // Latte 5_5_5_1 -> RGBA8
    RGBA4,       // Latte 4_4_4_4 -> RGBA8
    RG4,         // Latte 4_4 -> RG8
    D24S8,       // 24-bit depth + 8-bit stencil -> Depth32Float_Stencil8
    D24_R32F,    // 24-bit depth sampled as a color texture -> R32Float
    X24_8_32F,   // 32F depth + 8 stencil in 64 bits -> Depth32Float_Stencil8
};

struct FormatInfo {
    MTLPixelFormat pixel = MTLPixelFormatInvalid;
    uint32_t bytesPerBlock = 0;  // guest (source) bytes per texel or per 4x4 block
    uint32_t hostBytesPerBlock = 0;
    bool compressed = false;     // 4x4 blocks
    bool depth = false;
    bool stencil = false;
    Convert convert = Convert::NONE;
    enum Kind : uint8_t { FLOAT, UINT, SINT } kind = FLOAT;  // shader-visible data type
    // A BC format on a GPU without BC textures (an Apple TV's A15): the texture is RGBA8 (BC4 R8, BC5 RG8) and the upload decodes
    // the blocks (gfxvk::bc::decode; bcMode is its mode: BC type 1..5, +256 signed). Everything else still sees
    // 4x4 blocks of bytesPerBlock (compressed): detiling and copies are unchanged.
    bool decodeBC = false;
    uint16_t bcMode = 0;
    // with etcpak built in (WWHD_ETCPAK: the Apple TV), BC1-3 are re-encoded to EAC_RGBA8 (ETC2 colour and EAC
    // alpha, 16 bytes a block) instead of RGBA8: a quarter of the memory. hostBytesPerBlock stays the guest's
    // (the detiled blocks are BC); the upload knows the ETC2 size.
    bool etc2 = false;
};
// the GPU samples BC textures itself (otherwise format_info decodes them, decodeBC)
bool bc_textures_supported();

// isDepth: the surface is used as a depth buffer (selects depth pixel formats)
FormatInfo format_info(uint32_t gx2Format, bool isDepth);

// convert one row of `count` texels (or blocks) from guest layout to host layout
void convert_row(Convert c, const uint8_t* src, uint8_t* dst, uint32_t count);

}  // namespace gfx
