// GX2 surface format mapping. Texel layouts follow Cemu's LatteTextureLoader decoders.
#include "formats.h"

namespace gfx {

static FormatInfo make(MTLPixelFormat p, uint32_t bpb, FormatInfo::Kind kind = FormatInfo::FLOAT, Convert cv = Convert::NONE,
                       uint32_t hostBpb = 0) {
    FormatInfo f;
    f.pixel = p;
    f.bytesPerBlock = bpb;
    f.hostBytesPerBlock = hostBpb ? hostBpb : bpb;
    f.convert = cv;
    f.kind = kind;
    return f;
}

bool bc_textures_supported() {
    static const bool supported = [] {
        id<MTLDevice> d = MTLCreateSystemDefaultDevice();
        if (@available(macOS 11.0, iOS 16.4, tvOS 16.4, *)) return d && d.supportsBCTextureCompression;
        return true;
    }();
    return supported;
}

static FormatInfo decoded_bc(FormatInfo f, unsigned type, bool isSigned, bool isSrgb) {
    if (bc_textures_supported()) return f;
    // BC4 / BC5 hold one / two channels: R8 / RG8 keep exactly what they decode to (the rest reads 0, 0, 1 as BC)
    if (type == 4) f.pixel = isSigned ? MTLPixelFormatR8Snorm : MTLPixelFormatR8Unorm;
    else if (type == 5) f.pixel = isSigned ? MTLPixelFormatRG8Snorm : MTLPixelFormatRG8Unorm;
    else f.pixel = isSrgb ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm;
#ifdef WWHD_ETCPAK
    if (type <= 3) {
        f.pixel = isSrgb ? MTLPixelFormatEAC_RGBA8_sRGB : MTLPixelFormatEAC_RGBA8;
        f.etc2 = true;
    }
#endif
    f.decodeBC = true;
    f.bcMode = uint16_t(type | (isSigned ? 256 : 0));
    return f;
}

FormatInfo format_info(uint32_t fmt, bool isDepth) {
    const uint32_t hw = fmt & 0x3F;
    const bool isInt = fmt & 0x100, isSigned = fmt & 0x200, isSrgb = fmt & 0x400;
    const auto kind = isInt ? (isSigned ? FormatInfo::SINT : FormatInfo::UINT) : FormatInfo::FLOAT;
    auto pick = [&](MTLPixelFormat unorm, MTLPixelFormat snorm, MTLPixelFormat uint, MTLPixelFormat sint) {
        if (isInt) return isSigned ? sint : uint;
        return isSigned ? snorm : unorm;
    };
    if (isDepth) {
        FormatInfo f;
        f.depth = true;
        switch (hw) {
        case 0x05: f.pixel = MTLPixelFormatDepth16Unorm; f.bytesPerBlock = f.hostBytesPerBlock = 2; return f;
        case 0x0E: f.pixel = MTLPixelFormatDepth32Float; f.bytesPerBlock = f.hostBytesPerBlock = 4; return f;
        case 0x11: case 0x12: case 0x13: case 0x14:
            f.pixel = MTLPixelFormatDepth32Float_Stencil8; f.stencil = true; f.bytesPerBlock = 4; f.hostBytesPerBlock = 8;
            f.convert = Convert::D24S8; return f;
        case 0x1C:
            f.pixel = MTLPixelFormatDepth32Float_Stencil8; f.stencil = true; f.bytesPerBlock = 8; f.hostBytesPerBlock = 8;
            f.convert = Convert::X24_8_32F; return f;
        default: break;
        }
    }
    FormatInfo f;
    switch (hw) {
    case 0x01: f = make(pick(MTLPixelFormatR8Unorm, MTLPixelFormatR8Snorm, MTLPixelFormatR8Uint, MTLPixelFormatR8Sint), 1, kind); break;
    case 0x02: f = make(MTLPixelFormatRG8Unorm, 1, kind, Convert::RG4, 2); break;
    case 0x05: f = make(pick(MTLPixelFormatR16Unorm, MTLPixelFormatR16Snorm, MTLPixelFormatR16Uint, MTLPixelFormatR16Sint), 2, kind); break;
    case 0x06: f = make(MTLPixelFormatR16Float, 2); break;
    case 0x07: f = make(pick(MTLPixelFormatRG8Unorm, MTLPixelFormatRG8Snorm, MTLPixelFormatRG8Uint, MTLPixelFormatRG8Sint), 2, kind); break;
    case 0x08: f = make(MTLPixelFormatRGBA8Unorm, 2, kind, Convert::RGB565, 4); break;
    case 0x0A: f = make(MTLPixelFormatRGBA8Unorm, 2, kind, Convert::RGBA5551, 4); break;
    case 0x0B: f = make(MTLPixelFormatRGBA8Unorm, 2, kind, Convert::RGBA4, 4); break;
    case 0x0C: f = make(MTLPixelFormatRGBA8Unorm, 2, kind, Convert::ABGR1555, 4); break;
    case 0x0D: f = make(isSigned ? MTLPixelFormatR32Sint : MTLPixelFormatR32Uint, 4, isSigned ? FormatInfo::SINT : FormatInfo::UINT); break;
    case 0x0E: f = make(MTLPixelFormatR32Float, 4); break;
    case 0x0F: f = make(pick(MTLPixelFormatRG16Unorm, MTLPixelFormatRG16Snorm, MTLPixelFormatRG16Uint, MTLPixelFormatRG16Sint), 4, kind); break;
    case 0x10: f = make(MTLPixelFormatRG16Float, 4); break;
    case 0x16: case 0x15: f = make(MTLPixelFormatRG11B10Float, 4); break;
    case 0x19: f = make(isInt ? MTLPixelFormatRGB10A2Uint : MTLPixelFormatRGB10A2Unorm, 4, kind); break;
    case 0x1A:
        f = make(isSrgb ? MTLPixelFormatRGBA8Unorm_sRGB
                        : pick(MTLPixelFormatRGBA8Unorm, MTLPixelFormatRGBA8Snorm, MTLPixelFormatRGBA8Uint, MTLPixelFormatRGBA8Sint),
                 4, kind);
        break;
    case 0x1B: f = make(MTLPixelFormatRGB10A2Unorm, 4, kind); break;
    case 0x1D: f = make(isSigned ? MTLPixelFormatRG32Sint : MTLPixelFormatRG32Uint, 8, isSigned ? FormatInfo::SINT : FormatInfo::UINT); break;
    case 0x1E: f = make(MTLPixelFormatRG32Float, 8); break;
    case 0x1F: f = make(pick(MTLPixelFormatRGBA16Unorm, MTLPixelFormatRGBA16Snorm, MTLPixelFormatRGBA16Uint, MTLPixelFormatRGBA16Sint), 8, kind); break;
    case 0x20: f = make(MTLPixelFormatRGBA16Float, 8); break;
    case 0x22: f = make(isSigned ? MTLPixelFormatRGBA32Sint : MTLPixelFormatRGBA32Uint, 16, isSigned ? FormatInfo::SINT : FormatInfo::UINT); break;
    case 0x23: f = make(MTLPixelFormatRGBA32Float, 16); break;
    case 0x31: f = make(isSrgb ? MTLPixelFormatBC1_RGBA_sRGB : MTLPixelFormatBC1_RGBA, 8); f.compressed = true; f = decoded_bc(f, 1, false, isSrgb); break;
    case 0x32: f = make(isSrgb ? MTLPixelFormatBC2_RGBA_sRGB : MTLPixelFormatBC2_RGBA, 16); f.compressed = true; f = decoded_bc(f, 2, false, isSrgb); break;
    case 0x33: f = make(isSrgb ? MTLPixelFormatBC3_RGBA_sRGB : MTLPixelFormatBC3_RGBA, 16); f.compressed = true; f = decoded_bc(f, 3, false, isSrgb); break;
    case 0x34: f = make(isSigned ? MTLPixelFormatBC4_RSnorm : MTLPixelFormatBC4_RUnorm, 8); f.compressed = true; f = decoded_bc(f, 4, isSigned, false); break;
    case 0x35: f = make(isSigned ? MTLPixelFormatBC5_RGSnorm : MTLPixelFormatBC5_RGUnorm, 16); f.compressed = true; f = decoded_bc(f, 5, isSigned, false); break;
    // depth formats sampled as color textures
    case 0x11: case 0x12: case 0x13: case 0x14: f = make(MTLPixelFormatR32Float, 4, FormatInfo::FLOAT, Convert::D24_R32F, 4); break;
    default: f = make(MTLPixelFormatRGBA8Unorm, 4); break;
    }
    return f;
}

static inline uint8_t ex5(uint32_t v) { return (uint8_t)((v << 3) | (v >> 2)); }
static inline uint8_t ex6(uint32_t v) { return (uint8_t)((v << 2) | (v >> 4)); }
static inline uint8_t ex4(uint32_t v) { return (uint8_t)((v << 4) | v); }

void convert_row(Convert c, const uint8_t* src, uint8_t* dst, uint32_t n) {
    switch (c) {
    case Convert::NONE: break;
    case Convert::RGB565:
        for (uint32_t i = 0; i < n; i++) {
            uint16_t v = ((const uint16_t*)src)[i];
            dst[4 * i + 0] = ex5(v & 0x1F);
            dst[4 * i + 1] = ex6((v >> 5) & 0x3F);
            dst[4 * i + 2] = ex5((v >> 11) & 0x1F);
            dst[4 * i + 3] = 255;
        }
        break;
    case Convert::RGBA5551:
        for (uint32_t i = 0; i < n; i++) {
            uint16_t v = ((const uint16_t*)src)[i];
            dst[4 * i + 0] = ex5(v & 0x1F);
            dst[4 * i + 1] = ex5((v >> 5) & 0x1F);
            dst[4 * i + 2] = ex5((v >> 10) & 0x1F);
            dst[4 * i + 3] = (v >> 15) ? 255 : 0;
        }
        break;
    case Convert::ABGR1555:
        for (uint32_t i = 0; i < n; i++) {
            uint16_t v = ((const uint16_t*)src)[i];
            dst[4 * i + 0] = ex5((v >> 11) & 0x1F);
            dst[4 * i + 1] = ex5((v >> 6) & 0x1F);
            dst[4 * i + 2] = ex5((v >> 1) & 0x1F);
            dst[4 * i + 3] = (v & 1) ? 255 : 0;
        }
        break;
    case Convert::RGBA4:
        for (uint32_t i = 0; i < n; i++) {
            uint16_t v = ((const uint16_t*)src)[i];
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
            uint32_t v = ((const uint32_t*)src)[i];
            float d = (float)(v & 0xFFFFFF) / 16777215.0f;
            memcpy(dst + 8 * i, &d, 4);
            dst[8 * i + 4] = (uint8_t)(v >> 24);
            dst[8 * i + 5] = dst[8 * i + 6] = dst[8 * i + 7] = 0;
        }
        break;
    case Convert::D24_R32F:
        for (uint32_t i = 0; i < n; i++) {
            uint32_t v = ((const uint32_t*)src)[i];
            float d = (float)(v & 0xFFFFFF) / 16777215.0f;
            memcpy(dst + 4 * i, &d, 4);
        }
        break;
    case Convert::X24_8_32F:
        memcpy(dst, src, n * 8);
        break;
    }
}

}  // namespace gfx
