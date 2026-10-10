// GX2 surface format mapping. Texel layouts follow Cemu's LatteTextureLoader decoders.
#include "formats.h"
#include <cstring>

namespace gfxvk {

static FormatInfo make(VkFormat p, uint32_t bpb, FormatInfo::Kind kind = FormatInfo::FLOAT, Convert cv = Convert::NONE,
                       uint32_t hostBpb = 0) {
    FormatInfo f;
    f.pixel = p;
    f.bytesPerBlock = bpb;
    f.hostBytesPerBlock = hostBpb ? hostBpb : bpb;
    f.convert = cv;
    f.kind = kind;
    return f;
}

FormatInfo format_info(uint32_t fmt, bool isDepth) {
    const uint32_t hw = fmt & 0x3F;
    const bool isInt = fmt & 0x100, isSigned = fmt & 0x200, isSrgb = fmt & 0x400;
    const auto kind = isInt ? (isSigned ? FormatInfo::SINT : FormatInfo::UINT) : FormatInfo::FLOAT;
    auto pick = [&](VkFormat unorm, VkFormat snorm, VkFormat uint, VkFormat sint) {
        if (isInt) return isSigned ? sint : uint;
        return isSigned ? snorm : unorm;
    };
    if (isDepth) {
        FormatInfo f;
        f.depth = true;
        switch (hw) {
        case 0x05: f.pixel = VK_FORMAT_D16_UNORM; f.bytesPerBlock = f.hostBytesPerBlock = 2; return f;
        case 0x0E: f.pixel = VK_FORMAT_D32_SFLOAT; f.bytesPerBlock = f.hostBytesPerBlock = 4; return f;
        case 0x11: case 0x12: case 0x13: case 0x14:
            f.pixel = VK_FORMAT_D32_SFLOAT_S8_UINT; f.stencil = true; f.bytesPerBlock = 4; f.hostBytesPerBlock = 8;
            f.convert = Convert::D24S8; return f;
        case 0x1C:
            f.pixel = VK_FORMAT_D32_SFLOAT_S8_UINT; f.stencil = true; f.bytesPerBlock = 8; f.hostBytesPerBlock = 8;
            f.convert = Convert::X24_8_32F; return f;
        default: return {};
        }
    }
    // Packed normalized expansions have no integer/signed equivalent in this mapping.
    if ((isInt || isSigned) && (hw == 0x02 || hw == 0x08 || hw == 0x0A || hw == 0x0B || hw == 0x0C || hw == 0x1B)) return {};
    if (isSigned && hw == 0x19) return {};
    FormatInfo f;
    switch (hw) {
    case 0x01: f = make(pick(VK_FORMAT_R8_UNORM, VK_FORMAT_R8_SNORM, VK_FORMAT_R8_UINT, VK_FORMAT_R8_SINT), 1, kind); break;
    case 0x02: f = make(VK_FORMAT_R8G8_UNORM, 1, kind, Convert::RG4, 2); break;
    case 0x05: f = make(pick(VK_FORMAT_R16_UNORM, VK_FORMAT_R16_SNORM, VK_FORMAT_R16_UINT, VK_FORMAT_R16_SINT), 2, kind); break;
    case 0x06: f = make(VK_FORMAT_R16_SFLOAT, 2); break;
    case 0x07: f = make(pick(VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8_SNORM, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8_SINT), 2, kind); break;
    case 0x08: f = make(VK_FORMAT_R8G8B8A8_UNORM, 2, kind, Convert::RGB565, 4); break;
    case 0x0A: f = make(VK_FORMAT_R8G8B8A8_UNORM, 2, kind, Convert::RGBA5551, 4); break;
    case 0x0B: f = make(VK_FORMAT_R8G8B8A8_UNORM, 2, kind, Convert::RGBA4, 4); break;
    case 0x0C: f = make(VK_FORMAT_R8G8B8A8_UNORM, 2, kind, Convert::ABGR1555, 4); break;
    case 0x0D: f = make(isSigned ? VK_FORMAT_R32_SINT : VK_FORMAT_R32_UINT, 4, isSigned ? FormatInfo::SINT : FormatInfo::UINT); break;
    case 0x0E: f = make(VK_FORMAT_R32_SFLOAT, 4); break;
    case 0x0F: f = make(pick(VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16_SINT), 4, kind); break;
    case 0x10: f = make(VK_FORMAT_R16G16_SFLOAT, 4); break;
    case 0x16: case 0x15: f = make(VK_FORMAT_B10G11R11_UFLOAT_PACK32, 4); break;
    case 0x19: f = make(isInt ? VK_FORMAT_A2B10G10R10_UINT_PACK32 : VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4, kind); break;
    case 0x1A:
        f = make(isSrgb ? VK_FORMAT_R8G8B8A8_SRGB
                        : pick(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SNORM, VK_FORMAT_R8G8B8A8_UINT, VK_FORMAT_R8G8B8A8_SINT),
                 4, kind);
        break;
    case 0x1B: f = make(VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4, kind); break;
    case 0x1D: f = make(isSigned ? VK_FORMAT_R32G32_SINT : VK_FORMAT_R32G32_UINT, 8, isSigned ? FormatInfo::SINT : FormatInfo::UINT); break;
    case 0x1E: f = make(VK_FORMAT_R32G32_SFLOAT, 8); break;
    case 0x1F: f = make(pick(VK_FORMAT_R16G16B16A16_UNORM, VK_FORMAT_R16G16B16A16_SNORM, VK_FORMAT_R16G16B16A16_UINT, VK_FORMAT_R16G16B16A16_SINT), 8, kind); break;
    case 0x20: f = make(VK_FORMAT_R16G16B16A16_SFLOAT, 8); break;
    case 0x22: f = make(isSigned ? VK_FORMAT_R32G32B32A32_SINT : VK_FORMAT_R32G32B32A32_UINT, 16, isSigned ? FormatInfo::SINT : FormatInfo::UINT); break;
    case 0x23: f = make(VK_FORMAT_R32G32B32A32_SFLOAT, 16); break;
    case 0x31: f = make(isSrgb ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 8); f.compressed = true; break;
    case 0x32: f = make(isSrgb ? VK_FORMAT_BC2_SRGB_BLOCK : VK_FORMAT_BC2_UNORM_BLOCK, 16); f.compressed = true; break;
    case 0x33: f = make(isSrgb ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK, 16); f.compressed = true; break;
    case 0x34: f = make(isSigned ? VK_FORMAT_BC4_SNORM_BLOCK : VK_FORMAT_BC4_UNORM_BLOCK, 8); f.compressed = true; break;
    case 0x35: f = make(isSigned ? VK_FORMAT_BC5_SNORM_BLOCK : VK_FORMAT_BC5_UNORM_BLOCK, 16); f.compressed = true; break;
    // depth formats sampled as color textures
    case 0x11: case 0x12: case 0x13: case 0x14: f = make(VK_FORMAT_R32_SFLOAT, 4, FormatInfo::FLOAT, Convert::D24_R32F, 4); break;
    default: return {};
    }
    return f;
}

FormatInfo comparison_format_info(uint32_t fmt) {
    auto f = format_info(fmt, true);
    if (f.pixel != VK_FORMAT_UNDEFINED) return f;
    if (fmt == 0x1A) { // RGBA8 UNORM; comparison reads the normalized red component
        f = make(VK_FORMAT_D32_SFLOAT, 4, FormatInfo::FLOAT, Convert::RGBA8_DEPTH);
        f.depth = true;
    }
    return f;
}

static inline uint8_t ex5(uint32_t v) { return (uint8_t)((v << 3) | (v >> 2)); }
static inline uint8_t ex6(uint32_t v) { return (uint8_t)((v << 2) | (v >> 4)); }
static inline uint8_t ex4(uint32_t v) { return (uint8_t)((v << 4) | v); }

void convert_row(Convert c, const uint8_t* src, uint8_t* dst, uint32_t n) {
    switch (c) {
    case Convert::NONE: break;
    case Convert::RGBA8_DEPTH:
        for (uint32_t i = 0; i < n; ++i) {
            float depth = float(src[4 * i]) / 255.0f;
            memcpy(dst + 4 * i, &depth, sizeof depth);
        }
        break;
    case Convert::RGB565:
        for (uint32_t i = 0; i < n; i++) {
            uint16_t v; memcpy(&v, src + 2 * i, sizeof(v));
            dst[4 * i + 0] = ex5(v & 0x1F);
            dst[4 * i + 1] = ex6((v >> 5) & 0x3F);
            dst[4 * i + 2] = ex5((v >> 11) & 0x1F);
            dst[4 * i + 3] = 255;
        }
        break;
    case Convert::RGBA5551:
        for (uint32_t i = 0; i < n; i++) {
            uint16_t v; memcpy(&v, src + 2 * i, sizeof(v));
            dst[4 * i + 0] = ex5(v & 0x1F);
            dst[4 * i + 1] = ex5((v >> 5) & 0x1F);
            dst[4 * i + 2] = ex5((v >> 10) & 0x1F);
            dst[4 * i + 3] = (v >> 15) ? 255 : 0;
        }
        break;
    case Convert::ABGR1555:
        for (uint32_t i = 0; i < n; i++) {
            uint16_t v; memcpy(&v, src + 2 * i, sizeof(v));
            dst[4 * i + 0] = ex5((v >> 11) & 0x1F);
            dst[4 * i + 1] = ex5((v >> 6) & 0x1F);
            dst[4 * i + 2] = ex5((v >> 1) & 0x1F);
            dst[4 * i + 3] = (v & 1) ? 255 : 0;
        }
        break;
    case Convert::RGBA4:
        for (uint32_t i = 0; i < n; i++) {
            uint16_t v; memcpy(&v, src + 2 * i, sizeof(v));
            dst[4 * i + 0] = ex4(v & 0xF);
            dst[4 * i + 1] = ex4((v >> 4) & 0xF);
            dst[4 * i + 2] = ex4((v >> 8) & 0xF);
            dst[4 * i + 3] = ex4((v >> 12) & 0xF);
        }
        break;
    case Convert::RG4:
        for (uint32_t i = 0; i < n; i++) {
            uint8_t v = src[i];
            dst[2 * i + 0] = ex4(v >> 4);
            dst[2 * i + 1] = ex4(v & 0xF);
        }
        break;
    case Convert::D24S8:
        for (uint32_t i = 0; i < n; i++) {
            uint32_t v; memcpy(&v, src + 4 * i, sizeof(v));
            float d = (float)(v & 0xFFFFFF) / 16777215.0f;
            memcpy(dst + 8 * i, &d, 4);
            dst[8 * i + 4] = (uint8_t)(v >> 24);
            dst[8 * i + 5] = dst[8 * i + 6] = dst[8 * i + 7] = 0;
        }
        break;
    case Convert::D24_R32F:
        for (uint32_t i = 0; i < n; i++) {
            uint32_t v; memcpy(&v, src + 4 * i, sizeof(v));
            float d = (float)(v & 0xFFFFFF) / 16777215.0f;
            memcpy(dst + 4 * i, &d, 4);
        }
        break;
    case Convert::X24_8_32F:
        memcpy(dst, src, n * 8);
        break;
    }
}

}  // namespace gfxvk
