// GX2 surface formats -> Vulkan formats, and texel conversion for
// formats that need a host texel conversion.
#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>

namespace gfxvk {

enum class Convert : uint8_t {
    NONE,        // copy texels as stored
    RGB565,      // Latte 5_6_5 -> RGBA8
    RGBA5551,    // Latte 1_5_5_5 (R in low bits) -> RGBA8
    ABGR1555,    // Latte 5_5_5_1 -> RGBA8
    RGBA4,       // Latte 4_4_4_4 -> RGBA8
    RG4,         // Latte 4_4 -> RG8
    RGBA8_DEPTH, // normalized colour red channel -> D32F for comparison sampling
    D24S8,       // 24-bit depth + 8-bit stencil -> D32_SFLOAT_S8_UINT
    D24_R32F,    // 24-bit depth sampled as a color texture -> R32_SFLOAT
    X24_8_32F,   // 32F depth + 8 stencil in 64 bits -> D32_SFLOAT_S8_UINT
};

struct FormatInfo {
    VkFormat pixel = VK_FORMAT_UNDEFINED;
    uint32_t bytesPerBlock = 0;  // guest (source) bytes per texel or per 4x4 block
    uint32_t hostBytesPerBlock = 0;
    bool compressed = false;     // 4x4 blocks
    bool depth = false;
    bool stencil = false;
    Convert convert = Convert::NONE;
    enum Kind : uint8_t { FLOAT, UINT, SINT } kind = FLOAT;  // shader-visible data type
};

// isDepth: the surface is used as a depth buffer (selects depth pixel formats)
FormatInfo format_info(uint32_t gx2Format, bool isDepth);

// Comparison sampling can read a colour texture (for example an unrendered shadow-map placeholder).
// Keep its colour guest layout, but give Vulkan a depth image for its comparison sampler.
FormatInfo comparison_format_info(uint32_t gx2Format);

// convert one row of `count` texels (or blocks) from guest layout to host layout
void convert_row(Convert c, const uint8_t* src, uint8_t* dst, uint32_t count);

}  // namespace gfxvk
